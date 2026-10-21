#include "croftech/editor/BrushOperations.h"
#include "croftech/editor/EditorActions.h"
#include "croftech/editor/EditorDocument.h"
#include "croftech/level/BrushGeometry.h"

#include <doctest/doctest.h>

#include <string_view>
#include <vector>

namespace {

using ct::LevelItem;

ct::Brush box(float x, float height = 1.0f) {
    return ct::makeBoxBrush({x, 0.0f, 0.0f}, {x + 1.0f, 1.0f, height});
}

ct::Brush owned(ct::Brush brush, std::uint32_t entity) {
    brush.entity = entity;
    return brush;
}

// A spawn, then a door of brushes 1 and 2 and a trigger of brush 3; brush 0 is world.
ct::Level doorAndTrigger() {
    ct::Level level;
    level.entities.push_back(ct::makePlayerSpawn({0.0f, 0.0f, 1.0f}, 0.0f));
    level.entities.push_back({"mover", {{"name", "gate"}, {"move", "0 0 2"}}});
    level.entities.push_back({"trigger", {}, {{"on_enter", "gate", "open", 0.0f}}});
    level.brushes = {box(0.0f), owned(box(2.0f), 1), owned(box(4.0f, 2.0f), 1),
                     owned(box(6.0f), 2)};
    return level;
}

std::vector<std::uint32_t> owners(const ct::Level& level) {
    std::vector<std::uint32_t> result;
    for (const ct::Brush& brush : level.brushes) {
        result.push_back(brush.entity);
    }
    return result;
}

constexpr std::uint32_t kWorld = ct::kNoEntity;

const ct::EntityPreset& preset(std::string_view name) {
    for (const ct::EntityPreset& candidate : ct::entityPresets()) {
        if (candidate.name == name) {
            return candidate;
        }
    }
    FAIL("no preset " << name);
    return ct::entityPresets().front();
}

} // namespace

TEST_CASE("a brush picks its whole entity") {
    const ct::Level level = doorAndTrigger();
    const std::vector<LevelItem> one{LevelItem::brush(2)};
    CHECK(ct::withWholeEntities(level, one) ==
          std::vector<LevelItem>{LevelItem::brush(1), LevelItem::brush(2), LevelItem::entity(1)});
    const std::vector<LevelItem> entity{LevelItem::entity(2), LevelItem::brush(0)};
    CHECK(ct::withWholeEntities(level, entity) ==
          std::vector<LevelItem>{LevelItem::brush(0), LevelItem::brush(3), LevelItem::entity(2)});
    const std::vector<LevelItem> spawn{LevelItem::entity(0), LevelItem::entity(9)};
    CHECK(ct::withWholeEntities(level, spawn) == std::vector<LevelItem>{LevelItem::entity(0)});
}

TEST_CASE("a click selects a whole entity, Alt-click one brush, Ctrl-click toggles") {
    ct::EditorDocument document(doorAndTrigger(), "test.level");
    ct::selectClicked(document, LevelItem::brush(1), false, ct::SelectScope::Entity);
    CHECK(document.selection() ==
          std::vector<LevelItem>{LevelItem::brush(1), LevelItem::brush(2), LevelItem::entity(1)});
    ct::selectClicked(document, LevelItem::brush(3), true, ct::SelectScope::Entity);
    CHECK(document.selection().size() == 5);
    ct::selectClicked(document, LevelItem::brush(2), true, ct::SelectScope::Entity);
    CHECK(document.selection() ==
          std::vector<LevelItem>{LevelItem::brush(3), LevelItem::entity(2)});
    ct::selectClicked(document, LevelItem::brush(2), false, ct::SelectScope::Item);
    CHECK(document.selection() == std::vector<LevelItem>{LevelItem::brush(2)});
    CHECK(ct::describeSelection(document) == "brush");
}

