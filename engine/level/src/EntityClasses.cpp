#include "croftech/level/EntityClasses.h"

#include "croftech/core/Assert.h"
#include "croftech/core/ConfigFile.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace ct {
namespace {

using enum PropertyType;

constexpr float kMaxAngleDegrees = 1e6f;

constexpr PropertyInfo kName{"name", Name, "", "Name that outputs of other entities target"};
constexpr PropertyInfo kOrigin{"origin", Vector, "", "Position, metres"};
constexpr PropertyInfo kStartDisabled{"start_disabled", Flag, "0",
                                      "Starts switched off; an enable input turns it on"};

constexpr PropertyInfo kSpawnProperties[] = {
    {"origin", Vector, "", "Where the player's feet are, metres"},
    {"yaw", Angle, "0", "Direction the player faces, degrees; 0 is +X, 90 is +Y"},
};

constexpr PropertyInfo kTriggerProperties[] = {
    kName,
    {"once", Flag, "0", "Switches itself off after the first player enters"},
    kStartDisabled,
};
constexpr SignalInfo kTriggerEvents[] = {
    {"on_enter", "A player came into the trigger"},
    {"on_exit", "A player left the trigger"},
};
constexpr SignalInfo kSwitchInputs[] = {
    {"enable", "Switch on"},
    {"disable", "Switch off"},
    {"toggle", "Switch on if off, off if on"},
};

constexpr PropertyInfo kCheckpointProperties[] = {
    kName,
    kStartDisabled,
};
constexpr SignalInfo kCheckpointEvents[] = {
    {"on_reached", "The first player came into it, and the game was saved"},
};

constexpr PropertyInfo kHurtProperties[] = {
    kName,
    {"damage", Count, "10", "Damage each hit; armour takes its usual share", 1.0f, 100000.0f},
    {"interval", Number, "0.5", "Seconds between hits while something stays inside", 0.0078125f,
     60.0f},
    kStartDisabled,
};

constexpr PropertyInfo kRelayProperties[] = {
    kOrigin,
    kName,
    kStartDisabled,
};
constexpr SignalInfo kRelayEvents[] = {
    {"on_trigger", "Passed on a trigger input while switched on"},
};
constexpr SignalInfo kRelayInputs[] = {
    {"trigger", "Fire on_trigger, if switched on"},
    {"enable", "Switch on"},
    {"disable", "Switch off"},
};

constexpr PropertyInfo kCounterProperties[] = {
    kOrigin,
    kName,
    {"count", Count, "2", "How many add inputs fire on_reached", 1.0f, 65535.0f},
};
constexpr SignalInfo kCounterEvents[] = {
    {"on_reached", "The count was reached; it then starts again from zero"},
};
constexpr SignalInfo kCounterInputs[] = {
    {"add", "Add one to the count"},
    {"reset", "Start again from zero"},
};

constexpr PropertyInfo kTimerProperties[] = {
    kOrigin,
    kName,
    {"interval", Number, "1", "Seconds between on_timer events", 0.0078125f, 3600.0f},
    {"repeat", Flag, "1", "Keeps firing every interval; 0 fires once and stops"},
    {"start_on", Flag, "0", "Starts running when the level starts"},
};
constexpr SignalInfo kTimerEvents[] = {
    {"on_timer", "The interval passed"},
};
constexpr SignalInfo kTimerInputs[] = {
    {"start", "Start running from the beginning of an interval"},
    {"stop", "Stop running"},
    {"toggle", "Start if stopped, stop if running"},
};

constexpr std::string_view kActivateChoices[] = {"input", "touch", "stand"};
constexpr std::string_view kMoverNavChoices[] = {"auto", "door", "lift", "solid"};

constexpr PropertyInfo kMoverProperties[] = {
    kName,
    {"move", Vector, "0 0 0",
     "From where it is built to where it moves, metres (x y z); drag its end handle in the view"},
    {"speed", Number, "3", "Metres per second", 0.01f, 100.0f},
    {"activate", Choice, "input",
     "What sets it moving: only inputs, a player walking into it (doors), or a player standing on "
     "it (lifts)",
     0.0f, 0.0f, kActivateChoices},
    {"wait", Number, "3",
     "Seconds it waits at the end before coming back by itself; -1 stays there", -1.0f, 3600.0f},
    {"loop", Flag, "0", "Keeps going back and forth, waiting at each end (platforms)"},
    {"start_moving", Flag, "0", "Sets off as the level starts"},
    {"start_at_end", Flag, "0", "Starts at the end position (a door that starts open)"},
    {"nav", Choice, "auto",
     "How enemies find their way past it: a door they go through when it is open, a lift they "
     "ride between its stops, or a solid obstacle. auto: movers that open to a touch or an input "
     "are doors, those stood on or that loop are lifts, and those that don't move are solid",
     0.0f, 0.0f, kMoverNavChoices},
};
constexpr SignalInfo kMoverEvents[] = {
    {"on_leave_start", "Set off from the start (a door starts opening)"},
    {"on_reached_end", "Arrived at the end (a door is open)"},
    {"on_leave_end", "Set off back from the end (a door starts closing)"},
    {"on_reached_start", "Arrived back at the start (a door is shut)"},
};
constexpr SignalInfo kMoverInputs[] = {
    {"move_to_end", "Move to the end; it comes back after its wait"},
    {"move_to_start", "Move back to the start"},
    {"toggle", "Head the other way"},
    {"stop", "Stop where it is"},
    {"resume", "Carry on the way it was going"},
};

constexpr std::string_view kPickupItems[] = {"health", "armour", "ammo", "weapon"};

constexpr PropertyInfo kPickupProperties[] = {
    {"origin", Vector, "", "Where it rests, metres"},
    kName,
    {"item", Choice, "health",
     "What it gives: health, armour, ammo for its weapon, or its weapon (with some ammo)", 0.0f,
     0.0f, kPickupItems},
    {"amount", Count, "0",
     "Health or armour it gives, or rounds of ammo; 0 is the usual: 25 health, 50 armour, or "
     "the weapon's pickup_ammo",
     0.0f, 10000.0f},
    {"limit", Count, "0",
     "Most health or armour it tops up to; 0 is the rules' maximum (rules.max_health, "
     "rules.max_armour). Above the maximum, it drains away again",
     0.0f, 100000.0f},
    {"weapon", Weapon, "", "The weapon for ammo and weapon items"},
    {"respawn", Number, "30", "Seconds after it is taken before it comes back; -1 never does",
     -1.0f, 3600.0f},
    kStartDisabled,
};
constexpr SignalInfo kPickupEvents[] = {
    {"on_pickup", "A player took it"},
};
constexpr SignalInfo kPickupInputs[] = {
    {"enable", "Show it, so it can be taken (once back, if it was taken)"},
    {"disable", "Hide it, so it can't be taken"},
    {"toggle", "Show it if hidden, hide it if shown"},
};

constexpr PropertyInfo kEnemyProperties[] = {
    {"origin", Vector, "", "Where its feet are, metres"},
    {"yaw", Angle, "0", "Direction it faces, degrees; 0 is +X, 90 is +Y"},
    kName,
    {"type", Enemy, "",
     "The kind of enemy, from the game's list (enemies in game.cfg); empty is the first"},
    {"ambush", Flag, "0",
     "Deaf to shots and footsteps: it wakes only on seeing a player or being hurt"},
};
constexpr SignalInfo kEnemyEvents[] = {
    {"on_alert", "It noticed a player (saw one, was hurt, or was told to) and went after them"},
    {"on_death", "It was killed"},
};
constexpr SignalInfo kEnemyInputs[] = {
    {"alert", "Go after the player who set this off, or the nearest one"},
};

constexpr EntityClassInfo kClasses[] = {
    {kPlayerSpawnClass, "Where a player starts", EntityShape::Point, kSpawnProperties, {}, {}},
    {"trigger", "An invisible volume that fires events when players go in and out",
     EntityShape::Brush, kTriggerProperties, kTriggerEvents, kSwitchInputs},
    {"checkpoint", "An invisible volume that saves the game the first time a player comes into it",
     EntityShape::Brush, kCheckpointProperties, kCheckpointEvents, kSwitchInputs},
    {"hurt",
     "An invisible volume that hurts whatever is inside it: lava, slime, kill pits",
     EntityShape::Brush,
     kHurtProperties,
     {},
     kSwitchInputs},
    {"relay", "Passes a trigger input on to its outputs, unless switched off", EntityShape::Point,
     kRelayProperties, kRelayEvents, kRelayInputs},
    {"counter", "Fires an event after a number of add inputs", EntityShape::Point,
     kCounterProperties, kCounterEvents, kCounterInputs},
    {"timer", "Fires an event at intervals while running", EntityShape::Point, kTimerProperties,
     kTimerEvents, kTimerInputs},
    {"mover",
     "Brushes that slide between where they are built and an end position: doors, lifts, "
     "platforms, crushers",
     EntityShape::Brush, kMoverProperties, kMoverEvents, kMoverInputs},
    {kPickupClass, "An item players take by walking into it: health, armour, ammo, a weapon",
     EntityShape::Point, kPickupProperties, kPickupEvents, kPickupInputs},
    {kEnemyClass, "An enemy that hunts players once it notices them", EntityShape::Point,
     kEnemyProperties, kEnemyEvents, kEnemyInputs},
};

constexpr PresetProperty kDoorPreset[] = {{"activate", "touch"}, {"speed", "3"}, {"wait", "3"}};
constexpr PresetProperty kLiftPreset[] = {{"activate", "stand"}, {"speed", "2"}, {"wait", "3"}};
constexpr PresetProperty kPlatformPreset[] = {
    {"speed", "2"}, {"wait", "1"}, {"loop", "1"}, {"start_moving", "1"}};

constexpr PresetProperty kKillPreset[] = {{"damage", "100000"}};

constexpr PresetProperty kHealthPreset[] = {{"item", "health"}};
constexpr PresetProperty kMegaHealthPreset[] = {
    {"item", "health"}, {"amount", "100"}, {"limit", "200"}, {"respawn", "35"}};
constexpr PresetProperty kArmourPreset[] = {{"item", "armour"}};
constexpr PresetProperty kHeavyArmourPreset[] = {
    {"item", "armour"}, {"amount", "100"}, {"limit", "200"}};
constexpr PresetProperty kAmmoPreset[] = {{"item", "ammo"}, {"respawn", "40"}};
constexpr PresetProperty kWeaponPreset[] = {{"item", "weapon"}, {"respawn", "5"}};

constexpr EntityPreset kPresets[] = {
    {"Door",
     "Slides up out of the way when a player walks into it, and shuts again",
     "mover",
     kDoorPreset,
     {},
     true,
     {2.0f, 0.25f, 3.0f}},
    {"Lift",
     "Rises when a player stands on it, and comes back down",
     "mover",
     kLiftPreset,
     {0.0f, 0.0f, 4.0f},
     false,
     {2.0f, 2.0f, 0.25f}},
    {"Platform",
     "Shuttles back and forth by itself",
     "mover",
     kPlatformPreset,
     {6.0f, 0.0f, 0.0f},
     false,
     {2.0f, 2.0f, 0.25f}},
    {"Trigger",
     "An invisible volume that fires events when players go in and out",
     "trigger",
     {},
     {},
     false,
     {2.0f, 2.0f, 2.0f}},
    {"Checkpoint",
     "Saves the game the first time a player walks into it",
     "checkpoint",
     {},
     {},
     false,
     {4.0f, 4.0f, 2.0f}},
    {"Hurt",
     "Hurts whatever is inside it, every half second: lava, slime",
     "hurt",
     {},
     {},
     false,
     {4.0f, 4.0f, 1.0f}},
    {"Kill",
     "Kills whatever comes into it: a bottomless pit",
     "hurt",
     kKillPreset,
     {},
     false,
     {4.0f, 4.0f, 1.0f}},
    {"Health", "Gives 25 health, up to the maximum", kPickupClass, kHealthPreset, {}, false, {}},
    {"Mega health",
     "Gives 100 health, up to twice the maximum; the extra drains away",
     kPickupClass,
     kMegaHealthPreset,
     {},
     false,
     {}},
    {"Armour", "Gives 50 armour, up to the maximum", kPickupClass, kArmourPreset, {}, false, {}},
    {"Heavy armour",
     "Gives 100 armour, up to twice the maximum; the extra drains away",
     kPickupClass,
     kHeavyArmourPreset,
     {},
     false,
     {}},
    {"Ammo",
     "A box of ammo for the weapon it names (set weapon in the inspector)",
     kPickupClass,
     kAmmoPreset,
     {},
     false,
     {}},
    {"Weapon",
     "The weapon it names, with some ammo (set weapon in the inspector)",
     kPickupClass,
     kWeaponPreset,
     {},
     false,
     {}},
    {"Enemy",
     "The game's first kind of enemy, standing here (set type in the inspector)",
     kEnemyClass,
     {},
     {},
     false,
     {}},
};

const PropertyInfo& requireProperty(const EntityClassInfo& entityClass, std::string_view key,
                                    PropertyType type) {
    const PropertyInfo* property = findPropertyInfo(entityClass, key);
    CT_ASSERT(property != nullptr && property->type == type);
    return *property;
}

} // namespace

