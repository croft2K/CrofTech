#include "croftech/client/StepSmoothing.h"

#include "croftech/sim/Health.h"

#include <algorithm>
#include <cmath>

namespace ct {

Vec3 StepSmoothing::apply(const LocalGame& game, Vec3 eye, float seconds, Clock::time_point now) {
    const float elapsed =
        std::clamp(std::chrono::duration<float>(now - m_lastFrame).count(), 0.0f, 0.1f);
    m_lastFrame = now;
    return apply(game.latestState(), game.player(), game.timeline(), game.settings().movement, eye,
                 seconds, elapsed);
}

Vec3 StepSmoothing::apply(const State* state, EntityId player, std::uint32_t timeline,
                          const MovementSettings& movement, Vec3 eye, float seconds,
                          float elapsed) {
    const Transform* transform = state != nullptr ? state->world.transforms.get(player) : nullptr;
    const PlayerMotion* motion =
        state != nullptr ? state->world.playerMotions.get(player) : nullptr;
    const Health* health = state != nullptr ? state->world.healths.get(player) : nullptr;
    // Nothing to ease after a load, while dead, or with smoothing off.
    if (transform == nullptr || motion == nullptr || seconds <= 0.0f || !m_known ||
        timeline != m_timeline || (health != nullptr && isDead(*health))) {
        m_known = transform != nullptr && motion != nullptr;
        if (m_known) {
            m_tick = state->tick;
            m_timeline = timeline;
            m_z = transform->position.z;
            m_onGround = (motion->flags & kPlayerOnGround) != 0;
        }
        m_stepped = false;
        m_offset = 0.0f;
        m_drawnZ = eye.z;
        return eye;
    }

    const float eyeZ = eyePosition(movement, *transform).z;
    if (state->tick != m_tick) {
        const float rise = transform->position.z - m_z;
        const bool onGround = (motion->flags & kPlayerOnGround) != 0;
        m_stepped = m_onGround && onGround && std::fabs(rise) >= kMinStep &&
                    std::fabs(rise) <= movement.stepHeight + CollisionWorld::kSurfaceEpsilon;
        if (m_stepped) {
            // Keep the view where it was drawn, and let it catch up from there.
            m_offset = std::clamp(m_drawnZ - eyeZ, -movement.stepHeight, movement.stepHeight);
        }
        m_tick = state->tick;
        m_z = transform->position.z;
        m_onGround = onGround;
    }
    // About 95% of the way there after the given time.
    m_offset *= std::exp(-3.0f * std::max(elapsed, 0.0f) / seconds);
    // Drawn from the newest snapshot alone after a step, since drawing between it and the one
    // before would bring the step back.
    const float baseZ = m_stepped ? eyeZ : eye.z;
    m_drawnZ = baseZ + m_offset;
    return {eye.x, eye.y, m_drawnZ};
}

} // namespace ct