TEST_CASE("selected brushes become a door that lifts by their height") {
    ct::Level level;
    level.brushes = {box(0.0f), box(2.0f, 3.0f), box(4.0f)};
    ct::EditorDocument document(level, "test.level");
    document.setSelection({LevelItem::brush(1), LevelItem::brush(2)});
    REQUIRE(ct::createBrushEntity(document, preset("Door")));

    REQUIRE(document.level().entities.size() == 1);
    const ct::LevelEntity& door = document.level().entities[0];
    CHECK(door.className == "mover");
    CHECK(*ct::findProperty(door, "move") == "0 0 3");
    CHECK(*ct::findProperty(door, "activate") == "touch");
    CHECK(owners(document.level()) == std::vector<std::uint32_t>{kWorld, 0, 0});
    CHECK(document.selection() ==
          std::vector<LevelItem>{LevelItem::brush(1), LevelItem::brush(2), LevelItem::entity(0)});
    CHECK(ct::describeSelection(document) == "mover");
    CHECK(document.history().undoName() == "Make Door");

    REQUIRE(document.undo());
    CHECK(document.level() == level);
    REQUIRE(document.redo());
    CHECK(owners(document.level()) == std::vector<std::uint32_t>{kWorld, 0, 0});

    document.clearSelection();
    CHECK_FALSE(ct::createBrushEntity(document, preset("Trigger")));
}

TEST_CASE("taking an entity's last brushes deletes it, and the rest are renumbered") {
    const ct::Level level = doorAndTrigger();
    ct::EditorDocument document(level, "test.level");
    document.setSelection({LevelItem::brush(1), LevelItem::brush(2)});
    REQUIRE(ct::createBrushEntity(document, preset("Lift")));

    // The old mover went; the trigger moved down to 1 and the lift is 2.
    const ct::Level& after = document.level();
    REQUIRE(after.entities.size() == 3);
    CHECK(after.entities[1].className == "trigger");
    CHECK(after.entities[2].className == "mover");
    CHECK(owners(after) == std::vector<std::uint32_t>{kWorld, 2, 2, 1});
    CHECK(document.selection().back() == LevelItem::entity(2));
    REQUIRE(document.undo());
    CHECK(document.level() == level);
}

TEST_CASE("presets make a ready-made entity from one new brush") {
    ct::EditorDocument document(doorAndTrigger(), "test.level");
    REQUIRE(ct::addPresetEntity(document, preset("Lift"), {10.0f, 0.0f, 0.0f}));
    const ct::Level& level = document.level();
    REQUIRE(level.entities.size() == 4);
    const ct::LevelEntity& lift = level.entities[3];
    CHECK(lift.className == "mover");
    CHECK(*ct::findProperty(lift, "activate") == "stand");
    CHECK(*ct::findProperty(lift, "move") == "0 0 4");
    REQUIRE(level.brushes.size() == 5);
    CHECK(level.brushes[4] ==
          owned(ct::makeBoxBrush({9.0f, -1.0f, 0.0f}, {11.0f, 1.0f, 0.25f}), 3));
    CHECK(document.selection() ==
          std::vector<LevelItem>{LevelItem::brush(4), LevelItem::entity(3)});
    CHECK(document.history().undoName() == "Create Lift");

    REQUIRE(ct::addPresetEntity(document, preset("Door"), {0.0f, 5.0f, 0.0f}));
    CHECK(*ct::findProperty(document.level().entities[4], "move") == "0 0 3");
    REQUIRE(document.undo());
    REQUIRE(document.undo());
    CHECK(document.level() == doorAndTrigger());
}

TEST_CASE("brushes go back to the world, and their emptied entity is deleted") {
    const ct::Level level = doorAndTrigger();
    ct::EditorDocument document(level, "test.level");
    document.setSelection({LevelItem::brush(0), LevelItem::brush(1)});
    REQUIRE(ct::moveSelectionToWorld(document));
    CHECK(owners(document.level()) == std::vector<std::uint32_t>{kWorld, kWorld, 1, 2});
    CHECK(document.level().entities.size() == 3);

    document.setSelection({LevelItem::brush(2)});
    REQUIRE(ct::moveSelectionToWorld(document));
    CHECK(owners(document.level()) == std::vector<std::uint32_t>{kWorld, kWorld, kWorld, 1});
    REQUIRE(document.level().entities.size() == 2);
    CHECK(document.level().entities[1].className == "trigger");

    document.setSelection({LevelItem::brush(0)});
    CHECK_FALSE(ct::moveSelectionToWorld(document));
    REQUIRE(document.undo());
    REQUIRE(document.undo());
    CHECK(document.level() == level);
}