std::span<const EntityClassInfo> entityClasses() { return kClasses; }

std::span<const EntityPreset> entityPresets() { return kPresets; }

LevelEntity makePresetEntity(const EntityPreset& preset) {
    LevelEntity entity{std::string(preset.className), {}};
    for (const PresetProperty& property : preset.properties) {
        entity.properties.push_back({std::string(property.key), std::string(property.value)});
    }
    return entity;
}

const EntityClassInfo* findEntityClass(std::string_view name) {
    for (const EntityClassInfo& entityClass : kClasses) {
        if (entityClass.name == name) {
            return &entityClass;
        }
    }
    return nullptr;
}

const PropertyInfo* findPropertyInfo(const EntityClassInfo& entityClass, std::string_view key) {
    for (const PropertyInfo& property : entityClass.properties) {
        if (property.key == key) {
            return &property;
        }
    }
    return nullptr;
}

const SignalInfo* findSignal(std::span<const SignalInfo> signals, std::string_view name) {
    for (const SignalInfo& signal : signals) {
        if (signal.name == name) {
            return &signal;
        }
    }
    return nullptr;
}

std::string_view propertyValue(const LevelEntity& entity, const PropertyInfo& property) {
    const std::string* value = findProperty(entity, property.key);
    return value != nullptr ? std::string_view(*value) : property.defaultValue;
}

