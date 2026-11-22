// backends.h - factory functions for the concrete platform backends.
// Each backend is compiled only on its own OS (see the #if in its .cpp).
#pragma once
#include <memory>
#include <string>

#include "ran/platform/platform.h"

namespace ran::platform {

class Win32Window;

// POSIX (Linux, Android): monotonic clock + nanosleep.
std::unique_ptr<ITimer> CreateTimerPosix();

// Windows: QueryPerformanceCounter + Sleep.
std::unique_ptr<ITimer> CreateTimerWin32();

// Windows: top-level window that owns its message queue.
std::unique_ptr<Win32Window> CreateWindowWin32();

// Windows: keyboard and mouse input read from the window's queue.
// The window must outlive the returned input object.
std::unique_ptr<IInput> CreateInputWin32(Win32Window& window);

// Portable (std::filesystem). assetRoot is read-only game data,
// writableRoot is for saves and config. Both are UTF-8 paths.
std::unique_ptr<IFileSystem> CreateFileSystem(const std::string& assetRoot,
                                              const std::string& writableRoot);

}  // namespace ran::platform
