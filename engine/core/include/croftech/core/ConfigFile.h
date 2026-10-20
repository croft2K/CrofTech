#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
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

// Reads a whole text file. Fails for files over maxBytes.
std::optional<std::string> readTextFile(const std::filesystem::path& path,
                                        std::size_t maxBytes = 1u << 20);

} // namespace ct