bool checkPropertyValue(const PropertyInfo& property, std::string_view value,
                        std::string& problem) {
    switch (property.type) {
    case Text:
        return true;
    case Name:
        if (!value.empty() && !isValidEntityName(value)) {
            problem = std::format("{} must be letters, digits, '_', '.' or '-'", property.key);
            return false;
        }
        return true;
    case Number:
    case Count: {
        float number = 0.0f;
        const bool whole = property.type == Count;
        if (!parseFloat(value, number) || (whole && number != std::floor(number)) ||
            !(number >= property.minimum && number <= property.maximum)) {
            problem =
                std::format("{} must be a {} from {} to {}", property.key,
                            whole ? "whole number" : "number", property.minimum, property.maximum);
            return false;
        }
        return true;
    }
    case Flag:
        if (value != "0" && value != "1") {
            problem = std::format("{} must be 0 or 1", property.key);
            return false;
        }
        return true;
    case Vector: {
        const std::optional<Vec3> vector = parseVec3(value);
        if (!vector || std::fabs(vector->x) > kMaxWorldCoordinate ||
            std::fabs(vector->y) > kMaxWorldCoordinate ||
            std::fabs(vector->z) > kMaxWorldCoordinate) {
            problem = std::format("{} must be three numbers inside the world: x y z", property.key);
            return false;
        }
        return true;
    }
    case Choice:
        if (std::find(property.choices.begin(), property.choices.end(), value) ==
            property.choices.end()) {
            std::string list;
            for (const std::string_view choice : property.choices) {
                list += list.empty() ? "" : ", ";
                list += choice;
            }
            problem = std::format("{} must be one of: {}", property.key, list);
            return false;
        }
        return true;
    case Weapon:
    case Enemy:
        if (!value.empty() && !std::all_of(value.begin(), value.end(), [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
            })) {
            problem =
                std::format("{} must be {} name: lowercase letters, digits, '-' or '_'",
                            property.key, property.type == Weapon ? "a weapon's" : "an enemy's");
            return false;
        }
        return true;
    case Angle: {
        float degrees = 0.0f;
        if (!parseFloat(value, degrees) || std::fabs(degrees) > kMaxAngleDegrees) {
            problem = std::format("{} must be a number of degrees", property.key);
            return false;
        }
        return true;
    }
    }
    return true;
}

