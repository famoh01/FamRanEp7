// AI EP7 - JNI bridge between MainActivity (Java dialogs) and mobile/net_client (C++).
//
// net_client does the protocol work (JSON bodies, Bearer token, error codes). Each request is
// handed to Java NetBridge.request(), which performs it with HttpURLConnection. Every JNI entry
// point runs on Java's `net` thread, one call at a time, so the JNIEnv saved in g_env is only used
// while a call is active.
#include <jni.h>
#include <android/log.h>
// android_app / ANativeActivity: BridgeAttachApp() reads app->activity->vm and ->clazz, so the real
// game-activity header is required here (a forward declaration in bridge_api.h is not enough).
#include <android_native_app_glue.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "api_client.h"
#include "combat_session.h"
#include "social_view.h"
#include "world_flow.h"
#include "world_realtime.h"
#include "ui_strings.h"
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
// Combat: world sync every ~250 ms, attacks and skills (host-tested in combat_session.cpp).
mobile::net::CombatSession g_combat;
mobile::net::CombatEvent g_lastCombatEvent = mobile::net::CombatEvent::None;
// Player stats the world view cannot fill in (before the first sync) are kept here so the HUD always
// has numbers to draw.
long long g_characterId = 0;

// Values returned by nativeSyncMove. Java keeps the sync loop running for kSyncContinue and
// kSyncReconnecting only.
constexpr jint kSyncStop = 0;          // not in the world (left, or never entered): the loop ends
constexpr jint kSyncContinue = 1;      // connected: keep syncing
constexpr jint kSyncLoginAgain = 2;    // the login has expired: go back to the login screen
constexpr jint kSyncReconnecting = 3;  // the world session was lost: retrying, keep syncing

// ---------------------------------------------------------------- Java panel helpers
// The round-4 panels are native AlertDialogs (MainActivity): the row lists come from the natives at
// the bottom of this file, so Java never parses JSON.
jclass ActivityClass(JNIEnv* env) {
    if (g_bridge == nullptr) return nullptr;
    return env->GetObjectClass(g_bridge);
}

// Runs a void method of MainActivity that opens one of the panels.
void CallActivity(JNIEnv* env, const char* method) {
    jclass cls = ActivityClass(env);
    if (cls == nullptr) return;
    jmethodID mid = env->GetMethodID(cls, method, "()V");
    if (mid != nullptr) env->CallVoidMethod(g_bridge, mid);
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
}

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

// Starts the combat session with the stats from an enter/world answer (world.player).
void StartCombatFromAnswer(const std::string& json) {
    mobile::net::PlayerStats stats;
    mobile::net::ParsePlayerStats(json, stats);
    g_combat.Start(g_characterId, stats);
    g_lastCombatEvent = mobile::net::CombatEvent::None;
    LOGI("combat ready character=%lld level=%d hp=%d/%d target=%lld", g_characterId, stats.level,
         stats.hp, stats.maxHp, (long long)g_combat.Target());
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
    g_combat.Stop();
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
    g_characterId = g_selectedId;
    g_lastEvent = mobile::net::SyncEvent::None;
    g_lastTick = g_sync.Ticks();
    g_lastLost = 0;
    std::string name;
    mobile::net::JsonFindString(json, "name", name);
    g_selectedName = name;
    mobile::net::World().Enter(start, std::move(blocks));
    g_sync.Attached(g_selectedId);
    StartCombatFromAnswer(json);
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
    g_characterId = 0;
    g_combat.Stop();
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
    const int64_t nowMs = NowMs();
    const mobile::net::SyncOutcome outcome =
        g_sync.Tick(Api(), mobile::net::World(), NowMs(), err);
    // The same net tick drives the playable loop: world sync (also moves the monsters), attacks
    // and skills. A world resume restarts the combat session at the server's position/stats.
    if (outcome == mobile::net::SyncOutcome::Stop) {
        g_combat.Stop();
    } else if (g_combat.Active()) {
        std::string combatErr;
        g_combat.Tick(Api(), nowMs, combatErr);
        const mobile::net::CombatEvent event = g_combat.LastEvent();
        if (event != g_lastCombatEvent && event != mobile::net::CombatEvent::None) {
            LOGI("combat event %s (hits=%d kills=%d target=%lld hp=%d/%d)",
                 mobile::net::CombatEventName(event), g_combat.Hits(), g_combat.Kills(),
                 (long long)g_combat.Target(), g_combat.TargetHp(), g_combat.TargetMaxHp());
        }
        g_lastCombatEvent = event;
    }
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

// ---- combat (all called on the net thread, one at a time) ----

// C++ wrappers for main.cpp's HUD hooks (which have no JNIEnv). They only queue the request; the
// next sync tick sends it, so the render thread never touches the network.
// bridge_api.h declares them with C++ linkage, so they must stay outside the extern "C" block above
// (a definition inside it would get C linkage and never match the declaration).
}  // extern "C"
// ================================================================ round 4 bridge (render thread)
// The HUD hooks run on the render thread and must never block on the network. They call the
// Bridge* functions below, which post one job to Java; Java runs it on its single net thread (where
// the JNIEnv and the ServerApi live) and then shows the panel. The answer is parsed into SocialView,
// which the render thread reads on its next frame.

