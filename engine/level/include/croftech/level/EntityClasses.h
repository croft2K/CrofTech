#pragma once

#include "croftech/core/Math.h"
#include "croftech/level/Level.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace ct {

// What the engine knows about each entity class: its properties with their types and defaults, the
// events it fires and the inputs it accepts. The editor builds its inspector from this and the game
// reads properties through it, so a default lives in one place. Levels may also hold classes not
// listed here; they load and save unchanged, and the game ignores them.

enum class PropertyType : std::uint8_t {
    Text,
    Name,   // an entity name, which outputs target: 1 to kMaxNameLength of [A-Za-z0-9_.-]
    Number, // a decimal number from minimum to maximum
    Count,  // a whole number from minimum to maximum
    Flag,   // 0 or 1
    Vector, // three numbers: x y z, inside the world
    Angle,  // degrees
};

struct PropertyInfo {
    std::string_view key;
    PropertyType type = PropertyType::Text;
    std::string_view defaultValue;
    std::string_view description;
    float minimum = 0.0f; // Number and Count only
    float maximum = 0.0f;
};

// An event an entity fires, or an input it accepts.
struct SignalInfo {
    std::string_view name;
    std::string_view description;
};

enum class EntityShape : std::uint8_t {
    Point, // placed at its "origin"
    Brush, // made of brushes
};

struct EntityClassInfo {
    std::string_view name;
    std::string_view description;
    EntityShape shape = EntityShape::Point;
    std::span<const PropertyInfo> properties;
    std::span<const SignalInfo> events;
    std::span<const SignalInfo> inputs;
};

// Every class, in the order the editor lists them.
std::span<const EntityClassInfo> entityClasses();
const EntityClassInfo* findEntityClass(std::string_view name);
const PropertyInfo* findPropertyInfo(const EntityClassInfo& entityClass, std::string_view key);
const SignalInfo* findSignal(std::span<const SignalInfo> signals, std::string_view name);

// The entity's value for the property, or the class default when the entity doesn't set it.
std::string_view propertyValue(const LevelEntity& entity, const PropertyInfo& property);

// Whether the value is valid for the property; problem says why not.
bool checkPropertyValue(const PropertyInfo& property, std::string_view value, std::string& problem);

// Checks every property the class defines (set or defaulted) and that every output names an
// event of the class and a valid target. Inputs are checked by the game, which knows the targets.
bool checkEntityClassProperties(const LevelEntity& entity, const EntityClassInfo& entityClass,
                                std::string& problem);

// Typed reads of a property of the entity's class, which must exist with that type. Values that
// don't parse give the type's zero; checkEntityClassProperties first to report them.
float numberProperty(const LevelEntity& entity, const EntityClassInfo& entityClass,
                     std::string_view key);
std::uint32_t countProperty(const LevelEntity& entity, const EntityClassInfo& entityClass,
                            std::string_view key);
bool flagProperty(const LevelEntity& entity, const EntityClassInfo& entityClass,
                  std::string_view key);
Vec3 vectorProperty(const LevelEntity& entity, const EntityClassInfo& entityClass,
                    std::string_view key);
std::string_view textProperty(const LevelEntity& entity, const EntityClassInfo& entityClass,
                              std::string_view key);

// A new entity of the class with no properties set, so it takes every default.
LevelEntity makeEntity(const EntityClassInfo& entityClass);

} // namespace ct
