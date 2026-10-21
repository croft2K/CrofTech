#include "WorldMesh.h"

#include "croftech/level/BrushGeometry.h"

#include <array>
#include <cstddef>

namespace ct {
namespace {

// Muted blockout colours (linear RGB). The first brush, the floor, gets the neutral grey.
constexpr std::array kPalette{
    Vec3{0.30f, 0.30f, 0.31f}, Vec3{0.55f, 0.36f, 0.24f}, Vec3{0.26f, 0.42f, 0.55f},
    Vec3{0.40f, 0.52f, 0.30f}, Vec3{0.58f, 0.52f, 0.30f}, Vec3{0.45f, 0.33f, 0.52f},
    Vec3{0.33f, 0.50f, 0.48f},
};

} // namespace

WorldMesh buildWorldMesh(const Level& level) {
    WorldMesh mesh;
    for (std::size_t brushIndex = 0; brushIndex < level.brushes.size(); ++brushIndex) {
        const Vec3 color = kPalette[brushIndex % kPalette.size()];
        for (const BrushPolygon& face : brushPolygons(level.brushes[brushIndex])) {
            const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
            for (const Vec3& position : face.vertices) {
                mesh.vertices.push_back({position, face.plane.normal, color});
            }
            // Faces are convex, so a fan from the first vertex triangulates them.
            for (std::uint32_t i = 1; i + 1 < face.vertices.size(); ++i) {
                mesh.indices.insert(mesh.indices.end(), {first, first + i, first + i + 1});
            }
        }
    }
    return mesh;
}

} // namespace ct
