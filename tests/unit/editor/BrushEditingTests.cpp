#include "croftech/editor/BrushEditing.h"

#include <doctest/doctest.h>

#include <cmath>
#include <optional>

TEST_CASE("values snap to the nearest grid line") {
    CHECK(ct::snapToGrid(0.13f, 0.25f) == 0.25f);
    CHECK(ct::snapToGrid(-0.13f, 0.25f) == -0.25f);
    CHECK(ct::snapToGrid(0.12f, 0.25f) == 0.0f);
    CHECK(ct::snapToGrid(ct::Vec3{1.4f, -2.6f, 0.0f}, 1.0f) == ct::Vec3{1.0f, -3.0f, 0.0f});
}

TEST_CASE("translating a box keeps it a box at the new place") {
    const ct::Brush box = ct::makeBoxBrush({0.0f, 0.0f, 0.0f}, {1.0f, 2.0f, 3.0f});
    const auto bounds = ct::boxBounds(ct::translatedBrush(box, {0.5f, -1.0f, 2.0f}));
    REQUIRE(bounds.has_value());
    CHECK(bounds->min == ct::Vec3{0.5f, -1.0f, 2.0f});
    CHECK(bounds->max == ct::Vec3{1.5f, 1.0f, 5.0f});
}

TEST_CASE("only six axis-aligned planes make a box") {
    CHECK(ct::boxBounds(ct::makeBoxBrush({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f})).has_value());
    CHECK_FALSE(ct::boxBounds(ct::makeRampBrush({0.0f, 0.0f, 0.0f}, {2.0f, 1.0f, 1.0f})));
    ct::Brush twoTops = ct::makeBoxBrush({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f});
    twoTops.planes[5] = twoTops.planes[4];
    CHECK_FALSE(ct::boxBounds(twoTops));
}

TEST_CASE("moving a face resizes the brush, but never through itself") {
    const ct::Brush box = ct::makeBoxBrush({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f});
    // Plane 4 is the top (+Z).
    const auto taller = ct::withPlaneDistance(box, 4, 3.0f);
    REQUIRE(taller.has_value());
    CHECK(ct::boxBounds(*taller)->max.z == 3.0f);
    CHECK_FALSE(ct::withPlaneDistance(box, 4, -1.0f).has_value());
    CHECK_FALSE(ct::withPlaneDistance(box, 4, 0.0f).has_value());
    CHECK_FALSE(ct::withPlaneDistance(box, 9, 1.0f).has_value());
}

TEST_CASE("rays hit brushes on the face they enter through") {
    const ct::Brush box = ct::makeBoxBrush({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f});
    const auto hit = ct::rayCastBrush(box, {-5.0f, 0.5f, 0.5f}, {1.0f, 0.0f, 0.0f});
    REQUIRE(hit.has_value());
    CHECK(hit->distance == doctest::Approx(5.0f));
    CHECK(box.planes[hit->plane].normal == ct::Vec3{-1.0f, 0.0f, 0.0f});

    const auto fromAbove = ct::rayCastBrush(box, {0.5f, 0.5f, 4.0f}, {0.0f, 0.0f, -1.0f});
    REQUIRE(fromAbove.has_value());
    CHECK(box.planes[fromAbove->plane].normal == ct::Vec3{0.0f, 0.0f, 1.0f});

    CHECK_FALSE(ct::rayCastBrush(box, {-5.0f, 2.0f, 0.5f}, {1.0f, 0.0f, 0.0f}));  // passes beside
    CHECK_FALSE(ct::rayCastBrush(box, {-5.0f, 0.5f, 0.5f}, {-1.0f, 0.0f, 0.0f})); // points away
    CHECK_FALSE(ct::rayCastBrush(box, {0.5f, 0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}));   // from inside

    const auto ramp = ct::makeRampBrush({0.0f, 0.0f, 0.0f}, {2.0f, 1.0f, 1.0f});
    const auto slope = ct::rayCastBrush(ramp, {1.0f, 0.5f, 5.0f}, {0.0f, 0.0f, -1.0f});
    REQUIRE(slope.has_value());
    CHECK(slope->distance == doctest::Approx(4.5f));
}

TEST_CASE("rays hit planes in front of them only") {
    const ct::Plane ground{{0.0f, 0.0f, 1.0f}, 0.0f};
    const ct::Vec3 down = ct::normalize(ct::Vec3{1.0f, 0.0f, -1.0f});
    const auto t = ct::rayCastPlane(ground, {0.0f, 0.0f, 2.0f}, down);
    REQUIRE(t.has_value());
    CHECK(*t == doctest::Approx(2.0f * std::sqrt(2.0f)));
    CHECK_FALSE(ct::rayCastPlane(ground, {0.0f, 0.0f, 2.0f}, {0.0f, 0.0f, 1.0f}));
    CHECK_FALSE(ct::rayCastPlane(ground, {0.0f, 0.0f, 2.0f}, {1.0f, 0.0f, 0.0f}));
}

TEST_CASE("the closest point along an axis follows a ray that crosses it") {
    // A vertical axis through the origin; a horizontal ray passing it at height 3.
    const auto s = ct::closestAlongAxis({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {-5.0f, 1.0f, 3.0f},
                                        {1.0f, 0.0f, 0.0f});
    REQUIRE(s.has_value());
    CHECK(*s == doctest::Approx(3.0f));
    CHECK_FALSE(ct::closestAlongAxis({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f},
                                     {0.0f, 0.0f, -1.0f}));
}
