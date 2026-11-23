// mobile/net_client/social_view.cpp
#include "social_view.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>

namespace mobile::net {
namespace {

// ---------------------------------------------------------------- tiny JSON scanner
// The server writes plain JSON with no nested escaped quotes in the fields we read (names come from
// content.json / the player, and JsonEscape on the server keeps them quote-free in practice). One
// small scanner keeps the Android build free of a JSON dependency.
struct Scanner {
    const std::string& s;
    explicit Scanner(const std::string& text) : s(text) {}

    void Ws(size_t& i) const {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    }

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

    // Index of the value of "key" that belongs to the object starting at `obj` (its closing brace
    // ends the search), so a key of an outer object is not mistaken for this one's.
    size_t ValueIn(const std::string& key, size_t obj) const {
        if (obj == std::string::npos) return std::string::npos;
        const size_t end = ElementEnd(obj);
        const std::string needle = "\"" + key + "\"";
        size_t p = s.find(needle, obj);
        if (p == std::string::npos || (end != std::string::npos && p >= end)) return std::string::npos;
        p = s.find(':', p + needle.size());
        if (p == std::string::npos || (end != std::string::npos && p >= end)) return std::string::npos;
        ++p;
        Ws(p);
        return p;
    }

    bool Str(const std::string& key, std::string& out, size_t from = 0) const {
        size_t p = Value(key, from);
        if (p == std::string::npos || s[p] != '"') return false;
        const size_t e = s.find('"', p + 1);
        if (e == std::string::npos) return false;
        out = Unescape(s.substr(p + 1, e - p - 1));
        return true;
    }

    // UTF-16 \u escapes become UTF-8 so Thai text sent back by the server survives (names come from
    // content.json and are Thai); \n, \t, \\ and \" are undone too.
    static std::string Unescape(const std::string& in) {
        std::string out;
        out.reserve(in.size());
        for (size_t i = 0; i < in.size(); ++i) {
            if (in[i] != '\\' || i + 1 >= in.size()) { out.push_back(in[i]); continue; }
            const char c = in[++i];
            switch (c) {
                case 'n': out.push_back('\n'); break;
                case 't': out.push_back('\t'); break;
                case 'r': out.push_back('\r'); break;
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'u': {
                    if (i + 4 >= in.size()) break;
                    const std::string hex = in.substr(i + 1, 4);
                    char* end = nullptr;
                    const long cp = std::strtol(hex.c_str(), &end, 16);
                    i += 4;
                    if (cp < 0x80) {
                        out.push_back(static_cast<char>(cp));
                    } else if (cp < 0x800) {
                        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    } else {
                        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    }
                    break;
                }
                default: out.push_back(c); break;
            }
        }
        return out;
    }

    bool Num(const std::string& key, double& out, size_t from = 0) const {
        const size_t p = Value(key, from);
        if (p == std::string::npos) return false;
        const char* start = s.c_str() + p;
        char* end = nullptr;
        const double v = std::strtod(start, &end);
        if (end == start) return false;
        out = v;
        return true;
    }

    bool Int(const std::string& key, long long& out, size_t from = 0) const {
        const size_t p = Value(key, from);
        if (p == std::string::npos) return false;
        const char* start = s.c_str() + p;
        char* end = nullptr;
        const long long v = std::strtoll(start, &end, 10);
        if (end == start) return false;
        out = v;
        return true;
    }

    bool Bool(const std::string& key, bool& out, size_t from = 0) const {
        const size_t p = Value(key, from);
        if (p == std::string::npos) return false;
        if (s.compare(p, 4, "true") == 0) { out = true; return true; }
        if (s.compare(p, 5, "false") == 0) { out = false; return true; }
        return false;
    }

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

    // Runs `visit` with the [begin, end) of every element of the array at `pos`.
    template <typename Visit>
    static bool ArrayAt(const std::string& s, size_t pos, Visit visit) {
        Scanner sc(s);
        if (pos == std::string::npos || pos >= s.size() || s[pos] != '[') return false;
        size_t i = pos + 1;
        int index = 0;
        while (true) {
            sc.Ws(i);
            if (i >= s.size()) return false;
            if (s[i] == ']') return true;
            const size_t begin = i;
            const size_t end = sc.ElementEnd(i);
            if (end == std::string::npos) return false;
            visit(s, begin, end, index++);
            i = end;
            sc.Ws(i);
            if (i < s.size() && s[i] == ',') { ++i; continue; }
            return i < s.size() && s[i] == ']';
        }
    }

