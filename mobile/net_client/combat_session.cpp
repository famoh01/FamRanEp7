// mobile/net_client/combat_session.cpp
#include "combat_session.h"

#include <algorithm>

namespace mobile::net {

const char* CombatEventName(CombatEvent event) {
    switch (event) {
        case CombatEvent::None:        return "none";
        case CombatEvent::Hit:         return "hit";
        case CombatEvent::Miss:        return "miss";
        case CombatEvent::Killed:      return "killed";
        case CombatEvent::Cooldown:    return "cooldown";
        case CombatEvent::OutOfRange:  return "out_of_range";
        case CombatEvent::NoTarget:    return "no_target";
        case CombatEvent::Dead:        return "dead";
        case CombatEvent::NoMana:      return "no_mana";
        case CombatEvent::NotInWorld:  return "not_in_world";
        case CombatEvent::Failed:      return "failed";
    }
    return "none";
}

void CombatSession::Start(long long characterId, const PlayerStats& stats) {
    std::lock_guard<std::mutex> lock(mu_);
    skillCooldownUntilMs_.clear();
    characterId_ = characterId;
    active_ = true;
    stats_ = stats;
    attackPending_ = false;
    pendingSkills_.clear();
    lastSyncMs_ = -1;
    lastAttackMs_ = -1;
    cooldownUntilMs_ = 0;
    lastEvent_ = CombatEvent::None;
    hits_ = kills_ = syncs_ = 0;
    targetHp_ = targetMaxHp_ = -1;
    View().SetStats(stats);
}

void CombatSession::Stop() {
    std::lock_guard<std::mutex> lock(mu_);
    active_ = false;
    characterId_ = 0;
    attackPending_ = false;
    pendingSkills_.clear();
    targetId_ = 0;
    targetHp_ = targetMaxHp_ = -1;
    View().Clear();
}

bool CombatSession::Active() const {
    std::lock_guard<std::mutex> lock(mu_);
    return active_;
}

void CombatSession::SelectTarget(long long monsterId) {
    std::lock_guard<std::mutex> lock(mu_);
    targetId_ = monsterId;
    targetHp_ = targetMaxHp_ = -1;
    View().SetTarget(monsterId);
}

long long CombatSession::Target() const {
    std::lock_guard<std::mutex> lock(mu_);
    return targetId_;
}

void CombatSession::RequestAttack() {
    std::lock_guard<std::mutex> lock(mu_);
    if (!active_) return;
    attackPending_ = true;
}

void CombatSession::RequestSkill(int skillId) {
    if (skillId <= 0) return;
    std::lock_guard<std::mutex> lock(mu_);
    if (!active_) return;
    pendingSkills_.push_back(skillId);
    if (pendingSkills_.size() > 4) pendingSkills_.erase(pendingSkills_.begin());
}

bool CombatSession::AttackPending() const {
    std::lock_guard<std::mutex> lock(mu_);
    return attackPending_;
}

CombatEvent CombatSession::LastEvent() const {
    std::lock_guard<std::mutex> lock(mu_);
    return lastEvent_;
}

int CombatSession::Hits() const {
    std::lock_guard<std::mutex> lock(mu_);
    return hits_;
}

int CombatSession::Kills() const {
    std::lock_guard<std::mutex> lock(mu_);
    return kills_;
}

int CombatSession::Syncs() const {
    std::lock_guard<std::mutex> lock(mu_);
    return syncs_;
}

int CombatSession::TargetHp() const {
    std::lock_guard<std::mutex> lock(mu_);
    return targetHp_;
}

int CombatSession::TargetMaxHp() const {
    std::lock_guard<std::mutex> lock(mu_);
    return targetMaxHp_;
}

float CombatSession::SkillCooldownLeft(long long nowMs, int skillId) const {
    std::lock_guard<std::mutex> lock(mu_);
    const auto it = skillCooldownUntilMs_.find(skillId);
    if (it == skillCooldownUntilMs_.end()) return 0.f;
    if (nowMs >= it->second) return 0.f;
    return float(it->second - nowMs) / 1000.f;
}

bool CombatSession::Tick(ServerApi& api, int64_t nowMs, std::string& err) {
    long long cid = 0;
    bool attack = false;
    int skill = 0;
    long long target = 0;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!active_) return false;
        cid = characterId_;
        attack = attackPending_;
        if (!pendingSkills_.empty()) skill = pendingSkills_.front();
        target = targetId_;
    }

    // The sync also runs the server's monster AI: other players and monsters move because of it.
    if (lastSyncMs_ < 0 || nowMs - lastSyncMs_ >= kWorldSyncIntervalMs) {
        std::string json;
        std::string syncErr;
        if (api.GetWorldSync(cid, json, syncErr)) {
            std::lock_guard<std::mutex> lock(mu_);
            lastSyncMs_ = nowMs;
            ++syncs_;
            View().ApplySync(json);
            if (target == 0) {
                // No manual selection: keep the nearest monster in range as the target.
                auto snap = View().Get();
                const MonsterView* best = nullptr;
                for (const MonsterView& m : snap.monsters) {
                    if (m.dead || !m.inRange) continue;
                    if (best == nullptr || m.distance < best->distance) best = &m;
                }
                if (best != nullptr) {
                    target = best->monsterId;
                    targetId_ = target;
                    View().SetTarget(target);
                }
            }
            const auto snap = View().Get();
            for (const MonsterView& m : snap.monsters) {
                if (m.monsterId == target) {
                    targetHp_ = m.hp;
                    targetMaxHp_ = m.maxHp;
                }
            }
        } else {
            std::lock_guard<std::mutex> lock(mu_);
            lastSyncMs_ = nowMs;   // do not retry on every tick
            err = syncErr;
        }
    }

    if (skill != 0) {
        SendSkill(api, nowMs, err);
        return true;
    }
    if (attack) SendAttack(api, nowMs, err);
    return true;
}

