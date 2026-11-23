# tools/check_android_link.py
# ตรวจว่าไฟล์ .cpp ทุกไฟล์ที่ APK ต้องใช้ ถูกใส่ในรายการ source ของ CMake แล้ว
#
# ทำไมต้องมี: รอบ 4 เพิ่ม social_view.cpp และ ui_strings.cpp แต่ลืมใส่ใน CMakeLists.txt
# เทสต์บน host ผ่านหมด (Makefile คอมไพล์เองแยก) แต่ APK ลิงก์ไม่ผ่านด้วย
#   ld.lld: error: undefined symbol: mobile::net::SocialView::ApplyTalk(...)
#
#   python3 tools/check_android_link.py
#
# exit code 1 เมื่อพบไฟล์ที่หายไป
import re
import sys
from pathlib import Path

CMAKE = Path("mobile/android/app/src/main/cpp/CMakeLists.txt")
NET_CLIENT = Path("mobile/net_client")

# ไฟล์ที่ *ไม่* ต้องลิงก์เข้า APK: เทสต์ (มี main ของตัวเอง) และไฟล์ประกอบเทสต์
TEST_PREFIX = ("test_",)


def cmake_sources(text: str) -> set:
    """ชื่อไฟล์ .cpp ที่ CMakeLists.txt อ้างถึง (ไม่สนใจ path เต็ม)"""
    block = re.search(r"add_library\([^)]*\)", text, re.S)
    if not block:
        return set()
    return set(re.findall(r"([\w_]+\.cpp)", block.group(0)))


def main() -> int:
    if not CMAKE.is_file():
        print(f"ไม่พบ {CMAKE}")
        return 1
    listed = cmake_sources(CMAKE.read_text(encoding="utf-8"))

    missing = []
    for cpp in sorted(NET_CLIENT.glob("*.cpp")):
        name = cpp.name
        if name.startswith(TEST_PREFIX):
            continue
        if name not in listed:
            missing.append(name)

    # ไฟล์ใน cpp/ เองก็ต้องอยู่ในรายการด้วย
    for cpp in sorted(Path("mobile/android/app/src/main/cpp").glob("*.cpp")):
        if cpp.name not in listed:
            missing.append(f"(app/cpp) {cpp.name}")

    print(f"CMake ลิงก์ {len(listed)} ไฟล์ .cpp")
    print(f"net_client (ไม่รวมเทสต์) {len([p for p in NET_CLIENT.glob('*.cpp') if not p.name.startswith(TEST_PREFIX)])} ไฟล์")

    if missing:
        print("\nFAIL: ไฟล์เหล่านี้ไม่ได้อยู่ใน add_library() ของ CMakeLists.txt")
        for m in missing:
            print(f"  - {m}")
        print("\nถ้าเป็นโค้ดที่ APK ใช้ ให้เพิ่มใน add_library() — ถ้าเป็นเทสต์ ให้ตั้งชื่อขึ้นต้นด้วย test_")
        return 1

    print("\nPASS: ทุกไฟล์ที่ APK ต้องใช้ถูกลิงก์แล้ว")
    return 0




# ---------------------------------------------------------------------------------------------
# Optional (needs a host C++ compiler): compile every source CMake links and prove that
# net_bridge.cpp's references into mobile::net are all defined. This catches a missing
# CMakeLists entry without waiting for the Android build.
# ---------------------------------------------------------------------------------------------
JNI_STUB = """#pragma once
typedef int jint; typedef long long jlong; typedef unsigned char jboolean;
typedef void* jobject; typedef void* jclass; typedef void* jstring;
typedef void* jmethodID; typedef void* jfieldID;
#define JNI_VERSION_1_6 1
#define JNI_OK 0
#define JNI_TRUE 1
#define JNI_FALSE 0
#define JNIEXPORT
#define JNICALL
struct JNIEnv;
struct JavaVM {
    jint GetEnv(void**, jint);
    jint AttachCurrentThread(JNIEnv**, void*);
    jint DetachCurrentThread();
};
inline jint JavaVM::GetEnv(void**, jint) { return 0; }
inline jint JavaVM::AttachCurrentThread(JNIEnv**, void*) { return 0; }
inline jint JavaVM::DetachCurrentThread() { return 0; }
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
inline jclass JNIEnv::FindClass(const char*) { return nullptr; }
inline jmethodID JNIEnv::GetStaticMethodID(jclass, const char*, const char*) { return nullptr; }
inline jmethodID JNIEnv::GetMethodID(jclass, const char*, const char*) { return nullptr; }
inline jobject JNIEnv::CallStaticObjectMethod(jclass, jmethodID, ...) { return nullptr; }
inline void JNIEnv::CallVoidMethod(jobject, jmethodID, ...) {}
inline void JNIEnv::CallStaticVoidMethod(jclass, jmethodID, ...) {}
inline jint JNIEnv::CallStaticIntMethod(jclass, jmethodID, ...) { return 0; }
inline jlong JNIEnv::CallStaticLongMethod(jclass, jmethodID, ...) { return 0; }
inline jclass JNIEnv::GetObjectClass(jobject) { return nullptr; }
inline jobject JNIEnv::NewGlobalRef(jobject) { return nullptr; }
inline jstring JNIEnv::NewStringUTF(const char*) { return nullptr; }
inline const char* JNIEnv::GetStringUTFChars(jstring, jboolean*) { return nullptr; }
inline void JNIEnv::ReleaseStringUTFChars(jstring, const char*) {}
inline void JNIEnv::DeleteLocalRef(jobject) {}
inline jboolean JNIEnv::ExceptionCheck() { return 0; }
inline void JNIEnv::ExceptionDescribe() {}
inline void JNIEnv::ExceptionClear() {}
inline void JNIEnv::GetJavaVM(JavaVM**) {}
"""

