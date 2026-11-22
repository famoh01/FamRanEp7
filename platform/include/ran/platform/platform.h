// platform.h - platform abstraction interfaces (Phase 2).
// Original code for this project. Game logic must depend only on these
// interfaces, never on Win32, DirectX, or Android headers directly.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ran::platform {

// ---- Window / surface -------------------------------------------------------
class IWindow {
public:
    virtual ~IWindow() = default;
    virtual bool Create(int width, int height, const char* title) = 0;
    virtual void Destroy() = 0;
    // Called when the OS takes away the drawing surface (app backgrounded).
    virtual void SetSurfaceLostHandler(void (*handler)(void* user), void* user) = 0;
    virtual void GetSize(int& width, int& height) const = 0;
    virtual void* NativeHandle() const = 0;   // HWND on Windows, ANativeWindow* on Android
};

// ---- Input ------------------------------------------------------------------
enum class PointerAction { Down, Move, Up, Cancel };

struct PointerEvent {
    int id;                 // stable per finger / mouse button
    PointerAction action;
    float x, y;             // pixels, origin top-left
};

class IInput {
public:
    virtual ~IInput() = default;
    // Drain pending events once per frame. Touch and keyboard share one queue.
    virtual void Poll(std::vector<PointerEvent>& out) = 0;
    virtual bool IsKeyDown(int keyCode) const = 0;
};

// ---- Time -------------------------------------------------------------------
class ITimer {
public:
    virtual ~ITimer() = default;
    virtual uint64_t NowMicros() const = 0;   // monotonic
    virtual void SleepMicros(uint64_t us) = 0;
};

// ---- Files ------------------------------------------------------------------
class IFileSystem {
public:
    virtual ~IFileSystem() = default;
    // Read a file relative to the game's asset root (APK assets on Android).
    virtual bool ReadAsset(const std::string& relPath, std::vector<uint8_t>& out) = 0;
    // Writable location for saves and config (internal storage on Android).
    virtual std::string WritableRoot() const = 0;
};

// ---- Logging ----------------------------------------------------------------
enum class LogLevel { Debug, Info, Warn, Error };

class ILog {
public:
    virtual ~ILog() = default;
    virtual void Write(LogLevel level, const char* tag, const char* message) = 0;
};

// ---- Audio ------------------------------------------------------------------
class IAudio {
public:
    virtual ~IAudio() = default;
    virtual bool Open(int sampleRate, int channels) = 0;
    virtual void Close() = 0;
    virtual void Pause(bool paused) = 0;   // required on app pause/resume
};

// ---- Path helper (pure, shared by all backends) -----------------------------
// Converts '\' to '/', collapses repeated '/', drops "." segments, and resolves
// ".." segments. Returns false if the path would escape the root (leading ".."),
// so asset lookups cannot read outside the asset directory.
bool NormalizeRelativePath(const std::string& in, std::string& out);

}  // namespace ran::platform
