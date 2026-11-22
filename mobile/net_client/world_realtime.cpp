// mobile/net_client/world_realtime.cpp
#include "world_realtime.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>

namespace mobile::net {
namespace {

constexpr float kTextLifeSec = 1.1f;
constexpr size_t kMaxFloatingTexts = 32;

// ---------------------------------------------------------------- tiny JSON scanner
// The server's answers are plain JSON with no escaped quotes in the strings we read (names, codes,
// keys). One nesting level of arrays/objects is all we need, so a small scanner is enough and it keeps
// the Android build free of a JSON library.
struct Scanner {
    const std::string& s;
    explicit Scanner(const std::string& text) : s(text) {}

    void Ws(size_t& i) const {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    }

    // Index of the value that follows "key": ... or npos.
    size_t Value(const std::string& key, size_t from = 0) const {
        const std::string needle = "\"" + key + "\"";
        size_t p = s.find(needle, from);
        if (p == std::string::npos) return std::string::npos;
        p = s.find(':', p + needle.size());
        if (p == std::string::npos) return std::string::npos;
        ++p;
        Ws(p);
        return p;
    }

    // One value of "key" that appears inside jsonFrom..jsonTo.
    bool Str(const std::string& key, std::string& out, size_t from = 0) const {
        size_t p = Value(key, from);
        if (p == std::string::npos || s[p] != '"') return false;
        const size_t e = s.find('"', p + 1);
        if (e == std::string::npos) return false;
        out = s.substr(p + 1, e - p - 1);
        return true;
    }

    bool Num(const std::string& key, double& out, size_t from = 0) const {
        size_t p = Value(key, from);
        if (p == std::string::npos) return false;
        const char* start = s.c_str() + p;
        char* end = nullptr;
        const double v = std::strtod(start, &end);
        if (end == start) return false;
        out = v;
        return true;
    }

    bool Int(const std::string& key, long long& out, size_t from = 0) const {
        size_t p = Value(key, from);
        if (p == std::string::npos) return false;
        const char* start = s.c_str() + p;
        char* end = nullptr;
        const long long v = std::strtoll(start, &end, 10);
        if (end == start) return false;
        out = v;
        return true;
    }

    bool Bool(const std::string& key, bool& out, size_t from = 0) const {
        size_t p = Value(key, from);
        if (p == std::string::npos) return false;
        if (s.compare(p, 4, "true") == 0) { out = true; return true; }
        if (s.compare(p, 5, "false") == 0) { out = false; return true; }
        return false;
    }

    // Iterator over the elements of the array whose '[' is at `pos`.
    // `Visit` receives the [begin, end) of each element and its index.
    template <typename Visit>
    bool ArrayAt(size_t pos, Visit visit) const {
        if (pos == std::string::npos || pos >= s.size() || s[pos] != '[') return false;
        size_t i = pos + 1;
        int index = 0;
        while (true) {
            Ws(i);
            if (i >= s.size()) return false;
            if (s[i] == ']') return true;
            const size_t begin = i;
            const size_t end = ElementEnd(i);
            if (end == std::string::npos) return false;
            visit(begin, end, index++);
            i = end;
            Ws(i);
            if (i < s.size() && s[i] == ',') { ++i; continue; }
            return i < s.size() && s[i] == ']';
        }
    }

    // End index (exclusive) of the value starting at `i`: string, object, array or scalar.
    size_t ElementEnd(size_t i) const {
        Ws(i);
        if (i >= s.size()) return std::string::npos;
        if (s[i] == '"') {
            const size_t e = s.find('"', i + 1);
            return e == std::string::npos ? std::string::npos : e + 1;
        }
        if (s[i] == '{' || s[i] == '[') {
            const char open = s[i];
            const char close = (open == '{') ? '}' : ']';
            int depth = 0;
            bool inString = false;
            for (size_t p = i; p < s.size(); ++p) {
                const char c = s[p];
                if (inString) {
                    if (c == '\\') ++p;
                    else if (c == '"') inString = false;
                    continue;
                }
                if (c == '"') inString = true;
                else if (c == open) ++depth;
                else if (c == close) {
                    --depth;
                    if (depth == 0) return p + 1;
                }
            }
            return std::string::npos;
        }
        size_t p = i;
        while (p < s.size() && s[p] != ',' && s[p] != ']' && s[p] != '}') ++p;
        return p;
    }

