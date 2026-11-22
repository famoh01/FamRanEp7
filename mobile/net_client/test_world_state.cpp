// mobile/net_client/test_world_state.cpp
// Host unit tests for world_state (no server, no Android). Run: make test_world
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "api_client.h"
#include "world_state.h"

using mobile::net::ParseWorldEnter;
using mobile::net::WorldPoint;
using mobile::net::WorldRect;
using mobile::net::WorldSession;

static int g_failures = 0;

static void Check(bool ok, const char* what) {
    if (ok) {
        std::printf("ok   %s\n", what);
    } else {
        std::printf("FAIL %s\n", what);
        ++g_failures;
    }
}

static bool Near(double a, double b) {
    return std::fabs(a - b) < 1e-9;
}

// The shape the server sends for POST /api/characters/{id}/world/enter.
static const char* kEnterJson =
    "{\"status\": \"entered\", \"character\": {\"id\": 7, \"name\": \"Walker1\", \"map_id\": 1, "
    "\"position\": {\"x\": 0.9, \"y\": 0.9}, \"in_world\": true}, "
    "\"world\": {\"map_id\": 1, \"map_name\": \"test\", \"spawn\": {\"x\": 0.5, \"y\": 0.5}, "
    "\"blocks\": [[0.42, 0.12, 0.16, 0.06], [0.2, 0.22, 0.1, 0.18]], "
    "\"position\": {\"x\": 0.6, \"y\": 0.45}, "
    "\"monsters\": [{\"monster_id\": 1, \"position\": {\"x\": 0.15, \"y\": 0.75}}], \"connected\": true}}";

static void TestParse() {
    WorldPoint start{};
    std::vector<WorldRect> blocks;
    Check(ParseWorldEnter(kEnterJson, start, blocks), "parse enter response");
    // The character's own position (0.9) is not the start; the world position (0.6, 0.45) is.
    Check(Near(start.x, 0.6) && Near(start.y, 0.45), "start is world.position, not character.position");
    Check(blocks.size() == 2, "two blocks parsed");
    Check(blocks.size() == 2 && Near(blocks[0].x, 0.42) && Near(blocks[0].y, 0.12) && Near(blocks[0].w, 0.16) &&
              Near(blocks[0].h, 0.06),
          "first block values");
    Check(blocks.size() == 2 && Near(blocks[1].x, 0.2) && Near(blocks[1].h, 0.18), "second block values");

    std::vector<WorldRect> none{WorldRect{1, 1, 1, 1}};
    Check(ParseWorldEnter("{\"world\": {\"position\": {\"x\": 0.1, \"y\": 0.2}, \"blocks\": []}}", start, none) &&
              none.empty() && Near(start.x, 0.1),
          "empty blocks array is valid");

    Check(!ParseWorldEnter("{\"status\": \"entered\"}", start, blocks), "missing world object is rejected");
    Check(!ParseWorldEnter("{\"world\": {\"blocks\": []}}", start, blocks), "missing position is rejected");
    Check(!ParseWorldEnter("{\"world\": {\"position\": {\"x\": 0.1, \"y\": 0.2}}}", start, blocks),
          "missing blocks is rejected");
    Check(!ParseWorldEnter("{\"world\": {\"position\": {\"x\": 1.5, \"y\": 0.2}, \"blocks\": []}}", start, blocks),
          "position out of range is rejected");
    Check(!ParseWorldEnter("{\"world\": {\"position\": {\"x\": 0.1, \"y\": 0.2}, \"blocks\": [[0.1, 0.2, 0.3]]}}",
                           start, blocks),
          "block with three numbers is rejected");
    Check(!ParseWorldEnter("{\"world\": {\"position\": {\"x\": 0.1, \"y\": 0.2}, \"blocks\": [[0.1, 0.2, 0.3, 0.4]",
                           start, blocks),
          "truncated blocks array is rejected");
}

static void TestSpeeds() {
    // The client never exceeds the server's limit, even on a diagonal at full run speed.
    const double server_max = 0.45;
    Check(mobile::net::kRunSpeed < server_max && mobile::net::kWalkSpeed < mobile::net::kRunSpeed,
          "walk < run < server limit");
    const double per_tick = mobile::net::kRunSpeed * mobile::net::kMoveIntervalMs / 1000.0;
    Check(per_tick < server_max * mobile::net::kMoveIntervalMs / 1000.0 + 0.03,
          "a sync tick is inside the server's distance allowance");
}

