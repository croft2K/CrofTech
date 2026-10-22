#include "croftech/level/EntityClasses.h"
#include "croftech/nav/NavQuery.h"
#include "croftech/sim/GameLevel.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

// A 20 x 20 m floor whose top is at z = 0.
ct::Brush smallFloor() { return ct::makeBoxBrush({-10.0f, -10.0f, -1.0f}, {10.0f, 10.0f, 0.0f}); }

ct::Brush owned(ct::Vec3 min, ct::Vec3 max, std::uint32_t entity) {
    ct::Brush brush = ct::makeBoxBrush(min, max);
    brush.entity = entity;
    return brush;
}

ct::LevelEntity entity(std::string className, std::vector<ct::EntityProperty> properties) {
    return {std::move(className), std::move(properties), {}};
}

ct::NavMesh buildMesh(const ct::Level& level) {
    return ct::NavMesh(ct::navInput(level), ct::navAgent({}));
}

float pathLength(const ct::NavPath& path) {
    float total = 0.0f;
    for (std::uint32_t i = 1; i < path.count; ++i) {
        total += ct::length(path.corners[i].position - path.corners[i - 1].position);
    }
    return total;
}

bool passes(const ct::NavPath& path, ct::NavArea area, std::uint32_t mover) {
    const std::span<const ct::NavCorner> corners = path.view();
    return std::any_of(corners.begin(), corners.end(), [&](const ct::NavCorner& corner) {
        return corner.area == area && corner.mover == mover;
    });
}

constexpr ct::Vec3 kSouth{0.0f, -5.0f, 0.0f};
constexpr ct::Vec3 kNorth{0.0f, 5.0f, 0.0f};

// A wall across the floor at y = 0..0.5 with a 2 m doorway in the middle, closed by a door (level
// entity 0) that slides up.
ct::Level doorLevel(std::vector<ct::EntityProperty> doorProperties) {
    doorProperties.push_back({"move", "0 0 3"});
    return {{smallFloor(), ct::makeBoxBrush({-10.0f, 0.0f, 0.0f}, {-1.0f, 0.5f, 4.0f}),
             ct::makeBoxBrush({1.0f, 0.0f, 0.0f}, {10.0f, 0.5f, 4.0f}),
             ct::makeBoxBrush({-1.0f, 0.0f, 3.0f}, {1.0f, 0.5f, 4.0f}),
             owned({-1.0f, 0.125f, 0.0f}, {1.0f, 0.375f, 3.0f}, 0)},
            {entity("mover", std::move(doorProperties))}};
}

// The floor south of y = 0, ground 4 m higher to the north of y = 2, and a 2 x 2 m lift (level
// entity 0) in a shaft between them: its top is level with the lower floor, and with the upper
// ground when it has risen.
ct::Level liftLevel() {
    return {{ct::makeBoxBrush({-10.0f, -10.0f, -1.0f}, {10.0f, 0.0f, 0.0f}),
             ct::makeBoxBrush({-10.0f, 2.0f, -1.0f}, {10.0f, 10.0f, 4.0f}),
             ct::makeBoxBrush({-10.0f, 0.0f, -1.0f}, {-1.0f, 2.0f, 4.0f}),
             ct::makeBoxBrush({1.0f, 0.0f, -1.0f}, {10.0f, 2.0f, 4.0f}),
             owned({-1.0f, 0.0f, -0.25f}, {1.0f, 2.0f, 0.0f}, 0)},
            {entity("mover", {{"move", "0 0 4"}, {"activate", "stand"}})}};
}

} // namespace

