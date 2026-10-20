#pragma once

#include "croftech/core/Math.h"

#include <cstdint>
#include <vector>

namespace ct {

// World units are metres, Z up, right-handed. Yaw 0 faces +X and increases towards +Y.

// The set of points p with dot(normal, p) == distance. Normals point out of the solid.
struct Plane {
    Vec3 normal;
    float distance = 0.0f;
};

// A convex solid: the intersection of the back half-spaces of its planes.
struct Brush {
    std::vector<Plane> planes;
};

struct SpawnPoint {
    Vec3 feetPosition;
    std::uint16_t yaw = 0;
};

struct Level {
    std::vector<Brush> brushes;
    std::vector<SpawnPoint> spawnPoints;
};

Brush makeBoxBrush(Vec3 min, Vec3 max);

// A ramp filling the box, rising from min.z at min.x to max.z at max.x.
Brush makeRampBrush(Vec3 min, Vec3 max);

} // namespace ct
