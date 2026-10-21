#pragma once

#include "croftech/sim/Components.h"
#include "croftech/sim/UserCmd.h"

namespace ct {

// The player's mouse look, in radians, as the game and play-in-editor both turn it.
struct PlayerLook {
    float yaw = 0.0f;
    float pitch = 0.0f;

    // Turns by mouse counts; sensitivity as client.sensitivity (degrees = 0.022 x sensitivity per
    // count, as Quake's m_yaw, so sensitivities carry over). Pitch stops short of straight up and
    // down.
    void turn(float countsX, float countsY, float sensitivity);

    // Looking the way a player in the state faces, as after loading a save.
    static PlayerLook facing(const Transform& transform);
};

// The command for this frame: the look angles, plus WASD and Space from the keyboard state
// (SDL_GetKeyboardState) and fire from the left mouse button, when keys is not null.
UserCmd makeUserCmd(const PlayerLook& look, const bool* keys);

} // namespace ct
