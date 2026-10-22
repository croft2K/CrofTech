#pragma once

#include "croftech/core/Schema.h"
#include "croftech/sim/EntityId.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace ct {

// What hurt something: the death screen and, later, kill messages and AI read it.
enum class DamageKind : std::uint32_t {
    Generic,
    Hazard,     // a hurt volume: lava, slime, a kill pit
    OutOfWorld, // fell below the level
    Weapon,     // shot by the attacker
    Melee,      // struck by the attacker, an enemy
};

enum HealthFlags : std::uint32_t {
    kHealthDead = 1u << 0,
};

// Anything that can be hurt and killed: players and enemies, and breakables later. Damage only
// ever changes it through applyDamage (sim/Damage.h).
struct Health {
    std::int32_t health = 0;
    std::int32_t armour = 0;
    std::uint32_t flags = 0;
    std::uint32_t hurtTick = 0;   // when it last took damage; 0 is never
    std::uint32_t hurtKind = 0;   // DamageKind of that damage, which says how it died
    std::uint32_t diedTick = 0;   // while dead
    std::uint32_t hazardTick = 0; // hurt volumes leave it alone until this tick
    EntityId attacker;            // who hurt it last; invalid for the level itself
};

template <>
struct Schema<Health> {
    static constexpr std::array<std::string_view, 1> kFlagNames{"dead"};
    static constexpr FieldInfo kFields[] = {
        CT_FIELD(Health, health),
        CT_FIELD(Health, armour),
        CT_FIELD(Health, flags).withFlagNames(kFlagNames),
        CT_FIELD(Health, hurtTick).withDescription("Tick it last took damage; 0 is never"),
        CT_FIELD(Health, hurtKind)
            .withRange(0, 4)
            .withDescription("What hurt it last: 0 generic, 1 hazard, 2 fell out of the world, "
                             "3 a weapon, 4 a blow"),
        CT_FIELD(Health, diedTick).withDescription("Tick it died, while dead"),
        CT_FIELD(Health, hazardTick).withDescription("Hurt volumes leave it alone until this tick"),
        CT_FIELD(Health, attacker).withDescription("Who hurt it last"),
    };
    static constexpr TypeSchema value{"health", sizeof(Health), kFields};
};
static_assert(describesEveryByte(Schema<Health>::value));

inline bool isDead(const Health& health) { return (health.flags & kHealthDead) != 0; }

} // namespace ct