namespace {

// Filled once from android_main (the render thread, which owns android_app):
//   g_vm    the JavaVM, so any native thread can attach before calling Java;
//   g_clazz MainActivity, as a global reference (the activity outlives the bridge).
JavaVM* g_vm = nullptr;
jobject g_clazz = nullptr;
jmethodID g_postJob = nullptr;   // MainActivity.postNativeJob(int what, long id, String text)

constexpr jint kJobTalk = 0;         // id = npc id  -> talk, then open the shop dialog
constexpr jint kJobGate = 1;         // id = gate id -> use the warp gate
constexpr jint kJobPartyAccept = 2;  // id = party id
constexpr jint kJobPartyLeave = 3;
constexpr jint kJobChatMap = 4;      // text = message
constexpr jint kJobChatParty = 5;
constexpr jint kJobShowSocial = 6;
constexpr jint kJobShowDialog = 7;
constexpr jint kJobShowChat = 8;
constexpr jint kJobRefreshChat = 9;
constexpr jint kJobUiStrings = 10;   // โหลดข้อความ UI ไทยจากไฟล์จริงใน Drive (ThaiPack)

bool PostJob(jint what, jlong id, const char* text) {
    if (g_vm == nullptr || g_clazz == nullptr || g_postJob == nullptr) return false;
    JNIEnv* env = nullptr;
    bool attached = false;
    if (g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return false;
        attached = true;
    }
    jstring arg = (text == nullptr) ? nullptr : env->NewStringUTF(text);
    env->CallVoidMethod(g_clazz, g_postJob, what, id, arg);
    if (arg != nullptr) env->DeleteLocalRef(arg);
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
    if (attached) g_vm->DetachCurrentThread();
    return true;
}

// The jobs themselves: run on Java's net thread by the nativeRun* entry points below.
void RunTalkToNpc(long long npcId) {
    if (g_env == nullptr || g_api == nullptr || g_characterId == 0) return;
    std::string json, err;
    if (!g_api->TalkToNpc(g_characterId, npcId, json, err)) {
        g_lastError = err;
        LOGI("talk npc %lld failed: %s", npcId, err.c_str());
        return;
    }
    mobile::net::Social().ApplyTalk(json);
    LOGI("talk npc %lld ok", npcId);
}

void RunUseGate(long long gateId) {
    if (g_env == nullptr || g_api == nullptr || g_characterId == 0) return;
    std::string json, code, err;
    if (g_api->UseGate(g_characterId, gateId, json, code, err) !=
        mobile::net::ServerApi::GateResult::Ok) {
        g_lastError = code.empty() ? err : code;
        LOGI("gate %lld refused: %s", gateId, g_lastError.c_str());
        return;
    }
    mobile::net::View().ApplyHeal(json);   // the answer carries the character's stats
    LOGI("gate %lld used", gateId);
}

void RunPartyAction(int kind, long long partyId) {
    if (g_env == nullptr || g_api == nullptr || g_characterId == 0) return;
    std::string json, err;
    const bool ok = (kind == 0) ? g_api->AcceptPartyInvite(g_characterId, partyId, json, err)
                                : g_api->LeaveParty(g_characterId, json, err);
    if (!ok) {
        g_lastError = err;
        return;
    }
    std::string state;
    if (g_api->GetParty(g_characterId, state, err)) mobile::net::Social().ApplyPartyState(state);
    LOGI("party action %d ok", kind);
}

void RunSendChat(std::string text, int channel) {
    if (g_env == nullptr || g_api == nullptr || g_characterId == 0) return;
    if (channel == 1 && (text.empty() || text[0] != '#')) text = "#" + text;
    std::string json, err;
    if (!g_api->SendChat(g_characterId, text, json, err)) {
        g_lastError = err;
        LOGI("chat failed: %s", err.c_str());
        return;
    }
    mobile::net::Social().ApplyChatSent(json);
    LOGI("chat sent (channel %d)", channel);
}

// ข้อความ UI ไทย: server อ่านจากไฟล์จริงของ Drive (ThaiPack data/gui/game*_th.txt) แล้วส่งมาเป็น JSON
// เก็บดิบไว้ให้ Java ดึงไปแสดงผ่าน nativeUiString(key) — ไม่มีข้อความที่แต่งขึ้นในโค้ด
mobile::net::UiStrings g_uiStrings;

void RunLoadUiStrings() {
    if (g_env == nullptr || g_api == nullptr) return;
    std::string json, err;
    if (!g_api->GetUiStrings(g_characterId, json, err)) {
        g_lastError = err;
        LOGI("ui strings failed: %s", err.c_str());
        return;
    }
    if (!g_uiStrings.Parse(json)) {
        LOGI("ui strings: unexpected answer shape");
        return;
    }
    LOGI("ui strings loaded: %zu keys", g_uiStrings.Size());
}

}  // namespace

