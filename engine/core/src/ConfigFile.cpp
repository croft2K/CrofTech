#include "croftech/core/ConfigFile.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <system_error>

namespace ct {
namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }

std::string_view trim(std::string_view text) {
    while (!text.empty() && isSpace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && isSpace(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

} // namespace

std::vector<ConfigEntry> parseConfig(std::string_view text) {
    std::vector<ConfigEntry> entries;
    int lineNumber = 0;
    while (!text.empty()) {
        ++lineNumber;
        const std::size_t newline = text.find('\n');
        std::string_view line = text.substr(0, newline);
        text.remove_prefix(newline == std::string_view::npos ? text.size() : newline + 1);

        line = trim(line.substr(0, line.find('#')));
        if (line.empty()) {
            continue;
        }
        std::size_t split = 0;
        while (split < line.size() && !isSpace(line[split])) {
            ++split;
        }
        entries.push_back({std::string(line.substr(0, split)),
                           std::string(trim(line.substr(split))), lineNumber});
    }
    return entries;
}

bool parseFloat(std::string_view text, float& value) {
    float parsed = 0.0f;
    const char* end = text.data() + text.size();
    const auto [ptr, error] = std::from_chars(text.data(), end, parsed);
    if (error != std::errc{} || ptr != end || !std::isfinite(parsed)) {
        return false;
    }
    value = parsed;
    return true;
}

bool parseBool(std::string_view text, bool& value) {
    if (text == "1" || text == "true" || text == "on") {
        value = true;
        return true;
    }
    if (text == "0" || text == "false" || text == "off") {
        value = false;
        return true;
    }
    return false;
}

std::optional<std::string> readTextFile(const std::filesystem::path& path, std::size_t maxBytes) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size > maxBytes) {
        return std::nullopt;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

} // namespace ct