    void ForEachInArray(const std::string& key, const std::function<void(size_t, size_t)>& visit,
                        size_t from = 0) const {
        size_t p = Value(key, from);
        if (p == std::string::npos) return;
        ArrayAt(p, [&](size_t b, size_t e, int) { visit(b, e); });
    }
};

double Clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

PlayerStats ParseStatsObject(const Scanner& sc, size_t from) {
    PlayerStats s;
    double v = 0;
    long long i = 0;
    bool b = false;
    if (sc.Num("level", v, from)) { s.level = int(v); s.valid = true; }
    if (sc.Num("hp", v, from)) s.hp = int(v);
    if (sc.Num("max_hp", v, from)) s.maxHp = int(v);
    if (sc.Num("mp", v, from)) s.mp = int(v);
    if (sc.Num("max_mp", v, from)) s.maxMp = int(v);
    if (sc.Num("sp", v, from)) s.sp = int(v);
    if (sc.Num("max_sp", v, from)) s.maxSp = int(v);
    if (sc.Num("exp", v, from)) s.exp = int(v);
    if (sc.Num("exp_required", v, from)) s.expRequired = int(v);
    if (sc.Num("atk", v, from)) s.atk = int(v);
    if (sc.Num("def", v, from)) s.def = int(v);
    if (sc.Num("money", v, from)) s.money = int(v);
    if (sc.Bool("dead", b, from)) s.dead = b;
    (void)i;
    return s;
}

MonsterView ParseMonsterObject(const Scanner& sc, size_t from) {
    MonsterView m;
    double v = 0;
    bool b = false;
    long long i = 0;
    if (sc.Int("monster_id", i, from)) m.monsterId = i;
    sc.Str("name", m.name, from);
    if (sc.Num("level", v, from)) m.level = int(v);
    if (sc.Num("hp", v, from)) m.hp = int(v);
    if (sc.Num("max_hp", v, from)) m.maxHp = int(v);
    if (sc.Bool("dead", b, from)) m.dead = b;
    sc.Str("state", m.state, from);
    if (sc.Int("target_id", i, from)) m.targetId = i;
    // "position": {"x": .., "y": ..}
    const size_t pos = sc.Value("position", from);
    if (pos != std::string::npos) {
        if (sc.Num("x", v, pos)) m.x = Clamp01(v);
        if (sc.Num("y", v, pos)) m.y = Clamp01(v);
    }
    const size_t home = sc.Value("home", from);
    if (home != std::string::npos) {
        if (sc.Num("x", v, home)) m.homeX = Clamp01(v);
        if (sc.Num("y", v, home)) m.homeY = Clamp01(v);
    }
    if (sc.Num("distance", v, from)) m.distance = v;
    if (sc.Bool("in_range", b, from)) m.inRange = b;
    return m;
}

}  // namespace

// ---------------------------------------------------------------- parsing

bool ParsePlayerStats(const std::string& json, PlayerStats& out) {
    Scanner sc(json);
    const size_t p = sc.Value("player");
    if (p == std::string::npos) return false;
    out = ParseStatsObject(sc, p);
    return out.valid;
}

bool ParsePlayerObject(const std::string& json, PlayerStats& out) {
    return ParsePlayerStats(json, out);
}

bool ParseWorldSync(const std::string& json, PlayerStats& stats, std::vector<PlayerView>& players,
                    std::vector<MonsterView>& monsters) {
    Scanner sc(json);
    const size_t root = sc.Value("connected");
    if (root == std::string::npos) return false;   // not a world answer at all
    bool connected = false;
    sc.Bool("connected", connected, root);
    // The stats object belongs to this answer: reset when it is missing so stale numbers are not drawn.
    stats = PlayerStats{};
    const size_t p = sc.Value("player");
    if (p != std::string::npos) stats = ParseStatsObject(sc, p);

    players.clear();
    sc.ForEachInArray("players", [&](size_t b, size_t e) {
        const std::string obj = json.substr(b, e - b);
        Scanner item(obj);
        PlayerView pv;
        long long i = 0;
        double v = 0;
        if (item.Int("character_id", i)) pv.characterId = i;
        item.Str("name", pv.name);
        item.Str("class", pv.cls);
        if (item.Num("level", v)) pv.level = int(v);
        if (item.Num("hp", v)) pv.hp = int(v);
        const size_t at = item.Value("position");
        if (at != std::string::npos) {
            if (item.Num("x", v, at)) pv.x = Clamp01(v);
            if (item.Num("y", v, at)) pv.y = Clamp01(v);
        }
        if (item.Num("distance", v)) pv.distance = v;
        if (pv.characterId > 0) players.push_back(pv);
    }, root);

    monsters.clear();
    sc.ForEachInArray("monsters", [&](size_t b, size_t e) {
        const std::string obj = json.substr(b, e - b);
        Scanner item(obj);
        MonsterView mv = ParseMonsterObject(item, 0);
        if (mv.maxHp <= 0) mv.maxHp = 1;
        if (mv.monsterId > 0) monsters.push_back(mv);
    }, root);
    return true;
}

bool ParseHitResult(const std::string& json, MonsterView& monster, int& damage, bool& crit, bool& missed,
                    int& retaliation) {
    Scanner sc(json);
    const size_t m = sc.Value("monster");
    if (m == std::string::npos) return false;
    monster = ParseMonsterObject(sc, m);
    if (monster.maxHp <= 0) monster.maxHp = 1;
    double d = 0;
    damage = 0;
    if (sc.Num("damage", d)) damage = int(d);
    bool b = false;
    crit = sc.Bool("crit", b) && b;
    bool hit = true;
    if (sc.Bool("hit", hit)) missed = !hit;
    else missed = damage <= 0;
    long long r = 0;
    retaliation = sc.Int("retaliation_damage", r) ? int(r) : 0;
    return true;
}

bool ParseRewards(const std::string& json, int& exp, int& money, int& levelsGained) {
    Scanner sc(json);
    const size_t r = sc.Value("rewards");
    exp = money = levelsGained = 0;
    if (r == std::string::npos) return false;
    // "rewards": null  -> the hit did not kill, so nothing was gained
    if (sc.s.compare(r, 4, "null") == 0) return true;
    double v = 0;
    if (sc.Num("exp", v, r)) exp = int(v);
    if (sc.Num("money", v, r)) money = int(v);
    if (sc.Num("levels_gained", v, r)) levelsGained = int(v);
    return true;
}

int ParseSkillCooldownMs(const std::string& json) {
    Scanner sc(json);
    const size_t s = sc.Value("skill");
    if (s == std::string::npos) return 0;
    double v = 0;
    return sc.Num("cooldown_ms", v, s) ? int(v) : 0;
}

// ---------------------------------------------------------------- WorldView

void WorldView::ApplySync(const std::string& json) {
    PlayerStats stats;
    std::vector<PlayerView> players;
    std::vector<MonsterView> monsters;
    if (!ParseWorldSync(json, stats, players, monsters)) return;
    bool connected = false;
    Scanner sc(json);
    sc.Bool("connected", connected);
    std::lock_guard<std::mutex> lock(mu_);
    inWorld_ = connected;
    if (stats.valid) stats_ = stats;
    players_ = std::move(players);
    monsters_ = std::move(monsters);
    if (targetId_ != 0 && std::none_of(monsters_.begin(), monsters_.end(),
                                      [this](const MonsterView& m) { return m.monsterId == targetId_; }))
        targetId_ = 0;
}

void WorldView::ApplyHit(const std::string& json) {
    MonsterView mon;
    int damage = 0, retaliation = 0;
    bool crit = false, missed = false;
    const bool hasMonster = ParseHitResult(json, mon, damage, crit, missed, retaliation);
    PlayerStats stats;
    const bool hasStats = ParsePlayerStats(json, stats);
    int exp = 0, money = 0, levels = 0;
    const bool hasRewards = ParseRewards(json, exp, money, levels);
    std::lock_guard<std::mutex> lock(mu_);
    if (hasMonster) {
        auto it = std::find_if(monsters_.begin(), monsters_.end(),
                               [&](const MonsterView& m) { return m.monsterId == mon.monsterId; });
        const bool known = it != monsters_.end();
        const double x = known ? it->x : mon.x;
        const double y = known ? it->y : mon.y;
        if (known) {
            const double keepDistance = it->distance;
            const bool keepRange = it->inRange;
            const double keepHomeX = it->homeX, keepHomeY = it->homeY;
            *it = mon;
            if (it->x == 0.5 && it->y == 0.5) { it->x = x; it->y = y; }   // answer carries no position
            it->distance = keepDistance;
            it->inRange = keepRange;
            it->homeX = keepHomeX;
            it->homeY = keepHomeY;
        } else {
            monsters_.push_back(mon);
        }
        if (mon.dead && targetId_ == mon.monsterId) targetId_ = 0;
        if (!missed) {
            AddFloatingLocked(x, y, damage, 0, crit, false);
        } else {
            AddFloatingLocked(x, y, 0, 0, false, true);
        }
        if (exp > 0) AddFloatingLocked(x, y - 0.04, exp, 2, false, false);
        if (money > 0 && exp > 0) AddFloatingLocked(x, y - 0.07, money, 3, false, false);
    }
    if (retaliation > 0) {
        // มอนสเตอร์ตีสวน: ตัวเลขสีแดงลอยเหนือจุดที่ผู้เล่นยืนอยู่
        AddFloatingLocked(0.5, 0.5, retaliation, 1, false, false, true);
    }
    if (hasStats && stats.valid) stats_ = stats;
    if (hasRewards && stats.valid) stats_.valid = true;
}

void WorldView::SetStats(const PlayerStats& stats) {
    if (!stats.valid) return;
    std::lock_guard<std::mutex> lock(mu_);
    stats_ = stats;
}

void WorldView::ApplyHeal(const std::string& json) {
    PlayerStats stats;
    if (!ParsePlayerStats(json, stats) || !stats.valid) return;
    std::lock_guard<std::mutex> lock(mu_);
    stats_ = stats;
}

WorldView::Snapshot WorldView::Get() const {
    std::lock_guard<std::mutex> lock(mu_);
    Snapshot out;
    out.stats = stats_;
    out.players = players_;
    out.monsters = monsters_;
    out.texts = texts_;
    out.targetId = targetId_;
    out.inWorld = inWorld_;
    return out;
}

void WorldView::SetTarget(long long monsterId) {
    std::lock_guard<std::mutex> lock(mu_);
    targetId_ = monsterId;
}

long long WorldView::Target() const {
    std::lock_guard<std::mutex> lock(mu_);
    return targetId_;
}

void WorldView::DropTargetIfMissing() {
    std::lock_guard<std::mutex> lock(mu_);
    if (targetId_ == 0) return;
    const auto it = std::find_if(monsters_.begin(), monsters_.end(),
                                 [this](const MonsterView& m) { return m.monsterId == targetId_; });
    if (it == monsters_.end() || it->dead) targetId_ = 0;
}

void WorldView::AgeTexts(float dtSec) {
    std::lock_guard<std::mutex> lock(mu_);
    for (FloatingText& t : texts_) t.ageSec += dtSec;
    texts_.erase(std::remove_if(texts_.begin(), texts_.end(),
                                [](const FloatingText& t) { return t.ageSec >= t.lifeSec; }),
                 texts_.end());
}

void WorldView::AddFloating(double x, double y, int value, int kind, bool crit, bool miss) {
    std::lock_guard<std::mutex> lock(mu_);
    AddFloatingLocked(x, y, value, kind, crit, miss);
}

void WorldView::AddFloatingLocked(double x, double y, int value, int kind, bool crit, bool miss,
                                 bool onPlayer) {
    if (texts_.size() >= kMaxFloatingTexts) texts_.erase(texts_.begin());
    FloatingText t;
    t.x = Clamp01(x);
    t.y = Clamp01(y);
    t.value = value;
    t.kind = kind;
    t.crit = crit;
    t.miss = miss;
    t.lifeSec = kTextLifeSec;
    t.onPlayer = onPlayer;
    if (onPlayer) {
        // ใช้ตำแหน่งผู้เล่นที่ Client วาดอยู่ (ตำแหน่งบนแผนที่ของผู้เล่นเอง)
        t.x = Clamp01(playerX_);
        t.y = Clamp01(playerY_);
    }
    texts_.push_back(t);
}

void WorldView::SetPlayerPosition(double x, double y) {
    std::lock_guard<std::mutex> lock(mu_);
    playerX_ = Clamp01(x);
    playerY_ = Clamp01(y);
}

void WorldView::Clear() {
    std::lock_guard<std::mutex> lock(mu_);
    stats_ = PlayerStats{};
    players_.clear();
    monsters_.clear();
    texts_.clear();
    targetId_ = 0;
    inWorld_ = false;
}

bool WorldView::TargetPosition(double& x, double& y) const {
    std::lock_guard<std::mutex> lock(mu_);
    for (const MonsterView& m : monsters_) {
        if (m.monsterId == targetId_ && !m.dead) {
            x = m.x;
            y = m.y;
            return true;
        }
    }
    return false;
}

WorldView& View() {
    static WorldView view;
    return view;
}

}  // namespace mobile::net
