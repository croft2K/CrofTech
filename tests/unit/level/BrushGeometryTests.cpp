#include "croftech/level/BrushGeometry.h"

#include <doctest/doctest.h>

#include <cstddef>

namespace {

ct::Vec3 faceCentre(const ct::BrushFace& face) {
    ct::Vec3 sum;
    for (const ct::Vec3& v : face.vertices) {
        sum += v;
    }
    return sum / static_cast<float>(face.vertices.size());
}

// Winding is counter-clockwise from outside when the polygon's normal agrees with the plane's.
bool windsCounterClockwise(const ct::BrushFace& face) {
    ct::Vec3 normal;
    for (std::size_t i = 0; i < face.vertices.size(); ++i) {
        const ct::Vec3& a = face.vertices[i];
        const ct::Vec3& b = face.vertices[(i + 1) % face.vertices.size()];
        normal += ct::cross(a - faceCentre(face), b - faceCentre(face));
    }
    return ct::dot(normal, face.plane.normal) > 0.0f;
}

} // namespace

TEST_CASE("a box brush has six quads wound counter-clockwise") {
    const ct::Brush box = ct::makeBoxBrush({0.0f, 0.0f, 0.0f}, {2.0f, 3.0f, 4.0f});
    const auto faces = ct::buildBrushFaces(box);
    REQUIRE(faces.size() == 6);
    for (const auto& face : faces) {
        CHECK(face.vertices.size() == 4);
        CHECK(windsCounterClockwise(face));
    }
}

TEST_CASE("a ramp brush has two triangles and three quads") {
    const ct::Brush ramp = ct::makeRampBrush({0.0f, 0.0f, 0.0f}, {4.0f, 2.0f, 1.0f});
    const auto faces = ct::buildBrushFaces(ramp);
    REQUIRE(faces.size() == 5);
    std::size_t triangles = 0;
    for (const auto& face : faces) {
        triangles += face.vertices.size() == 3 ? 1u : 0u;
        CHECK(windsCounterClockwise(face));
    }
    CHECK(triangles == 2);
}

TEST_CASE("brush bounds enclose the solid") {
    ct::Aabb bounds;
    REQUIRE(ct::computeBrushBounds(ct::makeRampBrush({-1.0f, -2.0f, 0.0f}, {3.0f, 2.0f, 1.5f}),
                                   bounds));
    CHECK(bounds.min.x == doctest::Approx(-1.0f));
    CHECK(bounds.max.x == doctest::Approx(3.0f));
    CHECK(bounds.min.y == doctest::Approx(-2.0f));
    CHECK(bounds.max.z == doctest::Approx(1.5f));
}

TEST_CASE("planes that enclose nothing produce no faces") {
    ct::Brush empty;
    empty.planes = {{{0.0f, 0.0f, 1.0f}, 0.0f}, {{0.0f, 0.0f, -1.0f}, -1.0f}};
    CHECK(ct::buildBrushFaces(empty).empty());
    ct::Aabb bounds;
    CHECK_FALSE(ct::computeBrushBounds(empty, bounds));
}
