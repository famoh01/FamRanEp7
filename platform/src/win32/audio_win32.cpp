// audio_win32.cpp - waveOut playback backend (see win32_audio.h).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>

#include <cstring>
#include <vector>

#include "ran/platform/win32_audio.h"

namespace ran::platform {

namespace {
constexpr int kBufferCount = 4;
constexpr std::size_t kFramesPerBuffer = 4096;  // ~93 ms at 44.1 kHz
}  // namespace

struct WinAudio::Impl {
    struct Slot {
        WAVEHDR hdr{};
        std::vector<int16_t> data;
    };

    HWAVEOUT device = nullptr;
    HANDLE doneEvent = nullptr;
    int rate = 0;
    int channels = 0;
    bool paused = false;
    std::vector<Slot> slots;

    bool IsFree(const Slot& s) const { return (s.hdr.dwFlags & WHDR_INQUEUE) == 0; }
};

WinAudio::WinAudio() : impl_(std::make_unique<Impl>()) {}

WinAudio::~WinAudio() { Close(); }

bool WinAudio::Open(int sampleRate, int channels) {
    if (impl_->device) return false;  // already open
    if (sampleRate < 8000 || sampleRate > 192000) return false;
    if (channels < 1 || channels > 2) return false;

    WAVEFORMATEX fmt{};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = static_cast<WORD>(channels);
    fmt.nSamplesPerSec = static_cast<DWORD>(sampleRate);
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = static_cast<WORD>(channels * 2);
    fmt.nAvgBytesPerSec = static_cast<DWORD>(sampleRate * channels * 2);

    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event) return false;

    HWAVEOUT device = nullptr;
    if (waveOutOpen(&device, WAVE_MAPPER, &fmt, reinterpret_cast<DWORD_PTR>(event), 0,
                    CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        CloseHandle(event);
        return false;
    }

    impl_->device = device;
    impl_->doneEvent = event;
    impl_->rate = sampleRate;
    impl_->channels = channels;
    impl_->paused = false;
    impl_->slots.assign(kBufferCount, Impl::Slot{});
    const std::size_t bytes = kFramesPerBuffer * static_cast<std::size_t>(channels) * sizeof(int16_t);
    for (auto& s : impl_->slots) {
        s.data.assign(bytes / sizeof(int16_t), 0);
        std::memset(&s.hdr, 0, sizeof(s.hdr));
        s.hdr.lpData = reinterpret_cast<LPSTR>(s.data.data());
        s.hdr.dwBufferLength = static_cast<DWORD>(bytes);
        if (waveOutPrepareHeader(device, &s.hdr, sizeof(s.hdr)) != MMSYSERR_NOERROR) {
            Close();
            return false;
        }
    }
    return true;
}

void WinAudio::Close() {
    if (!impl_->device) return;
    waveOutReset(impl_->device);  // marks queued buffers done and wakes waiters
    for (auto& s : impl_->slots) {
        if (s.hdr.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(impl_->device, &s.hdr, sizeof(s.hdr));
    }
    waveOutClose(impl_->device);
    CloseHandle(impl_->doneEvent);
    impl_->device = nullptr;
    impl_->doneEvent = nullptr;
    impl_->slots.clear();
    impl_->rate = 0;
    impl_->channels = 0;
    impl_->paused = false;
}

void WinAudio::Pause(bool paused) {
    if (!impl_->device) return;  // pause before open is ignored, like NullAudio
    if (paused == impl_->paused) return;
    if (paused) {
        waveOutPause(impl_->device);
    } else {
        waveOutRestart(impl_->device);
    }
    impl_->paused = paused;
}

bool WinAudio::Submit(const int16_t* samples, std::size_t frames, unsigned timeoutMs) {
    if (!impl_->device || !samples || frames == 0) return false;
    const std::size_t ch = static_cast<std::size_t>(impl_->channels);
    std::size_t done = 0;
    while (done < frames) {
        Impl::Slot* slot = nullptr;
        for (auto& s : impl_->slots) {
            if (impl_->IsFree(s)) {
                slot = &s;
                break;
            }
        }
        if (!slot) {
            if (WaitForSingleObject(impl_->doneEvent, timeoutMs) != WAIT_OBJECT_0) return false;
            continue;
        }
        const std::size_t take = (frames - done) < kFramesPerBuffer ? (frames - done) : kFramesPerBuffer;
        const std::size_t valueCount = take * ch;
        std::memcpy(slot->data.data(), samples + done * ch, valueCount * sizeof(int16_t));
        // Pad the final short chunk with silence so every header is full length.
        std::memset(slot->data.data() + valueCount, 0,
                    (slot->data.size() - valueCount) * sizeof(int16_t));
        if (waveOutWrite(impl_->device, &slot->hdr, sizeof(slot->hdr)) != MMSYSERR_NOERROR) return false;
        done += take;
    }
    return true;
}

bool WinAudio::IsOpen() const { return impl_->device != nullptr; }
bool WinAudio::IsPaused() const { return impl_->paused; }
int WinAudio::SampleRate() const { return impl_->rate; }
int WinAudio::Channels() const { return impl_->channels; }

std::unique_ptr<IAudio> CreateAudioWin32() { return std::make_unique<WinAudio>(); }

}  // namespace ran::platform
