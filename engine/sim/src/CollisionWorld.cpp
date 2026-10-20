#include "croftech/sim/CollisionWorld.h"

#include "croftech/core/Profile.h"
#include "croftech/level/BrushGeometry.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ct {
namespace {

constexpr float kAxialEpsilon = 1e-5f;

bool hasPlaneWithNormal(std::span<const Plane> planes, Vec3 normal) {
    return std::any_of(planes.begin(), planes.end(), [&](const Plane& plane) {
        return dot(plane.normal, normal) > 1.0f - kAxialEpsilon;
    });
}

} // namespace

CollisionWorld::CollisionWorld(std::span<const Brush> brushes) {
    for (const Brush& brush : brushes) {
        Aabb bounds;
        if (!computeBrushBounds(brush, bounds)) {
            continue;
        }
        CollisionBrush collision;
        collision.firstPlane = static_cast<std::uint32_t>(m_planes.size());
        collision.bounds = bounds;
        m_planes.insert(m_planes.end(), brush.planes.begin(), brush.planes.end());

        // Expanding a sloped brush by the box only moves its planes outwards, which leaves the
        // expanded shape too large past sharp edges. Axis-aligned bevel planes at the brush bounds
        // trim it back, so boxes don't catch on the corners of ramps.
        const std::array<Plane, 6> bevels{{
            {{1.0f, 0.0f, 0.0f}, bounds.max.x},
            {{-1.0f, 0.0f, 0.0f}, -bounds.min.x},
            {{0.0f, 1.0f, 0.0f}, bounds.max.y},
            {{0.0f, -1.0f, 0.0f}, -bounds.min.y},
            {{0.0f, 0.0f, 1.0f}, bounds.max.z},
            {{0.0f, 0.0f, -1.0f}, -bounds.min.z},
        }};
        for (const Plane& bevel : bevels) {
            if (!hasPlaneWithNormal(brush.planes, bevel.normal)) {
                m_planes.push_back(bevel);
            }
        }
        collision.planeCount = static_cast<std::uint32_t>(m_planes.size()) - collision.firstPlane;
        m_brushes.push_back(collision);
    }
}

TraceResult CollisionWorld::traceBox(Vec3 start, Vec3 end, Vec3 halfExtents) const {
    CT_PROFILE_ZONE();
    TraceResult result;
    const Aabb sweep{min(start, end) - halfExtents, max(start, end) + halfExtents};
    for (const CollisionBrush& brush : m_brushes) {
        if (overlaps(sweep, brush.bounds)) {
            clipToBrush(brush, start, end, halfExtents, result);
            if (result.allSolid) {
                break;
            }
        }
    }
    result.endPosition = start + (end - start) * result.fraction;
    return result;
}

// Clips the segment against the brush grown by the box: each plane is pushed out by the box's
// extent along its normal. The segment enters the solid at the latest entering plane and leaves at
// the earliest leaving plane; it hits the brush only if it enters before it leaves.
void CollisionWorld::clipToBrush(const CollisionBrush& brush, Vec3 start, Vec3 end,
                                 Vec3 halfExtents, TraceResult& result) const {
    float enterFraction = -1.0f;
    float leaveFraction = 1.0f;
    Vec3 hitNormal;
    bool startsOutside = false;
    bool endsOutside = false;

    for (std::uint32_t i = 0; i < brush.planeCount; ++i) {
        const Plane& plane = m_planes[brush.firstPlane + i];
        const float expand = std::fabs(plane.normal.x) * halfExtents.x +
                             std::fabs(plane.normal.y) * halfExtents.y +
                             std::fabs(plane.normal.z) * halfExtents.z;
        const float distance = plane.distance + expand;
        const float startDistance = dot(plane.normal, start) - distance;
        const float endDistance = dot(plane.normal, end) - distance;

        if (startDistance > 0.0f) {
            startsOutside = true;
        }
        if (endDistance > 0.0f) {
            endsOutside = true;
        }
        // Entirely in front of this plane: the segment misses the brush.
        if (startDistance > 0.0f &&
            (endDistance >= kSurfaceEpsilon || endDistance >= startDistance)) {
            return;
        }
        // Entirely behind this plane: it doesn't limit the segment.
        if (startDistance <= 0.0f && endDistance <= 0.0f) {
            continue;
        }
        if (startDistance > endDistance) {
            // Starting closer than the epsilon gives a negative fraction, which can be below the
            // -1 sentinel when the move is short; clamping keeps such hits from being ignored, so
            // creeping into a wall a little each tick stops at the surface.
            const float fraction =
                std::max((startDistance - kSurfaceEpsilon) / (startDistance - endDistance), 0.0f);
            if (fraction > enterFraction) {
                enterFraction = fraction;
                hitNormal = plane.normal;
            }
        } else {
            const float fraction =
                (startDistance + kSurfaceEpsilon) / (startDistance - endDistance);
            leaveFraction = std::min(leaveFraction, fraction);
        }
    }

    if (!startsOutside) {
        result.startSolid = true;
        if (!endsOutside) {
            result.allSolid = true;
            result.fraction = 0.0f;
        }
        return;
    }
    if (enterFraction < leaveFraction && enterFraction > -1.0f && enterFraction < result.fraction) {
        result.fraction = std::max(enterFraction, 0.0f);
        result.normal = hitNormal;
    }
}

} // namespace ct
