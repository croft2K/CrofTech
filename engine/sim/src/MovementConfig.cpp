#include "croftech/sim/MovementConfig.h"

#include <format>
#include <string_view>

namespace ct {
namespace {

struct FloatSetting {
    std::string_view key;
    float MovementSettings::* field;
};

constexpr FloatSetting kFloatSettings[] = {
    {"gravity", &MovementSettings::gravity},
    {"max_ground_speed", &MovementSettings::maxGroundSpeed},
    {"ground_acceleration", &MovementSettings::groundAcceleration},
    {"air_acceleration", &MovementSettings::airAcceleration},
    {"air_wish_speed_cap", &MovementSettings::airWishSpeedCap},
    {"friction", &MovementSettings::friction},
    {"stop_speed", &MovementSettings::stopSpeed},
    {"jump_speed", &MovementSettings::jumpSpeed},
    {"step_height", &MovementSettings::stepHeight},
    {"min_ground_normal_z", &MovementSettings::minGroundNormalZ},
    {"eye_height", &MovementSettings::eyeHeight},
};

// Returns false if the key is not a movement setting.
bool applyEntry(const ConfigEntry& entry, MovementSettings& settings, bool& valid) {
    if (entry.key == "auto_jump") {
        valid = parseBool(entry.value, settings.autoJump);
        return true;
    }

    float value = 0.0f;
    valid = parseFloat(entry.value, value) && value >= 0.0f;

    if (entry.key == "player_width" || entry.key == "player_height") {
        valid = valid && value > 0.0f;
        if (valid && entry.key == "player_width") {
            settings.halfExtents.x = value * 0.5f;
            settings.halfExtents.y = value * 0.5f;
        } else if (valid) {
            settings.halfExtents.z = value * 0.5f;
        }
        return true;
    }

    for (const FloatSetting& setting : kFloatSettings) {
        if (entry.key == setting.key) {
            if (valid) {
                settings.*setting.field = value;
            }
            return true;
        }
    }
    return false;
}

} // namespace

std::vector<std::string> applyMovementConfig(std::span<const ConfigEntry> entries,
                                             MovementSettings& settings) {
    std::vector<std::string> problems;
    for (const ConfigEntry& entry : entries) {
        bool valid = true;
        if (!applyEntry(entry, settings, valid)) {
            problems.push_back(std::format("line {}: unknown setting '{}'", entry.line, entry.key));
        } else if (!valid) {
            problems.push_back(std::format("line {}: invalid value '{}' for {}", entry.line,
                                           entry.value, entry.key));
        }
    }
    return problems;
}

} // namespace ct
