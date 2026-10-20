#pragma once

#include "croftech/core/Math.h"
#include "croftech/sim/Components.h"
#include "croftech/sim/UserCmd.h"

namespace ct {

class CollisionWorld;

// Every number that shapes how movement feels. Defaults are classic Quake values converted to
// metres (1 Quake unit = 1 inch); the tuning file overrides them.
struct MovementSettings {
    float gravity = 20.32f;           // m/s^2
    float maxGroundSpeed = 8.128f;    // m/s
    float groundAcceleration = 10.0f; // fraction of wish speed gained per second
    float airAcceleration = 10.0f;    // as above, in the air
    float airWishSpeedCap =
        0.762f; // m/s; a low cap with high air acceleration allows strafe-jumping
    float friction = 6.0f;
    float stopSpeed = 2.54f;       // below this speed, friction acts as if moving at this speed
    float jumpSpeed = 6.858f;      // m/s upwards
    float stepHeight = 0.457f;     // tallest ledge walked up without jumping
    float minGroundNormalZ = 0.7f; // steeper surfaces (about 45 degrees) are slopes, not ground
    Vec3 halfExtents{0.4f, 0.4f, 0.9f}; // collision box: 0.8 x 0.8 x 1.8 m
    float eyeHeight = 1.6f;             // above the feet
    bool autoJump = false;              // holding jump re-jumps on landing
};

// Advances one player by one tick.
void movePlayer(const CollisionWorld& world, const MovementSettings& settings, const UserCmd& cmd,
                Transform& transform, PlayerMotion& motion);

Vec3 eyePosition(const MovementSettings& settings, const Transform& transform);

} // namespace ct
