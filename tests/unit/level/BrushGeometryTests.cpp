#include "croftech/level/BrushGeometry.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace {

// Winding is counter-clockwise from outside when the polygon's normal agrees with the plane's.
bool windsCounterClockwise(const ct::BrushPolygon& face) {
    ct::Vec3 normal;
    for (std::size_t i = 1; i + 1 < face.vertices.size(); ++i) {
        normal +=
            ct::cross(face.vertices[i] - face.vertices[0], face.vertices[i + 1] - face.vertices[0]);
    }
    return ct::dot(normal, face.plane.normal) > 0.0f;
}

bool valid(const ct::Brush& brush) {
    std::string problem;
    const bool ok = ct::checkBrush(brush, problem);
    CAPTURE(problem);
    CHECK(ok);
    return ok;
}

// Whether the brush has a corner at exactly this point.
bool hasCorner(const ct::Brush& brush, ct::Vec3 corner) {
    return std::find(brush.vertices.begin(), brush.vertices.end(), corner) != brush.vertices.end();
}

std::vector<ct::DPlane> exactPlanes(const ct::Brush& brush) {
    std::vector<ct::DPlane> planes;
    for (std::size_t i = 0; i < brush.faces.size(); ++i) {
        planes.push_back(ct::exactFacePlane(brush, i));
    }
    return planes;
}

} // namespace

TEST_CASE("a box brush has six quads wound counter-clockwise on exact planes") {
    const ct::Brush box = ct::makeBoxBrush({0.0f, 0.0f, 0.0f}, {2.0f, 3.0f, 4.0f});
    REQUIRE(valid(box));
    const auto faces = ct::brushPolygons(box);
    REQUIRE(faces.size() == 6);
    for (const auto& face : faces) {
        CHECK(face.vertices.size() == 4);
        CHECK(windsCounterClockwise(face));
    }
    CHECK(faces[0].plane == ct::Plane{{1.0f, 0.0f, 0.0f}, 2.0f});
    CHECK(faces[5].plane == ct::Plane{{0.0f, 0.0f, -1.0f}, 0.0f});
}

TEST_CASE("a ramp brush has two triangles and three quads") {
    const ct::Brush ramp = ct::makeRampBrush({0.0f, 0.0f, 0.0f}, {4.0f, 2.0f, 1.0f});
    REQUIRE(valid(ramp));
    const auto faces = ct::brushPolygons(ramp);
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
    CHECK(bounds.min == ct::Vec3{-1.0f, -2.0f, 0.0f});
    CHECK(bounds.max == ct::Vec3{3.0f, 2.0f, 1.5f});
    CHECK_FALSE(ct::computeBrushBounds(ct::Brush{}, bounds));
}

TEST_CASE("brushes that are not closed convex solids are refused") {
    const auto problem = [](const ct::Brush& brush) {
        std::string text;
        CHECK_FALSE(ct::checkBrush(brush, text));
        return text;
    };
    const ct::Brush box = ct::makeBoxBrush({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f});

    ct::Brush open = box;
    open.faces.pop_back();
    CHECK(problem(open) == "brush is not a closed solid");

    ct::Brush inside = box;
    std::reverse(inside.faces[2].corners.begin(), inside.faces[2].corners.end());
    CHECK(problem(inside) == "brush is not a closed solid");

    ct::Brush bent = box;
    bent.vertices[7].z = 1.5f; // the top is no longer flat
    CHECK(problem(bent) == "a face is not flat");

    // Two triangular pyramids base to base; pushing the lower tip up through the base keeps
    // every face flat and the surface closed, but dents it.
    ct::Brush bipyramid;
    bipyramid.vertices = {{0.0f, 0.0f, 0.0f},
                          {2.0f, 0.0f, 0.0f},
                          {0.0f, 2.0f, 0.0f},
                          {0.5f, 0.5f, 1.0f},
                          {0.5f, 0.5f, -1.0f}};
    bipyramid.faces = {{{0, 1, 3}}, {{1, 2, 3}}, {{2, 0, 3}},
                       {{1, 0, 4}}, {{2, 1, 4}}, {{0, 2, 4}}};
    CHECK(valid(bipyramid));
    bipyramid.vertices[4].z = 0.5f;
    CHECK(problem(bipyramid) == "brush is not convex");

    ct::Brush missing = box;
    missing.faces[0].corners[0] = 8;
    CHECK(problem(missing) == "face uses corner 8, which does not exist");
    missing.faces[0].corners = {1, 3, 3, 5};
    CHECK(problem(missing) == "a face uses a corner twice");

    CHECK(problem(ct::makeBoxBrush({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 3000.0f}))
              .starts_with("brush reaches outside the world"));
    CHECK(problem(ct::Brush{}).starts_with("a brush needs 4 to"));
}

