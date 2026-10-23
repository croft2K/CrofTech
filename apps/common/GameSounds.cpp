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
constexpr float kFireReach = 70.0f;
constexpr float kImpactVolume = 0.35f;
constexpr float kImpactReach = 25.0f;
constexpr float kOwnFireVolume = 0.7f;
constexpr float kPickupRespawnVolume = 0.6f;
constexpr float kAlertReach = 50.0f;

} // namespace

void GameSounds::setSettings(const GameSettings& settings) {
    m_weaponNames.clear();
    for (const WeaponSettings& weapon : settings.weapons) {
        m_weaponNames.push_back(weapon.name);
    }
    m_enemySoundSets.clear();
    for (const EnemySettings& enemy : settings.enemies) {
        m_enemySoundSets.emplace_back(enemy.soundSet());
    }
}

GameSounds::GameSounds(AudioOutput& output, const ContentPaths& content)
    : m_output(output), m_content(content) {}

GameSounds::~GameSounds() { stopAll(); }

std::optional<Mixer::SoundId> GameSounds::sound(std::string_view name, bool quiet) {
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
    if (!id && !quiet) {
        logWarning("{}: {}", relative, error);
    }
    m_sounds.emplace(std::string(name), id);
    return id;
}

std::string GameSounds::named(std::string_view name, const GameEvent& event) {
    if (event.enemyType < m_enemySoundSets.size()) {
        std::string own = std::format("{}_{}", m_enemySoundSets[event.enemyType], name);
        if (sound(own, true)) {
            return own;
        }
    }
    return std::string(name);
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
            play(named("footstep", event), event, player, kFootstepVolume);
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
            play(named("pain", event), event, player);
            break;
        case GameEventKind::Death:
            play(named("death", event), event, player);
            break;
        case GameEventKind::Respawn:
        case GameEventKind::EnemySpawn:
            play("respawn", event, player);
            break;
        case GameEventKind::CheckpointReached:
            play("checkpoint", event, player);
            break;
        case GameEventKind::Explosion:
            play("explosion", event, player, 1.0f, kExplosionReach);
            break;
        case GameEventKind::Fire:
            if (event.weapon < m_weaponNames.size()) {
                play(m_weaponNames[event.weapon] + "_fire", event, player,
                     event.entity == player ? kOwnFireVolume : 1.0f, kFireReach);
            }
            break;
        case GameEventKind::Impact:
            if (event.strength > 0.0f && event.entity == player) {
                // Your hit on someone: a tick in both ears, not a sound from over there.
                play("hit", event, player);
            } else if (event.strength == 0.0f) {
                GameEvent placed = event;
                placed.entity = {};
                play("impact", placed, player, kImpactVolume, kImpactReach);
            }
            break;
        case GameEventKind::Pickup:
            if (const GameLevel::Entity* pickup = level.entity(event.levelEntity)) {
                constexpr std::string_view kPickupSounds[] = {"pickup_health", "pickup_armour",
                                                              "pickup_ammo", "pickup_weapon"};
                play(kPickupSounds[static_cast<std::size_t>(pickup->item)], event, player);
            }
            break;
        case GameEventKind::PickupRespawn:
            play("pickup_respawn", event, player, kPickupRespawnVolume);
            break;
        case GameEventKind::EnemyAlert:
            play(named("alert", event), event, player, 1.0f, kAlertReach);
            break;
        case GameEventKind::EnemySwing:
            play(named("swing", event), event, player);
            break;
        case GameEventKind::WeaponSwitch:
            if (event.entity == player) {
                play("weapon_switch", event, player);
            }
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
