#pragma once

#include "croftech/client/GameEvents.h"
#include "croftech/render/Renderer.h"

#include <chrono>
#include <span>
#include <vector>

namespace ct {

// Short-lived effects for GameEvents: the glow where a player respawns, the flash of an
// explosion, the streak of a hitscan pellet and the spark where it hit, and a spark off the head
// of an enemy that notices a player. Presentation only, timed
// by the clock; they are added to the effect geometry each frame until they fade.
class GameEffects {
public:
    using Clock = std::chrono::steady_clock;

    void add(std::span<const GameEvent> events, Clock::time_point now);
    // Appends what is showing now, and forgets what has faded.
    void build(Clock::time_point now, std::vector<EffectVertex>& out);
    void clear() { m_effects.clear(); }

private:
    struct Effect {
        GameEventKind kind = GameEventKind::Respawn;
        Vec3 position;
        Clock::time_point start;
        Vec3 origin;      // tracers: where the pellet came from
        bool hit = false; // impacts: on someone, not a wall
    };
    std::vector<Effect> m_effects;
};

} // namespace ct
