#pragma once

#include "croftech/level/Level.h"

namespace ct {

// A movement playground built in code until the editor exists: stairs, a walkable ramp, a slope
// too steep to climb, boxes at step, jump and too-high heights, and gaps of increasing width.
Level makeMovementTestLevel();

} // namespace ct