void CombatSession::SendAttack(ServerApi& api, int64_t nowMs, std::string& err) {
    long long cid = 0, target = 0;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!attackPending_) return;
        if (nowMs < cooldownUntilMs_) {
            lastEvent_ = CombatEvent::Cooldown;
            return;
        }
        if (lastAttackMs_ >= 0 && nowMs - lastAttackMs_ < kClientAttackGapMs) return;
        if (targetId_ == 0) {
            attackPending_ = false;
            lastEvent_ = CombatEvent::NoTarget;
            return;
        }
        attackPending_ = false;
        lastAttackMs_ = nowMs;
        cid = characterId_;
        target = targetId_;
    }
    const ServerApi::CombatAnswer answer = api.Attack(cid, target);
    ApplyAnswer(answer, false);
    if (answer.result != ServerApi::CombatResult::Ok) err = answer.code;
}

void CombatSession::SendSkill(ServerApi& api, int64_t nowMs, std::string& err) {
    long long cid = 0, target = 0;
    int skillId = 0;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (pendingSkills_.empty()) return;
        skillId = pendingSkills_.front();
        pendingSkills_.erase(pendingSkills_.begin());
        if (targetId_ == 0) {
            lastEvent_ = CombatEvent::NoTarget;
            return;
        }
        cid = characterId_;
        target = targetId_;
        lastAttackMs_ = nowMs;
    }
    const ServerApi::CombatAnswer answer = api.UseSkill(cid, skillId, target);
    if (answer.result == ServerApi::CombatResult::Ok) {
        const int cooldownMs = ParseSkillCooldownMs(answer.body);
        if (cooldownMs > 0) {
            std::lock_guard<std::mutex> lock(mu_);
            skillCooldownUntilMs_[skillId] = nowMs + cooldownMs;
        }
    }
    ApplyAnswer(answer, true);
    if (answer.result != ServerApi::CombatResult::Ok) err = answer.code;
}

void CombatSession::ApplyAnswer(const ServerApi::CombatAnswer& answer, bool isSkill) {
    std::lock_guard<std::mutex> lock(mu_);
    const int skillCooldownMs = isSkill ? ParseSkillCooldownMs(answer.body) : 0;
    (void)skillCooldownMs;
    switch (answer.result) {
        case ServerApi::CombatResult::Ok: {
            MonsterView mon;
            int damage = 0, retaliation = 0;
            bool crit = false, missed = false;
            const bool hasMonster = ParseHitResult(answer.body, mon, damage, crit, missed, retaliation);
            PlayerStats stats;
            const bool hasStats = ParsePlayerStats(answer.body, stats);
            int exp = 0, money = 0, levels = 0;
            ParseRewards(answer.body, exp, money, levels);
            View().ApplyHit(answer.body);
            if (hasStats && stats.valid) stats_ = stats;
            if (hasMonster) {
                targetHp_ = mon.hp;
                targetMaxHp_ = mon.maxHp;
                if (mon.dead) {
                    ++kills_;
                    lastEvent_ = CombatEvent::Killed;
                    if (targetId_ == mon.monsterId) {
                        targetId_ = 0;   // the target died: the player picks the next one
                        View().SetTarget(0);
                    }
                } else {
                    ++hits_;
                    lastEvent_ = missed ? CombatEvent::Miss : CombatEvent::Hit;
                }
            } else {
                lastEvent_ = CombatEvent::Hit;
            }
            break;
        }
        case ServerApi::CombatResult::OutOfRange:
            lastEvent_ = CombatEvent::OutOfRange;
            break;
        case ServerApi::CombatResult::Cooldown: {
            // The server knows the real cooldown; remember it so the client stops asking early.
            if (answer.retryAfterMs > 0) {
                // nowMs is not passed here: the next Tick compares against this relative wait.
                cooldownUntilMs_ = lastAttackMs_ < 0 ? 0 : lastAttackMs_ + answer.retryAfterMs;
            }
            lastEvent_ = CombatEvent::Cooldown;
            break;
        }
        case ServerApi::CombatResult::NotInWorld:
            lastEvent_ = CombatEvent::NotInWorld;
            break;
        case ServerApi::CombatResult::Dead:
            lastEvent_ = CombatEvent::Dead;
            break;
        case ServerApi::CombatResult::NoMana:
            lastEvent_ = CombatEvent::NoMana;
            break;
        case ServerApi::CombatResult::Unauthorized:
        case ServerApi::CombatResult::Failed:
            lastEvent_ = CombatEvent::Failed;
            break;
    }
}

}  // namespace mobile::net
