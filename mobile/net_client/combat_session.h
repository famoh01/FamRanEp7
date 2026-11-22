// mobile/net_client/combat_session.h
// The playable loop of one character, independent of Android and sockets so it is host-tested:
//
//   Tick()      syncs the world (~250 ms): other players, monsters with their AI state, the player's
//               stats, and the attack result the render thread asked for
//   Attack()    render thread asks for one basic attack on a monster
//   Skill()     render thread asks to cast a skill on a monster
//   Results     the net thread reports what the server answered (cooldown, out of range, death)
//
// The server stays authoritative: the client only decides when to ask. Damage, HP, EXP, drops and the
// cooldown itself are all judged by ran_mobile/Server.
#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "api_client.h"
#include "world_realtime.h"

namespace mobile::net {

// How often the world is synced while playing (ms). The server advances the monster AI on every
// world request, so this also drives the monsters' movement.
constexpr int kWorldSyncIntervalMs = 250;
// An attack request cannot be sent more often than this (the server's own cooldown is 600 ms; this
// keeps a held attack button from flooding the server between ticks).
constexpr int kClientAttackGapMs = 220;

// What happened on the last action, so the platform layer can log it (the CI smoke test greps
// "combat ..." lines) without the logic depending on any logging library.
enum class CombatEvent {
    None,
    Hit,            // a hit landed
    Miss,           // a hit was dodged
    Killed,         // the monster died (rewards and drops are in the answer)
    Cooldown,       // the server said the attack/skill is still cooling down
    OutOfRange,     // the target is too far: the player must walk closer
    NoTarget,       // nothing selected
    Dead,           // the player or the monster is dead
    NoMana,         // not enough MP/SP for the skill
    NotInWorld,     // the world session ended: the sync flow will resume it
    Failed,         // transport or unexpected error
};

const char* CombatEventName(CombatEvent event);

class CombatSession {
public:
    // Net thread. After Enter World / a resume: the character and the server's stats for it.
    void Start(long long characterId, const PlayerStats& stats);
    // Net thread. After leave world or logout.
    void Stop();
    bool Active() const;

    // Render thread. The HUD tap: which monster the attack button and skills apply to (0 = none).
    void SelectTarget(long long monsterId);
    long long Target() const;
    // Render thread. The attack button (and its hold repeat) asks for one attack.
    void RequestAttack();
    // Render thread. A skill slot was tapped.
    void RequestSkill(int skillId);
    // Render thread. True while an attack is waiting to be sent (the button shows it).
    bool AttackPending() const;

    // Net thread, called from the sync tick: syncs the world when due and sends pending actions.
    // Returns false when the player is not in the world any more.
    bool Tick(ServerApi& api, int64_t nowMs, std::string& err);

    // What the last action did, and the number of hits/kills (CI smoke-test signals).
    CombatEvent LastEvent() const;
    int Hits() const;
    int Kills() const;
    int Syncs() const;
    // Target's HP as the server last reported it (for the CI check "the monster's HP dropped").
    int TargetHp() const;
    int TargetMaxHp() const;
    // Render thread. Seconds left on a skill's cooldown (0 = ready). The server's answer for that skill
    // starts the timer, so the HUD shows the real cooldown, not a client guess.
    float SkillCooldownLeft(long long nowMs, int skillId) const;

private:
    void SendAttack(ServerApi& api, int64_t nowMs, std::string& err);
    void SendSkill(ServerApi& api, int64_t nowMs, std::string& err);
    void ApplyAnswer(const ServerApi::CombatAnswer& answer, bool isSkill);

    mutable std::mutex mu_;
    long long characterId_ = 0;
    bool active_ = false;
    // Requests from the render thread.
    long long targetId_ = 0;
    bool attackPending_ = false;
    std::vector<int> pendingSkills_;
    // Timing on the net thread.
    int64_t lastSyncMs_ = -1;
    int64_t lastAttackMs_ = -1;
    int64_t cooldownUntilMs_ = 0;     // the server said "wait": do not ask again before this
    CombatEvent lastEvent_ = CombatEvent::None;
    int hits_ = 0, kills_ = 0, syncs_ = 0;
    int targetHp_ = -1, targetMaxHp_ = -1;
    PlayerStats stats_;
    std::map<int, int64_t> skillCooldownUntilMs_;   // skill id -> wall time the cooldown ends
};

}  // namespace mobile::net
