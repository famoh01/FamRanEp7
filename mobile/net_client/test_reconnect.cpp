// mobile/net_client/test_reconnect.cpp
// Live test against a running RAN MOBILE server, for the cases a unit test cannot prove:
//   1. session expires on the server (idle timeout) -> the next move gets 409 not_in_world
//      -> WorldSync resumes -> the character is back at the exact position the server saved
//   2. app lifecycle: while paused the client sends no move requests; after resume it continues
//   3. 401: the login token is invalidated on the server -> the flow reports LoginAgain and stops
//
// Usage: ./test_reconnect <host> <port> <world-idle-ms>
// The server must be started with --world-idle-s matching/world-idle-ms (see the Makefile).
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "api_client.h"
#include "http_client.h"
#include "world_flow.h"
#include "world_state.h"

using mobile::net::HttpResponse;
using mobile::net::ServerApi;
using mobile::net::SyncOutcome;
using mobile::net::WorldPoint;
using mobile::net::WorldSession;
using mobile::net::WorldSync;

static int g_failures = 0;

static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    if (ok) {
        std::printf("ok   %s\n", what.c_str());
    } else {
        std::printf("FAIL %s %s\n", what.c_str(), detail.c_str());
        ++g_failures;
    }
}

static int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

static void SleepMs(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// Sends a position through the flow and returns the outcome. The render side publishes first.
static SyncOutcome Step(WorldSync& sync, ServerApi& api, WorldSession& session, double x, double y,
                        int settleMs = 130) {
    session.Publish(WorldPoint{x, y});
    SleepMs(settleMs);   // the client's minimum interval (50 ms) and the server's move window
    std::string err;
    return sync.Tick(api, session, NowMs(), err);
}

// Reads the character's position straight from the server (independent of the client state).
static bool ServerPosition(ServerApi& api, long long cid, WorldPoint& out) {
    std::string json, err;
    if (!api.GetWorldState(cid, json, err)) return false;
    const size_t pos = json.find("\"position\"");
    if (pos == std::string::npos) return false;
    double x = 0.0, y = 0.0;
    if (!mobile::net::JsonFindNumber(json.substr(pos), "x", x)) return false;
    if (!mobile::net::JsonFindNumber(json.substr(pos), "y", y)) return false;
    out = WorldPoint{x, y};
    return true;
}

static bool Same(const WorldPoint& a, const WorldPoint& b) {
    return std::fabs(a.x - b.x) < 1e-6 && std::fabs(a.y - b.y) < 1e-6;
}

static std::string Str(const WorldPoint& p) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "(%.6f, %.6f)", p.x, p.y);
    return buf;
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::printf("usage: %s <host> <port> <world-idle-ms>\n", argv[0]);
        return 2;
    }
    const std::string host = argv[1];
    const int port = std::atoi(argv[2]);
    const int idleMs = std::atoi(argv[3]);
    if (idleMs < 1000) {
        std::printf("world-idle-ms must be at least 1000 (got %d)\n", idleMs);
        return 2;
    }

    mobile::net::HttpClient http(host, port, 10);
    ServerApi api(http);
    std::string err;

    // Unique account so repeated runs do not collide.
    const std::string user = "Recon" + std::to_string(NowMs() % 100000);
    const std::string pass = "reconnect-pass-1";
    if (!api.Register(user, pass, err)) {
        std::printf("FAIL register %s\n", err.c_str());
        return 1;
    }
    Check(api.Login(user, pass, err), "login", err);
    long long cid = 0;
    Check(api.CreateCharacter("Rec" + user.substr(5, 5), "male", "fighter", cid, err) && cid > 0,
          "create character", err);

    // ---- 1. enter world, walk, let the session expire, resume at the saved position ----
    std::string json;
    Check(api.EnterWorld(cid, json, err), "enter world", err);
    WorldPoint start;
    std::vector<mobile::net::WorldRect> blocks;
    Check(mobile::net::ParseWorldEnter(json, start, blocks), "parse enter response");
    WorldSession session;
    session.Enter(start, blocks);
    WorldSync sync;
    sync.Attached(cid);
    std::string tickErr;
    Check(sync.Tick(api, session, NowMs(), tickErr) == SyncOutcome::Continue, "first tick continues", tickErr);

    Check(Step(sync, api, session, 0.51, 0.5) == SyncOutcome::Continue, "walk step 1 accepted");
    Check(Step(sync, api, session, 0.52, 0.505) == SyncOutcome::Continue, "walk step 2 accepted");
    WorldPoint saved;
    const bool walked = ServerPosition(api, cid, saved) && std::fabs(saved.x - 0.52) < 1e-6;
    Check(walked, "server has the walked position " + Str(saved));

    // No requests at all for longer than the server's idle timeout: the session expires there.
    SleepMs(idleMs + 1500);
    // The next move must get 409 not_in_world and the flow must resume (world/enter) in the same tick.
    session.Publish(WorldPoint{0.54, 0.505});
    SleepMs(130);
    const SyncOutcome afterExpiry = sync.Tick(api, session, NowMs(), tickErr);
    Check(afterExpiry == SyncOutcome::Continue && !sync.Reconnecting(),
          "expired session resumed in the same tick", tickErr);
    Check(sync.InWorld(), "flow is live again after the resume");
    WorldSession::Snapshot snap;
    uint64_t seen = 0;
    Check(session.Refresh(seen, snap) && Same(snap.start, saved),
          "character is back at the exact saved position " + Str(saved));
    WorldPoint afterResume;
    const bool kept = ServerPosition(api, cid, afterResume) && Same(afterResume, saved);
    Check(kept, "server kept the saved position during the resume " + Str(afterResume));

    // Movement works after the resume.
    Check(Step(sync, api, session, 0.56, 0.505) == SyncOutcome::Continue, "walk after resume accepted");
    WorldPoint moved;
    const bool sawMove = ServerPosition(api, cid, moved) && std::fabs(moved.x - 0.56) < 1e-6;
    Check(sawMove, "server sees the new move " + Str(moved));

    // ---- 2. lifecycle: paused means no requests at all ----
    sync.Pause();
    const WorldPoint beforePause = moved;
    for (int i = 0; i < 5; ++i) {
        session.Publish(WorldPoint{0.60 + 0.01 * i, 0.505});
        SleepMs(120);
        Check(sync.Tick(api, session, NowMs(), tickErr) == SyncOutcome::Continue, "tick while paused continues");
    }
    WorldPoint whilePaused;
    const bool stayed = ServerPosition(api, cid, whilePaused) && Same(whilePaused, beforePause);
    Check(stayed, "no move reached the server while paused " + Str(whilePaused));
    sync.Resumed();
    SleepMs(130);
    Check(sync.Tick(api, session, NowMs(), tickErr) == SyncOutcome::Continue, "tick after resume");
    WorldPoint afterPause;
    const bool sentAfterPause = ServerPosition(api, cid, afterPause) && std::fabs(afterPause.x - 0.64) < 1e-6;
    Check(sentAfterPause, "the position held while paused is sent after resume " + Str(afterPause));

    // ---- 3. 401: the token is invalidated on the server (e.g. logout elsewhere) ----
    HttpResponse r;
    Check(http.Request("POST", "/api/auth/logout", api.Token(), "", r, err) && r.status == 200,
          "token invalidated on the server", err);
    session.Publish(WorldPoint{0.66, 0.505});
    SleepMs(130);
    const SyncOutcome after401 = sync.Tick(api, session, NowMs(), tickErr);
    Check(after401 == SyncOutcome::LoginAgain, "401 leads to the login screen", tickErr);
    Check(!sync.InWorld() && !session.InWorld(), "login expiry clears the world session");
    Check(sync.Tick(api, session, NowMs(), tickErr) == SyncOutcome::Stop, "flow stops after the login expiry");

    // The server really rejected the token (sanity check of the test itself).
    Check(http.Request("GET", "/api/characters", api.Token(), "", r, err) && r.status == 401,
          "token is no longer valid on the server", err);

    if (g_failures == 0) {
        std::printf("reconnect: all checks passed\n");
        return 0;
    }
    std::printf("reconnect: %d check(s) failed\n", g_failures);
    return 1;
}
