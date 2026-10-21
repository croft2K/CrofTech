#include "croftech/editor/BrushOperations.h"

#include "croftech/core/DVec3.h"

#include <cmath>

namespace ct {
namespace {

// The plane with a unit normal, divided component by component so an axis-aligned normal comes
// out exactly 1 and 0s.
DPlane unitPlane(const DPlane& plane) {
    const double size = length(plane.normal);
    return {{plane.normal.x / size, plane.normal.y / size, plane.normal.z / size},
            plane.distance / size};
}

} // namespace

std::optional<BrushSplit> splitBrush(const Brush& brush, const DPlane& plane) {
    bool anyBack = false;
    bool anyFront = false;
    for (const Vec3& corner : brush.vertices) {
        const double distance = plane.distanceTo(toDouble(corner));
        anyBack = anyBack || distance < -kOnPlaneDistance;
        anyFront = anyFront || distance > kOnPlaneDistance;
    }
    BrushSplit split;
    if (anyBack) {
        split.back = anyFront ? clipBrush(brush, plane) : brush;
        if (!split.back) {
            return std::nullopt;
        }
    }
    if (anyFront) {
        split.front = anyBack ? clipBrush(brush, plane.flipped()) : brush;
        if (!split.front) {
            return std::nullopt;
        }
    }
    return split;
}

std::optional<std::vector<Brush>> subtractBrush(const Brush& brush, const Brush& cutter) {
    Aabb brushBounds;
    Aabb cutterBounds;
    if (!computeBrushBounds(brush, brushBounds) || !computeBrushBounds(cutter, cutterBounds) ||
        !overlaps(brushBounds, cutterBounds)) {
        return std::vector<Brush>{brush};
    }
    // Peel off the part in front of each of the cutter's faces; what is left is inside it.
    std::vector<Brush> pieces;
    Brush rest = brush;
    for (std::size_t face = 0; face < cutter.faces.size(); ++face) {
        const std::optional<BrushSplit> split = splitBrush(rest, exactFacePlane(cutter, face));
        if (!split) {
            return std::nullopt;
        }
        if (!split->back) {
            return std::vector<Brush>{brush}; // wholly in front of one face: no overlap
        }
        if (split->front) {
            pieces.push_back(*split->front);
        }
        rest = *split->back;
    }
    return pieces;
}

std::optional<std::vector<Brush>> hollowBrush(const Brush& brush, float thickness) {
    if (!(thickness > 0.0f)) {
        return std::nullopt;
    }
    std::vector<DPlane> inner;
    for (std::size_t face = 0; face < brush.faces.size(); ++face) {
        DPlane plane = unitPlane(exactFacePlane(brush, face));
        plane.distance -= thickness;
        inner.push_back(plane);
    }
    const std::optional<Brush> inside = brushFromPlanes(inner);
    if (!inside) {
        return std::nullopt;
    }
    return subtractBrush(brush, *inside);
}

std::optional<Brush> mergeBrushes(std::span<const Brush> brushes) {
    std::vector<Vec3> corners;
    for (const Brush& brush : brushes) {
        corners.insert(corners.end(), brush.vertices.begin(), brush.vertices.end());
    }
    return brushFromPoints(corners);
}

std::optional<Brush> extrudeFace(const Brush& brush, std::size_t face, float distance) {
    if (face >= brush.faces.size() || !(distance > 0.0f)) {
        return std::nullopt;
    }
    const Vec3 offset = facePlane(brush, face).normal * distance;
    std::vector<Vec3> corners = faceVertices(brush, face);
    const std::size_t count = corners.size();
    for (std::size_t i = 0; i < count; ++i) {
        corners.push_back(corners[i] + offset);
    }
    return brushFromPoints(corners);
}

} // namespace ct
