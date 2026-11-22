#include "ran/platform/null_audio.h"

namespace ran::platform {

// Accepts only sample rates and channel counts the game can use. Pause before
// Open is a no-op, so an app pause during startup cannot crash.
bool NullAudio::Open(int sampleRate, int channels) {
    if (sampleRate < 8000 || sampleRate > 192000) return false;
    if (channels < 1 || channels > 2) return false;
    rate_ = sampleRate;
    channels_ = channels;
    open_ = true;
    paused_ = false;
    return true;
}

void NullAudio::Close() {
    open_ = false;
    paused_ = false;
    rate_ = 0;
    channels_ = 0;
}

void NullAudio::Pause(bool paused) {
    if (open_) paused_ = paused;
}

std::unique_ptr<IAudio> CreateAudioNull() { return std::make_unique<NullAudio>(); }

}  // namespace ran::platform
