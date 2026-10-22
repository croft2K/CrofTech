#pragma once

#include "croftech/core/Math.h"
#include "croftech/core/Schema.h"
#include "croftech/sim/UserCmd.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace ct {

// Every component is a plain struct with a schema listing each field once (CT_FIELD). The schema
// drives saves, the inspector and, later, replication, so a field missing from it would silently
// not be saved or sent: describesEveryByte() turns that into a compile error. It also proves the
// struct has no padding, which matters because states are hashed as raw bytes and padding bytes
// are indeterminate. Use fixed-width integers instead of bool.

struct Transform {
    Vec3 position; // centre of the collision box
    std::uint16_t yaw = 0;
    std::int16_t pitch = 0;
};

template <>
struct Schema<Transform> {
    static constexpr FieldInfo kFields[] = {
        CT_FIELD(Transform, position)
            .withHint(FieldHint::Distance)
            .withDescription("Centre of the collision box"),
        CT_FIELD(Transform, yaw).withHint(FieldHint::Angle).withDescription("Facing; 0 is +X"),
        CT_FIELD(Transform, pitch)
            .withHint(FieldHint::Angle)
            .withDescription("Look up (positive) or down (negative)"),
    };
    static constexpr TypeSchema value{"transform", sizeof(Transform), kFields};
};
static_assert(describesEveryByte(Schema<Transform>::value));

enum PlayerMotionFlags : std::uint32_t {
    kPlayerOnGround = 1u << 0,
    kPlayerJumpHeld = 1u << 1,
};

struct PlayerMotion {
    Vec3 velocity;
    std::uint32_t flags = 0;
};

template <>
struct Schema<PlayerMotion> {
    static constexpr std::array<std::string_view, 2> kFlagNames{"on_ground", "jump_held"};
    static constexpr FieldInfo kFields[] = {
        CT_FIELD(PlayerMotion, velocity).withHint(FieldHint::Speed),
        CT_FIELD(PlayerMotion, flags).withFlagNames(kFlagNames),
    };
    static constexpr TypeSchema value{"player_motion", sizeof(PlayerMotion), kFields};
};
static_assert(describesEveryByte(Schema<PlayerMotion>::value));

// Marks an entity as driven by the input command in this slot of the InputSet.
struct PlayerControl {
    std::uint32_t inputSlot = 0;
    std::uint32_t buttons = 0; // held last tick, to tell a press from a hold
};

template <>
struct Schema<PlayerControl> {
    static constexpr std::array<std::string_view, 3> kButtonNames{"jump", "attack", "crouch"};
    static constexpr FieldInfo kFields[] = {
        CT_FIELD(PlayerControl, inputSlot)
            .withDescription("Which player's input drives this")
            .withRange(0, kMaxPlayers - 1),
        CT_FIELD(PlayerControl, buttons)
            .withFlagNames(kButtonNames)
            .withDescription("Buttons held last tick"),
    };
    static constexpr TypeSchema value{"player_control", sizeof(PlayerControl), kFields};
};
static_assert(describesEveryByte(Schema<PlayerControl>::value));

} // namespace ct
