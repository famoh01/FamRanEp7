// mobile/net_client/http_client.h
// HTTP/1.1 client over POSIX sockets. Works on Linux and the Android NDK (no extra libraries).
// Supports the subset the RAN MOBILE server uses: Content-Length bodies, JSON, Bearer tokens.
#pragma once

#include <cstddef>
#include <string>

namespace mobile::net {

struct HttpResponse {
    int status = 0;
    std::string body;
};

// Largest response body accepted (the server's own limit is far smaller). Protects the phone from a
// misbehaving or hostile server that streams without end.
constexpr size_t kMaxResponseBytes = 1024 * 1024;
// Largest request body the client sends.
constexpr size_t kMaxRequestBytes = 64 * 1024;

class HttpClient {
public:
    HttpClient(std::string host, int port, int timeoutSec = 10);

    // Returns false only on transport errors (resolve, connect, send, receive, bad framing).
    // HTTP error statuses (4xx/5xx) are returned in out.status with a true return value.
    bool Request(const std::string& method, const std::string& path, const std::string& bearerToken,
                 const std::string& jsonBody, HttpResponse& out, std::string& err) const;

private:
    std::string host_;
    int port_;
    int timeoutSec_;
};

// Minimal JSON lookup for flat server responses: finds the first "key": value pair.
// Values must not contain escaped quotes (tokens, names, and codes from this server do not).
bool JsonFindString(const std::string& json, const std::string& key, std::string& value);
bool JsonFindInt(const std::string& json, const std::string& key, long long& value);
bool JsonFindNumber(const std::string& json, const std::string& key, double& value);

// Returns the text as a JSON string literal body (without the surrounding quotes): escapes quote,
// backslash and control characters. UTF-8 bytes pass through unchanged.
std::string JsonEscape(const std::string& text);

}  // namespace mobile::net
