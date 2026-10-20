#pragma once

#include "croftech/sim/CollisionWorld.h"
#include "croftech/sim/Simulation.h"

#include <memory>
#include <span>
#include <vector>

namespace ct::test {

// Runs a headless simulation of one player in a level built from brushes.
class SimHarness {
public:
    explicit SimHarness(std::vector<Brush> brushes, MovementSettings movement = {})
        : m_brushes(std::move(brushes)), m_collision(m_brushes),
          m_simulation(m_collision, movement), m_current(std::make_unique<State>()),
          m_next(std::make_unique<State>()) {}

    EntityId spawn(Vec3 feetPosition, std::uint16_t yaw = 0) {
        m_player =
            spawnPlayer(m_current->world, m_simulation.movementSettings(), {feetPosition, yaw}, 0);
        return m_player;
    }

    void run(const UserCmd& cmd, int ticks) {
        InputSet inputs;
        inputs.commands[0] = cmd;
        for (int i = 0; i < ticks; ++i) {
            m_simulation.step(*m_current, inputs, *m_next);
            std::swap(m_current, m_next);
        }
    }

    const State& state() const { return *m_current; }
    const Transform& transform() const { return *m_current->world.transforms.get(m_player); }
    const PlayerMotion& motion() const { return *m_current->world.playerMotions.get(m_player); }
    bool onGround() const { return (motion().flags & kPlayerOnGround) != 0; }
    float feetZ() const {
        return transform().position.z - m_simulation.movementSettings().halfExtents.z;
    }
    const MovementSettings& movement() const { return m_simulation.movementSettings(); }

private:
    std::vector<Brush> m_brushes;
    CollisionWorld m_collision;
    Simulation m_simulation;
    std::unique_ptr<State> m_current;
    std::unique_ptr<State> m_next;
    EntityId m_player;
};

// A 100 x 100 m floor whose top is at z = 0.
inline Brush floorBrush() { return makeBoxBrush({-50.0f, -50.0f, -1.0f}, {50.0f, 50.0f, 0.0f}); }

} // namespace ct::test
