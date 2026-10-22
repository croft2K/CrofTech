#include "croftech/audio/Mixer.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ct {
namespace {

// Placed sounds this close are heard from straight ahead, so one passing through the listener
// doesn't flip from ear to ear.
constexpr float kCentreDistance = 1.0f;

Sound resampled(const Sound& sound, std::uint32_t rate) {
    if (sound.sampleRate == rate || sound.sampleRate == 0 || sound.frameCount() == 0) {
        Sound copy = sound;
        copy.sampleRate = rate;
        return copy;
    }
    const std::size_t from = sound.frameCount();
    const double step = static_cast<double>(sound.sampleRate) / static_cast<double>(rate);
    const auto frames = static_cast<std::size_t>(static_cast<double>(from) / step);
    Sound out;
    out.sampleRate = rate;
    out.channels = sound.channels;
    out.samples.resize(frames * sound.channels);
    for (std::size_t i = 0; i < frames; ++i) {
        const double position = static_cast<double>(i) * step;
        const auto a = std::min(static_cast<std::size_t>(position), from - 1);
        const std::size_t b = std::min(a + 1, from - 1);
        const auto t = static_cast<float>(position - static_cast<double>(a));
        for (std::uint32_t c = 0; c < sound.channels; ++c) {
            const float x = sound.samples[a * sound.channels + c];
            const float y = sound.samples[b * sound.channels + c];
            out.samples[i * sound.channels + c] = x + (y - x) * t;
        }
    }
    return out;
}

} // namespace

Mixer::Mixer(std::uint32_t outputRate) : m_outputRate(outputRate) {}

Mixer::SoundId Mixer::addSound(const Sound& sound) {
    m_sounds.push_back(resampled(sound, m_outputRate));
    return static_cast<SoundId>(m_sounds.size() - 1);
}

Mixer::VoiceId Mixer::play(SoundId sound, const Play& play) {
    if (sound >= m_sounds.size() || m_sounds[sound].frameCount() == 0) {
        return kNoVoice;
    }
    Voice* chosen = nullptr;
    for (Voice& voice : m_voices) {
        if (!voice.active) {
            chosen = &voice;
            break;
        }
    }
    if (chosen == nullptr) {
        for (Voice& voice : m_voices) {
            if (!voice.play.loop && (chosen == nullptr || voice.started < chosen->started)) {
                chosen = &voice;
            }
        }
    }
    if (chosen == nullptr) {
        return kNoVoice;
    }
    const std::uint32_t generation = chosen->generation + 1;
    *chosen = {};
    chosen->active = true;
    chosen->generation = generation;
    chosen->sound = sound;
    chosen->play = play;
    chosen->started = ++m_plays;
    const auto slot = static_cast<std::uint32_t>(chosen - m_voices.data());
    // Ids never repeat until the generation wraps, so a stale one can't stop a new sound.
    return generation * static_cast<std::uint32_t>(kMaxVoices) + slot + 1;
}

Mixer::Voice* Mixer::find(VoiceId voice) {
    return const_cast<Voice*>(std::as_const(*this).find(voice));
}

const Mixer::Voice* Mixer::find(VoiceId voice) const {
    if (voice == kNoVoice) {
        return nullptr;
    }
    const std::uint32_t slot = (voice - 1) % kMaxVoices;
    const std::uint32_t generation = (voice - 1) / kMaxVoices;
    const Voice& found = m_voices[slot];
    return found.active && found.generation == generation ? &found : nullptr;
}

void Mixer::stop(VoiceId voice) {
    if (Voice* found = find(voice)) {
        found->active = false;
    }
}

void Mixer::setPosition(VoiceId voice, Vec3 position) {
    if (Voice* found = find(voice); found != nullptr && found->play.position) {
        found->play.position = position;
    }
}

bool Mixer::isPlaying(VoiceId voice) const { return find(voice) != nullptr; }

std::size_t Mixer::playingCount() const {
    return static_cast<std::size_t>(std::count_if(m_voices.begin(), m_voices.end(),
                                                  [](const Voice& voice) { return voice.active; }));
}

void Mixer::setListener(Vec3 position, float yawRadians) {
    m_listener = position;
    m_listenerYaw = yawRadians;
}

void Mixer::setVolume(float volume) { m_volume = std::clamp(volume, 0.0f, 1.0f); }

std::pair<float, float> Mixer::gains(const Voice& voice) const {
    const float volume = std::max(voice.play.volume, 0.0f) * m_volume;
    if (!voice.play.position) {
        return {volume, volume};
    }
    const Vec3 offset = *voice.play.position - m_listener;
    const float distance = length(offset);
    const float reach = std::max(voice.play.reach, 0.01f);
    const float fade = 1.0f - std::clamp(distance / reach, 0.0f, 1.0f);
    const float level = volume * fade * fade;
    // Constant power across the ears: centred, each gets cos 45 degrees.
    const float flat = std::sqrt(offset.x * offset.x + offset.y * offset.y);
    float pan = 0.0f;
    if (flat > 0.0f) {
        const Vec3 right{std::sin(m_listenerYaw), -std::cos(m_listenerYaw), 0.0f};
        pan = (offset.x * right.x + offset.y * right.y) / flat *
              std::min(flat / kCentreDistance, 1.0f);
    }
    const float angle = (pan + 1.0f) * (kPi / 4.0f);
    return {level * std::cos(angle), level * std::sin(angle)};
}

void Mixer::mix(std::span<float> out) {
    std::fill(out.begin(), out.end(), 0.0f);
    const std::size_t frames = out.size() / 2;
    if (frames == 0) {
        return;
    }
    for (Voice& voice : m_voices) {
        if (!voice.active) {
            continue;
        }
        const Sound& sound = m_sounds[voice.sound];
        const std::size_t length = sound.frameCount();
        const auto [left, right] = gains(voice);
        const float fromLeft = voice.fresh ? left : voice.left;
        const float fromRight = voice.fresh ? right : voice.right;
        const bool placed = voice.play.position.has_value();
        // Gains ramp across the buffer, so moving sounds and volume changes don't click.
        const float rampStep = 1.0f / static_cast<float>(frames);
        for (std::size_t i = 0; i < frames; ++i) {
            if (voice.frame >= length) {
                if (!voice.play.loop) {
                    voice.active = false;
                    break;
                }
                voice.frame = 0;
            }
            const float* sample = &sound.samples[voice.frame * sound.channels];
            float l = sample[0];
            float r = sound.channels == 2 ? sample[1] : sample[0];
            if (placed && sound.channels == 2) {
                l = r = (l + r) * 0.5f; // a placed sound is one point
            }
            const float t = static_cast<float>(i) * rampStep;
            out[i * 2] += l * (fromLeft + (left - fromLeft) * t);
            out[i * 2 + 1] += r * (fromRight + (right - fromRight) * t);
            ++voice.frame;
        }
        voice.left = left;
        voice.right = right;
        voice.fresh = false;
    }
    for (float& sample : out) {
        sample = std::clamp(sample, -1.0f, 1.0f);
    }
}

} // namespace ct