bool checkEntityClassProperties(const LevelEntity& entity, const EntityClassInfo& entityClass,
                                std::string& problem) {
    for (const PropertyInfo& property : entityClass.properties) {
        if (!checkPropertyValue(property, propertyValue(entity, property), problem)) {
            return false;
        }
    }
    for (const EntityOutput& output : entity.outputs) {
        if (findSignal(entityClass.events, output.event) == nullptr) {
            problem = std::format("{} has no event '{}'", entityClass.name, output.event);
            return false;
        }
    }
    return true;
}

float numberProperty(const LevelEntity& entity, const EntityClassInfo& entityClass,
                     std::string_view key) {
    float value = 0.0f;
    parseFloat(propertyValue(entity, requireProperty(entityClass, key, Number)), value);
    return value;
}

float angleProperty(const LevelEntity& entity, const EntityClassInfo& entityClass,
                    std::string_view key) {
    float value = 0.0f;
    parseFloat(propertyValue(entity, requireProperty(entityClass, key, Angle)), value);
    return value;
}

std::uint32_t countProperty(const LevelEntity& entity, const EntityClassInfo& entityClass,
                            std::string_view key) {
    const PropertyInfo& property = requireProperty(entityClass, key, Count);
    float value = 0.0f;
    if (!parseFloat(propertyValue(entity, property), value) ||
        !(value >= property.minimum && value <= property.maximum)) {
        return 0;
    }
    return static_cast<std::uint32_t>(value);
}

