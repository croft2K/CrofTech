#include "SimHarness.h"

#include "croftech/client/SimThread.h"

#include <doctest/doctest.h>

#include <chrono>
#include <memory>
#include <thread>

TEST_CASE("the simulation thread advances ticks and applies submitted input") {
    const std::vector<ct::Brush> brushes{ct::test::floorBrush()};
    const ct::CollisionWorld collision(brushes);
    const ct::MovementSettings movement;
    auto initial = std::make_unique<ct::State>();
    const ct::EntityId player = ct::spawnPlayer(initial->world, movement, {}, 0);

    ct::SimThread sim(collision, movement, *initial);
    ct::UserCmd cmd;
    cmd.forward = 127;
    sim.submitInput(0, cmd);
    sim.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    sim.stop();

    ct::TickIndex tick = 0;
    float x = 0.0f;
    sim.readStates([&](const ct::State&, const ct::State& current, auto) {
        tick = current.tick;
        x = current.world.transforms.get(player)->position.x;
    });
    // About 38 ticks in 300 ms; allow a wide margin for loaded test machines.
    CHECK(tick >= 10);
    CHECK(x > 0.5f);
}
