#pragma once

#include "croftech/core/Math.h"

#include <cstdint>

namespace ct {

// Components are hashed as raw bytes for the determinism check, so they must have no padding:
// padding bytes are indeterminate and would make identical states hash differently. Each one
// carries a size check; use fixed-width integers instead of bool.

struct Transform {
    Vec3 position; // centre of the collision box
    std::uint16_t yaw = 0;
    std::int16_t pitch = 0;
};
static_assert(sizeof(Transform) == 16, "Transform must not contain padding");

enum PlayerMotionFlags : std::uint32_t {
    kPlayerOnGround = 1u << 0,
    kPlayerJumpHeld = 1u << 1,
};

struct PlayerMotion {
    Vec3 velocity;
    std::uint32_t flags = 0;
};
static_assert(sizeof(PlayerMotion) == 16, "PlayerMotion must not contain padding");

// Marks an entity as driven by the input command in this slot of the InputSet.
struct PlayerControl {
    std::uint32_t inputSlot = 0;
};
static_assert(sizeof(PlayerControl) == 4, "PlayerControl must not contain padding");

} // namespace ct
