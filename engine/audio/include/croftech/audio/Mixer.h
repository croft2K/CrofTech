#pragma once

#include "croftech/audio/Sound.h"
#include "croftech/core/Math.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ct {

// A software mixer: plays sounds as voices, placed in the world or not, and mixes them into
// stereo for an output device. Presentation only, never part of the simulation. Not thread-safe:
// an output that mixes on its own thread locks around every call.
class Mixer {
public:
    using SoundId = std::uint32_t;
    using VoiceId = std::uint32_t;

    static constexpr std::size_t kMaxVoices = 64;
    static constexpr VoiceId kNoVoice = 0;

    struct Play {
        // Where in the world it sounds from. Without one it plays as recorded, in both ears: the
        // player's own sounds, the interface, music.
        std::optional<Vec3> position;
        float volume = 1.0f;
        bool loop = false;
        float reach = 40.0f; // metres at which a placed sound has faded to nothing
    };

    explicit Mixer(std::uint32_t outputRate);

    std::uint32_t outputRate() const { return m_outputRate; }

    // Converted to the output rate once, here.
    SoundId addSound(const Sound& sound);

    // kNoVoice if the sound is unknown, or every voice is busy with a looping sound; otherwise
    // the oldest sound that will end by itself makes way.
    VoiceId play(SoundId sound, const Play& play);
    void stop(VoiceId voice);
    void setPosition(VoiceId voice, Vec3 position);
    bool isPlaying(VoiceId voice) const;
    std::size_t playingCount() const;

    // Where the ears are, and which way they face (yaw 0 is +X, increasing towards +Y).
    void setListener(Vec3 position, float yawRadians);
    // Of everything, 0 to 1.
    void setVolume(float volume);

    // Mixes the next frames into out, interleaved left and right, from -1 to 1. Voices that reach
    // the end of a sound that doesn't loop stop.
    void mix(std::span<float> out);

private:
    struct Voice {
        bool active = false;
        std::uint32_t generation = 0;
        SoundId sound = 0;
        Play play;
        std::size_t frame = 0;
        std::uint64_t started = 0; // play order, for choosing which to stop
        float left = 0.0f;         // gains last mixed with, ramped from to the next
        float right = 0.0f;
        bool fresh = true; // not mixed yet, so it starts at its gains without a ramp
    };

    Voice* find(VoiceId voice);
    const Voice* find(VoiceId voice) const;
    // The gains for each ear now, from the volume, distance and direction.
    std::pair<float, float> gains(const Voice& voice) const;

    std::uint32_t m_outputRate;
    std::vector<Sound> m_sounds;
    std::array<Voice, kMaxVoices> m_voices{};
    std::uint64_t m_plays = 0;
    Vec3 m_listener;
    float m_listenerYaw = 0.0f;
    float m_volume = 1.0f;
};

} // namespace ct
