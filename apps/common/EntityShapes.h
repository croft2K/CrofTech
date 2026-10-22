#pragma once

#include "croftech/client/LocalGame.h"
#include "croftech/core/Math.h"
#include "croftech/render/Renderer.h"
#include "croftech/sim/CollisionWorld.h"

#include <chrono>
#include <vector>

namespace ct {

// Simple shapes for what moves in the game, drawn until M5 brings models and animation. Built on
// the CPU every frame as triangle lists for Renderer::setDynamicGeometry.

// A box along three perpendicular unit axes, halfExtents measured along each. The axes must be
// right-handed (the third is the cross product of the first two), or it is drawn inside out.
void addOrientedBox(std::vector<WorldVertex>& out, Vec3 centre, const Vec3 (&axes)[3],
                    Vec3 halfExtents, Vec3 color);

// A see-through line as two crossed strips, so it shows from any side.
void addEffectLine(std::vector<EffectVertex>& out, Vec3 start, Vec3 end, float width, Vec3 colour,
                   float alpha);

// A box turned by yaw about its centre's vertical axis.
void addBox(std::vector<WorldVertex>& out, Vec3 centre, Vec3 halfExtents, float yawRadians,
            Vec3 color);

// Every player in the newest snapshot as a body the size of its collision box, coloured by input
// slot (flashing pale for a moment when hurt), with a dark visor facing where it looks; the dead
// lie flat. The client's own player is
// left out unless includeOwn (seen from behind in third person).
void addPlayers(std::vector<WorldVertex>& out, const LocalGame& game,
                LocalGame::Clock::time_point now, bool includeOwn);

// Every enemy in the newest snapshot as a body the size of its kind's box, in a dark colour of its
// kind (flashing pale for a moment when hurt) with pale eyes facing where it looks, and an arm
// raised while a swing winds up; the dead lie flat.
void addEnemies(std::vector<WorldVertex>& out, const LocalGame& game,
                LocalGame::Clock::time_point now);

// Every rocket and grenade still flying in the newest snapshot, as a small box: pale for those that
// fly straight, dark for those that bounce.
void addProjectiles(std::vector<WorldVertex>& out, const LocalGame& game,
                    LocalGame::Clock::time_point now);

// Every pickup that can be taken in the newest snapshot, turning and bobbing above where it rests:
// a cube for health (blue for one that goes over the maximum), a flat slab for armour (red for
// heavy), a small box for ammo and a long one for a weapon.
void addPickups(std::vector<WorldVertex>& out, const LocalGame& game,
                LocalGame::Clock::time_point now);

// The weapon in the player's hand, held low and to the right of a first-person view from eye,
// looking along yaw and pitch (radians, pitch up positive). Each weapon is a box of its own length;
// it kicks back when fired and comes up from below when taken out, timed from the newest snapshot.
// Nothing while dead or unarmed.
void addViewWeapon(std::vector<WorldVertex>& out, const LocalGame& game, Vec3 eye, float yawRadians,
                   float pitchRadians, LocalGame::Clock::time_point now);

// Where a camera following the player from behind goes: back from the eyes along the view, and
// short of any wall in the way.
Vec3 thirdPersonPosition(const CollisionWorld& world, Vec3 eye, float yawRadians,
                         float pitchRadians);

} // namespace ct
