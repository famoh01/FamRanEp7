#include "ran/platform/backends.h"
#include "ran/platform/win32_window.h"

#if defined(_WIN32)

namespace ran::platform {
namespace {

class InputWin32 final : public IInput {
public:
    explicit InputWin32(Win32Window& w) : w_(w) {}
    void Poll(std::vector<PointerEvent>& out) override {
        out.clear();
        w_.PumpMessages();
        w_.TakePointerEvents(out);
    }
    bool IsKeyDown(int keyCode) const override { return w_.KeyIsDown(keyCode); }
private:
    Win32Window& w_;
};

}  // namespace

std::unique_ptr<IInput> CreateInputWin32(Win32Window& window) {
    return std::make_unique<InputWin32>(window);
}

}  // namespace ran::platform
#endif
