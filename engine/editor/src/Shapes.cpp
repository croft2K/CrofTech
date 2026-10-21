#include "croftech/editor/Shapes.h"

#include "croftech/level/BrushGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <string>

namespace ct {
namespace {

// Corners this close to a side of the box are put exactly on it.
constexpr float kSnapToBox = 1e-4f;

float component(Vec3 v, std::size_t axis) { return axis == 0 ? v.x : (axis == 1 ? v.y : v.z); }

void setComponent(Vec3& v, std::size_t axis, float value) {
    (axis == 0 ? v.x : (axis == 1 ? v.y : v.z)) = value;
}

// The horizontal axis forward runs along (0 = X, 1 = Y) and whether it points the positive way.
struct Facing {
    std::size_t axis = 0;
    bool positive = true;
};

Facing facingOf(Vec3 forward) {
    if (std::fabs(forward.y) > std::fabs(forward.x)) {
        return {1, forward.y > 0.0f};
    }
    return {0, !(forward.x < 0.0f)};
}

// Turns a point of the unit square (x, y in [0, 1]) so that +X, the way the unit shapes face,
// becomes the facing direction. Quarter turns about the square's centre are exact.
Vec3 faceUnitPoint(Vec3 p, Facing facing) {
    if (facing.axis == 0) {
        return facing.positive ? p : Vec3{1.0f - p.x, 1.0f - p.y, p.z};
    }
    return facing.positive ? Vec3{1.0f - p.y, p.x, p.z} : Vec3{p.y, 1.0f - p.x, p.z};
}

void snapToBox(Vec3& p, const Aabb& box) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
        for (const float side : {component(box.min, axis), component(box.max, axis)}) {
            if (std::fabs(component(p, axis) - side) < kSnapToBox) {
                setComponent(p, axis, side);
            }
        }
    }
}

// Stretches the corners from their own bounds onto the box.
void fitToBox(std::vector<Vec3>& corners, const Aabb& box) {
    Vec3 low = corners.front();
    Vec3 high = low;
    for (const Vec3& v : corners) {
        low = min(low, v);
        high = max(high, v);
    }
    for (Vec3& v : corners) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const float span = component(high, axis) - component(low, axis);
            const float t = span > 0.0f ? (component(v, axis) - component(low, axis)) / span : 0.0f;
            const float from = component(box.min, axis);
            const float to = component(box.max, axis);
            setComponent(v, axis, t == 1.0f ? to : from + t * (to - from));
        }
        snapToBox(v, box);
    }
}

// Corners of a regular polygon around the origin, radius 1, at height z. Turned by half a side so
// that with a multiple of 4 sides, flat faces (not corners) face the axes.
void addRing(std::vector<Vec3>& vertices, int sides, double radius, double z) {
    for (int i = 0; i < sides; ++i) {
        const double angle = std::numbers::pi * (2.0 * i + 1.0) / sides;
        vertices.push_back({static_cast<float>(radius * std::cos(angle)),
                            static_cast<float>(radius * std::sin(angle)), static_cast<float>(z)});
    }
}

std::vector<Vec3> wedge(Facing facing) {
    std::vector<Vec3> corners;
    // Rises from the low edge at x = 0 to the top of the back wall at x = 1.
    for (const Vec3& p : {Vec3{0.0f, 0.0f, 0.0f}, Vec3{1.0f, 0.0f, 0.0f}, Vec3{1.0f, 1.0f, 0.0f},
                          Vec3{0.0f, 1.0f, 0.0f}, Vec3{1.0f, 0.0f, 1.0f}, Vec3{1.0f, 1.0f, 1.0f}}) {
        corners.push_back(faceUnitPoint(p, facing));
    }
    return corners;
}

std::vector<Vec3> cylinder(int sides) {
    std::vector<Vec3> corners;
    addRing(corners, sides, 1.0, 0.0);
    addRing(corners, sides, 1.0, 1.0);
    return corners;
}

std::vector<Vec3> cone(int sides) {
    std::vector<Vec3> corners;
    addRing(corners, sides, 1.0, 0.0);
    corners.push_back({0.0f, 0.0f, 1.0f});
    return corners;
}

// Rings of corners between the poles; every face is a triangle at a pole or a flat trapezoid.
std::vector<Vec3> sphere(int sides) {
    const int bands = std::max(2, sides / 2);
    std::vector<Vec3> corners{{0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f}};
    for (int band = 1; band < bands; ++band) {
        const double latitude = std::numbers::pi * (static_cast<double>(band) / bands - 0.5);
        addRing(corners, sides, std::cos(latitude), std::sin(latitude));
    }
    return corners;
}

// A point given by how far along the facing axis, across it and up, in world units.
Vec3 worldPoint(Facing facing, float along, float across, float z) {
    Vec3 p;
    setComponent(p, facing.axis, along);
    setComponent(p, 1 - facing.axis, across);
    p.z = z;
    return p;
}

