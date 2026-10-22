#include "croftech/editor/BrushEditing.h"
#include "croftech/editor/EditorActions.h"
#include "croftech/editor/EditorDocument.h"
#include "croftech/editor/LevelItem.h"
#include "croftech/editor/SelectionDrag.h"
#include "croftech/level/BrushGeometry.h"
#include "croftech/level/EntityClasses.h"

#include <doctest/doctest.h>

#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace {

using ct::LevelItem;

ct::Brush box(float x, float width = 1.0f) {
    return ct::makeBoxBrush({x, 0.0f, 0.0f}, {x + width, 1.0f, 1.0f});
}

ct::Level threeBoxesAndASpawn() {
    ct::Level level;
    level.brushes = {box(0.0f), box(2.0f), box(4.0f)};
    level.entities.push_back(ct::makePlayerSpawn({0.0f, 0.0f, 1.0f}, 0.0f));
    return level;
}

} // namespace

TEST_CASE("the selection is a sorted set of items that exist") {
    ct::EditorDocument document(threeBoxesAndASpawn(), "test.level");
    document.setSelection({LevelItem::entity(0), LevelItem::brush(2), LevelItem::brush(0),
                           LevelItem::brush(2), LevelItem::brush(7)});
    CHECK(document.selection() ==
          std::vector<LevelItem>{LevelItem::brush(0), LevelItem::brush(2), LevelItem::entity(0)});
    CHECK(document.isSelected(LevelItem::brush(2)));
    CHECK_FALSE(document.isSelected(LevelItem::brush(1)));
    CHECK_FALSE(document.singleSelected().has_value());

    document.toggleSelected(LevelItem::brush(2));
    document.toggleSelected(LevelItem::brush(1));
    document.toggleSelected(LevelItem::entity(3)); // doesn't exist
    CHECK(document.selection() ==
          std::vector<LevelItem>{LevelItem::brush(0), LevelItem::brush(1), LevelItem::entity(0)});

    document.select(LevelItem::brush(1));
    CHECK(document.singleSelected() == LevelItem::brush(1));
    document.clearSelection();
    CHECK(document.selection().empty());

    ct::selectAll(document);
    CHECK(document.selection().size() == 4);
    CHECK(document.isSelected(LevelItem::entity(0)));
}

TEST_CASE("deleting several items is one command, and undo restores them in place") {
    ct::EditorDocument document(threeBoxesAndASpawn(), "test.level");
    document.setSelection({LevelItem::brush(0), LevelItem::brush(2), LevelItem::entity(0)});
    REQUIRE(ct::deleteSelection(document));
    REQUIRE(document.level().brushes.size() == 1);
    CHECK(document.level().brushes[0] == box(2.0f));
    CHECK(document.level().entities.empty());
    CHECK(document.selection().empty());
    CHECK(document.history().undoName() == "Delete 3 items");

    document.undo();
    CHECK(document.level() == threeBoxesAndASpawn());
    CHECK_FALSE(document.history().canUndo());
}

TEST_CASE("duplicating several items selects the copies") {
    ct::EditorDocument document(threeBoxesAndASpawn(), "test.level");
    document.setSelection({LevelItem::brush(1), LevelItem::entity(0)});
    REQUIRE(ct::duplicateSelection(document, {0.0f, 0.0f, 2.0f}));
    CHECK(document.selection() ==
          std::vector<LevelItem>{LevelItem::brush(3), LevelItem::entity(1)});
    CHECK(ct::boxBounds(document.level().brushes[3])->min == ct::Vec3{2.0f, 0.0f, 2.0f});
    CHECK(ct::entityOrigin(document.level().entities[1]) == ct::Vec3{0.0f, 0.0f, 3.0f});
    document.undo();
    CHECK(document.level() == threeBoxesAndASpawn());

    // A copy outside the world fails as a whole.
    document.setSelection({LevelItem::brush(0), LevelItem::brush(1)});
    CHECK_FALSE(ct::duplicateSelection(document, {3000.0f, 0.0f, 0.0f}));
    CHECK(document.level() == threeBoxesAndASpawn());
}

