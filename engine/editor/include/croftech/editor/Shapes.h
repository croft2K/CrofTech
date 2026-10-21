#pragma once

#include "croftech/core/Math.h"
#include "croftech/level/Level.h"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace ct {

// The shapes the editor draws. Each fills a box: the box tool's rectangle and height.
enum class ShapeKind : std::uint8_t { Box, Wedge, Cylinder, Cone, Pyramid, Sphere, Arch, Stairs };

inline constexpr ShapeKind kShapeKinds[]{ShapeKind::Box,  ShapeKind::Wedge,   ShapeKind::Cylinder,
                                         ShapeKind::Cone, ShapeKind::Pyramid, ShapeKind::Sphere,
                                         ShapeKind::Arch, ShapeKind::Stairs};

std::string_view shapeName(ShapeKind kind);

// Limits that keep every part within kMaxBrushPlanes. A sphere has sides * sides / 2 faces.
inline constexpr int kMinShapeSides = 3;
inline constexpr int kMaxShapeSides = 32;
inline constexpr int kMinSphereSides = 4;
inline constexpr int kMaxSphereSides = 10;
inline constexpr int kMaxStairSteps = 64;

struct ShapeSettings {
    int sides = 8;          // Cylinder, Cone and Sphere around; Arch segments
    int steps = 4;          // Stairs
    float thickness = 0.5f; // Arch: from the outer curve to the inner one, metres
    // The horizontal axis direction (+-X or +-Y) a wedge rises and stairs climb towards, and an
    // arch is walked through along.
    Vec3 forward{1.0f, 0.0f, 0.0f};

    friend bool operator==(const ShapeSettings&, const ShapeSettings&) = default;
};

// The settings with every value inside its limits for the kind.
ShapeSettings clampedSettings(ShapeKind kind, const ShapeSettings& settings);

// The brushes of a shape filling the box: one for most shapes, one per segment of an arch and
// one per step of stairs. Corners that fall on the box's sides land on them exactly, so an 8-sided
// cylinder's flat faces sit on the grid. Empty if the box is too thin for the shape or a part
// would not be a valid brush.
std::vector<Brush> makeShape(ShapeKind kind, const Aabb& box, const ShapeSettings& settings);

// A convex solid given by its corners and, for each face, the corners on it (in any order). The
// planes come out facing away from the centre of the corners.
struct Polyhedron {
    std::vector<Vec3> vertices;
    std::vector<std::vector<int>> faces;
};

std::optional<Brush> brushFromPolyhedron(const Polyhedron& polyhedron);

} // namespace ct
