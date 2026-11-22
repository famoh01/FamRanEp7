// mobile/net_client/test_combat.cpp
// Host unit tests for world_realtime (JSON -> monsters/players/stats/floating text) and combat_session
// (attack, cooldown, out of range, kill, skill). No server and no Android: a scripted transport stands
// in for ran_mobile/Server. Run: make -C mobile/net_client test
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "api_client.h"
#include "combat_session.h"
#include "world_realtime.h"

using mobile::net::CombatEvent;
using mobile::net::CombatSession;
using mobile::net::MonsterView;
using mobile::net::PlayerStats;
using mobile::net::PlayerView;
using mobile::net::ServerApi;
using mobile::net::WorldView;

static int g_failures = 0;

static void Check(bool ok, const std::string& what) {
    if (ok) {
        std::printf("ok   %s\n", what.c_str());
    } else {
        std::printf("FAIL %s\n", what.c_str());
        ++g_failures;
    }
}

// ------------------------------------------------------------------ sample answers
// Exactly the shape ran_mobile/Server sends (Api._world_view / h_world_sync / h_attack).
static const char* kSyncJson = R"({"connected": true, "map_id": 1, "tick_ms": 1700000000500,
  "position": {"x": 0.5, "y": 0.5},
  "player": {"level": 2, "exp": 30, "exp_required": 200, "hp": 140, "max_hp": 160, "mp": 60,
             "max_mp": 70, "sp": 100, "max_sp": 110, "atk": 36, "def": 18, "money": 1030, "dead": false},
  "players": [{"character_id": 2, "name": "Walker2", "class": "mage", "level": 1, "hp": 120,
               "position": {"x": 0.62, "y": 0.48}, "distance": 0.1217}],
  "monsters": [
    {"monster_id": 3, "name": "Training Dummy", "level": 1, "hp": 44, "max_hp": 80, "dead": false,
     "state": "idle", "target_id": 0, "respawn_in_ms": 0,
     "position": {"x": 0.44, "y": 0.57}, "home": {"x": 0.44, "y": 0.57}, "distance": 0.0894,
     "in_range": true},
    {"monster_id": 1, "name": "Little Vulgarian", "level": 2, "hp": 120, "max_hp": 120, "dead": false,
     "state": "chase", "target_id": 1, "respawn_in_ms": 0,
     "position": {"x": 0.2, "y": 0.7}, "home": {"x": 0.15, "y": 0.75}, "distance": 0.3606,
     "in_range": false}
  ],
  "attack": {"range": 0.12, "cooldown_ms": 600},
  "events": {"moved": 2, "attacked": 0, "killed_players": 0, "monsters": []}})";

static const char* kHitJson = R"({"hit": true, "crit": false, "damage": 23,
  "monster": {"monster_id": 3, "name": "Training Dummy", "hp": 21, "max_hp": 80, "dead": false,
              "position": {"x": 0.44, "y": 0.57}},
  "rewards": null, "quest_progress": [], "retaliation_damage": 0, "drops": [],
  "cooldown_ms": 600, "range": 0.12,
  "player": {"level": 2, "exp": 30, "exp_required": 200, "hp": 140, "max_hp": 160, "mp": 60,
             "max_mp": 70, "sp": 100, "max_sp": 110, "atk": 36, "def": 18, "money": 1030, "dead": false}})";

static const char* kKillJson = R"({"hit": true, "crit": true, "damage": 21,
  "monster": {"monster_id": 3, "name": "Training Dummy", "hp": 0, "max_hp": 80, "dead": true,
              "position": {"x": 0.44, "y": 0.57}},
  "rewards": {"exp": 20, "money": 10, "levels_gained": 0,
              "growth": {"atk_added": 0, "def_added": 0, "hp_added": 0, "mp_added": 0}},
  "quest_progress": [], "retaliation_damage": 0,
  "drops": [{"item_id": 2001, "name": "Vulgarian Fang", "quantity": 1, "stored": true}],
  "player": {"level": 2, "exp": 50, "exp_required": 200, "hp": 140, "max_hp": 160, "mp": 60,
             "max_mp": 70, "sp": 100, "max_sp": 110, "atk": 36, "def": 18, "money": 1040, "dead": false}})";

