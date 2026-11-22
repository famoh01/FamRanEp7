// mobile/net_client/test_world_map.cpp
// Host tests for world_map: collision, movement rules, and agreement with the server's collision.
// Usage: test_world_map [path/to/collision_vectors.txt]   (make test_world runs it)
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "world_map.h"

using mobile::net::CircleBlocked;
using mobile::net::MapSize;
using mobile::net::StepInput;
using mobile::net::StepPlayer;
using mobile::net::WorldPoint;
using mobile::net::WorldRect;

static int g_failures = 0;

static void Check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

static const MapSize kScreen{1000.0, 600.0};  // a landscape phone, in pixels
static const std::vector<WorldRect> kBlocks = {
    {0.20, 0.22, 0.10, 0.18},
    {0.70, 0.55, 0.12, 0.16},
    {0.42, 0.12, 0.16, 0.06},
};

static void TestCollision() {
    // Circle centre inside the block, touching it, and far away.
    const WorldRect b{0.2, 0.2, 0.1, 0.1};   // px: x 200..300, y 120..180
    Check(mobile::net::CircleHitsBlock(0.25, 0.25, 5.0, b, kScreen), "centre inside block hits");
    Check(mobile::net::CircleHitsBlock(0.31, 0.25, 12.0, b, kScreen), "circle reaching the edge hits");
    Check(!mobile::net::CircleHitsBlock(0.31, 0.25, 5.0, b, kScreen), "circle short of the edge misses");
    Check(!mobile::net::CircleHitsBlock(0.9, 0.9, 18.0, b, kScreen), "far circle misses");
    // Radius counts in pixels, not fractions: the same fraction offset means a different distance on a wider screen.
    const MapSize wide{2000.0, 600.0};
    Check(mobile::net::CircleHitsBlock(0.31, 0.25, 12.0, b, kScreen) ==
              mobile::net::CircleHitsBlock(0.31, 0.25, 12.0, b, kScreen),
          "collision is deterministic");
    Check(!mobile::net::CircleHitsBlock(0.31, 0.25, 12.0, b, wide), "same fraction, wider screen: no hit");
}

// Reads "block x y w h" and "point x y expect" lines; comments start with '#'.
static bool LoadVectors(const std::string& path, std::vector<WorldRect>& blocks,
                        std::vector<std::pair<WorldPoint, bool>>& points) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string kind;
        ss >> kind;
        if (kind == "block") {
            WorldRect r{};
            ss >> r.x >> r.y >> r.w >> r.h;
            if (ss.fail()) return false;
            blocks.push_back(r);
        } else if (kind == "point") {
            WorldPoint p{};
            int expect = -1;
            ss >> p.x >> p.y >> expect;
            if (ss.fail() || (expect != 0 && expect != 1)) return false;
            points.push_back({p, expect == 1});
        } else {
            return false;
        }
    }
    return !blocks.empty() && !points.empty();
}

static void TestSharedVectors(const std::string& path) {
    std::vector<WorldRect> blocks;
    std::vector<std::pair<WorldPoint, bool>> points;
    const bool loaded = LoadVectors(path, blocks, points);
    Check(loaded, "collision vectors file loads");
    if (!loaded) return;
    int bad = 0;
    for (const auto& pt : points) {
        // Radius 0.5 px: the test asks about the point itself, not about a body around it.
        const bool blocked = CircleBlocked(blocks, pt.first.x, pt.first.y, 0.5, kScreen);
        if (blocked != pt.second) {
            std::printf("     point (%.2f, %.2f): client blocked=%d expected=%d\n", pt.first.x, pt.first.y,
                        blocked, pt.second);
            ++bad;
        }
    }
    Check(bad == 0, "client collision agrees with the shared vectors");
}

static void TestWalkSpeed() {
    // One second of walking on an empty map covers the walk speed in fractions (the server's speed unit).
    WorldPoint p{0.1, 0.5};
    StepInput in;
    in.joyX = 1.0;
    in.dtSec = 1.0 / 60.0;
    for (int i = 0; i < 60; ++i) p = StepPlayer(p, in, {}, kScreen);
    const double walked = p.x - 0.1;
    Check(std::fabs(walked - mobile::net::kWalkSpeed) < 0.01, "walking covers about kWalkSpeed per second");

    WorldPoint r{0.1, 0.5};
    in.running = true;
    for (int i = 0; i < 60; ++i) r = StepPlayer(r, in, {}, kScreen);
    Check(std::fabs((r.x - 0.1) - mobile::net::kRunSpeed) < 0.01, "running covers about kRunSpeed per second");
}

static void TestDiagonalNotFaster() {
    StepInput straight;
    straight.joyX = 1.0;
    straight.dtSec = 0.05;
    StepInput diag;
    diag.joyX = 1.0;
    diag.joyY = 1.0;   // magnitude 1.41 before clamping
    diag.dtSec = 0.05;
    const WorldPoint s = StepPlayer({0.3, 0.3}, straight, {}, kScreen);
    const WorldPoint d = StepPlayer({0.3, 0.3}, diag, {}, kScreen);
    const double ds = std::hypot(s.x - 0.3, s.y - 0.3);
    const double dd = std::hypot(d.x - 0.3, d.y - 0.3);
    Check(dd <= ds + 1e-9, "diagonal push is not faster than a straight push");
}

