#include "SimHarness.h"

#include "croftech/client/ClientConnection.h"
#include "croftech/client/LocalGame.h"
#include "croftech/net/LoopbackLink.h"
#include "croftech/net/Protocol.h"
#include "croftech/server/Server.h"

#include <doctest/doctest.h>

#include <chrono>
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
    std::vector<ct::Brush> brushes{ct::test::floorBrush()};
    ct::LoopbackLink link;
    ct::Server server;
    ct::ClientConnection client;
    Clock::time_point now{};

    explicit Session(ct::LinkConditions conditions = {})
        : link(conditions, 5), server(brushes, {ct::SpawnPoint{}}, {}), client(link.clientEnd()) {
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
    const std::vector<ct::Brush> brushes{ct::test::floorBrush()};
    ct::LocalGame game(brushes, {ct::SpawnPoint{}}, {});
    const auto end = Clock::now() + std::chrono::milliseconds(400);
    while (Clock::now() < end) {
        game.update(forward(), Clock::now());
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const auto eye = game.eyePosition(Clock::now());
    REQUIRE(eye.has_value());
    // About 50 ticks in 400 ms; allow a wide margin for loaded test machines.
    CHECK(eye->x > 0.5f);
    CHECK(game.malformedPackets() == 0);
}
