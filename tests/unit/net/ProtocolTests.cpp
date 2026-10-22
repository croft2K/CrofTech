#include "croftech/core/Angle.h"
#include "croftech/net/Protocol.h"
#include "croftech/sim/Simulation.h"
#include "croftech/sim/StateSave.h"

#include <doctest/doctest.h>

#include <cstddef>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace {

std::vector<std::byte> bytesOf(const ct::ClientMessage& message) {
    ct::ByteWriter writer;
    ct::writeClientMessage(writer, message);
    return writer.take();
}

std::vector<std::byte> bytes(std::initializer_list<int> values) {
    std::vector<std::byte> result;
    for (const int value : values) {
        result.push_back(static_cast<std::byte>(value));
    }
    return result;
}

} // namespace

TEST_CASE("client messages read back as written") {
    const auto hello = ct::readClientMessage(bytesOf(ct::HelloMessage{}));
    REQUIRE(hello.has_value());
    CHECK(std::get<ct::HelloMessage>(*hello).protocolVersion == ct::kProtocolVersion);

    ct::InputMessage input;
    input.sequence = 123456;
    input.command = {40000, -1200, 127, -127, ct::kButtonJump, 3};
    const auto read = ct::readClientMessage(bytesOf(input));
    REQUIRE(read.has_value());
    const auto& back = std::get<ct::InputMessage>(*read);
    CHECK(back.sequence == 123456);
    CHECK(back.command.yaw == 40000);
    CHECK(back.command.pitch == -1200);
    CHECK(back.command.forward == 127);
    CHECK(back.command.side == -127);
    CHECK(back.command.buttons == ct::kButtonJump);
    CHECK(back.command.weapon == 3);

    CHECK(std::holds_alternative<ct::GoodbyeMessage>(
        *ct::readClientMessage(bytesOf(ct::GoodbyeMessage{}))));
}

TEST_CASE("malformed client messages are rejected") {
    CHECK_FALSE(ct::readClientMessage({}));
    CHECK_FALSE(ct::readClientMessage(bytes({9})));       // unknown type
    CHECK_FALSE(ct::readClientMessage(bytes({1, 1})));    // hello cut short
    CHECK_FALSE(ct::readClientMessage(bytes({3, 0})));    // goodbye with extra bytes
    CHECK_FALSE(ct::readClientMessage(bytes({4, 0, 0}))); // a snapshot is not a client message

    ct::InputMessage input;
    std::vector<std::byte> packet = bytesOf(input);
    packet.pop_back();
    CHECK_FALSE(ct::readClientMessage(packet));

    const auto rejects = [](ct::UserCmd command) {
        ct::InputMessage bad;
        bad.command = command;
        return !ct::readClientMessage(bytesOf(bad)).has_value();
    };
    CHECK(rejects({0, ct::kMaxPitch + 1, 0, 0, 0}));
    CHECK(rejects({0, 0, -128, 0, 0}));
    CHECK(rejects({0, 0, 0, -128, 0}));
    CHECK(rejects({0, 0, 0, 0, 0x8000}));
    CHECK(rejects({0, 0, 0, 0, 0, ct::kMaxWeapons + 1}));
    // The sub-tick fields are reserved until M7 gives them meaning.
    CHECK(rejects({0, 0, 0, 0, 0, 0, 1}));
    CHECK(rejects({0, 0, 0, 0, 0, 0, 0, 1}));
    CHECK_FALSE(rejects({0, -ct::kMaxPitch, -127, 127, ct::kButtonCrouch, ct::kMaxWeapons}));
}

TEST_CASE("snapshots carry the state and the player") {
    auto state = std::make_unique<ct::State>();
    state->tick = 77;
    const ct::EntityId player = ct::spawnPlayer(*state, {}, {{1.0f, 2.0f, 3.0f}, 0}, 0);
    ct::ByteWriter writer;
    ct::writeSnapshot(writer, 3, 41, player, *state);

    const auto snapshot = ct::readSnapshot(writer.bytes());
    REQUIRE(snapshot.has_value());
    CHECK(snapshot->tick == 77);
    CHECK(snapshot->timeline == 3);
    CHECK(snapshot->lastInputSequence == 41);
    CHECK(snapshot->player == player);
    auto loaded = std::make_unique<ct::State>();
    std::string error;
    REQUIRE_MESSAGE(ct::loadState(snapshot->state, *loaded, error), error);
    CHECK(ct::hashState(*loaded) == ct::hashState(*state));

    CHECK_FALSE(ct::readSnapshot(bytesOf(ct::GoodbyeMessage{})));
    std::vector<std::byte> header(writer.bytes().begin(), writer.bytes().begin() + 21);
    CHECK_FALSE(ct::readSnapshot(header)); // no state after the header
    std::vector<std::byte> badPlayer = writer.bytes();
    badPlayer[13] = std::byte{0xFF}; // player index far past the entity limit
    badPlayer[14] = std::byte{0x10};
    CHECK_FALSE(ct::readSnapshot(badPlayer));
}
