#include "croftech/level/Level.h"

namespace ct {

Brush makeBoxBrush(Vec3 min, Vec3 max) {
    Brush brush;
    brush.planes = {
        {{1.0f, 0.0f, 0.0f}, max.x},   {{-1.0f, 0.0f, 0.0f}, -min.x}, {{0.0f, 1.0f, 0.0f}, max.y},
        {{0.0f, -1.0f, 0.0f}, -min.y}, {{0.0f, 0.0f, 1.0f}, max.z},   {{0.0f, 0.0f, -1.0f}, -min.z},
    };
    return brush;
}

Brush makeRampBrush(Vec3 min, Vec3 max) {
    const Vec3 slopeNormal = normalize(Vec3{-(max.z - min.z), 0.0f, max.x - min.x});
    Brush brush;
    brush.planes = {
        {{1.0f, 0.0f, 0.0f}, max.x},
        {{0.0f, 1.0f, 0.0f}, max.y},
        {{0.0f, -1.0f, 0.0f}, -min.y},
        {{0.0f, 0.0f, -1.0f}, -min.z},
        {slopeNormal, dot(slopeNormal, Vec3{min.x, 0.0f, min.z})},
    };
    return brush;
}

} // namespace ct
