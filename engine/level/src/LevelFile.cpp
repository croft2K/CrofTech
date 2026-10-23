#include "croftech/level/LevelFile.h"

#include "croftech/core/ConfigFile.h"
#include "croftech/core/Hash.h"
#include "croftech/level/BrushGeometry.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <format>
#include <fstream>
#include <system_error>
#include <utility>
#include <vector>

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

// Vertex numbers separated by spaces: 3 to kMaxBrushVertices of them.
bool parseIndices(std::string_view text, std::vector<std::uint32_t>& indices) {
    while (!text.empty()) {
        const std::string_view word = takeWord(text);
        std::uint32_t value = 0;
        const auto [end, error] = std::from_chars(word.data(), word.data() + word.size(), value);
        if (error != std::errc{} || end != word.data() + word.size() ||
            indices.size() >= kMaxBrushVertices) {
            return false;
        }
        indices.push_back(value);
    }
    return indices.size() >= 3;
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
    int blockLine = 0; // where the current entity starts
    int brushLine = 0;
    int lineNumber = 0;
    bool sawHeader = false;
    int version = kLevelFileVersion;
    Brush brush;
    std::vector<DPlane> planes; // version 1
    LevelEntity entity;
    const auto fail = [&error](int line, std::string_view message) -> std::optional<Level> {
        error = std::format("line {}: {}", line, message);
        return std::nullopt;
    };
    // Groups by name, for the brushes and entities that name them.
    std::vector<std::pair<std::string, std::uint32_t>> groupNames;
    const auto findGroup = [&groupNames](std::string_view name) -> std::optional<std::uint32_t> {
        const auto found = std::lower_bound(
            groupNames.begin(), groupNames.end(), name,
            [](const auto& entry, std::string_view key) { return entry.first < key; });
        if (found == groupNames.end() || found->first != name) {
            return std::nullopt;
        }
        return found->second;
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
            if (rest == "1" || rest == "2" || rest == "3") {
                version = rest.front() - '0';
            } else if (rest != std::to_string(kLevelFileVersion)) {
                return fail(lineNumber, "unsupported level file version");
            }
            sawHeader = true;
            continue;
        }

        const auto startBrush = [&](std::uint32_t owner) -> bool {
            if (level.brushes.size() >= kMaxLevelBrushes) {
                return false;
            }
            block = Block::Brush;
            brushLine = lineNumber;
            brush = {};
            brush.entity = owner;
            planes.clear();
            return true;
        };

        switch (block) {
        case Block::None:
            if (word == "brush" && rest.empty()) {
                if (!startBrush(kNoEntity)) {
                    return fail(lineNumber, std::format("more than {} brushes", kMaxLevelBrushes));
                }
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
            } else if (version >= 4 && word == "group") {
                if (!isValidLabel(rest)) {
                    return fail(lineNumber, "invalid group name");
                }
                if (level.groups.size() >= kMaxLevelGroups) {
                    return fail(lineNumber, std::format("more than {} groups", kMaxLevelGroups));
                }
                if (findGroup(rest)) {
                    return fail(lineNumber, "two groups have the same name");
                }
                const auto position = std::lower_bound(
                    groupNames.begin(), groupNames.end(), rest,
                    [](const auto& entry, std::string_view key) { return entry.first < key; });
                groupNames.insert(
                    position, {std::string(rest), static_cast<std::uint32_t>(level.groups.size())});
                level.groups.push_back({std::string(rest)});
            } else {
                return fail(lineNumber, version >= 4 ? "expected 'group', 'brush' or 'entity'"
                                                     : "expected 'brush' or 'entity'");
            }
            break;

        case Block::Brush:
            if (word == "end" && rest.empty()) {
                if (version == 1) {
                    // Version 1 stored planes; they become corners on load.
                    std::optional<Brush> built = brushFromPlanes(planes);
                    if (!built) {
                        return fail(brushLine, "planes do not enclose a valid brush");
                    }
                    built->entity = brush.entity;
                    brush = std::move(*built);
                }
                std::string problem;
                if (!checkBrush(brush, problem)) {
                    return fail(brushLine, problem);
                }
                // A brush inside an entity block belongs to it; the entity is added at its end.
                block = brush.entity == kNoEntity ? Block::None : Block::Entity;
                level.brushes.push_back(std::move(brush));
                brush = {};
            } else if (version == 1 && word == "plane") {
                std::array<float, 4> values{};
                if (!parseFloats(rest, values)) {
                    return fail(lineNumber, "a plane is four numbers: normal x y z, distance");
                }
                if (planes.size() >= kMaxBrushFaces) {
                    return fail(lineNumber, std::format("more than {} planes", kMaxBrushFaces));
                }
                const DPlane plane{{values[0], values[1], values[2]}, values[3]};
                if (!(length(plane.normal) > 0.0)) {
                    return fail(lineNumber, "plane normal is zero");
                }
                planes.push_back(plane);
            } else if (version >= 2 && word == "vertex") {
                std::array<float, 3> values{};
                if (!parseFloats(rest, values)) {
                    return fail(lineNumber, "a vertex is three numbers: x y z");
                }
                if (brush.vertices.size() >= kMaxBrushVertices) {
                    return fail(lineNumber,
                                std::format("more than {} vertices", kMaxBrushVertices));
                }
                brush.vertices.push_back({values[0], values[1], values[2]});
            } else if (version >= 4 && word == "name") {
                if (!isValidLabel(rest)) {
                    return fail(lineNumber, "invalid brush name");
                }
                brush.name = rest;
            } else if (version >= 4 && word == "group") {
                const std::optional<std::uint32_t> group = findGroup(rest);
                if (!group) {
                    return fail(lineNumber, "no group of that name is declared above");
                }
                if (brush.entity != kNoEntity) {
                    return fail(lineNumber, "a brush of an entity is in its entity's group");
                }
                brush.group = *group;
            } else if (version >= 2 && word == "face") {
                BrushFace face;
                if (!parseIndices(rest, face.corners)) {
                    return fail(lineNumber,
                                std::format("a face is 3 to {} vertex numbers", kMaxBrushVertices));
                }
                if (brush.faces.size() >= kMaxBrushFaces) {
                    return fail(lineNumber, std::format("more than {} faces", kMaxBrushFaces));
                }
                brush.faces.push_back(std::move(face));
            } else {
                return fail(lineNumber, version == 1  ? "expected 'plane' or 'end'"
                                        : version < 4 ? "expected 'vertex', 'face' or 'end'"
                                                      : "expected 'name', 'group', 'vertex', "
                                                        "'face' or 'end'");
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
            } else if (word == "brush" && rest.empty()) {
                if (!startBrush(static_cast<std::uint32_t>(level.entities.size()))) {
                    return fail(lineNumber, std::format("more than {} brushes", kMaxLevelBrushes));
                }
            } else if (version >= 4 && word == "group") {
                const std::optional<std::uint32_t> group = findGroup(rest);
                if (!group) {
                    return fail(lineNumber, "no group of that name is declared above");
                }
                entity.group = *group;
            } else if (word == "output") {
                EntityOutput output;
                output.event = takeWord(rest);
                output.target = takeWord(rest);
                output.input = takeWord(rest);
                const std::string_view delay = takeWord(rest);
                if (output.input.empty() || !rest.empty() || !parseFloat(delay, output.delay)) {
                    return fail(lineNumber,
                                "an output is: output <event> <target> <input> <delay>");
                }
                if (entity.outputs.size() >= kMaxEntityOutputs) {
                    return fail(lineNumber, std::format("more than {} outputs", kMaxEntityOutputs));
                }
                entity.outputs.push_back(std::move(output));
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
        return block == Block::Brush ? fail(brushLine, "brush has no 'end'")
                                     : fail(blockLine, "entity has no 'end'");
    }
    // In the order formatLevel writes them, which a hand-edited file may not keep.
    const auto rank = [](const Brush& brush) {
        return brush.entity == kNoEntity ? 0u : brush.entity + 1u;
    };
    std::stable_sort(level.brushes.begin(), level.brushes.end(),
                     [&rank](const Brush& a, const Brush& b) { return rank(a) < rank(b); });
    return level;
}

std::optional<std::string> formatLevel(const Level& level, std::string& error) {
    if (level.brushes.size() > kMaxLevelBrushes || level.entities.size() > kMaxLevelEntities) {
        error = "level has too many brushes or entities";
        return std::nullopt;
    }
    std::string problem;
    if (!checkBrushOwners(level, problem) || !checkGroups(level, problem)) {
        error = problem;
        return std::nullopt;
    }
    for (std::size_t i = 0; i < level.brushes.size(); ++i) {
        if (!checkBrush(level.brushes[i], problem)) {
            error = std::format("brush {}: {}", i, problem);
            return std::nullopt;
        }
    }

    std::string text = std::format("{} {}\n", kHeader, kLevelFileVersion);
    if (!level.groups.empty()) {
        text += '\n';
    }
    for (const LevelGroup& group : level.groups) {
        text += std::format("group {}\n", group.name);
    }
    const auto writeBrush = [&](const Brush& brush, std::string_view indent) {
        text += std::format("{}brush\n", indent);
        if (!brush.name.empty()) {
            text += std::format("{}    name {}\n", indent, brush.name);
        }
        if (brush.group != kNoGroup) {
            text += std::format("{}    group {}\n", indent, level.groups[brush.group].name);
        }
        for (const Vec3& vertex : brush.vertices) {
            text += std::format("{}    vertex {}\n", indent, formatVec3(vertex));
        }
        for (const BrushFace& face : brush.faces) {
            text += std::format("{}    face", indent);
            for (const std::uint32_t corner : face.corners) {
                text += std::format(" {}", corner);
            }
            text += '\n';
        }
        text += std::format("{}end\n", indent);
    };

    // World brushes first, then each entity with its brushes inside its block, so a level whose
    // brushes are in that order loads back exactly as it was.
    std::vector<std::size_t> owned;
    for (std::size_t i = 0; i < level.brushes.size(); ++i) {
        if (level.brushes[i].entity == kNoEntity) {
            text += '\n';
            writeBrush(level.brushes[i], "");
        } else {
            owned.push_back(i);
        }
    }
    std::stable_sort(owned.begin(), owned.end(), [&level](std::size_t a, std::size_t b) {
        return level.brushes[a].entity < level.brushes[b].entity;
    });
    std::size_t nextOwned = 0;
    for (std::size_t i = 0; i < level.entities.size(); ++i) {
        const LevelEntity& entity = level.entities[i];
        if (!checkEntity(entity, problem)) {
            error = std::format("entity {} ({}): {}", i, entity.className, problem);
            return std::nullopt;
        }
        text += std::format("\nentity {}\n", entity.className);
        if (entity.group != kNoGroup) {
            text += std::format("    group {}\n", level.groups[entity.group].name);
        }
        for (const EntityProperty& property : entity.properties) {
            text += property.value.empty()
                        ? std::format("    {}\n", property.key)
                        : std::format("    {} {}\n", property.key, property.value);
        }
        for (const EntityOutput& output : entity.outputs) {
            text += std::format("    output {} {} {} {}\n", output.event, output.target,
                                output.input, formatFloat(output.delay));
        }
        for (; nextOwned < owned.size() && level.brushes[owned[nextOwned]].entity == i;
             ++nextOwned) {
            writeBrush(level.brushes[owned[nextOwned]], "    ");
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
    return text && writeTextFile(path, *text, error);
}

std::uint64_t levelContentHash(const Level& level) {
    std::string error;
    const std::optional<std::string> text = formatLevel(level, error);
    return text ? hashBytes(std::as_bytes(std::span{*text})) : 0;
}

} // namespace ct
