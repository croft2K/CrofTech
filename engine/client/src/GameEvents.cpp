#include "croftech/client/GameEvents.h"

#include "croftech/sim/Health.h"

#include <cmath>
#include <cstddef>

namespace ct {
namespace {

// Rising faster than this when leaving the ground is a jump, not walking off a ledge.
constexpr float kMinJumpSpeed = 1.0f;

bool onGround(const PlayerMotion& motion) { return (motion.flags & kPlayerOnGround) != 0; }

bool moving(const Mover& mover) {
    const auto phase = static_cast<MoverPhase>(mover.phase);
    return (phase == MoverPhase::ToEnd || phase == MoverPhase::ToStart) &&
           (mover.flags & kMoverStopped) == 0;
}

} // namespace

GameEvents::GameEvents() : m_last(std::make_unique<State>()) {}

GameEvents::~GameEvents() = default;

void GameEvents::update(const State* state, std::uint32_t timeline, const GameLevel& level,
                        const MovementSettings& movement, std::vector<GameEvent>& out) {
    if (state == nullptr) {
        m_known = false;
        return;
    }
    if (m_known && timeline == m_timeline && state->tick == m_last->tick) {
        return;
    }
    if (m_known && timeline == m_timeline && state->tick > m_last->tick) {
        compare(*state, level, movement, out);
    } else {
        m_walked.fill(0.0f);
    }
    *m_last = *state;
    m_known = true;
    m_timeline = timeline;
}

void GameEvents::compare(const State& state, const GameLevel& level,
                         const MovementSettings& movement, std::vector<GameEvent>& out) {
    const World& now = state.world;
    const World& before = m_last->world;

    const std::span<const EntityId> players = now.playerControls.entities();
    for (const EntityId id : players) {
        const Transform* transform = now.transforms.get(id);
        const PlayerMotion* motion = now.playerMotions.get(id);
        const Transform* lastTransform = before.transforms.get(id);
        const PlayerMotion* lastMotion = before.playerMotions.get(id);
        if (transform == nullptr || motion == nullptr) {
            continue;
        }
        const Vec3 feet = transform->position - Vec3{0.0f, 0.0f, movement.halfExtents.z};
        const auto event = [&](GameEventKind kind, float strength = 0.0f) {
            out.push_back({kind, id, feet, strength, 0});
        };
        const Health* health = now.healths.get(id);
        const Health* lastHealth = before.healths.get(id);
        const bool dead = health != nullptr && isDead(*health);
        const bool wasDead = lastHealth != nullptr && isDead(*lastHealth);
        if (lastHealth != nullptr && health != nullptr) {
            if (dead && !wasDead) {
                event(GameEventKind::Death);
            } else if (!dead && wasDead) {
                event(GameEventKind::Respawn);
                m_walked[id.index] = 0.0f;
                continue; // the move to the spawn point is no jump or step
            } else if (!dead && health->hurtTick != lastHealth->hurtTick &&
                       health->health < lastHealth->health) {
                event(GameEventKind::Hurt, static_cast<float>(lastHealth->health - health->health));
            }
        }
        if (dead || lastTransform == nullptr || lastMotion == nullptr) {
            continue;
        }
        if (onGround(*lastMotion) && !onGround(*motion) && motion->velocity.z > kMinJumpSpeed) {
            event(GameEventKind::Jump);
        } else if (!onGround(*lastMotion) && onGround(*motion) &&
                   -lastMotion->velocity.z >= kMinLandingSpeed) {
            event(GameEventKind::Land, -lastMotion->velocity.z);
            m_walked[id.index] = 0.0f;
        }
        if (onGround(*motion) && onGround(*lastMotion)) {
            const Vec3 step = transform->position - lastTransform->position;
            m_walked[id.index] += std::sqrt(step.x * step.x + step.y * step.y);
            if (m_walked[id.index] >= kStride) {
                m_walked[id.index] = std::fmod(m_walked[id.index], kStride);
                event(GameEventKind::Footstep);
            }
        }
    }

    for (std::size_t i = 0; i < now.movers.size(); ++i) {
        const EntityId id = now.movers.entities()[i];
        const Mover& mover = now.movers.components()[i];
        const Mover* last = before.movers.get(id);
        const LevelEntityRef* ref = now.levelEntities.get(id);
        if (last == nullptr || ref == nullptr || moving(mover) == moving(*last)) {
            continue;
        }
        out.push_back({moving(mover) ? GameEventKind::MoverStart : GameEventKind::MoverStop, id,
                       moverCentre(level, ref->index, now), 0.0f, ref->index});
    }

    for (std::size_t i = 0; i < now.projectiles.size(); ++i) {
        const EntityId id = now.projectiles.entities()[i];
        const Projectile* last = before.projectiles.get(id);
        const Transform* transform = now.transforms.get(id);
        if (hasExploded(now.projectiles.components()[i]) && transform != nullptr &&
            (last == nullptr || !hasExploded(*last))) {
            out.push_back({GameEventKind::Explosion, id, transform->position, 0.0f, 0});
        }
    }

    for (std::size_t i = 0; i < now.checkpoints.size(); ++i) {
        const EntityId id = now.checkpoints.entities()[i];
        const Checkpoint& checkpoint = now.checkpoints.components()[i];
        const Checkpoint* last = before.checkpoints.get(id);
        const LevelEntityRef* ref = now.levelEntities.get(id);
        if (last != nullptr && ref != nullptr && (checkpoint.flags & kCheckpointReached) != 0 &&
            (last->flags & kCheckpointReached) == 0) {
            const GameLevel::Entity* entity = level.entity(ref->index);
            Vec3 centre;
            if (entity != nullptr && entity->model != nullptr) {
                const Aabb bounds = entity->model->bounds();
                centre = (bounds.min + bounds.max) * 0.5f;
            }
            out.push_back({GameEventKind::CheckpointReached, id, centre, 0.0f, ref->index});
        }
    }
}

Vec3 moverCentre(const GameLevel& level, std::uint32_t levelEntity, const World& world) {
    const GameLevel::Entity* entity = level.entity(levelEntity);
    if (entity == nullptr || entity->model == nullptr) {
        return {};
    }
    const Aabb bounds = entity->model->bounds();
    const Mover* mover = world.movers.get(entity->id);
    return (bounds.min + bounds.max) * 0.5f + (mover != nullptr ? mover->offset : Vec3{});
}

} // namespace ct
