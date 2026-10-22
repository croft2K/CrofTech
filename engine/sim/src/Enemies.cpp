#include "croftech/sim/Enemies.h"

#include "LevelLogic.h"
#include "NavQueries.h"

#include "croftech/core/Angle.h"
#include "croftech/core/Profile.h"
#include "croftech/core/Random.h"
#include "croftech/nav/NavQuery.h"
#include "croftech/sim/CollisionScene.h"
#include "croftech/sim/Damage.h"
#include "croftech/sim/Firing.h"
#include "croftech/sim/PlayerMovement.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <optional>
#include <span>

namespace ct {
namespace {

// Paths are found again four times a second, each enemy on a tick of its own, so a crowd doesn't
// all search at once.
constexpr TickIndex kPathInterval = kTickRate / 4;
constexpr TickIndex kPathStagger = 8;
// Close enough to a corner of its path to head for the next.
constexpr float kCornerReached = 0.3f;
// At its goal when its feet are this close, across the ground, to the end of its path: the goal,
// or the nearest the mesh lets it get.
constexpr float kGoalReached = 0.5f;
// Waits for a lift this far from where the ride starts, rather than walking into its shaft.
constexpr float kLiftWaitDistance = 2.0f;
// A lift's top within this of the stop counts as there.
constexpr float kLiftThere = 0.25f;
// For this long after losing sight of its target it still knows where the target is going (it
// hears it), so it follows round corners.
constexpr TickIndex kTrackTicks = kTickRate / 2;
// Going to where it last knew the target to be, it starts searching on getting there, or this
// long after it last saw the target.
constexpr TickIndex kGiveUpTicks = 5 * kTickRate;
// Searching, it stands looking about at each spot for between these many seconds.
constexpr float kSearchPauseMin = 0.5f;
constexpr float kSearchPauseMax = 1.5f;
// Spots it tries in one go before trying again a little later.
constexpr int kSearchTries = 4;
// Cover is looked for at this many distances in each of this many directions, and the paths to
// the nearest few spots hidden from the target are tried.
constexpr int kCoverRings = 3;
constexpr int kCoverDirections = 8;
constexpr std::size_t kCoverTries = 3;
// After coming out of cover it fights for about this many seconds before it may run again.
constexpr float kCoverRest = 2.0f;
// It starts a swing only when facing within this of the target (half a turn is 32768).
constexpr std::int32_t kSwingAim = 8192;
// A swing still lands on a target this much further than its reach when it lands.
constexpr float kSwingSlack = 0.3f;
// How hard a swing throws upwards, as a share of its knockback.
constexpr float kSwingLift = 0.3f;
// Doors and lifts at most that a path search avoids.
constexpr std::size_t kMaxBlockedMovers = 64;
// It shoots only when facing within this of the target (about 20 degrees); the shot itself is
// aimed at the target, off by the kind's aim error.
constexpr std::int32_t kShotAim = 3641;
// Players moving faster than this on the ground are heard running.
constexpr float kRunningSpeed = 3.0f;
// While sidestepping it also keeps going along its path, at this share of the step.
constexpr float kStrafeShare = 0.7f;
// It checks for floor this far to the side before stepping that way.
constexpr float kStrafeLook = 1.0f;

TickIndex secondsToTicks(float seconds) {
    return static_cast<TickIndex>(std::max(seconds, 0.0f) * static_cast<float>(kTickRate) + 0.5f);
}

Vec3 flat(Vec3 v) { return {v.x, v.y, 0.0f}; }

Vec3 feetOf(const Transform& transform, const MovementSettings& movement) {
    return transform.position - Vec3{0.0f, 0.0f, movement.halfExtents.z};
}

std::uint16_t randomYaw(std::uint32_t& random) {
    return static_cast<std::uint16_t>(randomBits(random++) >> 16);
}

TickIndex searchPause(std::uint32_t& random) {
    return secondsToTicks(kSearchPauseMin +
                          (kSearchPauseMax - kSearchPauseMin) * randomUnit(random));
}

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

float pathLength(const NavPath& path) {
    float total = 0.0f;
    for (std::uint32_t i = 1; i < path.count; ++i) {
        total += length(path.corners[i].position - path.corners[i - 1].position);
    }
    return total;
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

// The nearest living player it hears: shooting within its hearing range, or running within its
// footstep range. Walls don't stop sound.
EntityId hearPlayer(const World& world, const EnemySettings& type, Vec3 position, TickIndex tick) {
    EntityId heard;
    float best = std::numeric_limits<float>::max();
    for (const EntityId player : world.playerControls.entities()) {
        if (!isLivingPlayer(world, player)) {
            continue;
        }
        const Vec3 offset = world.transforms.get(player)->position - position;
        const float distance = dot(offset, offset);
        bool loud = false;
        // Players fire after enemies think, so a shot last tick is the newest.
        if (const Weapons* weapons = world.weapons.get(player);
            weapons != nullptr && weapons->firedTick != 0 && weapons->firedTick + 1 >= tick) {
            loud = distance <= type.hearingRange * type.hearingRange;
        }
        if (const PlayerMotion* motion = world.playerMotions.get(player);
            !loud && motion != nullptr && (motion->flags & kPlayerOnGround) != 0) {
            const Vec3 velocity = flat(motion->velocity);
            loud = dot(velocity, velocity) >= kRunningSpeed * kRunningSpeed &&
                   distance <= type.footstepRange * type.footstepRange;
        }
        if (loud && distance < best) {
            best = distance;
            heard = player;
        }
    }
    return heard;
}

// The angle units a type turns in one tick.
std::int32_t turnStep(const EnemySettings& type) {
    return std::max(
        static_cast<std::int32_t>(type.turnSpeed / 360.0f * static_cast<float>(kAngleUnitsPerTurn) *
                                  kTickSeconds),
        1);
}

// A spot of the mesh within radius of centre, chosen at random, that it can walk to from position
// without going far round; nullopt if the tries all fail.
std::optional<Vec3> findSearchSpot(NavQuery& query, const NavFilter& filter, Vec3 position,
                                   Vec3 centre, float radius, std::uint32_t& random) {
    NavPath path;
    for (int i = 0; i < kSearchTries; ++i) {
        const SinCos direction = sinCos(randomYaw(random));
        const float distance = radius * (0.25f + 0.75f * randomUnit(random));
        const Vec3 point = centre + Vec3{direction.cos, direction.sin, 0.0f} * distance;
        const std::optional<Vec3> spot = query.nearestPoint(point, filter, {2.0f, 2.0f, 3.0f});
        if (spot && query.findPath(position, *spot, filter, path) &&
            pathLength(path) <= length(centre - position) + radius * 2.0f) {
            return spot;
        }
    }
    return std::nullopt;
}

// The nearest spot within the kind's cover range where it would be hidden, eye and middle, from
// an eye at targetEye, and that it can run to without going far round. Spots much nearer the
// target than it is now aren't cover.
std::optional<Vec3> findCover(const CollisionScene& scene, NavQuery& query, const NavFilter& filter,
                              const EnemySettings& type, Vec3 position, Vec3 targetEye) {
    const MovementSettings& movement = type.movement;
    const Vec3 feet = position - Vec3{0.0f, 0.0f, movement.halfExtents.z};
    const float away = length(flat(feet - targetEye));
    struct Spot {
        Vec3 point;
        float distance = 0.0f;
    };
    std::array<Spot, static_cast<std::size_t>(kCoverRings * kCoverDirections)> spots{};
    std::size_t count = 0;
    for (int ring = 1; ring <= kCoverRings; ++ring) {
        for (int direction = 0; direction < kCoverDirections; ++direction) {
            // Each ring turned a little from the last, so the spots spread out.
            const auto angle = static_cast<std::uint16_t>(
                direction * (kAngleUnitsPerTurn / kCoverDirections) +
                ring * (kAngleUnitsPerTurn / (kCoverDirections * kCoverRings)));
            const SinCos way = sinCos(angle);
            const float distance =
                type.coverRange * static_cast<float>(ring) / static_cast<float>(kCoverRings);
            const std::optional<Vec3> spot =
                query.nearestPoint(feet + Vec3{way.cos, way.sin, 0.0f} * distance, filter,
                                   {1.0f, 1.0f, movement.stepHeight + 1.0f});
            if (!spot || length(flat(*spot - targetEye)) < away * 0.5f) {
                continue;
            }
            const Vec3 eye = *spot + Vec3{0.0f, 0.0f, movement.eyeHeight};
            const Vec3 middle = *spot + Vec3{0.0f, 0.0f, movement.halfExtents.z};
            if (scene.traceBox(targetEye, eye, {}).fraction >= 1.0f ||
                scene.traceBox(targetEye, middle, {}).fraction >= 1.0f) {
                continue;
            }
            // Kept nearest first; equals stay in the order found.
            const Spot candidate{*spot, length(*spot - feet)};
            std::size_t at = count++;
            for (; at > 0 && spots[at - 1].distance > candidate.distance; --at) {
                spots[at] = spots[at - 1];
            }
            spots[at] = candidate;
        }
    }
    NavPath path;
    for (std::size_t i = 0; i < std::min(count, kCoverTries); ++i) {
        if (query.findPath(position, spots[i].point, filter, path) &&
            pathLength(path) <= type.coverRange * 2.0f) {
            return spots[i].point;
        }
    }
    return std::nullopt;
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
    enemy.goal = feet;
    enemy.waypointMover = kNoNavMover;
    enemy.home = feet;
    enemy.homeYaw = yaw;
    enemy.lookYaw = yaw;
    enemy.random = (id.index << 16) ^ type;
    world.enemies.add(id, enemy);
    const std::uint32_t weapon = enemySettings.weaponIndex;
    if (weapon < settings.weapons.size() && weapon < kMaxWeapons) {
        // Its gun, with ammo it never runs out of.
        Weapons weapons;
        weapons.owned = 1u << weapon;
        weapons.current = weapon;
        weapons.ammo[weapon] = static_cast<std::int16_t>(kMaxAmmo);
        weapons.random = enemy.random ^ 0x9E3779B9u;
        world.weapons.add(id, weapons);
    }
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
    // One already after a living player stays after them.
    const bool available = state == EnemyState::Idle || state == EnemyState::Patrol ||
                           state == EnemyState::Lost || state == EnemyState::Search;
    if (!isLivingPlayer(world, target) || (!available && isLivingPlayer(world, enemy.target))) {
        return;
    }
    enemy.target = target;
    enemy.lastSeen = world.transforms.get(target)->position;
    enemy.seenTick = m_state.tick;
    enemy.goal = enemy.lastSeen;
    // Already hunting, it goes straight back to the chase; otherwise it first has to react.
    const bool hunting = state == EnemyState::Lost || state == EnemyState::Search;
    enemy.state = static_cast<std::uint32_t>(hunting ? EnemyState::Chase : EnemyState::Alerted);
    enemy.stateTick = m_state.tick;
    enemy.pathTick = m_state.tick;
    enemy.flags &= ~kEnemyAtGoal;
    if ((enemy.flags & kEnemyAlertFired) == 0) {
        enemy.flags |= kEnemyAlertFired;
        fire(index, LogicEvent::OnAlert, target);
    }
}

void LevelLogic::goBack(Enemy& enemy) {
    enemy.target = {};
    enemy.state = static_cast<std::uint32_t>(EnemyState::Patrol);
    enemy.stateTick = m_state.tick;
    enemy.pathTick = m_state.tick;
    enemy.flags &= ~(kEnemyHasPath | kEnemyRideStart | kEnemyAtGoal);
    // To the patrol point it was going to or standing at, or else its post.
    const GameLevel::Entity* point = m_level.entity(enemy.patrolPoint);
    enemy.goal = point != nullptr ? point->position : enemy.home;
}

void LevelLogic::startSearch(Enemy& enemy, const EnemySettings& type) {
    if (type.searchTime <= 0.0f) {
        goBack(enemy);
        return;
    }
    // It first stands where it lost the target, looking about.
    enemy.state = static_cast<std::uint32_t>(EnemyState::Search);
    enemy.stateTick = m_state.tick;
    enemy.flags = (enemy.flags | kEnemyAtGoal) & ~(kEnemyHasPath | kEnemyRideStart);
    enemy.waitTick = m_state.tick + searchPause(enemy.random);
    enemy.lookYaw = randomYaw(enemy.random);
}

bool LevelLogic::takeCover(const CollisionScene& scene, const GameSettings& settings, EntityId id,
                           Enemy& enemy, const EnemySettings& type, NavQuery& query,
                           const NavFilter& filter) {
    const World& world = m_state.world;
    const TickIndex tick = m_state.tick;
    if (type.coverChance <= 0.0f || tick < enemy.coverTick ||
        !isLivingPlayer(world, enemy.target) || randomUnit(enemy.random) >= type.coverChance) {
        return false;
    }
    const Transform& target = *world.transforms.get(enemy.target);
    const Vec3 targetEye = eyePosition(movementOf(world, enemy.target, settings), target);
    const std::optional<Vec3> spot =
        findCover(scene, query, filter, type, world.transforms.get(id)->position, targetEye);
    if (!spot) {
        return false;
    }
    enemy.state = static_cast<std::uint32_t>(EnemyState::Cover);
    enemy.stateTick = tick;
    enemy.goal = *spot;
    enemy.pathTick = tick;
    enemy.flags &= ~(kEnemyHasPath | kEnemyRideStart | kEnemyAtGoal);
    enemy.coverTick = tick + secondsToTicks(type.coverTime + kCoverRest);
    return true;
}

LevelLogic::Steering LevelLogic::steer(EntityId id, Enemy& enemy, const MovementSettings& movement,
                                       NavQuery& query, const NavFilter& filter) {
    const World& world = m_state.world;
    const TickIndex tick = m_state.tick;
    const Transform& transform = *world.transforms.get(id);
    const Vec3 feet = feetOf(transform, movement);
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
    const bool reached =
        (enemy.flags & kEnemyHasPath) != 0 && dot(across, across) < kCornerReached * kCornerReached;
    if (reached && (enemy.flags & kEnemyRideStart) != 0) {
        // On the lift where its ride starts: it stands still to be carried, and once the lift
        // has stopped somewhere else, looks for the way on from there.
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
            query.findPath(transform.position, enemy.goal, filter, path);
            if (path.count >= 2 && !path.corners[1].link) {
                follow(path.corners[1]);
            }
        }
        return {{}, true};
    }
    if (tick >= enemy.pathTick || reached) {
        NavPath path;
        query.findPath(transform.position, enemy.goal, filter, path);
        enemy.pathTick = tick + kPathInterval + id.index % kPathStagger;
        if (path.count >= 2) {
            follow(path.corners[1]);
        } else {
            enemy.flags &= ~(kEnemyHasPath | kEnemyRideStart);
        }
        // At the end of its path: the goal, or as near it as the mesh goes. Off the mesh, it has
        // nowhere to go either.
        const Vec3 left =
            path.count > 0 ? flat(path.corners[path.count - 1].position - feet) : Vec3{};
        if (dot(left, left) < kGoalReached * kGoalReached) {
            enemy.flags |= kEnemyAtGoal;
        }
    }
    if ((enemy.flags & kEnemyAtGoal) != 0 || (enemy.flags & kEnemyHasPath) == 0) {
        return {};
    }
    Steering steering{flat(enemy.waypoint - feet), false};
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
            dot(steering.heading, steering.heading) < kLiftWaitDistance * kLiftWaitDistance) {
            steering = {{}, true};
        }
    }
    return steering;
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

