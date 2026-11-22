// mobile/net_client/api_client.h
// Typed calls to the RAN MOBILE API (ran_mobile/Server). Keeps the Bearer token after login.
#pragma once

#include <string>

#include "http_client.h"

namespace mobile::net {

class ServerApi {
public:
    explicit ServerApi(HttpClient http) : http_(std::move(http)) {}

    bool Health(std::string& err) const;
    bool Register(const std::string& username, const std::string& password, std::string& err) const;
    bool Login(const std::string& username, const std::string& password, std::string& err);
    bool ListCharacters(std::string& json, std::string& err) const;
    bool ListMonsters(std::string& json, std::string& err) const;

    const std::string& Token() const { return token_; }

private:
    // Sends one request; a 4xx/5xx status becomes an error string containing the server's error code.
    bool Call(const std::string& method, const std::string& path, const std::string& body,
              HttpResponse& out, std::string& err) const;

    HttpClient http_;
    std::string token_;
};

}  // namespace mobile::net
