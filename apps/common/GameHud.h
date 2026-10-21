#pragma once

#include "croftech/sim/GameSettings.h"
#include "croftech/sim/Health.h"
#include "croftech/sim/World.h"

#include <chrono>
#include <cstdint>

namespace ct {

// The heads-up display over the game's view, shared by the game and play-in-editor: health and
// armour, a red flash when hurt, and the death screen. It reads only the states the client has
// been sent, like any other presentation.
class GameHud {
public:
    using Clock = std::chrono::steady_clock;

    // Once a frame, with the newest state the client has (null before the first). Returns true
    // when the player has just respawned, when the view should turn the way the player faces.
    bool update(const State* latest, EntityId player, const RulesSettings& rules,
                Clock::time_point now);

    // Into the current ImGui frame, behind any windows, over the part of the screen the game is
    // drawn in (ImGui coordinates).
    void draw(float x, float y, float width, float height, Clock::time_point now) const;

private:
    bool m_known = false; // whether the player's health has been seen yet
    bool m_dead = false;
    bool m_canRespawn = false;
    std::int32_t m_health = 0;
    std::int32_t m_armour = 0;
    std::uint32_t m_hurtTick = 0;
    DamageKind m_cause = DamageKind::Generic;
    Clock::time_point m_flashStart;
    float m_flashStrength = 0.0f;
};

} // namespace ct
