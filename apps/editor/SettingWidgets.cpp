#include "SettingWidgets.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ct {
namespace {

constexpr std::string_view kLookKeys[] = {"description", "colour", "sounds"};
constexpr std::string_view kBodyKeys[] = {"health",       "width",      "height",
                                          "eye_height",   "speed",      "walk_speed",
                                          "acceleration", "turn_speed", "corpse_time"};
constexpr std::string_view kSenseKeys[] = {"sight_range", "sight_angle", "reaction_time",
                                           "hearing_range", "footstep_range"};
constexpr std::string_view kMeleeKeys[] = {"melee_damage", "melee_range", "melee_windup",
                                           "melee_interval", "melee_knockback"};
constexpr std::string_view kRangedKeys[] = {"weapon", "attack_range", "keep_distance", "aim_error",
                                            "burst",  "burst_pause",  "strafe_time"};
constexpr std::string_view kSearchKeys[] = {"search_time", "search_radius", "cover_chance",
                                            "cover_time", "cover_range"};
constexpr SettingGroup kEnemyGroups[] = {
    {"Look and sound", kLookKeys},  {"Body and movement", kBodyKeys},
    {"Senses", kSenseKeys},         {"Melee attack", kMeleeKeys},
    {"Ranged attack", kRangedKeys}, {"Searching and cover", kSearchKeys},
};

// Decimal places that give three significant figures, for a value of this size.
int decimalsFor(float value) {
    const float size = std::fabs(value);
    if (!(size >= 0.001f)) {
        return 2;
    }
    const int digits = static_cast<int>(std::floor(std::log10(size)));
    return std::clamp(2 - digits, 0, 3);
}

} // namespace

std::span<const SettingGroup> enemySettingGroups() { return kEnemyGroups; }

std::string_view enemySettingGroup(std::string_view key) {
    for (const SettingGroup& group : kEnemyGroups) {
        if (std::find(group.keys.begin(), group.keys.end(), key) != group.keys.end()) {
            return group.title;
        }
    }
    return "Other";
}

bool dragNumber(const char* label, float& value, float minimum, float maximum) {
    // As ImGui takes them: equal limits are none.
    // ImGui rounds a dragged value to the format and carries the rest over, so slow drags still
    // move it.
    const int decimals = decimalsFor(value);
    char format[8];
    std::snprintf(format, sizeof format, "%%.%df", decimals);
    const float step = std::pow(10.0f, static_cast<float>(-decimals));
    const float speed = std::max(std::fabs(value) * 0.005f, step * 0.5f);
    return ImGui::DragFloat(label, &value, speed, minimum, maximum, format,
                            ImGuiSliderFlags_AlwaysClamp);
}

bool dragWholeNumber(const char* label, int& value, int minimum, int maximum) {
    const float speed = std::max(std::fabs(static_cast<float>(value)) * 0.005f, 0.05f);
    return ImGui::DragInt(label, &value, speed, minimum, maximum, "%d",
                          ImGuiSliderFlags_AlwaysClamp);
}

} // namespace ct
