#include "croftech/sim/Firing.h"

#include "croftech/core/Angle.h"
#include "croftech/core/Random.h"
#include "croftech/sim/CollisionScene.h"
#include "croftech/sim/Damage.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>

namespace ct {
namespace {

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
    return {pitch.cos * yaw.cos, pitch.cos * yaw.sin, -pitch.sin};
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

Weapons startingWeapons(const GameSettings& settings, std::uint32_t inputSlot, TickIndex tick) {
    Weapons weapons;
    weapons.random = inputSlot << 24;
    armPlayer(weapons, settings, tick);
    return weapons;
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
