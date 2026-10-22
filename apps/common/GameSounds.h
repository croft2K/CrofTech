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
// layers (so a game can replace any of them), and a hum while a mover moves. The player's own
// sounds play unplaced, in both ears; everyone else's come from where they happened.
class GameSounds {
public:
    GameSounds(AudioOutput& output, const ContentPaths& content);
    ~GameSounds();
    GameSounds(const GameSounds&) = delete;
    GameSounds& operator=(const GameSounds&) = delete;

    // Once a frame, with the events since the last and where the ears are.
    void update(std::span<const GameEvent> events, EntityId player, const GameLevel& level,
                const World& world, Vec3 listener, float listenerYaw, float volume);
    // Stops everything playing, as when a level ends.
    void stopAll();

private:
    // Loaded the first time it is asked for; a sound that is missing or broken is reported once
    // and stays silent.
    std::optional<Mixer::SoundId> sound(std::string_view name);
    void play(std::string_view name, const GameEvent& event, EntityId player, float volume = 1.0f);

    AudioOutput& m_output;
    const ContentPaths& m_content;
    std::map<std::string, std::optional<Mixer::SoundId>, std::less<>> m_sounds;
    struct Hum {
        std::uint32_t levelEntity = 0;
        Mixer::VoiceId voice = Mixer::kNoVoice;
    };
    std::vector<Hum> m_hums; // one per moving mover
};

} // namespace ct
