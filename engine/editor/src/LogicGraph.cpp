#include "croftech/editor/LogicGraph.h"

#include "croftech/core/ConfigFile.h"
#include "croftech/editor/EntityLinks.h"
#include "croftech/editor/LevelCommands.h"
#include "croftech/script/ScriptLibrary.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <format>
#include <limits>
#include <memory>
#include <optional>

namespace ct {
namespace {

// Nodes wired to none of the others wrap into rows no narrower than this.
constexpr float kMinimumRowWidth = 1200.0f;

const std::string* entityName(const LevelEntity& entity) {
    const std::string* name = findProperty(entity, "name");
    return name != nullptr && !name->empty() ? name : nullptr;
}

bool hasSignals(const LevelEntity& entity, const ScriptLibrary* scripts) {
    return !entitySignals(entity, scripts, true).empty() ||
           !entitySignals(entity, scripts, false).empty();
}

// Layers for the nodes of a graph given as each node's successors: each node a step to the right
// of the furthest of those before it, a loop broken at the node it was first entered from.
std::vector<std::uint32_t> layersOf(const std::vector<std::vector<std::uint32_t>>& successors,
                                    const std::vector<bool>& connected) {
    const std::size_t count = successors.size();
    std::vector<std::uint32_t> waiting(count, 0); // predecessors not yet laid
    for (const std::vector<std::uint32_t>& next : successors) {
        for (const std::uint32_t node : next) {
            ++waiting[node];
        }
    }
    std::vector<std::uint32_t> layer(count, 0);
    std::vector<bool> queued(count, false);
    std::deque<std::uint32_t> ready;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (connected[i] && waiting[i] == 0) {
            ready.push_back(i);
            queued[i] = true;
        }
    }
    std::size_t left =
        static_cast<std::size_t>(std::count(connected.begin(), connected.end(), true));
    while (left > 0) {
        if (ready.empty()) {
            // A loop: go on from the node fewest wait for, the first of them.
            std::optional<std::uint32_t> pick;
            for (std::uint32_t i = 0; i < count; ++i) {
                if (connected[i] && !queued[i] && (!pick || waiting[i] < waiting[*pick])) {
                    pick = i;
                }
            }
            ready.push_back(*pick);
            queued[*pick] = true;
        }
        const std::uint32_t node = ready.front();
        ready.pop_front();
        --left;
        for (const std::uint32_t next : successors[node]) {
            if (queued[next]) {
                continue;
            }
            layer[next] = std::max(layer[next], layer[node] + 1);
            if (--waiting[next] == 0) {
                ready.push_back(next);
                queued[next] = true;
            }
        }
    }
    return layer;
}

} // namespace

std::optional<GraphPoint> graphPosition(const LevelEntity& entity) {
    const std::string* value = findProperty(entity, kGraphPositionKey);
    if (value == nullptr) {
        return std::nullopt;
    }
    const std::string_view text = *value;
    const std::size_t space = text.find(' ');
    GraphPoint point;
    if (space == std::string_view::npos || !parseFloat(text.substr(0, space), point.x) ||
        !parseFloat(text.substr(space + 1), point.y) || !std::isfinite(point.x) ||
        !std::isfinite(point.y)) {
        return std::nullopt;
    }
    return point;
}

bool setGraphPositions(EditorDocument& document,
                       std::span<const std::pair<std::size_t, GraphPoint>> places,
                       std::string name) {
    const Level& level = document.level();
    std::vector<std::unique_ptr<Command>> commands;
    for (const auto& [index, place] : places) {
        LevelEntity entity = level.entities[index];
        setProperty(entity, kGraphPositionKey,
                    std::format("{} {}", formatFloat(std::round(place.x)),
                                formatFloat(std::round(place.y))));
        if (entity != level.entities[index]) {
            commands.push_back(
                makeSetEntity(name, index, level.entities[index], std::move(entity)));
        }
    }
    if (commands.empty()) {
        return false;
    }
    document.execute(commands.size() == 1 ? std::move(commands.front())
                                          : makeCompound(std::move(name), std::move(commands)));
    return true;
}

