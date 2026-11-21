// Backend tests.
//  - All platforms: timer (POSIX or Win32 backend) and the portable filesystem.
//  - Windows only: window creation, and input from real posted window messages.
#include "ran/platform/backends.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "ran/platform/win32_window.h"
#endif

using namespace ran::platform;

static void Check(bool cond, const char* what) {
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        assert(false);
    }
}

static void TestTimer(const char* name, ITimer& t) {
    uint64_t a = t.NowMicros();
    t.SleepMicros(20000);                    // 20 ms
    uint64_t dt = t.NowMicros() - a;
    if (dt + 5000 < 20000 || dt > 2000000) {  // allow 5 ms early slack
        std::printf("FAIL %s: slept 20ms, measured %llu us\n", name, (unsigned long long)dt);
        assert(false);
    }
    std::printf("ok %s: 20ms sleep measured %llu us\n", name, (unsigned long long)dt);
}

static void TestFileSystem() {
    namespace fs = std::filesystem;
    fs::path root = fs::temp_directory_path() / "ran_platform_fs_test";
    fs::remove_all(root);
    fs::create_directories(root / "data" / "gui");
    {
        std::ofstream f(root / "data" / "gui" / "ui.txt", std::ios::binary);
        f << "hello";
    }
    auto fsys = CreateFileSystem(root.string(), (root / "save").string());

    std::vector<uint8_t> bytes;
    Check(fsys->ReadAsset("data/gui/ui.txt", bytes), "read forward-slash path");
    Check(bytes.size() == 5 && bytes[0] == 'h', "content matches");

    bytes.clear();
    Check(fsys->ReadAsset("data\\gui\\ui.txt", bytes), "read backslash path");
    Check(bytes.size() == 5, "backslash content matches");

    bytes.clear();
    Check(!fsys->ReadAsset("../outside.txt", bytes), "reject escape from asset root");
    Check(!fsys->ReadAsset("data/missing.txt", bytes), "missing file returns false");
    Check(fsys->WritableRoot() == (root / "save").string(), "writable root reported");
    fs::remove_all(root);
    std::puts("ok filesystem: read, normalize, reject escape");
}

#if defined(_WIN32)
static void TestWindowAndInput() {
    auto w = CreateWindowWin32();
    Check(w->Create(640, 360, "RAN platform test"), "window create");
    int W = 0, H = 0;
    w->GetSize(W, H);
    Check(W == 640 && H == 360, "client size matches request");

    HWND hwnd = static_cast<HWND>(w->NativeHandle());
    Check(hwnd != nullptr, "native handle present");

    auto input = CreateInputWin32(*w);
    Check(input != nullptr, "input created");

    // Mouse press, move while held, release.
    PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(100, 120));
    PostMessageW(hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(110, 125));
    PostMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(110, 125));
    // Keyboard.
    PostMessageW(hwnd, WM_KEYDOWN, VK_SPACE, 0);

    std::vector<PointerEvent> ev;
    input->Poll(ev);
    Check(ev.size() == 3, "three pointer events delivered");
    Check(ev[0].action == PointerAction::Down && ev[0].x == 100 && ev[0].y == 120,
          "down event with coordinates");
    Check(ev[1].action == PointerAction::Move && ev[1].x == 110, "move event");
    Check(ev[2].action == PointerAction::Up, "up event");
    Check(input->IsKeyDown(VK_SPACE), "space reported down");

    PostMessageW(hwnd, WM_KEYUP, VK_SPACE, 0);
    input->Poll(ev);
    Check(ev.empty(), "poll clears previous events");
    Check(!input->IsKeyDown(VK_SPACE), "space released");

    // Close button only sets a flag; the game decides when to exit.
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
    input->Poll(ev);
    Check(w->CloseRequested(), "WM_CLOSE sets close flag");

    w->Destroy();
    std::puts("ok window+input: create, mouse down/move/up, key state, close flag");
}
#endif

int main() {
#if defined(_WIN32)
    auto t = CreateTimerWin32();
    TestTimer("timer(win32)", *t);
    TestWindowAndInput();
#else
    auto t = CreateTimerPosix();
    TestTimer("timer(posix)", *t);
#endif
    TestFileSystem();
    std::puts("platform backend tests: all checks passed");
    return 0;
}