TEST_CASE("transforming the selection moves every item in one command, or none") {
    ct::EditorDocument document(threeBoxesAndASpawn(), "test.level");
    document.setSelection({LevelItem::brush(0), LevelItem::brush(1), LevelItem::entity(0)});
    REQUIRE(
        ct::transformSelection(document, ct::AffineTransform::moveBy({0.0f, 1.0f, 0.0f}), "Move"));
    CHECK(ct::boxBounds(document.level().brushes[1])->min == ct::Vec3{2.0f, 1.0f, 0.0f});
    CHECK(ct::entityOrigin(document.level().entities[0]) == ct::Vec3{0.0f, 1.0f, 1.0f});
    CHECK(document.level().brushes[2] == box(4.0f));
    CHECK(document.history().undoName() == "Move 3 items");
    document.undo();
    CHECK(document.level() == threeBoxesAndASpawn());

    CHECK_FALSE(ct::transformSelection(document, ct::AffineTransform::moveBy({2047.0f, 0.0f, 0.0f}),
                                       "Move"));
    CHECK(document.level() == threeBoxesAndASpawn());
    CHECK_FALSE(document.history().canUndo());
}

TEST_CASE("quarter turns pivot on the selection's centre and leave it on the grid") {
    ct::Level level;
    level.brushes = {ct::makeBoxBrush({0.0f, 0.0f, 0.0f}, {1.0f, 2.0f, 1.0f})};
    ct::EditorDocument document(level, "test.level");
    document.select(LevelItem::brush(0));
    REQUIRE(ct::rotateSelection(document, 2, 1, 1.0f));
    const auto bounds = ct::boxBounds(document.level().brushes[0]);
    REQUIRE(bounds.has_value());
    // 2 wide and 1 deep now, still overlapping where it was and on whole metres.
    CHECK(bounds->max - bounds->min == ct::Vec3{2.0f, 1.0f, 1.0f});
    CHECK(bounds->min == ct::snapToGrid(bounds->min, 1.0f));
    CHECK(bounds->min.x <= 0.0f);
    CHECK(bounds->max.x >= 1.0f);
    CHECK(document.history().undoName() == "Rotate brush");
}

TEST_CASE("flipping mirrors in place and turns spawns to face the mirrored way") {
    ct::Level level;
    level.brushes = {ct::makeRampBrush({0.0f, 0.0f, 0.0f}, {2.0f, 1.0f, 1.0f})};
    level.entities = {ct::makePlayerSpawn({4.0f, 0.0f, 0.0f}, 30.0f)};
    ct::EditorDocument document(level, "test.level");
    document.select(LevelItem::entity(0));
    REQUIRE(ct::flipSelection(document, 1, 0.25f));
    CHECK(ct::entityOrigin(document.level().entities[0]) == ct::Vec3{4.0f, 0.0f, 0.0f});
    CHECK(*ct::findProperty(document.level().entities[0], "yaw") == "-30");

    document.select(LevelItem::brush(0));
    REQUIRE(ct::flipSelection(document, 0, 0.25f));
    ct::Aabb bounds;
    REQUIRE(ct::computeBrushBounds(document.level().brushes[0], bounds));
    CHECK(bounds.min.x == doctest::Approx(0.0f));
    CHECK(bounds.max.x == doctest::Approx(2.0f));
    // Now high at x = 0.
    const auto top =
        ct::rayCastBrush(document.level().brushes[0], {0.1f, 0.5f, 5.0f}, {0.0f, 0.0f, -1.0f});
    REQUIRE(top.has_value());
    CHECK(top->distance < 4.2f);
}

TEST_CASE("a selection drag previews without history and commits once") {
    ct::EditorDocument document(threeBoxesAndASpawn(), "test.level");
    document.setSelection({LevelItem::brush(0), LevelItem::entity(0)});
    ct::SelectionDrag drag(document);
    CHECK(drag.preview(document, ct::AffineTransform::moveBy({1.0f, 0.0f, 0.0f})));
    CHECK(drag.preview(document, ct::AffineTransform::moveBy({2.0f, 0.0f, 0.0f})));
    CHECK(ct::boxBounds(document.level().brushes[0])->min.x == 2.0f);
    CHECK_FALSE(document.history().canUndo());
    // An invalid step keeps the last good preview.
    CHECK_FALSE(drag.preview(document, ct::AffineTransform::moveBy({4000.0f, 0.0f, 0.0f})));
    CHECK(ct::boxBounds(document.level().brushes[0])->min.x == 2.0f);

    drag.cancel(document);
    CHECK(document.level() == threeBoxesAndASpawn());

    CHECK(drag.preview(document, ct::AffineTransform::moveBy({0.0f, 0.0f, 3.0f})));
    CHECK(drag.commit(document, "Move 2 items"));
    document.undo();
    CHECK(document.level() == threeBoxesAndASpawn());
    CHECK_FALSE(drag.commit(document, "Move 2 items")); // nothing moved now
}

