#pragma once

#include "croftech/core/Math.h"
#include "croftech/sim/EntityId.h"
#include "croftech/sim/GameSettings.h"
#include "croftech/sim/World.h"

#include <cstdint>

namespace ct {

// Players and enemies are bodies that movePlayer moves. This is what one moves by: the game's
// movement for players, and its kind's (EnemySettings::movement) for an enemy. Its box, eye height
// and speed come from it.
const MovementSettings& movementOf(const World& world, EntityId id, const GameSettings& settings);

// Gives an entity the body and mind of an enemy of the type (a GameSettings::enemies index),
// standing on feet and facing yaw, at full health and not yet alerted.
void makeEnemy(World& world, EntityId id, const GameSettings& settings, std::uint32_t type,
               Vec3 feet, std::uint16_t yaw);

} // namespace ct
