#include "croftech/level/LevelFile.h"

#include "croftech/core/ConfigFile.h"

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <system_error>

namespace ct {
namespace {

constexpr std::string_view kHeader = "croftech_level";

bool isSpace(char c) { return c == ' ' || c == '\t'; }

std::string_view trim(std::string_view text) {
    while (!text.empty() && isSpace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && isSpace(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

// Removes and returns the first word; text is left trimmed.
std::string_view takeWord(std::string_view& text) {
    std::size_t end = 0;
    while (end < text.size() && !isSpace(text[end])) {
        ++end;
    }
    const std::string_view word = text.substr(0, end);
    text = trim(text.substr(end));
    return word;
}

std::string pathText(const std::filesystem::path& path) {
    std::filesystem::path copy = path;
    return copy.make_preferred().string();
}

} // namespace

std::optional<Level> parseLevel(std::string_view text, std::string& error) {
    enum class Block { None, Brush, Entity };

    Level level;
    Block block = Block::None;
    int blockLine = 0;
    int lineNumber = 0;
    bool sawHeader = false;
    Brush brush;
    LevelEntity entity;
    const auto fail = [&error](int line, std::string_view message) -> std::optional<Level> {
        error = std::format("line {}: {}", line, message);
        return std::nullopt;
    };

    while (!text.empty()) {
        ++lineNumber;
        const std::size_t newline = text.find('\n');
        std::string_view line = text.substr(0, newline);
        text.remove_prefix(newline == std::string_view::npos ? text.size() : newline + 1);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        line = trim(line);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        std::string_view rest = line;
        const std::string_view word = takeWord(rest);

        if (!sawHeader) {
            if (word != kHeader) {
                return fail(lineNumber, std::format("not a level file (expected '{} {}')", kHeader,
                                                    kLevelFileVersion));
            }
            if (rest != std::to_string(kLevelFileVersion)) {
                return fail(lineNumber, "unsupported level file version");
            }
            sawHeader = true;
            continue;
        }

        switch (block) {
        case Block::None:
            if (word == "brush" && rest.empty()) {
                if (level.brushes.size() >= kMaxLevelBrushes) {
                    return fail(lineNumber, std::format("more than {} brushes", kMaxLevelBrushes));
                }
                block = Block::Brush;
                blockLine = lineNumber;
                brush.planes.clear();
            } else if (word == "entity") {
                if (level.entities.size() >= kMaxLevelEntities) {
                    return fail(lineNumber,
                                std::format("more than {} entities", kMaxLevelEntities));
                }
                if (!isValidEntityName(rest)) {
                    return fail(lineNumber, "invalid entity class name");
                }
                block = Block::Entity;
                blockLine = lineNumber;
                entity = {std::string(rest), {}};
            } else {
                return fail(lineNumber, "expected 'brush' or 'entity'");
            }
            break;

        case Block::Brush:
            if (word == "end" && rest.empty()) {
                std::string problem;
                if (!checkBrush(brush, problem)) {
                    return fail(blockLine, problem);
                }
                level.brushes.push_back(std::move(brush));
                brush = {};
                block = Block::None;
            } else if (word == "plane") {
                std::array<float, 4> values{};
                if (!parseFloats(rest, values)) {
                    return fail(lineNumber, "a plane is four numbers: normal x y z, distance");
                }
                if (brush.planes.size() >= kMaxBrushPlanes) {
                    return fail(lineNumber, std::format("more than {} planes", kMaxBrushPlanes));
                }
                brush.planes.push_back({{values[0], values[1], values[2]}, values[3]});
            } else {
                return fail(lineNumber, "expected 'plane' or 'end'");
            }
            break;

        case Block::Entity:
            if (word == "end" && rest.empty()) {
                std::string problem;
                if (!checkEntity(entity, problem)) {
                    return fail(blockLine, problem);
                }
                level.entities.push_back(std::move(entity));
                entity = {};
                block = Block::None;
            } else {
                if (!isValidEntityName(word)) {
                    return fail(lineNumber, "invalid property key");
                }
                if (entity.properties.size() >= kMaxEntityProperties) {
                    return fail(lineNumber,
                                std::format("more than {} properties", kMaxEntityProperties));
                }
                entity.properties.push_back({std::string(word), std::string(rest)});
            }
            break;
        }
    }

    if (!sawHeader) {
        return fail(std::max(lineNumber, 1), "not a level file (it is empty)");
    }
    if (block != Block::None) {
        return fail(blockLine,
                    block == Block::Brush ? "brush has no 'end'" : "entity has no 'end'");
    }
    return level;
}

std::optional<std::string> formatLevel(const Level& level, std::string& error) {
    if (level.brushes.size() > kMaxLevelBrushes || level.entities.size() > kMaxLevelEntities) {
        error = "level has too many brushes or entities";
        return std::nullopt;
    }
    std::string text = std::format("{} {}\n", kHeader, kLevelFileVersion);
    std::string problem;
    for (std::size_t i = 0; i < level.brushes.size(); ++i) {
        const Brush& brush = level.brushes[i];
        if (!checkBrush(brush, problem)) {
            error = std::format("brush {}: {}", i, problem);
            return std::nullopt;
        }
        text += "\nbrush\n";
        for (const Plane& plane : brush.planes) {
            text += std::format("    plane {} {} {} {}\n", formatFloat(plane.normal.x),
                                formatFloat(plane.normal.y), formatFloat(plane.normal.z),
                                formatFloat(plane.distance));
        }
        text += "end\n";
    }
    for (std::size_t i = 0; i < level.entities.size(); ++i) {
        const LevelEntity& entity = level.entities[i];
        if (!checkEntity(entity, problem)) {
            error = std::format("entity {} ({}): {}", i, entity.className, problem);
            return std::nullopt;
        }
        text += std::format("\nentity {}\n", entity.className);
        for (const EntityProperty& property : entity.properties) {
            text += property.value.empty()
                        ? std::format("    {}\n", property.key)
                        : std::format("    {} {}\n", property.key, property.value);
        }
        text += "end\n";
    }
    return text;
}

std::optional<Level> loadLevelFile(const std::filesystem::path& path, std::string& error) {
    const std::optional<std::string> text = readTextFile(path, kMaxLevelFileBytes);
    if (!text) {
        error = std::format("{}: cannot read (missing, unreadable or over {} MiB)", pathText(path),
                            kMaxLevelFileBytes >> 20);
        return std::nullopt;
    }
    std::optional<Level> level = parseLevel(*text, error);
    if (!level) {
        error = std::format("{}: {}", pathText(path), error);
    }
    return level;
}

bool saveLevelFile(const Level& level, const std::filesystem::path& path, std::string& error) {
    const std::optional<std::string> text = formatLevel(level, error);
    if (!text) {
        return false;
    }
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.write(text->data(), static_cast<std::streamsize>(text->size()));
        file.close();
        if (!file) {
            error = std::format("{}: cannot write", pathText(temporary));
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return false;
        }
    }
    std::error_code renameError;
    std::filesystem::rename(temporary, path, renameError);
    if (renameError) {
        error = std::format("{}: {}", pathText(path), renameError.message());
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    return true;
}

} // namespace ct
