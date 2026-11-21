#include "ran/platform/backends.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <string>

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

class WindowWin32 final : public IWindow {
public:
    ~WindowWin32() override { Destroy(); }

    bool Create(int width, int height, const char* title) override {
        if (hwnd_) return false;
        HINSTANCE hinst = GetModuleHandleW(nullptr);

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_OWNDC;
        wc.lpfnWndProc = &WindowWin32::WndProc;
        wc.hInstance = hinst;
        wc.lpszClassName = kClassName;
        // Registering twice fails with ERROR_CLASS_ALREADY_EXISTS; that is fine.
        RegisterClassExW(&wc);

        RECT rc{0, 0, width, height};
        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
        std::wstring wtitle = ToWide(title);
        hwnd_ = CreateWindowExW(0, kClassName, wtitle.c_str(), WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                rc.right - rc.left, rc.bottom - rc.top,
                                nullptr, nullptr, hinst, nullptr);
        if (!hwnd_) return false;
        ShowWindow(hwnd_, SW_SHOW);
        return true;
    }

    void Destroy() override {
        if (hwnd_) {
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
    }

    void SetSurfaceLostHandler(void (*handler)(void* user), void* user) override {
        // Windows does not revoke the window surface like Android does.
        // The handler is stored for the renderer (device lost is handled there).
        lostHandler_ = handler;
        lostUser_ = user;
    }

    void GetSize(int& width, int& height) const override {
        RECT rc{};
        if (hwnd_) GetClientRect(hwnd_, &rc);
        width = rc.right - rc.left;
        height = rc.bottom - rc.top;
    }

    void* NativeHandle() const override { return hwnd_; }

private:
    static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        return DefWindowProcW(h, msg, wp, lp);
    }

    HWND hwnd_ = nullptr;
    void (*lostHandler_)(void*) = nullptr;
    void* lostUser_ = nullptr;
};

}  // namespace

std::unique_ptr<IWindow> CreateWindowWin32() { return std::make_unique<WindowWin32>(); }

}  // namespace ran::platform
#endif
