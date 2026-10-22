#pragma once

#include "croftech/audio/Mixer.h"

#include <mutex>
#include <vector>

struct SDL_AudioStream;

namespace ct {

// The sound card: a Mixer that SDL's audio thread pulls from. Every use of the mixer goes
// through withMixer, which holds a lock the audio thread also takes, briefly. If no device
// opens, the game carries on silent.
class AudioOutput {
public:
    static constexpr std::uint32_t kSampleRate = 48000;

    AudioOutput();
    ~AudioOutput();
    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    bool isOpen() const { return m_stream != nullptr; }

    template <typename Fn>
    auto withMixer(Fn&& fn) {
        std::lock_guard lock(m_mutex);
        return fn(m_mixer);
    }

private:
    static void feed(void* self, SDL_AudioStream* stream, int additionalBytes, int totalBytes);

    std::mutex m_mutex;
    Mixer m_mixer{kSampleRate};
    std::vector<float> m_buffer; // used only by the audio thread
    SDL_AudioStream* m_stream = nullptr;
};

} // namespace ct
