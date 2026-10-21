#include "PlayerInput.h"

#include "croftech/core/Angle.h"
#include "croftech/core/Math.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

namespace ct {
namespace {

constexpr float kDegreesPerMouseCount = 0.022f;

} // namespace

void PlayerLook::turn(float countsX, float countsY, float sensitivity) {
    const float radiansPerCount = kDegreesPerMouseCount * sensitivity * (kPi / 180.0f);
    const float maxPitch = angleToRadians(kMaxPitch);
    yaw = std::remainder(yaw - countsX * radiansPerCount, 2.0f * kPi);
    pitch = std::clamp(pitch - countsY * radiansPerCount, -maxPitch, maxPitch);
}

PlayerLook PlayerLook::facing(const Transform& transform) {
    return {angleToRadians(transform.yaw), angleToRadians(transform.pitch)};
}

UserCmd makeUserCmd(const PlayerLook& look, const bool* keys) {
    UserCmd cmd;
    cmd.yaw = static_cast<std::uint16_t>(radiansToAngle(look.yaw));
    cmd.pitch = static_cast<std::int16_t>(radiansToAngle(look.pitch));
    if (keys != nullptr) {
        const auto axis = [keys](SDL_Scancode positive, SDL_Scancode negative) {
            return static_cast<std::int8_t>((keys[positive] ? 127 : 0) -
                                            (keys[negative] ? 127 : 0));
        };
        cmd.forward = axis(SDL_SCANCODE_W, SDL_SCANCODE_S);
        cmd.side = axis(SDL_SCANCODE_D, SDL_SCANCODE_A);
        const bool fire = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK) != 0;
        cmd.buttons = static_cast<std::uint16_t>((keys[SDL_SCANCODE_SPACE] ? kButtonJump : 0u) |
                                                 (fire ? kButtonAttack : 0u));
    }
    return cmd;
}

} // namespace ct
