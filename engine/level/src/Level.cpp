#include "croftech/level/Level.h"

#include "croftech/core/Angle.h"
#include "croftech/core/Assert.h"
#include "croftech/core/ConfigFile.h"
#include "croftech/core/DVec3.h"
#include "croftech/level/BrushGeometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <utility>

namespace ct {
namespace {

// A cube of 1 mm sides; the smallest brush on the editor's grid is 1/32 m across.
constexpr double kMinBrushVolume = 1e-9;
constexpr double kMaxYawDegrees = 1e6;

bool isFinite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

bool insideWorld(Vec3 v) {
    return std::fabs(v.x) <= kMaxWorldCoordinate && std::fabs(v.y) <= kMaxWorldCoordinate &&
           std::fabs(v.z) <= kMaxWorldCoordinate;
}

} // namespace

std::string formatVec3(Vec3 v) {
    return std::format("{} {} {}", formatFloat(v.x), formatFloat(v.y), formatFloat(v.z));
}

std::optional<Vec3> parseVec3(std::string_view text) {
    std::array<float, 3> xyz{};
    if (!parseFloats(text, xyz)) {
        return std::nullopt;
    }
    return Vec3{xyz[0], xyz[1], xyz[2]};
}

std::optional<Vec3> entityOrigin(const LevelEntity& entity) {
    const std::string* origin = findProperty(entity, "origin");
    return origin != nullptr ? parseVec3(*origin) : std::nullopt;
}

void setProperty(LevelEntity& entity, std::string_view key, std::string value) {
    for (EntityProperty& property : entity.properties) {
        if (property.key == key) {
            property.value = std::move(value);
            return;
        }
    }
    entity.properties.push_back({std::string(key), std::move(value)});
}

bool checkBrush(const Brush& brush, std::string& problem) {
    const std::size_t vertexCount = brush.vertices.size();
    const std::size_t faceCount = brush.faces.size();
    if (vertexCount < 4 || vertexCount > kMaxBrushVertices) {
        problem =
            std::format("a brush needs 4 to {} corners, not {}", kMaxBrushVertices, vertexCount);
        return false;
    }
    if (faceCount < 4 || faceCount > kMaxBrushFaces) {
        problem = std::format("a brush needs 4 to {} faces, not {}", kMaxBrushFaces, faceCount);
        return false;
    }
    for (const Vec3& v : brush.vertices) {
        if (!isFinite(v)) {
            problem = "corner is not finite";
            return false;
        }
        if (!insideWorld(v)) {
            problem = std::format("brush reaches outside the world (+-{} m)", kMaxWorldCoordinate);
            return false;
        }
    }

    // Topology: every edge is used once in each direction (a closed surface, consistently wound)
    // and V - E + F == 2 (one piece, no holes).
    std::vector<std::pair<std::uint32_t, std::uint32_t>> edges;
    std::vector<std::size_t> lastFace(vertexCount, faceCount);
    for (std::size_t f = 0; f < faceCount; ++f) {
        const std::vector<std::uint32_t>& corners = brush.faces[f].corners;
        if (corners.size() < 3 || corners.size() > vertexCount) {
            problem = "a face needs 3 corners or more";
            return false;
        }
        for (std::size_t i = 0; i < corners.size(); ++i) {
            const std::uint32_t corner = corners[i];
            if (corner >= vertexCount) {
                problem = std::format("face uses corner {}, which does not exist", corner);
                return false;
            }
            if (lastFace[corner] == f) {
                problem = "a face uses a corner twice";
                return false;
            }
            lastFace[corner] = f;
            edges.emplace_back(corner, corners[(i + 1) % corners.size()]);
        }
    }
    if (std::find(lastFace.begin(), lastFace.end(), faceCount) != lastFace.end()) {
        problem = "a corner is on no face";
        return false;
    }
    std::sort(edges.begin(), edges.end());
    const bool closed =
        std::adjacent_find(edges.begin(), edges.end()) == edges.end() &&
        std::all_of(edges.begin(), edges.end(),
                    [&](const auto& edge) {
                        return std::binary_search(edges.begin(), edges.end(),
                                                  std::pair{edge.second, edge.first});
                    }) &&
        vertexCount + faceCount == edges.size() / 2 + 2;
    if (!closed) {
        problem = "brush is not a closed solid";
        return false;
    }

    // Shape: flat faces with convex outlines, every corner behind every face, some volume.
    double volume = 0.0;
    for (std::size_t f = 0; f < faceCount; ++f) {
        const std::vector<std::uint32_t>& corners = brush.faces[f].corners;
        const DPlane exact = exactFacePlane(brush, f);
        const double size = length(exact.normal);
        if (!(size > 0.0)) {
            problem = "a face has no area";
            return false;
        }
        const DVec3 normal = exact.normal * (1.0 / size);
        const double distance = exact.distance / size;
        volume += exact.distance / 6.0;
        for (std::size_t i = 0; i < corners.size(); ++i) {
            const DVec3 prev =
                toDouble(brush.vertices[corners[(i + corners.size() - 1) % corners.size()]]);
            const DVec3 here = toDouble(brush.vertices[corners[i]]);
            const DVec3 next = toDouble(brush.vertices[corners[(i + 1) % corners.size()]]);
            if (std::fabs(dot(normal, here) - distance) > kBrushTolerance) {
                problem = "a face is not flat";
                return false;
            }
            // How far the corner pokes into the face from the line between its neighbours.
            const double chord = length(next - prev);
            if (chord > 0.0 &&
                dot(cross(here - prev, next - here), normal) / chord < -kBrushTolerance) {
                problem = "a face is not convex";
                return false;
            }
        }
        for (const Vec3& v : brush.vertices) {
            if (dot(normal, toDouble(v)) - distance > kBrushTolerance) {
                problem = "brush is not convex";
                return false;
            }
        }
    }
    if (!(volume > kMinBrushVolume)) {
        problem = "brush encloses no volume";
        return false;
    }
    return true;
}

bool isValidEntityName(std::string_view name) {
    return !name.empty() && name.size() <= kMaxNameLength && name != "end" &&
           std::all_of(name.begin(), name.end(), [](char c) {
               return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                      c == '_' || c == '.' || c == '-';
           });
}

bool checkEntity(const LevelEntity& entity, std::string& problem) {
    if (!isValidEntityName(entity.className)) {
        problem = "invalid class name";
        return false;
    }
    if (entity.properties.size() > kMaxEntityProperties) {
        problem = std::format("more than {} properties", kMaxEntityProperties);
        return false;
    }
    if (entity.outputs.size() > kMaxEntityOutputs) {
        problem = std::format("more than {} outputs", kMaxEntityOutputs);
        return false;
    }
    for (const EntityOutput& output : entity.outputs) {
        if (!isValidEntityName(output.event) || !isValidEntityName(output.target) ||
            !isValidEntityName(output.input)) {
            problem = "an output has an invalid event, target or input name";
            return false;
        }
        if (!(output.delay >= 0.0f && output.delay <= kMaxOutputDelay)) {
            problem = std::format("output delays are 0 to {} seconds", kMaxOutputDelay);
            return false;
        }
    }
    for (std::size_t i = 0; i < entity.properties.size(); ++i) {
        const EntityProperty& property = entity.properties[i];
        if (!isValidEntityName(property.key) || property.key == "brush" ||
            property.key == "output") {
            problem = "invalid property key";
            return false;
        }
        const std::string_view value = property.value;
        const bool hasControl = std::any_of(value.begin(), value.end(), [](char c) {
            return static_cast<unsigned char>(c) < 0x20 || c == 0x7F;
        });
        if (value.size() > kMaxPropertyValueLength || hasControl ||
            (!value.empty() && (value.front() == ' ' || value.back() == ' '))) {
            problem = std::format("property '{}' has an invalid value", property.key);
            return false;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (entity.properties[j].key == property.key) {
                problem = std::format("property '{}' appears twice", property.key);
                return false;
            }
        }
    }
    return true;
}

bool checkBrushOwners(const Level& level, std::string& problem) {
    for (std::size_t i = 0; i < level.brushes.size(); ++i) {
        const std::uint32_t owner = level.brushes[i].entity;
        if (owner != kNoEntity && owner >= level.entities.size()) {
            problem = std::format("brush {} belongs to entity {}, which does not exist", i, owner);
            return false;
        }
    }
    return true;
}

void insertEntity(Level& level, std::size_t index, LevelEntity entity) {
    CT_ASSERT(index <= level.entities.size());
    for (Brush& brush : level.brushes) {
        if (brush.entity != kNoEntity && brush.entity >= index) {
            ++brush.entity;
        }
    }
    level.entities.insert(level.entities.begin() + static_cast<std::ptrdiff_t>(index),
                          std::move(entity));
}

LevelEntity removeEntity(Level& level, std::size_t index) {
    CT_ASSERT(index < level.entities.size());
    for (Brush& brush : level.brushes) {
        CT_ASSERT(brush.entity != index);
        if (brush.entity != kNoEntity && brush.entity > index) {
            --brush.entity;
        }
    }
    const auto position = level.entities.begin() + static_cast<std::ptrdiff_t>(index);
    LevelEntity removed = std::move(*position);
    level.entities.erase(position);
    return removed;
}

std::vector<std::size_t> entityBrushes(const Level& level, std::size_t entity) {
    std::vector<std::size_t> brushes;
    for (std::size_t i = 0; i < level.brushes.size(); ++i) {
        if (level.brushes[i].entity == entity) {
            brushes.push_back(i);
        }
    }
    return brushes;
}

const std::string* findProperty(const LevelEntity& entity, std::string_view key) {
    for (const EntityProperty& property : entity.properties) {
        if (property.key == key) {
            return &property.value;
        }
    }
    return nullptr;
}

LevelEntity makePlayerSpawn(Vec3 feetPosition, float yawDegrees) {
    return {std::string(kPlayerSpawnClass),
            {{"origin", formatVec3(feetPosition)}, {"yaw", formatFloat(yawDegrees)}}};
}

std::optional<std::vector<SpawnPoint>> readSpawnPoints(const Level& level, std::string& error) {
    std::vector<SpawnPoint> spawns;
    for (std::size_t i = 0; i < level.entities.size(); ++i) {
        const LevelEntity& entity = level.entities[i];
        if (entity.className != kPlayerSpawnClass) {
            continue;
        }
        SpawnPoint spawn;
        const std::optional<Vec3> origin = entityOrigin(entity);
        if (!origin) {
            error =
                std::format("entity {} ({}): origin must be three numbers", i, kPlayerSpawnClass);
            return std::nullopt;
        }
        spawn.feetPosition = *origin;
        if (!insideWorld(spawn.feetPosition)) {
            error =
                std::format("entity {} ({}): origin is outside the world", i, kPlayerSpawnClass);
            return std::nullopt;
        }
        if (const std::string* yaw = findProperty(entity, "yaw")) {
            float degrees = 0.0f;
            if (!parseFloat(*yaw, degrees) || std::fabs(degrees) > kMaxYawDegrees) {
                error = std::format("entity {} ({}): yaw must be a number of degrees", i,
                                    kPlayerSpawnClass);
                return std::nullopt;
            }
            const long long units = std::llround(static_cast<double>(degrees) / 360.0 *
                                                 static_cast<double>(kAngleUnitsPerTurn));
            spawn.yaw = static_cast<std::uint16_t>(units & 0xFFFF);
        }
        spawns.push_back(spawn);
    }
    return spawns;
}

Brush makeBoxBrush(Vec3 min, Vec3 max) {
    Brush brush;
    // Corner i has bit 0 for x, bit 1 for y, bit 2 for z.
    for (std::uint32_t i = 0; i < 8; ++i) {
        brush.vertices.push_back({(i & 1) != 0 ? max.x : min.x, (i & 2) != 0 ? max.y : min.y,
                                  (i & 4) != 0 ? max.z : min.z});
    }
    // +X, -X, +Y, -Y, +Z, -Z.
    brush.faces = {{{1, 3, 7, 5}}, {{0, 4, 6, 2}}, {{2, 6, 7, 3}},
                   {{0, 1, 5, 4}}, {{4, 5, 7, 6}}, {{0, 2, 3, 1}}};
    return brush;
}

Brush makeRampBrush(Vec3 min, Vec3 max) {
    Brush brush;
    brush.vertices = {{min.x, min.y, min.z}, {max.x, min.y, min.z}, {max.x, max.y, min.z},
                      {min.x, max.y, min.z}, {max.x, min.y, max.z}, {max.x, max.y, max.z}};
    // Back wall (+X), the two sides, the bottom and the slope.
    brush.faces = {{{1, 2, 5, 4}}, {{3, 5, 2}}, {{0, 1, 4}}, {{0, 3, 2, 1}}, {{0, 4, 5, 3}}};
    return brush;
}

} // namespace ct
