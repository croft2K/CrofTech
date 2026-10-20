#include "croftech/sim/MovementConfig.h"

#include "croftech/core/Assert.h"

#include <string>

namespace ct {
namespace {

struct FloatSetting {
    std::string_view key;
    float MovementSettings::* field;
    std::string_view description;
};

constexpr FloatSetting kFloatSettings[] = {
    {"gravity", &MovementSettings::gravity, "m/s^2"},
    {"max_ground_speed", &MovementSettings::maxGroundSpeed, "top running speed, m/s"},
    {"ground_acceleration", &MovementSettings::groundAcceleration,
     "how quickly you reach top speed (fraction of it per second)"},
    {"air_acceleration", &MovementSettings::airAcceleration, "steering strength in the air"},
    {"air_wish_speed_cap", &MovementSettings::airWishSpeedCap,
     "low cap + high air_acceleration = strafe-jumping and air control"},
    {"friction", &MovementSettings::friction, "how quickly you stop"},
    {"stop_speed", &MovementSettings::stopSpeed,
     "below this speed friction bites as if moving this fast"},
    {"jump_speed", &MovementSettings::jumpSpeed,
     "upward speed at take-off; jump height = jump_speed^2 / (2 * gravity)"},
    {"step_height", &MovementSettings::stepHeight, "tallest ledge you walk up without jumping"},
    {"min_ground_normal_z", &MovementSettings::minGroundNormalZ,
     "steepest standable slope, as the cosine of its angle (0.7 is about 45 degrees)"},
    {"eye_height", &MovementSettings::eyeHeight, "above the feet"},
};

// The collision box must have some size.
constexpr float kMinPlayerSize = 0.01f;

std::string cvarName(std::string_view key) { return std::string(kMovementCVarPrefix).append(key); }

float floatValue(const Console& console, std::string_view key) {
    const CVar* cvar = console.find(cvarName(key));
    CT_ASSERT(cvar != nullptr);
    return cvar->asFloat();
}

} // namespace

void registerMovementCVars(Console& console) {
    const MovementSettings defaults;
    for (const FloatSetting& setting : kFloatSettings) {
        console.add(CVar::makeFloat(cvarName(setting.key), defaults.*setting.field, 0.0f,
                                    CVar::kNoLimit, std::string(setting.description)));
    }
    console.add(CVar::makeFloat(cvarName("player_width"), defaults.halfExtents.x * 2.0f,
                                kMinPlayerSize, CVar::kNoLimit, "collision box width, m"));
    console.add(CVar::makeFloat(cvarName("player_height"), defaults.halfExtents.z * 2.0f,
                                kMinPlayerSize, CVar::kNoLimit, "collision box height, m"));
    console.add(CVar::makeBool(cvarName("auto_jump"), defaults.autoJump,
                               "1 = holding jump bunny-hops automatically"));
}

MovementSettings movementSettingsFromCVars(const Console& console) {
    MovementSettings settings;
    for (const FloatSetting& setting : kFloatSettings) {
        settings.*setting.field = floatValue(console, setting.key);
    }
    const float halfWidth = floatValue(console, "player_width") * 0.5f;
    settings.halfExtents = {halfWidth, halfWidth, floatValue(console, "player_height") * 0.5f};
    settings.autoJump = floatValue(console, "auto_jump") != 0.0f;
    return settings;
}

} // namespace ct
