#pragma once

#include "croftech/core/ConfigFile.h"
#include "croftech/sim/PlayerMovement.h"

#include <span>
#include <string>
#include <vector>

namespace ct {

// Applies tuning-file entries (see content/config/movement.cfg) to movement settings. Returns one
// message per unknown key or invalid value; valid entries are applied regardless.
std::vector<std::string> applyMovementConfig(std::span<const ConfigEntry> entries,
                                             MovementSettings& settings);

} // namespace ct