TEST_CASE("deleting an entity deletes its brushes; deleting its last brush deletes it") {
    const ct::Level level = doorAndTrigger();
    ct::EditorDocument document(level, "test.level");
    document.select(LevelItem::entity(1));
    REQUIRE(ct::deleteSelection(document));
    CHECK(owners(document.level()) == std::vector<std::uint32_t>{kWorld, 1});
    REQUIRE(document.level().entities.size() == 2);
    CHECK(document.level().entities[1].className == "trigger");

    document.select(LevelItem::brush(1));
    REQUIRE(ct::deleteSelection(document));
    CHECK(document.level().brushes.size() == 1);
    CHECK(document.level().entities.size() == 1);

    REQUIRE(document.undo());
    REQUIRE(document.undo());
    CHECK(document.level() == level);
}

TEST_CASE("a duplicated entity owns copies of its brushes; a duplicated brush stays put") {
    const ct::Level level = doorAndTrigger();
    ct::EditorDocument document(level, "test.level");
    document.setSelection(ct::withWholeEntities(level, std::vector{LevelItem::brush(1)}));
    REQUIRE(ct::duplicateSelection(document, {0.0f, 5.0f, 0.0f}));
    const ct::Level& after = document.level();
    REQUIRE(after.entities.size() == 4);
    CHECK(after.entities[3] == level.entities[1]);
    CHECK(owners(after) == std::vector<std::uint32_t>{kWorld, 1, 1, 2, 3, 3});
    CHECK(document.selection() ==
          std::vector<LevelItem>{LevelItem::brush(4), LevelItem::brush(5), LevelItem::entity(3)});

    document.select(LevelItem::brush(3));
    REQUIRE(ct::duplicateSelection(document, {0.0f, 5.0f, 0.0f}));
    CHECK(document.level().brushes.back().entity == 2);
}

TEST_CASE("brush edits keep brushes in their entity") {
    const ct::Level level = doorAndTrigger();
    ct::EditorDocument document(level, "test.level");

    document.select(LevelItem::brush(2));
    const ct::DPlane cut{{0.0, 0.0, 1.0}, 1.0};
    REQUIRE(ct::clipSelection(document, cut, ct::ClipKeep::Both));
    CHECK(owners(document.level()) == std::vector<std::uint32_t>{kWorld, 1, 1, 2, 1});

    document.select(LevelItem::brush(3));
    REQUIRE(ct::hollowSelection(document, 0.25f));
    for (std::size_t i = 5; i < document.level().brushes.size(); ++i) {
        CHECK(document.level().brushes[i].entity == 2);
    }
    CHECK(document.level().brushes[3].entity == 2);

    const std::vector<ct::Vec3> corner{{3.0f, 1.0f, 1.0f}};
    document.select(LevelItem::brush(1));
    REQUIRE(ct::moveCorners(document, corner, {0.0f, 0.0f, 0.5f}));
    CHECK(document.level().brushes[1].entity == 1);
}

TEST_CASE("turning a door turns the way it opens") {
    const ct::Level level = doorAndTrigger();
    ct::EditorDocument document(level, "test.level");
    document.setSelection(ct::withWholeEntities(level, std::vector{LevelItem::entity(1)}));
    REQUIRE(ct::rotateSelection(document, 0, 1, 1.0f));
    CHECK(*ct::findProperty(document.level().entities[1], "move") == "0 -2 0");
}

TEST_CASE("subtracting with a whole door deletes the door") {
    ct::Level level;
    level.brushes = {ct::makeBoxBrush({0.0f, 0.0f, 0.0f}, {4.0f, 1.0f, 3.0f}),
                     owned(ct::makeBoxBrush({1.0f, -1.0f, 0.0f}, {2.0f, 2.0f, 2.0f}), 1)};
    level.entities = {ct::makePlayerSpawn({0.0f, 0.0f, 3.0f}, 0.0f), {"mover", {}}};
    ct::EditorDocument document(level, "test.level");
    document.setSelection(ct::withWholeEntities(level, std::vector{LevelItem::brush(1)}));
    REQUIRE(ct::subtractSelection(document));
    CHECK(document.level().entities.size() == 1);
    for (const ct::Brush& brush : document.level().brushes) {
        CHECK(brush.entity == kWorld);
    }
    REQUIRE(document.undo());
    CHECK(document.level() == level);
}
