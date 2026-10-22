#include "croftech/sim/Damage.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ct {
namespace {

// Health and armour come from saves too, so sums are done wide and clamped back.
std::int32_t clampToInt32(std::int64_t value) {
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(
        value, std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()));
}

void die(const State& state, Health& health) {
    health.flags |= kHealthDead;
    health.diedTick = state.tick;
}

} // namespace

bool applyDamage(State& state, const RulesSettings& rules, EntityId target, const Damage& damage) {
    Health* health = state.world.healths.get(target);
    if (health == nullptr || isDead(*health)) {
        return false;
    }
    if (PlayerMotion* motion = state.world.playerMotions.get(target)) {
        motion->velocity += damage.push;
    }
    if (damage.amount <= 0) {
        return false;
    }
    const std::int64_t amount = damage.amount;
    const float share = std::clamp(rules.armourShare, 0.0f, 1.0f);
    const std::int64_t armourWanted =
        static_cast<std::int64_t>(std::ceil(static_cast<float>(amount) * share));
    const std::int64_t armourTaken =
        std::clamp<std::int64_t>(armourWanted, 0, std::max<std::int32_t>(health->armour, 0));
    health->armour = clampToInt32(health->armour - armourTaken);
    health->health = clampToInt32(health->health - (amount - armourTaken));
    health->hurtTick = state.tick;
    health->hurtKind = static_cast<std::uint32_t>(damage.kind);
    health->attacker = damage.attacker;
    if (health->health > 0) {
        return false;
    }
    die(state, *health);
    return true;
}

void kill(State& state, EntityId target, DamageKind kind, EntityId attacker) {
    Health* health = state.world.healths.get(target);
    if (health == nullptr || isDead(*health)) {
        return;
    }
    health->health = std::min(health->health, 0);
    health->hurtTick = state.tick;
    health->hurtKind = static_cast<std::uint32_t>(kind);
    health->attacker = attacker;
    die(state, *health);
}

bool canRespawn(const Health& health, const RulesSettings& rules, TickIndex tick) {
    const auto delay = static_cast<std::uint32_t>(
        std::clamp(rules.respawnDelay, 0.0f, 3600.0f) * static_cast<float>(kTickRate) + 0.5f);
    return isDead(health) && tick >= health.diedTick + delay;
}

} // namespace ct
