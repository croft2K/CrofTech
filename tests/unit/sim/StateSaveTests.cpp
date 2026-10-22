#include "croftech/core/ByteStream.h"
#include "croftech/sim/StateSave.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "sim/SimHarness.h"

namespace {

using ct::test::SimHarness;

// Three players mid-jump, one destroyed so the free list isn't empty, after a second of play.
std::unique_ptr<ct::State> makeBusyState() {
    SimHarness sim({ct::test::floorBrush()});
    auto state = std::make_unique<ct::State>(sim.state());
    for (std::uint32_t slot = 0; slot < 3; ++slot) {
        const float x = static_cast<float>(slot) * 2.0f;
        ct::spawnPlayer(*state, sim.settings(), {{x, 0.0f, 0.0f}, 0}, slot);
    }
    state->world.destroy(state->world.playerControls.entities()[1]);

    ct::Simulation simulation(sim.level(), sim.settings());
    ct::InputSet inputs;
    inputs.commands[0].forward = 127;
    inputs.commands[0].buttons = ct::kButtonJump;
    inputs.commands[2].side = -127;
    auto next = std::make_unique<ct::State>();
    for (int i = 0; i < 128; ++i) {
        simulation.step(*state, inputs, *next);
        std::swap(state, next);
    }
    return state;
}

struct Rejection {
    std::string error;
    bool loaded = true;
};

Rejection load(std::span<const std::byte> bytes) {
    auto state = std::make_unique<ct::State>();
    Rejection result;
    result.loaded = ct::loadState(bytes, *state, result.error);
    return result;
}

// Writes a save header and an entity table of `count` live entities.
void writeHeader(ct::ByteWriter& writer, std::uint32_t count) {
    writer.writeU32(0x54535443);
    writer.writeU32(ct::kStateSaveVersion);
    writer.writeU32(7); // tick
    writer.writeU32(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        writer.writeU32(0);
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        writer.writeU8(1);
    }
    writer.writeU32(0);
}

} // namespace

TEST_CASE("a saved state loads back identical and plays on identically") {
    const auto original = makeBusyState();
    const std::vector<std::byte> bytes = ct::saveState(*original);

    auto loaded = std::make_unique<ct::State>();
    std::string error;
    REQUIRE_MESSAGE(ct::loadState(bytes, *loaded, error), error);
    CHECK(loaded->tick == original->tick);
    CHECK(ct::hashState(*loaded) == ct::hashState(*original));
    CHECK(ct::saveState(*loaded) == bytes);

    const ct::GameLevel level(ct::Level{{ct::test::floorBrush()}, {}});
    ct::Simulation simulation(level, ct::GameSettings{});
    ct::InputSet inputs;
    inputs.commands[2].forward = 127;
    inputs.commands[2].buttons = ct::kButtonJump;
    auto a = std::make_unique<ct::State>(*original);
    auto b = std::make_unique<ct::State>(*loaded);
    auto next = std::make_unique<ct::State>();
    for (int i = 0; i < 256; ++i) {
        simulation.step(*a, inputs, *next);
        std::swap(a, next);
        simulation.step(*b, inputs, *next);
        std::swap(b, next);
        REQUIRE(ct::hashState(*a) == ct::hashState(*b));
    }
}

TEST_CASE("saves survive components gaining, losing and reordering fields") {
    ct::ByteWriter writer;
    writeHeader(writer, 1);
    writer.writeU32(2); // component types

    // A transform saved by a build where it had an extra field and no pitch, in another order.
    writer.writeShortString("transform");
    writer.writeU8(3);
    writer.writeShortString("yaw");
    writer.writeU8(static_cast<std::uint8_t>(ct::FieldType::U16));
    writer.writeShortString("roll");
    writer.writeU8(static_cast<std::uint8_t>(ct::FieldType::Float));
    writer.writeShortString("position");
    writer.writeU8(static_cast<std::uint8_t>(ct::FieldType::Vec3));
    writer.writeU32(1);
    writer.writeU32(0); // entity 0, generation 0
    writer.writeU32(0);
    writer.writeU16(16384);
    writer.writeF32(99.0f);
    writer.writeF32(1.0f);
    writer.writeF32(2.0f);
    writer.writeF32(3.0f);

    // A component type this build doesn't have.
    writer.writeShortString("old_health");
    writer.writeU8(1);
    writer.writeShortString("points");
    writer.writeU8(static_cast<std::uint8_t>(ct::FieldType::I32));
    writer.writeU32(1);
    writer.writeU32(0);
    writer.writeU32(0);
    writer.writeU32(100);

    auto state = std::make_unique<ct::State>();
    std::string error;
    REQUIRE_MESSAGE(ct::loadState(writer.bytes(), *state, error), error);
    CHECK(state->tick == 7);
    const ct::Transform* transform = state->world.transforms.get({0, 0});
    REQUIRE(transform != nullptr);
    CHECK(transform->position == ct::Vec3{1.0f, 2.0f, 3.0f});
    CHECK(transform->yaw == 16384);
    CHECK(transform->pitch == 0);
    CHECK(state->world.playerMotions.size() == 0);
}