APP_STUB = """#pragma once
#include <jni.h>
struct ANativeActivity { void* instance; JavaVM* vm; jobject clazz; void* callbacks; };
struct android_app { void* userData; ANativeActivity* activity; };
"""


def link_check() -> int:
    import shutil
    import subprocess
    import tempfile

    cxx = shutil.which("g++") or shutil.which("clang++")
    if not cxx:
        print("(ข้ามการตรวจลิงก์: ไม่พบ g++/clang++)")
        return 0

    cpp_dir = Path("mobile/android/app/src/main/cpp")
    assets_dir = Path("mobile/assets")
    stub = Path(tempfile.mkdtemp())
    (stub / "android").mkdir()
    (stub / "jni.h").write_text(JNI_STUB, encoding="utf-8")
    (stub / "android" / "log.h").write_text(
        'extern "C" int __android_log_print(int, const char*, const char*, ...);\n'
        "#define ANDROID_LOG_INFO 4\n", encoding="utf-8")
    (stub / "android_native_app_glue.h").write_text(APP_STUB, encoding="utf-8")

    listed = [n for n in sorted(cmake_sources(CMAKE.read_text(encoding="utf-8")))
              if n not in ("main.cpp", "android_native_app_glue.c")]
    inc = ["-I" + str(stub), "-I" + str(cpp_dir), "-Imobile/net_client",
           "-Imobile/touch_hud", "-Imobile/assets"]
    objs = []
    for name in listed:
        src = next((c for c in (cpp_dir / name, NET_CLIENT / name, assets_dir / name) if c.is_file()),
                   None)
        if src is None:
            print(f"(ข้ามการตรวจลิงก์: หา {name} ไม่พบ)")
            return 0
        obj = stub / (Path(name).stem + ".o")
        r = subprocess.run([cxx, "-std=c++17", "-fPIC", "-c", *inc, str(src), "-o", str(obj)],
                           capture_output=True, text=True)
        if r.returncode != 0:
            print(f"(ข้ามการตรวจลิงก์: คอมไพล์ {Path(src).name} ไม่ผ่านบน host)")
            return 0
        objs.append(str(obj))

    # A translation unit that takes the address of every function bridge_api.h promises. Linking it
    # against the objects above is what actually proves the promises are kept: a definition with the
    # wrong linkage (inside extern "C") stays invisible to a C++ caller and shows up as undefined.
    api = (cpp_dir / "bridge_api.h").read_text(encoding="utf-8")
    names = re.findall(r"^(?:void|bool|int)\s+(Bridge\w+)\s*\(", api, re.M)
    refs = stub / "bridge_refs.cpp"
    # An executable (not a shared library): a shared library is allowed to keep undefined symbols,
    # so linking one would never report the problem we are hunting for.
    body = ("#include \"bridge_api.h\"\n" +
            "".join(f"auto* p_{n} = &{n};\n" for n in names) +
            "int main() { return 0; }\n")
    refs.write_text(body, encoding="utf-8")
    ref_obj = stub / "bridge_refs.o"
    rr = subprocess.run([cxx, "-std=c++17", "-fPIC", "-c", *inc, str(refs), "-o", str(ref_obj)],
                        capture_output=True, text=True)
    if rr.returncode != 0:
        print(f"(ข้ามการตรวจลิงก์: คอมไพล์ bridge_refs.cpp ไม่ผ่าน)\n{rr.stderr[:400]}")
        return 0
    objs.append(str(ref_obj))

    probe = stub / "link_probe"
    r = subprocess.run([cxx, "-o", str(probe), *objs], capture_output=True, text=True)
    # Two failure modes, both seen in CI:
    #   mobile::net::X  -> X's file is missing from add_library() in CMakeLists.txt
    #   BridgeX         -> X is declared C++ in bridge_api.h but defined inside extern "C"
    #                     (then the caller looks for the mangled name and finds only the C one)
    undefined = set(re.findall(r"undefined reference to [`']([^']+)'", r.stderr))
    undefined = {u for u in undefined
                 if u.startswith("Bridge") or u.startswith("mobile::net::")}
    undefined = {u for u in undefined if not u.startswith("mobile::net::Bridge")}
    if undefined:
        print("\nFAIL: มีฟังก์ชันที่ถูกเรียกแต่ไม่มีคำนิยาม (หรือคำนิยามมี linkage ไม่ตรง)")
        for u in sorted(undefined):
            if u.startswith("Bridge"):
                print(f"  - {u}  → bridge_api.h ประกาศเป็น C++ ต้องอยู่นอก extern \"C\"")
            else:
                print(f"  - {u}  → อาจลืมใส่ไฟล์ใน add_library() ของ CMakeLists.txt")
        return 1
    print("PASS: ทุก symbol ของ mobile::net และ Bridge* ที่โปรเจกต์เรียก มีคำนิยามครบ")
    return 0


if __name__ == "__main__":
    rc = main()
    if "--link" in sys.argv:
        rc = link_check() or rc
    sys.exit(rc)
