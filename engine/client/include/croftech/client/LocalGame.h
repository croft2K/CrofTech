#pragma once

#include "croftech/client/ClientConnection.h"
#include "croftech/net/LoopbackLink.h"
#include "croftech/server/Server.h"
#include "croftech/server/ServerThread.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ct {

// A game played in this process: a server ticking on its own thread, and a client joined to it
// over a LoopbackLink, so everything between them goes through the bytes a network would carry
// and can be delayed and lost the way a network would. Single-player and play-in-editor both run
// this; M6 puts a socket where the link is.
class LocalGame {
public:
    using Clock = std::chrono::steady_clock;

    // The player starts at the start point if given, otherwise at the level's player spawn.
    LocalGame(const Level& level, const GameSettings& settings,
              const LinkConditions& conditions = {}, std::optional<SpawnPoint> start = {});
    ~LocalGame();
    LocalGame(const LocalGame&) = delete;
    LocalGame& operator=(const LocalGame&) = delete;

    void setConditions(const LinkConditions& conditions) { m_link.setConditions(conditions); }
    void setSettings(const GameSettings& settings);
    const GameSettings& settings() const { return m_settings; }

    // Once a frame: sends the player's command and reads the snapshots that have arrived.
    void update(const UserCmd& command, Clock::time_point now);

    // Where the player's eyes are, drawn between the two newest snapshots by how long ago the
    // newest arrived; they sink to the floor when the player dies. Nullopt until the server has
    // let the player in.
    std::optional<Vec3> eyePosition(Clock::time_point now) const;
    // How far the mover made from the level entity has moved, drawn between snapshots the same
    // way; zero until known.
    Vec3 moverOffset(std::uint32_t levelEntity, Clock::time_point now) const;
    // Where an entity is, drawn between snapshots the same way (turning the short way round);
    // nullopt if the newest snapshot has no transform for it.
    std::optional<Transform> transform(EntityId id, Clock::time_point now) const;
    // The newest state the client has, or null.
    const State* latestState() const { return m_client.current(); }
    EntityId player() const { return m_client.player(); }
    // Changes once the client sees a loaded state, when its view should take the loaded angles.
    std::uint32_t timeline() const { return m_client.timeline(); }
    // The server's copy of the level, which never changes while it runs.
    const GameLevel& level() const { return m_server.level(); }

    // The game's state now, as StateSave bytes, for a save game.
    std::vector<std::byte> save();
    // Replaces the game's state with a saved one, which must be of this level. Fails, changing
    // nothing, if the bytes don't load.
    bool load(std::span<const std::byte> state, std::string& error);
    // The state as of a checkpoint reached since the last call, if any, for a checkpoint save.
    std::optional<std::vector<std::byte>> takeCheckpoint();

    float serverTickMilliseconds() const { return m_thread.lastTickMilliseconds(); }
    LinkStats linkStats() const { return m_link.stats(); }
    std::uint64_t malformedPackets() const { return m_client.malformedPackets(); }

private:
    // How far from the previous snapshot towards the newest to draw, 0 to 1.
    float blend(Clock::time_point now) const;

    GameSettings m_settings;
    LoopbackLink m_link;
    Server m_server;
    ServerThread m_thread;
    ClientConnection m_client;
    std::uint32_t m_checkpointsTaken = 0;
};

} // namespace ct
