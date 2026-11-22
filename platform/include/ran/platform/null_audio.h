// null_audio.h - audio backend that accepts the IAudio calls and produces no
// sound. Used on headless test runs and as the fallback when no device opens.
// Portable: no OS headers.
#pragma once
#include <memory>

#include "ran/platform/platform.h"

namespace ran::platform {

class NullAudio final : public IAudio {
public:
    bool Open(int sampleRate, int channels) override;
    void Close() override;
    void Pause(bool paused) override;

    bool IsOpen() const { return open_; }
    bool IsPaused() const { return paused_; }
    int SampleRate() const { return rate_; }
    int Channels() const { return channels_; }

private:
    bool open_ = false;
    bool paused_ = false;
    int rate_ = 0;
    int channels_ = 0;
};

std::unique_ptr<IAudio> CreateAudioNull();

}  // namespace ran::platform
