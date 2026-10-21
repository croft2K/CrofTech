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

constexpr PropertyInfo kDoorProperties[] = {
    kName,
    {"move", Vector, "0 0 0", "How far it moves to open, metres (x y z)"},
    {"speed", Number, "3", "Metres per second", 0.01f, 100.0f},
    {"wait", Number, "3", "Seconds it stays open before closing; -1 stays open", -1.0f, 3600.0f},
    {"touch_open", Flag, "1", "Opens when a player walks into it"},
    {"start_open", Flag, "0", "Starts open"},
};
constexpr SignalInfo kDoorEvents[] = {
    {"on_open", "Started opening"},
    {"on_opened", "Finished opening"},
    {"on_close", "Started closing"},
    {"on_closed", "Finished closing"},
};
constexpr SignalInfo kDoorInputs[] = {
    {"open", "Open; it closes again after its wait"},
    {"close", "Close"},
    {"toggle", "Open if closed or closing, close if open or opening"},
};

constexpr PropertyInfo kMoverProperties[] = {
    kName,
    {"move", Vector, "0 0 0", "From the start position to the end position, metres (x y z)"},
    {"speed", Number, "2", "Metres per second", 0.01f, 100.0f},
    {"wait", Number, "1", "Seconds it waits at each end before going back, with loop", 0.0f,
     3600.0f},
    {"loop", Flag, "0", "Keeps going back and forth between the ends"},
    {"start_on", Flag, "0", "Starts moving towards the end when the level starts"},
};
constexpr SignalInfo kMoverEvents[] = {
    {"on_move", "Started moving"},
    {"on_reached_end", "Arrived at the end position"},
    {"on_reached_start", "Arrived back at the start position"},
};
constexpr SignalInfo kMoverInputs[] = {
    {"to_end", "Move to the end position"},
    {"to_start", "Move back to the start position"},
    {"toggle", "Turn round"},
    {"stop", "Stop where it is"},
};

constexpr EntityClassInfo kClasses[] = {
    {kPlayerSpawnClass, "Where a player starts", EntityShape::Point, kSpawnProperties, {}, {}},
    {"trigger", "An invisible volume that fires events when players go in and out",
     EntityShape::Brush, kTriggerProperties, kTriggerEvents, kSwitchInputs},
    {"relay", "Passes a trigger input on to its outputs, unless switched off", EntityShape::Point,
     kRelayProperties, kRelayEvents, kRelayInputs},
    {"counter", "Fires an event after a number of add inputs", EntityShape::Point,
     kCounterProperties, kCounterEvents, kCounterInputs},
    {"timer", "Fires an event at intervals while running", EntityShape::Point, kTimerProperties,
     kTimerEvents, kTimerInputs},
    {"door", "Brushes that slide open and closed", EntityShape::Brush, kDoorProperties, kDoorEvents,
     kDoorInputs},
    {"mover", "Brushes that move between two positions: lifts, platforms, crushers",
     EntityShape::Brush, kMoverProperties, kMoverEvents, kMoverInputs},
};

const PropertyInfo& requireProperty(const EntityClassInfo& entityClass, std::string_view key,
                                    PropertyType type) {
    const PropertyInfo* property = findPropertyInfo(entityClass, key);
    CT_ASSERT(property != nullptr && property->type == type);
    return *property;
}

} // namespace

std::span<const EntityClassInfo> entityClasses() { return kClasses; }

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

std::string_view textProperty(const LevelEntity& entity, const EntityClassInfo& entityClass,
                              std::string_view key) {
    const PropertyInfo* property = findPropertyInfo(entityClass, key);
    CT_ASSERT(property != nullptr && (property->type == Text || property->type == Name));
    return propertyValue(entity, *property);
}

LevelEntity makeEntity(const EntityClassInfo& entityClass) {
    return {std::string(entityClass.name), {}};
}

} // namespace ct
