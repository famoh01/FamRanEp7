// mobile/net_client/test_world_flow.cpp
// Host unit tests for world_flow (the sync / reconnect / lifecycle state machine). No sockets, no
// Android: a scripted transport stands in for the server. Run: make test_flow
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "api_client.h"
#include "world_flow.h"
#include "world_state.h"

using mobile::net::ServerApi;
using mobile::net::SyncOutcome;
using mobile::net::WorldPoint;
using mobile::net::WorldSession;
using mobile::net::WorldSync;

static int g_failures = 0;

static void Check(bool ok, const char* what) {
    if (ok) {
        std::printf("ok   %s\n", what);
    } else {
        std::printf("FAIL %s\n", what);
        ++g_failures;
    }
}

// ---------------------------------------------------------------- scripted server
// Behaves like ran_mobile/Server for the calls the flow makes: world/move and world/enter.
struct FakeServer {
    bool loggedIn = true;      // token still valid (401 when false)
    bool inWorld = true;       // server-side world session exists
    bool transportUp = true;   // socket level
    bool enterFails = false;   // enter returns 500 (server up, request failing)
    double posX = 0.5, posY = 0.5;   // saved position
    int moveCalls = 0, enterCalls = 0;
    std::vector<std::string> log;

    bool Request(const std::string& method, const std::string& path, const std::string&,
                 const std::string& body, mobile::net::HttpResponse& out, std::string& err) {
        (void)method;
        if (!transportUp) {
            err = "connect failed";
            return false;
        }
        if (!loggedIn) {
            out.status = 401;
            out.body = R"({"error": {"code": "unauthorized", "message": "login expired"}})";
            return true;
        }
        const bool move = path.size() > 5 && path.rfind("/move") == path.size() - 5;
        if (move) {
            ++moveCalls;
            log.push_back("move");
            if (!inWorld) {
                out.status = 409;
                out.body = "{\"error\": {\"code\": \"not_in_world\", \"message\": \"x\"}, \"position\": " +
                           Point(posX, posY) + "}";
                return true;
            }
            // Accept anything the client sends (rules are covered by the server and live tests).
            std::string x, y;
            mobile::net::JsonFindNumber(body, "x", posX);
            mobile::net::JsonFindNumber(body, "y", posY);
            out.status = 200;
            out.body = "{\"accepted\": true, \"position\": " + Point(posX, posY) + "}";
            return true;
        }
        // world/enter
        ++enterCalls;
        log.push_back("enter");
        if (enterFails) {
            out.status = 500;
            out.body = R"({"error": {"code": "internal", "message": "boom"}})";
            return true;
        }
        inWorld = true;
        out.status = 200;
        out.body = "{\"status\": \"resumed\", \"character\": {\"id\": 7, \"position\": {\"x\": 0.9, \"y\": 0.9}}, "
                   "\"world\": {\"map_id\": 1, \"blocks\": [[0.2, 0.2, 0.1, 0.1]], \"position\": " +
                   Point(posX, posY) + "}}";
        return true;
    }

    static std::string Point(double x, double y) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "{\"x\": %.6f, \"y\": %.6f}", x, y);
        return buf;
    }
};

static bool Near(double a, double b) { return (a - b) < 1e-9 && (b - a) < 1e-9; }

// ---------------------------------------------------------------- tests

