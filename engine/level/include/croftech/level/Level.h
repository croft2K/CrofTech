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

// One side of a brush: indices into Brush::vertices, counter-clockwise seen from outside.
struct BrushFace {
    std::vector<std::uint32_t> corners;

    friend bool operator==(const BrushFace&, const BrushFace&) = default;
};

inline constexpr std::uint32_t kNoEntity = UINT32_MAX;

// A convex solid, stored by its corners and faces. Planes are derived from the corners
// (BrushGeometry.h) rather than stored: corners placed on the grid then stay exactly on it through
// any number of edits, where float planes would move them by millimetres far from the origin.
struct Brush {
    std::vector<Vec3> vertices;
    std::vector<BrushFace> faces;
    // The entity this brush is part of, as an index into Level::entities, or kNoEntity for world
    // geometry. Doors, movers and triggers are entities made of brushes.
    std::uint32_t entity = kNoEntity;

    friend bool operator==(const Brush&, const Brush&) = default;
};

// Entity properties are text, as in the level file; each entity class reads its own. Keeping them
// as text means a level round-trips classes and keys this build doesn't know about.
struct EntityProperty {
    std::string key;
    std::string value;

    friend bool operator==(const EntityProperty&, const EntityProperty&) = default;
};

// A wire from one entity to others: when the event happens (a player entering a trigger, a door
// finishing opening), the input (open, enable, add) is sent to every entity whose "name" property
// is the target, after the delay. Wiring is how levels get logic without scripts.
struct EntityOutput {
    std::string event;
    std::string target;
    std::string input;
    float delay = 0.0f; // seconds

    friend bool operator==(const EntityOutput&, const EntityOutput&) = default;
};

// Anything placed in a level that isn't world geometry: spawn points, triggers, doors, relays.
// Point entities have an "origin" property; brush entities are made of the brushes whose entity
// is theirs. Properties and outputs keep their order so saved files diff cleanly.
struct LevelEntity {
    std::string className;
    std::vector<EntityProperty> properties;
    std::vector<EntityOutput> outputs{};

    friend bool operator==(const LevelEntity&, const LevelEntity&) = default;
};

struct Level {
    std::vector<Brush> brushes;
    std::vector<LevelEntity> entities;

    friend bool operator==(const Level&, const Level&) = default;
};

// Inserting or removing an entity renumbers the entities after it, so these also renumber the
// brushes that belong to them. An entity must have no brushes when it is removed.
void insertEntity(Level& level, std::size_t index, LevelEntity entity);
LevelEntity removeEntity(Level& level, std::size_t index);

// Indices of the brushes that belong to the entity, in level order.
std::vector<std::size_t> entityBrushes(const Level& level, std::size_t entity);

// Limits on level data. Levels come from files, so these bound the work a malformed or hostile
// file can cause as well as describing what the engine supports.
//
// Collision keeps boxes kSurfaceEpsilon (about 1 mm) off surfaces, so float positions need several
// times that precision everywhere in the level; at 2 km from the origin a float step is 0.24 mm.
inline constexpr float kMaxWorldCoordinate = 2048.0f;
inline constexpr std::size_t kMaxLevelBrushes = 65536;
// Checking a brush costs faces x corners. A convex solid with F faces has at most 2F - 4 corners.
inline constexpr std::size_t kMaxBrushFaces = 512;
inline constexpr std::size_t kMaxBrushVertices = 2 * kMaxBrushFaces - 4;
inline constexpr std::size_t kMaxLevelEntities = 65536;
inline constexpr std::size_t kMaxEntityProperties = 256;
inline constexpr std::size_t kMaxEntityOutputs = 64;
inline constexpr float kMaxOutputDelay = 3600.0f; // seconds
inline constexpr std::size_t kMaxNameLength = 64; // class names, property keys, output names
inline constexpr std::size_t kMaxPropertyValueLength = 1024;

// Each check returns false with a description of the first problem it finds.

// Corners are finite and inside the world; faces are flat, convex and wound outwards, and together
// close a convex solid with some volume (each edge shared by exactly two faces, every corner
// behind every face). Allows kBrushTolerance for float rounding.
bool checkBrush(const Brush& brush, std::string& problem);

// How far a corner may stray from the plane of a face it is on, or in front of any face, for
// rounding to float far from the origin. Collision keeps boxes kSurfaceEpsilon (about 1 mm) off
// surfaces, so this stays below it.
inline constexpr float kBrushTolerance = 5e-4f;

// Class names, keys and the names in outputs are 1 to kMaxNameLength of [A-Za-z0-9_.-]; "end" is
// reserved by the level file, and so are the keys "brush" and "output". Values are single-line
// text with no control characters or surrounding spaces. No duplicate keys. Output delays are 0 to
// kMaxOutputDelay seconds.
bool checkEntity(const LevelEntity& entity, std::string& problem);
bool isValidEntityName(std::string_view name);

// Every brush belongs to the world or to an entity of the level.
bool checkBrushOwners(const Level& level, std::string& problem);

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
