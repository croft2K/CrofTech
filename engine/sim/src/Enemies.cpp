#include "croftech/sim/Enemies.h"

#include "LevelLogic.h"
#include "NavQueries.h"

#include "croftech/core/Angle.h"
#include "croftech/core/Profile.h"
#include "croftech/nav/NavQuery.h"
#include "croftech/sim/CollisionScene.h"
#include "croftech/sim/Damage.h"
#include "croftech/sim/PlayerMovement.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <span>

namespace ct {
namespace {

// Paths are found again four times a second, each enemy on a tick of its own, so a crowd doesn't
// all search at once.
constexpr TickIndex kPathInterval = kTickRate / 4;
constexpr TickIndex kPathStagger = 8;
// Close enough to a corner of its path to head for the next.
constexpr float kCornerReached = 0.3f;
// Waits for a lift this far from where the ride starts, rather than walking into its shaft.
constexpr float kLiftWaitDistance = 2.0f;
// A lift's top within this of the stop counts as there.
constexpr float kLiftThere = 0.25f;
// After losing sight of its target it goes to where it was last seen, and gives up this long
// after it last saw it.
constexpr TickIndex kGiveUpTicks = 5 * kTickRate;
// It starts a swing only when facing within this of the target (half a turn is 32768).
constexpr std::int32_t kSwingAim = 8192;
// A swing still lands on a target this much further than its reach when it lands.
constexpr float kSwingSlack = 0.3f;
// How hard a swing throws upwards, as a share of its knockback.
constexpr float kSwingLift = 0.3f;
// Doors and lifts at most that a path search avoids.
constexpr std::size_t kMaxBlockedMovers = 64;

TickIndex secondsToTicks(float seconds) {
    return static_cast<TickIndex>(std::max(seconds, 0.0f) * static_cast<float>(kTickRate) + 0.5f);
}

Vec3 flat(Vec3 v) { return {v.x, v.y, 0.0f}; }

// The yaw turned towards wanted by at most step angle units.
std::uint16_t turnTowards(std::uint16_t yaw, std::uint16_t wanted, std::int32_t step) {
    const auto difference = static_cast<std::int16_t>(static_cast<std::uint16_t>(wanted - yaw));
    const std::int32_t turn = std::clamp<std::int32_t>(difference, -step, step);
    return static_cast<std::uint16_t>(yaw + turn);
}

std::int32_t angleBetween(std::uint16_t a, std::uint16_t b) {
    const auto difference = static_cast<std::int16_t>(static_cast<std::uint16_t>(a - b));
    return std::abs(static_cast<std::int32_t>(difference));
}

// The gap between two boxes across the ground, or 0 where they overlap; and whether they overlap
// in height.
float groundGap(Vec3 a, Vec3 aHalf, Vec3 b, Vec3 bHalf, bool& level) {
    const Vec3 d = b - a;
    level = std::fabs(d.z) <= aHalf.z + bHalf.z;
    const float x = std::max(std::fabs(d.x) - aHalf.x - bHalf.x, 0.0f);
    const float y = std::max(std::fabs(d.y) - aHalf.y - bHalf.y, 0.0f);
    return std::sqrt(x * x + y * y);
}

bool isLivingPlayer(const World& world, EntityId id) {
    const Health* health = world.healths.get(id);
    return world.playerControls.has(id) && world.transforms.has(id) && health != nullptr &&
           !isDead(*health);
}

// Whether an enemy sees the target: within its sight range, inside its view cone unless it is
// already hunting, and with nothing solid between its eye and the target's eye or middle.
bool canSee(const CollisionScene& scene, const World& world, const GameSettings& settings,
            const EnemySettings& type, const Transform& self, EntityId target, bool hunting) {
    const Transform* other = world.transforms.get(target);
    if (other == nullptr) {
        return false;
    }
    const Vec3 eye = eyePosition(type.movement, self);
    const Vec3 targetEye = eyePosition(movementOf(world, target, settings), *other);
    const Vec3 toTarget = targetEye - eye;
    if (dot(toTarget, toTarget) > type.sightRange * type.sightRange) {
        return false;
    }
    if (!hunting && type.sightAngle < 360.0f) {
        const Vec3 across = flat(toTarget);
        const float distance = length(across);
        const SinCos facing = sinCos(self.yaw);
        const auto halfAngle = static_cast<std::uint16_t>(type.sightAngle * 0.5f / 360.0f *
                                                          static_cast<float>(kAngleUnitsPerTurn));
        if (distance > 0.0f &&
            (across.x * facing.cos + across.y * facing.sin) < sinCos(halfAngle).cos * distance) {
            return false;
        }
    }
    return scene.traceBox(eye, targetEye, {}).fraction >= 1.0f ||
           scene.traceBox(eye, other->position, {}).fraction >= 1.0f;
}

// The angle units a type turns in one tick.
std::int32_t turnStep(const EnemySettings& type) {
    return std::max(
        static_cast<std::int32_t>(type.turnSpeed / 360.0f * static_cast<float>(kAngleUnitsPerTurn) *
                                  kTickSeconds),
        1);
}

} // namespace

const MovementSettings& movementOf(const World& world, EntityId id, const GameSettings& settings) {
    if (const Enemy* enemy = world.enemies.get(id);
        enemy != nullptr && enemy->type < settings.enemies.size()) {
        return settings.enemies[enemy->type].movement;
    }
    return settings.movement;
}

void makeEnemy(World& world, EntityId id, const GameSettings& settings, std::uint32_t type,
               Vec3 feet, std::uint16_t yaw) {
    const EnemySettings& enemySettings = settings.enemies[type];
    const Vec3 centre =
        feet +
        Vec3{0.0f, 0.0f, enemySettings.movement.halfExtents.z + CollisionWorld::kSurfaceEpsilon};
    world.transforms.add(id, {centre, yaw, 0});
    world.playerMotions.add(id, {});
    Health health;
    health.health = enemySettings.health;
    world.healths.add(id, health);
    Enemy enemy;
    enemy.type = type;
    enemy.target = {};
    enemy.waypointMover = kNoNavMover;
    enemy.random = (id.index << 16) ^ type;
    world.enemies.add(id, enemy);
}

EntityId LevelLogic::nearestPlayer(Vec3 point) const {
    const World& world = m_state.world;
    EntityId nearest;
    float best = std::numeric_limits<float>::max();
    for (const EntityId player : world.playerControls.entities()) {
        if (!isLivingPlayer(world, player)) {
            continue;
        }
        const Vec3 offset = world.transforms.get(player)->position - point;
        if (dot(offset, offset) < best) {
            best = dot(offset, offset);
            nearest = player;
        }
    }
    return nearest;
}

void LevelLogic::alertEnemy(std::uint32_t index, Enemy& enemy, EntityId target) {
    const World& world = m_state.world;
    const EnemyState state = enemyState(enemy);
    if (!isLivingPlayer(world, target) || (state != EnemyState::Idle && state != EnemyState::Lost &&
                                           isLivingPlayer(world, enemy.target))) {
        return;
    }
    enemy.target = target;
    enemy.lastSeen = world.transforms.get(target)->position;
    enemy.seenTick = m_state.tick;
    // From lost it goes straight back to the chase; from idle it first has to react.
    enemy.state = static_cast<std::uint32_t>(state == EnemyState::Lost ? EnemyState::Chase
                                                                       : EnemyState::Alerted);
    enemy.stateTick = m_state.tick;
    enemy.pathTick = m_state.tick;
    if ((enemy.flags & kEnemyAlertFired) == 0) {
        enemy.flags |= kEnemyAlertFired;
        fire(index, LogicEvent::OnAlert, target);
    }
}

void LevelLogic::runEnemies(const CollisionScene& scene, const GameSettings& settings,
                            NavQueries& queries) {
    CT_PROFILE_ZONE();
    World& world = m_state.world;
    const TickIndex tick = m_state.tick;
    std::array<EntityId, kMaxEntities> gone{};
    std::size_t goneCount = 0;

    // Doors and lifts no enemy can work: those set going only by inputs (unless they loop), and
    // not already open. Paths go round them.
    std::array<std::uint32_t, kMaxBlockedMovers> blocked{};
    std::size_t blockedCount = 0;
    for (std::size_t i = 0; i < world.movers.size() && blockedCount < kMaxBlockedMovers; ++i) {
        std::uint32_t index = 0;
        const GameLevel::Entity* entity =
            levelEntityOf(world.movers.entities()[i], LogicClass::Mover, index);
        const Mover& mover = world.movers.components()[i];
        const MoverPhase phase = static_cast<MoverPhase>(mover.phase);
        if (entity != nullptr && entity->activate == MoverActivation::Input && !entity->loop &&
            phase != MoverPhase::AtEnd && phase != MoverPhase::ToEnd) {
            blocked[blockedCount++] = index;
        }
    }
    std::sort(blocked.begin(), blocked.begin() + static_cast<std::ptrdiff_t>(blockedCount));
    NavFilter filter;
    filter.blockedMovers = std::span(blocked.data(), blockedCount);

    const std::span<const EntityId> ids = world.enemies.entities();
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const EntityId id = ids[i];
        Enemy& enemy = world.enemies.components()[i];
        Transform* transform = world.transforms.get(id);
        PlayerMotion* motion = world.playerMotions.get(id);
        Health* health = world.healths.get(id);
        if (transform == nullptr || motion == nullptr || health == nullptr ||
            enemy.type >= settings.enemies.size()) {
            continue;
        }
        const EnemySettings& type = settings.enemies[enemy.type];
        const MovementSettings& movement = type.movement;
        const LevelEntityRef* ref = world.levelEntities.get(id);

        if (isDead(*health)) {
            if ((enemy.flags & kEnemyDeathFired) == 0) {
                enemy.flags |= kEnemyDeathFired;
                if (ref != nullptr) {
                    fire(ref->index, LogicEvent::OnDeath, health->attacker);
                }
            }
            if (type.corpseTime >= 0.0f &&
                tick >= health->diedTick + secondsToTicks(type.corpseTime)) {
                gone[goneCount++] = id;
                continue;
            }
            // The dead fall where they are.
            movePlayer(scene, movement, UserCmd{transform->yaw, 0, 0, 0, 0}, *transform, *motion);
            continue;
        }

        // Hurt by a player: goes after them.
        if (health->hurtTick != enemy.hurtSeen) {
            enemy.hurtSeen = health->hurtTick;
            if (ref != nullptr) {
                alertEnemy(ref->index, enemy, health->attacker);
            }
        }

        EnemyState state = enemyState(enemy);
        if (state == EnemyState::Idle) {
            // Looks for the nearest player it can see.
            EntityId seen;
            float best = std::numeric_limits<float>::max();
            for (const EntityId player : world.playerControls.entities()) {
                if (!isLivingPlayer(world, player) ||
                    !canSee(scene, world, settings, type, *transform, player, false)) {
                    continue;
                }
                const Vec3 offset = world.transforms.get(player)->position - transform->position;
                if (dot(offset, offset) < best) {
                    best = dot(offset, offset);
                    seen = player;
                }
            }
            if (seen.isValid() && ref != nullptr) {
                alertEnemy(ref->index, enemy, seen);
            }
        } else if (!isLivingPlayer(world, enemy.target)) {
            // Its target died or left: it hunts whoever else it can see, or stands down.
            enemy.target = {};
            enemy.state = static_cast<std::uint32_t>(EnemyState::Idle);
            enemy.stateTick = tick;
            enemy.flags &= ~kEnemyHasPath;
        }
        state = enemyState(enemy);

        const Vec3 feet = transform->position - Vec3{0.0f, 0.0f, movement.halfExtents.z};
        UserCmd cmd{transform->yaw, transform->pitch, 0, 0, 0};
        if (state != EnemyState::Idle) {
            const Transform& target = *world.transforms.get(enemy.target);
            const MovementSettings& targetMovement = movementOf(world, enemy.target, settings);
            const bool visible =
                canSee(scene, world, settings, type, *transform, enemy.target, true);
            if (visible) {
                enemy.lastSeen = target.position;
                enemy.seenTick = tick;
                if (state == EnemyState::Lost) {
                    state = EnemyState::Chase;
                    enemy.state = static_cast<std::uint32_t>(state);
                    enemy.stateTick = tick;
                }
            } else if (state == EnemyState::Chase) {
                state = EnemyState::Lost;
                enemy.state = static_cast<std::uint32_t>(state);
                enemy.stateTick = tick;
            } else if (state == EnemyState::Lost && tick >= enemy.seenTick + kGiveUpTicks) {
                enemy.target = {};
                enemy.state = static_cast<std::uint32_t>(EnemyState::Idle);
                enemy.stateTick = tick;
                enemy.flags &= ~kEnemyHasPath;
                movePlayer(scene, movement, cmd, *transform, *motion);
                continue;
            }

            if (state == EnemyState::Alerted &&
                tick >= enemy.stateTick + secondsToTicks(type.reactionTime)) {
                state = EnemyState::Chase;
                enemy.state = static_cast<std::uint32_t>(state);
                enemy.stateTick = tick;
                enemy.pathTick = tick;
            }

            // The way to go: along its path towards the target, or where it was last seen.
            bool level = false;
            const float gap = groundGap(transform->position, movement.halfExtents, target.position,
                                        targetMovement.halfExtents, level);
            const bool inReach = type.meleeDamage > 0 && visible && level && gap <= type.meleeRange;
            Vec3 heading;
            if (state != EnemyState::Alerted && !inReach) {
                const NavMesh& mesh = m_level.navMesh(navAgent(movement));
                const auto follow = [&](const NavCorner& corner) {
                    enemy.waypoint = corner.position;
                    enemy.waypointArea = static_cast<std::uint32_t>(corner.area);
                    enemy.waypointMover = corner.mover;
                    enemy.flags = (enemy.flags | kEnemyHasPath) & ~kEnemyRideStart;
                    if (corner.link) {
                        enemy.flags |= kEnemyRideStart;
                    }
                };
                const Vec3 across = flat(enemy.waypoint - feet);
                const bool reached = (enemy.flags & kEnemyHasPath) != 0 &&
                                     dot(across, across) < kCornerReached * kCornerReached;
                const bool riding = reached && (enemy.flags & kEnemyRideStart) != 0;
                if (riding) {
                    // On the lift where its ride starts: it stands still to be carried, and once
                    // the lift has stopped somewhere else, looks for the way on from there.
                    const GameLevel::Entity* lift = m_level.entity(enemy.waypointMover);
                    const Mover* mover = lift != nullptr ? world.movers.get(lift->id) : nullptr;
                    bool arrived = true;
                    if (lift != nullptr && lift->model != nullptr && mover != nullptr) {
                        const auto phase = static_cast<MoverPhase>(mover->phase);
                        const Aabb bounds = lift->model->bounds();
                        const Vec3 deck = Vec3{(bounds.min.x + bounds.max.x) * 0.5f,
                                               (bounds.min.y + bounds.max.y) * 0.5f, bounds.max.z} +
                                          mover->offset;
                        const Vec3 moved = deck - enemy.waypoint;
                        arrived = (phase == MoverPhase::AtStart || phase == MoverPhase::AtEnd) &&
                                  dot(moved, moved) > kLiftThere * kLiftThere;
                    }
                    if (arrived && tick >= enemy.pathTick) {
                        enemy.pathTick = tick + kPathInterval;
                        NavPath path;
                        queries.forMesh(mesh).findPath(transform->position, enemy.lastSeen, filter,
                                                       path);
                        if (path.count >= 2 && !path.corners[1].link) {
                            follow(path.corners[1]);
                        }
                    }
                } else if (tick >= enemy.pathTick || reached) {
                    NavPath path;
                    queries.forMesh(mesh).findPath(transform->position, enemy.lastSeen, filter,
                                                   path);
                    enemy.pathTick = tick + kPathInterval + id.index % kPathStagger;
                    if (path.count >= 2) {
                        follow(path.corners[1]);
                    } else {
                        enemy.flags &= ~(kEnemyHasPath | kEnemyRideStart);
                    }
                }
                if (riding) {
                    heading = {};
                } else if ((enemy.flags & kEnemyHasPath) != 0) {
                    heading = flat(enemy.waypoint - feet);
                    // A lift that isn't at this stop is waited for, short of its shaft.
                    if (enemy.waypointArea == static_cast<std::uint32_t>(NavArea::Lift)) {
                        const GameLevel::Entity* lift = m_level.entity(enemy.waypointMover);
                        const Mover* mover = lift != nullptr ? world.movers.get(lift->id) : nullptr;
                        bool there = false;
                        if (lift != nullptr && lift->model != nullptr && mover != nullptr) {
                            const auto phase = static_cast<MoverPhase>(mover->phase);
                            const float top = lift->model->bounds().max.z + mover->offset.z;
                            there = (phase == MoverPhase::AtStart || phase == MoverPhase::AtEnd) &&
                                    std::fabs(top - enemy.waypoint.z) < kLiftThere;
                        }
                        if (!there &&
                            dot(heading, heading) < kLiftWaitDistance * kLiftWaitDistance) {
                            heading = {};
                        }
                    }
                } else if (visible) {
                    // Off the mesh, or no way round: straight at the target.
                    heading = flat(target.position - transform->position);
                }
            }

            // Faces the target while it can see it, and otherwise the way it is going.
            const Vec3 look = visible ? target.position - transform->position : heading;
            if (dot(flat(look), flat(look)) > 0.0f) {
                cmd.yaw =
                    turnTowards(transform->yaw, directionToAngle(look.x, look.y), turnStep(type));
            }
            if (visible) {
                cmd.pitch = pitchToward(length(flat(look)), look.z);
            }
            if (dot(heading, heading) > 0.0f) {
                const Vec3 way = normalize(heading);
                const SinCos facing = sinCos(cmd.yaw);
                const float forward = way.x * facing.cos + way.y * facing.sin;
                const float side = way.x * facing.sin - way.y * facing.cos;
                cmd.forward = static_cast<std::int8_t>(std::lround(forward * 127.0f));
                cmd.side = static_cast<std::int8_t>(std::lround(side * 127.0f));
            }

            // Melee: a swing starts when the target is in reach and in front, and lands after
            // its wind-up if the target is still there.
            const std::uint16_t towards =
                directionToAngle(target.position.x - transform->position.x,
                                 target.position.y - transform->position.y);
            if (inReach && state == EnemyState::Chase && tick >= enemy.readyTick &&
                angleBetween(cmd.yaw, towards) <= kSwingAim) {
                state = EnemyState::Attack;
                enemy.state = static_cast<std::uint32_t>(state);
                enemy.stateTick = tick;
                enemy.attackTick = tick + secondsToTicks(type.meleeWindup);
                enemy.readyTick = tick + std::max(secondsToTicks(type.meleeInterval), 1u);
            }
            if (state == EnemyState::Attack && tick >= enemy.attackTick) {
                if (level && gap <= type.meleeRange + kSwingSlack) {
                    Vec3 away = flat(target.position - transform->position);
                    away = dot(away, away) > 0.0f ? normalize(away) : Vec3{1.0f, 0.0f, 0.0f};
                    Damage damage;
                    damage.amount = type.meleeDamage;
                    damage.kind = DamageKind::Melee;
                    damage.attacker = id;
                    damage.push = (away + Vec3{0.0f, 0.0f, kSwingLift}) * type.meleeKnockback;
                    applyDamage(m_state, settings.rules, enemy.target, damage);
                }
                enemy.state = static_cast<std::uint32_t>(EnemyState::Chase);
                enemy.stateTick = tick;
            }
        }

        movePlayer(scene, movement, cmd, *transform, *motion);
        if (transform->position.z - movement.halfExtents.z < m_level.killHeight()) {
            kill(m_state, id, DamageKind::OutOfWorld);
        }
    }
    for (std::size_t i = 0; i < goneCount; ++i) {
        world.destroy(gone[i]);
    }
    deliverQueued();
}

} // namespace ct
