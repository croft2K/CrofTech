#pragma once

#include <span>
#include <string_view>

namespace ct {

// Widgets the Game data window and the inspector share for the game's settings.

// An enemy type's settings, in the groups both show them in. Settings in none go under "Other".
struct SettingGroup {
    std::string_view title;
    std::span<const std::string_view> keys;
};
std::span<const SettingGroup> enemySettingGroups();
// The group the key is in, or "Other".
std::string_view enemySettingGroup(std::string_view key);

// A number dragged in steps that suit its size, so dragging leaves round numbers (15.6, not
// 15.6465): three significant figures, and whole numbers from 100 up. Ctrl-click types one.
bool dragNumber(const char* label, float& value, float minimum, float maximum);
bool dragWholeNumber(const char* label, int& value, int minimum, int maximum);

} // namespace ct
