#pragma once

#include "croftech/core/ByteStream.h"
#include "croftech/net/Transport.h"
#include "croftech/sim/UserCmd.h"
#include "croftech/sim/World.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace ct {

// A game client's side of the link to a server. It asks to join (repeating the hello, since
// packets can be lost, until the first snapshot arrives), sends the player's command every frame,
// and keeps the two newest snapshots for drawing between them. It never steps the simulation
// itself: what it shows is what the server decided. Prediction arrives with real networking (M6).
class ClientConnection {
public:
    using Clock = Transport::Clock;

    explicit ClientConnection(Transport& transport);
    ClientConnection(const ClientConnection&) = delete;
    ClientConnection& operator=(const ClientConnection&) = delete;

    void sendInput(const UserCmd& command, Clock::time_point now);
    // Reads every snapshot that has arrived. Late ones (older than the newest) are skipped;
    // malformed ones are dropped and counted. The first snapshot of a new timeline (the server
    // loaded a save) replaces both states, since there is nothing to draw between.
    void receive(Clock::time_point now);
    // Tells the server the player is leaving.
    void disconnect(Clock::time_point now);

    bool joined() const { return m_current != nullptr; }
    EntityId player() const { return m_player; }
    // The two newest states (the same one until a second arrives) and when the newest arrived.
    // Null until joined.
    const State* previous() const { return m_previous.get(); }
    const State* current() const { return m_current.get(); }
    Clock::time_point currentArrivedAt() const { return m_arrivedAt; }
    // The newest input the server had received when it sent the newest snapshot.
    std::uint32_t acknowledgedInput() const { return m_acknowledged; }
    std::uint64_t malformedPackets() const { return m_malformed; }
    // Changes when the server's state is replaced, as by loading a save.
    std::uint32_t timeline() const { return m_timeline; }

private:
    Transport& m_transport;
    std::uint32_t m_sequence = 0;
    Clock::time_point m_nextHello;
    std::unique_ptr<State> m_previous;
    std::unique_ptr<State> m_current;
    std::unique_ptr<State> m_incoming; // decoded into, then swapped in
    EntityId m_player;
    std::uint32_t m_timeline = 0;
    Clock::time_point m_arrivedAt;
    std::uint32_t m_acknowledged = 0;
    std::uint64_t m_malformed = 0;
    std::vector<std::byte> m_packet;
    ByteWriter m_writer;
};

} // namespace ct
