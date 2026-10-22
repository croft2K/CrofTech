#pragma once

#include "croftech/sim/GameSettings.h"
#include "croftech/sim/Health.h"
#include "croftech/sim/World.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

struct ImDrawList;

namespace ct {

// The heads-up display over the game's view, shared by the game and play-in-editor: health and
// armour, a red flash when hurt, the crosshair with a mark when your shots hurt someone, the
// weapons carried and the ammo of the one in hand, and the death screen. It reads only the states
// the client has been sent, like any other presentation.
class GameHud {
public:
    using Clock = std::chrono::steady_clock;

    // The game's weapon names, in the order of GameSettings::weapons (as game.cfg lists them).
    void setWeaponNames(std::vector<std::string> names) { m_weaponNames = std::move(names); }

    // Once a frame, with the newest state the client has (null before the first). Returns true
    // when the player has just respawned, when the view should turn the way the player faces.
    bool update(const State* latest, EntityId player, const GameSettings& settings,
                Clock::time_point now);

    // Into the current ImGui frame, behind any windows, over the part of the screen the game is
    // drawn in (ImGui coordinates).
    void draw(float x, float y, float width, float height, Clock::time_point now) const;

private:
    void drawWeapons(ImDrawList& draw, float right, float bottom, float big) const;

    bool m_known = false; // whether the player's health has been seen yet
    bool m_dead = false;
    bool m_canRespawn = false;
    std::int32_t m_health = 0;
    std::int32_t m_armour = 0;
    std::uint32_t m_hurtTick = 0;
    DamageKind m_cause = DamageKind::Generic;
    Clock::time_point m_flashStart;
    float m_flashStrength = 0.0f;

    std::vector<std::string> m_weaponNames;
    bool m_armed = false; // carries any weapon
    std::uint32_t m_weaponCount = 0;
    std::uint32_t m_current = 0;
    std::uint32_t m_owned = 0;
    std::array<std::int32_t, kMaxWeapons> m_ammo{};
    std::array<bool, kMaxWeapons> m_endless{}; // needs no ammo
    std::array<bool, kMaxWeapons> m_empty{};   // too little ammo to fire

    TickIndex m_seenTick = 0;
    TickIndex m_hitTick = 0; // the newest tick the player hurt someone else
    Clock::time_point m_hitStart;
};

} // namespace ct
