#pragma once

#include "croftech/core/Console.h"
#include "croftech/sim/PlayerMovement.h"

#include <cstdint>
#include <string_view>

namespace ct {

// The rules of play a game tunes: health, armour and respawning. Like movement, each is a console
// variable, "rules.<key>", set by a line of content/config/rules.cfg.
struct RulesSettings {
    std::int32_t startHealth = 100;
    std::int32_t startArmour = 0;
    float armourShare = 0.66f; // of each hit's damage that armour takes while it lasts
    float respawnDelay = 1.0f; // seconds after dying before fire respawns
};

// Everything a game tunes that the simulation reads. It may only change between ticks.
struct GameSettings {
    MovementSettings movement;
    RulesSettings rules;
};

inline constexpr std::string_view kRulesCVarPrefix = "rules.";

// Adds the movement and rules variables with the built-in defaults.
void registerGameCVars(Console& console);

GameSettings gameSettingsFromCVars(const Console& console);

} // namespace ct
