#pragma once

#include "croftech/core/Math.h"
#include "croftech/core/Schema.h"
#include "croftech/sim/EntityId.h"
#include "croftech/sim/UserCmd.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace ct {

enum ProjectileFlags : std::uint32_t {
    kProjectileExploded = 1u << 0,
    kProjectileResting = 1u << 1, // a grenade that has stopped bouncing
};

// A rocket or grenade in flight: a simulated entity with a Transform, moved and set off by
// runProjectiles (sim/Firing.h) following its weapon's settings. Once exploded it stays a moment,
// still, so every client sees where it went off.
struct Projectile {
    Vec3 velocity;
    EntityId owner;             // who fired it: never hit directly, and credited with its damage
    std::uint32_t weapon = 0;   // GameSettings::weapons index it was fired from
    std::uint32_t fuseTick = 0; // explodes then, if nothing set it off sooner
    std::uint32_t explodedTick = 0; // once exploded
    std::uint32_t flags = 0;
};

template <>
struct Schema<Projectile> {
    static constexpr std::array<std::string_view, 2> kFlagNames{"exploded", "resting"};
    static constexpr FieldInfo kFields[] = {
        CT_FIELD(Projectile, velocity).withHint(FieldHint::Speed),
        CT_FIELD(Projectile, owner).withDescription("Who fired it"),
        CT_FIELD(Projectile, weapon)
            .withRange(0, kMaxWeapons - 1)
            .withDescription("Weapon it was fired from"),
        CT_FIELD(Projectile, fuseTick).withDescription("Explodes at this tick"),
        CT_FIELD(Projectile, explodedTick).withDescription("Tick it exploded"),
        CT_FIELD(Projectile, flags).withFlagNames(kFlagNames),
    };
    static constexpr TypeSchema value{"projectile", sizeof(Projectile), kFields};
};
static_assert(describesEveryByte(Schema<Projectile>::value));

inline bool hasExploded(const Projectile& projectile) {
    return (projectile.flags & kProjectileExploded) != 0;
}

} // namespace ct
