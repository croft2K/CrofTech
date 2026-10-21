#include "croftech/sim/CollisionScene.h"

#include "croftech/core/Profile.h"
#include "croftech/sim/GameLevel.h"

namespace ct {

const CollisionWorld* CollisionScene::moverModel(EntityId id) const {
    const LevelEntityRef* ref = m_world.levelEntities.get(id);
    if (ref == nullptr) {
        return nullptr;
    }
    const GameLevel::Entity* entity = m_level.entity(ref->index);
    return entity != nullptr && entity->logicClass == LogicClass::Mover ? entity->model : nullptr;
}

TraceResult CollisionScene::traceBox(Vec3 start, Vec3 end, Vec3 halfExtents) const {
    CT_PROFILE_ZONE();
    TraceResult result = m_level.collision().traceBox(start, end, halfExtents);
    // Each mover is traced in its own frame, where its brushes are as built.
    forEachMover([&](EntityId id, const CollisionWorld& model, Vec3 offset) {
        if (result.allSolid) {
            return;
        }
        const TraceResult trace = model.traceBox(start - offset, end - offset, halfExtents);
        result.startSolid = result.startSolid || trace.startSolid;
        if (trace.allSolid) {
            result.allSolid = true;
            result.fraction = 0.0f;
            result.entity = id;
        } else if (trace.fraction < result.fraction) {
            result.fraction = trace.fraction;
            result.normal = trace.normal;
            result.entity = id;
        }
    });
    result.endPosition = start + (end - start) * result.fraction;
    return result;
}

bool CollisionScene::overlapsBox(Vec3 centre, Vec3 halfExtents, float inset) const {
    if (m_level.collision().overlapsBox(centre, halfExtents, inset)) {
        return true;
    }
    bool overlapping = false;
    forEachMover([&](EntityId, const CollisionWorld& model, Vec3 offset) {
        overlapping = overlapping || model.overlapsBox(centre - offset, halfExtents, inset);
    });
    return overlapping;
}

} // namespace ct
