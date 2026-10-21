#include "croftech/editor/BrushEditing.h"
#include "croftech/editor/EditorActions.h"
#include "croftech/level/BrushGeometry.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace {

bool hasCorner(const ct::Brush& brush, ct::Vec3 corner) {
    return std::find(brush.vertices.begin(), brush.vertices.end(), corner) != brush.vertices.end();
}

std::size_t triangles(const ct::Brush& brush) {
    return static_cast<std::size_t>(
        std::count_if(brush.faces.begin(), brush.faces.end(),
                      [](const ct::BrushFace& face) { return face.corners.size() == 3; }));
}

const ct::Brush kBox = ct::makeBoxBrush({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f});

} // namespace

TEST_CASE("a box has twelve edges") {
    const auto edges = ct::brushEdges(kBox);
    CHECK(edges.size() == 12);
    for (const auto& [a, b] : edges) {
        CHECK(a < b);
    }
}

TEST_CASE("raising one corner of a box folds its top into two triangles") {
    const std::vector<ct::Vec3> corner{{1.0f, 1.0f, 1.0f}};
    const auto raised = ct::withCornersMoved(kBox, corner, {0.0f, 0.0f, 0.5f});
    REQUIRE(raised.has_value());
    CHECK(raised->vertices.size() == 8);
    CHECK(hasCorner(*raised, {1.0f, 1.0f, 1.5f}));
    CHECK(raised->faces.size() == 7);
    CHECK(triangles(*raised) == 2);

    // Not a corner of this brush: nothing to do.
    CHECK(ct::withCornersMoved(kBox, std::vector<ct::Vec3>{{5.0f, 5.0f, 5.0f}},
                               {1.0f, 0.0f, 0.0f}) == kBox);
}

TEST_CASE("moves that would dent or flatten a brush are refused") {
    // Pushed in towards the middle: only a concave brush could have it there.
    CHECK_FALSE(ct::withCornersMoved(kBox, std::vector<ct::Vec3>{{1.0f, 1.0f, 1.0f}},
                                     {-0.5f, -0.5f, -0.5f}));
    // Pulled in less, it is a cut corner: still convex, so allowed.
    CHECK(ct::withCornersMoved(kBox, std::vector<ct::Vec3>{{1.0f, 1.0f, 1.0f}},
                               {-0.25f, -0.25f, -0.25f}));
    // Slid along the top onto the diagonal between two other corners: no longer a corner.
    CHECK_FALSE(ct::withCornersMoved(kBox, std::vector<ct::Vec3>{{1.0f, 1.0f, 1.0f}},
                                     {-0.5f, -0.5f, 0.0f}));
    // Pulled out above (1, 0, 1), it leaves that corner in the middle of an edge.
    CHECK_FALSE(
        ct::withCornersMoved(kBox, std::vector<ct::Vec3>{{1.0f, 1.0f, 1.0f}}, {0.0f, -1.0f, 1.0f}));
    // The whole top moved onto the bottom: flat.
    const std::vector<ct::Vec3> top{
        {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}};
    CHECK_FALSE(ct::withCornersMoved(kBox, top, {0.0f, 0.0f, -1.0f}));
}

TEST_CASE("an edge moved onto the one below merges into a wedge") {
    const std::vector<ct::Vec3> topLeftEdge{{0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 1.0f}};
    const auto wedge = ct::withCornersMoved(kBox, topLeftEdge, {0.0f, 0.0f, -1.0f});
    REQUIRE(wedge.has_value());
    CHECK(wedge->vertices.size() == 6);
    CHECK(wedge->faces.size() == 5);
    CHECK(triangles(*wedge) == 2);
}

TEST_CASE("removing a corner cuts it off") {
    const auto cut = ct::withCornersRemoved(kBox, std::vector<ct::Vec3>{{1.0f, 1.0f, 1.0f}});
    REQUIRE(cut.has_value());
    CHECK(cut->vertices.size() == 7);
    CHECK(cut->faces.size() == 7);
    CHECK_FALSE(hasCorner(*cut, {1.0f, 1.0f, 1.0f}));
    const std::vector<ct::Vec3> top{
        {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}};
    CHECK_FALSE(ct::withCornersRemoved(kBox, top)); // only the flat bottom is left
}

TEST_CASE("corners on the grid stay exactly on it through thousands of edits, 2 km out") {
    // The check that decided how brushes are stored: with planes, corners drifted up to 4 mm here.
    const float grid = 0.125f;
    ct::Brush brush = ct::makeBoxBrush({1500.25f, -1800.5f, 600.0f}, {1504.25f, -1796.5f, 604.0f});
    std::uint32_t seed = 12345;
    const auto random = [&seed](std::uint32_t range) {
        seed = seed * 1664525u + 1013904223u;
        return (seed >> 8) % range;
    };
    int accepted = 0;
    for (int edit = 0; edit < 2000; ++edit) {
        const ct::Vec3 corner =
            brush.vertices[random(static_cast<std::uint32_t>(brush.vertices.size()))];
        const auto step = [&] { return (static_cast<float>(random(5)) - 2.0f) * grid; };
        const ct::Vec3 offset{step(), step(), step()};
        if (const auto moved = ct::withCornersMoved(brush, std::vector<ct::Vec3>{corner}, offset)) {
            brush = *moved;
            ++accepted;
        }
    }
    CHECK(accepted > 200);
    std::string problem;
    CHECK(ct::checkBrush(brush, problem));
    for (const ct::Vec3& v : brush.vertices) {
        for (const float value : {v.x, v.y, v.z}) {
            CHECK(value / grid == std::round(value / grid));
        }
    }
}

TEST_CASE("corners shared by selected brushes move together as one command") {
    ct::Level level;
    level.brushes = {ct::makeBoxBrush({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}),
                     ct::makeBoxBrush({1.0f, 0.0f, 0.0f}, {2.0f, 1.0f, 1.0f}),
                     ct::makeBoxBrush({5.0f, 5.0f, 0.0f}, {6.0f, 6.0f, 1.0f})};
    ct::EditorDocument document(level, "test.level");
    document.setSelection({ct::LevelItem::brush(0), ct::LevelItem::brush(1)});

    // The shared top edge rises: both brushes keep meeting along it.
    const std::vector<ct::Vec3> edge{{1.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}};
    REQUIRE(ct::moveCorners(document, edge, {0.0f, 0.0f, 0.5f}));
    CHECK(hasCorner(document.level().brushes[0], {1.0f, 0.0f, 1.5f}));
    CHECK(hasCorner(document.level().brushes[1], {1.0f, 1.0f, 1.5f}));
    CHECK(document.level().brushes[2] == level.brushes[2]);
    CHECK(document.history().undoName() == "Move corners");
    document.undo();
    CHECK(document.level() == level);

    // One brush refusing stops the whole move.
    CHECK_FALSE(ct::moveCorners(document, edge, {-0.5f, 0.0f, -0.5f}));
    CHECK(document.level() == level);

    REQUIRE(ct::removeCorners(document, std::vector<ct::Vec3>{{1.0f, 1.0f, 1.0f}}));
    CHECK(document.level().brushes[0].vertices.size() == 7);
    CHECK(document.level().brushes[1].vertices.size() == 7);
    CHECK(document.history().undoName() == "Delete corner");
}