extern "C" {


// Java's net thread runs one job per call, through these entry points.
JNIEXPORT void JNICALL
Java_com_aiep7_app_MainActivity_nativeRunJob(JNIEnv* env, jobject, jint what, jlong id,
                                             jstring text) {
    CallScope scope(env);
    if (!scope.ok()) return;
    std::string arg;
    if (text != nullptr) {
        const char* raw = env->GetStringUTFChars(text, nullptr);
        if (raw != nullptr) {
            arg = raw;
            env->ReleaseStringUTFChars(text, raw);
        }
    }
    switch (what) {
        case kJobTalk: RunTalkToNpc(static_cast<long long>(id)); break;
        case kJobGate: RunUseGate(static_cast<long long>(id)); break;
        case kJobPartyAccept: RunPartyAction(0, static_cast<long long>(id)); break;
        case kJobPartyLeave: RunPartyAction(1, static_cast<long long>(id)); break;
        case kJobChatMap: RunSendChat(std::move(arg), 0); break;
        case kJobChatParty: RunSendChat(std::move(arg), 1); break;
        case kJobUiStrings: RunLoadUiStrings(); break;
        case kJobRefreshChat: {
            if (g_api == nullptr || g_characterId == 0) return;
            std::string json, err;
            if (g_api->GetChat(g_characterId, json, err)) mobile::net::Social().ApplyChatHistory(json);
            break;
        }
        default: break;   // the show* jobs are handled by Java itself
    }
}

}  // extern "C"