std::vector<SignalInfo> entitySignals(const LevelEntity& entity, const ScriptLibrary* scripts,
                                      bool events) {
    const EntityClassInfo* entityClass = findEntityClass(entity.className);
    if (entityClass == nullptr) {
        return {};
    }
    if (!entityClass->scripted) {
        const std::span<const SignalInfo> signals =
            events ? entityClass->events : entityClass->inputs;
        return {signals.begin(), signals.end()};
    }
    std::vector<SignalInfo> signals;
    const ScriptProgram* program =
        scripts != nullptr && findPropertyInfo(*entityClass, "script") != nullptr
            ? scripts->find(textProperty(entity, *entityClass, "script"))
            : nullptr;
    if (program != nullptr) {
        const ScriptDeclarations& declared = program->declarations();
        for (const std::string& name : events ? declared.events : declared.inputs) {
            signals.push_back({name, events ? "Fired by its script" : "Calls its script"});
        }
    }
    return signals;
}

std::vector<std::size_t> logicGraphEntities(const Level& level, const ScriptLibrary* scripts,
                                            bool everything) {
    std::vector<bool> shown(level.entities.size(), false);
    for (const Wire& wire : allWires(level)) {
        shown[wire.from] = true;
        shown[wire.to] = true;
    }
    std::vector<std::size_t> entities;
    for (std::size_t i = 0; i < level.entities.size(); ++i) {
        // Outputs to no one are wiring too, waiting for their target.
        if (shown[i] || !level.entities[i].outputs.empty() ||
            (everything && hasSignals(level.entities[i], scripts))) {
            entities.push_back(i);
        }
    }
    return entities;
}

std::vector<GraphPoint> layoutLogicGraph(const Level& level, std::span<const std::size_t> nodes,
                                         std::span<const GraphSize> sizes, bool keepSaved,
                                         float columnGap, float rowGap) {
    const std::size_t count = nodes.size();
    std::vector<GraphPoint> places(count);
    std::vector<bool> placed(count, false);
    std::vector<GraphPoint> taken;
    float savedLeft = std::numeric_limits<float>::max();
    float savedBottom = std::numeric_limits<float>::lowest();
    for (std::size_t i = 0; i < count && keepSaved; ++i) {
        const std::optional<GraphPoint> saved = graphPosition(level.entities[nodes[i]]);
        // A copy of another entity starts on top of it, so goes elsewhere.
        if (saved && std::find(taken.begin(), taken.end(), *saved) == taken.end()) {
            places[i] = *saved;
            placed[i] = true;
            taken.push_back(*saved);
            savedLeft = std::min(savedLeft, saved->x);
            savedBottom = std::max(savedBottom, saved->y + sizes[i].height);
        }
    }

    // The rest, numbered among themselves, with the wires between them.
    std::vector<std::uint32_t> rest;
    std::vector<std::uint32_t> restOf(level.entities.size(), ~0u);
    for (std::size_t i = 0; i < count; ++i) {
        if (!placed[i]) {
            restOf[nodes[i]] = static_cast<std::uint32_t>(rest.size());
            rest.push_back(static_cast<std::uint32_t>(i));
        }
    }
    if (rest.empty()) {
        return places;
    }
    std::vector<std::vector<std::uint32_t>> successors(rest.size());
    std::vector<std::vector<std::uint32_t>> neighbours(rest.size());
    std::vector<bool> connected(rest.size(), false);
    for (const Wire& wire : allWires(level)) {
        const std::uint32_t from = restOf[wire.from];
        const std::uint32_t to = restOf[wire.to];
        if (from == ~0u || to == ~0u || from == to ||
            std::find(successors[from].begin(), successors[from].end(), to) !=
                successors[from].end()) {
            continue;
        }
        successors[from].push_back(to);
        neighbours[from].push_back(to);
        neighbours[to].push_back(from);
        connected[from] = connected[to] = true;
    }

    const std::vector<std::uint32_t> layer = layersOf(successors, connected);
    std::vector<std::vector<std::uint32_t>> columns;
    for (std::uint32_t node = 0; node < rest.size(); ++node) {
        if (connected[node]) {
            if (columns.size() <= layer[node]) {
                columns.resize(layer[node] + 1);
            }
            columns[layer[node]].push_back(node);
        }
    }
    // Each node near the middle of its neighbours in the other columns, so fewer wires cross.
    std::vector<float> row(rest.size(), 0.0f);
    const auto number = [&] {
        for (const std::vector<std::uint32_t>& column : columns) {
            for (std::size_t i = 0; i < column.size(); ++i) {
                row[column[i]] = static_cast<float>(i);
            }
        }
    };
    number();
    for (int sweep = 0; sweep < 4; ++sweep) {
        std::vector<float> wanted(rest.size(), 0.0f);
        for (std::uint32_t node = 0; node < rest.size(); ++node) {
            float sum = 0.0f;
            int seen = 0;
            for (const std::uint32_t other : neighbours[node]) {
                if (layer[other] != layer[node]) {
                    sum += row[other];
                    ++seen;
                }
            }
            wanted[node] = seen > 0 ? sum / static_cast<float>(seen) : row[node];
        }
        for (std::vector<std::uint32_t>& column : columns) {
            std::stable_sort(column.begin(), column.end(), [&](std::uint32_t a, std::uint32_t b) {
                return wanted[a] < wanted[b];
            });
        }
        number();
    }

    const float left = savedLeft == std::numeric_limits<float>::max() ? 0.0f : savedLeft;
    const float top =
        savedBottom == std::numeric_limits<float>::lowest() ? 0.0f : savedBottom + rowGap * 3.0f;
    float x = 0.0f;
    float bottom = 0.0f;
    for (const std::vector<std::uint32_t>& column : columns) {
        float width = 0.0f;
        float y = 0.0f;
        for (const std::uint32_t node : column) {
            const std::uint32_t i = rest[node];
            places[i] = {left + x, top + y};
            y += sizes[i].height + rowGap;
            width = std::max(width, sizes[i].width);
        }
        bottom = std::max(bottom, y);
        x += width + columnGap;
    }
    // Those wired to none of the others, in rows.
    const float rowWidth = std::max(x, kMinimumRowWidth);
    float rowX = 0.0f;
    float rowY = bottom > 0.0f ? bottom + rowGap * 2.0f : 0.0f;
    float rowHeight = 0.0f;
    for (std::uint32_t node = 0; node < rest.size(); ++node) {
        if (connected[node]) {
            continue;
        }
        const std::uint32_t i = rest[node];
        if (rowX > 0.0f && rowX + sizes[i].width > rowWidth) {
            rowX = 0.0f;
            rowY += rowHeight + rowGap;
            rowHeight = 0.0f;
        }
        places[i] = {left + rowX, top + rowY};
        rowX += sizes[i].width + rowGap;
        rowHeight = std::max(rowHeight, sizes[i].height);
    }
    for (GraphPoint& place : places) {
        place = {std::floor(place.x), std::floor(place.y)};
    }
    return places;
}

