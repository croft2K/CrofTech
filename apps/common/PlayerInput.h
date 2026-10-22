#pragma once

#include "croftech/sim/Components.h"
#include "croftech/sim/GameSettings.h"
#include "croftech/sim/UserCmd.h"
#include "croftech/sim/World.h"

#include <chrono>
#include <cstdint>

union SDL_Event;

namespace ct {

// The player's mouse look, in radians, as the game and play-in-editor both turn it.
struct PlayerLook {
    float yaw = 0.0f;
    float pitch = 0.0f;

    // Turns by mouse counts; sensitivity as client.sensitivity (degrees = 0.022 x sensitivity per
    // count, as Quake's m_yaw, so sensitivities carry over). Pitch stops short of straight up and
    // down. Inverted (client.invert_mouse), moving the mouse forward looks down.
    void turn(float countsX, float countsY, float sensitivity, bool invert = false);

    // Looking the way a player in the state faces, as after loading a save.
    static PlayerLook facing(const Transform& transform);
};

// The command for this frame: the look angles, plus WASD and Space from the keyboard state
// (SDL_GetKeyboardState) and fire from the left mouse button, when keys is not null.
UserCmd makeUserCmd(const PlayerLook& look, const bool* keys);

// Weapon choices from the number keys and the mouse wheel. A choice goes out in every command
// for a moment, since a lost packet would otherwise lose it; asking again for the weapon in hand
// does nothing.
class WeaponSelect {
public:
    using Clock = std::chrono::steady_clock;

    // Number keys choose a weapon; the wheel changes to the next or previous weapon carried that
    // has ammo, counting from the one in hand in the latest state (or the one just chosen).
    // Returns whether the event was one of these.
    bool handleEvent(const SDL_Event& event, const State* latest, EntityId player,
                     const GameSettings& settings, Clock::time_point now);

    // Adds the choice to the command while it is being sent.
    void apply(UserCmd& cmd, Clock::time_point now) const;

private:
    void choose(std::uint32_t weapon, Clock::time_point now);

    std::uint32_t m_weapon = 0;
    Clock::time_point m_until;
};

} // namespace ct
