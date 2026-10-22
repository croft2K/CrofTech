#pragma once

#include <array>
#include <cstdint>

namespace ct {

enum Buttons : std::uint16_t {
    kButtonJump = 1u << 0,
    kButtonAttack = 1u << 1,
    kButtonCrouch = 1u << 2,
};

inline constexpr std::uint32_t kMaxPlayers = 16;
// Weapons a game can have, one per number key.
inline constexpr std::uint32_t kMaxWeapons = 8;

// One tick of player intent: the only thing a client ever sends. View angles are quantized here
// so the server and client simulate from identical values.
struct UserCmd {
    std::uint16_t yaw = 0;
    std::int16_t pitch = 0;
    std::int8_t forward = 0; // -127..127
    std::int8_t side = 0;    // -127..127, positive is right
    std::uint16_t buttons = 0;
    // 1..kMaxWeapons asks to switch to that weapon; 0 keeps the one in hand. Asking again for the
    // weapon in hand does nothing, so a client may repeat the request until it is seen.
    std::uint8_t weapon = 0;
    // Reserved for M7's sub-tick inputs (when in the tick the buttons changed, and what the
    // player was seeing), so the protocol keeps its shape. Zero until then.
    std::uint8_t subtick = 0;
    std::uint16_t viewDelay = 0;
};
static_assert(sizeof(UserCmd) == 12, "UserCmd must not contain padding");

struct InputSet {
    std::array<UserCmd, kMaxPlayers> commands{};
};

} // namespace ct
