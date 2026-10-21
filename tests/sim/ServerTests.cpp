#include "SimHarness.h"

#include "croftech/client/ClientConnection.h"
#include "croftech/client/LocalGame.h"
#include "croftech/net/LoopbackLink.h"
#include "croftech/net/Protocol.h"
#include "croftech/server/Server.h"
#include "croftech/sim/StateSave.h"

#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = ct::Transport::Clock;

constexpr auto kTick = std::chrono::duration_cast<Clock::duration>(
    std::chrono::nanoseconds(1'000'000'000 / ct::kTickRate));

ct::UserCmd forward() {
    ct::UserCmd command;
    command.forward = 127;
    return command;
}

// A server and a client joined by a link, run a tick at a time on a made-up clock.
struct Session {
    ct::LoopbackLink link;
    ct::Server server;
    ct::ClientConnection client;
    Clock::time_point now{};

    explicit Session(ct::LinkConditions conditions = {},
                     const ct::Level& level = ct::test::floorLevel())
        : link(conditions, 5), server(level, {}), client(link.clientEnd()) {
        server.addConnection(link.serverEnd());
    }

    // Each tick: the client sends its command, the server reads and steps, the client reads.
    void run(const ct::UserCmd& command, int ticks) {
        for (int i = 0; i < ticks; ++i) {
            client.sendInput(command, now);
            server.receive(now);
            server.tick(now);
            client.receive(now);
            now += kTick;
        }
    }

    void sendRaw(const ct::ClientMessage& message) {
        ct::ByteWriter writer;
        ct::writeClientMessage(writer, message);
        link.clientEnd().send(writer.bytes(), now);
    }

    const ct::Transform* clientPlayer() const {
        return client.current() != nullptr ? client.current()->world.transforms.get(client.player())
                                           : nullptr;
    }
};

} // namespace

TEST_CASE("a client joins and its inputs drive its player on the server") {
    Session session;
    session.run({}, 2);
    REQUIRE(session.client.joined());
    CHECK(session.server.playerCount() == 1);
    REQUIRE(session.clientPlayer() != nullptr);

    session.run(forward(), ct::kTickRate);
    CHECK(session.clientPlayer()->position.x > 3.0f);
    // With no delay, the newest snapshot is exactly the server's state.
    CHECK(ct::hashState(*session.client.current()) == ct::hashState(session.server.state()));
    CHECK(session.client.acknowledgedInput() == 2 + ct::kTickRate);
}

TEST_CASE("the network changes nothing about the simulation") {
    // The same commands, stepped directly, give bit-identical movement.
    Session session;
    session.run({}, 2);
    session.run(forward(), 300);

    ct::test::SimHarness direct({ct::test::floorBrush()});
    direct.spawn({0.0f, 0.0f, 0.0f});
    direct.run({}, 2);
    direct.run(forward(), 300);
    CHECK(session.clientPlayer()->position == direct.transform().position);
}

TEST_CASE("play still works at 100 ms with jitter and loss, a little behind the server") {
    Session session({100.0f, 20.0f, 5.0f});
    session.run(forward(), 2 * ct::kTickRate);
    REQUIRE(session.client.joined());
    CHECK(session.clientPlayer()->position.x > 3.0f);
    const ct::TickIndex behind = session.server.state().tick - session.client.current()->tick;
    CHECK(behind >= 12); // 100 ms is 12.8 ticks
    CHECK(behind < 30);
    CHECK(session.client.malformedPackets() == 0);
}

TEST_CASE("malformed and late packets are dropped") {
    Session session;
    session.run({}, 2);
    session.link.clientEnd().send(std::vector<std::byte>{std::byte{9}, std::byte{9}}, session.now);
    session.sendRaw(ct::HelloMessage{99}); // another protocol version
    session.server.receive(session.now);
    CHECK(session.server.malformedPackets() == 2);

    // A newer input, then an older one arriving late: the newer one stands.
    ct::InputMessage newer{1000, forward()};
    ct::InputMessage older{999, forward()};
    older.command.forward = -127;
    session.sendRaw(newer);
    session.sendRaw(older);
    session.server.receive(session.now);
    const float x =
        session.server.state().world.transforms.get(session.client.player())->position.x;
    session.server.tick(session.now);
    CHECK(session.server.state().world.transforms.get(session.client.player())->position.x > x);
}

TEST_CASE("goodbye takes the player out of the game") {
    Session session;
    session.run({}, 2);
    const ct::EntityId player = session.client.player();
    session.client.disconnect(session.now);
    session.server.receive(session.now);
    CHECK(session.server.playerCount() == 0);
    CHECK_FALSE(session.server.state().world.entities.isAlive(player));
}

TEST_CASE("a local game runs its server on a thread and shows the player moving") {
    ct::LocalGame game(ct::test::floorLevel(), {});
    // Walking half a metre takes about 25 ticks (200 ms). Loaded test machines, sanitizer builds on
    // CI especially, can run the thread far slower, so wait for it up to a generous deadline.
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    std::optional<ct::Vec3> eye;
    while (Clock::now() < deadline && !(eye && eye->x > 0.5f)) {
        game.update(forward(), Clock::now());
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        eye = game.eyePosition(Clock::now());
    }
    REQUIRE(eye.has_value());
    CHECK(eye->x > 0.5f);
    CHECK(game.malformedPackets() == 0);
}

TEST_CASE("restoring a saved state puts the player back, and the client follows it back in time") {
    Session session;
    session.run({}, 2);
    session.run(forward(), 64);
    const auto saved = std::make_unique<ct::State>(session.server.state());
    const ct::Vec3 savedPosition = session.clientPlayer()->position;
    session.run(forward(), 64);
    REQUIRE(session.clientPlayer()->position.x > savedPosition.x + 1.0f);

    session.server.restore(*saved);
    session.run({}, 1);
    CHECK(session.client.current()->tick == saved->tick + 1);
    CHECK(session.client.timeline() == 1);
    // Still moving as it was when saved, and slowing with no input.
    CHECK(session.clientPlayer()->position.x > savedPosition.x);
    CHECK(session.clientPlayer()->position.x < savedPosition.x + 0.2f);
    CHECK(session.server.playerCount() == 1);
    CHECK(session.server.state().world.playerControls.size() == 1);
}

TEST_CASE("a client that joins after a state is restored takes over its slot's player") {
    Session first;
    first.run({}, 2);
    first.run(forward(), ct::kTickRate);
    const auto saved = std::make_unique<ct::State>(first.server.state());

    Session second;
    second.server.restore(*saved);
    second.run({}, 2);
    REQUIRE(second.client.joined());
    CHECK(second.clientPlayer()->position.x > 3.0f);
    CHECK(second.server.state().world.playerControls.size() == 1);
}

TEST_CASE("reaching a checkpoint keeps a save of that moment") {
    ct::Level level = ct::test::floorLevel();
    ct::Brush volume = ct::makeBoxBrush({3.0f, -2.0f, 0.0f}, {5.0f, 2.0f, 2.0f});
    volume.entity = 1;
    level.brushes.push_back(volume);
    level.entities.push_back({"checkpoint", {}});
    Session session({}, level);
    session.run({}, 2);
    CHECK(session.server.checkpointCount() == 0);
    session.run(forward(), 2 * ct::kTickRate);
    REQUIRE(session.server.checkpointCount() == 1);

    auto state = std::make_unique<ct::State>();
    std::string error;
    REQUIRE_MESSAGE(ct::loadState(session.server.checkpointState(), *state, error), error);
    const ct::Transform* player = state->world.transforms.get(session.client.player());
    REQUIRE(player != nullptr);
    CHECK(player->position.x > 2.5f);
    CHECK(player->position.x < 3.0f);
}

TEST_CASE("players restored from saves made before health get the starting health") {
    Session session;
    session.run({}, 2);
    const auto saved = std::make_unique<ct::State>(session.server.state());
    saved->world.healths.remove(session.client.player());
    session.server.restore(*saved);
    const ct::Health* health = session.server.state().world.healths.get(session.client.player());
    REQUIRE(health != nullptr);
    CHECK(health->health == ct::RulesSettings{}.startHealth);
}

TEST_CASE("a second player joins at the spawn point furthest from the first") {
    ct::Level level = ct::test::floorLevel();
    level.entities.push_back(ct::makePlayerSpawn({30.0f, 0.0f, 0.0f}, 0.0f));
    Session session({}, level);
    ct::LoopbackLink second(ct::LinkConditions{}, 6);
    session.server.addConnection(second.serverEnd());
    ct::ClientConnection other(second.clientEnd());
    for (int i = 0; i < 3; ++i) {
        session.client.sendInput({}, session.now);
        other.sendInput({}, session.now);
        session.server.receive(session.now);
        session.server.tick(session.now);
        session.client.receive(session.now);
        other.receive(session.now);
        session.now += kTick;
    }
    REQUIRE(session.server.playerCount() == 2);
    const ct::World& world = session.server.state().world;
    const float a = world.transforms.get(session.client.player())->position.x;
    const float b = world.transforms.get(other.player())->position.x;
    CHECK(std::abs(a - b) == doctest::Approx(30.0f));
}
