#include "ran/platform/backends.h"

#if defined(__unix__) || defined(__APPLE__) || defined(__ANDROID__)
#include <ctime>

namespace ran::platform {
namespace {

class TimerPosix final : public ITimer {
public:
    uint64_t NowMicros() const override {
        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return uint64_t(ts.tv_sec) * 1000000ull + uint64_t(ts.tv_nsec) / 1000ull;
    }
    void SleepMicros(uint64_t us) override {
        timespec ts{};
        ts.tv_sec = time_t(us / 1000000ull);
        ts.tv_nsec = long((us % 1000000ull) * 1000ull);
        while (nanosleep(&ts, &ts) != 0) {}   // resume after signals
    }
};

}  // namespace

std::unique_ptr<ITimer> CreateTimerPosix() { return std::make_unique<TimerPosix>(); }

}  // namespace ran::platform
#endif
