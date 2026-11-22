// mobile/net_client/world_state.cpp
#include "world_state.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace mobile::net {

namespace {

// Finds "key" in json[from, ...) and returns the index just after its ':' (or npos).
size_t FindValue(const std::string& json, const std::string& key, size_t from) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle, from);
    if (pos == std::string::npos) return std::string::npos;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return std::string::npos;
    return pos + 1;
}

// Reads a number at pos (skipping spaces). Returns the index after it, or npos.
size_t ReadNumber(const std::string& json, size_t pos, double& out) {
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r'))
        ++pos;
    const char* start = json.c_str() + pos;
    char* end = nullptr;
    const double v = std::strtod(start, &end);
    if (end == start) return std::string::npos;
    out = v;
    return pos + static_cast<size_t>(end - start);
}

bool FiniteCoord(double v) {
    return std::isfinite(v) && v >= 0.0 && v <= 1.0;
}

}  // namespace

bool ParseWorldEnter(const std::string& json, WorldPoint& start, std::vector<WorldRect>& blocks) {
    const size_t worldKey = json.find("\"world\"");
    if (worldKey == std::string::npos) return false;
    const std::string world = json.substr(worldKey);

    // position: {"x": .., "y": ..}
    const size_t posAt = FindValue(world, "position", 0);
    if (posAt == std::string::npos) return false;
    const size_t open = world.find('{', posAt);
    if (open == std::string::npos) return false;
    const size_t close = world.find('}', open);
    if (close == std::string::npos) return false;
    const std::string pos = world.substr(open, close - open + 1);
    double x = 0.0, y = 0.0;
    if (FindValue(pos, "x", 0) == std::string::npos || FindValue(pos, "y", 0) == std::string::npos) return false;
    if (ReadNumber(pos, FindValue(pos, "x", 0), x) == std::string::npos) return false;
    if (ReadNumber(pos, FindValue(pos, "y", 0), y) == std::string::npos) return false;
    if (!FiniteCoord(x) || !FiniteCoord(y)) return false;

    // blocks: [[x, y, w, h], ...]
    const size_t blocksAt = FindValue(world, "blocks", 0);
    if (blocksAt == std::string::npos) return false;
    size_t i = world.find('[', blocksAt);
    if (i == std::string::npos) return false;
    ++i;
    std::vector<WorldRect> out;
    while (true) {
        const size_t next = world.find_first_not_of(" \t\r\n,", i);
        if (next == std::string::npos) return false;
        if (world[next] == ']') {  // end of the blocks array
            i = next;
            break;
        }
        if (world[next] != '[') return false;
        size_t p = next + 1;
        double v[4] = {0, 0, 0, 0};
        for (int k = 0; k < 4; ++k) {
            p = ReadNumber(world, p, v[k]);
            if (p == std::string::npos) return false;
            p = world.find_first_not_of(" \t\r\n", p);
            if (p == std::string::npos) return false;
            if (k < 3) {
                if (world[p] != ',') return false;
                ++p;
            }
        }
        if (p >= world.size() || world[p] != ']') return false;
        for (double c : v)
            if (!std::isfinite(c)) return false;
        out.push_back(WorldRect{v[0], v[1], v[2], v[3]});
        i = p + 1;
    }

    start = WorldPoint{x, y};
    blocks = std::move(out);
    return true;
}

void WorldSession::Enter(WorldPoint start, std::vector<WorldRect> blocks) {
    std::lock_guard<std::mutex> lock(mu_);
    inWorld_ = true;
    start_ = start;
    published_ = start;
    lastSent_ = start;
    blocks_ = std::move(blocks);
    correctionPending_ = false;
    lastSentMs_ = -1;
    ++version_;
}

ResumeResult ResumeWorld(const ServerApi& api, long long characterId, WorldSession& session, std::string& err) {
    std::string json;
    if (!api.EnterWorld(characterId, json, err)) {
        // EnterWorld reports HTTP errors as "HTTP <status> <code>"; a 401 means the login has ended.
        return err.rfind("HTTP 401", 0) == 0 ? ResumeResult::Unauthorized : ResumeResult::Failed;
    }
    WorldPoint start;
    std::vector<WorldRect> blocks;
    if (!ParseWorldEnter(json, start, blocks)) {
        err = "bad world data";
        return ResumeResult::Failed;
    }
    session.Enter(start, std::move(blocks));
    err.clear();
    return ResumeResult::Resumed;
}

void ReconnectBackoff::Failed(int64_t nowMs) {
    ++failures_;
    int64_t delay = kFirstDelayMs;
    for (int i = 1; i < failures_ && delay < kMaxDelayMs; ++i) delay *= 2;
    nextMs_ = nowMs + std::min(delay, kMaxDelayMs);
}

void ReconnectBackoff::Reset() {
    failures_ = 0;
    nextMs_ = 0;
}

void WorldSession::Leave() {
    std::lock_guard<std::mutex> lock(mu_);
    inWorld_ = false;
    blocks_.clear();
    correctionPending_ = false;
    ++version_;
}

bool WorldSession::Refresh(uint64_t& seen, Snapshot& out) const {
    std::lock_guard<std::mutex> lock(mu_);
    if (seen == version_) return false;
    seen = version_;
    out.inWorld = inWorld_;
    out.start = start_;
    out.blocks = blocks_;
    out.version = version_;
    return true;
}

void WorldSession::Publish(WorldPoint p) {
    std::lock_guard<std::mutex> lock(mu_);
    published_ = p;
}

bool WorldSession::TakeCorrection(WorldPoint& out) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!correctionPending_) return false;
    correctionPending_ = false;
    out = correction_;
    published_ = correction_;
    return true;
}

bool WorldSession::PendingMove(WorldPoint& out, int64_t nowMs) const {
    std::lock_guard<std::mutex> lock(mu_);
    if (!inWorld_ || correctionPending_) return false;
    if (nowMs >= 0 && lastSentMs_ >= 0 && nowMs - lastSentMs_ < kMinMoveGapMs) return false;
    const double dx = published_.x - lastSent_.x;
    const double dy = published_.y - lastSent_.y;
    if (std::hypot(dx, dy) <= kMinMoveDist) return false;
    out = published_;
    return true;
}

void WorldSession::MoveAccepted(WorldPoint sent, int64_t nowMs) {
    std::lock_guard<std::mutex> lock(mu_);
    lastSent_ = sent;
    if (nowMs >= 0) lastSentMs_ = nowMs;
}

void WorldSession::MoveThrottled(int64_t nowMs) {
    std::lock_guard<std::mutex> lock(mu_);
    if (nowMs >= 0) lastSentMs_ = nowMs;
}

void WorldSession::MoveRejected(WorldPoint serverPos, int64_t nowMs) {
    std::lock_guard<std::mutex> lock(mu_);
    lastSent_ = serverPos;
    if (nowMs >= 0) lastSentMs_ = nowMs;
    correction_ = serverPos;
    correctionPending_ = true;
}

bool WorldSession::InWorld() const {
    std::lock_guard<std::mutex> lock(mu_);
    return inWorld_;
}

WorldSession& World() {
    static WorldSession session;
    return session;
}

}  // namespace mobile::net