    void ForEachInArray(const std::string& key, const std::function<void(const Scanner&, size_t)>& visit,
                        size_t from = 0) const {
        const size_t p = Value(key, from);
        if (p == std::string::npos) return;
        ArrayAt(s, p, [&](const std::string& text, size_t b, size_t e, int) {
            const std::string obj = text.substr(b, e - b);
            Scanner item(obj);
            visit(item, 0);
        });
    }
};

double NumOr(const Scanner& sc, size_t from, const std::string& key, double fallback) {
    double v = fallback;
    if (!sc.Num(key, v, from)) return fallback;
    return v;
}

}  // namespace

// ---------------------------------------------------------------- parsing

bool ParseGates(const std::string& json, std::vector<GateView>& out) {
    out.clear();
    Scanner sc(json);
    if (sc.Value("gates") == std::string::npos) return false;
    sc.ForEachInArray("gates", [&](const Scanner& g, size_t base) {
        GateView v;
        long long i = 0;
        double d = 0;
        bool b = false;
        if (!g.Int("gate_id", i, base)) return;
        v.gateId = i;
        g.Str("name", v.name, base);
        if (g.Int("to_map", i, base)) v.toMap = i;
        g.Str("to_map_name", v.toMapName, base);
        if (g.Num("distance", d, base)) v.distance = d;
        if (g.Bool("in_range", b, base)) v.inRange = b;
        if (g.Num("radius", d, base)) v.radius = d;
        if (g.Num("required_level", d, base)) v.requiredLevel = int(d);
        if (g.Num("required_money", d, base)) v.requiredMoney = int(d);
        // The gate's own "position": the first one inside the element belongs to the gate.
        const size_t pos = g.Value("position", base);
        if (pos != std::string::npos) {
            v.x = NumOr(g, pos, "x", 0.5);
            v.y = NumOr(g, pos, "y", 0.5);
        }
        out.push_back(v);
    });
    return true;
}

bool ParseNpcs(const std::string& json, std::vector<NpcView>& out) {
    out.clear();
    Scanner sc(json);
    if (sc.Value("npcs") == std::string::npos) return false;
    sc.ForEachInArray("npcs", [&](const Scanner& n, size_t base) {
        NpcView v;
        long long i = 0;
        if (!n.Int("npc_id", i, base)) return;
        v.npcId = i;
        n.Str("name", v.name, base);
        n.Str("role", v.role, base);
        n.Str("shop_name", v.shopName, base);
        const size_t pos = n.Value("position", base);
        if (pos != std::string::npos) {
            v.x = NumOr(n, pos, "x", 0.5);
            v.y = NumOr(n, pos, "y", 0.5);
        }
        out.push_back(v);
    });
    return true;
}

bool ParseNpcDialog(const std::string& json, NpcDialog& out) {
    out = NpcDialog{};
    Scanner sc(json);
    const size_t npc = sc.Value("npc");
    if (npc == std::string::npos) return false;
    long long i = 0;
    if (sc.Int("npc_id", i, npc)) out.npcId = i;
    sc.Str("name", out.npcName, npc);
    sc.Str("shop_name", out.shopName, npc);
    double d = 0;
    if (sc.Num("sell_rate_percent", d, npc)) out.sellRatePercent = int(d);
    if (sc.Num("money", d)) out.money = int(d);

    // "dialog": ["line", "line"] — plain strings.
    const size_t dlg = sc.Value("dialog");
    if (dlg != std::string::npos && dlg < json.size() && json[dlg] == '[') {
        Scanner::ArrayAt(json, dlg, [&](const std::string& text, size_t b, size_t e, int) {
            (void)e;
            size_t p = b;
            while (p < text.size() && text[p] != '"') ++p;
            if (p >= text.size()) return;
            const size_t q = text.find('"', p + 1);
            if (q == std::string::npos) return;
            out.lines.push_back(Scanner::Unescape(text.substr(p + 1, q - p - 1)));
        });
    }

    sc.ForEachInArray("shop", [&](const Scanner& s, size_t base) {
        ShopEntry e;
        long long id = 0;
        if (!s.Int("item_id", id, base)) return;
        e.itemId = id;
        s.Str("name", e.name, base);
        s.Str("type", e.type, base);
        e.buyPrice = int(NumOr(s, base, "buy_price", 0));
        bool b = false;
        if (s.Bool("stackable", b, base)) e.stackable = b;
        e.attackBonus = int(NumOr(s, base, "attack_bonus", 0));
        e.defenseBonus = int(NumOr(s, base, "defense_bonus", 0));
        e.hpRecovery = int(NumOr(s, base, "hp_recovery", 0));
        e.mpRecovery = int(NumOr(s, base, "mp_recovery", 0));
        out.shop.push_back(e);
    });

    sc.ForEachInArray("quests", [&](const Scanner& q, size_t base) {
        QuestOffer o;
        long long id = 0;
        if (!q.Int("quest_id", id, base)) return;
        o.questId = id;
        q.Str("name", o.name, base);
        o.requiredCount = int(NumOr(q, base, "required_count", 0));
        o.rewardExp = int(NumOr(q, base, "reward_exp", 0));
        o.rewardMoney = int(NumOr(q, base, "reward_money", 0));
        out.quests.push_back(o);
    });
    out.open = true;
    return true;
}

bool ParseChatHistory(const std::string& json, std::vector<ChatMessage>& out) {
    out.clear();
    Scanner sc(json);
    if (sc.Value("messages") == std::string::npos) return false;
    sc.ForEachInArray("messages", [&](const Scanner& m, size_t base) {
        ChatMessage c;
        m.Str("channel", c.channel, base);
        m.Str("name", c.name, base);
        m.Str("body", c.body, base);
        double d = 0;
        if (m.Num("at_ms", d, base)) c.atMs = static_cast<long long>(d);
        out.push_back(c);
    });
    return true;
}

bool ParseChatSent(const std::string& json, ChatMessage& out) {
    Scanner sc(json);
    if (!sc.Str("body", out.body)) return false;
    sc.Str("channel", out.channel);
    sc.Str("name", out.name);
    double d = 0;
    if (sc.Num("at_ms", d)) out.atMs = static_cast<long long>(d);
    return true;
}

bool ParsePartyState(const std::string& json, PartyView& party, std::vector<PartyInvite>& invites) {
    party = PartyView{};
    invites.clear();
    Scanner sc(json);
    // The accept/invite answers carry only "party"; the state answer carries both keys.
    if (sc.Value("invites") == std::string::npos && sc.Value("party") == std::string::npos)
        return false;
    const size_t p = sc.Value("party");
    // "party": null when the player has no party.
    if (p != std::string::npos && p < json.size() && json[p] == '{') {
        long long i = 0;
        if (sc.Int("party_id", i, p)) party.partyId = i;
        double d = 0;
        if (sc.Num("max_members", d, p)) party.maxMembers = int(d);
        sc.ForEachInArray("members", [&](const Scanner& m, size_t base) {
            PartyMemberView v;
            long long id = 0;
            if (!m.Int("character_id", id, base)) return;
            v.characterId = id;
            m.Str("name", v.name, base);
            m.Str("class", v.cls, base);
            v.level = int(NumOr(m, base, "level", 1));
            v.hp = int(NumOr(m, base, "hp", 0));
            m.Str("map_name", v.mapName, base);
            bool b = false;
            if (m.Bool("is_leader", b, base)) v.leader = b;
            if (m.Bool("dead", b, base)) v.dead = b;
            party.members.push_back(v);
        }, p);
    }
    sc.ForEachInArray("invites", [&](const Scanner& inv, size_t base) {
        PartyInvite v;
        long long id = 0;
        if (!inv.Int("party_id", id, base)) return;
        v.partyId = id;
        inv.Str("inviter", v.inviter, base);
        invites.push_back(v);
    });
    return true;
}

bool ParseTrade(const std::string& json, int& paid, int& received, int& moneyAfter, int& quantity) {
    paid = received = quantity = 0;
    Scanner sc(json);
    double d = 0;
    bool any = false;
    if (sc.Num("paid", d)) { paid = int(d); any = true; }
    if (sc.Num("income", d)) { received = int(d); any = true; }
    const size_t ch = sc.Value("character");
    if (ch != std::string::npos && sc.Num("money", d, ch)) { moneyAfter = int(d); any = true; }
    const size_t bought = sc.Value("bought");
    if (bought != std::string::npos && sc.Num("quantity", d, bought)) quantity = int(d);
    const size_t sold = sc.Value("sold");
    if (sold != std::string::npos && sc.Num("quantity", d, sold)) quantity = int(d);
    return any;
}

std::string ErrorCode(const std::string& err) {
    // "HTTP 409 gate_locked" -> "gate_locked"; anything else -> "".
    const size_t sp = err.find("HTTP ");
    if (sp != std::string::npos) {
        const size_t a = err.find(' ', sp + 5);
        if (a != std::string::npos) return err.substr(a + 1);
    }
    return "";
}

// ---------------------------------------------------------------- SocialView

void SocialView::ApplySync(const std::string& json) {
    std::vector<GateView> gates;
    std::vector<NpcView> npcs;
    const bool hasGates = ParseGates(json, gates);
    const bool hasNpcs = ParseNpcs(json, npcs);
    if (!hasGates && !hasNpcs) return;
    std::lock_guard<std::mutex> lock(mu_);
    if (hasGates) gates_ = std::move(gates);
    if (hasNpcs) npcs_ = std::move(npcs);
}

void SocialView::SetGates(std::vector<GateView> gates) {
    std::lock_guard<std::mutex> lock(mu_);
    gates_ = std::move(gates);
}

void SocialView::SetNpcs(std::vector<NpcView> npcs) {
    std::lock_guard<std::mutex> lock(mu_);
    npcs_ = std::move(npcs);
}

void SocialView::ApplyTalk(const std::string& json) {
    NpcDialog dlg;
    if (!ParseNpcDialog(json, dlg)) return;
    std::lock_guard<std::mutex> lock(mu_);
    dialog_ = std::move(dlg);
}

bool SocialView::ApplyTalkError(const std::string& err) {
    (void)err;
    std::lock_guard<std::mutex> lock(mu_);
    // A refused talk (too far, other map) must not leave a stale panel open.
    dialog_ = NpcDialog{};
    return false;
}

void SocialView::CloseDialog() {
    std::lock_guard<std::mutex> lock(mu_);
    dialog_ = NpcDialog{};
}

void SocialView::ApplyChatHistory(const std::string& json) {
    std::vector<ChatMessage> msgs;
    if (!ParseChatHistory(json, msgs)) return;
    std::lock_guard<std::mutex> lock(mu_);
    chat_ = std::move(msgs);
    if (chat_.size() > kMaxChat) chat_.erase(chat_.begin(), chat_.end() - kMaxChat);
}

void SocialView::ApplyChatSent(const std::string& json) {
    ChatMessage msg;
    if (!ParseChatSent(json, msg)) return;
    std::lock_guard<std::mutex> lock(mu_);
    // The send answer and the history poll can overlap: drop an exact duplicate.
    if (!chat_.empty() && chat_.back().atMs == msg.atMs && chat_.back().body == msg.body &&
        chat_.back().name == msg.name)
        return;
    chat_.push_back(std::move(msg));
    if (chat_.size() > kMaxChat) chat_.erase(chat_.begin(), chat_.end() - kMaxChat);
}

void SocialView::AddSystemLine(const std::string& text) {
    ChatMessage msg;
    msg.channel = "system";
    msg.name = "";
    msg.body = text;
    std::lock_guard<std::mutex> lock(mu_);
    chat_.push_back(std::move(msg));
    if (chat_.size() > kMaxChat) chat_.erase(chat_.begin(), chat_.end() - kMaxChat);
}

void SocialView::ApplyPartyState(const std::string& json) {
    PartyView party;
    std::vector<PartyInvite> invites;
    if (!ParsePartyState(json, party, invites)) return;
    std::lock_guard<std::mutex> lock(mu_);
    party_ = std::move(party);
    invites_ = std::move(invites);
}

SocialView::Snapshot SocialView::Get() const {
    std::lock_guard<std::mutex> lock(mu_);
    Snapshot snap;
    snap.gates = gates_;
    snap.npcs = npcs_;
    snap.dialog = dialog_;
    snap.party = party_;
    snap.invites = invites_;
    snap.chat = chat_;
    return snap;
}

std::vector<ChatMessage> SocialView::Messages() const {
    std::lock_guard<std::mutex> lock(mu_);
    return chat_;
}

long long SocialView::DialogNpc() const {
    std::lock_guard<std::mutex> lock(mu_);
    return dialog_.open ? dialog_.npcId : 0;
}

long long SocialView::GateUnderPlayer() const {
    std::lock_guard<std::mutex> lock(mu_);
    const GateView* best = nullptr;
    for (const auto& g : gates_) {
        if (!g.inRange) continue;
        if (best == nullptr || g.distance < best->distance) best = &g;
    }
    return best == nullptr ? 0 : best->gateId;
}

void SocialView::Clear() {
    std::lock_guard<std::mutex> lock(mu_);
    gates_.clear();
    npcs_.clear();
    dialog_ = NpcDialog{};
    party_ = PartyView{};
    invites_.clear();
    chat_.clear();
}

SocialView& Social() {
    static SocialView view;
    return view;
}

}  // namespace mobile::net
