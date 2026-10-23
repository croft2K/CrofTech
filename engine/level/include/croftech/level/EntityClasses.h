#pragma once

#include "croftech/core/Math.h"
#include "croftech/level/Level.h"

#include <cstddef>
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
    Choice, // one of a list of words
    Weapon, // the name of one of the game's weapons (its game.cfg lists them), or empty
    Enemy,  // the name of one of the game's kinds of enemy (its game.cfg lists them), or empty
    // The name of another entity this one uses (a patrol's next point), or empty; its choices are
    // the classes that entity may be of.
    Reference,
    // The name of a script, scripts/<name>.luau in the content layers, or empty.
    Script,
};

// Enemies placed in a level, standing where their origin is.
inline constexpr std::string_view kEnemyClass = "enemy";
// The points of an enemy's patrol route: it walks from each to the next.
inline constexpr std::string_view kPatrolPointClass = "patrol_point";
// Where enemies appear during play, sent by an encounter's waves or a spawn input.
inline constexpr std::string_view kSpawnerClass = "spawner";
// A fight in waves, each sent by the spawners that name the encounter and that wave.
inline constexpr std::string_view kEncounterClass = "encounter";
// Runs a Luau script (script/ScriptProgram.h), whose inputs and events are the ones it declares.
inline constexpr std::string_view kScriptClass = "script";

// Items players pick up by walking into them. The origin is where one rests; it is taken by
// touching this box above it.
inline constexpr std::string_view kPickupClass = "pickup";
inline constexpr Vec3 kPickupHalfExtents{0.35f, 0.35f, 0.35f};

struct PropertyInfo {
    std::string_view key;
    PropertyType type = PropertyType::Text;
    std::string_view defaultValue;
    std::string_view description;
    float minimum = 0.0f; // Number and Count only
    float maximum = 0.0f;
    // Choice: the words it may be. Reference: the classes the entity it names may be of.
    std::span<const std::string_view> choices{};
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
    // Its events and inputs are declared by its script rather than listed here, so only something
    // that has read the script can check them.
    bool scripted = false;
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
// event of the class (unless its script declares them) and a valid target. Inputs are checked by
// the game, which knows the targets.
bool checkEntityClassProperties(const LevelEntity& entity, const EntityClassInfo& entityClass,
                                std::string& problem);

// Typed reads of a property of the entity's class, which must exist with that type. Values that
// don't parse give the type's zero; checkEntityClassProperties first to report them.
float numberProperty(const LevelEntity& entity, const EntityClassInfo& entityClass,
                     std::string_view key);
// Degrees.
float angleProperty(const LevelEntity& entity, const EntityClassInfo& entityClass,
                    std::string_view key);
std::uint32_t countProperty(const LevelEntity& entity, const EntityClassInfo& entityClass,
                            std::string_view key);
bool flagProperty(const LevelEntity& entity, const EntityClassInfo& entityClass,
                  std::string_view key);
Vec3 vectorProperty(const LevelEntity& entity, const EntityClassInfo& entityClass,
                    std::string_view key);
// The index of the value in the property's choices.
std::size_t choiceProperty(const LevelEntity& entity, const EntityClassInfo& entityClass,
                           std::string_view key);
// Text, names, weapons, enemies, references and scripts.
std::string_view textProperty(const LevelEntity& entity, const EntityClassInfo& entityClass,
                              std::string_view key);

// A new entity of the class with no properties set, so it takes every default.
LevelEntity makeEntity(const EntityClassInfo& entityClass);

// Ready-made entities for the common cases, so a door or a lift needs no setting up or wiring:
// an entity of a class with some properties set. The editor lists them in its Create menu, which
// makes a brush of the preset's size, and offers them for brushes already built. Everything a
// preset sets stays editable in the inspector.
struct PresetProperty {
    std::string_view key;
    std::string_view value;
};

struct EntityPreset {
    std::string_view name;
    std::string_view description;
    std::string_view className;
    std::span<const PresetProperty> properties;
    Vec3 move;           // where a mover goes...
    bool moveUpByHeight; // ...or up by its brushes' height, as a door does
    Vec3 size;           // the box brush the Create menu makes; none for point classes
};

std::span<const EntityPreset> entityPresets();
LevelEntity makePresetEntity(const EntityPreset& preset);

} // namespace ct
