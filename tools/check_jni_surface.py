# tools/check_jni_surface.py
# ตรวจว่า native method ที่ประกาศใน MainActivity.java กับฟังก์ชัน JNI ที่ลงทะเบียนใน net_bridge.cpp
# ตรงกันทั้งชื่อและจำนวน (ส่วนที่พังง่ายที่สุดเมื่อไม่มี JDK/Android SDK ใน environment นี้)
#
#   python3 tools/check_jni_surface.py
#
# คืน exit code 1 เมื่อพบความไม่ตรงกัน
import re
import sys
from pathlib import Path

JAVA = Path("mobile/android/app/src/main/java/com/aiep7/app/MainActivity.java")
CPP = Path("mobile/android/app/src/main/cpp/net_bridge.cpp")
PKG = "Java_com_aiep7_app_MainActivity_"


def java_natives(text):
    """native method ที่ประกาศใน Java: ชื่อ -> จำนวนพารามิเตอร์"""
    out = {}
    for m in re.finditer(r"^\s*(?:public\s+|private\s+|protected\s+)?native\s+"
                         r"[\w\[\]<>.]+\s+(\w+)\s*\(([^)]*)\)\s*;", text, re.M):
        name, params = m.group(1), m.group(2).strip()
        count = 0 if not params else len(params.split(","))
        out[name] = count
    return out


def cpp_exports(text):
    """JNIEXPORT ... JAVA_..._Name(JNIEnv* env, jobject, ...) -> ชื่อ -> จำนวนพารามิเตอร์ทั้งหมด"""
    out = {}
    pat = re.compile(r"JNIEXPORT\s+[\w\s\*]+?JNICALL\s*\n?\s*" + re.escape(PKG) +
                     r"(\w+)\s*\(([^)]*)\)", re.S)
    for m in pat.finditer(text):
        name, params = m.group(1), m.group(2)
        count = len([p for p in params.split(",") if p.strip()])
        out[name] = count
    return out


def brace_balance(text):
    """ตรวจว่า {} ในไฟล์ Java สมดุล (จับการแทรกโค้ดที่ทำให้คลาสพัง)"""
    depth = 0
    in_str = in_char = in_line = in_block = False
    i = 0
    while i < len(text):
        c = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if in_line:
            if c == "\n":
                in_line = False
        elif in_block:
            if c == "*" and nxt == "/":
                in_block = False
                i += 1
        elif in_str:
            if c == "\\":
                i += 1
            elif c == '"':
                in_str = False
        elif in_char:
            if c == "\\":
                i += 1
            elif c == "'":
                in_char = False
        elif c == "/" and nxt == "/":
            in_line = True
            i += 1
        elif c == "/" and nxt == "*":
            in_block = True
            i += 1
        elif c == '"':
            in_str = True
        elif c == "'":
            in_char = True
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth < 0:
                return depth, i
        i += 1
    return depth, None


def main():
    java = JAVA.read_text(encoding="utf-8")
    cpp = CPP.read_text(encoding="utf-8")

    natives = java_natives(java)
    exports = cpp_exports(cpp)

    fail = 0
    print("native ที่ประกาศใน Java : %d" % len(natives))
    print("ฟังก์ชัน JNI ใน net_bridge.cpp: %d" % len(exports))

    missing = sorted(set(natives) - set(exports))
    if missing:
        fail += 1
        print("\n[ERROR] ประกาศใน Java แต่ไม่มีฟังก์ชัน JNI (จะเกิด UnsatisfiedLinkError):")
        for n in missing:
            print("  - %s" % n)

    extra = sorted(set(exports) - set(natives))
    if extra:
        print("\n[WARN] มีฟังก์ชัน JNI แต่ Java ไม่ได้ประกาศ (ไม่ทำให้พัง):")
        for n in extra:
            print("  - %s" % n)

    # พารามิเตอร์: JNI มี JNIEnv*, jobject เพิ่มมา 2 ตัวเสมอ
    for name in sorted(set(natives) & set(exports)):
        if exports[name] != natives[name] + 2:
            fail += 1
            print("\n[ERROR] จำนวนพารามิเตอร์ไม่ตรง %s: Java %d, JNI %d (ต้องเป็น Java+2)"
                  % (name, natives[name], exports[name]))

    depth, pos = brace_balance(java)
    if depth != 0:
        fail += 1
        print("\n[ERROR] วงเล็บปีกกาใน MainActivity.java ไม่สมดุล (depth=%d, offset=%s)" % (depth, pos))
    else:
        print("\n{} ใน MainActivity.java สมดุล")

    # JOB_* ฝั่ง Java ต้องตรงกับ kJob* ฝั่ง C++ (หมายเลขต้องเหมือนกัน)
    job_java = dict((m.group(1), int(m.group(2)))
                    for m in re.finditer(r"static final int JOB_(\w+)\s*=\s*(\d+)\s*;", java))
    job_cpp = dict((m.group(1).replace("kJob", "").upper(), int(m.group(2)))
                   for m in re.finditer(r"constexpr jint kJob(\w+)\s*=\s*(\d+)\s*;", cpp))
    for name in sorted(set(job_java) & set(job_cpp)):
        if job_java[name] != job_cpp[name]:
            fail += 1
            print("\n[ERROR] JOB %s: Java=%d C++=%d" % (name, job_java[name], job_cpp[name]))
    print("JOB_* ฝั่ง Java: %d, kJob* ฝั่ง C++: %d" % (len(job_java), len(job_cpp)))

    print("\n%s" % ("FAIL" if fail else "PASS"))
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
