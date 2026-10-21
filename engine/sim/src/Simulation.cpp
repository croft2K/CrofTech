#include "croftech/sim/Simulation.h"

#include "LevelLogic.h"

#include "croftech/core/Assert.h"
#include "croftech/core/Hash.h"
#include "croftech/core/Profile.h"
#include "croftech/sim/CollisionScene.h"
#include "croftech/sim/GameLevel.h"

#include <cstddef>
#include <span>

namespace ct {

Simulation::Simulation(const GameLevel& level, const MovementSettings& movement)
    : m_level(&level), m_movement(movement) {}

void Simulation::step(const State& prev, const InputSet& inputs, State& next) const {
    CT_PROFILE_ZONE();
    CT_ASSERT(&prev != &next);
    next = prev;
    next.tick = prev.tick + 1;
    World& world = next.world;

    // Systems run in a fixed order every tick.
    LevelLogic logic(*m_level, next);
    logic.deliverPendingInputs();
    logic.runTimers();
    logic.runMovers(m_movement);

    const CollisionScene scene(*m_level, world);
    const std::span<const EntityId> controlled = world.playerControls.entities();
    const std::span<const PlayerControl> controls = world.playerControls.components();
    for (std::size_t i = 0; i < controlled.size(); ++i) {
        const std::uint32_t slot = controls[i].inputSlot;
        Transform* transform = world.transforms.get(controlled[i]);
        PlayerMotion* motion = world.playerMotions.get(controlled[i]);
        if (slot >= kMaxPlayers || transform == nullptr || motion == nullptr) {
            continue;
        }
        movePlayer(scene, m_movement, inputs.commands[slot], *transform, *motion);
    }
    logic.activateMovers(m_movement);
    logic.updateTriggers(m_movement);
}

EntityId spawnPlayer(World& world, const MovementSettings& movement, const SpawnPoint& spawn,
                     std::uint32_t inputSlot) {
    const EntityId id = world.entities.create();
    if (!id.isValid()) {
        return id;
    }
    const Vec3 centre = spawn.feetPosition +
                        Vec3{0.0f, 0.0f, movement.halfExtents.z + CollisionWorld::kSurfaceEpsilon};
    world.transforms.add(id, {centre, spawn.yaw, 0});
    world.playerMotions.add(id, {});
    world.playerControls.add(id, {inputSlot});
    return id;
}

std::uint64_t hashState(const State& state) {
    CT_PROFILE_ZONE();
    std::uint64_t hash = hashBytes(std::as_bytes(std::span{&state.tick, 1}));
    const auto& entities = state.world.entities;
    hash = hashBytes(std::as_bytes(entities.generations()), hash);
    hash = hashBytes(std::as_bytes(entities.aliveFlags()), hash);
    hash = hashBytes(std::as_bytes(entities.freeList()), hash);
    forEachPool(state.world, [&hash](const auto& pool) {
        hash = hashBytes(std::as_bytes(pool.entities()), hash);
        hash = hashBytes(std::as_bytes(pool.components()), hash);
    });
    return hash;
}

} // namespace ct
