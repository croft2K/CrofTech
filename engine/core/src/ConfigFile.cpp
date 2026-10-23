#include "croftech/core/ConfigFile.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <format>
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

bool parseFloats(std::string_view text, std::span<float> values) {
    for (float& value : values) {
        text = trim(text);
        std::size_t end = 0;
        while (end < text.size() && !isSpace(text[end])) {
            ++end;
        }
        if (!parseFloat(text.substr(0, end), value)) {
            return false;
        }
        text.remove_prefix(end);
    }
    return trim(text).empty();
}

std::string formatFloat(float value) {
    if (value == 0.0f) {
        return "0";
    }
    char buffer[32];
    const auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return error == std::errc{} ? std::string(buffer, end) : std::string("0");
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

bool writeTextFile(const std::filesystem::path& path, std::string_view text, std::string& error) {
    const auto shown = [](std::filesystem::path shownPath) {
        return shownPath.make_preferred().string();
    };
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.close();
        if (!file) {
            error = std::format("{}: cannot write", shown(temporary));
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return false;
        }
    }
    std::error_code renameError;
    std::filesystem::rename(temporary, path, renameError);
    if (renameError) {
        error = std::format("{}: {}", shown(path), renameError.message());
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    return true;
}

} // namespace ct
