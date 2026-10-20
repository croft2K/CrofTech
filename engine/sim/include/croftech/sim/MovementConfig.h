#pragma once

#include "croftech/core/Console.h"
#include "croftech/sim/PlayerMovement.h"

namespace ct {

// Movement tuning lives in console variables named "movement.<key>", one per line of
// content/config/movement.cfg, so the tuning file and the developer console change the same values.
// Both are outside the simulation: the result reaches it as a MovementSettings between ticks.
inline constexpr std::string_view kMovementCVarPrefix = "movement.";

// Adds the variables with the built-in defaults.
void registerMovementCVars(Console& console);

MovementSettings movementSettingsFromCVars(const Console& console);

} // namespace ct
