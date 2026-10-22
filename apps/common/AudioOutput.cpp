#include "AudioOutput.h"

#include "croftech/core/Log.h"

#include <SDL3/SDL.h>

namespace ct {

AudioOutput::AudioOutput() {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        logWarning("no sound: {}", SDL_GetError());
        return;
    }
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, static_cast<int>(kSampleRate)};
    m_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &feed, this);
    if (m_stream == nullptr) {
        logWarning("no sound: {}", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return;
    }
    SDL_ResumeAudioStreamDevice(m_stream);
}

AudioOutput::~AudioOutput() {
    if (m_stream != nullptr) {
        // Stops the audio thread before the mixer it reads goes.
        SDL_DestroyAudioStream(m_stream);
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
}

void AudioOutput::feed(void* self, SDL_AudioStream* stream, int additionalBytes, int) {
    auto& output = *static_cast<AudioOutput*>(self);
    if (additionalBytes <= 0) {
        return;
    }
    const auto frames = static_cast<std::size_t>(additionalBytes) / (2 * sizeof(float));
    output.m_buffer.resize(frames * 2);
    {
        std::lock_guard lock(output.m_mutex);
        output.m_mixer.mix(output.m_buffer);
    }
    SDL_PutAudioStreamData(stream, output.m_buffer.data(),
                           static_cast<int>(output.m_buffer.size() * sizeof(float)));
}

} // namespace ct
