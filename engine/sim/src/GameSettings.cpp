#include "croftech/sim/GameSettings.h"

#include "croftech/core/Assert.h"
#include "croftech/sim/MovementConfig.h"

#include <string>

namespace ct {
namespace {

// Large enough for any game, small enough that adding damage can't overflow.
constexpr int kMaxHealthSetting = 100000;

std::string cvarName(std::string_view key) { return std::string(kRulesCVarPrefix).append(key); }

const CVar& rulesCVar(const Console& console, std::string_view key) {
    const CVar* cvar = console.find(cvarName(key));
    CT_ASSERT(cvar != nullptr);
    return *cvar;
}

} // namespace

void registerGameCVars(Console& console) {
    registerMovementCVars(console);
    const RulesSettings defaults;
    console.add(CVar::makeInt(cvarName("start_health"), defaults.startHealth, 1, kMaxHealthSetting,
                              "health a player spawns with"));
    console.add(CVar::makeInt(cvarName("start_armour"), defaults.startArmour, 0, kMaxHealthSetting,
                              "armour a player spawns with"));
    console.add(CVar::makeFloat(cvarName("armour_share"), defaults.armourShare, 0.0f, 1.0f,
                                "share of each hit's damage armour takes while it lasts"));
    console.add(CVar::makeFloat(cvarName("respawn_delay"), defaults.respawnDelay, 0.0f, 60.0f,
                                "seconds after dying before fire respawns"));
}

GameSettings gameSettingsFromCVars(const Console& console) {
    GameSettings settings;
    settings.movement = movementSettingsFromCVars(console);
    settings.rules.startHealth = rulesCVar(console, "start_health").asInt();
    settings.rules.startArmour = rulesCVar(console, "start_armour").asInt();
    settings.rules.armourShare = rulesCVar(console, "armour_share").asFloat();
    settings.rules.respawnDelay = rulesCVar(console, "respawn_delay").asFloat();
    return settings;
}

} // namespace ct