// ------------------------------------------------------------------ parsing / view tests
static void TestParseWorldSync() {
    PlayerStats stats;
    std::vector<PlayerView> players;
    std::vector<MonsterView> monsters;
    Check(mobile::net::ParseWorldSync(kSyncJson, stats, players, monsters), "parse world sync");
    Check(stats.valid && stats.level == 2 && stats.hp == 140 && stats.maxHp == 160, "player stats parsed");
    Check(stats.exp == 30 && stats.expRequired == 200, "exp and the next level parsed");
    Check(stats.HpRatio() > 0.87f && stats.HpRatio() < 0.88f, "HP ratio for the bar");
    Check(players.size() == 1 && players[0].characterId == 2 && players[0].name == "Walker2",
          "other player parsed");
    Check(players.size() == 1 && players[0].x > 0.61 && players[0].y < 0.49, "other player position");
    Check(monsters.size() == 2, "two monsters parsed");
    const MonsterView& dummy = monsters[0];
    Check(dummy.monsterId == 3 && dummy.hp == 44 && dummy.maxHp == 80 && !dummy.dead, "monster hp");
    Check(dummy.inRange && dummy.x > 0.43 && dummy.y > 0.56, "monster in range and position");
    Check(dummy.state == "idle", "monster AI state");
    Check(monsters[1].state == "chase" && monsters[1].targetId == 1, "chase state and target");
    // An answer without a world is rejected (so the HUD keeps the last good frame).
    PlayerStats s2;
    std::vector<PlayerView> p2;
    std::vector<MonsterView> m2;
    Check(!mobile::net::ParseWorldSync("{\"error\": {\"code\": \"not_in_world\"}}", s2, p2, m2),
          "error answer is not a world sync");
}

static void TestParseHitAndRewards() {
    MonsterView mon;
    int damage = 0, retaliation = 0;
    bool crit = false, missed = false;
    Check(mobile::net::ParseHitResult(kHitJson, mon, damage, crit, missed, retaliation), "parse hit");
    Check(damage == 23 && !crit && !missed && mon.hp == 21 && !mon.dead, "hit numbers");
    int exp = 0, money = 0, levels = 0;
    Check(mobile::net::ParseRewards(kHitJson, exp, money, levels), "rewards field present");
    Check(exp == 0 && money == 0 && levels == 0, "a hit that does not kill gives nothing");
    Check(mobile::net::ParseRewards(kKillJson, exp, money, levels), "kill rewards");
    Check(exp == 20 && money == 10, "kill gives exp and money");
    Check(mobile::net::ParseHitResult(kKillJson, mon, damage, crit, missed, retaliation) && mon.dead,
          "kill marks the monster dead");
    // A miss: hit=false, damage=0.
    const char* missJson = R"({"hit": false, "crit": false, "damage": 0,
        "monster": {"monster_id": 3, "hp": 44, "max_hp": 80, "dead": false},
        "rewards": null, "player": {"level": 1, "hp": 100, "max_hp": 100}})";
    Check(mobile::net::ParseHitResult(missJson, mon, damage, crit, missed, retaliation) && missed,
          "a miss is reported");
    PlayerStats stats;
    Check(mobile::net::ParsePlayerStats(kKillJson, stats) && stats.exp == 50, "stats after the kill");
    Check(mobile::net::ParseSkillCooldownMs(R"({"skill": {"skill_id": 1, "cooldown_ms": 3000}})") == 3000,
          "skill cooldown parsed");
}

static void TestFloatingTexts() {
    WorldView v;
    v.ApplySync(kSyncJson);
    v.AddFloating(0.44, 0.57, 23, 0, true, false);
    auto snap = v.Get();
    Check(snap.texts.size() == 1 && snap.texts[0].value == 23 && snap.texts[0].crit, "damage number");
    v.AgeTexts(0.5f);
    Check(v.Get().texts.size() == 1, "number still visible after 0.5 s");
    v.AgeTexts(1.0f);
    Check(v.Get().texts.empty(), "number fades out");
    // The list is capped so a burst of hits cannot grow it without bound.
    for (int i = 0; i < 100; ++i) v.AddFloating(0.5, 0.5, i, 0);
    Check(v.Get().texts.size() <= 32, "floating numbers are capped");
}

static void TestTargetAndHitUpdates() {
    WorldView v;
    v.ApplySync(kSyncJson);
    Check(v.Get().targetId == 0, "no target at the start");
    v.SetTarget(3);
    double x = 0, y = 0;
    Check(v.Target() == 3 && v.TargetPosition(x, y) && x > 0.43, "target locked at its position");
    v.ApplyHit(kHitJson);
    auto snap = v.Get();
    const MonsterView* dummy = nullptr;
    for (const MonsterView& m : snap.monsters) if (m.monsterId == 3) dummy = &m;
    Check(dummy != nullptr && dummy->hp == 21, "the hit updates the monster HP");
    Check(snap.stats.exp == 30, "stats come from the answer");
    Check(snap.texts.size() == 1 && snap.texts[0].value == 23, "damage number spawned at the monster");
    v.ApplyHit(kKillJson);
    snap = v.Get();
    Check(snap.targetId == 0, "a dead target is released automatically");
    Check(!v.TargetPosition(x, y), "no position for a released target");
}

