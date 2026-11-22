#include "ran/platform/win32_window.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <memory>
#include <string>
#include <vector>

namespace ran::platform {
namespace {

const wchar_t* kClassName = L"RanPlatformWindow";

std::wstring ToWide(const char* utf8) {
    if (!utf8) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (n <= 0) return L"";
    std::wstring w(size_t(n - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &w[0], n);
    return w;
}

HWND H(void* p) { return static_cast<HWND>(p); }

// The C-level window procedure. Calling convention is WNDPROC's own.
LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    return static_cast<LRESULT>(Win32Window::RouteMessage(
        h, msg, static_cast<unsigned long long>(wp), static_cast<long long>(lp)));
}

}  // namespace

Win32Window::~Win32Window() { Destroy(); }

bool Win32Window::Create(int width, int height, const char* title) {
    if (hwnd_) return false;
    HINSTANCE hinst = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = &WndProc;
    wc.hInstance = hinst;
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);   // fails harmlessly if already registered

    RECT rc{0, 0, width, height};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    std::wstring wtitle = ToWide(title);
    HWND hwnd = CreateWindowExW(0, kClassName, wtitle.c_str(), WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                rc.right - rc.left, rc.bottom - rc.top,
                                nullptr, nullptr, hinst, nullptr);
    if (!hwnd) return false;
    hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    ShowWindow(hwnd, SW_SHOW);
    return true;
}

void Win32Window::Destroy() {
    if (!hwnd_) return;
    HWND hwnd = H(hwnd_);
    hwnd_ = nullptr;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);   // stop routing messages to us
    DestroyWindow(hwnd);
}

void Win32Window::SetSurfaceLostHandler(void (*handler)(void*), void* user) {
    // Windows does not revoke the window surface like Android does. The
    // renderer handles device loss itself. Stored for API parity.
    lostHandler_ = handler;
    lostUser_ = user;
}

void Win32Window::GetSize(int& width, int& height) const {
    RECT rc{};
    if (hwnd_) GetClientRect(H(hwnd_), &rc);
    width = rc.right - rc.left;
    height = rc.bottom - rc.top;
}

void* Win32Window::NativeHandle() const { return hwnd_; }

void Win32Window::PumpMessages() {
    MSG m{};
    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
        if (m.message == WM_QUIT) closeRequested_ = true;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}

void Win32Window::TakePointerEvents(std::vector<PointerEvent>& out) {
    out.insert(out.end(), pending_.begin(), pending_.end());
    pending_.clear();
}

bool Win32Window::KeyIsDown(int virtualKey) const {
    return virtualKey >= 0 && virtualKey < 256 && keys_[virtualKey];
}

void Win32Window::Push(PointerAction action, float x, float y) {
    pending_.push_back(PointerEvent{0, action, x, y});   // id 0 = mouse
}

long long Win32Window::RouteMessage(void* hwnd, unsigned int msg,
                                   unsigned long long wp, long long lp) {
    HWND h = static_cast<HWND>(hwnd);
    auto* self = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (self) return self->HandleMessage(hwnd, msg, wp, lp);
    return static_cast<long long>(DefWindowProcW(h, msg, static_cast<WPARAM>(wp),
                                                 static_cast<LPARAM>(lp)));
}

long long Win32Window::HandleMessage(void* hwnd, unsigned int msg,
                                     unsigned long long wp, long long lp) {
    HWND h = static_cast<HWND>(hwnd);
    WPARAM w = static_cast<WPARAM>(wp);
    LPARAM l = static_cast<LPARAM>(lp);
    float x = float(static_cast<short>(LOWORD(l)));
    float y = float(static_cast<short>(HIWORD(l)));

    switch (msg) {
    case WM_CLOSE:
        closeRequested_ = true;     // the game decides when to really close
        return 0;
    case WM_LBUTTONDOWN:
        SetCapture(h);
        mouseDown_ = true;
        Push(PointerAction::Down, x, y);
        return 0;
    case WM_LBUTTONUP:
        if (mouseDown_) {
            mouseDown_ = false;
            ReleaseCapture();
            Push(PointerAction::Up, x, y);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (mouseDown_ && (w & MK_LBUTTON)) Push(PointerAction::Move, x, y);
        return 0;
    case WM_CAPTURECHANGED:
        if (mouseDown_) {           // capture lost mid-drag: cancel the pointer
            mouseDown_ = false;
            Push(PointerAction::Cancel, x, y);
        }
        return 0;
    case WM_KEYDOWN:
        if (w < 256) keys_[w] = true;
        return 0;
    case WM_KEYUP:
        if (w < 256) keys_[w] = false;
        return 0;
    case WM_KILLFOCUS:
        for (bool& k : keys_) k = false;   // no stuck keys after alt-tab
        if (mouseDown_) {
            mouseDown_ = false;
            Push(PointerAction::Cancel, x, y);
        }
        return 0;
    default:
        break;
    }
    return static_cast<long long>(DefWindowProcW(h, msg, w, l));
}

std::unique_ptr<Win32Window> CreateWindowWin32() { return std::make_unique<Win32Window>(); }

}  // namespace ran::platform
#endif
