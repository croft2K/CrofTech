#include "WorldMesh.h"

#include "croftech/level/BrushGeometry.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ct {
namespace {

// Muted blockout colours (linear RGB). The first brush, the floor, gets the neutral grey.
constexpr std::array kPalette{
    Vec3{0.30f, 0.30f, 0.31f}, Vec3{0.55f, 0.36f, 0.24f}, Vec3{0.26f, 0.42f, 0.55f},
    Vec3{0.40f, 0.52f, 0.30f}, Vec3{0.58f, 0.52f, 0.30f}, Vec3{0.45f, 0.33f, 0.52f},
    Vec3{0.33f, 0.50f, 0.48f},
};

bool isClass(const Level& level, const Brush& brush, std::string_view className) {
    return brush.entity < level.entities.size() &&
           level.entities[brush.entity].className == className;
}

// Brushes of entities are coloured by what they are, so movers and triggers stand out.
Vec3 brushColor(const Level& level, std::size_t index) {
    const std::uint32_t entity = level.brushes[index].entity;
    if (entity < level.entities.size()) {
        const std::string& className = level.entities[entity].className;
        if (className == "trigger") {
            return {0.80f, 0.42f, 0.08f};
        }
        if (className == "mover") {
            return {0.16f, 0.40f, 0.80f};
        }
        if (className == "checkpoint") {
            return {0.20f, 0.65f, 0.30f};
        }
        if (className == "hurt") {
            return {0.75f, 0.10f, 0.12f};
        }
    }
    return kPalette[index % kPalette.size()];
}

void addBrush(WorldMesh& mesh, const Level& level, std::size_t brushIndex) {
    const Vec3 color = brushColor(level, brushIndex);
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

} // namespace

WorldMesh buildWorldMesh(const Level& level, MeshPurpose purpose) {
    WorldMesh mesh;
    if (purpose == MeshPurpose::Editing) {
        for (std::size_t brushIndex = 0; brushIndex < level.brushes.size(); ++brushIndex) {
            addBrush(mesh, level, brushIndex);
        }
        return mesh;
    }
    for (std::size_t brushIndex = 0; brushIndex < level.brushes.size(); ++brushIndex) {
        const Brush& brush = level.brushes[brushIndex];
        // Triggers, checkpoints and hurt volumes are invisible; movers are parts of their own.
        if (!isClass(level, brush, "trigger") && !isClass(level, brush, "checkpoint") &&
            !isClass(level, brush, "hurt") && !isClass(level, brush, "mover")) {
            addBrush(mesh, level, brushIndex);
        }
    }
    for (std::uint32_t entity = 0; entity < level.entities.size(); ++entity) {
        if (level.entities[entity].className != "mover") {
            continue;
        }
        const auto first = static_cast<std::uint32_t>(mesh.indices.size());
        for (const std::size_t brushIndex : entityBrushes(level, entity)) {
            addBrush(mesh, level, brushIndex);
        }
        mesh.parts.push_back({first, static_cast<std::uint32_t>(mesh.indices.size()) - first});
        mesh.partEntities.push_back(entity);
    }
    return mesh;
}

} // namespace ct
