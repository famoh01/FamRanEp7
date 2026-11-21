// win32_window.h - Win32 window backend with message pumping and input capture.
// Only compiled on Windows. Declared here without windows.h so that other
// platforms can include backends.h safely.
#pragma once
#include <vector>

#include "ran/platform/platform.h"

namespace ran::platform {

class Win32Window final : public IWindow {
public:
    ~Win32Window() override;

    bool Create(int width, int height, const char* title) override;
    void Destroy() override;
    void SetSurfaceLostHandler(void (*handler)(void* user), void* user) override;
    void GetSize(int& width, int& height) const override;
    void* NativeHandle() const override;

    // Win32-specific: called by the input backend once per frame.
    void PumpMessages();                                  // drain the OS queue
    bool CloseRequested() const { return closeRequested_; }
    void TakePointerEvents(std::vector<PointerEvent>& out); // appends and clears
    bool KeyIsDown(int virtualKey) const;

    // Called by the window procedure in window_win32.cpp. Public only so that
    // the C callback can reach it; not part of the game-facing API.
    static long long RouteMessage(void* hwnd, unsigned int msg,
                                  unsigned long long wp, long long lp);

private:
    long long HandleMessage(void* hwnd, unsigned int msg,
                            unsigned long long wp, long long lp);
    void Push(PointerAction action, float x, float y);

    void* hwnd_ = nullptr;
    bool closeRequested_ = false;
    bool mouseDown_ = false;
    bool keys_[256] = {};
    std::vector<PointerEvent> pending_;
    void (*lostHandler_)(void*) = nullptr;
    void* lostUser_ = nullptr;
};

}  // namespace ran::platform
