#include "croftech/editor/AffineTransform.h"
#include "croftech/editor/BrushEditing.h"
#include "croftech/level/BrushGeometry.h"

#include <doctest/doctest.h>

#include <cmath>
#include <optional>

namespace {

ct::Aabb boundsOf(const ct::Brush& brush) {
    ct::Aabb bounds;
    REQUIRE(ct::computeBrushBounds(brush, bounds));
    return bounds;
}

} // namespace

TEST_CASE("quarter turns are exact, so four of them come back to the same brush") {
    const ct::Brush box = ct::makeBoxBrush({0.0f, 0.0f, 0.0f}, {1.0f, 2.0f, 3.0f});
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const ct::AffineTransform turn =
            ct::AffineTransform::rotation(axis, 90.0, {0.5f, 0.25f, 1.0f});
        ct::Brush turned = box;
        for (int i = 0; i < 4; ++i) {
            const auto next = ct::transformedBrush(turned, turn);
            REQUIRE(next.has_value());
            turned = *next;
        }
        CHECK(turned == box);
    }
}

TEST_CASE("a quarter turn about Z swaps a box's width and depth") {
    const ct::Brush box = ct::makeBoxBrush({0.0f, 0.0f, 0.0f}, {1.0f, 2.0f, 3.0f});
    const auto turned = ct::transformedBrush(box, ct::AffineTransform::rotation(2, 90.0, {}));
    REQUIRE(turned.has_value());
    const auto bounds = ct::boxBounds(*turned);
    REQUIRE(bounds.has_value());
    CHECK(bounds->min == ct::Vec3{-2.0f, 0.0f, 0.0f});
    CHECK(bounds->max == ct::Vec3{0.0f, 1.0f, 3.0f});

    // Right-handed: +X turns to +Y.
    CHECK(ct::AffineTransform::rotation(2, 90.0, {}).apply({1.0f, 0.0f, 0.0f}) ==
          ct::Vec3{0.0f, 1.0f, 0.0f});
    CHECK(ct::AffineTransform::rotation(0, 90.0, {}).apply({0.0f, 1.0f, 0.0f}) ==
          ct::Vec3{0.0f, 0.0f, 1.0f});
    CHECK(ct::AffineTransform::rotation(1, 90.0, {}).apply({0.0f, 0.0f, 1.0f}) ==
          ct::Vec3{1.0f, 0.0f, 0.0f});
}

TEST_CASE("a turn by any angle keeps a brush valid, with its corners turned") {
    const ct::Brush box = ct::makeBoxBrush({-1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f});
    const auto turned = ct::transformedBrush(box, ct::AffineTransform::rotation(2, 45.0, {}));
    REQUIRE(turned.has_value());
    const ct::Aabb bounds = boundsOf(*turned);
    CHECK(bounds.max.x == doctest::Approx(std::sqrt(2.0f)).epsilon(1e-5));
    CHECK(bounds.min.y == doctest::Approx(-std::sqrt(2.0f)).epsilon(1e-5));
    CHECK(bounds.max.z == doctest::Approx(1.0f));
}

TEST_CASE("mirroring keeps the solid inside its planes") {
    // Rises from x = 0 to x = 2; mirrored about x = 1 it rises the other way.
    const ct::Brush ramp = ct::makeRampBrush({0.0f, 0.0f, 0.0f}, {2.0f, 1.0f, 1.0f});
    const auto mirrored = ct::transformedBrush(
        ramp, ct::AffineTransform::scale({-1.0, 1.0, 1.0}, {1.0f, 0.0f, 0.0f}));
    REQUIRE(mirrored.has_value());
    const auto highSide = ct::rayCastBrush(*mirrored, {0.25f, 0.5f, 5.0f}, {0.0f, 0.0f, -1.0f});
    const auto lowSide = ct::rayCastBrush(*mirrored, {1.75f, 0.5f, 5.0f}, {0.0f, 0.0f, -1.0f});
    REQUIRE(highSide.has_value());
    REQUIRE(lowSide.has_value());
    CHECK(highSide->distance == doctest::Approx(4.125f));
    CHECK(lowSide->distance == doctest::Approx(4.875f));
}

