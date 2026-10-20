#pragma once

#include "croftech/core/Math.h"
#include "croftech/level/Level.h"

#include <vector>

namespace ct {

struct BrushFace {
    Plane plane;
    // Convex polygon, counter-clockwise when viewed from outside the brush.
    std::vector<Vec3> vertices;
};

// Builds the visible faces of a brush. Planes that do not touch the solid (redundant or degenerate)
// produce no face. Used for rendering, bounds and collision bevels.
std::vector<BrushFace> buildBrushFaces(const Brush& brush);

// Returns false if the brush encloses no volume.
bool computeBrushBounds(const Brush& brush, Aabb& bounds);

} // namespace ct
