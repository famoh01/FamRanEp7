// AI EP7 - JNI bridge between MainActivity (Java dialogs) and mobile/net_client (C++).
//
// net_client does the protocol work (JSON bodies, Bearer token, error codes). Each request is
// handed to Java NetBridge.request(), which performs it with HttpURLConnection. Every JNI entry
// point runs on Java's `net` thread, one call at a time, so the JNIEnv saved in g_env is only used
// while a call is active.
#include <jni.h>
#include <android/log.h>

#include <cstdlib>
#include <atomic>
#include <memory>
#include <string>

#include "api_client.h"

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
std::atomic<bool> g_inWorld{false};

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
    return g_inWorld.load();
}

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
    g_inWorld = false;
    return JNI_TRUE;
}

// Enter World: the character must exist on the server. Its server record is fetched and stored as the
// selected character; the game screen reads it in Phase 7.
JNIEXPORT jboolean JNICALL
Java_com_aiep7_app_MainActivity_nativeEnterWorld(JNIEnv* env, jobject, jlong id) {
    CallScope scope(env);
    if (!scope.ok()) return JNI_FALSE;
    std::string json, err;
    if (!Api().GetCharacter(static_cast<long long>(id), json, err)) {
        g_lastError = err;
        return JNI_FALSE;
    }
    g_selectedId = static_cast<long long>(id);
    std::string name;
    mobile::net::JsonFindString(json, "name", name);
    g_selectedName = name;
    g_inWorld = true;
    LOGI("enter world character id=%lld name=%s", g_selectedId, g_selectedName.c_str());
    return JNI_TRUE;
}

JNIEXPORT jstring JNICALL
Java_com_aiep7_app_MainActivity_nativeLastError(JNIEnv* env, jobject) {
    return env->NewStringUTF(g_lastError.c_str());
}

}  // extern "C"
