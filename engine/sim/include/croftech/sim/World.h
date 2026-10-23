#pragma once

#include "croftech/sim/ComponentPool.h"
#include "croftech/sim/Components.h"
#include "croftech/sim/Enemy.h"
#include "croftech/sim/EntityId.h"
#include "croftech/sim/Health.h"
#include "croftech/sim/LogicComponents.h"
#include "croftech/sim/Projectile.h"
#include "croftech/sim/Weapons.h"

#include <cstdint>
#include <span>
#include <type_traits>

namespace ct {

using TickIndex = std::uint32_t;

inline constexpr std::uint32_t kTickRate = 128;
inline constexpr float kTickSeconds = 1.0f / static_cast<float>(kTickRate); // exactly 7.8125 ms

inline constexpr std::uint32_t kMaxEntities = 1024;

// All simulated game state. Plain data with fixed capacity: copying a World is a snapshot.
struct World {
    EntityAllocator<kMaxEntities> entities;
    ComponentPool<Transform, kMaxEntities> transforms;
    ComponentPool<PlayerMotion, kMaxEntities> playerMotions;
    ComponentPool<PlayerControl, kMaxEntities> playerControls;
    ComponentPool<Health, kMaxEntities> healths;
    ComponentPool<Weapons, kMaxEntities> weapons;
    ComponentPool<Projectile, kMaxEntities> projectiles;
    ComponentPool<Enemy, kMaxEntities> enemies;
    ComponentPool<LevelEntityRef, kMaxEntities> levelEntities;
    ComponentPool<Trigger, kMaxEntities> triggers;
    ComponentPool<Checkpoint, kMaxEntities> checkpoints;
    ComponentPool<Hurt, kMaxEntities> hurts;
    ComponentPool<Relay, kMaxEntities> relays;
    ComponentPool<Counter, kMaxEntities> counters;
    ComponentPool<Timer, kMaxEntities> timers;
    ComponentPool<Mover, kMaxEntities> movers;
    ComponentPool<Pickup, kMaxEntities> pickups;
    ComponentPool<Spawner, kMaxEntities> spawners;
    ComponentPool<Encounter, kMaxEntities> encounters;
    ComponentPool<Script, kMaxEntities> scripts;
    ComponentPool<PendingInput, kMaxEntities> pendingInputs;

    void destroy(EntityId id) noexcept;
};

// Calls fn on every component pool. Register each new pool here so destruction and state hashing
// stay complete.
template <typename WorldT, typename Fn>
void forEachPool(WorldT& world, Fn&& fn) {
    fn(world.transforms);
    fn(world.playerMotions);
    fn(world.playerControls);
    fn(world.healths);
    fn(world.weapons);
    fn(world.projectiles);
    fn(world.enemies);
    fn(world.levelEntities);
    fn(world.triggers);
    fn(world.checkpoints);
    fn(world.hurts);
    fn(world.relays);
    fn(world.counters);
    fn(world.timers);
    fn(world.movers);
    fn(world.pickups);
    fn(world.spawners);
    fn(world.encounters);
    fn(world.scripts);
    fn(world.pendingInputs);
}

// Calls fn(schema, bytes) for each component the entity has, in pool order. For tools that work
// on any component through its schema, such as inspectors.
template <typename Fn>
void forEachComponent(const World& world, EntityId id, Fn&& fn) {
    forEachPool(world, [&](const auto& pool) {
        if (const auto* component = pool.get(id)) {
            using T = std::remove_cvref_t<decltype(*component)>;
            fn(Schema<T>::value, std::as_bytes(std::span{component, 1}));
        }
    });
}

inline void World::destroy(EntityId id) noexcept {
    if (!entities.isAlive(id)) {
        return;
    }
    forEachPool(*this, [id](auto& pool) { pool.remove(id); });
    entities.destroy(id);
}

struct State {
    TickIndex tick = 0;
    World world;
};

} // namespace ct
