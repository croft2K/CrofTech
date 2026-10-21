#include "croftech/level/Level.h"

#include "croftech/core/Angle.h"
#include "croftech/core/ConfigFile.h"
#include "croftech/level/BrushGeometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <utility>

namespace ct {
namespace {

constexpr float kUnitNormalTolerance = 1e-4f;
// A closed solid's face areas, each times its outward normal, sum to zero.
constexpr float kClosureTolerance = 1e-3f;
constexpr double kMaxYawDegrees = 1e6;

bool isFinite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

bool insideWorld(Vec3 v) {
    return std::fabs(v.x) <= kMaxWorldCoordinate && std::fabs(v.y) <= kMaxWorldCoordinate &&
           std::fabs(v.z) <= kMaxWorldCoordinate;
}

// Area times unit normal, measured from the first vertex to keep precision far from the origin.
Vec3 vectorArea(const BrushFace& face) {
    Vec3 sum;
    const Vec3 origin = face.vertices.front();
    for (std::size_t i = 1; i + 1 < face.vertices.size(); ++i) {
        sum += cross(face.vertices[i] - origin, face.vertices[i + 1] - origin);
    }
    return sum * 0.5f;
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
    if (brush.planes.size() < 4 || brush.planes.size() > kMaxBrushPlanes) {
        problem = std::format("a brush needs 4 to {} planes, not {}", kMaxBrushPlanes,
                              brush.planes.size());
        return false;
    }
    for (const Plane& plane : brush.planes) {
        if (!isFinite(plane.normal) || !std::isfinite(plane.distance)) {
            problem = "plane is not finite";
            return false;
        }
        if (std::fabs(dot(plane.normal, plane.normal) - 1.0f) > kUnitNormalTolerance) {
            problem = "plane normal is not unit length";
            return false;
        }
    }

    const std::vector<BrushFace> faces = buildBrushFaces(brush);
    if (faces.size() < 4) {
        problem = "brush encloses no volume";
        return false;
    }
    Vec3 areaSum;
    float totalArea = 0.0f;
    for (const BrushFace& face : faces) {
        for (const Vec3& vertex : face.vertices) {
            if (!insideWorld(vertex)) {
                problem =
                    std::format("brush reaches outside the world (+-{} m)", kMaxWorldCoordinate);
                return false;
            }
        }
        const Vec3 area = vectorArea(face);
        areaSum += area;
        totalArea += length(area);
    }
    if (!(totalArea > 0.0f) || length(areaSum) > kClosureTolerance * totalArea) {
        problem = "brush is not a closed solid";
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
    for (std::size_t i = 0; i < entity.properties.size(); ++i) {
        const EntityProperty& property = entity.properties[i];
        if (!isValidEntityName(property.key)) {
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
