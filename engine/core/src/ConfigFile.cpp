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

namespace {

// Where a new value starts when a line has no gap to keep, as in the hand-written files.
constexpr std::size_t kValueColumn = 16;

// One line of a config file, split up so a tool can change the value and keep the rest.
struct ConfigLine {
    std::string_view indent;
    std::string_view key;
    std::string_view gap; // between the key and the value
    std::string_view value;
    std::string_view comment; // from the '#', or empty
    std::size_t commentColumn = 0;
    std::string_view ending; // "\n", "\r\n" or nothing on the last line
};

ConfigLine splitLine(std::string_view line) {
    ConfigLine split;
    if (line.ends_with("\r\n")) {
        split.ending = line.substr(line.size() - 2);
    } else if (line.ends_with('\n')) {
        split.ending = line.substr(line.size() - 1);
    }
    line.remove_suffix(split.ending.size());
    const std::size_t hash = line.find('#');
    if (hash != std::string_view::npos) {
        split.comment = line.substr(hash);
        split.commentColumn = hash;
        line = line.substr(0, hash);
    }
    std::size_t at = 0;
    while (at < line.size() && isSpace(line[at])) {
        ++at;
    }
    split.indent = line.substr(0, at);
    std::size_t end = at;
    while (end < line.size() && !isSpace(line[end])) {
        ++end;
    }
    split.key = line.substr(at, end - at);
    std::size_t valueStart = end;
    while (valueStart < line.size() && isSpace(line[valueStart])) {
        ++valueStart;
    }
    split.gap = line.substr(end, valueStart - end);
    split.value = trim(line.substr(valueStart));
    return split;
}

// The text's lines, each with its line ending.
std::vector<std::string_view> splitLines(std::string_view text) {
    std::vector<std::string_view> lines;
    while (!text.empty()) {
        const std::size_t newline = text.find('\n');
        const std::size_t length = newline == std::string_view::npos ? text.size() : newline + 1;
        lines.push_back(text.substr(0, length));
        text.remove_prefix(length);
    }
    return lines;
}

} // namespace

bool isConfigValue(std::string_view value) {
    return value.find_first_of("#\r\n") == std::string_view::npos;
}

std::string setConfigValue(std::string_view text, std::string_view key, std::string_view value) {
    value = trim(value);
    const std::vector<std::string_view> lines = splitLines(text);
    std::size_t last = lines.size();
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (splitLine(lines[i]).key == key) {
            last = i;
        }
    }
    std::string result;
    result.reserve(text.size() + key.size() + value.size() + kValueColumn);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i != last) {
            result += lines[i];
            continue;
        }
        const ConfigLine line = splitLine(lines[i]);
        result += line.indent;
        result += line.key;
        if (!value.empty()) {
            const std::size_t column = line.indent.size() + line.key.size();
            if (!line.gap.empty()) {
                result += line.gap;
            } else {
                result.append(column < kValueColumn ? kValueColumn - column : 1, ' ');
            }
            result += value;
        }
        if (!line.comment.empty()) {
            const std::size_t lineStart = result.rfind('\n') + 1; // npos + 1 is 0
            const std::size_t column = result.size() - lineStart;
            result.append(line.commentColumn > column + 1 ? line.commentColumn - column : 2, ' ');
            result += line.comment;
        }
        result += line.ending;
    }
    if (last == lines.size()) {
        if (!result.empty() && !result.ends_with('\n')) {
            result += text.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";
        }
        result += key;
        if (!value.empty()) {
            result.append(key.size() < kValueColumn ? kValueColumn - key.size() : 1, ' ');
            result += value;
        }
        result += text.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";
    }
    return result;
}

std::string removeConfigValue(std::string_view text, std::string_view key) {
    std::string result;
    result.reserve(text.size());
    for (const std::string_view line : splitLines(text)) {
        if (splitLine(line).key != key) {
            result += line;
        }
    }
    return result;
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
