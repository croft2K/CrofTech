#include "croftech/editor/BrushEditing.h"
#include "croftech/editor/EditorActions.h"
#include "croftech/editor/Shapes.h"
#include "croftech/level/BrushGeometry.h"

#include <doctest/doctest.h>

#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace {

const ct::Aabb kBox{{0.0f, 0.0f, 0.0f}, {2.0f, 4.0f, 3.0f}};

ct::Aabb boundsOf(const std::vector<ct::Brush>& brushes) {
    REQUIRE_FALSE(brushes.empty());
    ct::Aabb total;
    REQUIRE(ct::computeBrushBounds(brushes.front(), total));
    for (const ct::Brush& brush : brushes) {
        ct::Aabb bounds;
        REQUIRE(ct::computeBrushBounds(brush, bounds));
        total = {ct::min(total.min, bounds.min), ct::max(total.max, bounds.max)};
    }
    return total;
}

// Whether something solid is under (x, y), looking down from above the box.
bool solidBelow(const std::vector<ct::Brush>& brushes, float x, float y) {
    for (const ct::Brush& brush : brushes) {
        if (ct::rayCastBrush(brush, {x, y, 10.0f}, {0.0f, 0.0f, -1.0f})) {
            return true;
        }
    }
    return false;
}

std::optional<float> topAt(const std::vector<ct::Brush>& brushes, float x, float y) {
    std::optional<float> top;
    for (const ct::Brush& brush : brushes) {
        if (const auto hit = ct::rayCastBrush(brush, {x, y, 10.0f}, {0.0f, 0.0f, -1.0f})) {
            const float z = 10.0f - hit->distance;
            top = top ? std::max(*top, z) : z;
        }
    }
    return top;
}

} // namespace

TEST_CASE("every shape fills its box with valid brushes") {
    for (const ct::ShapeKind kind : ct::kShapeKinds) {
        CAPTURE(ct::shapeName(kind));
        ct::ShapeSettings settings;
        settings.thickness = 0.5f;
        const std::vector<ct::Brush> brushes = ct::makeShape(kind, kBox, settings);
        REQUIRE_FALSE(brushes.empty());
        const ct::Aabb bounds = boundsOf(brushes);
        CHECK(bounds.min.x == doctest::Approx(kBox.min.x).epsilon(1e-4));
        CHECK(bounds.min.y == doctest::Approx(kBox.min.y).epsilon(1e-4));
        CHECK(bounds.min.z == doctest::Approx(kBox.min.z).epsilon(1e-4));
        CHECK(bounds.max.x == doctest::Approx(kBox.max.x).epsilon(1e-4));
        CHECK(bounds.max.y == doctest::Approx(kBox.max.y).epsilon(1e-4));
        CHECK(bounds.max.z == doctest::Approx(kBox.max.z).epsilon(1e-4));
        for (const ct::Brush& brush : brushes) {
            CHECK(brush.faces.size() <= ct::kMaxBrushFaces);
        }
    }
}

TEST_CASE("an 8-sided cylinder has its flat sides exactly on the box") {
    ct::ShapeSettings settings;
    settings.sides = 8;
    const auto brushes = ct::makeShape(ct::ShapeKind::Cylinder, kBox, settings);
    REQUIRE(brushes.size() == 1);
    int onBox = 0;
    const std::vector<ct::Plane> planes = ct::brushPlanes(brushes[0]);
    for (const ct::Plane& plane : planes) {
        if ((plane.normal == ct::Vec3{1.0f, 0.0f, 0.0f} && plane.distance == kBox.max.x) ||
            (plane.normal == ct::Vec3{-1.0f, 0.0f, 0.0f} && plane.distance == -kBox.min.x) ||
            (plane.normal == ct::Vec3{0.0f, 1.0f, 0.0f} && plane.distance == kBox.max.y) ||
            (plane.normal == ct::Vec3{0.0f, -1.0f, 0.0f} && plane.distance == -kBox.min.y)) {
            ++onBox;
        }
    }
    CHECK(onBox == 4);
    CHECK(planes.size() == 10);
    // No -0 in what the inspector shows and the level file stores.
    for (const ct::Plane& plane : planes) {
        for (const float value : {plane.normal.x, plane.normal.y, plane.normal.z, plane.distance}) {
            CHECK_FALSE((value == 0.0f && std::signbit(value)));
        }
    }
    for (const ct::Vec3& corner : brushes[0].vertices) {
        for (const float value : {corner.x, corner.y, corner.z}) {
            CHECK_FALSE((value == 0.0f && std::signbit(value)));
        }
    }
}