TEST_CASE("an open floor is one walkable mesh, crossed in a straight line") {
    const ct::NavMesh mesh = buildMesh({{smallFloor()}, {}});
    CHECK(mesh.warnings().empty());
    CHECK(mesh.polygonCount() > 0);
    const ct::NavQuery query(mesh);
    ct::NavPath path;
    REQUIRE(query.findPath({-5.0f, -5.0f, 0.0f}, {5.0f, 5.0f, 0.0f}, {}, path));
    REQUIRE(path.count == 2);
    CHECK(ct::length(path.corners[0].position - ct::Vec3{-5.0f, -5.0f, 0.0f}) < 0.1f);
    CHECK(ct::length(path.corners[1].position - ct::Vec3{5.0f, 5.0f, 0.0f}) < 0.1f);
    CHECK(path.corners[0].area == ct::NavArea::Ground);
    CHECK(path.corners[0].mover == ct::kNoNavMover);

    // Points in the air are found on the floor below; the mesh stops short of the edge, where
    // the agent's box would hang over the drop.
    const std::optional<ct::Vec3> above = query.nearestPoint({2.0f, 3.0f, 1.0f});
    REQUIRE(above);
    CHECK(std::fabs(above->z) < 0.1f);
    const std::optional<ct::Vec3> edge = query.nearestPoint({9.9f, 0.0f, 0.0f});
    REQUIRE(edge);
    CHECK(edge->x < 9.7f);
    CHECK(edge->x > 9.4f);
    CHECK_FALSE(query.nearestPoint({30.0f, 0.0f, 0.0f}));
}

TEST_CASE("paths go round walls, through the gap") {
    const ct::NavMesh mesh = buildMesh(
        {{smallFloor(), ct::makeBoxBrush({-10.0f, -0.5f, 0.0f}, {6.0f, 0.5f, 3.0f})}, {}});
    const ct::NavQuery query(mesh);
    ct::NavPath path;
    REQUIRE(query.findPath(kSouth, kNorth, {}, path));
    CHECK(path.count >= 3);
    const std::span<const ct::NavCorner> corners = path.view();
    CHECK(std::any_of(corners.begin(), corners.end(),
                      [](const ct::NavCorner& corner) { return corner.position.x > 6.0f; }));
    CHECK(pathLength(path) > 14.0f);
}

TEST_CASE("doors are areas paths go through, and a blocked door stops them") {
    const ct::NavMesh mesh = buildMesh(doorLevel({{"activate", "touch"}}));
    CHECK(mesh.warnings().empty());
    const ct::NavQuery query(mesh);
    ct::NavPath path;
    REQUIRE(query.findPath(kSouth, kNorth, {}, path));
    CHECK(passes(path, ct::NavArea::Door, 0));
    // Straight through the doorway.
    CHECK(pathLength(path) < 10.1f);

    const std::uint32_t locked[] = {0};
    CHECK_FALSE(query.findPath(kSouth, kNorth, {locked}, path));
    CHECK(path.count > 0);
    CHECK(path.corners[path.count - 1].position.y < 0.0f);
}

TEST_CASE("lifts link their stops, and paths ride them") {
    const ct::NavMesh mesh = buildMesh(liftLevel());
    CHECK(mesh.warnings().empty());
    const std::vector<ct::NavLink> links = mesh.links();
    REQUIRE(links.size() == 1);
    CHECK(links[0].mover == 0);
    CHECK(std::fabs(links[0].start.z) < 0.01f);
    CHECK(std::fabs(links[0].end.z - 4.0f) < 0.01f);

    const ct::NavQuery query(mesh);
    const ct::Vec3 top{0.0f, 6.0f, 4.0f};
    ct::NavPath path;
    REQUIRE(query.findPath(kSouth, top, {}, path));
    CHECK(passes(path, ct::NavArea::Lift, 0));
    CHECK(std::fabs(path.corners[path.count - 1].position.z - 4.0f) < 0.1f);
    // And back down.
    REQUIRE(query.findPath(top, kSouth, {}, path));
    CHECK(passes(path, ct::NavArea::Lift, 0));

    const std::uint32_t switchedOff[] = {0};
    CHECK_FALSE(query.findPath(kSouth, top, {switchedOff}, path));
}

TEST_CASE("hurt volumes are kept out of unless a path is allowed through them") {
    // Lava across the whole floor.
    const ct::NavMesh mesh =
        buildMesh({{smallFloor(), owned({-10.0f, -1.0f, -0.5f}, {10.0f, 1.0f, 0.25f}, 0)},
                   {entity("hurt", {})}});
    CHECK(mesh.warnings().empty());
    const ct::NavQuery query(mesh);
    ct::NavPath path;
    CHECK_FALSE(query.findPath(kSouth, kNorth, {}, path));
    ct::NavFilter through;
    through.avoidHazards = false;
    REQUIRE(query.findPath(kSouth, kNorth, through, path));
    CHECK(passes(path, ct::NavArea::Hazard, ct::kNoNavMover));
}

