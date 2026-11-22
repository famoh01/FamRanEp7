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
    const std::string body = "{\"username\": \"" + username + "\", \"password\": \"" + password + "\"}";
    return Call("POST", "/api/auth/register", body, r, err);
}

bool ServerApi::Login(const std::string& username, const std::string& password, std::string& err) {
    HttpResponse r;
    const std::string body = "{\"username\": \"" + username + "\", \"password\": \"" + password + "\"}";
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
    const std::string body = "{\"name\": \"" + name + "\", \"gender\": \"" + gender +
                             "\", \"class\": \"" + cls + "\"}";
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
        return MoveResult::Rejected;
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

}  // namespace mobile::net
