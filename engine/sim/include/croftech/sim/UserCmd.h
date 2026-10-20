#pragma once

#include <array>
#include <cstdint>

namespace ct {

enum Buttons : std::uint16_t {
    kButtonJump = 1u << 0,
    kButtonAttack = 1u << 1,
    kButtonCrouch = 1u << 2,
};

// One tick of player intent: the only thing a client ever sends. View angles are quantized here
// so the server and client simulate from identical values.
struct UserCmd {
    std::uint16_t yaw = 0;
    std::int16_t pitch = 0;
    std::int8_t forward = 0; // -127..127
    std::int8_t side = 0;    // -127..127, positive is right
    std::uint16_t buttons = 0;
};
static_assert(sizeof(UserCmd) == 8, "UserCmd must not contain padding");

inline constexpr std::uint32_t kMaxPlayers = 16;

struct InputSet {
    std::array<UserCmd, kMaxPlayers> commands{};
};

} // namespace ct