// ================================================================ round 4 C++ bridge
// These are declared in bridge_api.h with C++ linkage (main.cpp, the HUD and the render thread call
// them), so they must NOT be inside extern "C" — a definition there gets C linkage and the linker
// then reports "undefined symbol: BridgeTalkToNpc(long long)" even though the body exists.
// Call once from android_main: saves what a foreign thread needs to reach Java.
void BridgeAttachApp(android_app* app) {
    if (app == nullptr || app->activity == nullptr) return;
    g_vm = app->activity->vm;
    if (g_vm == nullptr) return;
    JNIEnv* env = nullptr;
    bool attached = false;
    if (g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
        attached = true;
    }
    if (g_clazz == nullptr && app->activity->clazz != nullptr) {
        g_clazz = env->NewGlobalRef(app->activity->clazz);
        jclass cls = env->GetObjectClass(g_clazz);
        g_postJob = env->GetMethodID(cls, "postNativeJob", "(IJLjava/lang/String;)V");
        if (g_postJob == nullptr && env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(cls);
    }
    if (attached) g_vm->DetachCurrentThread();
    LOGI("round 4 bridge attached (postNativeJob %s)",
         g_postJob == nullptr ? "MISSING" : "found");
}

bool BridgeTalkToNpc(long long npcId) { return PostJob(kJobTalk, npcId, nullptr); }
bool BridgeUseGate(long long gateId) { return PostJob(kJobGate, gateId, nullptr); }
bool BridgePartyAction(int kind, long long partyId) {
    return PostJob(kind == 0 ? kJobPartyAccept : kJobPartyLeave, partyId, nullptr);
}
bool BridgeSendChat(const char* text, int channel) {
    return PostJob(channel == 1 ? kJobChatParty : kJobChatMap, 0, text);
}
void BridgeOpenSocialPanel() { PostJob(kJobShowSocial, 0, nullptr); }
void BridgeOpenNpcDialog() { PostJob(kJobShowDialog, 0, nullptr); }
void BridgeOpenChatPanel() { PostJob(kJobShowChat, 0, nullptr); }

extern "C" {
// ================================================================ round 4 systems
// The social panel and the chat panel are drawn by the HUD; these entry points do the server work
// on the net thread and hand the answers to SocialView (parsed once, drawn by the render thread).
// One line per social-panel row: "kind|id|label" joined by '\n'.
//   kind 0 = NPC (talk -> shop), 1 = warp gate (use), 2 = party invite (accept), 3 = leave party.
JNIEXPORT jstring JNICALL
Java_com_aiep7_app_MainActivity_nativeSocialRows(JNIEnv* env, jobject) {
    const auto snap = mobile::net::Social().Get();
    std::string out;
    for (const auto& inv : snap.invites) {
        if (!out.empty()) out += "\n";
        out += "2|" + std::to_string(inv.partyId) + "|รับคำเชิญจาก " + inv.inviter;
    }
    if (snap.party.joined()) {
        if (!out.empty()) out += "\n";
        out += "3|" + std::to_string(snap.party.partyId) + "|ออกจากปาร์ตี้ (" +
               std::to_string(snap.party.members.size()) + "/" +
               std::to_string(snap.party.maxMembers) + ")";
    }
    for (const auto& npc : snap.npcs) {
        if (!out.empty()) out += "\n";
        out += "0|" + std::to_string(npc.npcId) + "|" + npc.name +
               (npc.shopName.empty() ? "" : " (" + npc.shopName + ")");
    }
    return env->NewStringUTF(out.c_str());
}
// The open dialog as "npcId|name|shopName|sellRate|money\nline\nline..." (the renderer wraps it).
JNIEXPORT jstring JNICALL
Java_com_aiep7_app_MainActivity_nativeDialog(JNIEnv* env, jobject) {
    const auto d = mobile::net::Social().Get().dialog;
    std::string out = std::to_string(d.npcId) + "|" + d.npcName + "|" + d.shopName + "|" +
                      std::to_string(d.sellRatePercent) + "|" + std::to_string(d.money);
    for (const auto& line : d.lines) out += "\n" + line;
    return env->NewStringUTF(out.c_str());
}

// The shop of the open dialog as "itemId|name|price" lines (Java draws one buy button per line).
JNIEXPORT jstring JNICALL
Java_com_aiep7_app_MainActivity_nativeShop(JNIEnv* env, jobject) {
    const auto d = mobile::net::Social().Get().dialog;
    std::string out;
    for (const auto& e : d.shop) {
        if (!out.empty()) out += "\n";
        out += std::to_string(e.itemId) + "|" + e.name + "|" + std::to_string(e.buyPrice);
    }
    return env->NewStringUTF(out.c_str());
}

// Buys `quantity` of an item from the open dialog's NPC. Returns the money paid (-1 = refused).
// Runs on the net thread: Java posts it (the dialog's buy button) so the buy is a server action.
JNIEXPORT jint JNICALL
Java_com_aiep7_app_MainActivity_nativeBuyItem(JNIEnv* env, jobject, jlong itemId, jint quantity) {
    CallScope scope(env);
    if (!scope.ok() || g_characterId == 0) return -1;
    const long long npcId = mobile::net::Social().DialogNpc();
    if (npcId == 0) {
        g_lastError = "no open dialog";
        return -1;
    }
    std::string json, err;
    if (!Api().BuyFromNpc(g_characterId, npcId, static_cast<long long>(itemId),
                          static_cast<int>(quantity), json, err)) {
        g_lastError = err;
        LOGI("buy item %lld failed: %s", (long long)itemId, err.c_str());
        return -1;
    }
    int paid = 0, received = 0, money = 0, qty = 0;
    mobile::net::ParseTrade(json, paid, received, money, qty);
    mobile::net::View().ApplyHeal(json);
    LOGI("buy item %lld qty=%d paid=%d money=%d", (long long)itemId, qty, paid, money);
    return paid;
}

// The chat log as "channel\tname\tbody" lines (oldest first).
JNIEXPORT jstring JNICALL
Java_com_aiep7_app_MainActivity_nativeChatLog(JNIEnv* env, jobject) {
    const auto msgs = mobile::net::Social().Messages();
    std::string out;
    for (const auto& m : msgs) {
        if (!out.empty()) out += "\n";
        out += m.channel + "\t" + m.name + "\t" + m.body;
    }
    return env->NewStringUTF(out.c_str());
}
}  // extern "C"

void BridgeSelectTarget(long long monsterId) { g_combat.SelectTarget(monsterId); }
void BridgeRequestAttack() { g_combat.RequestAttack(); }
void BridgeRequestSkill(int skillId) { g_combat.RequestSkill(skillId); }

// Milliseconds left on a skill's cooldown (0 = ready), read by the render thread each frame.
int BridgeSkillCooldownMs(int skillId) {
    if (skillId <= 0) return 0;
    const float seconds = g_combat.SkillCooldownLeft(NowMs(), skillId);
    return int(seconds * 1000.f);
}
bool BridgeUsePotion(int slot) {
    if (!g_api || g_characterId == 0) return false;
    std::string json, err;
    if (!g_api->UseItemAtSlot(g_characterId, slot, json, err)) {
        g_lastError = err;
        return false;
    }
    mobile::net::View().ApplyHeal(json);
    return true;
}

extern "C" {

// The player tapped a monster on the map: lock it (0 clears the selection).
JNIEXPORT void JNICALL
Java_com_aiep7_app_MainActivity_nativeSelectTarget(JNIEnv* env, jobject, jlong monsterId) {
    CallScope scope(env);
    g_combat.SelectTarget(static_cast<long long>(monsterId));
    LOGI("target selected %lld", static_cast<long long>(monsterId));
}

// The attack button (and its hold repeat): the next sync tick sends the attack.
JNIEXPORT void JNICALL
Java_com_aiep7_app_MainActivity_nativeAttack(JNIEnv* env, jobject) {
    CallScope scope(env);
    g_combat.RequestAttack();
}

// A skill slot was tapped: the next sync tick casts it on the locked target.
JNIEXPORT void JNICALL
Java_com_aiep7_app_MainActivity_nativeUseSkill(JNIEnv* env, jobject, jint skillId) {
    CallScope scope(env);
    g_combat.RequestSkill(static_cast<int>(skillId));
}

// The bag window: drinks one potion from the given inventory slot. Returns true when the server
// accepted it (the answer carries the new HP/MP, which the HUD picks up on the next sync).
JNIEXPORT jboolean JNICALL
Java_com_aiep7_app_MainActivity_nativeUsePotion(JNIEnv* env, jobject, jint slot) {
    CallScope scope(env);
    if (!scope.ok() || g_characterId == 0) return JNI_FALSE;
    std::string json, err;
    if (!Api().UseItemAtSlot(g_characterId, static_cast<int>(slot), json, err)) {
        g_lastError = err;
        LOGI("use potion failed: %s", err.c_str());
        return JNI_FALSE;
    }
    mobile::net::View().ApplyHeal(json);
    LOGI("use potion slot=%d ok", static_cast<int>(slot));
    return JNI_TRUE;
}

// Combat counters for the CI smoke test (one line, easy to grep): event, hits, kills, target HP.
JNIEXPORT jstring JNICALL
Java_com_aiep7_app_MainActivity_nativeCombatStatus(JNIEnv* env, jobject) {
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "{\"event\": \"%s\", \"hits\": %d, \"kills\": %d, \"syncs\": %d, "
                  "\"target\": %lld, \"target_hp\": %d, \"target_max_hp\": %d}",
                  mobile::net::CombatEventName(g_combat.LastEvent()), g_combat.Hits(), g_combat.Kills(),
                  g_combat.Syncs(), (long long)g_combat.Target(), g_combat.TargetHp(),
                  g_combat.TargetMaxHp());
    return env->NewStringUTF(buf);
}

// Debug-only (CI): aim the player's camera at the nearest monster and lock it, so the automated
// smoke test does not have to know where the monsters were drawn. Returns its id (0 = none in range).
JNIEXPORT jlong JNICALL
Java_com_aiep7_app_MainActivity_nativeAutoTarget(JNIEnv* env, jobject) {
    CallScope scope(env);
    const auto snap = mobile::net::View().Get();
    long long best = 0;
    double bestDistance = 0.0;
    for (const auto& m : snap.monsters) {
        if (m.dead) continue;
        if (best == 0 || m.distance < bestDistance) {
            best = m.monsterId;
            bestDistance = m.distance;
        }
    }
    g_combat.SelectTarget(best);
    LOGI("device test: auto target %lld (distance %.4f)", best, bestDistance);
    return static_cast<jlong>(best);
}

JNIEXPORT jstring JNICALL
Java_com_aiep7_app_MainActivity_nativeLastError(JNIEnv* env, jobject) {
    return env->NewStringUTF(g_lastError.c_str());
}

// ข้อความ UI ไทยจาก Drive: Java ขอเป็นรายคีย์ เช่น "SENTENSE,BUYITEM_NOMONEY"
// คืน NULL ถ้ายังโหลดไม่เสร็จหรือไม่พบคีย์ (Java จะใช้ข้อความสำรองของตัวเอง)
JNIEXPORT jstring JNICALL
Java_com_aiep7_app_MainActivity_nativeUiString(JNIEnv* env, jobject, jstring key) {
    if (key == nullptr || g_uiStrings.Size() == 0) return nullptr;
    const char* rawKey = env->GetStringUTFChars(key, nullptr);
    if (rawKey == nullptr) return nullptr;
    const std::string wanted = rawKey;
    env->ReleaseStringUTFChars(key, rawKey);
    const std::string& text = g_uiStrings.Raw(wanted);
    if (text.empty()) return nullptr;
    return env->NewStringUTF(text.c_str());
}

// จำนวนคีย์ที่โหลดได้ (Java ใช้บอกผู้ใช้ว่าข้อความมาจากไฟล์จริงกี่รายการ)
JNIEXPORT jint JNICALL
Java_com_aiep7_app_MainActivity_nativeUiStringCount(JNIEnv*, jobject) {
    return static_cast<jint>(g_uiStrings.Size());
}

}  // extern "C"