        // Hurt by a player: goes after them, and if it already was, may run for cover.
        bool hurtInFight = false;
        if (health->hurtTick != enemy.hurtSeen) {
            enemy.hurtSeen = health->hurtTick;
            hurtInFight = enemyState(enemy) == EnemyState::Chase;
            if (ref != nullptr) {
                alertEnemy(ref->index, enemy, health->attacker);
            }
        }

        EnemyState state = enemyState(enemy);
        const bool calm =
            state == EnemyState::Idle || state == EnemyState::Patrol || state == EnemyState::Search;
        if (calm) {
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
        }
        if (calm || state == EnemyState::Lost) {
            // Shots and running nearby wake it, or tell it where its lost target went.
            if (const EntityId heard = hearPlayer(world, type, transform->position, tick);
                heard.isValid() && (enemy.flags & kEnemyAmbush) == 0 && ref != nullptr &&
                enemyState(enemy) == state) {
                alertEnemy(ref->index, enemy, heard);
            }
        }
        state = enemyState(enemy);
        if (state != EnemyState::Idle && state != EnemyState::Patrol &&
            !isLivingPlayer(world, enemy.target)) {
            // Its target died or left: back to its post or patrol, watching for others.
            goBack(enemy);
            state = enemyState(enemy);
        }

        NavQuery& query = queries.forMesh(m_level.navMesh(navAgent(movement)));
        UserCmd cmd{transform->yaw, transform->pitch, 0, 0, 0};
        Vec3 heading;
        float pace = 1.0f; // of its running speed
        // The way it faces; with none, it turns to its look yaw.
        Vec3 look;
        bool shoot = false;

