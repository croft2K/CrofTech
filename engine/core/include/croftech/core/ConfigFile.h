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
