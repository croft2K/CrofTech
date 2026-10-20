#include "croftech/sim/CollisionWorld.h"

#include <doctest/doctest.h>

#include <vector>

namespace {

const ct::Vec3 kHalf{0.5f, 0.5f, 0.5f};

ct::CollisionWorld makeWallWorld() {
    // A wall occupying x in [5, 6].
    const std::vector<ct::Brush> brushes{
        ct::makeBoxBrush({5.0f, -10.0f, -10.0f}, {6.0f, 10.0f, 10.0f})};
    return ct::CollisionWorld(brushes);
}

} // namespace

TEST_CASE("a box stops just short of a wall") {
    const ct::CollisionWorld world = makeWallWorld();
    const ct::TraceResult trace = world.traceBox({0.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}, kHalf);
    CHECK(trace.fraction < 1.0f);
    CHECK(trace.endPosition.x == doctest::Approx(4.5f).epsilon(0.001));
    CHECK(trace.endPosition.x < 4.5f);
    CHECK(trace.normal == ct::Vec3{-1.0f, 0.0f, 0.0f});
    CHECK_FALSE(trace.startSolid);
}

TEST_CASE("a box moving away from a wall is not blocked") {
    const ct::CollisionWorld world = makeWallWorld();
    const ct::TraceResult trace = world.traceBox({4.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, kHalf);
    CHECK(trace.fraction == 1.0f);
}

TEST_CASE("a trace that misses everything completes") {
    const ct::CollisionWorld world = makeWallWorld();
    const ct::TraceResult trace = world.traceBox({0.0f, 0.0f, 0.0f}, {0.0f, 5.0f, 0.0f}, kHalf);
    CHECK(trace.fraction == 1.0f);
    CHECK(trace.endPosition == ct::Vec3{0.0f, 5.0f, 0.0f});
}

TEST_CASE("a box inside a brush reports all solid") {
    const ct::CollisionWorld world = makeWallWorld();
    const ct::TraceResult trace = world.traceBox({5.5f, 0.0f, 0.0f}, {5.6f, 0.0f, 0.0f}, kHalf);
    CHECK(trace.startSolid);
    CHECK(trace.allSolid);
}

TEST_CASE("a box lands on a ramp surface, not its bounding box") {
    // Ramp rising from z=0 at x=0 to z=2 at x=4.
    const std::vector<ct::Brush> brushes{
        ct::makeRampBrush({0.0f, -5.0f, 0.0f}, {4.0f, 5.0f, 2.0f})};
    const ct::CollisionWorld world(brushes);
    const ct::TraceResult trace = world.traceBox({1.0f, 0.0f, 5.0f}, {1.0f, 0.0f, -5.0f}, kHalf);
    REQUIRE(trace.fraction < 1.0f);
    CHECK(trace.normal.z > 0.8f);
    CHECK(trace.normal.x < 0.0f);
    // The box's uphill bottom edge (x = 1.5) rests on the slope at z = 0.75.
    CHECK(trace.endPosition.z == doctest::Approx(1.25f).epsilon(0.01));
}
