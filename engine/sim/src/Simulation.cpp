#include "croftech/sim/Simulation.h"

#include "LevelLogic.h"

#include "croftech/core/Assert.h"
#include "croftech/core/Hash.h"
#include "croftech/core/Profile.h"
#include "croftech/sim/CollisionScene.h"
#include "croftech/sim/Damage.h"
#include "croftech/sim/Firing.h"
#include "croftech/sim/GameLevel.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <span>

namespace ct {
namespace {

// Puts a player on the spawn point as if just arrived: standing still, unhurt, with the weapons
// given from spawn, raised from the given tick.
void placePlayer(World& world, const GameSettings& settings, const SpawnPoint& spawn, EntityId id,
                 TickIndex tick) {
    const Vec3 centre =
        spawn.feetPosition +
        Vec3{0.0f, 0.0f, settings.movement.halfExtents.z + CollisionWorld::kSurfaceEpsilon};
    *world.transforms.get(id) = {centre, spawn.yaw, 0};
    *world.playerMotions.get(id) = {};
    Health health;
    health.health = settings.rules.startHealth;
    health.armour = settings.rules.startArmour;
    *world.healths.get(id) = health;
    armPlayer(*world.weapons.get(id), settings, tick);
}

} // namespace

Simulation::Simulation(const GameLevel& level, const GameSettings& settings)
    : m_level(&level), m_settings(settings) {}

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
    logic.runMovers(m_settings.movement);
    const CollisionScene scene(*m_level, world);
    movePlayers(scene, inputs, next);
    runWeapons(scene, m_settings, inputs, next);
    logic.activateMovers(m_settings.movement);
    logic.updateTriggers(m_settings.movement);
    logic.runHurtVolumes(m_settings);
}

void Simulation::movePlayers(const CollisionScene& scene, const InputSet& inputs,
                             State& state) const {
    World& world = state.world;
    const MovementSettings& movement = m_settings.movement;
    const std::span<const EntityId> controlled = world.playerControls.entities();
    const std::span<PlayerControl> controls = world.playerControls.components();
    for (std::size_t i = 0; i < controlled.size(); ++i) {
        const EntityId id = controlled[i];
        PlayerControl& control = controls[i];
        Transform* transform = world.transforms.get(id);
        PlayerMotion* motion = world.playerMotions.get(id);
        if (control.inputSlot >= kMaxPlayers || transform == nullptr || motion == nullptr) {
            continue;
        }
        UserCmd cmd = inputs.commands[control.inputSlot];
        const std::uint32_t pressed = cmd.buttons & ~control.buttons;
        control.buttons = cmd.buttons;

        if (const Health* health = world.healths.get(id); health != nullptr && isDead(*health)) {
            if ((pressed & (kButtonAttack | kButtonJump)) != 0 &&
                canRespawn(*health, m_settings.rules, state.tick)) {
                if (const SpawnPoint* spawn = chooseSpawnPoint(*m_level, world, id)) {
                    placePlayer(world, m_settings, *spawn, id, state.tick);
                    // A jump that respawned doesn't also jump.
                    motion->flags = (cmd.buttons & kButtonJump) != 0 ? kPlayerJumpHeld : 0u;
                }
                continue;
            }
            // The dead drop where they fell, and stop once out of the world.
            if (transform->position.z - movement.halfExtents.z < m_level->killHeight()) {
                continue;
            }
            cmd = UserCmd{transform->yaw, transform->pitch, 0, 0, 0};
        }
        movePlayer(scene, movement, cmd, *transform, *motion);
        if (transform->position.z - movement.halfExtents.z < m_level->killHeight()) {
            kill(state, id, DamageKind::OutOfWorld);
        }
    }
}

EntityId spawnPlayer(State& state, const GameSettings& settings, const SpawnPoint& spawn,
                     std::uint32_t inputSlot) {
    World& world = state.world;
    const EntityId id = world.entities.create();
    if (!id.isValid()) {
        return id;
    }
    world.transforms.add(id, {});
    world.playerMotions.add(id, {});
    world.playerControls.add(id, {inputSlot, 0});
    world.healths.add(id, {});
    world.weapons.add(id, startingWeapons(settings, inputSlot, state.tick));
    placePlayer(world, settings, spawn, id, state.tick);
    return id;
}

const SpawnPoint* chooseSpawnPoint(const GameLevel& level, const World& world, EntityId except) {
    const SpawnPoint* best = nullptr;
    float bestDistance = -1.0f;
    for (const SpawnPoint& spawn : level.spawns()) {
        float nearest = std::numeric_limits<float>::max();
        const std::span<const EntityId> players = world.playerControls.entities();
        for (const EntityId player : players) {
            const Transform* transform = world.transforms.get(player);
            const Health* health = world.healths.get(player);
            if (player == except || transform == nullptr ||
                (health != nullptr && isDead(*health))) {
                continue;
            }
            const Vec3 offset = transform->position - spawn.feetPosition;
            nearest = std::min(nearest, dot(offset, offset));
        }
        if (nearest > bestDistance) {
            best = &spawn;
            bestDistance = nearest;
        }
    }
    return best;
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
