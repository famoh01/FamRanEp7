// mobile/net_client/api_client.cpp
#include "api_client.h"

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

bool ServerApi::Logout(std::string& err) {
    HttpResponse r;
    const bool ok = Call("POST", "/api/auth/logout", "", r, err);
    token_.clear();
    return ok;
}

}  // namespace mobile::net
