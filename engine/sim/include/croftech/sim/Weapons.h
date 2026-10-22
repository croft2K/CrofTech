#pragma once

#include "croftech/core/Schema.h"
#include "croftech/sim/UserCmd.h"

#include <array>
#include <cstdint>

namespace ct {

// Most ammo any weapon can hold, so counts fit the 16-bit fields below.
inline constexpr std::int32_t kMaxAmmo = 9999;

// What a player carries: which weapons, the ammo for each and the one in hand. Weapon i is
// GameSettings::weapons[i]. Shots draw their spread from a random counter kept here, so they
// repeat exactly in replays and rewinds.
struct Weapons {
    std::uint32_t owned = 0;      // bit i: carries weapon i
    std::uint32_t current = 0;    // in hand
    std::uint32_t readyTick = 0;  // fires no sooner: after a shot, or while being raised
    std::uint32_t raisedTick = 0; // when the one in hand was taken out
    std::uint32_t firedTick = 0;  // its last shot; 0 is never
    std::uint32_t random = 0;     // counter for randomUnit (core/Random.h)
    std::array<std::int16_t, kMaxWeapons> ammo{};
};

template <>
struct Schema<Weapons> {
    static constexpr FieldInfo kFields[] = {
        CT_FIELD(Weapons, owned)
            .withHint(FieldHint::Flags)
            .withDescription("Bit i: carries weapon i"),
        CT_FIELD(Weapons, current).withRange(0, kMaxWeapons - 1).withDescription("Weapon in hand"),
        CT_FIELD(Weapons, readyTick).withDescription("Fires no sooner than this tick"),
        CT_FIELD(Weapons, raisedTick).withDescription("Tick the weapon in hand was taken out"),
        CT_FIELD(Weapons, firedTick).withDescription("Tick of its last shot; 0 is never"),
        CT_FIELD(Weapons, random).withDescription("Counter its shots draw spread from"),
        CT_FIELD_ELEMENT(Weapons, ammo, 0).withRange(0, kMaxAmmo),
        CT_FIELD_ELEMENT(Weapons, ammo, 1).withRange(0, kMaxAmmo),
        CT_FIELD_ELEMENT(Weapons, ammo, 2).withRange(0, kMaxAmmo),
        CT_FIELD_ELEMENT(Weapons, ammo, 3).withRange(0, kMaxAmmo),
        CT_FIELD_ELEMENT(Weapons, ammo, 4).withRange(0, kMaxAmmo),
        CT_FIELD_ELEMENT(Weapons, ammo, 5).withRange(0, kMaxAmmo),
        CT_FIELD_ELEMENT(Weapons, ammo, 6).withRange(0, kMaxAmmo),
        CT_FIELD_ELEMENT(Weapons, ammo, 7).withRange(0, kMaxAmmo),
    };
    static constexpr TypeSchema value{"weapons", sizeof(Weapons), kFields};
};
static_assert(describesEveryByte(Schema<Weapons>::value));
static_assert(kMaxWeapons == 8, "list every ammo element in Schema<Weapons>");

inline bool carries(const Weapons& weapons, std::uint32_t weapon) {
    return weapon < kMaxWeapons && (weapons.owned & (1u << weapon)) != 0;
}

} // namespace ct
