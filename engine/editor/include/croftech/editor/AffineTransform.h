#pragma once

#include "croftech/core/DVec3.h"
#include "croftech/core/Math.h"
#include "croftech/level/Level.h"

#include <cstddef>
#include <optional>

namespace ct {

// p' = xAxis * p.x + yAxis * p.y + zAxis * p.z + translation: what the move, rotate, scale and flip
// tools do to brushes and entities.
//
// Held in double precision and rounded to float once, when a result is stored, so scaling a box
// onto grid lines lands on them exactly. Quarter turns, flips and moves keep grid positions exact:
// a box turned four times is bit-identical to where it started.
struct AffineTransform {
    DVec3 xAxis{1.0, 0.0, 0.0}; // where the linear part sends each axis
    DVec3 yAxis{0.0, 1.0, 0.0};
    DVec3 zAxis{0.0, 0.0, 1.0};
    DVec3 translation;

    static AffineTransform moveBy(Vec3 offset);
    // Right-handed rotation about a world axis (0 = X, 1 = Y, 2 = Z) through pivot. Multiples of
    // 90 degrees are exact.
    static AffineTransform rotation(std::size_t axis, double degrees, Vec3 pivot);
    // Per-axis scale about pivot; a negative factor mirrors along that axis.
    static AffineTransform scale(DVec3 factors, Vec3 pivot);
    // Maps one box onto another: what dragging a side of the selection's bounds does.
    static AffineTransform boxToBox(const Aabb& from, const Aabb& to);

    Vec3 apply(Vec3 p) const;
    double determinant() const;
    // This transform followed by next.
    AffineTransform then(const AffineTransform& next) const;
};

// The brush with every corner transformed; a mirror also reverses each face's winding, so faces
// still run counter-clockwise from outside. Nullopt if the transform flattens space (a zero scale)
// or the result is not a valid brush (checkBrush), for example outside the world.
std::optional<Brush> transformedBrush(const Brush& brush, const AffineTransform& transform);

// Moves the entity's origin and turns its "yaw" (degrees, as player spawns store it) with the
// transform. Other properties are unchanged, as is an entity without an origin.
LevelEntity transformedEntity(const LevelEntity& entity, const AffineTransform& transform);

// The multiple of step nearest to degrees.
double snapAngle(double degrees, double step);

} // namespace ct
