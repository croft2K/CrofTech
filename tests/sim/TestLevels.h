#pragma once

#include "croftech/level/Level.h"

namespace ct::test {

// A movement playground built in code: stairs, a walkable ramp, a slope too steep to climb, boxes
// at step, jump and too-high heights, and gaps of increasing width. Tests use this fixed copy; the
// playable level, content/levels/movement_test.level, started as a save of it and is free to
// change.
Level makeMovementTestLevel();

inline constexpr Vec3 kMovementTestSpawn{0.0f, -4.0f, 0.0f};

} // namespace ct::test
