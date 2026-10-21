#include "croftech/editor/BrushEditing.h"

#include "croftech/level/BrushGeometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace ct {
namespace {

constexpr float kParallelEpsilon = 1e-6f;

} // namespace

float snapToGrid(float value, float grid) { return std::round(value / grid) * grid; }

Vec3 snapToGrid(Vec3 value, float grid) {
    return {snapToGrid(value.x, grid), snapToGrid(value.y, grid), snapToGrid(value.z, grid)};
}

Brush translatedBrush(const Brush& brush, Vec3 offset) {
    Brush moved = brush;
    for (Vec3& vertex : moved.vertices) {
        vertex += offset;
    }
    return moved;
}

std::optional<Brush> withFacePushed(const Brush& brush, std::size_t face, float distance,
                                    std::size_t* movedFace) {
    if (face >= brush.faces.size()) {
        return std::nullopt;
    }
    std::vector<DPlane> planes;
    for (std::size_t i = 0; i < brush.faces.size(); ++i) {
        planes.push_back(exactFacePlane(brush, i));
    }
    // A unit normal, so the distance is in metres; an axis-aligned normal stays exact.
    const DPlane& before = planes[face];
    planes[face] = {before.normal * (1.0 / length(before.normal)), distance};
    std::vector<std::size_t> planeOfFace;
    std::optional<Brush> result = brushFromPlanes(planes, brush.vertices, &planeOfFace);
    if (!result) {
        return std::nullopt;
    }
    const auto found = std::find(planeOfFace.begin(), planeOfFace.end(), face);
    if (found == planeOfFace.end()) {
        return std::nullopt;
    }
    if (movedFace != nullptr) {
        *movedFace = static_cast<std::size_t>(found - planeOfFace.begin());
    }
    return result;
}

bool isAxial(Vec3 normal) {
    const float ax = std::fabs(normal.x);
    const float ay = std::fabs(normal.y);
    const float az = std::fabs(normal.z);
    return (ax == 1.0f && ay == 0.0f && az == 0.0f) || (ax == 0.0f && ay == 1.0f && az == 0.0f) ||
           (ax == 0.0f && ay == 0.0f && az == 1.0f);
}

std::optional<Aabb> boxBounds(const Brush& brush) {
    if (brush.faces.size() != 6 || brush.vertices.size() != 8) {
        return std::nullopt;
    }
    // One face per axis direction: +X, -X, +Y, -Y, +Z, -Z.
    std::array<bool, 6> sides{};
    for (std::size_t i = 0; i < brush.faces.size(); ++i) {
        const Vec3 n = facePlane(brush, i).normal;
        if (!isAxial(n)) {
            return std::nullopt;
        }
        const std::size_t axis = n.x != 0.0f ? 0 : (n.y != 0.0f ? 1 : 2);
        const bool positive = n.x + n.y + n.z > 0.0f;
        const std::size_t slot = axis * 2 + (positive ? 0 : 1);
        if (sides[slot]) {
            return std::nullopt;
        }
        sides[slot] = true;
    }
    Aabb box;
    computeBrushBounds(brush, box);
    if (!(box.min.x < box.max.x && box.min.y < box.max.y && box.min.z < box.max.z)) {
        return std::nullopt;
    }
    return box;
}

std::optional<BrushHit> rayCastBrush(const Brush& brush, Vec3 origin, Vec3 direction) {
    float enter = -std::numeric_limits<float>::infinity();
    float exit = std::numeric_limits<float>::infinity();
    std::size_t enterPlane = 0;
    const std::vector<Plane> planes = brushPlanes(brush);
    for (std::size_t i = 0; i < planes.size(); ++i) {
        const Plane& plane = planes[i];
        const float denominator = dot(plane.normal, direction);
        const float outside = dot(plane.normal, origin) - plane.distance;
        if (std::fabs(denominator) < kParallelEpsilon) {
            if (outside > 0.0f) {
                return std::nullopt;
            }
            continue;
        }
        const float t = -outside / denominator;
        if (denominator < 0.0f) {
            if (t > enter) {
                enter = t;
                enterPlane = i;
            }
        } else {
            exit = std::min(exit, t);
        }
    }
    if (enter < 0.0f || enter > exit) {
        return std::nullopt;
    }
    return BrushHit{enter, enterPlane};
}

std::optional<float> rayCastBox(const Aabb& box, Vec3 origin, Vec3 direction) {
    const std::optional<BrushHit> hit =
        rayCastBrush(makeBoxBrush(box.min, box.max), origin, direction);
    return hit ? std::optional<float>(hit->distance) : std::nullopt;
}

std::optional<float> rayCastPlane(const Plane& plane, Vec3 origin, Vec3 direction) {
    const float denominator = dot(plane.normal, direction);
    if (std::fabs(denominator) < kParallelEpsilon) {
        return std::nullopt;
    }
    const float t = (plane.distance - dot(plane.normal, origin)) / denominator;
    return t >= 0.0f ? std::optional<float>(t) : std::nullopt;
}

std::optional<float> closestAlongAxis(Vec3 axisOrigin, Vec3 axis, Vec3 rayOrigin,
                                      Vec3 rayDirection) {
    // Closest points of two lines: minimise |axisOrigin + s*axis - (rayOrigin + t*rayDirection)|.
    const Vec3 w = axisOrigin - rayOrigin;
    const float b = dot(axis, rayDirection);
    const float denominator = 1.0f - b * b; // both directions are unit length
    if (denominator < kParallelEpsilon) {
        return std::nullopt;
    }
    const float d = dot(axis, w);
    const float e = dot(rayDirection, w);
    return (b * e - d) / denominator;
}

} // namespace ct