TEST_CASE("wedges and stairs rise towards their facing") {
    for (const ct::Vec3 forward : {ct::Vec3{1.0f, 0.0f, 0.0f}, ct::Vec3{0.0f, -1.0f, 0.0f}}) {
        ct::ShapeSettings settings;
        settings.forward = forward;
        settings.steps = 3;
        // Near the low end and near the high end, along the facing.
        const ct::Vec3 centre = (kBox.min + kBox.max) * 0.5f;
        const ct::Vec3 half = (kBox.max - kBox.min) * 0.45f;
        const ct::Vec3 low = centre - ct::Vec3{forward.x * half.x, forward.y * half.y, 0.0f};
        const ct::Vec3 high = centre + ct::Vec3{forward.x * half.x, forward.y * half.y, 0.0f};

        const auto wedge = ct::makeShape(ct::ShapeKind::Wedge, kBox, settings);
        REQUIRE(wedge.size() == 1);
        CHECK(*topAt(wedge, low.x, low.y) < 0.5f);
        CHECK(*topAt(wedge, high.x, high.y) > 2.5f);

        const auto stairs = ct::makeShape(ct::ShapeKind::Stairs, kBox, settings);
        REQUIRE(stairs.size() == 3);
        CHECK(*topAt(stairs, low.x, low.y) == doctest::Approx(1.0f));
        CHECK(*topAt(stairs, centre.x, centre.y) == doctest::Approx(2.0f));
        CHECK(*topAt(stairs, high.x, high.y) == doctest::Approx(3.0f));
        for (const ct::Brush& step : stairs) {
            CHECK(ct::boxBounds(step).has_value());
        }
    }
}

TEST_CASE("an arch leaves a passage under it along its facing") {
    ct::ShapeSettings settings;
    settings.sides = 6;
    settings.thickness = 0.5f;
    settings.forward = {0.0f, 1.0f, 0.0f}; // walked through along Y, so it spans X
    const auto arch = ct::makeShape(ct::ShapeKind::Arch, kBox, settings);
    REQUIRE(arch.size() == 6);
    // Open underneath in the middle, solid at the crown and on both feet.
    for (const ct::Brush& brush : arch) {
        CHECK_FALSE(ct::rayCastBrush(brush, {1.0f, -5.0f, 0.5f}, {0.0f, 1.0f, 0.0f}));
    }
    CHECK(*topAt(arch, 1.0f, 2.0f) == doctest::Approx(3.0f).epsilon(1e-3));
    CHECK(solidBelow(arch, 0.1f, 2.0f));
    CHECK(solidBelow(arch, 1.9f, 2.0f));

    settings.thickness = 1.5f; // thicker than half the span: no opening left
    CHECK(ct::makeShape(ct::ShapeKind::Arch, kBox, settings).empty());
}

TEST_CASE("settings are kept within the limits that keep brushes valid") {
    ct::ShapeSettings settings;
    settings.sides = 1000;
    settings.steps = -3;
    settings.forward = {-0.2f, -0.9f, 0.0f};
    const ct::ShapeSettings cylinder = ct::clampedSettings(ct::ShapeKind::Cylinder, settings);
    CHECK(cylinder.sides == ct::kMaxShapeSides);
    CHECK(cylinder.steps == 1);
    CHECK(cylinder.forward == ct::Vec3{0.0f, -1.0f, 0.0f});
    CHECK(ct::clampedSettings(ct::ShapeKind::Sphere, settings).sides == ct::kMaxSphereSides);

    for (const ct::ShapeKind kind : ct::kShapeKinds) {
        CAPTURE(ct::shapeName(kind));
        CHECK_FALSE(ct::makeShape(kind, kBox, settings).empty());
        // A flat box makes nothing.
        CHECK(ct::makeShape(kind, {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}}, settings).empty());
    }
}

TEST_CASE("the most detailed shapes stay within the brush limits") {
    ct::ShapeSettings settings;
    settings.sides = ct::kMaxShapeSides;
    for (const ct::ShapeKind kind : {ct::ShapeKind::Cylinder, ct::ShapeKind::Cone}) {
        const auto brushes = ct::makeShape(kind, kBox, settings);
        REQUIRE(brushes.size() == 1);
        CHECK(brushes[0].faces.size() >= static_cast<std::size_t>(ct::kMaxShapeSides) + 1);
    }
    settings.sides = ct::kMaxSphereSides;
    const auto sphere = ct::makeShape(ct::ShapeKind::Sphere, kBox, settings);
    REQUIRE(sphere.size() == 1);
    const auto sides = static_cast<std::size_t>(ct::kMaxSphereSides);
    CHECK(sphere[0].faces.size() == sides * sides / 2);
    CHECK(sphere[0].faces.size() <= ct::kMaxBrushFaces);
}

TEST_CASE("a shape is added as one command and selected") {
    ct::Level level;
    level.brushes = {ct::makeBoxBrush({0.0f, 0.0f, -1.0f}, {8.0f, 8.0f, 0.0f})};
    ct::EditorDocument document(level, "test.level");
    ct::ShapeSettings settings;
    settings.steps = 5;
    REQUIRE(ct::addBrushes(document, ct::makeShape(ct::ShapeKind::Stairs, kBox, settings),
                           "Create stairs"));
    CHECK(document.level().brushes.size() == 6);
    CHECK(document.selection().size() == 5);
    CHECK(document.isSelected(ct::LevelItem::brush(5)));
    CHECK(document.history().undoName() == "Create stairs");
    document.undo();
    CHECK(document.level() == level);
    CHECK_FALSE(ct::addBrushes(document, {}, "Create nothing"));
}
