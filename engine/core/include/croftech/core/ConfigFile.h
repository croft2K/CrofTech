#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ct {

// "key value" settings files: one setting per line, '#' starts a comment, blank lines ignored.
struct ConfigEntry {
    std::string key;
    std::string value;
    int line = 0;
};

std::vector<ConfigEntry> parseConfig(std::string_view text);

// Edits for tools that write config files, keeping everything else as it was: comments, blank
// lines, the order and the layout.
//
// Whether the text can be a value: one line with no '#', which would start a comment.
bool isConfigValue(std::string_view value);
// The text with the key set to the value (isConfigValue). The key's last line, the one that wins,
// is changed in place, keeping its comment in the same column where the value leaves room; a key
// with no line gets a new one at the end.
std::string setConfigValue(std::string_view text, std::string_view key, std::string_view value);
// The text without any of the key's lines.
std::string removeConfigValue(std::string_view text, std::string_view key);

// Strict parsers: the whole text must be a valid, finite value.
bool parseFloat(std::string_view text, float& value);
bool parseBool(std::string_view text, bool& value); // 1/0, true/false, on/off

// Exactly values.size() floats separated by spaces or tabs.
bool parseFloats(std::string_view text, std::span<float> values);

// The shortest text that parses back to exactly the same float. Negative zero is written as "0".
std::string formatFloat(float value);

// Reads a whole text file. Fails for files over maxBytes.
std::optional<std::string> readTextFile(const std::filesystem::path& path,
                                        std::size_t maxBytes = 1u << 20);

// Writes a temporary file and renames it over the target, so a failed save never leaves a
// half-written file behind.
bool writeTextFile(const std::filesystem::path& path, std::string_view text, std::string& error);

} // namespace ct