TEST_CASE("malformed saves are rejected without changing the state") {
    const auto original = makeBusyState();
    const std::vector<std::byte> good = ct::saveState(*original);

    // Every truncation fails cleanly.
    for (std::size_t length = 0; length < good.size(); ++length) {
        CAPTURE(length);
        CHECK_FALSE(load(std::span{good}.first(length)).loaded);
    }
    std::vector<std::byte> trailing = good;
    trailing.push_back(std::byte{0});
    CHECK(load(trailing).error == "save is truncated or has trailing bytes");

    std::vector<std::byte> badMagic = good;
    badMagic[0] = std::byte{'X'};
    CHECK(load(badMagic).error == "not a CrofTech state save");

    std::vector<std::byte> badVersion = good;
    badVersion[4] = std::byte{9};
    CHECK(load(badVersion).error == "unsupported save version 9");

    // A failed load leaves the target alone.
    auto target = std::make_unique<ct::State>(*original);
    std::string error;
    CHECK_FALSE(ct::loadState(badMagic, *target, error));
    CHECK(ct::hashState(*target) == ct::hashState(*original));
}

TEST_CASE("saved values and references are checked") {
    const auto oneComponent = [](std::string_view type, std::string_view field,
                                 ct::FieldType fieldType, std::uint32_t entity, auto writeValue) {
        ct::ByteWriter writer;
        writeHeader(writer, 1);
        writer.writeU32(1);
        writer.writeShortString(type);
        writer.writeU8(1);
        writer.writeShortString(field);
        writer.writeU8(static_cast<std::uint8_t>(fieldType));
        writer.writeU32(1);
        writer.writeU32(entity);
        writer.writeU32(0);
        writeValue(writer);
        return writer.take();
    };

    const auto slot = [](std::uint32_t value) {
        return [value](ct::ByteWriter& writer) { writer.writeU32(value); };
    };
    CHECK(
        load(oneComponent("player_control", "inputSlot", ct::FieldType::U32, 0, slot(15))).loaded);
    CHECK(
        load(oneComponent("player_control", "inputSlot", ct::FieldType::U32, 0, slot(16))).error ==
        "player_control.inputSlot of entity 0 is out of range");
    CHECK(load(oneComponent("player_control", "inputSlot", ct::FieldType::U32, 3, slot(0))).error ==
          "player_control belongs to an entity that does not exist");
    CHECK(load(oneComponent("player_control", "inputSlot", ct::FieldType::I32, 0, slot(0))).error ==
          "field player_control.inputSlot is saved as i32 but is now u32");

    const auto nan = [](ct::ByteWriter& writer) {
        writer.writeF32(std::numeric_limits<float>::quiet_NaN());
        writer.writeF32(0.0f);
        writer.writeF32(0.0f);
    };
    CHECK(load(oneComponent("transform", "position", ct::FieldType::Vec3, 0, nan)).error ==
          "transform.position of entity 0 is out of range");

    // The same entity twice in one pool.
    ct::ByteWriter twice;
    writeHeader(twice, 1);
    twice.writeU32(1);
    twice.writeShortString("player_control");
    twice.writeU8(0);
    twice.writeU32(2);
    for (int i = 0; i < 2; ++i) {
        twice.writeU32(0);
        twice.writeU32(0);
    }
    CHECK(load(twice.bytes()).error == "entity 0 has two player_control components");
}

TEST_CASE("an inconsistent entity table is rejected") {
    const std::uint32_t generations[] = {0, 0, 0};
    const std::uint8_t alive[] = {1, 0, 1};
    const std::uint32_t freeList[] = {1};
    ct::EntityAllocator<8> entities;
    CHECK(entities.restore(generations, alive, freeList));
    CHECK(entities.highWater() == 3);
    CHECK(entities.create() == ct::EntityId{1, 0});

    const std::uint32_t freeAlive[] = {0};
    const std::uint32_t freeTwice[] = {1, 1};
    const std::uint8_t badFlag[] = {1, 0, 2};
    CHECK_FALSE(entities.restore(generations, alive, {})); // index 1 neither alive nor free
    CHECK_FALSE(entities.restore(generations, alive, freeAlive));
    CHECK_FALSE(entities.restore(generations, alive, freeTwice));
    CHECK_FALSE(entities.restore(generations, badFlag, freeList));
    CHECK_FALSE(entities.restore(generations, std::span{alive}.first(2), freeList));
    const std::uint32_t tooMany[9] = {};
    const std::uint8_t tooManyAlive[9] = {1, 1, 1, 1, 1, 1, 1, 1, 1};
    CHECK_FALSE(entities.restore(tooMany, tooManyAlive, {}));
    CHECK(entities.highWater() == 3); // unchanged by the failures
}