static void TestSession() {
    WorldSession s;
    WorldPoint p{};
    Check(!s.InWorld(), "not in world before enter");
    Check(!s.PendingMove(p), "no move before enter");

    WorldPoint start{0.5, 0.5};
    s.Enter(start, {WorldRect{0.42, 0.12, 0.16, 0.06}});
    Check(s.InWorld(), "in world after enter");
    Check(!s.PendingMove(p), "no move right after enter (same position)");

    // Refresh copies the snapshot once per version.
    uint64_t seen = 0;
    WorldSession::Snapshot snap;
    Check(s.Refresh(seen, snap) && snap.inWorld && snap.blocks.size() == 1 && Near(snap.start.x, 0.5),
          "render thread sees enter snapshot");
    Check(!s.Refresh(seen, snap), "no copy when version unchanged");

    // A moved render position becomes a pending move, and the accepted one is remembered.
    s.Publish(WorldPoint{0.52, 0.5});
    Check(s.PendingMove(p) && Near(p.x, 0.52), "moved position is pending");
    s.MoveAccepted(p);
    Check(!s.PendingMove(p), "accepted position is not sent again");

    // Tiny jitter below the threshold is not sent.
    s.Publish(WorldPoint{0.52 + 0.0001, 0.5});
    Check(!s.PendingMove(p), "sub-threshold change is not sent");

    // Server refusal: correction is applied once and becomes the published position.
    s.Publish(WorldPoint{0.95, 0.95});
    Check(s.PendingMove(p), "teleport is pending");
    s.MoveRejected(WorldPoint{0.52, 0.5});
    Check(!s.PendingMove(p), "no move is sent while a correction is pending");
    WorldPoint corr{};
    Check(s.TakeCorrection(corr) && Near(corr.x, 0.52) && Near(corr.y, 0.5), "correction is delivered");
    Check(!s.TakeCorrection(corr), "correction is delivered once");
    Check(!s.PendingMove(p), "after correction the published position equals the server position");

    // Leave clears the session; the render thread sees the change and no move is sent.
    s.Leave();
    Check(!s.InWorld(), "not in world after leave");
    Check(s.Refresh(seen, snap) && !snap.inWorld && snap.blocks.empty(), "render thread sees leave");
    s.Publish(WorldPoint{0.7, 0.7});
    Check(!s.PendingMove(p), "no move after leave");

    // Re-enter starts from the saved position.
    s.Enter(WorldPoint{0.6, 0.45}, {});
    Check(s.Refresh(seen, snap) && snap.inWorld && Near(snap.start.x, 0.6), "re-enter uses the saved position");
}

// Fake transport for the server calls. `mode` picks the answer for every request.
enum class FakeMode { Enter, TransportError, Unauthorized, NotInWorld, Blocked, TooFast };
static FakeMode g_mode = FakeMode::Enter;
static int g_calls = 0;

static bool FakeRequest(const std::string& method, const std::string& path, const std::string& token,
                        const std::string& body, mobile::net::HttpResponse& out, std::string& err) {
    (void)method; (void)path; (void)token; (void)body;
    ++g_calls;
    switch (g_mode) {
        case FakeMode::TransportError:
            err = "connect failed";
            return false;
        case FakeMode::Unauthorized:
            out.status = 401;
            out.body = "{\"error\": {\"code\": \"unauthorized\", \"message\": \"login expired\"}}";
            return true;
        case FakeMode::NotInWorld:
            out.status = 409;
            out.body = "{\"error\": {\"code\": \"not_in_world\", \"message\": \"x\"}, \"position\": {\"x\": 0.33, \"y\": 0.61}}";
            return true;
        case FakeMode::TooFast:
            out.status = 429;
            out.body = "{\"error\": {\"code\": \"move_too_fast\", \"message\": \"x\"}, "
                       "\"position\": {\"x\": 0.5, \"y\": 0.5}, \"retry_after_ms\": 40}";
            return true;
        case FakeMode::Blocked:
            out.status = 409;
            out.body = "{\"error\": {\"code\": \"blocked\", \"message\": \"x\"}, \"position\": {\"x\": 0.5, \"y\": 0.5}}";
            return true;
        case FakeMode::Enter:
            out.status = 200;
            out.body = kEnterJson;
            return true;
    }
    return false;
}

static void TestResume() {
    using mobile::net::ResumeResult;
    using mobile::net::ServerApi;
    ServerApi api(FakeRequest);
    WorldSession s;
    std::string err;

    // Resumed: the session takes the saved position and blocks from the server.
    g_mode = FakeMode::Enter;
    Check(mobile::net::ResumeWorld(api, 7, s, err) == ResumeResult::Resumed && err.empty(), "resume succeeds");
    WorldSession::Snapshot snap;
    uint64_t seen = 0;
    s.Refresh(seen, snap);
    Check(snap.inWorld && Near(snap.start.x, 0.6) && Near(snap.start.y, 0.45) && snap.blocks.size() == 2,
          "resume uses the saved position and blocks");

    // Transport error: the session is left as it was (here: in world from the call above).
    g_mode = FakeMode::TransportError;
    Check(mobile::net::ResumeWorld(api, 7, s, err) == ResumeResult::Failed && !err.empty(),
          "transport error is a failed resume");
    Check(s.InWorld(), "failed resume keeps the existing session");

    // A failed resume does not touch a session that was not in the world.
    s.Leave();
    Check(mobile::net::ResumeWorld(api, 7, s, err) == ResumeResult::Failed && !s.InWorld(),
          "failed resume does not enter the world");

    // 401: the login is over, the caller must send the user to login.
    g_mode = FakeMode::Unauthorized;
    Check(mobile::net::ResumeWorld(api, 7, s, err) == ResumeResult::Unauthorized, "401 is reported as unauthorized");
    Check(!s.InWorld(), "401 leaves the session out of the world");
}

