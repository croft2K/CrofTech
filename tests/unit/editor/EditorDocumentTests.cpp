#include "TestFiles.h"

#include "croftech/editor/BrushEditing.h"
#include "croftech/editor/EditorActions.h"
#include "croftech/editor/EditorDocument.h"
#include "croftech/editor/LevelCommands.h"
#include "croftech/level/LevelFile.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <vector>

namespace {

ct::Brush unitBox(float x) { return ct::makeBoxBrush({x, 0.0f, 0.0f}, {x + 1.0f, 1.0f, 1.0f}); }

ct::Level twoBoxes() {
    ct::Level level;
    level.brushes = {unitBox(0.0f), unitBox(2.0f)};
    level.entities.push_back(ct::makePlayerSpawn({0.0f, 0.0f, 1.0f}, 0.0f));
    return level;
}

} // namespace

TEST_CASE("commands undo and redo in order, and new edits drop the redo history") {
    ct::EditorDocument document(twoBoxes(), "test.level");
    const ct::Level original = document.level();

    document.execute(ct::makeSetBrush("Move brush", 0, unitBox(0.0f), unitBox(5.0f)));
    document.execute(ct::makeRemoveBrush("Delete brush", 1, unitBox(2.0f)));
    REQUIRE(document.level().brushes.size() == 1);
    CHECK(document.level().brushes[0] == unitBox(5.0f));
    CHECK(document.history().undoName() == "Delete brush");

    CHECK(document.undo());
    CHECK(document.level().brushes.size() == 2);
    CHECK(document.history().redoName() == "Delete brush");
    CHECK(document.undo());
    CHECK(document.level() == original);
    CHECK_FALSE(document.undo());

    CHECK(document.redo());
    CHECK(document.level().brushes[0] == unitBox(5.0f));

    document.execute(ct::makeInsertBrush("Create brush", 2, unitBox(9.0f)));
    CHECK_FALSE(document.history().canRedo());
    CHECK(document.level().brushes.size() == 3);
}

TEST_CASE("entity commands set, insert and remove") {
    ct::EditorDocument document(twoBoxes(), "test.level");
    ct::LevelEntity moved = document.level().entities[0];
    ct::setProperty(moved, "origin", ct::formatVec3({4.0f, 0.0f, 1.0f}));
    document.execute(ct::makeSetEntity("Move entity", 0, document.level().entities[0], moved));
    CHECK(ct::entityOrigin(document.level().entities[0]) == ct::Vec3{4.0f, 0.0f, 1.0f});

    document.execute(ct::makeInsertEntity("Add entity", 0, {"light", {}}));
    CHECK(document.level().entities[0].className == "light");
    document.execute(ct::makeRemoveEntity("Delete entity", 1, moved));
    CHECK(document.level().entities.size() == 1);

    document.undo();
    document.undo();
    document.undo();
    CHECK(document.level() == twoBoxes());
}

TEST_CASE("previews change the level without adding history") {
    ct::EditorDocument document(twoBoxes(), "test.level");
    const auto changes = document.changeCount();
    document.previewBrush(0, unitBox(3.0f));
    CHECK(document.level().brushes[0] == unitBox(3.0f));
    CHECK(document.changeCount() > changes);
    CHECK_FALSE(document.history().canUndo());
    CHECK_FALSE(document.hasUnsavedChanges());

    // Finishing the drag records one command from the pre-drag state.
    document.execute(ct::makeSetBrush("Move brush", 0, unitBox(0.0f), unitBox(3.0f)));
    CHECK(document.hasUnsavedChanges());
    document.undo();
    CHECK(document.level().brushes[0] == unitBox(0.0f));
}

TEST_CASE("unsaved changes are tracked through undo, redo and saving") {
    ct::test::TempDirectory temp;
    ct::EditorDocument document(twoBoxes(), temp.path() / "level.level");
    CHECK_FALSE(document.hasUnsavedChanges());
    document.execute(ct::makeSetBrush("Move brush", 0, unitBox(0.0f), unitBox(5.0f)));
    CHECK(document.hasUnsavedChanges());
    document.undo();
    CHECK_FALSE(document.hasUnsavedChanges());
    document.redo();

    std::string error;
    REQUIRE_MESSAGE(document.save(error), error);
    CHECK_FALSE(document.hasUnsavedChanges());
    CHECK(ct::loadLevelFile(temp.path() / "level.level", error) == document.level());
    document.undo();
    CHECK(document.hasUnsavedChanges());
}

TEST_CASE("a level imported from a .map saves as a .level next to it") {
    ct::test::TempDirectory temp;
    ct::EditorDocument document(twoBoxes(), temp.path() / "arena.map");
    std::string error;
    REQUIRE_MESSAGE(document.save(error), error);
    CHECK(document.path() == temp.path() / "arena.level");
    CHECK(std::filesystem::exists(temp.path() / "arena.level"));
}

TEST_CASE("selection is cleared when undo removes what was selected") {
    ct::EditorDocument document(twoBoxes(), "test.level");
    document.execute(ct::makeInsertBrush("Create brush", 2, unitBox(9.0f)));
    document.setSelection({ct::LevelItem::brush(0), ct::LevelItem::brush(2)});
    CHECK(document.isSelected(ct::LevelItem::brush(2)));
    document.undo();
    CHECK(document.selection() == std::vector<ct::LevelItem>{ct::LevelItem::brush(0)});

    document.select(ct::LevelItem::entity(5));
    CHECK(document.selection().empty());
}

TEST_CASE("delete and duplicate act on the selection as single commands") {
    ct::EditorDocument document(twoBoxes(), "test.level");
    CHECK_FALSE(ct::deleteSelection(document));

    document.select(ct::LevelItem::brush(0));
    CHECK(ct::duplicateSelection(document, {0.0f, 0.0f, 4.0f}));
    REQUIRE(document.level().brushes.size() == 3);
    CHECK(document.singleSelected() == ct::LevelItem::brush(2));
    CHECK(ct::boxBounds(document.level().brushes[2])->min == ct::Vec3{0.0f, 0.0f, 4.0f});

    CHECK(ct::deleteSelection(document));
    CHECK(document.level().brushes.size() == 2);
    CHECK(document.selection().empty());

    document.select(ct::LevelItem::entity(0));
    CHECK(ct::duplicateSelection(document, {1.0f, 0.0f, 0.0f}));
    CHECK(ct::entityOrigin(document.level().entities[1]) == ct::Vec3{1.0f, 0.0f, 1.0f});

    ct::addBox(document, {0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 2.0f});
    CHECK(document.singleSelected() == ct::LevelItem::brush(2));
    ct::addEntity(document, {"light", {}});
    CHECK(document.singleSelected() == ct::LevelItem::entity(2));
    CHECK(document.history().undoName() == "Add light");

    for (int i = 0; i < 5; ++i) {
        document.undo();
    }
    CHECK(document.level() == twoBoxes());
}

TEST_CASE("each document counts its changes from a start no other document has") {
    const ct::EditorDocument first(ct::Level{}, "a.level");
    const ct::EditorDocument second(ct::Level{}, "b.level");
    CHECK(first.changeCount() != second.changeCount());
}