bool flagProperty(const LevelEntity& entity, const EntityClassInfo& entityClass,
                  std::string_view key) {
    return propertyValue(entity, requireProperty(entityClass, key, Flag)) == "1";
}

Vec3 vectorProperty(const LevelEntity& entity, const EntityClassInfo& entityClass,
                    std::string_view key) {
    return parseVec3(propertyValue(entity, requireProperty(entityClass, key, Vector)))
        .value_or(Vec3{});
}

std::size_t choiceProperty(const LevelEntity& entity, const EntityClassInfo& entityClass,
                           std::string_view key) {
    const PropertyInfo& property = requireProperty(entityClass, key, Choice);
    const auto found = std::find(property.choices.begin(), property.choices.end(),
                                 propertyValue(entity, property));
    return found == property.choices.end()
               ? 0
               : static_cast<std::size_t>(found - property.choices.begin());
}

std::string_view textProperty(const LevelEntity& entity, const EntityClassInfo& entityClass,
                              std::string_view key) {
    const PropertyInfo* property = findPropertyInfo(entityClass, key);
    CT_ASSERT(property != nullptr && (property->type == Text || property->type == Name ||
                                      property->type == Weapon || property->type == Enemy));
    return propertyValue(entity, *property);
}

LevelEntity makeEntity(const EntityClassInfo& entityClass) {
    return {std::string(entityClass.name), {}};
}

} // namespace ct
