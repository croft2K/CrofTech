#include "PlayerInput.h"

#include "croftech/core/Angle.h"
#include "croftech/core/Math.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

namespace ct {
namespace {

constexpr float kDegreesPerMouseCount = 0.022f;
// Long enough to outlast a few lost packets, short enough not to fight a later choice.
constexpr auto kChoiceSendTime = std::chrono::milliseconds(250);

} // namespace

void PlayerLook::turn(float countsX, float countsY, float sensitivity, bool invert) {
    if (invert) {
        countsY = -countsY;
    }
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

bool WeaponSelect::handleEvent(const SDL_Event& event, const State* latest, EntityId player,
                               const GameSettings& settings, Clock::time_point now) {
    const auto count =
        static_cast<std::uint32_t>(std::min<std::size_t>(settings.weapons.size(), kMaxWeapons));
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
        event.key.scancode >= SDL_SCANCODE_1 && event.key.scancode <= SDL_SCANCODE_9) {
        const auto weapon = static_cast<std::uint32_t>(event.key.scancode - SDL_SCANCODE_1);
        if (weapon < count) {
            choose(weapon, now);
        }
        return true;
    }
    if (event.type != SDL_EVENT_MOUSE_WHEEL || event.wheel.y == 0.0f || latest == nullptr) {
        return false;
    }
    const Weapons* weapons = latest->world.weapons.get(player);
    if (weapons == nullptr || count == 0) {
        return true;
    }
    const std::uint32_t from = now < m_until ? m_weapon : weapons->current;
    const std::uint32_t step = event.wheel.y > 0.0f ? 1u : count - 1u;
    for (std::uint32_t weapon = (from + step) % count; weapon != from % count;
         weapon = (weapon + step) % count) {
        const WeaponSettings& setting = settings.weapons[weapon];
        if (carries(*weapons, weapon) &&
            (setting.ammoPerShot <= 0 || weapons->ammo[weapon] >= setting.ammoPerShot)) {
            choose(weapon, now);
            break;
        }
    }
    return true;
}

void WeaponSelect::apply(UserCmd& cmd, Clock::time_point now) const {
    if (now < m_until) {
        cmd.weapon = static_cast<std::uint8_t>(m_weapon + 1);
    }
}

void WeaponSelect::choose(std::uint32_t weapon, Clock::time_point now) {
    m_weapon = weapon;
    m_until = now + kChoiceSendTime;
}

} // namespace ct
