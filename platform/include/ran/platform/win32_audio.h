// win32_audio.h - IAudio backend for Windows using waveOut (winmm).
// Plays interleaved 16-bit PCM through the default output device.
// The Win32 headers stay in win32_audio.cpp; this header is portable.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

#include "ran/platform/platform.h"

namespace ran::platform {

class WinAudio final : public IAudio {
public:
    WinAudio();
    ~WinAudio() override;
    WinAudio(const WinAudio&) = delete;
    WinAudio& operator=(const WinAudio&) = delete;

    // Opens the default device for 16-bit PCM at the given rate (8 kHz..192 kHz)
    // and 1 or 2 channels. Returns false if the format is rejected or no device opens.
    bool Open(int sampleRate, int channels) override;
    void Close() override;
    void Pause(bool paused) override;   // waveOutPause / waveOutRestart

    // Queues `frames` sample frames (frames * channels int16 values). Blocks while
    // all internal buffers are busy, up to timeoutMs. Returns false on timeout,
    // when not open, or on a device error. A short final chunk is padded with silence.
    bool Submit(const int16_t* samples, std::size_t frames, unsigned timeoutMs = 5000);

    bool IsOpen() const;
    bool IsPaused() const;
    int SampleRate() const;
    int Channels() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::unique_ptr<IAudio> CreateAudioWin32();

}  // namespace ran::platform
