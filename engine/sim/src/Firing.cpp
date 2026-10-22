#include "croftech/sim/Firing.h"

#include "croftech/core/Angle.h"
#include "croftech/core/Random.h"
#include "croftech/sim/CollisionScene.h"
#include "croftech/sim/Damage.h"
#include "croftech/sim/GameLevel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>

namespace ct {
namespace {

// Bouncing projectiles slower than this off the ground come to rest on it.
constexpr float kRestSpeed = 1.0f;

// Where the segment from start along delta first enters the box, as a fraction of delta, if it
// does: 0 if it starts inside.
bool segmentEntersBox(Vec3 start, Vec3 delta, const Aabb& box, float& fraction) {
    float enter = 0.0f;
    float leave = 1.0f;
    const float starts[] = {start.x, start.y, start.z};
    const float deltas[] = {delta.x, delta.y, delta.z};
    const float mins[] = {box.min.x, box.min.y, box.min.z};
    const float maxes[] = {box.max.x, box.max.y, box.max.z};
    for (int axis = 0; axis < 3; ++axis) {
        if (deltas[axis] == 0.0f) {
            if (starts[axis] < mins[axis] || starts[axis] > maxes[axis]) {
                return false;
            }
            continue;
        }
        float in = (mins[axis] - starts[axis]) / deltas[axis];
        float out = (maxes[axis] - starts[axis]) / deltas[axis];
        if (in > out) {
            std::swap(in, out);
        }
        enter = std::max(enter, in);
        leave = std::min(leave, out);
        if (enter > leave) {
            return false;
        }
    }
    fraction = enter;
    return true;
}

std::int32_t roundToInt(float value) { return static_cast<std::int32_t>(value + 0.5f); }

std::uint32_t secondsToTicks(float seconds) {
    return static_cast<std::uint32_t>(
        std::clamp(seconds, 0.0f, 3600.0f) * static_cast<float>(kTickRate) + 0.5f);
}

bool hasAmmo(const Weapons& weapons, const WeaponSettings& weapon, std::uint32_t index) {
    return weapon.ammoPerShot <= 0 || weapons.ammo[index] >= weapon.ammoPerShot;
}

bool canUse(const Weapons& weapons, const GameSettings& settings, std::uint32_t index) {
    return index < settings.weapons.size() && carries(weapons, index) &&
           hasAmmo(weapons, settings.weapons[index], index);
}

void takeOut(Weapons& weapons, const GameSettings& settings, std::uint32_t index, TickIndex tick) {
    weapons.current = index;
    weapons.raisedTick = tick;
    weapons.readyTick =
        std::max(weapons.readyTick, tick + secondsToTicks(settings.weapons[index].raiseTime));
}

// The best weapon that can fire: the last in the list with ammo, as later weapons are stronger.
bool bestUsable(const Weapons& weapons, const GameSettings& settings, std::uint32_t& index) {
    for (std::uint32_t i = static_cast<std::uint32_t>(settings.weapons.size()); i-- > 0;) {
        if (canUse(weapons, settings, i)) {
            index = i;
            return true;
        }
    }
    return false;
}

void launch(const WeaponSettings& weapon, std::uint32_t index, EntityId shooter, Vec3 from,
            Vec3 direction, const Transform& aim, State& state) {
    World& world = state.world;
    const EntityId id = world.entities.create();
    if (!id.isValid()) {
        return; // the world is full; the shot is lost
    }
    world.transforms.add(id, {from, aim.yaw, aim.pitch});
    Projectile projectile;
    projectile.velocity = direction * weapon.projectileSpeed;
    projectile.owner = shooter;
    projectile.weapon = index;
    projectile.fuseTick = state.tick + std::max(secondsToTicks(weapon.fuse), 1u);
    world.projectiles.add(id, projectile);
}

// The nearest point of the box to the point, and so its distance from it.
Vec3 closestPoint(const Aabb& box, Vec3 point) {
    return {std::clamp(point.x, box.min.x, box.max.x), std::clamp(point.y, box.min.y, box.max.y),
            std::clamp(point.z, box.min.z, box.max.z)};
}

// Sets the projectile off where it is: the target it hit (if any) takes the direct damage, and
// everything living near enough, with nothing solid between, takes splash damage and is thrown,
// less the further away it is. The shooter takes only selfDamage of theirs, but the whole push,
// which is what makes rocket jumps.
void explode(const CollisionScene& scene, const GameSettings& settings,
             const WeaponSettings& weapon, Projectile& projectile, Vec3 position, EntityId direct,
             State& state) {
    projectile.flags |= kProjectileExploded;
    projectile.explodedTick = state.tick;
    const Vec3 heading = normalize(projectile.velocity);
    projectile.velocity = {};
    if (direct.isValid()) {
        Damage damage;
        damage.amount = weapon.damage;
        damage.kind = DamageKind::Weapon;
        damage.attacker = projectile.owner;
        damage.push = heading * weapon.knockback;
        applyDamage(state, settings.rules, direct, damage);
    }
    const float radius = weapon.splashRadius;
    if (radius <= 0.0f) {
        return;
    }
    World& world = state.world;
    const Vec3 half = settings.movement.halfExtents;
    const std::span<const EntityId> targets = world.healths.entities();
    for (std::size_t i = 0; i < targets.size(); ++i) {
        const EntityId target = targets[i];
        const Transform* transform = world.transforms.get(target);
        if (target == direct || transform == nullptr || isDead(world.healths.components()[i])) {
            continue;
        }
        const Vec3 centre = transform->position;
        const float distance =
            length(closestPoint({centre - half, centre + half}, position) - position);
        if (distance >= radius || scene.traceBox(position, centre, {}).fraction < 1.0f) {
            continue;
        }
        const float strength = 1.0f - distance / radius;
        float amount = static_cast<float>(weapon.splashDamage) * strength;
        if (target == projectile.owner) {
            amount *= weapon.selfDamage;
        }
        Vec3 away = normalize(centre - position);
        if (away == Vec3{}) {
            away = {0.0f, 0.0f, 1.0f};
        }
        Damage damage;
        damage.amount = roundToInt(amount);
        damage.kind = DamageKind::Weapon;
        damage.attacker = projectile.owner;
        damage.push = away * (weapon.splashKnockback * strength);
        applyDamage(state, settings.rules, target, damage);
    }
}

void fire(const CollisionScene& scene, const GameSettings& settings, EntityId shooter,
          Weapons& weapons, const Transform& transform, State& state) {
    const WeaponSettings& weapon = settings.weapons[weapons.current];
    if (weapon.ammoPerShot > 0) {
        weapons.ammo[weapons.current] =
            static_cast<std::int16_t>(weapons.ammo[weapons.current] - weapon.ammoPerShot);
    }
    weapons.firedTick = state.tick;
    weapons.readyTick = state.tick + std::max(secondsToTicks(weapon.fireInterval), 1u);

    const Vec3 eye = eyePosition(settings.movement, transform);
    const float range = std::clamp(weapon.range, 0.0f, 1000.0f);
    for (std::int32_t pellet = 0; pellet < std::clamp(weapon.pellets, 1, 64); ++pellet) {
        const Vec3 direction = pelletDirection(transform, weapon.spread, weapons.random);
        if (weapon.projectileSpeed > 0.0f) {
            launch(weapon, weapons.current, shooter, eye, direction, transform, state);
            continue;
        }
        const ShotHit hit =
            traceShot(scene, state.world, settings.movement, eye, eye + direction * range, shooter);
        if (hit.target.isValid()) {
            Damage damage;
            damage.amount = weapon.damage;
            damage.kind = DamageKind::Weapon;
            damage.attacker = shooter;
            damage.push = direction * weapon.knockback;
            applyDamage(state, settings.rules, hit.target, damage);
        }
    }
}

} // namespace

ShotHit traceShot(const CollisionScene& scene, const World& world, const MovementSettings& movement,
                  Vec3 start, Vec3 end, EntityId shooter) {
    const TraceResult solid = scene.traceBox(start, end, {});
    ShotHit hit;
    hit.fraction = solid.fraction;
    hit.position = solid.endPosition;
    hit.normal = solid.normal;
    hit.mover = solid.entity;
    const Vec3 delta = end - start;
    const std::span<const EntityId> targets = world.healths.entities();
    const std::span<const Health> healths = world.healths.components();
    for (std::size_t i = 0; i < targets.size(); ++i) {
        const Transform* transform = world.transforms.get(targets[i]);
        if (targets[i] == shooter || isDead(healths[i]) || transform == nullptr) {
            continue;
        }
        const Aabb box{transform->position - movement.halfExtents,
                       transform->position + movement.halfExtents};
        float fraction = 1.0f;
        if (segmentEntersBox(start, delta, box, fraction) && fraction < hit.fraction) {
            hit = {fraction, start + delta * fraction, {}, targets[i], {}};
        }
    }
    return hit;
}

Vec3 viewForward(const Transform& transform) {
    const SinCos yaw = sinCos(transform.yaw);
    const SinCos pitch = sinCos(transform.pitch);
    return {pitch.cos * yaw.cos, pitch.cos * yaw.sin, pitch.sin};
}

Vec3 pelletDirection(const Transform& transform, float spread, std::uint32_t& random) {
    const Vec3 forward = viewForward(transform);
    if (spread <= 0.0f) {
        return forward;
    }
    const SinCos yaw = sinCos(transform.yaw);
    const Vec3 right{yaw.sin, -yaw.cos, 0.0f};
    const Vec3 up = cross(right, forward);
    constexpr float kUnitsPerDegree = static_cast<float>(kAngleUnitsPerTurn) / 360.0f;
    // The square root spreads pellets evenly over the circle's area, not bunched at its middle.
    const float away = std::min(spread, 45.0f) * std::sqrt(randomUnit(random)) * kUnitsPerDegree;
    const auto around = static_cast<std::uint16_t>(
        static_cast<std::uint32_t>(randomUnit(random) * static_cast<float>(kAngleUnitsPerTurn)));
    const SinCos tilt = sinCos(static_cast<std::uint16_t>(away));
    const SinCos turn = sinCos(around);
    return forward * tilt.cos + (right * turn.cos + up * turn.sin) * tilt.sin;
}

void armPlayer(Weapons& weapons, const GameSettings& settings, TickIndex tick) {
    const std::uint32_t random = weapons.random;
    weapons = {};
    weapons.random = random;
    bool chosen = false;
    for (std::uint32_t i = 0; i < settings.weapons.size() && i < kMaxWeapons; ++i) {
        const WeaponSettings& weapon = settings.weapons[i];
        if (!weapon.given) {
            continue;
        }
        weapons.owned |= 1u << i;
        weapons.ammo[i] = static_cast<std::int16_t>(
            std::clamp(weapon.startAmmo, 0, std::min(weapon.maxAmmo, kMaxAmmo)));
        if (!chosen) {
            chosen = true;
            takeOut(weapons, settings, i, tick);
        }
    }
}

bool giveAmmo(Weapons& weapons, const GameSettings& settings, std::uint32_t weapon,
              std::int32_t amount) {
    if (weapon >= settings.weapons.size() || weapon >= kMaxWeapons) {
        return false;
    }
    const std::int32_t most = std::clamp(settings.weapons[weapon].maxAmmo, 0, kMaxAmmo);
    const std::int32_t held = weapons.ammo[weapon];
    if (held >= most) {
        return false;
    }
    weapons.ammo[weapon] =
        static_cast<std::int16_t>(std::clamp(held + std::clamp(amount, 0, kMaxAmmo), 0, most));
    return true;
}

bool giveWeapon(Weapons& weapons, const GameSettings& settings, std::uint32_t weapon,
                std::int32_t ammo, TickIndex tick) {
    if (weapon >= settings.weapons.size() || weapon >= kMaxWeapons) {
        return false;
    }
    if (carries(weapons, weapon)) {
        return giveAmmo(weapons, settings, weapon, ammo);
    }
    weapons.owned |= 1u << weapon;
    giveAmmo(weapons, settings, weapon, ammo);
    if (weapon > weapons.current || !canUse(weapons, settings, weapons.current)) {
        takeOut(weapons, settings, weapon, tick);
    }
    return true;
}

Weapons startingWeapons(const GameSettings& settings, std::uint32_t inputSlot, TickIndex tick) {
    Weapons weapons;
    weapons.random = inputSlot << 24;
    armPlayer(weapons, settings, tick);
    return weapons;
}

void runProjectiles(const CollisionScene& scene, const GameLevel& level,
                    const GameSettings& settings, State& state) {
    World& world = state.world;
    std::array<EntityId, kMaxEntities> finished{};
    std::size_t finishedCount = 0;
    const std::span<const EntityId> ids = world.projectiles.entities();
    const std::span<Projectile> projectiles = world.projectiles.components();
    for (std::size_t i = 0; i < ids.size(); ++i) {
        Projectile& projectile = projectiles[i];
        Transform* transform = world.transforms.get(ids[i]);
        const WeaponSettings* weapon = projectile.weapon < settings.weapons.size()
                                           ? &settings.weapons[projectile.weapon]
                                           : nullptr;
        if (transform == nullptr || weapon == nullptr ||
            (hasExploded(projectile) && state.tick >= projectile.explodedTick + kExplosionTicks) ||
            transform->position.z < level.killHeight()) {
            finished[finishedCount++] = ids[i];
            continue;
        }
        if (hasExploded(projectile)) {
            continue;
        }
        if (state.tick >= projectile.fuseTick) {
            explode(scene, settings, *weapon, projectile, transform->position, {}, state);
            continue;
        }
        if ((projectile.flags & kProjectileResting) != 0) {
            continue;
        }
        // Half the gravity before the move and half after, as for players.
        const float fall = settings.movement.gravity * weapon->projectileGravity * kTickSeconds;
        projectile.velocity.z -= fall * 0.5f;
        const Vec3 start = transform->position;
        const ShotHit hit = traceShot(scene, world, settings.movement, start,
                                      start + projectile.velocity * kTickSeconds, projectile.owner);
        transform->position = hit.position;
        if (hit.target.isValid() || (hit.fraction < 1.0f && weapon->bounce <= 0.0f)) {
            explode(scene, settings, *weapon, projectile, hit.position, hit.target, state);
            continue;
        }
        if (hit.fraction < 1.0f) {
            const Vec3 v = projectile.velocity;
            projectile.velocity = (v - hit.normal * (2.0f * dot(v, hit.normal))) * weapon->bounce;
            if (hit.normal.z >= settings.movement.minGroundNormalZ &&
                length(projectile.velocity) < kRestSpeed) {
                projectile.velocity = {};
                projectile.flags |= kProjectileResting;
                continue;
            }
        }
        projectile.velocity.z -= fall * 0.5f;
    }
    for (std::size_t i = 0; i < finishedCount; ++i) {
        world.destroy(finished[i]);
    }
}

void runWeapons(const CollisionScene& scene, const GameSettings& settings, const InputSet& inputs,
                State& state) {
    World& world = state.world;
    const std::span<const EntityId> shooters = world.weapons.entities();
    const std::span<Weapons> carried = world.weapons.components();
    for (std::size_t i = 0; i < shooters.size(); ++i) {
        const EntityId id = shooters[i];
        Weapons& weapons = carried[i];
        const PlayerControl* control = world.playerControls.get(id);
        const Transform* transform = world.transforms.get(id);
        const Health* health = world.healths.get(id);
        if (control == nullptr || control->inputSlot >= kMaxPlayers || transform == nullptr ||
            (health != nullptr && isDead(*health))) {
            continue;
        }
        const UserCmd& cmd = inputs.commands[control->inputSlot];
        if (cmd.weapon != 0) {
            const std::uint32_t wanted = cmd.weapon - 1u;
            if (wanted != weapons.current && canUse(weapons, settings, wanted)) {
                takeOut(weapons, settings, wanted, state.tick);
            }
        }
        if ((cmd.buttons & kButtonAttack) == 0 || state.tick < weapons.readyTick) {
            continue;
        }
        if (!canUse(weapons, settings, weapons.current)) {
            // Out of ammo: change to the best weapon that has some, which fires once raised.
            std::uint32_t best = 0;
            if (bestUsable(weapons, settings, best) && best != weapons.current) {
                takeOut(weapons, settings, best, state.tick);
            }
            continue;
        }
        fire(scene, settings, id, weapons, *transform, state);
    }
}

} // namespace ct