static void TestPlayerDeathAndClear() {
    WorldView v;
    v.ApplySync(kSyncJson);
    v.SetTarget(1);
    v.ApplyHit(R"({"hit": true, "damage": 5, "monster": {"monster_id": 3, "hp": 40, "max_hp": 80},
        "retaliation_damage": 12, "player": {"level": 1, "hp": 0, "max_hp": 100, "dead": true}})");
    auto snap = v.Get();
    Check(snap.stats.dead && snap.stats.hp == 0, "player death is reported");
    Check(snap.texts.size() == 2, "damage and retaliation numbers");
    v.Clear();
    snap = v.Get();
    Check(!snap.stats.valid && snap.monsters.empty() && snap.players.empty() && snap.targetId == 0,
          "logout clears the world");
}

// ------------------------------------------------------------------ combat session tests
struct ScriptedServer {
    std::vector<std::string> paths;
    std::vector<std::string> bodies;      // answer per call
    std::vector<int> statuses;
    size_t next = 0;

    bool Answer(const std::string& path, const std::string& body, int status,
                mobile::net::HttpResponse& out) {
        paths.push_back(path);
        out.status = status;
        out.body = body;
        return true;
    }

    bool Request(const std::string&, const std::string& path, const std::string&, const std::string&,
                 mobile::net::HttpResponse& out, std::string&) {
        // The world sync always answers the world; combat paths answer what the test queued.
        const bool combat = path.find("/combat/") != std::string::npos;
        if (!combat) return Answer(path, kSyncJson, 200, out);
        if (next < bodies.size()) {
            const std::string body = bodies[next];
            const int status = next < statuses.size() ? statuses[next] : 200;
            ++next;
            return Answer(path, body, status, out);
        }
        return Answer(path, kHitJson, 200, out);
    }
};

static PlayerStats Stats() {
    PlayerStats s;
    s.valid = true;
    s.level = 2;
    s.hp = 140;
    s.maxHp = 160;
    s.mp = 60;
    s.maxMp = 70;
    s.sp = 100;
    s.maxSp = 110;
    s.exp = 30;
    s.expRequired = 200;
    return s;
}

static void TestSessionAutoTargetsAndHits() {
    ScriptedServer server;
    ServerApi api(mobile::net::RequestFn([&server](const std::string& m, const std::string& p,
                                                   const std::string& t, const std::string& b,
                                                   mobile::net::HttpResponse& o, std::string& e) {
        return server.Request(m, p, t, b, o, e);
    }));
    CombatSession session;
    session.Start(1, Stats());
    std::string err;
    session.Tick(api, 1000, err);
    // No manual selection: the nearest monster in range becomes the target.
    Check(session.Target() == 3, "auto-target the monster in range");
    Check(session.Syncs() == 1, "one world sync");
    session.RequestAttack();
    Check(session.AttackPending(), "attack queued");
    session.Tick(api, 1100, err);
    Check(!session.AttackPending(), "attack sent");
    Check(session.Hits() == 1 && session.LastEvent() == CombatEvent::Hit, "hit registered");
    Check(session.TargetHp() == 21, "target HP follows the server");
    // The world is not synced again before the interval passes.
    session.Tick(api, 1200, err);
    Check(session.Syncs() == 1, "sync is rate limited");
    session.Tick(api, 1400, err);
    Check(session.Syncs() == 2, "sync resumes after the interval");
}

static void TestSessionKillRewardsAndRetarget() {
    ScriptedServer server;
    server.bodies.push_back(kKillJson);
    server.bodies.push_back(kSyncJson);   // the next sync says the dummy is still there in tests
    ServerApi api(mobile::net::RequestFn([&server](const std::string& m, const std::string& p,
                                                   const std::string& t, const std::string& b,
                                                   mobile::net::HttpResponse& o, std::string& e) {
        return server.Request(m, p, t, b, o, e);
    }));
    CombatSession session;
    session.Start(1, Stats());
    std::string err;
    session.Tick(api, 1000, err);
    session.SelectTarget(3);
    session.RequestAttack();
    session.Tick(api, 1050, err);
    Check(session.Kills() == 1 && session.LastEvent() == CombatEvent::Killed, "kill reported");
    Check(session.Target() == 0, "dead target released");
    Check(session.TargetHp() == 0, "target HP is zero after the kill");
}

