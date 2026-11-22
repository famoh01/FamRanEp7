// mobile/net_client/world_realtime.h
// Shared, thread-safe view of one RAN MOBILE world: the player's stats, the other players, the
// monsters (with their AI state) and the floating damage numbers the HUD draws.
//
// No Android/JNI/sockets here: the JSON that comes from the server is parsed into plain structs, so
// the whole module is unit-tested on the host (make -C mobile/net_client test).
//
// Who calls what:
//   net thread    ApplySync / ApplyHit / ApplySkill / ApplyHeal / SetStats  (server answers)
//   render thread Snapshot()                                              (one copy per frame)
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "api_client.h"

namespace mobile::net {

// One monster as the server reports it (see Api._monster_view on the server).
struct MonsterView {
    long long monsterId = 0;
    std::string name;
    int level = 1;
    int hp = 0, maxHp = 0;
    bool dead = false;
    std::string state = "idle";   // idle | patrol | chase | attack | return | dead
    long long targetId = 0;       // the player this monster is chasing/attacking
    double x = 0.5, y = 0.5;      // position on the map (0..1)
    double homeX = 0.5, homeY = 0.5;
    double distance = 0.0;
    bool inRange = false;         // within the player's basic attack range
};

// Another player on the same map, within the server's view range.
struct PlayerView {
    long long characterId = 0;
    std::string name;
    std::string cls;
    int level = 1;
    int hp = 0, maxHp = 0;
    double x = 0.5, y = 0.5;
    double distance = 0.0;
};

// The player's own numbers, for the HUD bars and the character window.
struct PlayerStats {
    bool valid = false;
    int level = 1;
    int hp = 0, maxHp = 0;
    int mp = 0, maxMp = 0;
    int sp = 0, maxSp = 0;
    int exp = 0, expRequired = 100;
    int atk = 0, def = 0;
    int money = 0;
    bool dead = false;

    float HpRatio() const { return maxHp > 0 ? float(hp) / float(maxHp) : 0.f; }
    float MpRatio() const { return maxMp > 0 ? float(mp) / float(maxMp) : 0.f; }
    float ExpRatio() const { return expRequired > 0 ? float(exp) / float(expRequired) : 0.f; }
};

// A number that floats up from a target and fades (damage, miss, EXP, drop).
struct FloatingText {
    double x = 0.0, y = 0.0;   // map fractions where it starts
    float ageSec = 0.f;        // seconds since it was created
    float lifeSec = 1.1f;
    int value = 0;             // the number to draw
    int kind = 0;              // 0 = damage to a monster, 1 = damage to the player, 2 = EXP, 3 = item
    bool crit = false;
    bool miss = false;
    bool onPlayer = false;     // drawn above the player instead of a target
};

// Returns false for "not JSON at all" but true for a well-formed answer with nothing in it.
bool ParsePlayerStats(const std::string& json, PlayerStats& out);
bool ParseWorldSync(const std::string& json, PlayerStats& stats, std::vector<PlayerView>& players,
                    std::vector<MonsterView>& monsters);
// The monster object of an attack/skill answer ("monster": {...}).
bool ParseHitResult(const std::string& json, MonsterView& monster, int& damage, bool& crit, bool& missed,
                    int& monsterCount);
// EXP/money/heal numbers of an attack answer ("rewards": {...}, "player": {...}).
bool ParseRewards(const std::string& json, int& exp, int& money, int& levelsGained);
// "player": {...} of any answer (stats after the hit).
bool ParsePlayerObject(const std::string& json, PlayerStats& out);
// Cooldown in ms of a skill answer ("skill": {"cooldown_ms": 3000}).
int ParseSkillCooldownMs(const std::string& json);

class WorldView {
public:
    // Net thread. The full /world/sync answer replaces the players and monsters.
    void ApplySync(const std::string& json);
    // Net thread. One attack or skill answer updates its monster and the player's stats.
    void ApplyHit(const std::string& json);
    // Net thread. The player's stats alone (enter world, world state, use item).
    void SetStats(const PlayerStats& stats);
    // Net thread. The player used a potion (the server sent the new stats).
    void ApplyHeal(const std::string& json);

    // Render thread. A consistent copy for this frame.
    struct Snapshot {
        PlayerStats stats;
        std::vector<PlayerView> players;
        std::vector<MonsterView> monsters;
        std::vector<FloatingText> texts;
        long long targetId = 0;       // the locked monster (0 = none)
        bool inWorld = false;
    };
    Snapshot Get() const;

    // Either thread (the render thread taps a monster; the net thread sends the attack).
    void SetTarget(long long monsterId);
    long long Target() const;
    // Render thread: the target is gone (died, left view) so the HUD stops highlighting it.
    void DropTargetIfMissing();
    // Render thread: advance the floating numbers and fade them out.
    void AgeTexts(float dtSec);
    // Render thread: spawn a number at a map position (the server's answer is drawn there).
    void AddFloating(double x, double y, int value, int kind, bool crit = false, bool miss = false);
    // Render thread: where the player is drawn (so "you were hit" numbers appear above the player).
    void SetPlayerPosition(double x, double y);
    // Net thread / logout.
    void Clear();

    // Map position of a monster the player has locked (false when there is none).
    bool TargetPosition(double& x, double& y) const;

private:
    // Requires mu_ (the caller already holds it).
    void AddFloatingLocked(double x, double y, int value, int kind, bool crit, bool miss,
                           bool onPlayer = false);

    mutable std::mutex mu_;
    PlayerStats stats_;
    std::vector<PlayerView> players_;
    std::vector<MonsterView> monsters_;
    std::vector<FloatingText> texts_;
    long long targetId_ = 0;
    bool inWorld_ = false;
    double playerX_ = 0.5, playerY_ = 0.5;   // last position the render thread published
};

// The single world view of the app (the render thread and the net thread share it).
WorldView& View();

}  // namespace mobile::net
