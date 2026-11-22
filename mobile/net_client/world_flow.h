// mobile/net_client/world_flow.h
// The world sync state machine of the mobile client, without JNI, Android or sockets, so it is
// fully tested on the host (make -C mobile/net_client test) and against a live server
// (test_reconnect.cpp).
//
// One Tick() is what the Android layer runs about every kMoveIntervalMs on its net thread:
//   Live         -> send the newest render position when it moved
//   Reconnecting -> the server ended the session (idle timeout, restart): re-enter with ResumeWorld,
//                   waiting 0.5 s, 1 s, 2 s ... 10 s between attempts, keeping the saved position
//   LoginExpired -> the token is no longer valid (401): forget it and tell the caller to show login
//   Paused       -> the app went to the background: no requests at all
//
// The caller owns the ServerApi (token) and the WorldSession (positions/blocks shared with the
// render thread); the flow only decides what to do next.
#pragma once

#include <cstdint>
#include <string>

#include "api_client.h"
#include "world_state.h"

namespace mobile::net {

// What the caller must do after a tick (net_bridge maps these onto its kSync* codes).
enum class SyncOutcome {
    Stop,          // not in the world any more: end the sync loop
    Continue,      // keep syncing (request sent, nothing to send, throttled, or paused)
    Reconnecting,  // the world session is being re-established: keep the loop alive
    LoginAgain,    // the login has expired: go back to the login screen
};

// What the last Tick did, so the platform layer can log it (the CI smoke test greps these lines)
// without the pure logic depending on any logging library.
enum class SyncEvent {
    None,
    MoveSent,        // a move request was accepted
    MoveRejected,    // the server refused the move (position/direction); the player is snapped back
    Throttled,       // 429 move_too_fast: nothing changed, retry later
    MoveFailed,      // network trouble: nothing changed
    SessionLost,     // 409 not_in_world: the server ended the session, a resume starts
    ResumeFailed,    // the resume attempt failed; it is retried with backoff
    Resumed,         // the resume succeeded: the character is back at the saved position
    LoginExpired,    // 401: the token is gone, the caller must show the login screen
};

const char* SyncEventName(SyncEvent event);

class WorldSync {
public:
    // Net thread. After a successful Enter World: the flow starts sending this character's moves.
    void Attached(long long characterId);
    // Net thread. After leave world or logout: the flow stops (any pending resume is dropped).
    void Detached();
    // Net thread.
    bool InWorld() const { return characterId_ != 0 && phase_ != Phase::LoginExpired; }
    long long CharacterId() const { return characterId_; }
    // Net thread. True while the server session is being re-established.
    bool Reconnecting() const { return phase_ == Phase::Reconnecting; }
    // Net thread. Number of failed resume attempts since the session was lost (0 when connected).
    int ResumeFailures() const { return backoff_.Failures(); }
    // Net thread. What the last Tick did.
    SyncEvent LastEvent() const { return lastEvent_; }
    // Net thread. Successful resumes since Attached (a CI/smoke-test signal).
    int Resumes() const { return resumes_; }

    // Net thread. The app went to the background (onPause). Aborts nothing: the position stays.
    void Pause();
    // Net thread. The app came back (onResume). While reconnecting the next attempt is immediate,
    // so a player who returns after a long time in the background does not wait out the 10 s cap.
    void Resumed();

    // Net thread. Runs one tick. `err` receives the reason when the outcome is Reconnecting or
    // LoginAgain (empty otherwise).
    SyncOutcome Tick(ServerApi& api, WorldSession& session, int64_t nowMs, std::string& err);

private:
    enum class Phase { Idle, Live, Reconnecting, LoginExpired };

    SyncOutcome Resume(ServerApi& api, WorldSession& session, int64_t nowMs, std::string& err);
    SyncOutcome EndForLogin(ServerApi& api, WorldSession& session, std::string& err);

    long long characterId_ = 0;
    int resumes_ = 0;
    SyncEvent lastEvent_ = SyncEvent::None;
    Phase phase_ = Phase::Idle;
    bool paused_ = false;
    ReconnectBackoff backoff_;
};

}  // namespace mobile::net
