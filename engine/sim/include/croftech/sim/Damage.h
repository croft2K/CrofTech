#pragma once

#include "croftech/core/Math.h"
#include "croftech/sim/EntityId.h"
#include "croftech/sim/GameSettings.h"
#include "croftech/sim/Health.h"
#include "croftech/sim/World.h"

#include <cstdint>

namespace ct {

// One hit: how much, of what kind, from whom, and how hard it pushes.
struct Damage {
    std::int32_t amount = 0;
    DamageKind kind = DamageKind::Generic;
    EntityId attacker; // invalid for the level itself (hazards, falling)
    Vec3 push;         // added to the target's velocity, m/s
};

// The one way anything is hurt. Armour takes its share of the damage (RulesSettings::armourShare)
// while it lasts and health takes the rest; at zero health the target dies. Targets without
// health, and the dead, are left alone. Returns whether this killed the target.
bool applyDamage(State& state, const RulesSettings& rules, EntityId target, const Damage& damage);

// Kills the target whatever its health and armour, as falling out of the world does.
void kill(State& state, EntityId target, DamageKind kind, EntityId attacker = {});

// Whether a dead player has been dead long enough to respawn.
bool canRespawn(const Health& health, const RulesSettings& rules, TickIndex tick);

} // namespace ct
