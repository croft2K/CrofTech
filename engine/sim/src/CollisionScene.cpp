#include "croftech/sim/CollisionScene.h"

#include "croftech/core/Profile.h"
#include "croftech/sim/Enemies.h"
#include "croftech/sim/GameLevel.h"

#include <algorithm>
#include <cmath>

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
    if (m_settings != nullptr && !result.allSolid) {
        traceBodies(start, end, halfExtents, result);
    }
    result.endPosition = start + (end - start) * result.fraction;
    return result;
}

void CollisionScene::traceBodies(Vec3 start, Vec3 end, Vec3 halfExtents,
                                 TraceResult& result) const {
    const Vec3 delta = end - start;
    const float distance = length(delta);
    const std::span<const EntityId> bodies = m_world.playerMotions.entities();
    for (const EntityId body : bodies) {
        const Transform* transform = m_world.transforms.get(body);
        const Health* health = m_world.healths.get(body);
        if (body == m_self || transform == nullptr || (health != nullptr && isDead(*health))) {
            continue;
        }
        // The other box grown by this one: the moving box's centre may not enter it.
        const Vec3 reach = movementOf(m_world, body, *m_settings).halfExtents + halfExtents;
        const Vec3 low = transform->position - reach;
        const Vec3 high = transform->position + reach;
        const float startAt[3] = {start.x, start.y, start.z};
        const float move[3] = {delta.x, delta.y, delta.z};
        const float lows[3] = {low.x, low.y, low.z};
        const float highs[3] = {high.x, high.y, high.z};
        bool inside = true;
        float enter = -1.0f;
        float leave = 1.0f;
        int axis = -1;
        bool missed = false;
        for (int i = 0; i < 3 && !missed; ++i) {
            if (startAt[i] <= lows[i] || startAt[i] >= highs[i]) {
                inside = false;
            }
            if (move[i] == 0.0f) {
                missed = startAt[i] <= lows[i] || startAt[i] >= highs[i];
                continue;
            }
            float t0 = (lows[i] - startAt[i]) / move[i];
            float t1 = (highs[i] - startAt[i]) / move[i];
            if (t0 > t1) {
                std::swap(t0, t1);
            }
            if (t0 > enter) {
                enter = t0;
                axis = i;
            }
            leave = std::min(leave, t1);
            missed = enter >= leave;
        }
        if (inside || missed || axis < 0 || enter < 0.0f || enter >= result.fraction) {
            continue;
        }
        // Stopped short of the box, as traces stop short of brushes.
        const float backOff = distance > 0.0f ? CollisionWorld::kSurfaceEpsilon / distance : 0.0f;
        result.fraction = std::max(enter - backOff, 0.0f);
        Vec3 normal;
        const float side = move[axis] > 0.0f ? -1.0f : 1.0f;
        (axis == 0 ? normal.x : axis == 1 ? normal.y : normal.z) = side;
        result.normal = normal;
        result.entity = body;
    }
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
