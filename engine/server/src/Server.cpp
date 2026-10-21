#include "croftech/server/Server.h"

#include "croftech/core/Assert.h"
#include "croftech/core/Profile.h"
#include "croftech/net/Protocol.h"

#include <algorithm>
#include <type_traits>
#include <variant>

namespace ct {

Server::Server(const Level& level, const MovementSettings& movement)
    : m_level(level), m_simulation(m_level, movement),
      m_current(std::make_unique<State>(m_level.startState())), m_next(std::make_unique<State>()) {
    CT_ASSERT(!m_level.spawns().empty());
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
    const SpawnPoint& spawn = m_level.spawns()[m_nextSpawn++ % m_level.spawns().size()];
    const EntityId player =
        spawnPlayer(m_current->world, m_simulation.movementSettings(), spawn, slot);
    if (!player.isValid()) {
        return;
    }
    m_slotTaken[slot] = true;
    connection.slot = slot;
    connection.player = player;
    connection.anyInput = false;
    connection.command = {};
    connection.pressed = 0;
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

void Server::tick(Transport::Clock::time_point now) {
    CT_PROFILE_ZONE();
    InputSet inputs;
    for (Connection& connection : m_connections) {
        if (connection.slot) {
            UserCmd command = connection.command;
            command.buttons = static_cast<std::uint16_t>(command.buttons | connection.pressed);
            connection.pressed = 0;
            inputs.commands[*connection.slot] = command;
        }
    }
    m_simulation.step(*m_current, inputs, *m_next);
    std::swap(m_current, m_next);

    for (Connection& connection : m_connections) {
        if (connection.slot) {
            m_writer.clear();
            writeSnapshot(m_writer, connection.lastSequence, connection.player, *m_current);
            connection.transport->send(m_writer.bytes(), now);
        }
    }
}

} // namespace ct
