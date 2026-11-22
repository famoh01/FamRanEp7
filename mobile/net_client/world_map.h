// mobile/net_client/world_map.h
// Map logic without rendering: collision with blocks and one step of player movement.
// Positions are fractions of the map (0..1, top-left = 0,0), the same units the server checks.
// Only the player's radius and the collision test use pixels (the screen size), so the rules are
// the same on any screen. main.cpp only draws the result.
#pragma once

#include <vector>

#include "world_state.h"

namespace mobile::net {

// Screen size of the map in pixels. Must be positive.
struct MapSize {
    double widthPx = 1.0;
    double heightPx = 1.0;
};

// Longest time step used for movement (seconds). A frame hitch never produces a bigger jump than
// this, so a normal client never sends a move the server would refuse for distance.
constexpr double kMaxStepSec = 0.1;

// True if a circle (center in map fractions, radius in pixels) overlaps the block.
bool CircleHitsBlock(double cxFrac, double cyFrac, double radiusPx, const WorldRect& block, const MapSize& size);
// True if the circle overlaps any block.
bool CircleBlocked(const std::vector<WorldRect>& blocks, double cxFrac, double cyFrac, double radiusPx,
                   const MapSize& size);

struct StepInput {
    double joyX = 0.0;        // joystick, screen space (x right); magnitude is clamped to 1
    double joyY = 0.0;        // joystick, screen space (y down)
    bool running = false;     // run speed (kRunSpeed) or walk speed (kWalkSpeed)
    double dtSec = 0.0;       // frame time; clamped to [0, kMaxStepSec]
    double radiusPx = 18.0;   // player radius in pixels
    double marginPx = 40.0;   // keeps the player this far from the screen edge
};

// One frame of movement. Each axis is tested separately, so the player slides along a block.
// The result stays inside [margin, 1 - margin] and never overlaps a block. Invalid input (NaN, zero
// size) leaves the position unchanged.
WorldPoint StepPlayer(WorldPoint pos, const StepInput& in, const std::vector<WorldRect>& blocks, const MapSize& size);

}  // namespace mobile::net
