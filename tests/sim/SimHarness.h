#pragma once

#include "croftech/sim/GameLevel.h"
#include "croftech/sim/Simulation.h"

#include <memory>
#include <vector>

namespace ct::test {

// Runs a headless simulation of a level, with players spawned wherever a test wants them.
class SimHarness {
public:
    explicit SimHarness(std::vector<Brush> brushes, MovementSettings movement = {},
                        RulesSettings rules = {})
        : SimHarness(Level{std::move(brushes), {}}, movement, rules) {}

    explicit SimHarness(const Level& level, MovementSettings movement = {},
                        RulesSettings rules = {})
        : SimHarness(level, GameSettings{movement, rules, {}, {}}) {}

    SimHarness(const Level& level, const GameSettings& settings)
        : m_level(level, {}, settings), m_simulation(m_level, settings),
          m_current(std::make_unique<State>(m_level.startState())),
          m_next(std::make_unique<State>()) {}

    // The first player spawned is the one transform() and motion() describe.
    EntityId spawn(Vec3 feetPosition, std::uint16_t yaw = 0, std::uint32_t inputSlot = 0) {
        const EntityId id =
            spawnPlayer(*m_current, m_simulation.settings(), {feetPosition, yaw}, inputSlot);
        if (!m_player.isValid()) {
            m_player = id;
        }
        return id;
    }

    void runInputs(const InputSet& inputs, int ticks) {
        for (int i = 0; i < ticks; ++i) {
            m_simulation.step(*m_current, inputs, *m_next);
            std::swap(m_current, m_next);
        }
    }

    // Every player but slot 0 stands still.
    void run(const UserCmd& cmd, int ticks) {
        InputSet inputs;
        inputs.commands[0] = cmd;
        runInputs(inputs, ticks);
    }

    // Moves the first player, as a teleport would.
    void place(Vec3 feetPosition) {
        m_current->world.transforms.get(m_player)->position =
            feetPosition +
            Vec3{0.0f, 0.0f, movement().halfExtents.z + CollisionWorld::kSurfaceEpsilon};
    }

    const GameLevel& level() const { return m_level; }
    const State& state() const { return *m_current; }
    State& state() { return *m_current; }
    const Transform& transform() const { return *m_current->world.transforms.get(m_player); }
    const PlayerMotion& motion() const { return *m_current->world.playerMotions.get(m_player); }
    const Health& health() const { return *m_current->world.healths.get(m_player); }
    Health& health() { return *m_current->world.healths.get(m_player); }
    const Weapons& weapons() const { return *m_current->world.weapons.get(m_player); }
    Weapons& weapons() { return *m_current->world.weapons.get(m_player); }
    bool onGround() const { return (motion().flags & kPlayerOnGround) != 0; }
    float feetZ() const { return transform().position.z - movement().halfExtents.z; }
    const GameSettings& settings() const { return m_simulation.settings(); }
    const MovementSettings& movement() const { return settings().movement; }

private:
    GameLevel m_level;
    Simulation m_simulation;
    std::unique_ptr<State> m_current;
    std::unique_ptr<State> m_next;
    EntityId m_player;
};

// A 100 x 100 m floor whose top is at z = 0.
inline Brush floorBrush() { return makeBoxBrush({-50.0f, -50.0f, -1.0f}, {50.0f, 50.0f, 0.0f}); }

// The floor with a player spawn at the origin, facing +X.
inline Level floorLevel() { return {{floorBrush()}, {makePlayerSpawn({0.0f, 0.0f, 0.0f}, 0.0f)}}; }

} // namespace ct::test
