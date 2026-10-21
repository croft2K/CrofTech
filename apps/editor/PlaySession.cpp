#include "PlaySession.h"

#include "croftech/core/Angle.h"

#include <cmath>
#include <format>
#include <optional>
#include <utility>

namespace ct {
namespace {

// The game's defaults (client.sensitivity, client.fov).
constexpr float kSensitivity = 3.0f;
constexpr float kFovDegrees = 100.0f;

} // namespace

std::unique_ptr<PlaySession> PlaySession::start(const Level& level,
                                                const MovementSettings& movement,
                                                const LinkConditions& conditions,
                                                std::string& error) {
    std::optional<std::vector<SpawnPoint>> spawns = readSpawnPoints(level, error);
    if (!spawns) {
        return nullptr;
    }
    if (spawns->empty()) {
        error = std::format("the level has no {} to start from (Create > Player spawn)",
                            kPlayerSpawnClass);
        return nullptr;
    }
    return std::make_unique<PlaySession>(level, movement, conditions);
}

PlaySession::PlaySession(const Level& level, const MovementSettings& movement,
                         const LinkConditions& conditions)
    : m_game(level, movement, conditions),
      m_look{angleToRadians(m_game.level().spawns().front().yaw), 0.0f} {}

void PlaySession::look(float countsX, float countsY) {
    m_look.turn(countsX, countsY, kSensitivity);
}

void PlaySession::update(const bool* keys, Clock::time_point now) {
    m_game.update(makeUserCmd(m_look, keys), now);
    if (const std::optional<Vec3> eye = m_game.eyePosition(now)) {
        m_eye = *eye;
    }
    if (const State* latest = m_game.latestState()) {
        if (const PlayerMotion* motion = latest->world.playerMotions.get(m_game.player())) {
            m_speed = std::sqrt(motion->velocity.x * motion->velocity.x +
                                motion->velocity.y * motion->velocity.y);
        }
    }
}

Camera PlaySession::camera() const {
    Camera camera;
    camera.position = m_eye;
    camera.yawRadians = m_look.yaw;
    camera.pitchRadians = m_look.pitch;
    camera.horizontalFovRadians16x9 = kFovDegrees * (kPi / 180.0f);
    return camera;
}

} // namespace ct
