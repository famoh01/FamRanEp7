// AI EP7 - JNI bridge between MainActivity (Java dialogs) and mobile/net_client (C++).
//
// net_client does the protocol work (JSON bodies, Bearer token, error codes). Each request is
// handed to Java NetBridge.request(), which performs it with HttpURLConnection. Every JNI entry
// point runs on Java's `net` thread, one call at a time, so the JNIEnv saved in g_env is only used
// while a call is active.
#include <jni.h>
#include <android/log.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "api_client.h"
#include "world_flow.h"
#include "world_state.h"

#define LOG_TAG "AiEp7"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace {

JNIEnv* g_env = nullptr;
jclass g_bridge = nullptr;
jmethodID g_request = nullptr;
std::unique_ptr<mobile::net::ServerApi> g_api;
std::string g_lastError;
long long g_selectedId = 0;
// Debug builds only: a forced session expiry counts down so the CI smoke test can prove the
// reconnect path on a real device (the production server never expires a session in 20 s).
jlong g_forceExpireInMs = -1;
jlong g_forceExpireAtMs = -1;
mobile::net::SyncEvent g_lastEvent = mobile::net::SyncEvent::None;
uint64_t g_lastTick = 0;
int g_lastLost = 0;
std::string g_selectedName;
// World sync and reconnect state machine (host-tested logic in world_flow.cpp).
mobile::net::WorldSync g_sync;

// Values returned by nativeSyncMove. Java keeps the sync loop running for kSyncContinue and
// kSyncReconnecting only.
constexpr jint kSyncStop = 0;          // not in the world (left, or never entered): the loop ends
constexpr jint kSyncContinue = 1;      // connected: keep syncing
constexpr jint kSyncLoginAgain = 2;    // the login has expired: go back to the login screen
constexpr jint kSyncReconnecting = 3;  // the world session was lost: retrying, keep syncing

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Uses the JNIEnv of the current JNI call.
bool JavaRequest(const std::string& method, const std::string& path, const std::string& token,
                 const std::string& body, mobile::net::HttpResponse& out, std::string& err) {
    JNIEnv* env = g_env;
    if (!env || !g_bridge || !g_request) {
        err = "bridge not ready";
        return false;
    }
    jstring jMethod = env->NewStringUTF(method.c_str());
    jstring jPath = env->NewStringUTF(path.c_str());
    jstring jToken = env->NewStringUTF(token.c_str());
    jstring jBody = env->NewStringUTF(body.c_str());
    auto res = static_cast<jstring>(env->CallStaticObjectMethod(g_bridge, g_request, jMethod, jPath, jToken, jBody));
    env->DeleteLocalRef(jMethod);
    env->DeleteLocalRef(jPath);
    env->DeleteLocalRef(jToken);
    env->DeleteLocalRef(jBody);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        err = "bridge exception";
        return false;
    }
    if (!res) {
        err = "no response from bridge";
        return false;
    }
    const char* chars = env->GetStringUTFChars(res, nullptr);
    std::string raw = chars ? chars : "";
    if (chars) env->ReleaseStringUTFChars(res, chars);
    env->DeleteLocalRef(res);

    const size_t nl = raw.find('\n');
    if (nl == std::string::npos) {
        err = "bad bridge response";
        return false;
    }
    const int status = std::atoi(raw.substr(0, nl).c_str());
    if (status == 0) {
        err = "network: " + raw.substr(nl + 1);
        return false;
    }
    out.status = status;
    out.body = raw.substr(nl + 1);
    return true;
}

bool EnsureBridge(JNIEnv* env) {
    if (g_bridge && g_request) return true;
    jclass local = env->FindClass("com/aiep7/app/NetBridge");
    if (!local) {
        env->ExceptionClear();
        return false;
    }
    g_bridge = static_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);
    g_request = env->GetStaticMethodID(g_bridge, "request",
                                       "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    if (!g_request) {
        env->ExceptionClear();
        return false;
    }
    return true;
}

mobile::net::ServerApi& Api() {
    if (!g_api) g_api = std::make_unique<mobile::net::ServerApi>(mobile::net::RequestFn(JavaRequest));
    return *g_api;
}

