#pragma once

#include "croftech/client/LocalGame.h"
#include "croftech/core/Camera.h"
#include "croftech/render/Renderer.h"

#include <vector>

namespace ct {

// What each enemy is thinking, drawn over the game for tuning and finding bugs (client.show_ai in
// the game, Play > Show what enemies think in the editor). Read from the newest snapshot, so it
// shows what the server's AI decided, not a guess.

// See-through shapes: while it is not after anyone, its view cone on the floor and the ring within
// which it hears players run; the next corner of its path and the line on to where the path leads
// (a post marks the goal); a red line to its target while it sees them, or an orange one to where
// it last saw them; and a blue post at its own post while it is away from it.
void addEnemyThoughts(std::vector<EffectVertex>& out, const LocalGame& game,
                      LocalGame::Clock::time_point now);

// A label above each enemy: its kind, state and how long it has been in it, its health, and what
// it is waiting for. Drawn behind ImGui's windows on the view, which fills x, y, width, height of
// the screen and is seen from camera.
void drawEnemyLabels(const LocalGame& game, LocalGame::Clock::time_point now, const Camera& camera,
                     float x, float y, float width, float height);

} // namespace ct
