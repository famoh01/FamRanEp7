// mobile/net_client/test_api_client.cpp
// Integration test: talks to a running RAN MOBILE server (see Makefile target "test").
// Usage: test_net [host] [port]
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <thread>
#include <chrono>

#include "api_client.h"

using mobile::net::HttpClient;
using mobile::net::ServerApi;

static int g_failures = 0;

static void Check(bool ok, const char* what, const std::string& err = "") {
    if (ok) {
        std::printf("ok   %s\n", what);
    } else {
        std::printf("FAIL %s %s\n", what, err.c_str());
        ++g_failures;
    }
}

int main(int argc, char** argv) {
    const std::string host = argc > 1 ? argv[1] : "127.0.0.1";
    const int port = argc > 2 ? std::atoi(argv[2]) : 8080;
    ServerApi api{HttpClient(host, port, 10)};
    std::string err;

    // The server may still be starting: retry health for a few seconds.
    bool up = false;
    for (int i = 0; i < 50 && !up; ++i) {
        up = api.Health(err);
        if (!up) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    Check(up, "health", err);
    if (!up) return 1;

    // Unique username: letters and digits only, 3-20 characters.
    const std::string user = "nc" + std::to_string(static_cast<long long>(std::time(nullptr)) % 100000000LL);
    const std::string pass = "pass12345";

    Check(api.Register(user, pass, err), "register new account", err);
    err.clear();
    Check(!api.Register(user, pass, err) && err.find("HTTP 409") == 0, "duplicate register is rejected", err);
    err.clear();
    Check(api.Login(user, pass, err), "login", err);
    Check(api.Token().size() >= 16, "token stored after login");

    err.clear();
    Check(!ServerApi(HttpClient(host, port, 10)).Login(user, "wrong-password", err), "wrong password rejected");

    std::string json;
    err.clear();
    Check(api.ListCharacters(json, err) && json.find("characters") != std::string::npos,
          "list characters", err);
    err.clear();
    Check(api.ListMonsters(json, err) && json.find("Little Vulgarian") != std::string::npos,
          "list monsters (seed data present)", err);

    // Character create, list, delete against the live server.
    long long charId = 0;
    err.clear();
    Check(api.CreateCharacter("Tester" + user.substr(2, 4), "male", "fighter", charId, err) && charId > 0,
          "create character", err);
    err.clear();
    Check(api.ListCharacters(json, err) && json.find("\"id\": " + std::to_string(charId)) != std::string::npos,
          "character appears in list", err);
    err.clear();
    Check(api.DeleteCharacter(charId, err), "delete character", err);
    err.clear();
    Check(api.ListCharacters(json, err) && json.find("\"id\": " + std::to_string(charId)) == std::string::npos,
          "deleted character is gone", err);

    if (g_failures == 0) {
        std::printf("net_client: all checks passed\n");
        return 0;
    }
    std::printf("net_client: %d check(s) failed\n", g_failures);
    return 1;
}