std::string freeEntityName(const Level& level, std::string_view base) {
    for (std::size_t number = 1;; ++number) {
        std::string name = std::format("{}_{}", base, number);
        const bool used =
            std::any_of(level.entities.begin(), level.entities.end(), [&](const LevelEntity& e) {
                const std::string* other = entityName(e);
                return other != nullptr && *other == name;
            });
        if (!used) {
            return name;
        }
    }
}

bool connectEntities(EditorDocument& document, std::size_t from, std::string_view event,
                     std::size_t to, std::string_view input) {
    const Level& level = document.level();
    std::vector<std::unique_ptr<Command>> commands;
    LevelEntity target = level.entities[to];
    std::string name;
    if (const std::string* existing = entityName(target)) {
        name = *existing;
    } else {
        name = freeEntityName(level, target.className);
        setProperty(target, "name", name);
        if (to != from) {
            commands.push_back(makeSetEntity("Name entity", to, level.entities[to], target));
        }
    }
    LevelEntity source = to == from ? target : level.entities[from];
    const bool already =
        std::any_of(source.outputs.begin(), source.outputs.end(), [&](const EntityOutput& output) {
            return output.event == event && output.target == name && output.input == input;
        });
    if (already) {
        return false;
    }
    source.outputs.push_back({std::string(event), name, std::string(input), 0.0f});
    commands.push_back(makeSetEntity("Wire", from, level.entities[from], std::move(source)));
    document.execute(commands.size() == 1 ? std::move(commands.front())
                                          : makeCompound("Wire", std::move(commands)));
    return true;
}

} // namespace ct
