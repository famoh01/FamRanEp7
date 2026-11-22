// mobile/net_client/api_client.cpp
#include "api_client.h"

namespace mobile::net {

bool ServerApi::Call(const std::string& method, const std::string& path, const std::string& body,
                     HttpResponse& out, std::string& err) const {
    if (!http_.Request(method, path, token_, body, out, err)) return false;
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

}  // namespace mobile::net