TEST_CASE("item bounds cover brushes and entity markers") {
    const ct::Level level = threeBoxesAndASpawn();
    ct::BodySizes bodies;
    bodies.player = {0.5f, 0.5f, 1.0f};
    const std::vector<LevelItem> items{LevelItem::brush(2), LevelItem::entity(0)};
    const auto bounds = ct::itemBounds(level, items, bodies);
    REQUIRE(bounds.has_value());
    CHECK(bounds->min == ct::Vec3{-0.5f, -0.5f, 0.0f});
    CHECK(bounds->max == ct::Vec3{5.0f, 1.0f, 3.0f});
    CHECK_FALSE(ct::itemBounds(level, LevelItem::brush(9), bodies).has_value());
}

TEST_CASE("enemy and spawner markers are the size of their kind") {
    ct::BodySizes bodies;
    bodies.player = {0.4f, 0.4f, 0.9f};
    const auto enemy = [](const char* type) {
        ct::LevelEntity entity{std::string(ct::kEnemyClass), {{"origin", "0 0 0"}}};
        if (type != nullptr) {
            entity.properties.push_back({"type", type});
        }
        return entity;
    };
    // With no kinds known, the player's size.
    CHECK(ct::entityMarker(enemy("gunner"), bodies)->max == ct::Vec3{0.4f, 0.4f, 1.8f});
    bodies.enemies = {{"charger", {0.5f, 0.5f, 1.0f}}, {"gunner", {0.3f, 0.3f, 1.1f}}};
    CHECK(ct::entityMarker(enemy("gunner"), bodies)->max == ct::Vec3{0.3f, 0.3f, 2.2f});
    CHECK(ct::entityMarker(enemy("gunner"), bodies)->min == ct::Vec3{-0.3f, -0.3f, 0.0f});
    // No type is the first kind; one the game doesn't have, the player's size.
    CHECK(ct::entityMarker(enemy(nullptr), bodies)->max == ct::Vec3{0.5f, 0.5f, 2.0f});
    CHECK(ct::entityMarker(enemy(""), bodies)->max == ct::Vec3{0.5f, 0.5f, 2.0f});
    CHECK(ct::entityMarker(enemy("ghost"), bodies)->max == ct::Vec3{0.4f, 0.4f, 1.8f});
    const ct::LevelEntity spawn{std::string(ct::kPlayerSpawnClass), {{"origin", "0 0 0"}}};
    CHECK(ct::entityMarker(spawn, bodies)->max == ct::Vec3{0.4f, 0.4f, 1.8f});
    // Spawners show the enemy they send.
    const ct::LevelEntity spawner{std::string(ct::kSpawnerClass),
                                  {{"origin", "0 0 0"}, {"type", "gunner"}}};
    CHECK(ct::entityMarker(spawner, bodies)->max == ct::Vec3{0.3f, 0.3f, 2.2f});
}

TEST_CASE("entities with a yaw face that way, or their class's default") {
    const std::optional<ct::Vec3> north =
        ct::entityFacing(ct::makePlayerSpawn({0.0f, 0.0f, 0.0f}, 90.0f));
    REQUIRE(north);
    CHECK(std::fabs(north->x) < 1e-6f);
    CHECK(north->y == doctest::Approx(1.0f));
    // An enemy left at its class's default faces +X.
    const ct::LevelEntity enemy{"enemy", {{"origin", "0 0 0"}}, {}};
    const std::optional<ct::Vec3> east = ct::entityFacing(enemy);
    REQUIRE(east);
    CHECK(east->x == doctest::Approx(1.0f));
    // Pickups and relays don't face anywhere.
    CHECK_FALSE(ct::entityFacing({"pickup", {{"origin", "0 0 0"}}, {}}));
    CHECK_FALSE(ct::entityFacing({"relay", {{"origin", "0 0 0"}}, {}}));
}
