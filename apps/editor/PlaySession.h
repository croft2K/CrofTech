#pragma once

#include "PlayerInput.h"

#include "croftech/client/LocalGame.h"
#include "croftech/core/Camera.h"
#include "croftech/level/Level.h"

#include <chrono>
#include <memory>
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

    // Null, with error saying why, if the level can't be played: it needs a player spawn.
    static std::unique_ptr<PlaySession> start(const Level& level, const MovementSettings& movement,
                                              const LinkConditions& conditions, std::string& error);

    PlaySession(std::span<const Brush> brushes, std::vector<SpawnPoint> spawns,
                const MovementSettings& movement, const LinkConditions& conditions);

    void look(float countsX, float countsY);
    // Once a frame. keys is the keyboard state, or null while the mouse is not captured.
    void update(const bool* keys, Clock::time_point now);

    // Where the player looks from, as of the last update.
    Camera camera() const;
    // Horizontal speed, metres per second.
    float speed() const { return m_speed; }
    const LocalGame& game() const { return m_game; }

private:
    LocalGame m_game;
    PlayerLook m_look;
    Vec3 m_eye;
    float m_speed = 0.0f;
};

} // namespace ct
