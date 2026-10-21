#pragma once

#include "croftech/client/LocalGame.h"
#include "croftech/core/Math.h"
#include "croftech/sim/PlayerMovement.h"
#include "croftech/sim/World.h"

#include <chrono>
#include <cstdint>

namespace ct {

// Eases the view over stairs. The simulation lifts a walking player up a step, or down onto the
// next one, within a single tick, which jolts the camera; this lets the view follow such changes
// in height over a moment instead. Presentation only: the player's position isn't changed, and
// jumps, falls and lifts are followed at once as before.
class StepSmoothing {
public:
    using Clock = std::chrono::steady_clock;

    // Height changes smaller than this in one snapshot are walking on slopes, not steps.
    static constexpr float kMinStep = 0.06f;

    // Once a frame, with where the game puts the player's eyes (LocalGame::eyePosition). Returns
    // where to draw them. seconds is about how long the view takes to settle after a step; 0
    // turns smoothing off.
    Vec3 apply(const LocalGame& game, Vec3 eye, float seconds, Clock::time_point now);

    // The same for any client: the newest state it has (or null), its timeline, and the time
    // since the last frame.
    Vec3 apply(const State* state, EntityId player, std::uint32_t timeline,
               const MovementSettings& movement, Vec3 eye, float seconds, float elapsed);

private:
    bool m_known = false;
    TickIndex m_tick = 0;
    std::uint32_t m_timeline = 0;
    float m_z = 0.0f; // the player's height in that snapshot
    bool m_onGround = false;
    bool m_stepped = false; // between the two newest snapshots
    float m_offset = 0.0f;  // of the drawn eyes from where they are, shrinking to zero
    float m_drawnZ = 0.0f;
    Clock::time_point m_lastFrame;
};

} // namespace ct
