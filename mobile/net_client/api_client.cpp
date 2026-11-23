// mobile/net_client/api_client.cpp
#include "api_client.h"

#include <cstdio>

namespace mobile::net {

bool ServerApi::Call(const std::string& method, const std::string& path, const std::string& body,
                     HttpResponse& out, std::string& err) const {
    if (!request_(method, path, token_, body, out, err)) return false;
    if (out.status >= 400) {
        std::string code = "unknown";
        JsonFindString(out.body, "code", code);
        err = "HTTP " + std::to_string(out.status) + " " + code;
        return false;
    }
    return true;
}

bool ServerApi::Health(std::string& err) const {
    HttpResponse r;
    return Call("GET", "/api/health", "", r, err);
}

bool ServerApi::Register(const std::string& username, const std::string& password, std::string& err) const {
    HttpResponse r;
    const std::string body = "{\"username\": \"" + JsonEscape(username) + "\", \"password\": \"" +
                             JsonEscape(password) + "\"}";
    return Call("POST", "/api/auth/register", body, r, err);
}

bool ServerApi::Login(const std::string& username, const std::string& password, std::string& err) {
    HttpResponse r;
    const std::string body = "{\"username\": \"" + JsonEscape(username) + "\", \"password\": \"" +
                             JsonEscape(password) + "\"}";
    if (!Call("POST", "/api/auth/login", body, r, err)) return false;
    std::string token;
    if (!JsonFindString(r.body, "token", token) || token.size() < 16) {
        err = "login response has no token";
        return false;
    }
    token_ = token;
    return true;
}

bool ServerApi::ListCharacters(std::string& json, std::string& err) const {
    HttpResponse r;
    if (!Call("GET", "/api/characters", "", r, err)) return false;
    json = r.body;
    return true;
}

bool ServerApi::ListMonsters(std::string& json, std::string& err) const {
    HttpResponse r;
    if (!Call("GET", "/api/world/monsters", "", r, err)) return false;
    json = r.body;
    return true;
}

bool ServerApi::CreateCharacter(const std::string& name, const std::string& gender, const std::string& cls,
                                long long& characterId, std::string& err) const {
    HttpResponse r;
    const std::string body = "{\"name\": \"" + JsonEscape(name) + "\", \"gender\": \"" + JsonEscape(gender) +
                             "\", \"class\": \"" + JsonEscape(cls) + "\"}";
    if (!Call("POST", "/api/characters", body, r, err)) return false;
    if (!JsonFindInt(r.body, "id", characterId) || characterId <= 0) {
        err = "create response has no character id";
        return false;
    }
    return true;
}

bool ServerApi::DeleteCharacter(long long characterId, std::string& err) const {
    HttpResponse r;
    return Call("DELETE", "/api/characters/" + std::to_string(characterId), "", r, err);
}

bool ServerApi::GetCharacter(long long characterId, std::string& json, std::string& err) const {
    HttpResponse r;
    if (!Call("GET", "/api/characters/" + std::to_string(characterId), "", r, err)) return false;
    json = r.body;
    return true;
}

bool ServerApi::EnterWorld(long long characterId, std::string& json, std::string& err) const {
    HttpResponse r;
    if (!Call("POST", "/api/characters/" + std::to_string(characterId) + "/world/enter", "", r, err)) return false;
    json = r.body;
    return true;
}

bool ServerApi::LeaveWorld(long long characterId, std::string& err) const {
    HttpResponse r;
    return Call("POST", "/api/characters/" + std::to_string(characterId) + "/world/leave", "", r, err);
}

bool ServerApi::GetWorldState(long long characterId, std::string& json, std::string& err) const {
    HttpResponse r;
    if (!Call("GET", "/api/characters/" + std::to_string(characterId) + "/world", "", r, err)) return false;
    json = r.body;
    return true;
}

bool ServerApi::GetWorldSync(long long characterId, std::string& json, std::string& err) const {
    HttpResponse r;
    if (!Call("GET", "/api/characters/" + std::to_string(characterId) + "/world/sync", "", r, err)) return false;
    json = r.body;
    return true;
}

bool ServerApi::UseItemAtSlot(long long characterId, int slot, std::string& json, std::string& err) const {
    HttpResponse r;
    const std::string body = "{\"slot\": " + std::to_string(slot) + ", \"quantity\": 1}";
    if (!Call("POST", "/api/characters/" + std::to_string(characterId) + "/inventory/use", body, r, err))
        return false;
    json = r.body;
    return true;
}

namespace {

// Maps a combat answer onto CombatResult. A 4xx body carries {"error": {"code": ...}}.
ServerApi::CombatAnswer Classify(HttpResponse& r, std::string& err) {
    ServerApi::CombatAnswer out;
    auto codeOf = [&r]() {
        std::string code = "unknown";
        JsonFindString(r.body, "code", code);
        return code;
    };
    if (r.status == 200) {
        out.result = ServerApi::CombatResult::Ok;
        out.body = r.body;
        return out;
    }
    out.code = codeOf();
    long long retry = 0;
    if (JsonFindInt(r.body, "retry_after_ms", retry)) out.retryAfterMs = int(retry);
    if (r.status == 401) {
        out.result = ServerApi::CombatResult::Unauthorized;
        err = "unauthorized";
    } else if (r.status == 429) {
        out.result = ServerApi::CombatResult::Cooldown;
        err = out.code;
    } else if (r.status == 409) {
        if (out.code == "out_of_range") out.result = ServerApi::CombatResult::OutOfRange;
        else if (out.code == "not_in_world") out.result = ServerApi::CombatResult::NotInWorld;
        else if (out.code == "character_dead" || out.code == "monster_dead")
            out.result = ServerApi::CombatResult::Dead;
        else if (out.code == "not_enough_mp" || out.code == "not_enough_sp")
            out.result = ServerApi::CombatResult::NoMana;
        else out.result = ServerApi::CombatResult::Failed;
        err = out.code;
    } else {
        out.result = ServerApi::CombatResult::Failed;
        err = "HTTP " + std::to_string(r.status) + " " + out.code;
    }
    return out;
}

}  // namespace

ServerApi::CombatAnswer ServerApi::Attack(long long characterId, long long monsterId) const {
    HttpResponse r;
    std::string err;
    const std::string body = "{\"monster_id\": " + std::to_string(monsterId) + "}";
    const std::string path = "/api/characters/" + std::to_string(characterId) + "/combat/attack";
    if (!request_("POST", path, token_, body, r, err)) {
        CombatAnswer out;
        out.result = CombatResult::Failed;
        out.code = err;
        return out;
    }
    return Classify(r, err);
}

ServerApi::CombatAnswer ServerApi::UseSkill(long long characterId, int skillId, long long monsterId) const {
    HttpResponse r;
    std::string err;
    const std::string body = "{\"skill_id\": " + std::to_string(skillId) +
                             ", \"monster_id\": " + std::to_string(monsterId) + "}";
    const std::string path = "/api/characters/" + std::to_string(characterId) + "/combat/skill";
    if (!request_("POST", path, token_, body, r, err)) {
        CombatAnswer out;
        out.result = CombatResult::Failed;
        out.code = err;
        return out;
    }
    return Classify(r, err);
}

namespace {

// Reads {"key": {"x": .., "y": ..}} from a flat-ish response.
bool JsonFindPoint(const std::string& json, const std::string& key, WorldPoint& p) {
    const size_t k = json.find("\"" + key + "\"");
    if (k == std::string::npos) return false;
    const size_t open = json.find('{', k);
    if (open == std::string::npos) return false;
    const size_t close = json.find('}', open);
    if (close == std::string::npos) return false;
    const std::string obj = json.substr(open, close - open + 1);
    return JsonFindNumber(obj, "x", p.x) && JsonFindNumber(obj, "y", p.y);
}

std::string FormatCoord(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6f", v);
    return buf;
}

}  // namespace

ServerApi::MoveResult ServerApi::MoveCharacter(long long characterId, double x, double y, WorldPoint& pos,
                                               std::string& err) const {
    // Not Call(): a 409 body carries the server's authoritative position, which Call() would discard.
    HttpResponse r;
    const std::string body = "{\"x\": " + FormatCoord(x) + ", \"y\": " + FormatCoord(y) + "}";
    const std::string path = "/api/characters/" + std::to_string(characterId) + "/world/move";
    if (!request_("POST", path, token_, body, r, err)) return MoveResult::Failed;
    if (r.status == 200) {
        if (!JsonFindPoint(r.body, "position", pos)) {
            err = "move response has no position";
            return MoveResult::Failed;
        }
        err.clear();
        return MoveResult::Accepted;
    }
    if (r.status == 409) {
        JsonFindPoint(r.body, "position", pos);
        std::string code = "unknown";
        JsonFindString(r.body, "code", code);
        err = code;
        return code == "not_in_world" ? MoveResult::SessionLost : MoveResult::Rejected;
    }
    if (r.status == 401) {
        err = "unauthorized";
        return MoveResult::Unauthorized;
    }
    if (r.status == 429) {
        // Too many move requests: the move was not judged and nothing changed on the server.
        std::string code = "unknown";
        JsonFindString(r.body, "code", code);
        err = code;
        return code == "move_too_fast" ? MoveResult::Throttled : MoveResult::Failed;
    }
    err = "HTTP " + std::to_string(r.status);
    return MoveResult::Failed;
}

bool ServerApi::Logout(std::string& err) {
    HttpResponse r;
    const bool ok = Call("POST", "/api/auth/logout", "", r, err);
    token_.clear();
    return ok;
}

// ---------------------------------------------------------------- round 4 systems
bool ServerApi::ListMaps(std::string& json, std::string& err) const {
    HttpResponse r;
    if (!Call("GET", "/api/maps", "", r, err)) return false;
    json = r.body;
    return true;
}

bool ServerApi::GetUiStrings(long long /*characterId*/, std::string& json,
                             std::string& err) const {
    // ไม่ผูกกับตัวละคร: ข้อความ UI เป็นของทั้งเซิร์ฟเวอร์ (ยังต้องมี token)
    HttpResponse r;
    if (!Call("GET", "/api/ui/strings", "", r, err)) return false;
    json = r.body;
    return true;
}

bool ServerApi::GetGates(long long characterId, std::string& json, std::string& err) const {
    HttpResponse r;
    if (!Call("GET", "/api/characters/" + std::to_string(characterId) + "/map/gates", "", r, err))
        return false;
    json = r.body;
    return true;
}

ServerApi::GateResult ServerApi::UseGate(long long characterId, long long gateId, std::string& json,
                                         std::string& code, std::string& err) const {
    HttpResponse r;
    const std::string body = "{\"gate_id\": " + std::to_string(gateId) + "}";
    const bool ok = Call("POST", "/api/characters/" + std::to_string(characterId) + "/map/gate",
                         body, r, err);
    code.clear();
    if (ok) {
        json = r.body;
        return GateResult::Ok;
    }
    // The transport failed (server down / timeout) rather than the server refusing the gate.
    if (r.status == 0) return GateResult::Failed;
    JsonFindString(r.body, "code", code);
    if (r.status == 401) return GateResult::Unauthorized;
    return GateResult::Refused;
}

bool ServerApi::TalkToNpc(long long characterId, long long npcId, std::string& json,
                          std::string& err) const {
    HttpResponse r;
    if (!Call("POST", "/api/characters/" + std::to_string(characterId) + "/npc/" +
                     std::to_string(npcId) + "/talk", "", r, err))
        return false;
    json = r.body;
    return true;
}

bool ServerApi::BuyFromNpc(long long characterId, long long npcId, long long itemId, int quantity,
                           std::string& json, std::string& err) const {
    HttpResponse r;
    const std::string body = "{\"item_id\": " + std::to_string(itemId) +
                             ", \"quantity\": " + std::to_string(quantity) + "}";
    if (!Call("POST", "/api/characters/" + std::to_string(characterId) + "/npc/" +
                     std::to_string(npcId) + "/buy", body, r, err))
        return false;
    json = r.body;
    return true;
}

bool ServerApi::SellToNpc(long long characterId, long long npcId, int slot, int quantity,
                          std::string& json, std::string& err) const {
    HttpResponse r;
    const std::string body = "{\"slot\": " + std::to_string(slot) +
                             ", \"quantity\": " + std::to_string(quantity) + "}";
    if (!Call("POST", "/api/characters/" + std::to_string(characterId) + "/npc/" +
                     std::to_string(npcId) + "/sell", body, r, err))
        return false;
    json = r.body;
    return true;
}

bool ServerApi::SendChat(long long characterId, const std::string& text, std::string& json,
                         std::string& err) const {
    HttpResponse r;
    const std::string body = "{\"text\": \"" + JsonEscape(text) + "\"}";
    if (!Call("POST", "/api/characters/" + std::to_string(characterId) + "/chat", body, r, err))
        return false;
    json = r.body;
    return true;
}

bool ServerApi::GetChat(long long characterId, std::string& json, std::string& err) const {
    HttpResponse r;
    if (!Call("GET", "/api/characters/" + std::to_string(characterId) + "/chat", "", r, err))
        return false;
    json = r.body;
    return true;
}

bool ServerApi::GetParty(long long characterId, std::string& json, std::string& err) const {
    HttpResponse r;
    if (!Call("GET", "/api/characters/" + std::to_string(characterId) + "/party", "", r, err))
        return false;
    json = r.body;
    return true;
}

bool ServerApi::InviteToParty(long long characterId, long long targetCharacterId, std::string& json,
                              std::string& err) const {
    HttpResponse r;
    const std::string body = "{\"character_id\": " + std::to_string(targetCharacterId) + "}";
    if (!Call("POST", "/api/characters/" + std::to_string(characterId) + "/party/invite", body, r, err))
        return false;
    json = r.body;
    return true;
}

bool ServerApi::AcceptPartyInvite(long long characterId, long long partyId, std::string& json,
                                  std::string& err) const {
    HttpResponse r;
    const std::string body = "{\"party_id\": " + std::to_string(partyId) + "}";
    if (!Call("POST", "/api/characters/" + std::to_string(characterId) + "/party/accept", body, r, err))
        return false;
    json = r.body;
    return true;
}

bool ServerApi::LeaveParty(long long characterId, std::string& json, std::string& err) const {
    HttpResponse r;
    if (!Call("POST", "/api/characters/" + std::to_string(characterId) + "/party/leave", "", r, err))
        return false;
    json = r.body;
    return true;
}

}  // namespace mobile::net
