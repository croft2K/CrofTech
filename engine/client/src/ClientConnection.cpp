#include "croftech/client/ClientConnection.h"

#include "croftech/net/Protocol.h"
#include "croftech/sim/StateSave.h"

#include <string>
#include <utility>

namespace ct {
namespace {

constexpr auto kHelloInterval = std::chrono::milliseconds(100);
constexpr int kGoodbyeRepeats = 3; // any one of them arriving is enough

} // namespace

ClientConnection::ClientConnection(Transport& transport)
    : m_transport(transport), m_incoming(std::make_unique<State>()) {}

void ClientConnection::sendInput(const UserCmd& command, Clock::time_point now) {
    if (!joined() && now >= m_nextHello) {
        m_writer.clear();
        writeClientMessage(m_writer, HelloMessage{});
        m_transport.send(m_writer.bytes(), now);
        m_nextHello = now + kHelloInterval;
    }
    m_writer.clear();
    writeClientMessage(m_writer, InputMessage{++m_sequence, command});
    m_transport.send(m_writer.bytes(), now);
}

void ClientConnection::receive(Clock::time_point now) {
    while (m_transport.receive(m_packet, now)) {
        const std::optional<SnapshotMessage> snapshot = readSnapshot(m_packet);
        if (!snapshot) {
            ++m_malformed;
            continue;
        }
        if (m_current && (snapshot->timeline < m_timeline || (snapshot->timeline == m_timeline &&
                                                              snapshot->tick <= m_current->tick))) {
            continue; // overtaken by a newer one
        }
        std::string error;
        if (!loadState(snapshot->state, *m_incoming, error) || m_incoming->tick != snapshot->tick) {
            ++m_malformed;
            continue;
        }
        if (!m_current) {
            m_current = std::make_unique<State>(*m_incoming);
            m_previous = std::make_unique<State>(*m_incoming);
        } else if (snapshot->timeline != m_timeline) {
            *m_current = *m_incoming;
            *m_previous = *m_incoming;
        } else {
            std::swap(m_previous, m_current);
            std::swap(m_current, m_incoming);
        }
        m_player = snapshot->player;
        m_timeline = snapshot->timeline;
        m_acknowledged = snapshot->lastInputSequence;
        m_arrivedAt = now;
    }
}

void ClientConnection::disconnect(Clock::time_point now) {
    m_writer.clear();
    writeClientMessage(m_writer, GoodbyeMessage{});
    for (int i = 0; i < kGoodbyeRepeats; ++i) {
        m_transport.send(m_writer.bytes(), now);
    }
}

} // namespace ct
