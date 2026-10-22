#pragma once

#include "croftech/core/Math.h"
#include "croftech/level/Level.h"
#include "croftech/sim/EntityId.h"

#include <cstdint>
#include <span>
#include <vector>

namespace ct {

struct TraceResult {
    float fraction = 1.0f; // portion of the move completed before hitting something
    Vec3 endPosition;
    Vec3 normal;             // surface normal at the hit; zero if nothing was hit
    bool startSolid = false; // the box started inside a brush
    bool allSolid = false;   // the box never left solid
    EntityId entity;         // the mover hit (CollisionScene); invalid for the world
};

// Immutable level collision. Built once from the level's brushes; never part of the per-tick state.
class CollisionWorld {
public:
    // Traces stop this far short of surfaces so the next trace never starts inside one.
    static constexpr float kSurfaceEpsilon = 1.0f / 1024.0f;

    explicit CollisionWorld(std::span<const Brush> brushes);

    // Sweeps an axis-aligned box (centre, half extents) from start to end.
    TraceResult traceBox(Vec3 start, Vec3 end, Vec3 halfExtents) const;
    // Whether the box (centre, half extents) is inside or touching any brush. With an inset it
    // must be inside by more than that: touching, or overlapping by rounding, doesn't count.
    bool overlapsBox(Vec3 centre, Vec3 halfExtents, float inset = 0.0f) const;
    // The box around every brush; empty (zero size at the origin) if there are none.
    Aabb bounds() const;

private:
    struct CollisionBrush {
        std::uint32_t firstPlane = 0;
        std::uint32_t planeCount = 0;
        Aabb bounds;
    };

    void clipToBrush(const CollisionBrush& brush, Vec3 start, Vec3 end, Vec3 halfExtents,
                     TraceResult& result) const;

    std::vector<Plane> m_planes;
    std::vector<CollisionBrush> m_brushes;
};

} // namespace ct
