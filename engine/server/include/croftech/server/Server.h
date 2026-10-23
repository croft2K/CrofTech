#pragma once

#include "croftech/core/ByteStream.h"
#include "croftech/level/Level.h"
#include "croftech/net/Transport.h"
#include "croftech/sim/GameLevel.h"
#include "croftech/sim/Simulation.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace ct {

// The authority for one game: it owns the level and the simulation, and clients reach it only
// through packets on their transports. A client says hello and gets a player at the spawn point
// furthest from the others;
// its inputs drive that player; after every tick each client is sent a snapshot of the state.
// Malformed packets are dropped and counted, never trusted.
//
// Not thread-safe: one thread calls everything (ServerThread runs it at the tick rate).
class Server {
public:
    // Players start at the start point if there is one (play from here, in the editor), and
    // otherwise at the level's spawns, so then the level must have one. Script entities run the
    // scripts given; what they print and their errors go to the log.
    Server(const Level& level, const GameSettings& settings, std::optional<SpawnPoint> start = {},
           std::span<const ScriptSource> scripts = {});
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // The transport must outlive the server, or be removed first.
    void addConnection(Transport& transport);
    void removeConnection(Transport& transport);

    // Reads every packet that has arrived: hellos add players, inputs update their commands.
    void receive(Transport::Clock::time_point now);
    // Steps the simulation once with the latest inputs, then sends each player a snapshot.
    void tick(Transport::Clock::time_point now);

    // Replaces the state, as loading a save does. Each joined client takes over the player with
    // its input slot in the new state, or gets a new one at a spawn point; a client joining later
    // takes over its slot's player too. The state must be of this level (saves record which).
    void restore(const State& state);

    // Applied from the next tick.
    void setSettings(const GameSettings& settings) { m_simulation.setSettings(settings); }
    const State& state() const { return *m_current; }
    // The state as of the last checkpoint a player reached (StateSave bytes), and how many have
    // been reached, for the game to keep as a save.
    std::uint32_t checkpointCount() const { return m_checkpointCount; }
    const std::vector<std::byte>& checkpointState() const { return m_checkpointState; }
    // Never changes, so other threads may read it while the server runs.
    const GameLevel& level() const { return m_level; }
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
        std::uint8_t weapon = 0;   // the last weapon asked for since the last tick, likewise
    };

    void handlePacket(Connection& connection, std::span<const std::byte> packet);
    void join(Connection& connection);
    void leave(Connection& connection);
    EntityId spawn(std::uint32_t slot);
    EntityId playerOfSlot(std::uint32_t slot) const;

    GameLevel m_level;
    Simulation m_simulation;
    std::uint32_t m_timeline = 0; // see SnapshotMessage
    std::uint32_t m_checkpointCount = 0;
    std::vector<std::byte> m_checkpointState;
    std::unique_ptr<State> m_current;
    std::unique_ptr<State> m_next;
    std::vector<Connection> m_connections;
    std::array<bool, kMaxPlayers> m_slotTaken{};
    std::uint64_t m_malformed = 0;
    std::vector<std::byte> m_packet; // reused for every packet received
    ByteWriter m_writer;             // reused for every packet sent
};

} // namespace ct
