#include "croftech/editor/BrushEditing.h"
#include "croftech/editor/BrushOperations.h"
#include "croftech/editor/EditorActions.h"

#include <doctest/doctest.h>

#include <cmath>
#include <optional>
#include <vector>

namespace {

double volume(const ct::Brush& brush) {
    double total = 0.0;
    for (std::size_t face = 0; face < brush.faces.size(); ++face) {
        total += ct::exactFacePlane(brush, face).distance / 6.0;
    }
    return total;
}

double volume(const std::vector<ct::Brush>& brushes) {
    double total = 0.0;
    for (const ct::Brush& brush : brushes) {
        total += volume(brush);
    }
    return total;
}

bool onGrid(const std::vector<ct::Brush>& brushes, float grid) {
    for (const ct::Brush& brush : brushes) {
        for (const ct::Vec3& v : brush.vertices) {
            for (const float value : {v.x, v.y, v.z}) {
                if (value / grid != std::round(value / grid)) {
                    return false;
                }
            }
        }
    }
    return true;
}

const ct::Brush kWall = ct::makeBoxBrush({0.0f, 0.0f, 0.0f}, {4.0f, 0.5f, 3.0f});
const ct::Brush kDoor = ct::makeBoxBrush({1.0f, -1.0f, 0.0f}, {2.0f, 1.0f, 2.0f});

} // namespace

TEST_CASE("splitting a box gives both halves, exactly on the cut") {
    const ct::Brush box = ct::makeBoxBrush({0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 2.0f});
    const auto split = ct::splitBrush(box, {{1.0, 0.0, 0.0}, 0.75});
    REQUIRE(split.has_value());
    REQUIRE(split->back.has_value());
    REQUIRE(split->front.has_value());
    CHECK(ct::boxBounds(*split->back)->max.x == 0.75f);
    CHECK(ct::boxBounds(*split->front)->min.x == 0.75f);

    const auto missed = ct::splitBrush(box, {{1.0, 0.0, 0.0}, 5.0});
    REQUIRE(missed.has_value());
    CHECK(missed->back == box);
    CHECK_FALSE(missed->front.has_value());
}

TEST_CASE("subtracting a doorway leaves pieces around it that fill the rest exactly") {
    const auto pieces = ct::subtractBrush(kWall, kDoor);
    REQUIRE(pieces.has_value());
    CHECK(pieces->size() == 3); // left, right and above
    CHECK(volume(*pieces) == doctest::Approx(volume(kWall) - 1.0 * 0.5 * 2.0));
    CHECK(onGrid(*pieces, 0.5f));

    // Apart, or only touching: unchanged. Covered: nothing left.
    const ct::Brush apart = ct::makeBoxBrush({10.0f, 0.0f, 0.0f}, {11.0f, 1.0f, 1.0f});
    CHECK(ct::subtractBrush(kWall, apart) == std::vector<ct::Brush>{kWall});
    const ct::Brush touching = ct::makeBoxBrush({4.0f, 0.0f, 0.0f}, {5.0f, 0.5f, 3.0f});
    CHECK(ct::subtractBrush(kWall, touching) == std::vector<ct::Brush>{kWall});
    const ct::Brush covering = ct::makeBoxBrush({-1.0f, -1.0f, -1.0f}, {5.0f, 5.0f, 5.0f});
    CHECK(ct::subtractBrush(kWall, covering)->empty());
}

TEST_CASE("a cylinder cut out of a wall leaves a round hole") {
    const ct::Brush wall = ct::makeBoxBrush({-2.0f, 0.0f, 0.0f}, {2.0f, 0.5f, 4.0f});
    // An 8-sided prism along Y through the middle of the wall.
    std::vector<ct::Vec3> corners;
    for (int i = 0; i < 8; ++i) {
        const double angle = 3.14159265358979 * (2.0 * i + 1.0) / 8.0;
        const float x = static_cast<float>(std::cos(angle));
        const float z = 2.0f + static_cast<float>(std::sin(angle));
        corners.push_back({x, -1.0f, z});
        corners.push_back({x, 1.5f, z});
    }
    const auto hole = ct::brushFromPoints(corners);
    REQUIRE(hole.has_value());
    const auto pieces = ct::subtractBrush(wall, *hole);
    REQUIRE(pieces.has_value());
    CHECK(pieces->size() == 8);
    // The prism's cross-section is a regular octagon of circumradius 1.
    const double octagon = 2.0 * std::sqrt(2.0);
    CHECK(volume(*pieces) == doctest::Approx(volume(wall) - octagon * 0.5).epsilon(1e-5));
}

TEST_CASE("hollowing a box leaves six walls of the given thickness") {
    const ct::Brush box = ct::makeBoxBrush({0.0f, 0.0f, 0.0f}, {4.0f, 4.0f, 4.0f});
    const auto walls = ct::hollowBrush(box, 0.5f);
    REQUIRE(walls.has_value());
    CHECK(walls->size() == 6);
    CHECK(volume(*walls) == doctest::Approx(64.0 - 27.0));
    CHECK(onGrid(*walls, 0.5f));
    CHECK_FALSE(ct::hollowBrush(box, 2.0f).has_value()); // nothing left inside
    CHECK_FALSE(ct::hollowBrush(box, 0.0f).has_value());
}

