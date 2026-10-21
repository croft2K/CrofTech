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

// A box turned by yaw about its centre's vertical axis.
void addBox(std::vector<WorldVertex>& out, Vec3 centre, Vec3 halfExtents, float yawRadians,
            Vec3 color);

// Every player in the newest snapshot as a body the size of its collision box, coloured by input
// slot, with a dark visor facing where it looks; the dead lie flat. The client's own player is
// left out unless includeOwn (seen from behind in third person).
void addPlayers(std::vector<WorldVertex>& out, const LocalGame& game,
                LocalGame::Clock::time_point now, bool includeOwn);

// Where a camera following the player from behind goes: back from the eyes along the view, and
// short of any wall in the way.
Vec3 thirdPersonPosition(const CollisionWorld& world, Vec3 eye, float yawRadians,
                         float pitchRadians);

} // namespace ct
