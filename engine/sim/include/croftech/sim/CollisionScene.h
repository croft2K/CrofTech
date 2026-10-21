#pragma once

#include "croftech/sim/CollisionWorld.h"
#include "croftech/sim/World.h"

namespace ct {

class GameLevel;

// The level's collision as it stands in one state: the static world, plus each mover's brushes
// moved by its current offset. Reads the state as it is at each call, so it stays correct while
// movers move during a tick. Traces report which mover they hit.
class CollisionScene {
public:
    // Both must outlive the scene.
    CollisionScene(const GameLevel& level, const World& world) : m_level(level), m_world(world) {}

    TraceResult traceBox(Vec3 start, Vec3 end, Vec3 halfExtents) const;
    // See CollisionWorld::overlapsBox.
    bool overlapsBox(Vec3 centre, Vec3 halfExtents, float inset = 0.0f) const;

    // Calls fn(id, model, offset) for each mover in the state that has a model in the level.
    template <typename Fn>
    void forEachMover(Fn&& fn) const;

private:
    const CollisionWorld* moverModel(EntityId id) const;

    const GameLevel& m_level;
    const World& m_world;
};

template <typename Fn>
void CollisionScene::forEachMover(Fn&& fn) const {
    const std::span<const Mover> movers = m_world.movers.components();
    for (std::size_t i = 0; i < movers.size(); ++i) {
        const EntityId id = m_world.movers.entities()[i];
        if (const CollisionWorld* model = moverModel(id)) {
            fn(id, *model, movers[i].offset);
        }
    }
}

} // namespace ct