TEST_CASE("planes become corners, in plane order, ignoring planes that touch nothing") {
    std::vector<ct::DPlane> planes =
        exactPlanes(ct::makeBoxBrush({0.0f, 0.0f, 0.0f}, {1.0f, 2.0f, 3.0f}));
    planes.insert(planes.begin() + 2, ct::DPlane{{1.0, 0.0, 0.0}, 5.0}); // well outside
    std::vector<std::size_t> planeOfFace;
    const auto brush = ct::brushFromPlanes(planes, {}, &planeOfFace);
    REQUIRE(brush.has_value());
    CHECK(brush->vertices.size() == 8);
    CHECK(planeOfFace == std::vector<std::size_t>{0, 1, 3, 4, 5, 6});
    CHECK(hasCorner(*brush, {1.0f, 2.0f, 3.0f}));
    CHECK(hasCorner(*brush, {0.0f, 0.0f, 0.0f}));

    // Open on one side, or empty.
    planes.pop_back();
    CHECK_FALSE(ct::brushFromPlanes(planes));
    planes.push_back({{0.0, 0.0, -1.0}, -5.0}); // below z = 5: nothing between that and z = 3
    CHECK_FALSE(ct::brushFromPlanes(planes));
}

TEST_CASE("corners on the grid survive the trip through planes, even far from the origin") {
    // A sloped wedge whose corners are all on a 1/32 grid, 2 km out: float planes would put its
    // corners millimetres off; exact planes put them back bit for bit.
    const ct::Vec3 o{2000.0f, -1999.5f, 1000.03125f};
    const std::vector<ct::Vec3> corners{o,
                                        o + ct::Vec3{3.0f, 0.0f, 0.0f},
                                        o + ct::Vec3{0.0f, 2.03125f, 0.0f},
                                        o + ct::Vec3{3.0f, 2.03125f, 0.0f},
                                        o + ct::Vec3{3.0f, 0.0f, 1.96875f},
                                        o + ct::Vec3{3.0f, 2.03125f, 1.96875f},
                                        o + ct::Vec3{0.0f, 0.0f, 0.5f}};
    const auto wedge = ct::brushFromPoints(corners);
    REQUIRE(wedge.has_value());
    const auto rebuilt = ct::brushFromPlanes(exactPlanes(*wedge));
    REQUIRE(rebuilt.has_value());
    CHECK(rebuilt->vertices.size() == wedge->vertices.size());
    for (const ct::Vec3& corner : wedge->vertices) {
        CHECK(hasCorner(*rebuilt, corner));
    }
}

TEST_CASE("clipping keeps the part behind the plane and closes it") {
    const ct::Brush box = ct::makeBoxBrush({0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 2.0f});
    // A diagonal cut through two opposite top and bottom edges leaves a wedge.
    const ct::DPlane diagonal =
        ct::planeThroughPoints({0.0, 0.0, 2.0}, {2.0, 0.0, 0.0}, {0.0, 2.0, 2.0});
    const auto wedge = ct::clipBrush(box, diagonal);
    REQUIRE(wedge.has_value());
    CHECK(valid(*wedge));
    CHECK(wedge->vertices.size() == 6);
    CHECK(wedge->faces.size() == 5);
    CHECK(ct::facePlane(*wedge, wedge->faces.size() - 1).normal.x > 0.0f); // the cut is last
    CHECK(hasCorner(*wedge, {0.0f, 0.0f, 2.0f}));
    CHECK_FALSE(hasCorner(*wedge, {2.0f, 2.0f, 2.0f}));

    const auto other = ct::clipBrush(box, diagonal.flipped());
    REQUIRE(other.has_value());
    CHECK(other->vertices.size() == 6);

    // A cut through the middle lands on exact coordinates.
    const auto half = ct::clipBrush(box, {{0.0, 0.0, 3.0}, 1.5}); // z <= 0.5
    REQUIRE(half.has_value());
    CHECK(hasCorner(*half, {2.0f, 2.0f, 0.5f}));

    CHECK(ct::clipBrush(box, {{0.0, 0.0, 1.0}, 5.0}) == box); // misses
    CHECK(ct::clipBrush(box, {{0.0, 0.0, 1.0}, 2.0}) == box); // touches the top
    CHECK_FALSE(ct::clipBrush(box, {{0.0, 0.0, 1.0}, 0.0}));  // all in front
}

TEST_CASE("the hull of points keeps only real corners and merges flat faces") {
    std::vector<ct::Vec3> points;
    for (int i = 0; i < 8; ++i) {
        points.push_back({(i & 1) != 0 ? 1.0f : -1.0f, (i & 2) != 0 ? 1.0f : -1.0f,
                          (i & 4) != 0 ? 1.0f : -1.0f});
    }
    points.push_back({0.0f, 0.0f, 0.0f});   // inside
    points.push_back({0.0f, 0.0f, 1.0f});   // middle of the top
    points.push_back({1.0f, 1.0f, 0.0f});   // middle of an edge
    points.push_back({1.0f, 1.0f, 1.0f});   // a repeat
    points.push_back({0.5f, -1.0f, 0.25f}); // on a side
    const auto box = ct::brushFromPoints(points);
    REQUIRE(box.has_value());
    CHECK(box->vertices.size() == 8);
    CHECK(box->faces.size() == 6);
    for (std::size_t i = 0; i < box->faces.size(); ++i) {
        CHECK(box->faces[i].corners.size() == 4);
    }
}

TEST_CASE("points that are flat or too few make no brush") {
    CHECK_FALSE(ct::brushFromPoints(std::vector<ct::Vec3>{
        {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}}));
    CHECK_FALSE(ct::brushFromPoints(
        std::vector<ct::Vec3>{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}}));
    const auto tetrahedron = ct::brushFromPoints(std::vector<ct::Vec3>{
        {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}});
    REQUIRE(tetrahedron.has_value());
    CHECK(tetrahedron->faces.size() == 4);
}
