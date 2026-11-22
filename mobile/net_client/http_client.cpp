// mobile/net_client/http_client.cpp
#include "http_client.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0   // Android/Linux define it; keeps other POSIX systems compiling
#endif

namespace mobile::net {
namespace {

bool ParseResponse(const std::string& raw, HttpResponse& out, std::string& err) {
    const size_t hdrEnd = raw.find("\r\n\r\n");
    if (hdrEnd == std::string::npos) { err = "incomplete response"; return false; }
    if (raw.compare(0, 5, "HTTP/") != 0) { err = "not an HTTP response"; return false; }
    const size_t sp = raw.find(' ');
    if (sp == std::string::npos || sp + 4 > raw.size()) { err = "bad status line"; return false; }
    out.status = std::atoi(raw.c_str() + sp + 1);

    std::string headers = raw.substr(0, hdrEnd);
    for (char& c : headers) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (headers.find("\r\ntransfer-encoding: chunked") != std::string::npos) {
        err = "chunked responses are not supported";
        return false;
    }
    const std::string clKey = "\r\ncontent-length:";
    const size_t clPos = headers.find(clKey);
    const std::string rest = raw.substr(hdrEnd + 4);
    if (clPos != std::string::npos) {
        const long n = std::strtol(headers.c_str() + clPos + clKey.size(), nullptr, 10);
        if (n < 0 || static_cast<size_t>(n) > rest.size()) { err = "truncated body"; return false; }
        out.body = rest.substr(0, static_cast<size_t>(n));
    } else {
        out.body = rest;
    }
    return true;
}

}  // namespace

HttpClient::HttpClient(std::string host, int port, int timeoutSec)
    : host_(std::move(host)), port_(port), timeoutSec_(timeoutSec) {}

bool HttpClient::Request(const std::string& method, const std::string& path, const std::string& bearerToken,
                         const std::string& jsonBody, HttpResponse& out, std::string& err) const {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    const std::string portStr = std::to_string(port_);
    const int rc = getaddrinfo(host_.c_str(), portStr.c_str(), &hints, &res);
    if (rc != 0) { err = std::string("resolve: ") + gai_strerror(rc); return false; }

    int fd = -1;
    for (addrinfo* ai = res; ai != nullptr; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        timeval tv{};
        tv.tv_sec = timeoutSec_;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) { err = "connect failed"; return false; }

    std::string req = method + " " + path + " HTTP/1.1\r\n";
    req += "Host: " + host_ + ":" + portStr + "\r\n";
    req += "Connection: close\r\n";
    req += "Accept: application/json\r\n";
    if (!bearerToken.empty()) req += "Authorization: Bearer " + bearerToken + "\r\n";
    if (method == "POST" || !jsonBody.empty()) {
        req += "Content-Type: application/json\r\n";
        req += "Content-Length: " + std::to_string(jsonBody.size()) + "\r\n";
    }
    req += "\r\n";
    req += jsonBody;

    size_t sent = 0;
    while (sent < req.size()) {
        const ssize_t n = send(fd, req.data() + sent, req.size() - sent, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { err = "send failed"; close(fd); return false; }
        sent += static_cast<size_t>(n);
    }

    std::string raw;
    char buf[4096];
    for (;;) {
        const ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { err = "receive failed or timed out"; close(fd); return false; }
        if (n == 0) break;   // server closes the connection after the response (Connection: close)
        raw.append(buf, static_cast<size_t>(n));
    }
    close(fd);
    return ParseResponse(raw, out, err);
}

bool JsonFindString(const std::string& json, const std::string& key, std::string& value) {
    const std::string needle = "\"" + key + "\"";
    size_t p = json.find(needle);
    if (p == std::string::npos) return false;
    p = json.find(':', p + needle.size());
    if (p == std::string::npos) return false;
    p = json.find('"', p + 1);
    if (p == std::string::npos) return false;
    const size_t e = json.find('"', p + 1);
    if (e == std::string::npos) return false;
    value = json.substr(p + 1, e - p - 1);
    return true;
}

bool JsonFindInt(const std::string& json, const std::string& key, long long& value) {
    const std::string needle = "\"" + key + "\"";
    size_t p = json.find(needle);
    if (p == std::string::npos) return false;
    p = json.find(':', p + needle.size());
    if (p == std::string::npos) return false;
    ++p;
    while (p < json.size() && std::isspace(static_cast<unsigned char>(json[p]))) ++p;
    char* end = nullptr;
    const long long v = std::strtoll(json.c_str() + p, &end, 10);
    if (end == json.c_str() + p) return false;
    value = v;
    return true;
}

bool JsonFindNumber(const std::string& json, const std::string& key, double& value) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    const char* start = json.c_str() + pos;
    char* end = nullptr;
    const double v = std::strtod(start, &end);
    if (end == start) return false;
    value = v;
    return true;
}

}  // namespace mobile::net