static void TestMoveMapping() {
    using mobile::net::ServerApi;
    ServerApi api(FakeRequest);
    WorldPoint pos{};
    std::string err;

    g_mode = FakeMode::NotInWorld;
    Check(api.MoveCharacter(7, 0.5, 0.5, pos, err) == ServerApi::MoveResult::SessionLost,
          "409 not_in_world is SessionLost (resume, do not snap)");
    Check(Near(pos.x, 0.33) && Near(pos.y, 0.61), "SessionLost carries the saved position");

    g_mode = FakeMode::Blocked;
    Check(api.MoveCharacter(7, 0.5, 0.5, pos, err) == ServerApi::MoveResult::Rejected && err == "blocked",
          "409 blocked is Rejected");

    g_mode = FakeMode::Unauthorized;
    Check(api.MoveCharacter(7, 0.5, 0.5, pos, err) == ServerApi::MoveResult::Unauthorized,
          "401 on move is Unauthorized");

    g_mode = FakeMode::TooFast;
    Check(api.MoveCharacter(7, 0.5, 0.5, pos, err) == ServerApi::MoveResult::Throttled &&
              err == "move_too_fast",
          "429 move_too_fast is Throttled (retry later, do not snap)");

    g_mode = FakeMode::TransportError;
    Check(api.MoveCharacter(7, 0.5, 0.5, pos, err) == ServerApi::MoveResult::Failed, "transport error is Failed");
}

// The client's own minimum interval between move requests (world.MIN_MOVE_INTERVAL_MS on the server).
static void TestMoveInterval() {
    WorldSession s;
    WorldPoint p{};
    s.Enter(WorldPoint{0.5, 0.5}, {});
    s.Publish(WorldPoint{0.52, 0.5});
    Check(s.PendingMove(p, 1000) && Near(p.x, 0.52), "first move is due");
    s.MoveAccepted(p, 1000);
    s.Publish(WorldPoint{0.54, 0.5});
    Check(!s.PendingMove(p, 1000 + mobile::net::kMinMoveGapMs - 1),
          "a move inside the minimum interval is not sent (no 429 from the server)");
    Check(s.PendingMove(p, 1000 + mobile::net::kMinMoveGapMs), "the move is due once the interval passed");
    Check(Near(p.x, 0.54), "the waiting move keeps its position");

    // A throttled request changes nothing but the request time: the same position stays pending.
    s.MoveThrottled(1000 + mobile::net::kMinMoveGapMs);
    Check(!s.PendingMove(p, 1000 + mobile::net::kMinMoveGapMs + 1), "throttled request also waits");
    Check(s.PendingMove(p, 1000 + 2 * mobile::net::kMinMoveGapMs + 1), "and is retried afterwards");
    WorldPoint corr{};
    Check(!s.TakeCorrection(corr), "a throttled move is not a correction (no snap)");

    // ForceNextMove makes the standing position pending once (status probe without walking).
    s.ForceNextMove();
    Check(s.PendingMove(p, 1000000) && Near(p.x, 0.54), "forced move sends the standing position");
    s.MoveAccepted(p, 1000000);
    Check(!s.PendingMove(p, 1000000 + mobile::net::kMinMoveGapMs), "after the forced move nothing is pending");

    // Timing information is optional: without it the session behaves as before (used by simple callers).
    s.Publish(WorldPoint{0.56, 0.5});
    Check(s.PendingMove(p) && Near(p.x, 0.56), "PendingMove without a clock always allows a change");
}

static void TestBackoff() {
    mobile::net::ReconnectBackoff b;
    Check(b.Due(0), "first retry is due at once");
    b.Failed(0);
    Check(!b.Due(499) && b.Due(500), "first delay is 500 ms");
    b.Failed(500);
    Check(!b.Due(1499) && b.Due(1500), "second delay doubles to 1 s");
    int64_t now = 1500;
    for (int i = 0; i < 20; ++i) {
        b.Failed(now);
        now += 20000;   // any gap longer than the cap
    }
    Check(b.Failures() == 22, "failure count keeps growing");
    b.Failed(100000);
    Check(!b.Due(100000 + 9999) && b.Due(100000 + 10000), "delay is capped at 10 s");
    b.Reset();
    Check(b.Due(0) && b.Failures() == 0, "reset makes the next retry due at once");
}

int main() {
    TestParse();
    TestSpeeds();
    TestSession();
    TestResume();
    TestMoveMapping();
    TestMoveInterval();
    TestBackoff();
    if (g_failures == 0) {
        std::printf("world_state: all checks passed\n");
        return 0;
    }
    std::printf("world_state: %d check(s) failed\n", g_failures);
    return 1;
}
