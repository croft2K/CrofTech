#include "croftech/sim/World.h"

#include <doctest/doctest.h>

#include <memory>

TEST_CASE("entity handles go stale when their entity is destroyed") {
    ct::EntityAllocator<4> entities;
    const ct::EntityId a = entities.create();
    REQUIRE(entities.isAlive(a));
    CHECK(entities.destroy(a));
    CHECK_FALSE(entities.isAlive(a));

    const ct::EntityId reused = entities.create();
    CHECK(reused.index == a.index);
    CHECK(reused.generation == a.generation + 1);
    CHECK_FALSE(entities.isAlive(a));
    CHECK(entities.isAlive(reused));
}

TEST_CASE("the allocator returns an invalid handle when full") {
    ct::EntityAllocator<2> entities;
    CHECK(entities.create().isValid());
    CHECK(entities.create().isValid());
    CHECK_FALSE(entities.create().isValid());
}

TEST_CASE("removing a component keeps the others packed and reachable") {
    ct::ComponentPool<ct::PlayerControl, 8> pool;
    const ct::EntityId a{0, 0};
    const ct::EntityId b{1, 0};
    const ct::EntityId c{2, 0};
    pool.add(a, {10});
    pool.add(b, {11});
    pool.add(c, {12});

    pool.remove(a);
    CHECK(pool.size() == 2);
    CHECK_FALSE(pool.has(a));
    REQUIRE(pool.get(b) != nullptr);
    REQUIRE(pool.get(c) != nullptr);
    CHECK(pool.get(b)->inputSlot == 11);
    CHECK(pool.get(c)->inputSlot == 12);
}

TEST_CASE("a pool does not resolve a stale handle to a reused index") {
    ct::ComponentPool<ct::PlayerControl, 8> pool;
    pool.add({3, 0}, {7});
    CHECK(pool.get({3, 1}) == nullptr);
}

TEST_CASE("destroying an entity removes all of its components") {
    auto world = std::make_unique<ct::World>();
    const ct::EntityId id = world->entities.create();
    world->transforms.add(id, {});
    world->playerControls.add(id, {});
    world->destroy(id);
    CHECK(world->transforms.size() == 0);
    CHECK(world->playerControls.size() == 0);
    CHECK_FALSE(world->entities.isAlive(id));
}