static void TestSessionErrors() {
    ScriptedServer server;
    server.bodies = {R"({"error": {"code": "out_of_range"}})",
                     R"({"error": {"code": "attack_too_fast"}, "retry_after_ms": 400})",
                     R"({"error": {"code": "not_in_world"}})",
                     R"({"error": {"code": "not_enough_mp"}})"};
    server.statuses = {409, 429, 409, 409};
    ServerApi api(mobile::net::RequestFn([&server](const std::string& m, const std::string& p,
                                                   const std::string& t, const std::string& b,
                                                   mobile::net::HttpResponse& o, std::string& e) {
        return server.Request(m, p, t, b, o, e);
    }));
    CombatSession session;
    session.Start(1, Stats());
    std::string err;
    session.Tick(api, 1000, err);
    session.SelectTarget(1);
    session.RequestAttack();
    session.Tick(api, 1010, err);
    Check(session.LastEvent() == CombatEvent::OutOfRange, "out of range");
    session.RequestAttack();
    session.Tick(api, 1300, err);
    Check(session.LastEvent() == CombatEvent::Cooldown, "cooldown");
    // The client stops asking until the server's retry time has passed.
    session.RequestAttack();
    session.Tick(api, 1350, err);
    Check(session.LastEvent() == CombatEvent::Cooldown, "cooldown still honoured");
    Check(server.paths.size() >= 3, "requests reached the transport");
    session.RequestAttack();
    session.Tick(api, 1800, err);
    Check(session.LastEvent() == CombatEvent::NotInWorld, "session lost is reported");
    session.RequestAttack();
    session.Tick(api, 2300, err);
    Check(session.LastEvent() == CombatEvent::NoMana, "not enough MP is reported");
}

static void TestSessionSkill() {
    ScriptedServer server;
    server.bodies.push_back(R"({"hit": true, "damage": 51, "crit": false,
        "skill": {"skill_id": 2, "name": "Fire Bolt", "effect": "fire", "cooldown_ms": 5000},
        "monster": {"monster_id": 3, "hp": 30, "max_hp": 80, "dead": false},
        "rewards": null, "player": {"level": 2, "hp": 140, "max_hp": 160, "mp": 40, "max_mp": 70}})");
    ServerApi api(mobile::net::RequestFn([&server](const std::string& m, const std::string& p,
                                                   const std::string& t, const std::string& b,
                                                   mobile::net::HttpResponse& o, std::string& e) {
        return server.Request(m, p, t, b, o, e);
    }));
    CombatSession session;
    session.Start(1, Stats());
    std::string err;
    session.Tick(api, 1000, err);
    session.SelectTarget(3);
    session.RequestSkill(2);
    session.RequestSkill(2);          // a double tap only queues one extra cast
    session.Tick(api, 1010, err);
    Check(session.LastEvent() == CombatEvent::Hit, "skill hit");
    Check(session.TargetHp() == 30, "skill damage applied to the monster");
    // A skill packet goes to /combat/skill, not /combat/attack.
    bool sawSkillPath = false;
    for (const std::string& p : server.paths)
        if (p.find("/combat/skill") != std::string::npos) sawSkillPath = true;
    Check(sawSkillPath, "skill uses the skill endpoint");
}

static void TestStoppedSessionDoesNothing() {
    ScriptedServer server;
    ServerApi api(mobile::net::RequestFn([&server](const std::string& m, const std::string& p,
                                                   const std::string& t, const std::string& b,
                                                   mobile::net::HttpResponse& o, std::string& e) {
        return server.Request(m, p, t, b, o, e);
    }));
    CombatSession session;
    std::string err;
    Check(!session.Tick(api, 1000, err), "an inactive session does not tick");
    session.Start(1, Stats());
    session.RequestAttack();
    session.Stop();
    Check(!session.Active() && !session.Tick(api, 2000, err), "stop ends the loop");
    Check(server.paths.empty(), "nothing was sent after stop");
}

int main() {
    TestParseWorldSync();
    TestParseHitAndRewards();
    TestFloatingTexts();
    TestTargetAndHitUpdates();
    TestPlayerDeathAndClear();
    TestSessionAutoTargetsAndHits();
    TestSessionKillRewardsAndRetarget();
    TestSessionErrors();
    TestSessionSkill();
    TestStoppedSessionDoesNothing();
    if (g_failures == 0) {
        std::printf("\nall combat checks passed\n");
        return 0;
    }
    std::printf("\n%d combat check(s) failed\n", g_failures);
    return 1;
}
