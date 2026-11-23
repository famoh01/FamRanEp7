# tools/check_includes.py
# ตรวจว่าไฟล์ C++ ที่ใช้สัญลักษณ์มาตรฐานได้ #include header ที่ให้สัญลักษณ์นั้นเอง
#
# ทำไมต้องมี: CI (ubuntu-latest, libstdc++ ใหม่กว่า) ล้มเพราะ test_social.cpp เรียก std::any_of
# โดยไม่ include <algorithm> — GCC 12 ในเครื่องที่ไม่ล้มเพราะมี include ต่อกันมาให้
# สคริปต์นี้จับปัญหาชนิดเดียวกันทั้งหมดก่อนถึง CI
#
#   python3 tools/check_includes.py           # ตรวจ
#   python3 tools/check_includes.py --verbose
#
# exit code 1 เมื่อพบปัญหา
import re
import sys
from pathlib import Path

# สัญลักษณ์ -> header ที่ต้องมี (เว้นแต่มี header ทางเลือกตามที่ระบุ)
NEEDS = {
    "std::function": {"functional"},
    "std::unique_ptr": {"memory"},
    "std::make_unique": {"memory"},
    "std::shared_ptr": {"memory"},
    "std::map": {"map"},
    "std::set": {"set"},
    "std::unordered_map": {"unordered_map"},
    "std::numeric_limits": {"limits"},
    "std::abs": {"cstdlib", "cmath"},
    "std::memcpy": {"cstring"},
    "std::memset": {"cstring"},
    "std::strcmp": {"cstring"},
    "std::strlen": {"cstring"},
    "std::strtod": {"cstdlib"},
    "std::strtoll": {"cstdlib"},
    "std::snprintf": {"cstdio"},
    "std::printf": {"cstdio"},
    "std::move": {"utility"},
    "std::to_string": {"string"},
    "std::floor": {"cmath"},
    "std::sqrt": {"cmath"},
    "std::fabs": {"cmath"},
    "std::isfinite": {"cmath"},
    "std::hypot": {"cmath"},
    "std::thread": {"thread"},
    "std::size_t": {"cstddef"},
    "std::any_of": {"algorithm"},
    "std::all_of": {"algorithm"},
    "std::find_if": {"algorithm"},
    "std::sort": {"algorithm"},
    "std::min_element": {"algorithm"},
    "std::max_element": {"algorithm"},
    "std::chrono::": {"chrono"},
}

DIRS = (
    "mobile/net_client",
    "mobile/touch_hud",
    "mobile/android/app/src/main/cpp",
    "mobile/assets",
    "platform",
    "renderer",
)


def scan() -> list:
    bad = []
    for d in DIRS:
        root = Path(d)
        if not root.is_dir():
            continue
        for f in sorted(list(root.glob("*.cpp")) + list(root.glob("*.h"))):
            text = f.read_text(encoding="utf-8", errors="replace")
            if not text.strip():
                continue
            # Header ที่ include มาตรง ๆ ในไฟล์นี้ (ไม่นับที่ได้มาทางไฟล์อื่น)
            included = set(re.findall(r"#include\s*<([a-z_0-9]+)>", text))
            for sym, headers in NEEDS.items():
                if sym in text and not (headers & included):
                    bad.append((str(f), sym, "/".join(sorted(headers))))
    return bad


def main() -> int:
    verbose = "--verbose" in sys.argv
    bad = scan()
    if verbose:
        print("ตรวจ %d โฟลเดอร์" % len(DIRS))
    if not bad:
        print("PASS: ทุกไฟล์ include header ของสัญลักษณ์ที่ใช้เอง")
        return 0
    seen = set()
    for f, sym, hdr in bad:
        key = (f, hdr)
        if key in seen:
            continue
        seen.add(key)
        print(f"{f}: ใช้ {sym} แต่ไม่มี #include <{hdr}>")
    print(f"\nFAIL: {len(seen)} จุด")
    return 1


if __name__ == "__main__":
    sys.exit(main())
