#pragma once

#include "croftech/core/ByteStream.h"
#include "croftech/level/Level.h"
#include "croftech/net/Transport.h"
#include "croftech/sim/CollisionWorld.h"
#include "croftech/sim/Simulation.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace ct {

// The authority for one game: it owns the simulation, and clients reach it only through packets
// on their transports. A client says hello and gets a player at a spawn point; its inputs drive
// that player; after every tick each client is sent a snapshot of the state. Malformed packets
// are dropped and counted, never trusted.
//
// Not thread-safe: one thread calls everything (ServerThread runs it at the tick rate).
class Server {
public:
    // spawns must not be empty; players take them in turn.
    Server(std::span<const Brush> brushes, std::vector<SpawnPoint> spawns,
           const MovementSettings& movement);
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // The transport must outlive the server, or be removed first.
    void addConnection(Transport& transport);
    void removeConnection(Transport& transport);

    // Reads every packet that has arrived: hellos add players, inputs update their commands.
    void receive(Transport::Clock::time_point now);
    // Steps the simulation once with the latest inputs, then sends each player a snapshot.
    void tick(Transport::Clock::time_point now);

    // Applied from the next tick.
    void setMovementSettings(const MovementSettings& movement) {
        m_simulation.setMovementSettings(movement);
    }
    const State& state() const { return *m_current; }
    std::uint64_t malformedPackets() const { return m_malformed; }
    std::size_t playerCount() const;

private:
    struct Connection {
        Transport* transport = nullptr;
        std::optional<std::uint32_t> slot; // set once its hello has been answered
        EntityId player;
        std::uint32_t lastSequence = 0;
        bool anyInput = false;
        UserCmd command;           // the newest
        std::uint16_t pressed = 0; // buttons seen since the last tick, so taps aren't missed
    };

    void handlePacket(Connection& connection, std::span<const std::byte> packet);
    void join(Connection& connection);
    void leave(Connection& connection);

    std::vector<Brush> m_brushes;
    CollisionWorld m_collision;
    Simulation m_simulation;
    std::vector<SpawnPoint> m_spawns;
    std::size_t m_nextSpawn = 0;
    std::unique_ptr<State> m_current;
    std::unique_ptr<State> m_next;
    std::vector<Connection> m_connections;
    std::array<bool, kMaxPlayers> m_slotTaken{};
    std::uint64_t m_malformed = 0;
    std::vector<std::byte> m_packet; // reused for every packet received
    ByteWriter m_writer;             // reused for every packet sent
};

} // namespace ct
