#pragma once

#include "croftech/client/ClientConnection.h"
#include "croftech/net/LoopbackLink.h"
#include "croftech/server/Server.h"
#include "croftech/server/ServerThread.h"

#include <chrono>
#include <optional>
#include <span>
#include <vector>

namespace ct {

// A game played in this process: a server ticking on its own thread, and a client joined to it
// over a LoopbackLink, so everything between them goes through the bytes a network would carry
// and can be delayed and lost the way a network would. Single-player and play-in-editor both run
// this; M6 puts a socket where the link is.
class LocalGame {
public:
    using Clock = std::chrono::steady_clock;

    // spawns must not be empty.
    LocalGame(std::span<const Brush> brushes, std::vector<SpawnPoint> spawns,
              const MovementSettings& movement, const LinkConditions& conditions = {});
    ~LocalGame();
    LocalGame(const LocalGame&) = delete;
    LocalGame& operator=(const LocalGame&) = delete;

    void setConditions(const LinkConditions& conditions) { m_link.setConditions(conditions); }
    void setMovementSettings(const MovementSettings& movement);

    // Once a frame: sends the player's command and reads the snapshots that have arrived.
    void update(const UserCmd& command, Clock::time_point now);

    // Where the player's eyes are, drawn between the two newest snapshots by how long ago the
    // newest arrived. Nullopt until the server has let the player in.
    std::optional<Vec3> eyePosition(Clock::time_point now) const;
    // The newest state the client has, or null.
    const State* latestState() const { return m_client.current(); }
    EntityId player() const { return m_client.player(); }

    float serverTickMilliseconds() const { return m_thread.lastTickMilliseconds(); }
    LinkStats linkStats() const { return m_link.stats(); }
    std::uint64_t malformedPackets() const { return m_client.malformedPackets(); }

private:
    MovementSettings m_movement;
    LoopbackLink m_link;
    Server m_server;
    ServerThread m_thread;
    ClientConnection m_client;
};

} // namespace ct