// Enter world, walk, then the server drops the session (idle timeout) during a move:
// Tick must go Reconnecting and ResumeWorld must restore the exact saved position.
static void TestSessionExpiryResume() {
    FakeServer srv;
    srv.posX = 0.52;
    srv.posY = 0.48;
    ServerApi api([&srv](const std::string& m, const std::string& p, const std::string& t,
                         const std::string& b, mobile::net::HttpResponse& o, std::string& e) {
        return srv.Request(m, p, t, b, o, e);
    });
    WorldSession session;
    WorldSync sync;
    std::string err;

    // Enter World as the Android layer does after nativeEnterWorld.
    std::string json;
    Check(api.EnterWorld(7, json, err), "enter world");
    WorldPoint start;
    std::vector<mobile::net::WorldRect> blocks;
    Check(mobile::net::ParseWorldEnter(json, start, blocks), "parse enter");
    session.Enter(start, blocks);
    sync.Attached(7);
    Check(sync.InWorld() && !sync.Reconnecting(), "attached and connected");

    // A move goes out and is accepted.
    session.Publish(WorldPoint{0.55, 0.48});
    Check(sync.Tick(api, session, 1000, err) == SyncOutcome::Continue && srv.moveCalls == 1,
          "a moved position is sent");

    // The server ends the session (idle timeout). The next tick re-enters at the saved position.
    srv.inWorld = false;
    session.Publish(WorldPoint{0.60, 0.48});
    const int entersBefore = srv.enterCalls;
    const SyncOutcome afterExpiry = sync.Tick(api, session, 2000, err);
    Check(afterExpiry == SyncOutcome::Continue && !sync.Reconnecting(),
          "expired session is resumed within the same tick");
    Check(srv.enterCalls == entersBefore + 1, "ResumeWorld called once (world/enter)");
    Check(sync.InWorld(), "back in world after resume");

    uint64_t seen = 0;
    WorldSession::Snapshot snap;
    // 0.55/0.48: the position the server accepted for the last valid move.
    Check(session.Refresh(seen, snap) && Near(snap.start.x, 0.55) && Near(snap.start.y, 0.48),
          "resume restored the server's exact saved position");
    Check(sync.Tick(api, session, 2500, err) == SyncOutcome::Continue,
          "sync continues normally after the resume");
}

// The network is down: every retry fails, waits grow, nothing is lost; when it comes back the
// position is restored and syncing continues.
static void TestReconnectBackoffAndRecovery() {
    FakeServer srv;
    srv.posX = 0.61;
    srv.posY = 0.39;
    ServerApi api([&srv](const std::string& m, const std::string& p, const std::string& t,
                         const std::string& b, mobile::net::HttpResponse& o, std::string& e) {
        return srv.Request(m, p, t, b, o, e);
    });
    WorldSession session;
    WorldSync sync;
    std::string err;
    session.Enter(WorldPoint{0.6, 0.4}, {});
    sync.Attached(7);

    // The server ends the session and every resume attempt fails with a server error (500).
    srv.inWorld = false;
    srv.enterFails = true;
    const int entersBefore = srv.enterCalls;
    session.Publish(WorldPoint{0.62, 0.4});
    Check(sync.Tick(api, session, 10000, err) == SyncOutcome::Reconnecting, "first resume fails -> reconnecting");
    Check(sync.ResumeFailures() == 1 && srv.enterCalls == entersBefore + 1, "one attempt, one failure");
    // The first attempt already happened in that tick, so the next one waits for the 500 ms backoff.
    Check(sync.Tick(api, session, 10499, err) == SyncOutcome::Reconnecting && srv.enterCalls == entersBefore + 1,
          "no attempt before the backoff passes");
    Check(sync.Tick(api, session, 10500, err) == SyncOutcome::Reconnecting && sync.ResumeFailures() == 2 &&
              srv.enterCalls == entersBefore + 2,
          "retry after 500 ms (fails again, backoff doubles)");
    // The server recovers: the next attempt (after 1 s) works and the flow is live again.
    srv.enterFails = false;
    Check(sync.Tick(api, session, 11500, err) == SyncOutcome::Continue && !sync.Reconnecting(),
          "retry after 1 s succeeds and the flow is live again");
    Check(sync.ResumeFailures() == 0, "failures reset after a successful resume");

    uint64_t seen = 0;
    WorldSession::Snapshot snap;
    session.Refresh(seen, snap);
    Check(Near(snap.start.x, 0.61) && Near(snap.start.y, 0.39), "position restored after reconnect");
}

