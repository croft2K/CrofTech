#pragma once

#include "croftech/level/Level.h"
#include "croftech/sim/PlayerMovement.h"
#include "croftech/sim/UserCmd.h"
#include "croftech/sim/World.h"

#include <cstdint>

namespace ct {

class GameLevel;

// The game simulation. step() is a pure function of the previous state, the inputs and the static
// level: no clock, no I/O, no globals. The same code runs on the server, in client prediction and
// in replays. States start from the level's GameLevel::startState().
class Simulation {
public:
    // The level must outlive the simulation.
    Simulation(const GameLevel& level, const MovementSettings& movement);

    // Settings may only change between ticks.
    void setMovementSettings(const MovementSettings& movement) { m_movement = movement; }
    const MovementSettings& movementSettings() const { return m_movement; }

    // Writes the state for prev.tick + 1 into next. prev and next must be different objects.
    void step(const State& prev, const InputSet& inputs, State& next) const;

    const GameLevel& level() const { return *m_level; }

private:
    const GameLevel* m_level;
    MovementSettings m_movement;
};

// Spawns a player standing on the spawn point, driven by the given input slot.
EntityId spawnPlayer(World& world, const MovementSettings& movement, const SpawnPoint& spawn,
                     std::uint32_t inputSlot);

// Hash of everything in the state that affects the future. Equal states hash equal on every
// platform; used to check determinism tick by tick.
std::uint64_t hashState(const State& state);

} // namespace ct