TEST_CASE("merging makes one brush around all the corners") {
    const std::vector<ct::Brush> brushes{ct::makeBoxBrush({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}),
                                         ct::makeBoxBrush({1.0f, 0.0f, 0.0f}, {2.0f, 1.0f, 1.0f})};
    const auto merged = ct::mergeBrushes(brushes);
    REQUIRE(merged.has_value());
    CHECK(ct::boxBounds(*merged)->max == ct::Vec3{2.0f, 1.0f, 1.0f});
    CHECK(merged->vertices.size() == 8);
}

TEST_CASE("extruding a face pulls a new brush out of it") {
    const ct::Brush box = ct::makeBoxBrush({0.0f, 0.0f, 0.0f}, {1.0f, 2.0f, 1.0f});
    const auto block = ct::extrudeFace(box, 4, 1.5f); // the top
    REQUIRE(block.has_value());
    const auto bounds = ct::boxBounds(*block);
    REQUIRE(bounds.has_value());
    CHECK(bounds->min == ct::Vec3{0.0f, 0.0f, 1.0f});
    CHECK(bounds->max == ct::Vec3{1.0f, 2.0f, 2.5f});
    CHECK_FALSE(ct::extrudeFace(box, 4, 0.0f));
    CHECK_FALSE(ct::extrudeFace(box, 9, 1.0f));
}

TEST_CASE("clipping the selection keeps the chosen side, as one command") {
    ct::Level level;
    level.brushes = {ct::makeBoxBrush({0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 2.0f}),
                     ct::makeBoxBrush({5.0f, 0.0f, 0.0f}, {6.0f, 1.0f, 1.0f})};
    ct::EditorDocument document(level, "test.level");
    document.setSelection({ct::LevelItem::brush(0), ct::LevelItem::brush(1)});
    const ct::DPlane cut{{1.0, 0.0, 0.0}, 1.0}; // x <= 1 is behind

    REQUIRE(ct::clipSelection(document, cut, ct::ClipKeep::Both));
    REQUIRE(document.level().brushes.size() == 3);
    CHECK(ct::boxBounds(document.level().brushes[0])->max.x == 1.0f);
    CHECK(ct::boxBounds(document.level().brushes[2])->min.x == 1.0f);
    CHECK(document.level().brushes[1] == level.brushes[1]); // missed
    CHECK(document.selection().size() == 3);
    document.undo();
    CHECK(document.level() == level);

    document.setSelection({ct::LevelItem::brush(0)});
    REQUIRE(ct::clipSelection(document, cut, ct::ClipKeep::Front));
    CHECK(document.level().brushes.size() == 2);
    CHECK(ct::boxBounds(document.level().brushes[0])->min.x == 1.0f);
    CHECK(document.history().undoName() == "Clip brush");

    document.setSelection({ct::LevelItem::brush(1)});
    CHECK_FALSE(ct::clipSelection(document, cut, ct::ClipKeep::Back)); // crosses nothing
}

TEST_CASE("subtracting the selection cuts other brushes and removes the cutter") {
    ct::Level level;
    level.brushes = {kWall, ct::makeBoxBrush({8.0f, 0.0f, 0.0f}, {9.0f, 1.0f, 1.0f}), kDoor};
    ct::EditorDocument document(level, "test.level");
    document.select(ct::LevelItem::brush(2));
    REQUIRE(ct::subtractSelection(document));
    const ct::Level& after = document.level();
    REQUIRE(after.brushes.size() == 4); // three wall pieces and the untouched box
    CHECK(after.brushes[1] == level.brushes[1]);
    CHECK(volume(after.brushes) == doctest::Approx(volume(kWall) - 1.0 + volume(level.brushes[1])));
    CHECK(document.selection().empty());
    document.undo();
    CHECK(document.level() == level);

    document.select(ct::LevelItem::brush(1));
    CHECK_FALSE(ct::subtractSelection(document)); // overlaps nothing
    CHECK(document.level() == level);
}

TEST_CASE("hollow and merge replace the selection and select the result") {
    ct::Level level;
    level.brushes = {ct::makeBoxBrush({0.0f, 0.0f, 0.0f}, {4.0f, 4.0f, 4.0f}),
                     ct::makeBoxBrush({4.0f, 0.0f, 0.0f}, {5.0f, 4.0f, 4.0f})};
    ct::EditorDocument document(level, "test.level");
    document.select(ct::LevelItem::brush(0));
    REQUIRE(ct::hollowSelection(document, 0.5f));
    CHECK(document.level().brushes.size() == 7);
    CHECK(document.selection().size() == 6);
    CHECK_FALSE(document.isSelected(ct::LevelItem::brush(1)));
    document.undo();

    document.setSelection({ct::LevelItem::brush(0), ct::LevelItem::brush(1)});
    REQUIRE(ct::mergeSelection(document));
    REQUIRE(document.level().brushes.size() == 1);
    CHECK(ct::boxBounds(document.level().brushes[0])->max.x == 5.0f);
    CHECK(document.selection() == std::vector<ct::LevelItem>{ct::LevelItem::brush(0)});
    document.undo();
    CHECK(document.level() == level);
}
