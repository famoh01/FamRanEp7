// Backend tests. Built on Windows (CI) with the Win32 backends,
// and on Linux with the POSIX timer.
#include "ran/platform/backends.h"

#include <cassert>
#include <cstdio>

using namespace ran::platform;

static void TestTimer(const char* name, ITimer& t) {
    uint64_t a = t.NowMicros();
    t.SleepMicros(20000);                    // 20 ms
    uint64_t b = t.NowMicros();
    uint64_t dt = b - a;
    // Sleep may run long, never much short. Allow 5 ms of slack below.
    if (dt + 5000 < 20000 || dt > 2000000) {
        std::printf("FAIL %s: slept 20ms, measured %llu us\n", name, (unsigned long long)dt);
        assert(false);
    }
    std::printf("ok %s: 20ms sleep measured %llu us\n", name, (unsigned long long)dt);
}

#if defined(_WIN32)
static void TestWindow() {
    auto w = CreateWindowWin32();
    bool ok = w->Create(640, 360, "RAN platform test");
    if (!ok) { std::puts("FAIL window create"); assert(false); }
    int W = 0, H = 0;
    w->GetSize(W, H);
    // Client area must match what we asked for.
    if (W != 640 || H != 360) {
        std::printf("FAIL window size %dx%d\n", W, H);
        assert(false);
    }
    assert(w->NativeHandle() != nullptr);
    w->Destroy();
    std::puts("ok window: created 640x360 and destroyed");
}
#endif

int main() {
#if defined(_WIN32)
    auto t = CreateTimerWin32();
    TestTimer("timer(win32)", *t);
    TestWindow();
#else
    auto t = CreateTimerPosix();
    TestTimer("timer(posix)", *t);
#endif
    std::puts("platform backend tests: all checks passed");
    return 0;
}
