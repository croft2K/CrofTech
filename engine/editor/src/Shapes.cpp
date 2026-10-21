#include "croftech/editor/Shapes.h"

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

// Stretches the polyhedron's corners from their own bounds onto the box.
void fitToBox(Polyhedron& shape, const Aabb& box) {
    Vec3 low = shape.vertices.front();
    Vec3 high = low;
    for (const Vec3& v : shape.vertices) {
        low = min(low, v);
        high = max(high, v);
    }
    for (Vec3& v : shape.vertices) {
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

int next(int i, int sides) { return (i + 1) % sides; }

Polyhedron wedge(Facing facing) {
    Polyhedron shape;
    // Rises from the low edge at x = 0 to the top of the back wall at x = 1.
    for (const Vec3& p : {Vec3{0.0f, 0.0f, 0.0f}, Vec3{1.0f, 0.0f, 0.0f}, Vec3{1.0f, 1.0f, 0.0f},
                          Vec3{0.0f, 1.0f, 0.0f}, Vec3{1.0f, 0.0f, 1.0f}, Vec3{1.0f, 1.0f, 1.0f}}) {
        shape.vertices.push_back(faceUnitPoint(p, facing));
    }
    shape.faces = {{0, 1, 2, 3}, {1, 2, 5, 4}, {0, 3, 5, 4}, {0, 1, 4}, {3, 2, 5}};
    return shape;
}

Polyhedron cylinder(int sides) {
    Polyhedron shape;
    addRing(shape.vertices, sides, 1.0, 0.0);
    addRing(shape.vertices, sides, 1.0, 1.0);
    std::vector<int> bottom;
    std::vector<int> top;
    for (int i = 0; i < sides; ++i) {
        bottom.push_back(i);
        top.push_back(sides + i);
        shape.faces.push_back({i, next(i, sides), sides + next(i, sides), sides + i});
    }
    shape.faces.push_back(bottom);
    shape.faces.push_back(top);
    return shape;
}

Polyhedron cone(int sides) {
    Polyhedron shape;
    addRing(shape.vertices, sides, 1.0, 0.0);
    shape.vertices.push_back({0.0f, 0.0f, 1.0f});
    std::vector<int> base;
    for (int i = 0; i < sides; ++i) {
        base.push_back(i);
        shape.faces.push_back({i, next(i, sides), sides});
    }
    shape.faces.push_back(base);
    return shape;
}

// Rings of corners between the poles; every face is a triangle at a pole or a flat trapezoid.
Polyhedron sphere(int sides) {
    const int bands = std::max(2, sides / 2);
    Polyhedron shape;
    shape.vertices.push_back({0.0f, 0.0f, -1.0f});
    for (int band = 1; band < bands; ++band) {
        const double latitude = std::numbers::pi * (static_cast<double>(band) / bands - 0.5);
        addRing(shape.vertices, sides, std::cos(latitude), std::sin(latitude));
    }
    const int north = static_cast<int>(shape.vertices.size());
    shape.vertices.push_back({0.0f, 0.0f, 1.0f});
    const auto ring = [sides](int band, int i) { return 1 + (band - 1) * sides + i; };
    for (int i = 0; i < sides; ++i) {
        shape.faces.push_back({0, ring(1, i), ring(1, next(i, sides))});
        for (int band = 1; band + 1 < bands; ++band) {
            shape.faces.push_back({ring(band, i), ring(band, next(i, sides)),
                                   ring(band + 1, next(i, sides)), ring(band + 1, i)});
        }
        shape.faces.push_back({north, ring(bands - 1, i), ring(bands - 1, next(i, sides))});
    }
    return shape;
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
        Polyhedron segment;
        for (const float along : {front, back}) {
            for (const auto& [across, z] : corners) {
                Vec3 p = worldPoint(facing, along, across, z);
                snapToBox(p, box);
                segment.vertices.push_back(p);
            }
        }
        segment.faces = {{0, 1, 2, 3}, {4, 5, 6, 7}, {0, 1, 5, 4},
                         {3, 2, 6, 7}, {0, 3, 7, 4}, {1, 2, 6, 5}};
        const std::optional<Brush> brush = brushFromPolyhedron(segment);
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
    Polyhedron shape;
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
    if (!shape.vertices.empty()) {
        fitToBox(shape, box);
        if (const std::optional<Brush> brush = brushFromPolyhedron(shape)) {
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

std::optional<Brush> brushFromPolyhedron(const Polyhedron& polyhedron) {
    if (polyhedron.vertices.empty()) {
        return std::nullopt;
    }
    Vec3 centre;
    for (const Vec3& v : polyhedron.vertices) {
        centre += v;
    }
    centre = centre / static_cast<float>(polyhedron.vertices.size());

    const auto vertex = [&](int index) -> const Vec3* {
        return index >= 0 && static_cast<std::size_t>(index) < polyhedron.vertices.size()
                   ? &polyhedron.vertices[static_cast<std::size_t>(index)]
                   : nullptr;
    };
    Brush brush;
    for (const std::vector<int>& face : polyhedron.faces) {
        if (face.size() < 3) {
            return std::nullopt;
        }
        // The widest triangle on the face gives the most accurate normal; exact for faces whose
        // corners share a coordinate.
        const Vec3* origin = vertex(face[0]);
        if (origin == nullptr) {
            return std::nullopt;
        }
        Vec3 best;
        float bestArea = 0.0f;
        for (std::size_t j = 1; j < face.size(); ++j) {
            for (std::size_t k = j + 1; k < face.size(); ++k) {
                const Vec3* a = vertex(face[j]);
                const Vec3* b = vertex(face[k]);
                if (a == nullptr || b == nullptr) {
                    return std::nullopt;
                }
                const Vec3 n = cross(*a - *origin, *b - *origin);
                const float area = dot(n, n);
                if (area > bestArea) {
                    best = n;
                    bestArea = area;
                }
            }
        }
        if (!(bestArea > 0.0f)) {
            return std::nullopt;
        }
        Plane plane{normalize(best), 0.0f};
        plane.distance = dot(plane.normal, *origin);
        if (dot(plane.normal, centre) > plane.distance) {
            plane = {-plane.normal, -plane.distance};
        }
        // Adding zero turns -0 into 0 and changes nothing else; -0 reads badly in the inspector.
        plane.normal += Vec3{};
        plane.distance += 0.0f;
        brush.planes.push_back(plane);
    }
    std::string problem;
    if (!checkBrush(brush, problem)) {
        return std::nullopt;
    }
    return brush;
}

} // namespace ct
