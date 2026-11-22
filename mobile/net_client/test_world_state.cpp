// mobile/net_client/test_world_state.cpp
// Host unit tests for world_state (no server, no Android). Run: make test_world
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

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

int main() {
    TestParse();
    TestSpeeds();
    TestSession();
    if (g_failures == 0) {
        std::printf("world_state: all checks passed\n");
        return 0;
    }
    std::printf("world_state: %d check(s) failed\n", g_failures);
    return 1;
}
