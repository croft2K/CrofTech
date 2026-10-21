#pragma once

#include "croftech/core/Math.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ct {

// World units are metres, Z up, right-handed. Yaw 0 faces +X and increases towards +Y.

// The set of points p with dot(normal, p) == distance. Normals point out of the solid.
struct Plane {
    Vec3 normal;
    float distance = 0.0f;

    friend bool operator==(const Plane&, const Plane&) = default;
};

// A convex solid: the intersection of the back half-spaces of its planes.
struct Brush {
    std::vector<Plane> planes;

    friend bool operator==(const Brush&, const Brush&) = default;
};

// Entity properties are text, as in the level file; each entity class reads its own. Keeping them
// as text means a level round-trips classes and keys this build doesn't know about.
struct EntityProperty {
    std::string key;
    std::string value;

    friend bool operator==(const EntityProperty&, const EntityProperty&) = default;
};

// Anything placed in a level that isn't world geometry: spawn points now; later lights, triggers
// and doors. Properties keep their order so saved files diff cleanly.
struct LevelEntity {
    std::string className;
    std::vector<EntityProperty> properties;

    friend bool operator==(const LevelEntity&, const LevelEntity&) = default;
};

struct Level {
    std::vector<Brush> brushes;
    std::vector<LevelEntity> entities;

    friend bool operator==(const Level&, const Level&) = default;
};

// Limits on level data. Levels come from files, so these bound the work a malformed or hostile
// file can cause as well as describing what the engine supports.
//
// Collision keeps boxes kSurfaceEpsilon (about 1 mm) off surfaces, so float positions need several
// times that precision everywhere in the level; at 2 km from the origin a float step is 0.24 mm.
inline constexpr float kMaxWorldCoordinate = 2048.0f;
inline constexpr std::size_t kMaxLevelBrushes = 65536;
inline constexpr std::size_t kMaxBrushPlanes = 64; // building faces costs planes^4
inline constexpr std::size_t kMaxLevelEntities = 65536;
inline constexpr std::size_t kMaxEntityProperties = 256;
inline constexpr std::size_t kMaxNameLength = 64; // class names and property keys
inline constexpr std::size_t kMaxPropertyValueLength = 1024;

// Each check returns false with a description of the first problem it finds.

// Planes are finite with unit normals, and they enclose a closed solid inside the world limits.
bool checkBrush(const Brush& brush, std::string& problem);

// Class names and keys are 1 to kMaxNameLength of [A-Za-z0-9_.-]; "end" is reserved by the level
// file. Values are single-line text with no control characters or surrounding spaces. No
// duplicate keys.
bool checkEntity(const LevelEntity& entity, std::string& problem);
bool isValidEntityName(std::string_view name);

const std::string* findProperty(const LevelEntity& entity, std::string_view key);
// Replaces the value of an existing key, or adds the key at the end.
void setProperty(LevelEntity& entity, std::string_view key, std::string value);

// "x y z" in shortest exact form, as level files store positions.
std::string formatVec3(Vec3 v);
std::optional<Vec3> parseVec3(std::string_view text);

// The "origin" property, the position of every point entity.
std::optional<Vec3> entityOrigin(const LevelEntity& entity);

struct SpawnPoint {
    Vec3 feetPosition;
    std::uint16_t yaw = 0;
};

inline constexpr std::string_view kPlayerSpawnClass = "player_spawn";

// A player_spawn has "origin" (x y z of the feet, metres) and an optional "yaw" in degrees.
LevelEntity makePlayerSpawn(Vec3 feetPosition, float yawDegrees);

// Every player_spawn in the level, in level order. Fails if any has a missing or bad property.
std::optional<std::vector<SpawnPoint>> readSpawnPoints(const Level& level, std::string& error);

Brush makeBoxBrush(Vec3 min, Vec3 max);

// A ramp filling the box, rising from min.z at min.x to max.z at max.x.
Brush makeRampBrush(Vec3 min, Vec3 max);

} // namespace ct
