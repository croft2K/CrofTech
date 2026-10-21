#pragma once

#include "croftech/level/Level.h"
#include "croftech/sim/GameSettings.h"
#include "croftech/sim/UserCmd.h"
#include "croftech/sim/World.h"

#include <cstdint>

namespace ct {

class CollisionScene;
class GameLevel;

// The game simulation. step() is a pure function of the previous state, the inputs and the static
// level: no clock, no I/O, no globals. The same code runs on the server, in client prediction and
// in replays. States start from the level's GameLevel::startState().
class Simulation {
public:
    // The level must outlive the simulation.
    Simulation(const GameLevel& level, const GameSettings& settings);

    // Settings may only change between ticks.
    void setSettings(const GameSettings& settings) { m_settings = settings; }
    const GameSettings& settings() const { return m_settings; }

    // Writes the state for prev.tick + 1 into next. prev and next must be different objects.
    void step(const State& prev, const InputSet& inputs, State& next) const;

    const GameLevel& level() const { return *m_level; }

private:
    // Players move by their commands, or, once dead, lie still until fire respawns them.
    void movePlayers(const CollisionScene& scene, const InputSet& inputs, State& state) const;

    const GameLevel* m_level;
    GameSettings m_settings;
};

// Spawns a player standing on the spawn point with the starting health and armour, driven by the
// given input slot.
EntityId spawnPlayer(World& world, const GameSettings& settings, const SpawnPoint& spawn,
                     std::uint32_t inputSlot);

// The spawn point furthest from every living player but the one given, so nobody appears next to
// an opponent; ties go to the first in the level. Null if the level has none.
const SpawnPoint* chooseSpawnPoint(const GameLevel& level, const World& world,
                                   EntityId except = {});

// Hash of everything in the state that affects the future. Equal states hash equal on every
// platform; used to check determinism tick by tick.
std::uint64_t hashState(const State& state);

} // namespace ct
