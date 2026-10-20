#pragma once

#include "croftech/sim/ComponentPool.h"
#include "croftech/sim/Components.h"
#include "croftech/sim/EntityId.h"

#include <cstdint>

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

    void destroy(EntityId id) noexcept;
};

// Calls fn on every component pool. Register each new pool here so destruction and state hashing
// stay complete.
template <typename WorldT, typename Fn>
void forEachPool(WorldT& world, Fn&& fn) {
    fn(world.transforms);
    fn(world.playerMotions);
    fn(world.playerControls);
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
