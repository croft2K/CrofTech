#pragma once

#include "croftech/core/Math.h"
#include "croftech/sim/EntityId.h"
#include "croftech/sim/GameSettings.h"
#include "croftech/sim/UserCmd.h"
#include "croftech/sim/World.h"

#include <cstdint>

namespace ct {

class CollisionScene;
class GameLevel;

// How long an exploded projectile stays, so every client sees where it went off.
inline constexpr std::uint32_t kExplosionTicks = kTickRate / 4;

// Where a shot from start towards end stops: at the first box of a living player (or, later,
// enemy) it passes into, other than the shooter's, or at the world or a mover.
struct ShotHit {
    float fraction = 1.0f; // of the way from start to end
    Vec3 position;
    Vec3 normal;     // of the surface hit; zero for a target, or when nothing was hit
    EntityId target; // the one hit, if any; shots damage only targets
    EntityId mover;  // the mover hit, if the shot stopped at one
};

ShotHit traceShot(const CollisionScene& scene, const World& world, const GameSettings& settings,
                  Vec3 start, Vec3 end, EntityId shooter);

// Fires the weapon in hand from the shooter's eye along where its transform looks, as a player
// pressing fire does once it is ready: for enemies, which choose when to fire themselves.
void fireWeapon(const CollisionScene& scene, const GameSettings& settings, EntityId shooter,
                Weapons& weapons, const Transform& transform, State& state);

// The way a transform looks: yaw 0 is +X, and a positive pitch looks up (as the camera does).
Vec3 viewForward(const Transform& transform);

// The direction a pellet flies: up to spread degrees from where the transform looks, spread evenly
// over the circle that makes, drawn from the random counter.
Vec3 pelletDirection(const Transform& transform, float spread, std::uint32_t& random);

// Arms a player as at spawn: the weapons given from spawn with their starting ammo, and the first
// of them in hand, being raised from the given tick. The random counter carries on.
void armPlayer(Weapons& weapons, const GameSettings& settings, TickIndex tick);

// Adds ammo for the weapon, up to its max_ammo. Returns false, changing nothing, if it is full
// (or the weapon isn't one of the game's), so a pickup can be left for someone who needs it.
bool giveAmmo(Weapons& weapons, const GameSettings& settings, std::uint32_t weapon,
              std::int32_t amount);

// Gives the weapon with some ammo. One not carried before is taken out at once if it comes later
// in the list (later weapons are stronger). Returns false if it was carried already with full
// ammo.
bool giveWeapon(Weapons& weapons, const GameSettings& settings, std::uint32_t weapon,
                std::int32_t ammo, TickIndex tick);

// A new player's weapons, armed from the given tick. Each input slot's shots draw from their own
// stretch of random numbers.
Weapons startingWeapons(const GameSettings& settings, std::uint32_t inputSlot, TickIndex tick);

// Moves every projectile one tick, bouncing or setting it off when it hits something (never its
// owner) or its fuse runs out, and removes it a little after it has exploded, or once it falls out
// of the world.
void runProjectiles(const CollisionScene& scene, const GameLevel& level,
                    const GameSettings& settings, State& state);

// Switches and fires every living player's weapons by their commands. Hitscan pellets hit at
// once; projectiles are launched from the eye and move from the next tick. Runs after movement,
// so shots leave from where players are at the end of the tick.
void runWeapons(const CollisionScene& scene, const GameSettings& settings, const InputSet& inputs,
                State& state);

} // namespace ct
