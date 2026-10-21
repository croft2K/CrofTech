#include "croftech/editor/EditorActions.h"
#include "croftech/editor/EditorDocument.h"
#include "croftech/level/LevelFile.h"

#include <doctest/doctest.h>

#include <string>
#include <string_view>
#include <vector>

namespace {

using ct::LevelItem;

ct::Brush box(float x) { return ct::makeBoxBrush({x, 0.0f, 0.0f}, {x + 1.0f, 1.0f, 1.0f}); }

ct::Brush owned(ct::Brush brush, std::uint32_t entity) {
    brush.entity = entity;
    return brush;
}

// World brushes 0 and 3, a door (entity 1) of brushes 1 and 2, and a spawn (entity 0).
ct::Level level() {
    ct::Level level;
    level.entities.push_back(ct::makePlayerSpawn({0.0f, 0.0f, 1.0f}, 0.0f));
    level.entities.push_back({"mover", {{"name", "gate"}, {"move", "0 0 2"}}});
    level.brushes = {box(0.0f), owned(box(2.0f), 1), owned(box(4.0f), 1), box(6.0f)};
    return level;
}

// Whether the level still saves, which checks its groups.
bool saves(const ct::EditorDocument& document) {
    std::string error;
    const bool ok = ct::formatLevel(document.level(), error).has_value();
    CAPTURE(error);
    return ok;
}

const ct::EntityPreset& preset(std::string_view name) {
    for (const ct::EntityPreset& candidate : ct::entityPresets()) {
        if (candidate.name == name) {
            return candidate;
        }
    }
    FAIL("no preset");
    return ct::entityPresets().front();
}

} // namespace

TEST_CASE("grouping puts world brushes and whole entities in a new group, and undoes") {
    ct::EditorDocument document(level(), "test.level");
    document.setSelection({LevelItem::brush(0), LevelItem::brush(2)});
    REQUIRE(ct::groupSelection(document));
    const ct::Level& grouped = document.level();
    REQUIRE(grouped.groups.size() == 1);
    CHECK(grouped.groups[0].name == "Group");
    CHECK(grouped.brushes[0].group == 0);
    CHECK(grouped.entities[1].group == 0);
    // The door's brushes are in its group, through it.
    CHECK(grouped.brushes[2].group == ct::kNoGroup);
    CHECK(ct::itemGroup(grouped, LevelItem::brush(2)) == 0);
    CHECK(ct::itemGroup(grouped, LevelItem::brush(3)) == ct::kNoGroup);
    CHECK(document.selection() == std::vector<LevelItem>{LevelItem::brush(0), LevelItem::brush(1),
                                                         LevelItem::brush(2),
                                                         LevelItem::entity(1)});
    CHECK(saves(document));
    document.undo();
    CHECK(document.level() == level());
}

TEST_CASE("a click in the view picks the whole group; Alt-click one brush") {
    ct::EditorDocument document(level(), "test.level");
    document.setSelection({LevelItem::brush(0), LevelItem::entity(1)});
    ct::groupSelection(document);
    document.clearSelection();
    ct::selectClicked(document, LevelItem::brush(0), false, ct::SelectScope::Group);
    CHECK(document.selection().size() == 4);
    ct::selectClicked(document, LevelItem::brush(0), false, ct::SelectScope::Item);
    CHECK(document.selection() == std::vector<LevelItem>{LevelItem::brush(0)});
    ct::selectClicked(document, LevelItem::brush(3), false, ct::SelectScope::Group);
    CHECK(document.selection() == std::vector<LevelItem>{LevelItem::brush(3)});
    ct::selectGroup(document, 0, true);
    CHECK(document.selection().size() == 5);
    ct::selectGroup(document, 0, true);
    CHECK(document.selection() == std::vector<LevelItem>{LevelItem::brush(3)});
}

TEST_CASE("regrouping takes items out of their old groups, and empty groups go") {
    ct::EditorDocument document(level(), "test.level");
    document.setSelection({LevelItem::brush(0)});
    ct::groupSelection(document);
    document.setSelection({LevelItem::brush(3)});
    ct::groupSelection(document);
    REQUIRE(document.level().groups.size() == 2);
    CHECK(document.level().groups[1].name == "Group 2");

    document.setSelection({LevelItem::brush(0), LevelItem::brush(3)});
    REQUIRE(ct::groupSelection(document));
    REQUIRE(document.level().groups.size() == 1);
    CHECK(document.level().groups[0].name == "Group 3");
    CHECK(document.level().brushes[0].group == 0);
    CHECK(document.level().brushes[3].group == 0);
    CHECK(saves(document));
    document.undo();
    CHECK(document.level().groups.size() == 2);
    CHECK(document.level().brushes[3].group == 1);
}

