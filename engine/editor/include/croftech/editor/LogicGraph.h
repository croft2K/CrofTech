#pragma once

#include "croftech/editor/EditorDocument.h"
#include "croftech/level/EntityClasses.h"
#include "croftech/level/Level.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ct {

class ScriptLibrary;

// The level's wiring as a graph, for the Logic window: each entity a node with its inputs on the
// left and its events on the right, and each output a link from an event to the input of every
// entity its target names. Nodes are laid out by the wiring until the designer moves them; then
// each keeps its place in the level as its "graph" property, which only the editor reads.

inline constexpr std::string_view kGraphPositionKey = "graph";

struct GraphPoint {
    float x = 0.0f;
    float y = 0.0f;

    friend bool operator==(const GraphPoint&, const GraphPoint&) = default;
};

struct GraphSize {
    float width = 0.0f;
    float height = 0.0f;
};

// The entity's place in the graph, if it has one that reads.
std::optional<GraphPoint> graphPosition(const LevelEntity& entity);

// Sets the entities' places, in whole units, as one command. False if none changes.
bool setGraphPositions(EditorDocument& document,
                       std::span<const std::pair<std::size_t, GraphPoint>> places,
                       std::string name);

// The events (or inputs) of an entity: its class's, or a script entity's script's, if the scripts
// are given and its script loads. The names of a script's stay valid until the library next
// refreshes.
std::vector<SignalInfo> entitySignals(const LevelEntity& entity, const ScriptLibrary* scripts,
                                      bool events);

// The entities the graph shows, in level order: those in the wiring (with outputs, or named by
// one), and with everything, every other entity with events or inputs too.
std::vector<std::size_t> logicGraphEntities(const Level& level, const ScriptLibrary* scripts,
                                            bool everything);

// Where each of the nodes goes (in the order given, each sizes[i] big). Each with a saved place
// that no node before it has keeps it, if keepSaved; the rest are laid out by the wiring between
// them, below those: left to right as events flow, a column for each step, as wide as its widest
// node, with those wired to none of the others in rows beneath. A loop in the wiring is broken
// where it was reached first.
std::vector<GraphPoint> layoutLogicGraph(const Level& level, std::span<const std::size_t> nodes,
                                         std::span<const GraphSize> sizes, bool keepSaved,
                                         float columnGap = 80.0f, float rowGap = 24.0f);

// The name "<base>_<n>" with the lowest n that no entity has.
std::string freeEntityName(const Level& level, std::string_view base);

// Wires an event of one entity to an input of another (or the same): an output of from, to to's
// name. A target without a name is given one of its own first (its class's, numbered), in the
// same command. False, changing nothing, if from already sends that input to that name on that
// event.
bool connectEntities(EditorDocument& document, std::size_t from, std::string_view event,
                     std::size_t to, std::string_view input);

} // namespace ct
