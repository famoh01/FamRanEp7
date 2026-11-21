// backends.h - factory functions for the concrete platform backends.
// Each backend is compiled only on its own OS (see the #if in its .cpp).
#pragma once
#include <memory>

#include "ran/platform/platform.h"

namespace ran::platform {

// POSIX (Linux, Android): monotonic clock + nanosleep.
std::unique_ptr<ITimer> CreateTimerPosix();

// Windows: QueryPerformanceCounter + Sleep.
std::unique_ptr<ITimer> CreateTimerWin32();

// Windows: a plain top-level window. Input and rendering are separate phases.
std::unique_ptr<IWindow> CreateWindowWin32();

}  // namespace ran::platform