static void TestTimeStepClamped() {
    StepInput hitch;
    hitch.joyX = 1.0;
    hitch.dtSec = 5.0;   // a paused frame
    const WorldPoint p = StepPlayer({0.1, 0.5}, hitch, {}, kScreen);
    Check(p.x - 0.1 <= mobile::net::kWalkSpeed * mobile::net::kMaxStepSec + 1e-9,
          "a long frame moves at most kMaxStepSec worth of distance");
}

static void TestSpeedNeverAboveServerLimit() {
    // Random joystick input at the highest speed. Every step must stay under the server's rule:
    // distance <= MAX_SPEED * dt + SLACK, with the server's dt capped at 1 s. The step cap is kMaxStepSec.
    const double server_max_speed = 0.45;
    unsigned seed = 12345;
    auto rnd = [&seed]() {
        seed = seed * 1103515245u + 12345u;
        return ((seed >> 16) & 0x7fff) / 32767.0;
    };
    WorldPoint p{0.5, 0.5};
    bool ok = true;
    for (int i = 0; i < 5000; ++i) {
        StepInput in;
        in.joyX = rnd() * 2 - 1;
        in.joyY = rnd() * 2 - 1;
        in.running = true;
        in.dtSec = 0.1;   // the sync interval the server sees
        const WorldPoint q = StepPlayer(p, in, kBlocks, kScreen);
        const double dist = std::hypot(q.x - p.x, q.y - p.y);
        if (dist > server_max_speed * 0.1 + 0.03 + 1e-9) ok = false;
        if (q.x < 0 || q.x > 1 || q.y < 0 || q.y > 1) ok = false;
        // The player never ends up inside a block.
        if (CircleBlocked(kBlocks, q.x, q.y, 0.5, kScreen) && !CircleBlocked(kBlocks, p.x, p.y, 0.5, kScreen))
            ok = false;
        p = q;
    }
    Check(ok, "5000 random steps stay under the server's speed rule, in the map, and out of blocks");
}

static void TestSlideAlongBlock() {
    // Block 0 spans x 0.20..0.30 and y 0.22..0.40. Walking right at y 0.30 hits its left face.
    StepInput in;
    in.joyX = 1.0;
    in.dtSec = 0.1;
    WorldPoint p{0.10, 0.30};
    for (int i = 0; i < 30; ++i) p = StepPlayer(p, in, kBlocks, kScreen);
    Check(p.x > 0.10 && p.x < 0.20, "the block stops the player on its left face");
    Check(!CircleBlocked(kBlocks, p.x, p.y, 18.0, kScreen), "stopped position is outside the block (radius 18 px)");
    // Pushing diagonally down-right against the same face: x is blocked, y keeps moving down the face.
    StepInput slide;
    slide.joyX = 1.0;
    slide.joyY = 1.0;
    slide.dtSec = 0.1;
    WorldPoint q{0.15, 0.25};
    const double startY = q.y;
    for (int i = 0; i < 4; ++i) q = StepPlayer(q, slide, kBlocks, kScreen);
    Check(q.x < 0.20, "diagonal push does not pass through the wall");
    Check(q.y > startY, "pushing along a wall still moves on the free axis");
}

static void TestInvalidInput() {
    const WorldPoint p{0.4, 0.4};
    StepInput nan;
    nan.joyX = std::nan("");
    nan.dtSec = 0.1;
    const WorldPoint a = StepPlayer(p, nan, {}, kScreen);
    Check(a.x == p.x && a.y == p.y, "NaN joystick leaves the position unchanged");
    StepInput ok;
    ok.joyX = 1.0;
    ok.dtSec = 0.1;
    const WorldPoint b = StepPlayer(p, ok, {}, MapSize{0.0, 600.0});
    Check(b.x == p.x && b.y == p.y, "zero-size screen leaves the position unchanged");
}

static void TestMarginKeepsPlayerOnScreen() {
    StepInput in;
    in.joyX = -1.0;
    in.dtSec = 0.1;
    in.marginPx = 40.0;
    WorldPoint p{0.5, 0.5};
    for (int i = 0; i < 200; ++i) p = StepPlayer(p, in, {}, kScreen);
    Check(std::fabs(p.x - 40.0 / kScreen.widthPx) < 1e-9, "player stops at the margin, not off screen");
}

int main(int argc, char** argv) {
    const std::string vectors = argc > 1 ? argv[1] : "../../ran_mobile/Database/fixtures/collision_vectors.txt";
    TestCollision();
    TestSharedVectors(vectors);
    TestWalkSpeed();
    TestDiagonalNotFaster();
    TestTimeStepClamped();
    TestSpeedNeverAboveServerLimit();
    TestSlideAlongBlock();
    TestInvalidInput();
    TestMarginKeepsPlayerOnScreen();
    if (g_failures == 0) {
        std::printf("world_map: all checks passed\n");
        return 0;
    }
    std::printf("world_map: %d check(s) failed\n", g_failures);
    return 1;
}
