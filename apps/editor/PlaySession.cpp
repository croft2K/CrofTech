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
constexpr auto kNoticeTime = std::chrono::seconds(2);

std::optional<SpawnPoint> spawnAt(const std::optional<Camera>& camera,
                                  const MovementSettings& movement) {
    if (!camera) {
        return std::nullopt;
    }
    return SpawnPoint{camera->position - Vec3{0.0f, 0.0f, movement.eyeHeight},
                      static_cast<std::uint16_t>(radiansToAngle(camera->yawRadians))};
}

} // namespace

std::unique_ptr<PlaySession> PlaySession::start(const Level& level,
                                                const MovementSettings& movement,
                                                const LinkConditions& conditions,
                                                const std::optional<Camera>& from,
                                                std::string& error) {
    if (from) {
        return std::make_unique<PlaySession>(level, movement, conditions, from);
    }
    std::optional<std::vector<SpawnPoint>> spawns = readSpawnPoints(level, error);
    if (!spawns) {
        return nullptr;
    }
    if (spawns->empty()) {
        error = std::format("the level has no {} to start from (Create > Player spawn)",
                            kPlayerSpawnClass);
        return nullptr;
    }
    return std::make_unique<PlaySession>(level, movement, conditions, std::nullopt);
}

PlaySession::PlaySession(const Level& level, const MovementSettings& movement,
                         const LinkConditions& conditions, const std::optional<Camera>& from)
    : m_game(level, movement, conditions, spawnAt(from, movement)),
      m_look{from ? PlayerLook{from->yawRadians, from->pitchRadians}
                  : PlayerLook{angleToRadians(m_game.level().spawns().front().yaw), 0.0f}} {}

void PlaySession::look(float countsX, float countsY) {
    m_look.turn(countsX, countsY, kSensitivity);
}

void PlaySession::update(const bool* keys, Clock::time_point now) {
    m_game.update(makeUserCmd(m_look, keys), now);
    if (std::optional<std::vector<std::byte>> checkpoint = m_game.takeCheckpoint()) {
        m_lastSave = std::move(*checkpoint);
        tell("Checkpoint", now);
    }
    // After a load, look the way the loaded player faces.
    if (m_game.timeline() != m_timeline && m_game.latestState() != nullptr) {
        m_timeline = m_game.timeline();
        if (const Transform* transform =
                m_game.latestState()->world.transforms.get(m_game.player())) {
            m_look = PlayerLook::facing(*transform);
        }
    }
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

void PlaySession::quickSave(Clock::time_point now) {
    m_lastSave = m_game.save();
    tell("Saved", now);
}

void PlaySession::quickLoad(Clock::time_point now) {
    if (m_lastSave.empty()) {
        tell("Nothing saved yet (F6 saves)", now);
        return;
    }
    std::string error;
    tell(m_game.load(m_lastSave, error) ? "Loaded" : "Can't load: " + error, now);
}

const std::string* PlaySession::notice(Clock::time_point now) const {
    return now < m_noticeUntil ? &m_notice : nullptr;
}

void PlaySession::tell(std::string text, Clock::time_point now) {
    m_notice = std::move(text);
    m_noticeUntil = now + kNoticeTime;
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