TEST_CASE("scaling one box onto another lands exactly on the new sides") {
    const ct::Aabb from{{0.25f, 0.0f, 0.0f}, {1.75f, 3.0f, 1.0f}};
    const ct::Aabb to{{0.25f, -1.0f, 0.0f}, {2.5f, 3.0f, 1.0f}};
    const auto scaled = ct::transformedBrush(ct::makeBoxBrush(from.min, from.max),
                                             ct::AffineTransform::boxToBox(from, to));
    REQUIRE(scaled.has_value());
    const auto bounds = ct::boxBounds(*scaled);
    REQUIRE(bounds.has_value());
    CHECK(bounds->min == to.min);
    CHECK(bounds->max == to.max);
}

TEST_CASE("a transform that flattens space makes no brush") {
    const ct::Brush box = ct::makeBoxBrush({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f});
    CHECK_FALSE(ct::transformedBrush(box, ct::AffineTransform::scale({1.0, 0.0, 1.0}, {})));
    // Moved out of the world.
    CHECK_FALSE(ct::transformedBrush(box, ct::AffineTransform::moveBy({3000.0f, 0.0f, 0.0f})));
}

TEST_CASE("transforms chain in order") {
    const ct::AffineTransform moveThenTurn = ct::AffineTransform::moveBy({1.0f, 0.0f, 0.0f})
                                                 .then(ct::AffineTransform::rotation(2, 90.0, {}));
    CHECK(moveThenTurn.apply({0.0f, 0.0f, 0.0f}) == ct::Vec3{0.0f, 1.0f, 0.0f});
    const ct::AffineTransform turnThenMove =
        ct::AffineTransform::rotation(2, 90.0, {})
            .then(ct::AffineTransform::moveBy({1.0f, 0.0f, 0.0f}));
    CHECK(turnThenMove.apply({0.0f, 0.0f, 0.0f}) == ct::Vec3{1.0f, 0.0f, 0.0f});
}

TEST_CASE("entities move with the transform and turn their yaw") {
    const ct::LevelEntity spawn = ct::makePlayerSpawn({2.0f, 0.0f, 1.0f}, 30.0f);
    const ct::LevelEntity turned =
        ct::transformedEntity(spawn, ct::AffineTransform::rotation(2, 90.0, {}));
    CHECK(ct::entityOrigin(turned) == ct::Vec3{0.0f, 2.0f, 1.0f});
    CHECK(*ct::findProperty(turned, "yaw") == "120");

    const ct::LevelEntity mirrored =
        ct::transformedEntity(spawn, ct::AffineTransform::scale({1.0, -1.0, 1.0}, {}));
    CHECK(*ct::findProperty(mirrored, "yaw") == "-30");

    // An enemy placed without a yaw faces its class's default, 0, and gets one when turned, but
    // not when only moved.
    const ct::LevelEntity enemy{"enemy", {{"origin", "2 0 0"}}, {}};
    const ct::LevelEntity enemyTurned =
        ct::transformedEntity(enemy, ct::AffineTransform::rotation(2, 90.0, {}));
    REQUIRE(ct::findProperty(enemyTurned, "yaw") != nullptr);
    CHECK(*ct::findProperty(enemyTurned, "yaw") == "90");
    const ct::LevelEntity enemyMoved =
        ct::transformedEntity(enemy, ct::AffineTransform::moveBy({1.0f, 0.0f, 0.0f}));
    CHECK(ct::findProperty(enemyMoved, "yaw") == nullptr);

    const ct::LevelEntity noOrigin{"worldspawn", {{"yaw", "10"}}};
    CHECK(ct::transformedEntity(noOrigin, ct::AffineTransform::moveBy({1.0f, 0.0f, 0.0f})) ==
          noOrigin);
}

TEST_CASE("angles snap to the nearest step") {
    CHECK(ct::snapAngle(22.0, 15.0) == 15.0);
    CHECK(ct::snapAngle(23.0, 15.0) == 30.0);
    CHECK(ct::snapAngle(-8.0, 15.0) == -15.0);
}
