// mobile/net_client/world_map.cpp
#include "world_map.h"

#include <algorithm>
#include <cmath>

namespace mobile::net {

bool CircleHitsBlock(double cxFrac, double cyFrac, double radiusPx, const WorldRect& block, const MapSize& size) {
    const double cx = cxFrac * size.widthPx, cy = cyFrac * size.heightPx;
    const double rx = block.x * size.widthPx, ry = block.y * size.heightPx;
    const double rw = block.w * size.widthPx, rh = block.h * size.heightPx;
    // Closest point of the rectangle to the circle center.
    const double nx = std::max(rx, std::min(cx, rx + rw));
    const double ny = std::max(ry, std::min(cy, ry + rh));
    const double dx = cx - nx, dy = cy - ny;
    return dx * dx + dy * dy < radiusPx * radiusPx;
}

bool CircleBlocked(const std::vector<WorldRect>& blocks, double cxFrac, double cyFrac, double radiusPx,
                   const MapSize& size) {
    for (const WorldRect& b : blocks)
        if (CircleHitsBlock(cxFrac, cyFrac, radiusPx, b, size)) return true;
    return false;
}

WorldPoint StepPlayer(WorldPoint pos, const StepInput& in, const std::vector<WorldRect>& blocks, const MapSize& size) {
    if (!(size.widthPx > 0.0) || !(size.heightPx > 0.0)) return pos;
    if (!std::isfinite(in.joyX) || !std::isfinite(in.joyY) || !std::isfinite(in.dtSec)) return pos;
    if (!std::isfinite(pos.x) || !std::isfinite(pos.y)) return pos;

    const double dt = std::max(0.0, std::min(in.dtSec, kMaxStepSec));
    double mx = in.joyX, my = in.joyY;
    const double len = std::hypot(mx, my);
    if (len > 1.0) {  // a diagonal push is never faster than a straight one
        mx /= len;
        my /= len;
    }
    const double speed = in.running ? kRunSpeed : kWalkSpeed;
    const double marginX = in.marginPx / size.widthPx;
    const double marginY = in.marginPx / size.heightPx;

    WorldPoint out = pos;
    const double nx = std::max(marginX, std::min(1.0 - marginX, pos.x + mx * speed * dt));
    if (!CircleBlocked(blocks, nx, out.y, in.radiusPx, size)) out.x = nx;
    const double ny = std::max(marginY, std::min(1.0 - marginY, pos.y + my * speed * dt));
    if (!CircleBlocked(blocks, out.x, ny, in.radiusPx, size)) out.y = ny;
    return out;
}

}  // namespace mobile::net
