#include "croftech/editor/LogicGraph.h"

#include <doctest/doctest.h>

#include <ostream> // doctest prints string_view through operator<<
#include <string>
#include <utility>
#include <vector>

namespace {

// A trigger tells a relay, which tells a mover; a timer loops on itself; a counter and a player
// spawn take no part.
ct::Level wiredLevel() {
    ct::Level level;
    level.entities = {
        {"trigger", {}, {{"on_enter", "relay1", "trigger", 0.0f}}},
        {"relay", {{"name", "relay1"}}, {{"on_trigger", "gate", "move_to_end", 0.5f}}},
        {"mover", {{"name", "gate"}}},
        {"timer", {{"name", "clock"}}, {{"on_timer", "clock", "start", 1.0f}}},
        {"counter", {}},
        {"player_spawn", {{"origin", "0 0 0"}}},
    };
    return level;
}

std::vector<ct::GraphSize> sized(std::size_t count) {
    return std::vector<ct::GraphSize>(count, {100.0f, 50.0f});
}

} // namespace

TEST_CASE("a node's place is kept in the entity's graph property") {
    ct::LevelEntity entity{"relay", {}};
    CHECK_FALSE(ct::graphPosition(entity).has_value());
    ct::setProperty(entity, ct::kGraphPositionKey, "12 -40.5");
    CHECK(ct::graphPosition(entity) == ct::GraphPoint{12.0f, -40.5f});
    ct::setProperty(entity, ct::kGraphPositionKey, "12");
    CHECK_FALSE(ct::graphPosition(entity).has_value());

    ct::EditorDocument document(wiredLevel(), "test.level");
    const std::pair<std::size_t, ct::GraphPoint> places[] = {{0, {10.4f, 20.6f}},
                                                             {2, {300.0f, 0.0f}}};
    REQUIRE(ct::setGraphPositions(document, places, "Move nodes"));
    CHECK(*ct::findProperty(document.level().entities[0], "graph") == "10 21");
    CHECK(ct::graphPosition(document.level().entities[2]) == ct::GraphPoint{300.0f, 0.0f});
    CHECK_FALSE(ct::setGraphPositions(document, places, "Move nodes"));
    // One command for them all.
    REQUIRE(document.undo());
    CHECK(ct::findProperty(document.level().entities[0], "graph") == nullptr);
    CHECK(ct::findProperty(document.level().entities[2], "graph") == nullptr);
}

TEST_CASE("the graph shows the entities in the wiring, or every one with signals") {
    const ct::Level level = wiredLevel();
    CHECK(ct::logicGraphEntities(level, nullptr, false) == std::vector<std::size_t>{0, 1, 2, 3});
    // The player spawn has no events or inputs.
    CHECK(ct::logicGraphEntities(level, nullptr, true) == std::vector<std::size_t>{0, 1, 2, 3, 4});
    CHECK(ct::entitySignals(level.entities[2], nullptr, true).size() == 4);
    CHECK(ct::entitySignals(level.entities[5], nullptr, false).empty());
}

TEST_CASE("the graph is laid out by the wiring, left to right") {
    const ct::Level level = wiredLevel();
    const std::vector<std::size_t> nodes = ct::logicGraphEntities(level, nullptr, true);
    const std::vector<ct::GraphPoint> places =
        ct::layoutLogicGraph(level, nodes, sized(nodes.size()), true, 80.0f, 20.0f);
    REQUIRE(places.size() == 5);
    // trigger -> relay -> mover, a column each.
    CHECK(places[0] == ct::GraphPoint{0.0f, 0.0f});
    CHECK(places[1] == ct::GraphPoint{180.0f, 0.0f});
    CHECK(places[2] == ct::GraphPoint{360.0f, 0.0f});
    // The timer wires only itself and the counter not at all: rows below.
    CHECK(places[3] == ct::GraphPoint{0.0f, 110.0f});
    CHECK(places[4] == ct::GraphPoint{120.0f, 110.0f});
}

TEST_CASE("nodes with saved places keep them, and the rest go below them") {
    ct::Level level = wiredLevel();
    ct::setProperty(level.entities[1], ct::kGraphPositionKey, "500 -200");
    // A copy of the relay starts where it is, so goes elsewhere.
    level.entities.push_back(level.entities[1]);
    const std::vector<std::size_t> nodes = {0, 1, 2, 6};
    const std::vector<ct::GraphPoint> places =
        ct::layoutLogicGraph(level, nodes, sized(nodes.size()), true, 80.0f, 20.0f);
    CHECK(places[1] == ct::GraphPoint{500.0f, -200.0f});
    // Below the saved node (its bottom is -150), from its left. The copy sends to the gate too.
    CHECK(places[0].y == -90.0f);
    CHECK(places[0].x == 500.0f);
    CHECK(places[2].x > places[0].x);
    CHECK(places[3].y >= -90.0f);
    CHECK(places[3] != places[1]);

    // Tidied, nothing saved counts.
    const std::vector<ct::GraphPoint> tidied =
        ct::layoutLogicGraph(level, nodes, sized(nodes.size()), false, 80.0f, 20.0f);
    CHECK(tidied[0] == ct::GraphPoint{0.0f, 0.0f});
}

TEST_CASE("a loop in the wiring is laid out without going round forever") {
    ct::Level level;
    level.entities = {
        {"relay", {{"name", "a"}}, {{"on_trigger", "b", "trigger", 0.0f}}},
        {"relay", {{"name", "b"}}, {{"on_trigger", "c", "trigger", 0.0f}}},
        {"relay", {{"name", "c"}}, {{"on_trigger", "a", "trigger", 0.0f}}},
    };
    const std::vector<std::size_t> nodes = {0, 1, 2};
    const std::vector<ct::GraphPoint> places =
        ct::layoutLogicGraph(level, nodes, sized(3), true, 80.0f, 20.0f);
    CHECK(places[0].x == 0.0f);
    CHECK(places[1].x == 180.0f);
    CHECK(places[2].x == 360.0f);
}

TEST_CASE("wiring two entities adds an output, naming the target if it has no name") {
    ct::EditorDocument document(wiredLevel(), "test.level");
    // The counter has no name: it gets one in the same step.
    REQUIRE(ct::connectEntities(document, 0, "on_exit", 4, "add"));
    const ct::Level& level = document.level();
    CHECK(*ct::findProperty(level.entities[4], "name") == "counter_1");
    REQUIRE(level.entities[0].outputs.size() == 2);
    CHECK(level.entities[0].outputs[1] == ct::EntityOutput{"on_exit", "counter_1", "add", 0.0f});
    // Already wired so.
    CHECK_FALSE(ct::connectEntities(document, 0, "on_exit", 4, "add"));
    REQUIRE(document.undo());
    CHECK(document.level().entities[0].outputs.size() == 1);
    CHECK(ct::findProperty(document.level().entities[4], "name") == nullptr);

    // To itself, an unnamed entity is named and wired in one change of it.
    REQUIRE(ct::connectEntities(document, 4, "on_reached", 4, "reset"));
    CHECK(*ct::findProperty(document.level().entities[4], "name") == "counter_1");
    CHECK(document.level().entities[4].outputs.front().target == "counter_1");
    CHECK(ct::freeEntityName(document.level(), "counter") == "counter_2");
}