// 401 at any point means the login ended: the token is dropped and the caller shows the login screen.
static void TestUnauthorizedPaths() {
    FakeServer srv;
    ServerApi api([&srv](const std::string& m, const std::string& p, const std::string& t,
                         const std::string& b, mobile::net::HttpResponse& o, std::string& e) {
        return srv.Request(m, p, t, b, o, e);
    });
    std::string err;
    WorldSession session;
    WorldSync sync;

    // 401 while moving.
    api.Login("user", "password", err);   // (transport fake accepts anything that is not 401)
    err.clear();
    session.Enter(WorldPoint{0.5, 0.5}, {});
    sync.Attached(7);
    srv.loggedIn = false;
    session.Publish(WorldPoint{0.52, 0.5});
    Check(sync.Tick(api, session, 1000, err) == SyncOutcome::LoginAgain, "401 on move -> login screen");
    Check(err == "unauthorized", "outcome carries the reason");
    Check(!sync.InWorld() && !session.InWorld(), "login expiry clears the local world session");
    Check(sync.Tick(api, session, 2000, err) == SyncOutcome::Stop, "after a login expiry the flow stops");

    // 401 while reconnecting (the token expired while the app was in the background).
    FakeServer srv2;
    srv2.posX = 0.5;
    ServerApi api2([&srv2](const std::string& m, const std::string& p, const std::string& t,
                           const std::string& b, mobile::net::HttpResponse& o, std::string& e) {
        return srv2.Request(m, p, t, b, o, e);
    });
    WorldSession session2;
    WorldSync sync2;
    session2.Enter(WorldPoint{0.5, 0.5}, {});
    sync2.Attached(9);
    srv2.inWorld = false;
    srv2.loggedIn = false;
    session2.Publish(WorldPoint{0.53, 0.5});
    Check(sync2.Tick(api2, session2, 1000, err) == SyncOutcome::LoginAgain, "401 while resuming -> login");

    // A 500 on resume is retried, not treated as a login problem.
    FakeServer srv3;
    ServerApi api3([&srv3](const std::string& m, const std::string& p, const std::string& t,
                           const std::string& b, mobile::net::HttpResponse& o, std::string& e) {
        return srv3.Request(m, p, t, b, o, e);
    });
    WorldSession session3;
    WorldSync sync3;
    session3.Enter(WorldPoint{0.5, 0.5}, {});
    sync3.Attached(5);
    srv3.inWorld = false;
    srv3.enterFails = true;
    session3.Publish(WorldPoint{0.54, 0.5});
    Check(sync3.Tick(api3, session3, 1000, err) == SyncOutcome::Reconnecting, "server error on resume retries");
    srv3.enterFails = false;
    Check(sync3.Tick(api3, session3, 1600, err) == SyncOutcome::Continue && sync3.InWorld(),
          "resume succeeds once the server recovers");
}

// App lifecycle: no requests while paused; on resume the reconnect attempt is immediate.
static void TestPauseResume() {
    FakeServer srv;
    ServerApi api([&srv](const std::string& m, const std::string& p, const std::string& t,
                         const std::string& b, mobile::net::HttpResponse& o, std::string& e) {
        return srv.Request(m, p, t, b, o, e);
    });
    WorldSession session;
    WorldSync sync;
    std::string err;
    session.Enter(WorldPoint{0.5, 0.5}, {});
    sync.Attached(7);

    session.Publish(WorldPoint{0.52, 0.5});
    sync.Pause();
    const int before = srv.moveCalls;
    Check(sync.Tick(api, session, 1000, err) == SyncOutcome::Continue && srv.moveCalls == before,
          "no move request while paused");
    sync.Resumed();
    Check(sync.Tick(api, session, 1100, err) == SyncOutcome::Continue && srv.moveCalls == before + 1,
          "the pending move is sent after resume");

    // Reconnect scheduled far in the future (10 s cap), then backgrounded and back: retry at once.
    srv.inWorld = false;
    srv.enterFails = true;
    session.Publish(WorldPoint{0.7, 0.5});
    int t = 5000;
    Check(sync.Tick(api, session, t, err) == SyncOutcome::Reconnecting, "server error -> reconnecting");
    for (int i = 0; i < 12; ++i) {          // push the backoff to its 10 s cap
        t += 10000;
        sync.Tick(api, session, t, err);
    }
    Check(sync.ResumeFailures() >= 10, "backoff reached its cap");
    // While paused, no resume attempts happen at all.
    sync.Pause();
    const int entersWhilePaused = srv.enterCalls;
    for (int i = 0; i < 50; ++i) {
        t += 100;
        Check(sync.Tick(api, session, t, err) == SyncOutcome::Reconnecting, "paused while reconnecting");
    }
    Check(srv.enterCalls == entersWhilePaused, "no requests while paused and reconnecting");
    // Back to the foreground with the server healthy: immediate resume, no 10 s wait.
    srv.enterFails = false;
    srv.inWorld = false;
    sync.Resumed();
    Check(sync.Tick(api, session, t + 1, err) == SyncOutcome::Continue && !sync.Reconnecting(),
          "resume after pause retries immediately");

    // Leaving while in the world stops the flow for good.
    sync.Detached();
    Check(!sync.InWorld() && sync.Tick(api, session, t + 2000, err) == SyncOutcome::Stop,
          "detached flow stops");
}