        if (state == EnemyState::Idle) {
            const GameLevel::Entity* point = m_level.entity(enemy.patrolPoint);
            if (point != nullptr && point->next != kNoEntity && tick >= enemy.waitTick) {
                // On to the next point of its patrol.
                enemy.patrolPoint = point->next;
                goBack(enemy);
                state = enemyState(enemy);
            }
        }
        if (state == EnemyState::Patrol) {
            heading = steer(id, enemy, movement, query, filter).heading;
            pace = type.speed > 0.0f ? std::min(type.walkSpeed / type.speed, 1.0f) : 1.0f;
            if ((enemy.flags & kEnemyAtGoal) != 0) {
                state = EnemyState::Idle;
                enemy.state = static_cast<std::uint32_t>(state);
                enemy.stateTick = tick;
                if (const GameLevel::Entity* point = m_level.entity(enemy.patrolPoint)) {
                    // It stands at the point facing the way it came.
                    enemy.lookYaw = transform->yaw;
                    enemy.waitTick = tick + static_cast<TickIndex>(point->waitTicks);
                    fire(enemy.patrolPoint, LogicEvent::OnArrive, id);
                } else {
                    enemy.lookYaw = enemy.homeYaw;
                }
            }
        } else if (state == EnemyState::Search) {
            if (tick >= enemy.stateTick + secondsToTicks(type.searchTime)) {
                goBack(enemy);
            } else if ((enemy.flags & kEnemyAtGoal) != 0) {
                if (tick >= enemy.waitTick) {
                    const std::optional<Vec3> spot =
                        findSearchSpot(query, filter, transform->position, enemy.lastSeen,
                                       type.searchRadius, enemy.random);
                    if (spot) {
                        enemy.goal = *spot;
                        enemy.flags &= ~(kEnemyAtGoal | kEnemyHasPath | kEnemyRideStart);
                        enemy.pathTick = tick;
                    } else {
                        enemy.waitTick = tick + kPathInterval;
                    }
                }
            } else {
                heading = steer(id, enemy, movement, query, filter).heading;
                if ((enemy.flags & kEnemyAtGoal) != 0) {
                    enemy.waitTick = tick + searchPause(enemy.random);
                    enemy.lookYaw = randomYaw(enemy.random);
                }
            }
        } else if (state != EnemyState::Idle) {
            const Transform& target = *world.transforms.get(enemy.target);
            const MovementSettings& targetMovement = movementOf(world, enemy.target, settings);
            const bool visible =
                canSee(scene, world, settings, type, *transform, enemy.target, true);
            const auto become = [&](EnemyState next) {
                state = next;
                enemy.state = static_cast<std::uint32_t>(next);
                enemy.stateTick = tick;
            };
            if (visible) {
                enemy.lastSeen = target.position;
                enemy.seenTick = tick;
                if (state == EnemyState::Lost ||
                    (state == EnemyState::Cover && (enemy.flags & kEnemyAtGoal) != 0)) {
                    // Found it again, or found out in its cover.
                    become(EnemyState::Chase);
                }
            } else if (state == EnemyState::Chase) {
                become(EnemyState::Lost);
                enemy.flags &= ~kEnemyAtGoal;
            } else if (state == EnemyState::Lost && tick < enemy.seenTick + kTrackTicks) {
                enemy.lastSeen = target.position;
            }
            if (state == EnemyState::Lost &&
                ((enemy.flags & kEnemyAtGoal) != 0 || tick >= enemy.seenTick + kGiveUpTicks)) {
                startSearch(enemy, type);
                state = enemyState(enemy);
            }
            if (state == EnemyState::Alerted &&
                tick >= enemy.stateTick + secondsToTicks(type.reactionTime)) {
                become(EnemyState::Chase);
                enemy.pathTick = tick;
            }
            if (state == EnemyState::Cover && (enemy.flags & kEnemyAtGoal) != 0 &&
                tick >= enemy.waitTick) {
                become(EnemyState::Chase);
                enemy.pathTick = tick;
            }
            if (state != EnemyState::Cover) {
                enemy.goal = enemy.lastSeen;
            }
            if (state == EnemyState::Chase || state == EnemyState::Alerted ||
                state == EnemyState::Attack) {
                // The goal moves with the target, so it is never reached for good.
                enemy.flags &= ~kEnemyAtGoal;
            }
            if (state == EnemyState::Chase && hurtInFight &&
                takeCover(scene, settings, id, enemy, type, query, filter)) {
                state = enemyState(enemy);
            }

            // The way to go: along its path to its goal.
            bool level = false;
            const float gap = groundGap(transform->position, movement.halfExtents, target.position,
                                        targetMovement.halfExtents, level);
            const bool inReach = type.meleeDamage > 0 && visible && level && gap <= type.meleeRange;
            Weapons* weapons = world.weapons.get(id);
            const float distance = length(target.position - transform->position);
            const bool inRange = weapons != nullptr && weapons->current < settings.weapons.size() &&
                                 visible && distance <= type.attackRange;
            const bool hiding = state == EnemyState::Cover;
            const bool inCover = hiding && (enemy.flags & kEnemyAtGoal) != 0;
            // Just lost, it stands looking about; in cover, it waits.
            if (state != EnemyState::Search && !inCover && state != EnemyState::Alerted &&
                !inReach) {
                const Steering steering = steer(id, enemy, movement, query, filter);
                heading = steering.heading;
                if (hiding && (enemy.flags & kEnemyAtGoal) != 0) {
                    enemy.waitTick = tick + secondsToTicks(type.coverTime);
                } else if (!hiding && !steering.waiting && dot(heading, heading) == 0.0f &&
                           visible) {
                    // Off the mesh, or no way round: straight at the target.
                    heading = flat(target.position - transform->position);
                }
            }

            // Shooting: it stops closing in at its distance, and sidesteps one way then the
            // other, turning back where there is no floor to step on.
            if (inRange && state == EnemyState::Chase) {
                if (distance <= type.keepDistance) {
                    heading = {};
                }
                if (type.strafeTime > 0.0f) {
                    if (tick >= enemy.strafeTick) {
                        enemy.strafeTick =
                            tick + std::max(secondsToTicks(type.strafeTime *
                                                           (0.5f + randomUnit(enemy.random))),
                                            1u);
                        if (randomUnit(enemy.random) < 0.5f) {
                            enemy.flags ^= kEnemyStrafeLeft;
                        }
                    }
                    const Vec3 toTarget = normalize(flat(target.position - transform->position));
                    const float sign = (enemy.flags & kEnemyStrafeLeft) != 0 ? 1.0f : -1.0f;
                    Vec3 side = Vec3{-toTarget.y, toTarget.x, 0.0f} * sign;
                    const Vec3 reach{0.3f, 0.3f, movement.stepHeight + 0.1f};
                    if (!query.nearestPoint(feetOf(*transform, movement) + side * kStrafeLook,
                                            filter, reach)) {
                        enemy.flags ^= kEnemyStrafeLeft;
                        side = -side;
                    }
                    heading = dot(heading, heading) > 0.0f
                                  ? normalize(heading) + side * kStrafeShare
                                  : side;
                }
            }

            // Faces the target while it can see it (except running for cover), else the way it
            // is going, else where it last knew the target to be.
            if (visible && !(hiding && dot(heading, heading) > 0.0f)) {
                look = target.position - transform->position;
            } else if (dot(heading, heading) > 0.0f) {
                look = heading;
            } else if (state != EnemyState::Search) {
                look = enemy.lastSeen - transform->position;
            }
            if (visible) {
                cmd.pitch = pitchToward(length(flat(look)), look.z);
            }

            // Melee: a swing starts when the target is in reach and in front, and lands after
            // its wind-up if the target is still there.
            const std::uint16_t towards =
                directionToAngle(target.position.x - transform->position.x,
                                 target.position.y - transform->position.y);
            const std::uint16_t facing =
                dot(flat(look), flat(look)) > 0.0f
                    ? turnTowards(transform->yaw, directionToAngle(look.x, look.y), turnStep(type))
                    : transform->yaw;
            if (inReach && state == EnemyState::Chase && tick >= enemy.readyTick &&
                angleBetween(facing, towards) <= kSwingAim) {
                become(EnemyState::Attack);
                enemy.attackTick = tick + secondsToTicks(type.meleeWindup);
                enemy.readyTick = tick + std::max(secondsToTicks(type.meleeInterval), 1u);
            }
            shoot = inRange && state == EnemyState::Chase && tick >= enemy.readyTick &&
                    angleBetween(facing, towards) <= kShotAim;
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
                become(EnemyState::Chase);
            }
        }

        // Turns, then moves the way it is going at its pace.
        if (dot(flat(look), flat(look)) > 0.0f) {
            cmd.yaw = turnTowards(transform->yaw, directionToAngle(look.x, look.y), turnStep(type));
        } else if (dot(heading, heading) > 0.0f) {
            cmd.yaw =
                turnTowards(transform->yaw, directionToAngle(heading.x, heading.y), turnStep(type));
        } else {
            cmd.yaw = turnTowards(transform->yaw, static_cast<std::uint16_t>(enemy.lookYaw),
                                  turnStep(type));
        }
        if (dot(heading, heading) > 0.0f) {
            const Vec3 way = normalize(heading) * pace;
            const SinCos facing = sinCos(cmd.yaw);
            const float forward = way.x * facing.cos + way.y * facing.sin;
            const float side = way.x * facing.sin - way.y * facing.cos;
            cmd.forward = static_cast<std::int8_t>(std::lround(forward * 127.0f));
            cmd.side = static_cast<std::int8_t>(std::lround(side * 127.0f));
        }

        movePlayer(scene.withBodies(settings, id), movement, cmd, *transform, *motion);
        if (transform->position.z - movement.halfExtents.z < m_level.killHeight()) {
            kill(m_state, id, DamageKind::OutOfWorld);
        }
        if (shoot) {
            // Aimed at the target's middle from where it now stands, off by up to its aim error.
            // The aim stays in its transform for the tick, so clients draw the shot as fired.
            Weapons& weapons = *world.weapons.get(id);
            const Vec3 aim =
                world.transforms.get(enemy.target)->position - eyePosition(movement, *transform);
            const float error = type.aimError / 360.0f * static_cast<float>(kAngleUnitsPerTurn);
            const auto yawError =
                static_cast<std::int32_t>((randomUnit(enemy.random) * 2.0f - 1.0f) * error);
            const auto pitchError =
                static_cast<std::int32_t>((randomUnit(enemy.random) * 2.0f - 1.0f) * error);
            transform->yaw = static_cast<std::uint16_t>(directionToAngle(aim.x, aim.y) + yawError);
            transform->pitch = static_cast<std::int16_t>(std::clamp<std::int32_t>(
                pitchToward(length(flat(aim)), aim.z) + pitchError, -kMaxPitch, kMaxPitch));
            fireWeapon(scene, settings, id, weapons, *transform, m_state);
            weapons.ammo[weapons.current] = static_cast<std::int16_t>(kMaxAmmo);
            if (enemy.burstLeft == 0) {
                enemy.burstLeft = static_cast<std::uint32_t>(std::max(type.burst, 1));
            }
            --enemy.burstLeft;
            const float wait = enemy.burstLeft > 0 ? settings.weapons[weapons.current].fireInterval
                                                   : type.burstPause;
            enemy.readyTick = tick + std::max(secondsToTicks(wait), 1u);
            if (enemy.burstLeft == 0) {
                // Between bursts it may duck out of sight.
                takeCover(scene, settings, id, enemy, type, query, filter);
            }
        }
    }
    for (std::size_t i = 0; i < goneCount; ++i) {
        world.destroy(gone[i]);
    }
    deliverQueued();
}

} // namespace ct
