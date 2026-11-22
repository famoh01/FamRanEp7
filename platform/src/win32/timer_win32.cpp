#include "ran/platform/backends.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace ran::platform {
namespace {

class TimerWin32 final : public ITimer {
public:
    TimerWin32() {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        freq_ = uint64_t(f.QuadPart);
    }
    uint64_t NowMicros() const override {
        LARGE_INTEGER c{};
        QueryPerformanceCounter(&c);
        uint64_t count = uint64_t(c.QuadPart);
        // Split to avoid overflow: count * 1e6 can exceed 64 bits after uptime.
        return (count / freq_) * 1000000ull + (count % freq_) * 1000000ull / freq_;
    }
    void SleepMicros(uint64_t us) override {
        // Sleep is coarse (about 1 ms or more). Good enough for frame limiting.
        Sleep(DWORD((us + 999ull) / 1000ull));
    }
private:
    uint64_t freq_ = 1;
};

}  // namespace

std::unique_ptr<ITimer> CreateTimerWin32() { return std::make_unique<TimerWin32>(); }

}  // namespace ran::platform
#endif
