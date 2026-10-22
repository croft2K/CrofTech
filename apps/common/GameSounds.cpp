#include "GameSounds.h"

#include "croftech/audio/WavFile.h"
#include "croftech/core/ConfigFile.h"
#include "croftech/core/Log.h"

#include <algorithm>
#include <format>

namespace ct {
namespace {

constexpr float kFootstepVolume = 0.45f;
constexpr float kHumVolume = 0.5f;
constexpr float kExplosionReach = 90.0f;

} // namespace

GameSounds::GameSounds(AudioOutput& output, const ContentPaths& content)
    : m_output(output), m_content(content) {}

GameSounds::~GameSounds() { stopAll(); }

std::optional<Mixer::SoundId> GameSounds::sound(std::string_view name) {
    if (const auto found = m_sounds.find(name); found != m_sounds.end()) {
        return found->second;
    }
    std::optional<Mixer::SoundId> id;
    const std::string relative = std::format("sounds/{}.wav", name);
    std::string error = "not found in any content layer";
    if (const std::optional<std::filesystem::path> path = m_content.find(relative)) {
        if (const std::optional<std::string> bytes = readTextFile(*path, kMaxWavBytes)) {
            if (const std::optional<Sound> decoded =
                    parseWav(std::as_bytes(std::span{*bytes}), error)) {
                id = m_output.withMixer([&](Mixer& mixer) { return mixer.addSound(*decoded); });
            }
        } else {
            error = "can't be read";
        }
    }
    if (!id) {
        logWarning("{}: {}", relative, error);
    }
    m_sounds.emplace(std::string(name), id);
    return id;
}

void GameSounds::play(std::string_view name, const GameEvent& event, EntityId player, float volume,
                      float reach) {
    const std::optional<Mixer::SoundId> id = sound(name);
    if (!id) {
        return;
    }
    Mixer::Play params;
    params.volume = volume;
    params.reach = reach;
    if (event.entity != player) {
        params.position = event.position;
    }
    m_output.withMixer([&](Mixer& mixer) { return mixer.play(*id, params); });
}

void GameSounds::update(std::span<const GameEvent> events, EntityId player, const GameLevel& level,
                        const World& world, Vec3 listener, float listenerYaw, float volume) {
    for (const GameEvent& event : events) {
        switch (event.kind) {
        case GameEventKind::Footstep:
            play("footstep", event, player, kFootstepVolume);
            break;
        case GameEventKind::Jump:
            play("jump", event, player);
            break;
        case GameEventKind::Land:
            play("land", event, player,
                 std::clamp((event.strength - GameEvents::kMinLandingSpeed) / 10.0f + 0.4f, 0.4f,
                            1.0f));
            break;
        case GameEventKind::Hurt:
            play("pain", event, player);
            break;
        case GameEventKind::Death:
            play("death", event, player);
            break;
        case GameEventKind::Respawn:
            play("respawn", event, player);
            break;
        case GameEventKind::CheckpointReached:
            play("checkpoint", event, player);
            break;
        case GameEventKind::Explosion:
            play("explosion", event, player, 1.0f, kExplosionReach);
            break;
        case GameEventKind::MoverStart: {
            play("mover_start", event, player);
            const std::optional<Mixer::SoundId> hum = sound("mover_move");
            if (hum && std::none_of(m_hums.begin(), m_hums.end(), [&](const Hum& existing) {
                    return existing.levelEntity == event.levelEntity;
                })) {
                Mixer::Play params;
                params.position = event.position;
                params.loop = true;
                params.volume = kHumVolume;
                const Mixer::VoiceId voice =
                    m_output.withMixer([&](Mixer& mixer) { return mixer.play(*hum, params); });
                m_hums.push_back({event.levelEntity, voice});
            }
            break;
        }
        case GameEventKind::MoverStop:
            play("mover_stop", event, player);
            std::erase_if(m_hums, [&](const Hum& hum) {
                if (hum.levelEntity != event.levelEntity) {
                    return false;
                }
                m_output.withMixer([&](Mixer& mixer) { mixer.stop(hum.voice); });
                return true;
            });
            break;
        }
    }
    m_output.withMixer([&](Mixer& mixer) {
        for (const Hum& hum : m_hums) {
            mixer.setPosition(hum.voice, moverCentre(level, hum.levelEntity, world));
        }
        mixer.setListener(listener, listenerYaw);
        mixer.setVolume(volume);
    });
}

void GameSounds::stopAll() {
    m_output.withMixer([&](Mixer& mixer) {
        for (const Hum& hum : m_hums) {
            mixer.stop(hum.voice);
        }
    });
    m_hums.clear();
}

} // namespace ct
