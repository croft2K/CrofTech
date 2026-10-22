#pragma once

#include "croftech/client/GameEvents.h"
#include "croftech/render/Renderer.h"

#include <chrono>
#include <span>
#include <vector>

namespace ct {

// Short-lived effects for GameEvents, such as the glow where a player respawns. Presentation only,
// timed by the clock; they are added to the effect geometry each frame until they fade.
class GameEffects {
public:
    using Clock = std::chrono::steady_clock;

    void add(std::span<const GameEvent> events, Clock::time_point now);
    // Appends what is showing now, and forgets what has faded.
    void build(Clock::time_point now, std::vector<EffectVertex>& out);
    void clear() { m_effects.clear(); }

private:
    struct Effect {
        Vec3 position;
        Clock::time_point start;
    };
    std::vector<Effect> m_effects; // respawn glows
};

} // namespace ct
