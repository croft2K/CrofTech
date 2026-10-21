#include "croftech/editor/EditorActions.h"
#include "croftech/editor/EntityLinks.h"

#include <doctest/doctest.h>

#include <vector>

namespace {

using ct::LevelItem;

ct::Brush owned(ct::Vec3 min, ct::Vec3 max, std::uint32_t entity) {
    ct::Brush brush = ct::makeBoxBrush(min, max);
    brush.entity = entity;
    return brush;
}

// A trigger that tells a relay, which opens two movers both named gate; one output names nobody.
ct::Level wiredLevel() {
    ct::Level level;
    level.entities = {
        {"trigger",
         {},
         {{"on_enter", "relay1", "trigger", 0.0f}, {"on_exit", "nobody", "x", 0.0f}}},
        {"relay",
         {{"origin", "5 5 1"}, {"name", "relay1"}},
         {{"on_trigger", "gate", "move_to_end", 0.5f}}},
        {"mover", {{"name", "gate"}, {"move", "0 0 3"}}},
        {"mover", {{"name", "gate"}}},
    };
    level.brushes = {ct::makeBoxBrush({-8.0f, -8.0f, -1.0f}, {8.0f, 8.0f, 0.0f}),
                     owned({0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 2.0f}, 0),
                     owned({4.0f, 0.0f, 0.0f}, {6.0f, 0.25f, 3.0f}, 2),
                     owned({6.0f, 0.0f, 0.0f}, {8.0f, 0.25f, 3.0f}, 2),
                     owned({0.0f, 4.0f, 0.0f}, {2.0f, 4.25f, 3.0f}, 3)};
    return level;
}

} // namespace

TEST_CASE("the selected entity is one entity with nothing but its own brushes") {
    ct::EditorDocument document(wiredLevel(), "test.level");
    document.setSelection({LevelItem::brush(2), LevelItem::brush(3), LevelItem::entity(2)});
    CHECK(ct::selectedEntity(document) == 2);
    document.setSelection({LevelItem::entity(1)});
    CHECK(ct::selectedEntity(document) == 1);
    document.setSelection({LevelItem::brush(0), LevelItem::entity(2)});
    CHECK_FALSE(ct::selectedEntity(document).has_value());
    document.setSelection({LevelItem::entity(1), LevelItem::entity(2)});
    CHECK_FALSE(ct::selectedEntity(document).has_value());
    document.setSelection({LevelItem::brush(2)});
    CHECK_FALSE(ct::selectedEntity(document).has_value());
}

TEST_CASE("entities are drawn from their origin or the middle of their brushes") {
    const ct::Level level = wiredLevel();
    CHECK(ct::entityAnchor(level, 1) == ct::Vec3{5.0f, 5.0f, 1.0f});
    CHECK(ct::entityAnchor(level, 2) == ct::Vec3{6.0f, 0.125f, 1.5f});
    CHECK_FALSE(ct::entityAnchor(level, 9).has_value());
}

TEST_CASE("a mover's path runs from its brushes by its move, which the end handle sets") {
    ct::EditorDocument document(wiredLevel(), "test.level");
    const auto path = ct::moverPath(document.level(), 2);
    REQUIRE(path.has_value());
    CHECK(path->start() == ct::Vec3{6.0f, 0.125f, 1.5f});
    CHECK(path->end() == ct::Vec3{6.0f, 0.125f, 4.5f});
    CHECK(ct::moverPath(document.level(), 3)->move == ct::Vec3{});
    CHECK_FALSE(ct::moverPath(document.level(), 0).has_value()); // a trigger doesn't move
    CHECK_FALSE(ct::moverPath(document.level(), 1).has_value());

    REQUIRE(ct::setMoverEnd(document, 2, {4.0f, 0.0f, 0.0f}));
    CHECK(*ct::findProperty(document.level().entities[2], "move") == "4 0 0");
    CHECK(document.history().undoName() == "Move mover end");
    CHECK_FALSE(ct::setMoverEnd(document, 2, {4.0f, 0.0f, 0.0f}));
    CHECK_FALSE(ct::setMoverEnd(document, 0, {4.0f, 0.0f, 0.0f}));
    REQUIRE(document.undo());
    CHECK(document.level() == wiredLevel());
}

TEST_CASE("wires join outputs to every entity with the target name") {
    const ct::Level level = wiredLevel();
    const std::vector<ct::Wire> relay = ct::entityWires(level, 1);
    REQUIRE(relay.size() == 3);
    // Out to both gates, then in from the trigger.
    CHECK((relay[0].from == 1 && relay[0].to == 2 && relay[0].output == 0));
    CHECK((relay[1].from == 1 && relay[1].to == 3 && relay[1].output == 0));
    CHECK((relay[2].from == 0 && relay[2].to == 1 && relay[2].output == 0));

    // The trigger's output to nobody has no wire.
    CHECK(ct::entityWires(level, 0).size() == 1);
    CHECK(ct::entityWires(level, 3).size() == 1);
    CHECK(ct::allWires(level).size() == 3);
}
