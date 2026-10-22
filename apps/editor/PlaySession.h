#pragma once

#include "GameHud.h"
#include "PlayerInput.h"

#include "croftech/client/GameEvents.h"
#include "croftech/client/LocalGame.h"
#include "croftech/client/StepSmoothing.h"
#include "croftech/core/Camera.h"
#include "croftech/level/Level.h"

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ct {

// Play-in-editor: the level being edited, run as a game in the view without saving it. It is the
// game itself, a local server and client over an in-memory link (LocalGame), slowed down and made
// lossy by the network conditions chosen in the Play menu. The level is copied when play starts,
// so editing waits until play stops.
class PlaySession {
public:
    using Clock = std::chrono::steady_clock;

    // Starts at the level's player spawn, or with from, where that camera is (play from here).
    // Null, with error saying why, if the level can't be played: without from, it needs a player
    // spawn.
    static std::unique_ptr<PlaySession> start(const Level& level, const GameSettings& settings,
                                              const LinkConditions& conditions,
                                              const std::optional<Camera>& from,
                                              std::string& error);

    // Without from, the level must have a player spawn.
    PlaySession(const Level& level, const GameSettings& settings, const LinkConditions& conditions,
                const std::optional<Camera>& from);

    void look(float countsX, float countsY);
    // Once a frame. keys is the keyboard state, or null while the mouse is not captured.
    void update(const bool* keys, Clock::time_point now);

    // Where the player looks from, as of the last update.
    Camera camera() const;
    // Horizontal speed, metres per second.
    float speed() const { return m_speed; }
    const GameHud& hud() const { return m_hud; }
    // What happened in the game since the last update, for sounds and effects.
    const std::vector<GameEvent>& events() const { return m_events; }
    const LocalGame& game() const { return m_game; }

    // Saves made during play are kept until it stops: quick saves, and checkpoints reached.
    void quickSave(Clock::time_point now);
    // Loads the newest of them.
    void quickLoad(Clock::time_point now);
    // What to tell the player ("Saved", "Checkpoint"), for a moment after it happens; null
    // otherwise.
    const std::string* notice(Clock::time_point now) const;

private:
    void tell(std::string text, Clock::time_point now);

    GameSettings m_settings;
    LocalGame m_game;
    PlayerLook m_look;
    GameHud m_hud;
    GameEvents m_gameEvents;
    std::vector<GameEvent> m_events;
    StepSmoothing m_stepSmoothing;
    std::uint32_t m_timeline = 0;
    std::vector<std::byte> m_lastSave;
    std::string m_notice;
    Clock::time_point m_noticeUntil;
    Vec3 m_eye;
    float m_speed = 0.0f;
};

} // namespace ct