TEST_CASE("steps the player can climb join floors, and higher ledges don't") {
    for (const auto& [height, joined] : {std::pair{0.4f, true}, std::pair{1.0f, false}}) {
        CAPTURE(height);
        const ct::NavMesh mesh = buildMesh(
            {{smallFloor(), ct::makeBoxBrush({2.0f, -4.0f, 0.0f}, {8.0f, 4.0f, height})}, {}});
        const ct::NavQuery query(mesh);
        ct::NavPath path;
        CHECK(query.findPath({-5.0f, 0.0f, 0.0f}, {5.0f, 0.0f, height}, {}, path) == joined);
    }
}

TEST_CASE("each mover is a door, a lift or an obstacle, by its nav setting or how it moves") {
    const ct::Level level{
        {owned({0, 0, 0}, {1, 1, 1}, 0), owned({0, 0, 0}, {1, 1, 1}, 1),
         owned({0, 0, 0}, {1, 1, 1}, 2), owned({0, 0, 0}, {1, 1, 1}, 3),
         owned({0, 0, 0}, {1, 1, 1}, 4), owned({0, 0, 0}, {1, 1, 1}, 5),
         owned({0, 0, 0}, {1, 1, 1}, 6)},
        {entity("mover", {{"move", "0 0 3"}, {"activate", "touch"}}),
         entity("mover", {{"move", "0 0 3"}, {"activate", "stand"}}),
         entity("mover", {{"move", "4 0 0"}, {"loop", "1"}}), entity("mover", {{"move", "0 0 3"}}),
         entity("mover", {}),
         entity("mover", {{"move", "0 0 3"}, {"activate", "stand"}, {"nav", "solid"}}),
         entity("hurt", {})}};
    const ct::NavInput input = ct::navInput(level);
    using enum ct::NavMoverKind;
    const ct::NavMoverKind expected[] = {Door, Lift, Lift, Door, Solid, Solid};
    REQUIRE(input.movers.size() == std::size(expected));
    for (std::uint32_t i = 0; i < input.movers.size(); ++i) {
        CAPTURE(i);
        CHECK(input.movers[i].id == i);
        CHECK(input.movers[i].kind == expected[i]);
        CHECK(input.movers[i].brushes.size() == 1);
    }
    CHECK(input.hazards.size() == 1);
    CHECK(input.solid.empty());
}

TEST_CASE("a solid mover is an obstacle where it is built") {
    ct::Level level = doorLevel({{"nav", "solid"}});
    const ct::NavMesh mesh = buildMesh(level);
    const ct::NavQuery query(mesh);
    ct::NavPath path;
    CHECK_FALSE(query.findPath(kSouth, kNorth, {}, path));
}

TEST_CASE("building the same level twice gives the same mesh") {
    const ct::NavMesh first = buildMesh(liftLevel());
    const ct::NavMesh second = buildMesh(liftLevel());
    const std::vector<ct::NavTriangle> a = first.triangles();
    const std::vector<ct::NavTriangle> b = second.triangles();
    REQUIRE(a.size() == b.size());
    CHECK_FALSE(a.empty());
    for (std::size_t i = 0; i < a.size(); ++i) {
        for (std::size_t k = 0; k < 3; ++k) {
            CHECK(a[i].corners[k].x == b[i].corners[k].x);
            CHECK(a[i].corners[k].y == b[i].corners[k].y);
            CHECK(a[i].corners[k].z == b[i].corners[k].z);
        }
        CHECK(a[i].area == b[i].area);
    }
    CHECK(first.edges().size() == second.edges().size());
}

TEST_CASE("a game level builds its mesh once, when first asked, for the player's size") {
    ct::GameSettings settings;
    settings.movement.halfExtents = {0.5f, 0.5f, 1.0f};
    const ct::Level level{{smallFloor()},
                          {entity(std::string(ct::kPlayerSpawnClass), {{"origin", "0 0 0"}})}};
    const ct::GameLevel gameLevel(level, {}, settings);
    const ct::NavMesh& mesh = gameLevel.navMesh();
    CHECK(&mesh == &gameLevel.navMesh());
    CHECK(mesh.agent().radius == 0.5f);
    CHECK(mesh.agent().height == 2.0f);
    CHECK(mesh.polygonCount() > 0);
}