// The event stream the Android layer logs (the CI smoke test greps these names).
static void TestEvents() {
    FakeServer srv;
    srv.posX = 0.52;
    ServerApi api([&srv](const std::string& m, const std::string& p, const std::string& t,
                         const std::string& b, mobile::net::HttpResponse& o, std::string& e) {
        return srv.Request(m, p, t, b, o, e);
    });
    WorldSession session;
    WorldSync sync;
    std::string err;
    Check(sync.LastEvent() == mobile::net::SyncEvent::None, "no event before anything happened");
    session.Enter(WorldPoint{0.5, 0.5}, {});
    sync.Attached(7);
    session.Publish(WorldPoint{0.52, 0.5});
    sync.Tick(api, session, 1000, err);
    Check(sync.LastEvent() == mobile::net::SyncEvent::MoveSent, "accepted move -> move_sent");
    Check(std::string(mobile::net::SyncEventName(sync.LastEvent())) == "move_sent", "event name");

    srv.inWorld = false;
    session.Publish(WorldPoint{0.54, 0.5});
    sync.Tick(api, session, 2000, err);
    Check(sync.LastEvent() == mobile::net::SyncEvent::Resumed && sync.Resumes() == 1,
          "session lost and resumed in one tick -> resumed (resume counter)");
    // Two ticks so far (accepted move, then the expired session), one of them found the loss.
    Check(sync.ResumesLost() == 1 && sync.Ticks() == 2,
          "the flow counts the lost session and its ticks (the platform logs the order)");

    srv.inWorld = false;
    srv.enterFails = true;
    session.Publish(WorldPoint{0.56, 0.5});
    sync.Tick(api, session, 3000, err);
    Check(sync.LastEvent() == mobile::net::SyncEvent::ResumeFailed, "failed attempt -> resume_failed");

    // Each event name is distinct and non-empty (stable strings for log parsing).
    const mobile::net::SyncEvent all[] = {
        mobile::net::SyncEvent::MoveSent, mobile::net::SyncEvent::MoveRejected,
        mobile::net::SyncEvent::Throttled, mobile::net::SyncEvent::MoveFailed,
        mobile::net::SyncEvent::SessionLost, mobile::net::SyncEvent::ResumeFailed,
        mobile::net::SyncEvent::Resumed, mobile::net::SyncEvent::LoginExpired};
    bool distinct = true;
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); ++i) {
        for (size_t j = i + 1; j < sizeof(all) / sizeof(all[0]); ++j) {
            if (std::string(mobile::net::SyncEventName(all[i])) ==
                std::string(mobile::net::SyncEventName(all[j]))) distinct = false;
        }
    }
    Check(distinct, "every event has its own name");
}

// A throttled move (429) is not a lost session and does not snap the player.
static void TestThrottledIsNotSessionLost() {
    int calls = 0;
    bool throttledOnce = false;
    ServerApi api([&](const std::string&, const std::string& path, const std::string&,
                      const std::string&, mobile::net::HttpResponse& out, std::string& err) {
        (void)path;
        (void)err;
        ++calls;
        if (!throttledOnce) {
            throttledOnce = true;
            out.status = 429;
            out.body = R"({"error": {"code": "move_too_fast", "message": "x"}, "retry_after_ms": 40})";
        } else {
            out.status = 200;
            out.body = R"({"accepted": true, "position": {"x": 0.53, "y": 0.5}})";
        }
        return true;
    });
    WorldSession session;
    WorldSync sync;
    std::string err;
    session.Enter(WorldPoint{0.5, 0.5}, {});
    sync.Attached(7);
    session.Publish(WorldPoint{0.53, 0.5});
    Check(sync.Tick(api, session, 1000, err) == SyncOutcome::Continue && !sync.Reconnecting(),
          "429 is not a lost session");
    Check(sync.Tick(api, session, 1100, err) == SyncOutcome::Continue && calls == 2,
          "the throttled move is retried later");
    WorldPoint corr{};
    Check(!session.TakeCorrection(corr), "throttling never snaps the player");
}

int main() {
    TestSessionExpiryResume();
    TestReconnectBackoffAndRecovery();
    TestUnauthorizedPaths();
    TestPauseResume();
    TestThrottledIsNotSessionLost();
    TestEvents();
    if (g_failures == 0) {
        std::printf("world_flow: all checks passed\n");
        return 0;
    }
    std::printf("world_flow: %d check(s) failed\n", g_failures);
    return 1;
}
