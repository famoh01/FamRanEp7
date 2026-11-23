// mobile/net_client/test_api_client.cpp
// Integration test: talks to a running RAN MOBILE server (see Makefile target "test").
// Usage: test_net [host] [port]
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <thread>
#include <chrono>

#include <cmath>

#include "api_client.h"
#include "world_state.h"

using mobile::net::HttpClient;
using mobile::net::ServerApi;
using mobile::net::WorldPoint;

static int g_failures = 0;

static void Check(bool ok, const char* what, const std::string& err = "") {
    if (ok) {
        std::printf("ok   %s\n", what);
    } else {
        std::printf("FAIL %s %s\n", what, err.c_str());
        ++g_failures;
    }
}

// Reads the character's own position through the parser the game uses. A hand-rolled search for
// "position" is wrong here: round 4 put "gates"/"npcs"/"monsters" in the same answer, each with
// its own "position", so the first (or last) match is not the player.
static bool JsonFindPoint_ForTest(const std::string& json, mobile::net::WorldPoint& p) {
    std::vector<mobile::net::WorldRect> blocks;
    return mobile::net::ParseWorldEnter(json, p, blocks);
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

    // Character detail, not-found, logout, and the token being rejected after logout.
    err.clear();
    Check(api.CreateCharacter("Detail" + user.substr(2, 4), "female", "mage", charId, err) && charId > 0,
          "create character for detail check", err);
    err.clear();
    Check(api.GetCharacter(charId, json, err) && json.find("\"class\": \"mage\"") != std::string::npos,
          "get character detail", err);
    err.clear();
    Check(!api.GetCharacter(999999999LL, json, err) && err.find("HTTP 404") == 0, "unknown character is 404", err);
    err.clear();
    Check(api.DeleteCharacter(charId, err), "delete detail character", err);

    err.clear();
    Check(api.Logout(err), "logout", err);
    Check(!api.LoggedIn(), "token forgotten after logout");
    err.clear();
    Check(!api.ListCharacters(json, err) && err.find("HTTP 401") == 0, "list rejected after logout", err);
    err.clear();
    Check(api.Login(user, pass, err) && api.LoggedIn(), "login again after logout", err);

    // World: enter, move (accepted, rejected, blocked), leave, reconnect with the saved position.
    err.clear();
    Check(api.CreateCharacter("Walk" + user.substr(2, 4), "male", "archer", charId, err) && charId > 0,
          "create character for world check", err);
    err.clear();
    Check(api.EnterWorld(charId, json, err) && json.find("\"status\": \"entered\"") != std::string::npos,
          "enter world", err);
    Check(json.find("\"map_id\": 1") != std::string::npos && json.find("\"monsters\"") != std::string::npos,
          "enter world returns map and monsters");
    WorldPoint spawn;
    Check(JsonFindPoint_ForTest(json, spawn) && std::abs(spawn.x - 0.5) < 1e-6 && std::abs(spawn.y - 0.5) < 1e-6,
          "enter world returns the map spawn position");
    err.clear();
    WorldPoint srv{0.0, 0.0};
    auto mv = api.MoveCharacter(charId, 0.52, 0.5, srv, err);
    Check(mv == ServerApi::MoveResult::Accepted && std::abs(srv.x - 0.52) < 1e-6,
          "valid move is accepted", err);
    err.clear();
    // A second request within the minimum interval is throttled, not judged: the result is Throttled
    // and the returned position is the server's unchanged one (the move stays pending on the client).
    mv = api.MoveCharacter(charId, 0.95, 0.95, srv, err);
    Check(mv == ServerApi::MoveResult::Throttled && err == "move_too_fast" && std::abs(srv.x - 0.52) < 1e-6,
          "a move inside the minimum interval is Throttled (429 move_too_fast)", err);
    err.clear();
    std::this_thread::sleep_for(std::chrono::milliseconds(120));    // let the interval pass
    mv = api.MoveCharacter(charId, 0.95, 0.95, srv, err);
    Check(mv == ServerApi::MoveResult::Rejected && err == "position_rejected" && std::abs(srv.x - 0.52) < 1e-6,
          "teleport is rejected and the server position is returned", err);
    err.clear();
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));   // let the distance budget refill
    mv = api.MoveCharacter(charId, 0.5, 0.15, srv, err);
    Check(mv == ServerApi::MoveResult::Rejected && err == "blocked", "walking into a block is rejected", err);
    err.clear();
    Check(api.GetWorldState(charId, json, err) && json.find("\"connected\": true") != std::string::npos,
          "world state shows connected", err);
    err.clear();
    Check(api.LeaveWorld(charId, err), "leave world", err);
    err.clear();
    Check(api.GetWorldState(charId, json, err) && json.find("\"connected\": false") != std::string::npos,
          "world state after leave is disconnected", err);
    err.clear();
    // A move after leave is not a "rejected" move: the server says the session ended and returns the
    // saved position, so the client resumes instead of snapping to it.
    WorldPoint saved;
    Check(api.MoveCharacter(charId, 0.3, 0.3, saved, err) == ServerApi::MoveResult::SessionLost &&
              std::abs(saved.x - 0.52) < 1e-6 && std::abs(saved.y - 0.5) < 1e-6,
          "move after leave is SessionLost with the saved position", err);
    err.clear();
    mobile::net::WorldSession resumeSession;
    Check(mobile::net::ResumeWorld(api, charId, resumeSession, err) == mobile::net::ResumeResult::Resumed &&
              resumeSession.InWorld(),
          "ResumeWorld re-enters after the session ended", err);
    err.clear();
    Check(api.LeaveWorld(charId, err), "leave again before the enter check", err);
    err.clear();
    Check(api.EnterWorld(charId, json, err) && json.find("\"status\": \"entered\"") != std::string::npos,
          "enter again after leave", err);
    WorldPoint back;
    Check(JsonFindPoint_ForTest(json, back) && std::abs(back.x - 0.52) < 1e-6 && std::abs(back.y - 0.5) < 1e-6,
          "saved position is restored on re-enter", err);
    err.clear();
    Check(api.DeleteCharacter(charId, err), "delete world character", err);

    if (g_failures == 0) {
        std::printf("net_client: all checks passed\n");
        return 0;
    }
    std::printf("net_client: %d check(s) failed\n", g_failures);
    return 1;
}
