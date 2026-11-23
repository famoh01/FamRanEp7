#!/usr/bin/env bash
# tools/check_android_cpp.sh
# ตรวจว่า C++ ฝั่ง Android คอมไพล์ผ่าน *โดยไม่ต้องมี Android SDK/NDK*
#
# ทำไมต้องมี: net_bridge.cpp เคยอ้างชนิด android_app โดยไม่ include <android_native_app_glue.h>
# การตรวจด้วย -include (force include) ทำให้ผ่านทั้งที่ CMake จริงล้ม — สคริปต์นี้ใช้ stub
# ของ jni.h / android/log.h / android_native_app_glue.h แล้วคอมไพล์แบบเดียวกับ CMake
# (ไม่ force include อะไร) จึงจับปัญหาชนิดนั้นได้ก่อน push
#
#   tools/check_android_cpp.sh          # ตรวจ net_bridge.cpp
#   tools/check_android_cpp.sh --all    # ตรวจทุกไฟล์ที่คอมไพล์ได้โดยไม่ต้องมี NDK
#
# exit code 1 เมื่อคอมไพล์ไม่ผ่าน
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STUB="$(mktemp -d)"
trap 'rm -rf "$STUB"' EXIT

mkdir -p "$STUB/android"

cat > "$STUB/jni.h" <<'EOF'
#pragma once
#include <cstdarg>
typedef int jint; typedef long long jlong; typedef unsigned char jboolean; typedef void* jobject;
typedef void* jclass; typedef void* jstring; typedef void* jmethodID; typedef void* jfieldID;
typedef void* jbyteArray; typedef void* jweak; typedef void* jthrowable;
struct JNIEnv;
struct JavaVM {
    jint GetEnv(void**, jint);
    jint AttachCurrentThread(JNIEnv**, void*);
    jint DetachCurrentThread();
};
#define JNI_VERSION_1_6 0x00010006
#define JNI_OK 0
#define JNI_TRUE 1
#define JNI_FALSE 0
#define JNIEXPORT
#define JNICALL
struct JNIEnv {
    jclass FindClass(const char*);
    jmethodID GetStaticMethodID(jclass, const char*, const char*);
    jmethodID GetMethodID(jclass, const char*, const char*);
    jobject CallStaticObjectMethod(jclass, jmethodID, ...);
    void CallVoidMethod(jobject, jmethodID, ...);
    void CallStaticVoidMethod(jclass, jmethodID, ...);
    jint CallStaticIntMethod(jclass, jmethodID, ...);
    jlong CallStaticLongMethod(jclass, jmethodID, ...);
    jclass GetObjectClass(jobject);
    jobject NewGlobalRef(jobject);
    jstring NewStringUTF(const char*);
    const char* GetStringUTFChars(jstring, jboolean*);
    void ReleaseStringUTFChars(jstring, const char*);
    void DeleteLocalRef(jobject);
    jboolean ExceptionCheck();
    void ExceptionDescribe();
    void ExceptionClear();
    void GetJavaVM(JavaVM**);
};
EOF

cat > "$STUB/android/log.h" <<'EOF'
#pragma once
extern "C" int __android_log_print(int, const char*, const char*, ...);
#define ANDROID_LOG_INFO 4
#define ANDROID_LOG_ERROR 6
#define ANDROID_LOG_WARN 5
EOF

# Only the members net_bridge.cpp touches. If it starts using more, the compile fails here —
# which is the point: it keeps this check honest about what the code needs.
cat > "$STUB/android_native_app_glue.h" <<'EOF'
#pragma once
#include <jni.h>
struct ANativeActivity {
    void* instance;
    JavaVM* vm;
    jobject clazz;
    void* callbacks;
    const char* (*getStringValue)(ANativeActivity*, const char*);
};
struct android_app {
    void* userData;
    ANativeActivity* activity;
};
EOF

CPP_DIR="$REPO/mobile/android/app/src/main/cpp"
INC="-I$STUB -I$REPO/mobile/net_client -I$REPO/mobile/touch_hud -I$CPP_DIR"
CXX="${CXX:-g++}"
FLAGS="-std=c++17 -Wall -Wextra -Werror -fsyntax-only"

files=("$CPP_DIR/net_bridge.cpp")
if [ "${1:-}" = "--all" ]; then
    files=("$CPP_DIR/net_bridge.cpp")
fi

rc=0
for f in "${files[@]}"; do
    printf '== %s\n' "$(basename "$f")"
    # NOTE: no -include. The file must bring its own headers, exactly as CMake expects.
    if $CXX $FLAGS $INC "$f"; then
        printf '   ok\n'
    else
        printf '   FAILED\n'
        rc=1
    fi
done

if [ "$rc" -eq 0 ]; then
    echo "PASS: C++ ฝั่ง Android คอมไพล์ผ่านด้วย NDK stub"
else
    echo "FAIL: C++ ฝั่ง Android คอมไพล์ไม่ผ่าน"
fi
exit "$rc"