std::string ToStd(JNIEnv* env, jstring s) {
    if (!s) return "";
    const char* chars = env->GetStringUTFChars(s, nullptr);
    std::string out = chars ? chars : "";
    if (chars) env->ReleaseStringUTFChars(s, chars);
    return out;
}

// Each entry point: set the env, make sure the bridge class is found, run the call, keep the error.
struct CallScope {
    CallScope(JNIEnv* env) : ok_(EnsureBridge(env)) {
        g_env = env;
        if (!ok_) g_lastError = "bridge class not found";
    }
    ~CallScope() { g_env = nullptr; }
    bool ok() const { return ok_; }

private:
    bool ok_;
};

}  // namespace

bool InWorld() {
    return mobile::net::World().InWorld();
}

namespace {

// Debug-only device test of the reconnect path: ends the SERVER's world session (POST /world/leave
// keeps the saved position) while the client keeps its own session and position. The next move then
// gets the same 409 not_in_world a real idle timeout produces, and the flow resumes at the position
// the server saved.
bool EndServerSessionForTest() {
    if (g_selectedId == 0) return false;
    std::string err;
    if (!Api().LeaveWorld(g_selectedId, err)) {
        g_lastError = err;
        LOGI("device test: could not end the server session: %s", err.c_str());
        return false;
    }
    const mobile::net::WorldPoint before = mobile::net::World().Start();
    LOGI("device test: position before expiry %.4f,%.4f", before.x, before.y);
    LOGI("device test: server session ended by /world/leave (position kept)");
    // The player is standing still, so nothing would be sent and no 409 would happen: force one
    // probe move of the current position. The server answers 409 not_in_world, exactly like a real
    // idle timeout, and the flow resumes at the saved position.
    mobile::net::World().ForceNextMove();
    return true;
}

// Events worth a log line on the device (and greppable by the CI smoke test).
bool NotableEvent(mobile::net::SyncEvent event) {
    switch (event) {
        case mobile::net::SyncEvent::SessionLost:
        case mobile::net::SyncEvent::Resumed:
        case mobile::net::SyncEvent::ResumeFailed:
        case mobile::net::SyncEvent::LoginExpired:
        case mobile::net::SyncEvent::MoveRejected:
        case mobile::net::SyncEvent::Throttled:
            return true;
        default:
            return false;
    }
}

// Maps the flow's outcome onto the kSync* codes returned to Java.
jint ToSyncCode(mobile::net::SyncOutcome outcome) {
    switch (outcome) {
        case mobile::net::SyncOutcome::Stop:         return kSyncStop;
        case mobile::net::SyncOutcome::Continue:     return kSyncContinue;
        case mobile::net::SyncOutcome::Reconnecting: return kSyncReconnecting;
        case mobile::net::SyncOutcome::LoginAgain:   return kSyncLoginAgain;
    }
    return kSyncStop;
}

}  // namespace

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_aiep7_app_MainActivity_nativeRegister(JNIEnv* env, jobject, jstring user, jstring pass) {
    CallScope scope(env);
    if (!scope.ok()) return JNI_FALSE;
    std::string err;
    if (!Api().Register(ToStd(env, user), ToStd(env, pass), err)) {
        g_lastError = err;
        LOGI("register failed: %s", err.c_str());
        return JNI_FALSE;
    }
    LOGI("register ok");
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_aiep7_app_MainActivity_nativeLogin(JNIEnv* env, jobject, jstring user, jstring pass) {
    CallScope scope(env);
    if (!scope.ok()) return JNI_FALSE;
    std::string err;
    if (!Api().Login(ToStd(env, user), ToStd(env, pass), err)) {
        g_lastError = err;
        LOGI("login failed: %s", err.c_str());
        return JNI_FALSE;
    }
    return JNI_TRUE;
}

JNIEXPORT jstring JNICALL
Java_com_aiep7_app_MainActivity_nativeListCharacters(JNIEnv* env, jobject) {
    CallScope scope(env);
    if (!scope.ok()) return nullptr;
    std::string json, err;
    if (!Api().ListCharacters(json, err)) {
        g_lastError = err;
        return nullptr;
    }
    return env->NewStringUTF(json.c_str());
}

JNIEXPORT jlong JNICALL
Java_com_aiep7_app_MainActivity_nativeCreateCharacter(JNIEnv* env, jobject, jstring name, jstring gender,
                                                      jstring cls) {
    CallScope scope(env);
    if (!scope.ok()) return -1;
    long long id = 0;
    std::string err;
    if (!Api().CreateCharacter(ToStd(env, name), ToStd(env, gender), ToStd(env, cls), id, err)) {
        g_lastError = err;
        return -1;
    }
    return static_cast<jlong>(id);
}

JNIEXPORT jboolean JNICALL
Java_com_aiep7_app_MainActivity_nativeDeleteCharacter(JNIEnv* env, jobject, jlong id) {
    CallScope scope(env);
    if (!scope.ok()) return JNI_FALSE;
    std::string err;
    if (!Api().DeleteCharacter(static_cast<long long>(id), err)) {
        g_lastError = err;
        return JNI_FALSE;
    }
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_aiep7_app_MainActivity_nativeLogout(JNIEnv* env, jobject) {
    CallScope scope(env);
    if (!scope.ok()) return JNI_FALSE;
    std::string err;
    const bool ok = Api().Logout(err);   // the token is forgotten even when the call fails
    if (!ok) {
        g_lastError = err;
        LOGI("logout: %s (local session cleared)", err.c_str());
    }
    g_selectedId = 0;
    g_selectedName.clear();
    g_sync.Detached();
    mobile::net::World().Leave();
    return JNI_TRUE;
}

// Enter World: the server creates or resumes the character's session and returns the start position and
// the map blocks. Those replace the placeholder map until the session ends (leave, logout).
JNIEXPORT jboolean JNICALL
Java_com_aiep7_app_MainActivity_nativeEnterWorld(JNIEnv* env, jobject, jlong id) {
    CallScope scope(env);
    if (!scope.ok()) return JNI_FALSE;
    std::string json, err;
    if (!Api().EnterWorld(static_cast<long long>(id), json, err)) {
        g_lastError = err;
        return JNI_FALSE;
    }
    mobile::net::WorldPoint start;
    std::vector<mobile::net::WorldRect> blocks;
    if (!mobile::net::ParseWorldEnter(json, start, blocks)) {
        g_lastError = "bad world data";
        return JNI_FALSE;
    }
    g_selectedId = static_cast<long long>(id);
    g_lastEvent = mobile::net::SyncEvent::None;
    g_lastTick = g_sync.Ticks();
    g_lastLost = 0;
    std::string name;
    mobile::net::JsonFindString(json, "name", name);
    g_selectedName = name;
    mobile::net::World().Enter(start, std::move(blocks));
    g_sync.Attached(g_selectedId);
    LOGI("enter world character id=%lld name=%s", g_selectedId, g_selectedName.c_str());
    return JNI_TRUE;
}

// Leave World: the server saves the position and ends the session. A failed call still ends the
// local session, so the player can always get back to the character list.
JNIEXPORT jboolean JNICALL
Java_com_aiep7_app_MainActivity_nativeLeaveWorld(JNIEnv* env, jobject) {
    CallScope scope(env);
    std::string err;
    if (scope.ok() && !Api().LeaveWorld(g_selectedId, err)) {
        g_lastError = err;
        LOGI("leave world: %s (local session ended)", err.c_str());
    }
    g_selectedId = 0;
    g_selectedName.clear();
    g_sync.Detached();
    mobile::net::World().Leave();
    return JNI_TRUE;
}

// Debug-only: ends the world session locally, so the next move gets a real 409 not_in_world from the
// server and the reconnect path runs. Enabled by MainActivity when the CI extra is present.
JNIEXPORT void JNICALL
Java_com_aiep7_app_MainActivity_nativeForceExpireAfter(JNIEnv* env, jobject, jlong ms) {
    CallScope scope(env);
    g_forceExpireInMs = ms;
    LOGI("device test: world session will expire in %lld ms", static_cast<long long>(ms));
}

// Movement sync, called on the net thread about every kMoveIntervalMs while in the world.
// Sends the render position when it changed, re-enters the world when the server ended the session,
// and reports the login expiry. The decisions live in mobile::net::WorldSync (world_flow.cpp).
// Returns one of the kSync* codes above.
JNIEXPORT jint JNICALL
Java_com_aiep7_app_MainActivity_nativeSyncMove(JNIEnv* env, jobject) {
    CallScope scope(env);
    if (!scope.ok()) return kSyncStop;
    std::string err;
    // Debug-only: at the armed time, end the server session so the reconnect path runs for real.
    if (g_forceExpireInMs >= 0) {
        const int64_t now = NowMs();
        if (g_forceExpireAtMs < 0) g_forceExpireAtMs = now + g_forceExpireInMs;
        if (now >= g_forceExpireAtMs) {
            g_forceExpireInMs = -1;
            g_forceExpireAtMs = -1;
            EndServerSessionForTest();
        }
    }
    const mobile::net::SyncOutcome outcome =
        g_sync.Tick(Api(), mobile::net::World(), NowMs(), err);
    // The flow resumes inside the same tick, so LastEvent() is already "resumed". Log the loss first
    // (Ticks() increments once per tick) to keep the order the device was in: lost -> resumed.
    if (g_sync.Ticks() != g_lastTick) {
        g_lastTick = g_sync.Ticks();
        if (g_sync.ResumesLost() > g_lastLost) {
            LOGI("sync event %s (resumes=%d, failures=%d, %s)",
                 mobile::net::SyncEventName(mobile::net::SyncEvent::SessionLost), g_sync.Resumes(),
                 g_sync.ResumeFailures(), err.c_str());
            g_lastLost = g_sync.ResumesLost();
        }
    }
    const mobile::net::SyncEvent event = g_sync.LastEvent();
    if (NotableEvent(event) && event != g_lastEvent) {
        if (event == mobile::net::SyncEvent::Resumed) {
            // The resume answer carries the server's saved position: log it so the CI smoke test can
            // compare it with the position the character had before the session ended.
            const mobile::net::WorldPoint at = mobile::net::World().Start();
            LOGI("sync event %s (resumes=%d, failures=%d, %s) pos=%.4f,%.4f",
                 mobile::net::SyncEventName(event), g_sync.Resumes(), g_sync.ResumeFailures(),
                 err.c_str(), at.x, at.y);
        } else {
            LOGI("sync event %s (resumes=%d, failures=%d, %s)", mobile::net::SyncEventName(event),
                 g_sync.Resumes(), g_sync.ResumeFailures(), err.c_str());
        }
    }
    g_lastEvent = event;
    switch (outcome) {
        case mobile::net::SyncOutcome::Reconnecting:
            g_lastError = err;
            LOGI("world sync: reconnecting (failures=%d, %s)", g_sync.ResumeFailures(), err.c_str());
            break;
        case mobile::net::SyncOutcome::LoginAgain:
            g_selectedId = 0;
            g_selectedName.clear();
            g_lastError = err;
            LOGI("login expired: back to login");
            break;
        case mobile::net::SyncOutcome::Continue:
            if (g_lastError.rfind("HTTP", 0) == 0 || g_lastError == "unauthorized") g_lastError.clear();
            break;
        case mobile::net::SyncOutcome::Stop:
            break;
    }
    return ToSyncCode(outcome);
}

// Lifecycle hooks for MainActivity.onPause/onResume (run on the net thread).
JNIEXPORT void JNICALL
Java_com_aiep7_app_MainActivity_nativePauseWorld(JNIEnv* env, jobject) {
    CallScope scope(env);
    g_lastEvent = mobile::net::SyncEvent::None;
    g_sync.Pause();
    LOGI("world sync paused (app in background)");
}

JNIEXPORT void JNICALL
Java_com_aiep7_app_MainActivity_nativeResumeWorld(JNIEnv* env, jobject) {
    CallScope scope(env);
    g_lastEvent = mobile::net::SyncEvent::None;
    g_sync.Resumed();
    LOGI("world sync resumed (app in foreground)");
}

JNIEXPORT jstring JNICALL
Java_com_aiep7_app_MainActivity_nativeLastError(JNIEnv* env, jobject) {
    return env->NewStringUTF(g_lastError.c_str());
}

}  // extern "C"
