#include "croftech/server/Server.h"

#include "croftech/core/Assert.h"
#include "croftech/core/Profile.h"
#include "croftech/net/Protocol.h"
#include "croftech/sim/Firing.h"
#include "croftech/sim/StateSave.h"

#include <algorithm>
#include <array>
#include <type_traits>
#include <variant>

namespace ct {

Server::Server(const Level& level, const GameSettings& settings, std::optional<SpawnPoint> start)
    : m_level(level, start), m_simulation(m_level, settings),
      m_current(std::make_unique<State>(m_level.startState())), m_next(std::make_unique<State>()) {
    CT_ASSERT(!m_level.spawns().empty());
}

EntityId Server::spawn(std::uint32_t slot) {
    const SpawnPoint* spawn = chooseSpawnPoint(m_level, m_current->world);
    CT_ASSERT(spawn != nullptr);
    return spawnPlayer(*m_current, m_simulation.settings(), *spawn, slot);
}

void Server::addConnection(Transport& transport) {
    Connection connection;
    connection.transport = &transport;
    m_connections.push_back(connection);
}

void Server::removeConnection(Transport& transport) {
    for (Connection& connection : m_connections) {
        if (connection.transport == &transport) {
            leave(connection);
        }
    }
    std::erase_if(m_connections,
                  [&](const Connection& connection) { return connection.transport == &transport; });
}

std::size_t Server::playerCount() const {
    return static_cast<std::size_t>(
        std::count_if(m_connections.begin(), m_connections.end(),
                      [](const Connection& connection) { return connection.slot.has_value(); }));
}

void Server::receive(Transport::Clock::time_point now) {
    for (Connection& connection : m_connections) {
        while (connection.transport->receive(m_packet, now)) {
            handlePacket(connection, m_packet);
        }
    }
}

void Server::handlePacket(Connection& connection, std::span<const std::byte> packet) {
    const std::optional<ClientMessage> message = readClientMessage(packet);
    if (!message) {
        ++m_malformed;
        return;
    }
    std::visit(
        [&](const auto& m) {
            using T = std::remove_cvref_t<decltype(m)>;
            if constexpr (std::is_same_v<T, HelloMessage>) {
                if (m.protocolVersion != kProtocolVersion) {
                    ++m_malformed;
                } else if (!connection.slot) {
                    join(connection);
                }
            } else if constexpr (std::is_same_v<T, InputMessage>) {
                // Inputs before joining, and ones overtaken by a newer one, are ignored.
                if (!connection.slot ||
                    (connection.anyInput && m.sequence <= connection.lastSequence)) {
                    return;
                }
                connection.anyInput = true;
                connection.lastSequence = m.sequence;
                connection.command = m.command;
                connection.pressed =
                    static_cast<std::uint16_t>(connection.pressed | m.command.buttons);
                if (m.command.weapon != 0) {
                    connection.weapon = m.command.weapon;
                }
            } else {
                leave(connection);
            }
        },
        *message);
}

void Server::join(Connection& connection) {
    const auto free = std::find(m_slotTaken.begin(), m_slotTaken.end(), false);
    if (free == m_slotTaken.end()) {
        return; // full; the client keeps asking
    }
    const auto slot = static_cast<std::uint32_t>(free - m_slotTaken.begin());
    // A loaded save may already have this slot's player, waiting for someone to take it over.
    EntityId player = playerOfSlot(slot);
    if (!player.isValid()) {
        player = spawn(slot);
    }
    if (!player.isValid()) {
        return;
    }
    m_slotTaken[slot] = true;
    connection.slot = slot;
    connection.player = player;
    connection.anyInput = false;
    connection.command = {};
    connection.pressed = 0;
    connection.weapon = 0;
}

void Server::leave(Connection& connection) {
    if (!connection.slot) {
        return;
    }
    m_current->world.destroy(connection.player);
    m_slotTaken[*connection.slot] = false;
    connection.slot.reset();
    connection.player = {};
}

void Server::restore(const State& state) {
    *m_current = state;
    ++m_timeline;
    World& world = m_current->world;

    // One whole player per slot stays; any others (a second for a slot, a slot out of range, a
    // player missing components) are removed. Saves from before players had health or weapons
    // get them.
    std::array<bool, kMaxPlayers> kept{};
    std::array<EntityId, kMaxEntities> unwanted{};
    std::size_t unwantedCount = 0;
    const std::span<const PlayerControl> controls = world.playerControls.components();
    for (std::size_t i = 0; i < controls.size(); ++i) {
        const EntityId id = world.playerControls.entities()[i];
        const std::uint32_t slot = controls[i].inputSlot;
        const bool whole = world.transforms.has(id) && world.playerMotions.has(id);
        if (slot < kMaxPlayers && whole && !kept[slot]) {
            kept[slot] = true;
            if (!world.healths.has(id)) {
                Health health;
                health.health = m_simulation.settings().rules.startHealth;
                health.armour = m_simulation.settings().rules.startArmour;
                world.healths.add(id, health);
            }
            if (!world.weapons.has(id)) {
                world.weapons.add(id,
                                  startingWeapons(m_simulation.settings(), slot, m_current->tick));
            }
        } else {
            unwanted[unwantedCount++] = id;
        }
    }
    for (std::size_t i = 0; i < unwantedCount; ++i) {
        world.destroy(unwanted[i]);
    }
    for (Connection& connection : m_connections) {
        if (!connection.slot) {
            continue;
        }
        connection.player = playerOfSlot(*connection.slot);
        if (!connection.player.isValid()) {
            connection.player = spawn(*connection.slot);
        }
    }
}

EntityId Server::playerOfSlot(std::uint32_t slot) const {
    const World& world = m_current->world;
    const std::span<const PlayerControl> controls = world.playerControls.components();
    for (std::size_t i = 0; i < controls.size(); ++i) {
        if (controls[i].inputSlot == slot) {
            return world.playerControls.entities()[i];
        }
    }
    return {};
}

void Server::tick(Transport::Clock::time_point now) {
    CT_PROFILE_ZONE();
    InputSet inputs;
    for (Connection& connection : m_connections) {
        if (connection.slot) {
            UserCmd command = connection.command;
            command.buttons = static_cast<std::uint16_t>(command.buttons | connection.pressed);
            command.weapon = connection.weapon;
            connection.pressed = 0;
            connection.weapon = 0;
            inputs.commands[*connection.slot] = command;
        }
    }
    m_simulation.step(*m_current, inputs, *m_next);
    std::swap(m_current, m_next);
    for (const Checkpoint& checkpoint : m_current->world.checkpoints.components()) {
        if ((checkpoint.flags & kCheckpointReached) != 0 &&
            checkpoint.reachedTick == m_current->tick) {
            m_checkpointState = saveState(*m_current);
            ++m_checkpointCount;
            break;
        }
    }

    for (Connection& connection : m_connections) {
        if (connection.slot) {
            m_writer.clear();
            writeSnapshot(m_writer, m_timeline, connection.lastSequence, connection.player,
                          *m_current);
            connection.transport->send(m_writer.bytes(), now);
        }
    }
}

} // namespace ct
