#include "croftech/level/BrushGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace ct {
namespace {

constexpr float kOnPlaneEpsilon = 1e-3f;
constexpr float kWeldEpsilon = 1e-3f;
constexpr float kParallelEpsilon = 1e-6f;

bool intersectPlanes(const Plane& a, const Plane& b, const Plane& c, Vec3& point) {
    const Vec3 bc = cross(b.normal, c.normal);
    const float det = dot(a.normal, bc);
    if (std::fabs(det) < kParallelEpsilon) {
        return false;
    }
    point = (a.distance * bc + b.distance * cross(c.normal, a.normal) +
             c.distance * cross(a.normal, b.normal)) /
            det;
    return true;
}

bool insideAllPlanes(const Brush& brush, Vec3 point) {
    return std::all_of(brush.planes.begin(), brush.planes.end(), [&](const Plane& plane) {
        return dot(plane.normal, point) - plane.distance <= kOnPlaneEpsilon;
    });
}

void addUnique(std::vector<Vec3>& points, Vec3 point) {
    for (const Vec3& existing : points) {
        if (length(existing - point) < kWeldEpsilon) {
            return;
        }
    }
    points.push_back(point);
}

void sortCounterClockwise(std::vector<Vec3>& points, Vec3 normal) {
    Vec3 centre;
    for (const Vec3& p : points) {
        centre += p;
    }
    centre = centre / static_cast<float>(points.size());

    // Basis (u, v) in the plane with cross(u, v) == normal, so increasing angle is CCW from
    // outside.
    const Vec3 helper =
        std::fabs(normal.z) < 0.9f ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{1.0f, 0.0f, 0.0f};
    const Vec3 u = normalize(cross(helper, normal));
    const Vec3 v = cross(normal, u);
    std::sort(points.begin(), points.end(), [&](Vec3 a, Vec3 b) {
        const Vec3 da = a - centre;
        const Vec3 db = b - centre;
        return std::atan2(dot(da, v), dot(da, u)) < std::atan2(dot(db, v), dot(db, u));
    });
}

} // namespace

std::vector<BrushFace> buildBrushFaces(const Brush& brush) {
    const std::size_t planeCount = brush.planes.size();
    std::vector<std::vector<Vec3>> facePoints(planeCount);

    for (std::size_t i = 0; i < planeCount; ++i) {
        for (std::size_t j = i + 1; j < planeCount; ++j) {
            for (std::size_t k = j + 1; k < planeCount; ++k) {
                Vec3 point;
                if (!intersectPlanes(brush.planes[i], brush.planes[j], brush.planes[k], point) ||
                    !insideAllPlanes(brush, point)) {
                    continue;
                }
                addUnique(facePoints[i], point);
                addUnique(facePoints[j], point);
                addUnique(facePoints[k], point);
            }
        }
    }

    std::vector<BrushFace> faces;
    for (std::size_t i = 0; i < planeCount; ++i) {
        if (facePoints[i].size() < 3) {
            continue;
        }
        sortCounterClockwise(facePoints[i], brush.planes[i].normal);
        faces.push_back({brush.planes[i], std::move(facePoints[i])});
    }
    return faces;
}

bool computeBrushBounds(const Brush& brush, Aabb& bounds) {
    const std::vector<BrushFace> faces = buildBrushFaces(brush);
    if (faces.empty()) {
        return false;
    }
    bounds = {faces.front().vertices.front(), faces.front().vertices.front()};
    for (const BrushFace& face : faces) {
        for (const Vec3& vertex : face.vertices) {
            bounds.min = min(bounds.min, vertex);
            bounds.max = max(bounds.max, vertex);
        }
    }
    return true;
}

} // namespace ct
