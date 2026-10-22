#include "PlaySession.h"

#include "croftech/core/Angle.h"

#include <cmath>
#include <format>
#include <optional>
#include <utility>

namespace ct {
namespace {

// The game's defaults (client.sensitivity, client.fov, client.step_smoothing).
constexpr float kSensitivity = 3.0f;
constexpr float kFovDegrees = 100.0f;
constexpr float kStepSmoothingSeconds = 0.1f;
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

std::unique_ptr<PlaySession> PlaySession::start(const Level& level, const GameSettings& settings,
                                                const LinkConditions& conditions,
                                                const std::optional<Camera>& from,
                                                std::string& error) {
    if (from) {
        return std::make_unique<PlaySession>(level, settings, conditions, from);
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
    return std::make_unique<PlaySession>(level, settings, conditions, std::nullopt);
}

PlaySession::PlaySession(const Level& level, const GameSettings& settings,
                         const LinkConditions& conditions, const std::optional<Camera>& from)
    : m_settings(settings), m_game(level, settings, conditions, spawnAt(from, settings.movement)),
      m_look{from ? PlayerLook{from->yawRadians, from->pitchRadians}
                  : PlayerLook{angleToRadians(m_game.level().spawns().front().yaw), 0.0f}} {}

void PlaySession::look(float countsX, float countsY) {
    m_look.turn(countsX, countsY, kSensitivity);
}

bool PlaySession::handleWeaponEvent(const SDL_Event& event, Clock::time_point now) {
    return m_weaponSelect.handleEvent(event, m_game.latestState(), m_game.player(), m_settings,
                                      now);
}

void PlaySession::update(const bool* keys, Clock::time_point now) {
    UserCmd cmd = makeUserCmd(m_look, keys);
    m_weaponSelect.apply(cmd, now);
    m_game.update(cmd, now);
    if (std::optional<std::vector<std::byte>> checkpoint = m_game.takeCheckpoint()) {
        m_lastSave = std::move(*checkpoint);
        tell("Checkpoint", now);
    }
    // After a load or a respawn, look the way the player faces.
    const bool respawned = m_hud.update(m_game.latestState(), m_game.player(), m_settings, now);
    if ((m_game.timeline() != m_timeline || respawned) && m_game.latestState() != nullptr) {
        m_timeline = m_game.timeline();
        if (const Transform* transform =
                m_game.latestState()->world.transforms.get(m_game.player())) {
            m_look = PlayerLook::facing(*transform);
        }
    }
    if (const std::optional<Vec3> eye = m_game.eyePosition(now)) {
        m_eye = m_stepSmoothing.apply(m_game, *eye, kStepSmoothingSeconds, now);
    }
    m_events.clear();
    m_gameEvents.update(m_game.latestState(), m_game.timeline(), m_game.level(), m_settings,
                        m_events);
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
