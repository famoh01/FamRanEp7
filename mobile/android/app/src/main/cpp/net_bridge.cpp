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
std::string g_selectedName;
// True after the server ended the world session (409 not_in_world) and the resume has not succeeded yet.
bool g_sessionLost = false;
// Spacing of the resume attempts while the network is down (see ReconnectBackoff).
mobile::net::ReconnectBackoff g_backoff;

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

// The login has expired (401): forget the token and the character. Java shows the login screen.
jint EndForLogin() {
    Api().ClearToken();
    mobile::net::World().Leave();
    g_selectedId = 0;
    g_selectedName.clear();
    g_sessionLost = false;
    g_backoff.Reset();
    g_lastError = "unauthorized";
    LOGI("login expired: back to login");
    return kSyncLoginAgain;
}

// The server ended the world session. Re-enter with the saved position, spacing the attempts out
// while the network is down. The character keeps its place: nothing is snapped or reset.
jint TryResumeWorld() {
    const int64_t now = NowMs();
    if (!g_backoff.Due(now)) return kSyncReconnecting;
    std::string err;
    switch (mobile::net::ResumeWorld(Api(), g_selectedId, mobile::net::World(), err)) {
        case mobile::net::ResumeResult::Resumed:
            LOGI("world session resumed");
            g_sessionLost = false;
            g_backoff.Reset();
            g_lastError.clear();
            return kSyncContinue;
        case mobile::net::ResumeResult::Unauthorized:
            return EndForLogin();
        case mobile::net::ResumeResult::Failed:
            g_backoff.Failed(now);
            g_lastError = err;
            LOGI("resume failed (attempt %d): %s", g_backoff.Failures(), err.c_str());
            return kSyncReconnecting;
    }
    return kSyncReconnecting;
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
    g_sessionLost = false;
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
    g_sessionLost = false;
    g_backoff.Reset();
    std::string name;
    mobile::net::JsonFindString(json, "name", name);
    g_selectedName = name;
    mobile::net::World().Enter(start, std::move(blocks));
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
    g_sessionLost = false;
    mobile::net::World().Leave();
    return JNI_TRUE;
}

// Movement sync, called on the net thread about every kMoveIntervalMs while in the world.
// Sends the render position when it changed, and reports the server's answer back to the render thread.
// Returns one of the kSync* codes above.
JNIEXPORT jint JNICALL
Java_com_aiep7_app_MainActivity_nativeSyncMove(JNIEnv* env, jobject) {
    CallScope scope(env);
    if (!scope.ok()) return kSyncStop;
    auto& world = mobile::net::World();
    if (!world.InWorld()) return kSyncStop;
    if (g_sessionLost) return TryResumeWorld();
    mobile::net::WorldPoint p;
    if (!world.PendingMove(p)) return kSyncContinue;
    mobile::net::WorldPoint serverPos;
    std::string err;
    switch (Api().MoveCharacter(g_selectedId, p.x, p.y, serverPos, err)) {
        case mobile::net::ServerApi::MoveResult::Accepted:
            world.MoveAccepted(p);
            return kSyncContinue;
        case mobile::net::ServerApi::MoveResult::Rejected:
            // The server keeps its own position. Snap the render position to it.
            LOGI("move rejected: %s", err.c_str());
            world.MoveRejected(serverPos);
            return kSyncContinue;
        case mobile::net::ServerApi::MoveResult::SessionLost:
            // Not a rejected move: the session ended (idle timeout or restart). Resume at the saved position.
            LOGI("world session ended by the server, resuming");
            g_sessionLost = true;
            g_backoff.Reset();   // the first attempt is immediate
            return TryResumeWorld();
        case mobile::net::ServerApi::MoveResult::Unauthorized:
            return EndForLogin();
        case mobile::net::ServerApi::MoveResult::Failed:
            // Network trouble: nothing changes. The next tick sends the latest position again.
            g_lastError = err;
            return kSyncContinue;
    }
    return kSyncContinue;
}

JNIEXPORT jstring JNICALL
Java_com_aiep7_app_MainActivity_nativeLastError(JNIEnv* env, jobject) {
    return env->NewStringUTF(g_lastError.c_str());
}

}  // extern "C"
