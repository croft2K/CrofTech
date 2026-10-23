#pragma once

#include "croftech/core/Math.h"
#include "croftech/sim/EntityId.h"
#include "croftech/sim/GameSettings.h"
#include "croftech/sim/World.h"

#include <cstdint>
#include <optional>

namespace ct {

// The type's settings with the overrides (if any) in their place, and its movement (the game's,
// with its size and speed) worked out again for them.
EnemyTuning tunedEnemy(const EnemySettings& type, const EnemyOverrides* overrides,
                       const MovementSettings& movement);
// What an enemy plays by: its type's settings with its own (EnemyOverrides). Nullopt for an
// entity that isn't an enemy of one of the game's types.
std::optional<EnemyTuning> enemyTuning(const World& world, EntityId id,
                                       const GameSettings& settings);

// Players and enemies are bodies that movePlayer moves. This is what one moves by: the game's
// movement for players, and its own (EnemyTuning::movement) for an enemy. Its box, eye height and
// speed come from it.
MovementSettings movementOf(const World& world, EntityId id, const GameSettings& settings);

// Gives an entity the body and mind of an enemy of the type (a GameSettings::enemies index), with
// any settings of its own, standing on feet and facing yaw, at full health and not yet alerted.
void makeEnemy(World& world, EntityId id, const GameSettings& settings, std::uint32_t type,
               Vec3 feet, std::uint16_t yaw, const EnemyOverrides& overrides = {});

} // namespace ct
