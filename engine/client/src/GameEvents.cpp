#include "croftech/client/GameEvents.h"

#include "croftech/sim/CollisionScene.h"
#include "croftech/sim/Enemies.h"
#include "croftech/sim/Firing.h"
#include "croftech/sim/Health.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace ct {
namespace {

// Rising faster than this when leaving the ground is a jump, not walking off a ledge.
constexpr float kMinJumpSpeed = 1.0f;

GameEvent makeEvent(GameEventKind kind, EntityId entity, Vec3 position, float strength = 0.0f) {
    GameEvent event;
    event.kind = kind;
    event.entity = entity;
    event.position = position;
    event.strength = strength;
    return event;
}

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
                        const GameSettings& settings, std::vector<GameEvent>& out) {
    if (state == nullptr) {
        m_known = false;
        return;
    }
    if (m_known && timeline == m_timeline && state->tick == m_last->tick) {
        return;
    }
    if (m_known && timeline == m_timeline && state->tick > m_last->tick) {
        compare(*state, level, settings, out);
    } else {
        m_walked.fill(0.0f);
    }
    *m_last = *state;
    m_known = true;
    m_timeline = timeline;
}

void GameEvents::compare(const State& state, const GameLevel& level, const GameSettings& settings,
                         std::vector<GameEvent>& out) {
    const World& now = state.world;
    const World& before = m_last->world;

    // Players and enemies: every body that moves by the player movement code.
    const std::span<const EntityId> bodies = now.playerMotions.entities();
    for (const EntityId id : bodies) {
        const MovementSettings& movement = movementOf(now, id, settings);
        const Transform* transform = now.transforms.get(id);
        const PlayerMotion* motion = now.playerMotions.get(id);
        const Transform* lastTransform = before.transforms.get(id);
        const PlayerMotion* lastMotion = before.playerMotions.get(id);
        if (transform == nullptr || motion == nullptr) {
            continue;
        }
        const Vec3 feet = transform->position - Vec3{0.0f, 0.0f, movement.halfExtents.z};
        const auto event = [&](GameEventKind kind, float strength = 0.0f) {
            out.push_back(makeEvent(kind, id, feet, strength));
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
        GameEvent event =
            makeEvent(moving(mover) ? GameEventKind::MoverStart : GameEventKind::MoverStop, id,
                      moverCentre(level, ref->index, now));
        event.levelEntity = ref->index;
        out.push_back(event);
    }

    compareWeapons(state, level, settings, out);

    for (std::size_t i = 0; i < now.projectiles.size(); ++i) {
        const EntityId id = now.projectiles.entities()[i];
        const Projectile* last = before.projectiles.get(id);
        const Transform* transform = now.transforms.get(id);
        if (hasExploded(now.projectiles.components()[i]) && transform != nullptr &&
            (last == nullptr || !hasExploded(*last))) {
            out.push_back(makeEvent(GameEventKind::Explosion, id, transform->position));
        }
    }

    for (std::size_t i = 0; i < now.pickups.size(); ++i) {
        const EntityId id = now.pickups.entities()[i];
        const Pickup& pickup = now.pickups.components()[i];
        const Pickup* last = before.pickups.get(id);
        const LevelEntityRef* ref = now.levelEntities.get(id);
        const GameLevel::Entity* entity = ref != nullptr ? level.entity(ref->index) : nullptr;
        if (last == nullptr || entity == nullptr) {
            continue;
        }
        const bool takenNow = (pickup.flags & kPickupTaken) != 0;
        const bool takenBefore = (last->flags & kPickupTaken) != 0;
        if (takenNow && !takenBefore) {
            GameEvent event = makeEvent(GameEventKind::Pickup, pickup.takenBy, entity->position);
            event.levelEntity = ref->index;
            out.push_back(event);
        } else if (!takenNow && takenBefore) {
            GameEvent event = makeEvent(GameEventKind::PickupRespawn, id, entity->position);
            event.levelEntity = ref->index;
            out.push_back(event);
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
            GameEvent event = makeEvent(GameEventKind::CheckpointReached, id, centre);
            event.levelEntity = ref->index;
            out.push_back(event);
        }
    }
}

void GameEvents::compareWeapons(const State& state, const GameLevel& level,
                                const GameSettings& settings, std::vector<GameEvent>& out) const {
    const World& now = state.world;
    const CollisionScene scene(level, now);
    for (std::size_t i = 0; i < now.weapons.size(); ++i) {
        const EntityId id = now.weapons.entities()[i];
        const Weapons& weapons = now.weapons.components()[i];
        const Weapons* last = m_last->world.weapons.get(id);
        const Transform* transform = now.transforms.get(id);
        const Health* health = now.healths.get(id);
        const Health* lastHealth = m_last->world.healths.get(id);
        const bool respawned =
            health != nullptr && lastHealth != nullptr && !isDead(*health) && isDead(*lastHealth);
        if (last == nullptr || transform == nullptr || respawned) {
            continue;
        }
        if (weapons.current != last->current) {
            GameEvent event = makeEvent(GameEventKind::WeaponSwitch, id, transform->position);
            event.weapon = weapons.current;
            out.push_back(event);
        }
        if (weapons.firedTick == last->firedTick || weapons.firedTick == 0 ||
            weapons.current >= settings.weapons.size()) {
            continue;
        }
        const WeaponSettings& weapon = settings.weapons[weapons.current];
        const Vec3 eye = eyePosition(movementOf(now, id, settings), *transform);
        GameEvent fire = makeEvent(GameEventKind::Fire, id, eye);
        fire.weapon = weapons.current;
        out.push_back(fire);
        if (weapon.projectileSpeed > 0.0f) {
            continue;
        }
        // Each pellet draws two numbers when it spreads, so the counter before the shot is known.
        const auto pellets = static_cast<std::uint32_t>(std::clamp(weapon.pellets, 1, 64));
        std::uint32_t random = weapons.random - (weapon.spread > 0.0f ? 2u * pellets : 0u);
        const float range = std::clamp(weapon.range, 0.0f, 1000.0f);
        for (std::uint32_t pellet = 0; pellet < pellets; ++pellet) {
            const Vec3 direction = pelletDirection(*transform, weapon.spread, random);
            const ShotHit hit = traceShot(scene, now, settings, eye, eye + direction * range, id);
            if (hit.fraction >= 1.0f) {
                continue;
            }
            GameEvent impact = makeEvent(GameEventKind::Impact, id, hit.position,
                                         hit.target.isValid() ? 1.0f : 0.0f);
            impact.weapon = weapons.current;
            impact.origin = eye;
            out.push_back(impact);
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
