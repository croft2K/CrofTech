#include "croftech/client/LocalGame.h"

#include "croftech/sim/PlayerMovement.h"
#include "croftech/sim/StateSave.h"

#include <algorithm>
#include <memory>

namespace ct {
namespace {

constexpr float kDeadEyeHeight = 0.3f; // above the feet
constexpr float kDeathSinkSeconds = 0.4f;

} // namespace

LocalGame::LocalGame(const Level& level, const GameSettings& settings,
                     const LinkConditions& conditions, std::optional<SpawnPoint> start)
    : m_settings(settings), m_link(conditions), m_server(level, settings, start),
      m_thread(m_server), m_client(m_link.clientEnd()) {
    m_server.addConnection(m_link.serverEnd());
    m_thread.start();
}

LocalGame::~LocalGame() {
    m_client.disconnect(Clock::now());
    m_thread.stop();
}

void LocalGame::setSettings(const GameSettings& settings) {
    m_settings = settings;
    m_thread.withServer([&settings](Server& server) { server.setSettings(settings); });
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
    const Vec3 a = ct::eyePosition(m_settings.movement, *from);
    const Vec3 b = ct::eyePosition(m_settings.movement, *to);
    Vec3 eye = a + (b - a) * blend(now);
    // The dead see from the floor, sinking there over a moment.
    const Health* health = current->world.healths.get(m_client.player());
    if (health != nullptr && isDead(*health)) {
        const float since =
            (static_cast<float>(current->tick - health->diedTick) + blend(now)) * kTickSeconds;
        const float sink = std::clamp(since / kDeathSinkSeconds, 0.0f, 1.0f);
        eye.z -= (m_settings.movement.eyeHeight - kDeadEyeHeight) * sink;
    }
    return eye;
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

std::optional<Transform> LocalGame::transform(EntityId id, Clock::time_point now) const {
    const State* current = m_client.current();
    const Transform* to = current != nullptr ? current->world.transforms.get(id) : nullptr;
    if (to == nullptr) {
        return std::nullopt;
    }
    const Transform* from = m_client.previous()->world.transforms.get(id);
    if (from == nullptr) {
        return *to;
    }
    const float t = blend(now);
    const auto turn = [t](auto a, auto b) {
        using Angle = decltype(a);
        const auto difference = static_cast<std::int16_t>(static_cast<std::uint16_t>(b - a));
        return static_cast<Angle>(a + static_cast<int>(static_cast<float>(difference) * t));
    };
    Transform drawn = *to;
    drawn.position = from->position + (to->position - from->position) * t;
    drawn.yaw = turn(from->yaw, to->yaw);
    drawn.pitch = turn(from->pitch, to->pitch);
    return drawn;
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
