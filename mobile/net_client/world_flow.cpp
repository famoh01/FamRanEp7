// mobile/net_client/world_flow.cpp
#include "world_flow.h"

namespace mobile::net {

const char* SyncEventName(SyncEvent event) {
    switch (event) {
        case SyncEvent::None:          return "none";
        case SyncEvent::MoveSent:      return "move_sent";
        case SyncEvent::MoveRejected:  return "move_rejected";
        case SyncEvent::Throttled:     return "throttled";
        case SyncEvent::MoveFailed:    return "move_failed";
        case SyncEvent::SessionLost:   return "session_lost";
        case SyncEvent::ResumeFailed:  return "resume_failed";
        case SyncEvent::Resumed:       return "resumed";
        case SyncEvent::LoginExpired:  return "login_expired";
    }
    return "none";
}

void WorldSync::Attached(long long characterId) {
    characterId_ = characterId;
    phase_ = Phase::Live;
    paused_ = false;
    resumes_ = 0;
    lastEvent_ = SyncEvent::None;
    backoff_.Reset();
}

void WorldSync::Detached() {
    characterId_ = 0;
    phase_ = Phase::Idle;
    paused_ = false;
    lastEvent_ = SyncEvent::None;
    backoff_.Reset();
}

void WorldSync::Pause() {
    paused_ = true;
}

void WorldSync::Resumed() {
    if (!paused_) return;
    paused_ = false;
    // Retry at once: the player is back and expects the character to work.
    if (phase_ == Phase::Reconnecting) backoff_.Reset();
}

SyncOutcome WorldSync::EndForLogin(ServerApi& api, WorldSession& session, std::string& err) {
    api.ClearToken();
    session.Leave();
    characterId_ = 0;
    phase_ = Phase::LoginExpired;
    paused_ = false;
    lastEvent_ = SyncEvent::LoginExpired;
    backoff_.Reset();
    err = "unauthorized";
    return SyncOutcome::LoginAgain;
}

SyncOutcome WorldSync::Resume(ServerApi& api, WorldSession& session, int64_t nowMs, std::string& err) {
    if (!backoff_.Due(nowMs)) {
        err = "waiting to retry";
        return SyncOutcome::Reconnecting;
    }
    switch (ResumeWorld(api, characterId_, session, err)) {
        case ResumeResult::Resumed:
            phase_ = Phase::Live;
            ++resumes_;
            lastEvent_ = SyncEvent::Resumed;
            backoff_.Reset();
            err.clear();
            return SyncOutcome::Continue;
        case ResumeResult::Unauthorized:
            return EndForLogin(api, session, err);
        case ResumeResult::Failed:
            backoff_.Failed(nowMs);
            lastEvent_ = SyncEvent::ResumeFailed;
            return SyncOutcome::Reconnecting;
    }
    return SyncOutcome::Reconnecting;
}

SyncOutcome WorldSync::Tick(ServerApi& api, WorldSession& session, int64_t nowMs, std::string& err) {
    err.clear();
    switch (phase_) {
        case Phase::Idle:
        case Phase::LoginExpired:
            return SyncOutcome::Stop;
        case Phase::Reconnecting:
            if (paused_) return SyncOutcome::Reconnecting;
            return Resume(api, session, nowMs, err);
        case Phase::Live:
            break;
    }
    if (paused_) return SyncOutcome::Continue;
    if (!session.InWorld()) {
        // The local session ended without a leave (defensive): nothing to sync.
        Detached();
        return SyncOutcome::Stop;
    }
    WorldPoint p;
    // PendingMove applies the client's own minimum interval (kMinMoveGapMs) as well.
    if (!session.PendingMove(p, nowMs)) return SyncOutcome::Continue;

    WorldPoint serverPos;
    switch (api.MoveCharacter(characterId_, p.x, p.y, serverPos, err)) {
        case ServerApi::MoveResult::Accepted:
            session.MoveAccepted(p, nowMs);
            lastEvent_ = SyncEvent::MoveSent;
            return SyncOutcome::Continue;
        case ServerApi::MoveResult::Rejected:
            // The server keeps its own position; the render thread snaps to it on the next frame.
            session.MoveRejected(serverPos, nowMs);
            lastEvent_ = SyncEvent::MoveRejected;
            return SyncOutcome::Continue;
        case ServerApi::MoveResult::Throttled:
            // Not judged (429 move_too_fast): keep the position and try again on a later tick.
            session.MoveThrottled(nowMs);
            lastEvent_ = SyncEvent::Throttled;
            return SyncOutcome::Continue;
        case ServerApi::MoveResult::SessionLost:
            // The session ended on the server (idle timeout, restart): re-enter at the saved position.
            phase_ = Phase::Reconnecting;
            lastEvent_ = SyncEvent::SessionLost;
            backoff_.Reset();   // the first attempt is immediate
            return Resume(api, session, nowMs, err);
        case ServerApi::MoveResult::Unauthorized:
            return EndForLogin(api, session, err);
        case ServerApi::MoveResult::Failed:
            // Network trouble: nothing changes. The next tick sends the latest position again.
            lastEvent_ = SyncEvent::MoveFailed;
            return SyncOutcome::Continue;
    }
    return SyncOutcome::Continue;
}

}  // namespace mobile::net
