#include "croftech/client/LocalGame.h"

#include "croftech/sim/PlayerMovement.h"
#include "croftech/sim/StateSave.h"

#include <algorithm>
#include <memory>

namespace ct {

LocalGame::LocalGame(const Level& level, const MovementSettings& movement,
                     const LinkConditions& conditions, std::optional<SpawnPoint> start)
    : m_movement(movement), m_link(conditions), m_server(level, movement, start),
      m_thread(m_server), m_client(m_link.clientEnd()) {
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
    const Vec3 a = ct::eyePosition(m_movement, *from);
    const Vec3 b = ct::eyePosition(m_movement, *to);
    return a + (b - a) * blend(now);
}

Vec3 LocalGame::moverOffset(std::uint32_t levelEntity, Clock::time_point now) const {
    const GameLevel::Entity* entity = level().entity(levelEntity);
    const State* current = m_client.current();
    if (entity == nullptr || current == nullptr) {
        return {};
    }
    const Mover* to = current->world.movers.get(entity->id);
    if (to == nullptr) {
        return {};
    }
    const Mover* from = m_client.previous()->world.movers.get(entity->id);
    if (from == nullptr) {
        from = to;
    }
    return from->offset + (to->offset - from->offset) * blend(now);
}

std::vector<std::byte> LocalGame::save() {
    std::vector<std::byte> bytes;
    m_thread.withServer([&bytes](Server& server) { bytes = saveState(server.state()); });
    return bytes;
}

bool LocalGame::load(std::span<const std::byte> state, std::string& error) {
    auto loaded = std::make_unique<State>();
    if (!loadState(state, *loaded, error)) {
        return false;
    }
    m_thread.withServer([&loaded](Server& server) { server.restore(*loaded); });
    return true;
}

std::optional<std::vector<std::byte>> LocalGame::takeCheckpoint() {
    std::optional<std::vector<std::byte>> checkpoint;
    m_thread.withServer([&](Server& server) {
        if (server.checkpointCount() != m_checkpointsTaken) {
            m_checkpointsTaken = server.checkpointCount();
            checkpoint = server.checkpointState();
        }
    });
    return checkpoint;
}

float LocalGame::blend(Clock::time_point now) const {
    return std::clamp(std::chrono::duration<float>(now - m_client.currentArrivedAt()).count() /
                          kTickSeconds,
                      0.0f, 1.0f);
}

} // namespace ct
