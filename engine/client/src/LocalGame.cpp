#include "croftech/client/LocalGame.h"

#include "croftech/sim/PlayerMovement.h"

#include <algorithm>

namespace ct {

LocalGame::LocalGame(const Level& level, const MovementSettings& movement,
                     const LinkConditions& conditions)
    : m_movement(movement), m_link(conditions), m_server(level, movement), m_thread(m_server),
      m_client(m_link.clientEnd()) {
    m_server.addConnection(m_link.serverEnd());
    m_thread.start();
}

LocalGame::~LocalGame() {
    m_client.disconnect(Clock::now());
    m_thread.stop();
}

void LocalGame::setMovementSettings(const MovementSettings& movement) {
    m_movement = movement;
    m_thread.withServer([&movement](Server& server) { server.setMovementSettings(movement); });
}

void LocalGame::update(const UserCmd& command, Clock::time_point now) {
    m_client.sendInput(command, now);
    m_client.receive(now);
}

std::optional<Vec3> LocalGame::eyePosition(Clock::time_point now) const {
    const State* previous = m_client.previous();
    const State* current = m_client.current();
    if (current == nullptr) {
        return std::nullopt;
    }
    const Transform* from = previous->world.transforms.get(m_client.player());
    const Transform* to = current->world.transforms.get(m_client.player());
    if (to == nullptr) {
        return std::nullopt;
    }
    if (from == nullptr) {
        from = to;
    }
    const float alpha = std::clamp(
        std::chrono::duration<float>(now - m_client.currentArrivedAt()).count() / kTickSeconds,
        0.0f, 1.0f);
    const Vec3 a = ct::eyePosition(m_movement, *from);
    const Vec3 b = ct::eyePosition(m_movement, *to);
    return a + (b - a) * alpha;
}

} // namespace ct