TEST_CASE("ungrouping removes the groups of the selection and leaves their members") {
    ct::EditorDocument document(level(), "test.level");
    document.setSelection({LevelItem::brush(0), LevelItem::entity(1)});
    ct::groupSelection(document);
    document.setSelection({LevelItem::brush(1)});
    REQUIRE(ct::ungroupSelection(document));
    CHECK(document.level().groups.empty());
    CHECK(document.level().brushes[0].group == ct::kNoGroup);
    CHECK(document.level().entities[1].group == ct::kNoGroup);
    CHECK(document.level().brushes.size() == 4);
    CHECK_FALSE(ct::ungroupSelection(document));
    document.undo();
    CHECK(document.level().groups.size() == 1);
}

TEST_CASE("deleting every member of a group deletes the group") {
    ct::EditorDocument document(level(), "test.level");
    document.setSelection({LevelItem::brush(0), LevelItem::brush(3)});
    ct::groupSelection(document);
    document.setSelection({LevelItem::brush(0)});
    ct::deleteSelection(document);
    CHECK(document.level().groups.size() == 1);
    document.setSelection({LevelItem::brush(2)});
    ct::deleteSelection(document);
    CHECK(document.level().groups.empty());
    CHECK(saves(document));
    document.undo();
    document.undo();
    CHECK(document.level().groups.size() == 1);
    CHECK(document.level().brushes[0].group == 0);
}

TEST_CASE("duplicating a whole group makes a new group; part of one stays in it") {
    ct::EditorDocument document(level(), "test.level");
    document.setSelection({LevelItem::brush(0), LevelItem::entity(1)});
    ct::groupSelection(document);
    REQUIRE(ct::duplicateSelection(document, {0.0f, 4.0f, 0.0f}));
    const ct::Level& copied = document.level();
    REQUIRE(copied.groups.size() == 2);
    CHECK(copied.groups[1].name == "Group 2");
    CHECK(copied.brushes.size() == 7);
    CHECK(copied.entities.size() == 3);
    CHECK(copied.entities[2].group == 1);
    CHECK(ct::groupMembers(copied, 1).size() == 4);
    CHECK(ct::groupMembers(copied, 1) == document.selection());

    document.setSelection({LevelItem::brush(0)});
    REQUIRE(ct::duplicateSelection(document, {0.0f, 8.0f, 0.0f}));
    CHECK(document.level().groups.size() == 2);
    CHECK(document.level().brushes.back().group == 0);
    CHECK(saves(document));
}

TEST_CASE("brushes made into an entity take its group, and back in the world they keep it") {
    ct::EditorDocument document(level(), "test.level");
    document.setSelection({LevelItem::brush(3)});
    ct::groupSelection(document);
    document.setSelection({LevelItem::brush(3)});
    REQUIRE(ct::createBrushEntity(document, preset("Door")));
    const ct::Level& made = document.level();
    CHECK(made.entities.back().group == 0);
    CHECK(made.brushes[3].group == ct::kNoGroup);
    CHECK(ct::itemGroup(made, LevelItem::brush(3)) == 0);
    CHECK(saves(document));

    document.setSelection({LevelItem::brush(3)});
    REQUIRE(ct::moveSelectionToWorld(document));
    CHECK(document.level().brushes[3].entity == ct::kNoEntity);
    CHECK(document.level().brushes[3].group == 0);
    CHECK(saves(document));
}

TEST_CASE("renaming brushes, entities and groups") {
    ct::EditorDocument document(level(), "test.level");
    std::string problem;
    CHECK(ct::renameItem(document, LevelItem::brush(0), "left wall", problem));
    CHECK(document.level().brushes[0].name == "left wall");
    CHECK_FALSE(ct::renameItem(document, LevelItem::brush(0), " padded", problem));
    CHECK_FALSE(problem.empty());
    CHECK(ct::renameItem(document, LevelItem::brush(0), "", problem));
    CHECK(document.level().brushes[0].name.empty());

    CHECK(ct::renameItem(document, LevelItem::entity(1), "door_1", problem));
    CHECK(*ct::findProperty(document.level().entities[1], "name") == "door_1");
    CHECK_FALSE(ct::renameItem(document, LevelItem::entity(1), "has spaces", problem));
    CHECK(ct::renameItem(document, LevelItem::entity(1), "", problem));
    CHECK(ct::findProperty(document.level().entities[1], "name") == nullptr);

    document.setSelection({LevelItem::brush(0)});
    ct::groupSelection(document);
    document.setSelection({LevelItem::brush(3)});
    ct::groupSelection(document);
    CHECK(ct::renameGroup(document, 0, "Stairs", problem));
    CHECK(document.level().groups[0].name == "Stairs");
    CHECK_FALSE(ct::renameGroup(document, 1, "Stairs", problem));
    CHECK_FALSE(ct::renameGroup(document, 1, "", problem));
    CHECK(ct::unusedGroupName(document.level(), "Stairs") == "Stairs 2");
    document.undo();
    CHECK(document.level().groups[0].name == "Group");
}
