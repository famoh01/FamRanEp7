// mobile/net_client/world_state.h
// Shared state between the render thread (main.cpp) and the net thread (net_bridge.cpp) for the
// RAN MOBILE world. No Android or JNI dependency, so it is tested on the host (make test_world).
//
// Coordinates are fractions of the map (0..1, top-left = 0,0), the same as the server's world.py.
// Render thread: Refresh, TakeCorrection, Publish. Net thread: Enter, Leave, PendingMove,
// MoveAccepted, MoveRejected. All state is guarded by one mutex; every call is short.
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "api_client.h"

namespace mobile::net {

// A blocked rectangle in map fractions: x, y (top-left), w, h.
struct WorldRect {
    double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
};

// Walking and running speed in map fractions per second. The server accepts up to 0.45
// (world.py MAX_SPEED); these values leave room for timing jitter. A diagonal joystick push is
// clamped to magnitude 1, so the speed never exceeds these values.
constexpr double kWalkSpeed = 0.18;
constexpr double kRunSpeed = 0.31;
// The render position is sent to the server at most this often (10 requests/s, as the server allows).
constexpr int kMoveIntervalMs = 100;
// The server refuses move requests closer together than this (world.MIN_MOVE_INTERVAL_MS). The client
// never sends faster than kMoveIntervalMs, but it also checks this itself so a clock step or a burst of
// queued ticks cannot produce a 429.
constexpr int64_t kMinMoveGapMs = 50;
// Smallest change (map fractions) that is worth a request.
constexpr double kMinMoveDist = 0.0005;

// Reads the enter response: world.position (where the character starts) and world.blocks.
// Returns false when either is missing or malformed. Only the "world" object is searched, so the
// character's own "position" is never mistaken for the start position.
bool ParseWorldEnter(const std::string& json, WorldPoint& start, std::vector<WorldRect>& blocks);

class WorldSession {
public:
    struct Snapshot {
        bool inWorld = false;
        WorldPoint start{};
        std::vector<WorldRect> blocks;  // server blocks; empty when not in world
        uint64_t version = 0;
    };

    // Net thread. Enter after a successful enter-world call: the render position, the last sent
    // position and the published position all start at `start`.
    void Enter(WorldPoint start, std::vector<WorldRect> blocks);
    // Net thread. Leave after the server leave call, or on logout.
    void Leave();

    // Render thread. Copies the snapshot only when `seen` is older than the current version, and
    // then sets `seen`. Returns true when `out` was filled.
    bool Refresh(uint64_t& seen, Snapshot& out) const;

    // Render thread. The position the player is drawn at after this frame.
    void Publish(WorldPoint p);
    // Render thread. Returns the server's correction once (the position it accepted), and makes
    // that the published position as well. Call it before moving the player each frame.
    bool TakeCorrection(WorldPoint& out);

    // Net thread. True when the published position differs from the last sent one by more than
    // kMinMoveDist, no correction is pending, the character is in the world, and the last move request
    // is at least kMinMoveGapMs ago (see `nowMs`, default: no timing information = always allowed).
    // `out` is the position to send.
    bool PendingMove(WorldPoint& out, int64_t nowMs = -1) const;
    // Net thread. The server accepted `sent` at `nowMs`.
    void MoveAccepted(WorldPoint sent, int64_t nowMs = -1);
    // Net thread. The server answered 429 move_too_fast: the move was not judged, so nothing changes;
    // only the request time is remembered so the next attempt waits. `sent` stays pending.
    void MoveThrottled(int64_t nowMs);
    // Net thread. Makes the next PendingMove return true even though the player is standing still:
    // used when the session state must be re-checked without waiting for the player to walk (the
    // device reconnect test, and a resume that has to confirm the server still knows the position).
    // The position sent is the current one, so the server judges it as a zero-distance move.
    void ForceNextMove();
    // Net thread. The server refused the move. Its position becomes a correction for the render
    // thread, and it becomes the new last-sent position.
    void MoveRejected(WorldPoint serverPos, int64_t nowMs = -1);

    bool InWorld() const;

private:
    mutable std::mutex mu_;
    bool inWorld_ = false;
    WorldPoint start_{};
    WorldPoint published_{};
    WorldPoint lastSent_{};
    std::vector<WorldRect> blocks_;
    uint64_t version_ = 0;
    bool correctionPending_ = false;
    WorldPoint correction_{};
    int64_t lastSentMs_ = -1;  // time of the last move request (accepted, rejected or throttled)
};

// Outcome of ResumeWorld.
enum class ResumeResult { Resumed, Failed, Unauthorized };

// Re-enters the character's world after the server ended the session (idle timeout, token renewal, or
// a server restart). The server returns the saved position, so the player carries on from there.
// On Resumed the session is replaced with the server's data; on Failed or Unauthorized it is left
// unchanged and err says why.
ResumeResult ResumeWorld(const ServerApi& api, long long characterId, WorldSession& session, std::string& err);

// Retry timing after failed resumes: waits 0.5 s, 1 s, 2 s, ... up to 10 s. Pure logic with the time
// passed in, so it is tested on the host.
class ReconnectBackoff {
public:
    static constexpr int64_t kFirstDelayMs = 500;
    static constexpr int64_t kMaxDelayMs = 10000;

    // True when a retry may run at nowMs.
    bool Due(int64_t nowMs) const { return nowMs >= nextMs_; }
    // A retry failed at nowMs: the next retry waits longer.
    void Failed(int64_t nowMs);
    // A retry succeeded: the next failure starts from the first delay again.
    void Reset();
    int Failures() const { return failures_; }

private:
    int64_t nextMs_ = 0;
    int failures_ = 0;
};

// The single world session of the app.
WorldSession& World();

}  // namespace mobile::net
