#pragma once

#include "AudioOutput.h"

#include "croftech/client/GameEvents.h"
#include "croftech/core/Content.h"

#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ct {

// The game's sounds: one for each GameEvent, read from sounds/<name>.wav through the content
// layers (so a game can replace any of them), and a hum while a mover moves. Each weapon fires
// with sounds/<weapon>_fire.wav. An enemy's footsteps, pain, death, alert and swing are
// sounds/<kind>_<name>.wav where there is one (charger_alert.wav), else the plain one. The
// player's own sounds play unplaced, in both ears; everyone else's come from where they happened.
class GameSounds {
public:
    GameSounds(AudioOutput& output, const ContentPaths& content);
    ~GameSounds();
    GameSounds(const GameSounds&) = delete;
    GameSounds& operator=(const GameSounds&) = delete;

    // The game's weapon names, in the order of GameSettings::weapons, for their firing sounds.
    void setWeaponNames(std::vector<std::string> names) { m_weaponNames = std::move(names); }
    // The game's enemy names, in the order of GameSettings::enemies, for their own sounds.
    void setEnemyNames(std::vector<std::string> names) { m_enemyNames = std::move(names); }

    // Once a frame, with the events since the last and where the ears are.
    void update(std::span<const GameEvent> events, EntityId player, const GameLevel& level,
                const World& world, Vec3 listener, float listenerYaw, float volume);
    // Stops everything playing, as when a level ends.
    void stopAll();

private:
    // Loaded the first time it is asked for; a sound that is missing or broken is reported once
    // (unless quiet) and stays silent.
    std::optional<Mixer::SoundId> sound(std::string_view name, bool quiet = false);
    // The enemy's own version of the sound, if the event is an enemy's and there is one.
    std::string named(std::string_view name, const GameEvent& event);
    void play(std::string_view name, const GameEvent& event, EntityId player, float volume = 1.0f,
              float reach = Mixer::Play{}.reach);

    AudioOutput& m_output;
    std::vector<std::string> m_weaponNames;
    std::vector<std::string> m_enemyNames;
    const ContentPaths& m_content;
    std::map<std::string, std::optional<Mixer::SoundId>, std::less<>> m_sounds;
    struct Hum {
        std::uint32_t levelEntity = 0;
        Mixer::VoiceId voice = Mixer::kNoVoice;
    };
    std::vector<Hum> m_hums; // one per moving mover
};

} // namespace ct