std::vector<Brush> stairs(const Aabb& box, const ShapeSettings& settings) {
    const Facing facing = facingOf(settings.forward);
    const float alongMin = component(box.min, facing.axis);
    const float alongMax = component(box.max, facing.axis);
    const float length = alongMax - alongMin;
    const float height = box.max.z - box.min.z;
    const int steps = settings.steps;
    std::vector<Brush> brushes;
    // Each step is a slab from its front edge to the back wall, on top of the one below.
    for (int i = 0; i < steps; ++i) {
        const float inset = length * static_cast<float>(i) / static_cast<float>(steps);
        const float bottom = box.min.z + height * static_cast<float>(i) / static_cast<float>(steps);
        const float top = i + 1 == steps ? box.max.z
                                         : box.min.z + height * static_cast<float>(i + 1) /
                                                           static_cast<float>(steps);
        Vec3 low = box.min;
        Vec3 high = box.max;
        low.z = bottom;
        high.z = top;
        if (facing.positive) {
            setComponent(low, facing.axis, alongMin + inset);
        } else {
            setComponent(high, facing.axis, alongMax - inset);
        }
        brushes.push_back(makeBoxBrush(low, high));
    }
    return brushes;
}

std::vector<Brush> arch(const Aabb& box, const ShapeSettings& settings) {
    const Facing facing = facingOf(settings.forward);
    const std::size_t acrossAxis = 1 - facing.axis;
    const float acrossMin = component(box.min, acrossAxis);
    const float acrossMax = component(box.max, acrossAxis);
    const float centre = (acrossMin + acrossMax) * 0.5f;
    const double outerWidth = (acrossMax - acrossMin) * 0.5;
    const double outerHeight = box.max.z - box.min.z;
    const double innerWidth = outerWidth - settings.thickness;
    const double innerHeight = outerHeight - settings.thickness;
    if (!(innerWidth > 0.0) || !(innerHeight > 0.0)) {
        return {};
    }
    const float front = component(box.min, facing.axis);
    const float back = component(box.max, facing.axis);
    const int segments = settings.sides;
    // Half an ellipse standing on the ground, from one side (angle 0) over to the other (pi).
    const auto curve = [&](double width, double height, int i) {
        const double angle = std::numbers::pi * i / segments;
        return std::pair{static_cast<float>(centre + width * std::cos(angle)),
                         static_cast<float>(box.min.z + height * std::sin(angle))};
    };
    std::vector<Brush> brushes;
    for (int i = 0; i < segments; ++i) {
        const std::pair<float, float> corners[4]{
            curve(outerWidth, outerHeight, i), curve(outerWidth, outerHeight, i + 1),
            curve(innerWidth, innerHeight, i + 1), curve(innerWidth, innerHeight, i)};
        std::vector<Vec3> segment;
        for (const float along : {front, back}) {
            for (const auto& [across, z] : corners) {
                Vec3 p = worldPoint(facing, along, across, z);
                snapToBox(p, box);
                segment.push_back(p);
            }
        }
        const std::optional<Brush> brush = brushFromPoints(segment);
        if (!brush) {
            return {};
        }
        brushes.push_back(*brush);
    }
    return brushes;
}

} // namespace

std::string_view shapeName(ShapeKind kind) {
    switch (kind) {
    case ShapeKind::Box:
        return "Box";
    case ShapeKind::Wedge:
        return "Wedge";
    case ShapeKind::Cylinder:
        return "Cylinder";
    case ShapeKind::Cone:
        return "Cone";
    case ShapeKind::Pyramid:
        return "Pyramid";
    case ShapeKind::Sphere:
        return "Sphere";
    case ShapeKind::Arch:
        return "Arch";
    case ShapeKind::Stairs:
        return "Stairs";
    }
    return "Shape";
}

ShapeSettings clampedSettings(ShapeKind kind, const ShapeSettings& settings) {
    ShapeSettings result = settings;
    if (kind == ShapeKind::Sphere) {
        result.sides = std::clamp(result.sides, kMinSphereSides, kMaxSphereSides);
    } else {
        result.sides = std::clamp(result.sides, kMinShapeSides, kMaxShapeSides);
    }
    result.steps = std::clamp(result.steps, 1, kMaxStairSteps);
    const Facing facing = facingOf(settings.forward);
    result.forward = {};
    setComponent(result.forward, facing.axis, facing.positive ? 1.0f : -1.0f);
    return result;
}

std::vector<Brush> makeShape(ShapeKind kind, const Aabb& box, const ShapeSettings& settings) {
    if (!(box.min.x < box.max.x && box.min.y < box.max.y && box.min.z < box.max.z)) {
        return {};
    }
    const ShapeSettings clamped = clampedSettings(kind, settings);
    std::vector<Brush> brushes;
    std::vector<Vec3> shape;
    switch (kind) {
    case ShapeKind::Box:
        brushes.push_back(makeBoxBrush(box.min, box.max));
        break;
    case ShapeKind::Wedge:
        shape = wedge(facingOf(clamped.forward));
        break;
    case ShapeKind::Cylinder:
        shape = cylinder(clamped.sides);
        break;
    case ShapeKind::Cone:
        shape = cone(clamped.sides);
        break;
    case ShapeKind::Pyramid:
        shape = cone(4);
        break;
    case ShapeKind::Sphere:
        shape = sphere(clamped.sides);
        break;
    case ShapeKind::Arch:
        brushes = arch(box, clamped);
        break;
    case ShapeKind::Stairs:
        brushes = stairs(box, clamped);
        break;
    }
    if (!shape.empty()) {
        fitToBox(shape, box);
        if (const std::optional<Brush> brush = brushFromPoints(shape)) {
            brushes.push_back(*brush);
        }
    }
    std::string problem;
    for (const Brush& brush : brushes) {
        if (!checkBrush(brush, problem)) {
            return {};
        }
    }
    return brushes;
}

} // namespace ct
