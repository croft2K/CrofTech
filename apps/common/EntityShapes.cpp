#include "EntityShapes.h"

#include "croftech/core/Angle.h"
#include "croftech/level/EntityClasses.h"
#include "croftech/sim/Enemies.h"
#include "croftech/sim/Health.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace ct {
namespace {

constexpr float kThirdPersonDistance = 3.0f;
constexpr float kThirdPersonRise = 0.4f;
constexpr Vec3 kCameraHalfExtents{0.15f, 0.15f, 0.15f};
constexpr Vec3 kProjectileHalfExtents{0.09f, 0.09f, 0.09f};
constexpr Vec3 kRocketColor{0.85f, 0.82f, 0.75f};
constexpr Vec3 kGrenadeColor{0.18f, 0.32f, 0.14f};

// The view weapon: where it is held from the eye (forward, right, up), how far it kicks back and
// drops while being raised, and how long a kick takes to recover.
constexpr Vec3 kViewWeaponHold{0.36f, 0.11f, -0.10f};
constexpr float kViewWeaponKick = 0.05f;
constexpr float kViewWeaponKickSeconds = 0.12f;
constexpr float kViewWeaponDrop = 0.22f;
constexpr Vec3 kViewWeaponColor{0.30f, 0.31f, 0.33f};

constexpr float kPickupSpinRadiansPerSecond = 1.6f;
constexpr float kPickupBobMetres = 0.06f;
constexpr float kPickupBobPerSecond = 2.2f;
constexpr Vec3 kViewWeaponBarrelColor{0.12f, 0.12f, 0.13f};

constexpr float kHurtFlashSeconds = 0.15f;
constexpr Vec3 kHurtFlashColor{1.0f, 0.55f, 0.45f};

// Player colours by input slot, linear RGB.
// Dark, so enemies stand apart from players' bright colours.
constexpr std::array<Vec3, 4> kEnemyColors{{
    {0.35f, 0.08f, 0.06f},
    {0.12f, 0.16f, 0.10f},
    {0.22f, 0.12f, 0.28f},
    {0.30f, 0.24f, 0.10f},
}};

constexpr std::array<Vec3, 8> kPlayerColors{{
    {0.80f, 0.25f, 0.15f},
    {0.15f, 0.40f, 0.85f},
    {0.20f, 0.65f, 0.25f},
    {0.85f, 0.65f, 0.10f},
    {0.55f, 0.25f, 0.75f},
    {0.10f, 0.65f, 0.65f},
    {0.85f, 0.40f, 0.60f},
    {0.45f, 0.45f, 0.45f},
}};

// The tick being drawn, between the last two snapshots, so what is timed from the state keeps time
// with what moves.
float drawnTick(const LocalGame& game, const State& state, LocalGame::Clock::time_point now) {
    return static_cast<float>(state.tick) - 1.0f + game.blend(now);
}

// A body's colour, flashing pale for a moment each time it is hurt.
Vec3 withHurtFlash(Vec3 color, const Health* health, float tick) {
    if (health == nullptr || health->hurtTick == 0) {
        return color;
    }
    const float since = std::max(tick - static_cast<float>(health->hurtTick), 0.0f) * kTickSeconds;
    if (since >= kHurtFlashSeconds) {
        return color;
    }
    return color + (kHurtFlashColor - color) * (1.0f - since / kHurtFlashSeconds);
}

} // namespace

void addEffectLine(std::vector<EffectVertex>& out, Vec3 start, Vec3 end, float width, Vec3 colour,
                   float alpha) {
    const Vec3 along = end - start;
    if (dot(along, along) == 0.0f) {
        return;
    }
    const Vec3 direction = normalize(along);
    Vec3 side = cross(direction, Vec3{0.0f, 0.0f, 1.0f});
    if (dot(side, side) < 1e-6f) {
        side = {1.0f, 0.0f, 0.0f};
    }
    side = normalize(side) * (width * 0.5f);
    const Vec3 other = normalize(cross(direction, side)) * (width * 0.5f);
    for (const Vec3 offset : {side, other}) {
        const Vec3 corners[4] = {start - offset, start + offset, end + offset, end - offset};
        for (const int i : {0, 1, 2, 0, 2, 3}) {
            out.push_back({corners[i], colour, alpha});
        }
    }
}

void addBox(std::vector<WorldVertex>& out, Vec3 centre, Vec3 halfExtents, float yawRadians,
            Vec3 color) {
    const float c = std::cos(yawRadians);
    const float s = std::sin(yawRadians);
    const Vec3 axes[3] = {{c, s, 0.0f}, {-s, c, 0.0f}, {0.0f, 0.0f, 1.0f}};
    addOrientedBox(out, centre, axes, halfExtents, color);
}

void addOrientedBox(std::vector<WorldVertex>& out, Vec3 centre, const Vec3 (&axes)[3],
                    Vec3 halfExtents, Vec3 color) {
    const Vec3 axisX = axes[0];
    const Vec3 axisY = axes[1];
    const Vec3 axisZ = axes[2];
    const auto corner = [&](float x, float y, float z) {
        return centre + axisX * (x * halfExtents.x) + axisY * (y * halfExtents.y) +
               axisZ * (z * halfExtents.z);
    };
    // Each face as an outward normal and two axes spanning it, wound counter-clockwise seen from
    // outside, as the world's are.
    struct Face {
        Vec3 normal;
        Vec3 a; // local unit axes, as signs
        Vec3 b;
        Vec3 n;
    };
    const std::array<Face, 6> faces{{
        {axisX, {0, 1, 0}, {0, 0, 1}, {1, 0, 0}},
        {-axisX, {0, 0, 1}, {0, 1, 0}, {-1, 0, 0}},
        {axisY, {0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
        {-axisY, {1, 0, 0}, {0, 0, 1}, {0, -1, 0}},
        {axisZ, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
        {-axisZ, {0, 1, 0}, {1, 0, 0}, {0, 0, -1}},
    }};
    for (const Face& face : faces) {
        const auto at = [&](float u, float v) {
            const Vec3 p = face.n + face.a * u + face.b * v;
            return WorldVertex{corner(p.x, p.y, p.z), face.normal, color};
        };
        const WorldVertex v00 = at(-1.0f, -1.0f);
        const WorldVertex v10 = at(1.0f, -1.0f);
        const WorldVertex v11 = at(1.0f, 1.0f);
        const WorldVertex v01 = at(-1.0f, 1.0f);
        out.insert(out.end(), {v00, v10, v11, v00, v11, v01});
    }
}

void addPlayers(std::vector<WorldVertex>& out, const LocalGame& game,
                LocalGame::Clock::time_point now, bool includeOwn) {
    const State* state = game.latestState();
    if (state == nullptr) {
        return;
    }
    const MovementSettings& movement = game.settings().movement;
    const World& world = state->world;
    const std::span<const EntityId> players = world.playerControls.entities();
    for (std::size_t i = 0; i < players.size(); ++i) {
        const EntityId id = players[i];
        if (id == game.player() && !includeOwn) {
            continue;
        }
        const std::optional<Transform> transform = game.transform(id, now);
        if (!transform) {
            continue;
        }
        const Vec3 color =
            kPlayerColors[world.playerControls.components()[i].inputSlot % kPlayerColors.size()];
        const float yaw = angleToRadians(transform->yaw);
        const Vec3 half = movement.halfExtents;
        const Vec3 feet = transform->position - Vec3{0.0f, 0.0f, half.z};
        const Health* health = world.healths.get(id);
        if (health != nullptr && isDead(*health)) {
            // Fallen forwards, lying along the way it faced.
            const Vec3 lying{half.z, half.y, half.x * 0.6f};
            addBox(out,
                   feet + Vec3{std::cos(yaw), std::sin(yaw), 0.0f} * half.z +
                       Vec3{0.0f, 0.0f, lying.z},
                   lying, yaw, color * 0.6f);
            continue;
        }
        addBox(out, transform->position, half, yaw,
               withHurtFlash(color, health, drawnTick(game, *state, now)));
        const Vec3 forward{std::cos(yaw), std::sin(yaw), 0.0f};
        const Vec3 visor{half.x * 0.25f, half.y * 0.7f, 0.08f};
        addBox(out, feet + Vec3{0.0f, 0.0f, movement.eyeHeight} + forward * (half.x + visor.x),
               visor, yaw, {0.05f, 0.05f, 0.06f});
    }
}

void addEnemies(std::vector<WorldVertex>& out, const LocalGame& game,
                LocalGame::Clock::time_point now) {
    const State* state = game.latestState();
    if (state == nullptr) {
        return;
    }
    const World& world = state->world;
    const std::span<const EntityId> enemies = world.enemies.entities();
    for (std::size_t i = 0; i < enemies.size(); ++i) {
        const EntityId id = enemies[i];
        const Enemy& enemy = world.enemies.components()[i];
        const std::optional<Transform> transform = game.transform(id, now);
        if (!transform) {
            continue;
        }
        const MovementSettings& movement = movementOf(world, id, game.settings());
        const Vec3 color = kEnemyColors[enemy.type % kEnemyColors.size()];
        const float yaw = angleToRadians(transform->yaw);
        const Vec3 half = movement.halfExtents;
        const Vec3 feet = transform->position - Vec3{0.0f, 0.0f, half.z};
        const Vec3 forward{std::cos(yaw), std::sin(yaw), 0.0f};
        const Health* health = world.healths.get(id);
        if (health != nullptr && isDead(*health)) {
            const Vec3 lying{half.z, half.y, half.x * 0.6f};
            addBox(out, feet + forward * half.z + Vec3{0.0f, 0.0f, lying.z}, lying, yaw,
                   color * 0.5f);
            continue;
        }
        addBox(out, transform->position, half, yaw,
               withHurtFlash(color, health, drawnTick(game, *state, now)));
        const Vec3 right{std::sin(yaw), -std::cos(yaw), 0.0f};
        const Vec3 eye{0.05f, half.y * 0.15f, 0.05f};
        const Vec3 eyes = feet + Vec3{0.0f, 0.0f, movement.eyeHeight} + forward * (half.x + eye.x);
        for (const float side : {-1.0f, 1.0f}) {
            addBox(out, eyes + right * (side * half.y * 0.4f), eye, yaw, {0.95f, 0.85f, 0.4f});
        }
        if (enemyState(enemy) == EnemyState::Attack) {
            // An arm raised above its head, as the swing winds up.
            const Vec3 arm{0.12f, 0.12f, half.z * 0.5f};
            addBox(out,
                   transform->position + right * (half.y + arm.y) +
                       Vec3{0.0f, 0.0f, half.z + arm.z * 0.5f},
                   arm, yaw, color * 1.3f);
        }
    }
}

void addProjectiles(std::vector<WorldVertex>& out, const LocalGame& game,
                    LocalGame::Clock::time_point now) {
    const State* state = game.latestState();
    if (state == nullptr) {
        return;
    }
    const std::vector<WeaponSettings>& weapons = game.settings().weapons;
    const World& world = state->world;
    for (std::size_t i = 0; i < world.projectiles.size(); ++i) {
        const Projectile& projectile = world.projectiles.components()[i];
        const std::optional<Transform> transform =
            game.transform(world.projectiles.entities()[i], now);
        if (hasExploded(projectile) || !transform) {
            continue;
        }
        const bool bounces =
            projectile.weapon < weapons.size() && weapons[projectile.weapon].bounce > 0.0f;
        addBox(out, transform->position, kProjectileHalfExtents, angleToRadians(transform->yaw),
               bounces ? kGrenadeColor : kRocketColor);
    }
}

void addPickups(std::vector<WorldVertex>& out, const LocalGame& game,
                LocalGame::Clock::time_point now) {
    const State* state = game.latestState();
    if (state == nullptr) {
        return;
    }
    const World& world = state->world;
    const RulesSettings& rules = game.settings().rules;
    const float seconds = std::chrono::duration<float>(now.time_since_epoch()).count();
    for (std::size_t i = 0; i < world.pickups.size(); ++i) {
        const EntityId id = world.pickups.entities()[i];
        const LevelEntityRef* ref = world.levelEntities.get(id);
        const GameLevel::Entity* entity =
            ref != nullptr ? game.level().entity(ref->index) : nullptr;
        if (entity == nullptr || !isAvailable(world.pickups.components()[i])) {
            continue;
        }
        // Each a little out of step with the others.
        const float phase = static_cast<float>(ref->index) * 0.7f;
        const float yaw = std::fmod(seconds * kPickupSpinRadiansPerSecond + phase, 2.0f * kPi);
        const Vec3 centre =
            entity->position +
            Vec3{0.0f, 0.0f, kPickupBobMetres * std::sin(seconds * kPickupBobPerSecond + phase)};
        switch (entity->item) {
        case PickupItem::Health: {
            const bool mega = entity->limit > rules.maxHealth;
            addBox(out, centre, mega ? Vec3{0.24f, 0.24f, 0.24f} : Vec3{0.17f, 0.17f, 0.17f}, yaw,
                   mega ? Vec3{0.25f, 0.45f, 0.95f} : Vec3{0.85f, 0.88f, 0.85f});
            // A red cross on top.
            const float top = mega ? 0.24f : 0.17f;
            const Vec3 bar{top * 0.8f, top * 0.25f, 0.01f};
            addBox(out, centre + Vec3{0.0f, 0.0f, top}, bar, yaw, {0.8f, 0.1f, 0.08f});
            addBox(out, centre + Vec3{0.0f, 0.0f, top}, bar, yaw + kPi * 0.5f, {0.8f, 0.1f, 0.08f});
            break;
        }
        case PickupItem::Armour: {
            const bool heavy = entity->limit > rules.maxArmour;
            addBox(out, centre, {0.26f, 0.2f, 0.09f}, yaw,
                   heavy ? Vec3{0.85f, 0.25f, 0.15f} : Vec3{0.9f, 0.75f, 0.2f});
            break;
        }
        case PickupItem::Ammo:
            addBox(out, centre - Vec3{0.0f, 0.0f, 0.1f}, {0.14f, 0.1f, 0.1f}, yaw,
                   {0.45f, 0.35f, 0.2f});
            break;
        case PickupItem::Weapon: {
            const float size =
                entity->weapon != kNoWeapon ? static_cast<float>(entity->weapon) : 0.0f;
            addBox(out, centre, {0.16f + 0.04f * size, 0.04f, 0.06f}, yaw, kViewWeaponColor);
            break;
        }
        }
    }
}

void addViewWeapon(std::vector<WorldVertex>& out, const LocalGame& game, Vec3 eye, float yawRadians,
                   float pitchRadians, LocalGame::Clock::time_point now) {
    const State* state = game.latestState();
    if (state == nullptr) {
        return;
    }
    const World& world = state->world;
    const Weapons* weapons = world.weapons.get(game.player());
    const Health* health = world.healths.get(game.player());
    const std::vector<WeaponSettings>& settings = game.settings().weapons;
    if (weapons == nullptr || weapons->owned == 0 || weapons->current >= settings.size() ||
        (health != nullptr && isDead(*health))) {
        return;
    }
    const WeaponSettings& weapon = settings[weapons->current];
    const float tick = drawnTick(game, *state, now);
    const float sinceRaised =
        std::max(tick - static_cast<float>(weapons->raisedTick), 0.0f) * kTickSeconds;
    const float raised =
        weapon.raiseTime > 0.0f ? std::clamp(sinceRaised / weapon.raiseTime, 0.0f, 1.0f) : 1.0f;
    const float sinceFired =
        std::max(tick - static_cast<float>(weapons->firedTick), 0.0f) * kTickSeconds;
    const float kick = weapons->firedTick != 0 && sinceFired < kViewWeaponKickSeconds
                           ? kViewWeaponKick * (1.0f - sinceFired / kViewWeaponKickSeconds)
                           : 0.0f;

    const float cp = std::cos(pitchRadians);
    const Vec3 forward{std::cos(yawRadians) * cp, std::sin(yawRadians) * cp,
                       std::sin(pitchRadians)};
    const Vec3 right{std::sin(yawRadians), -std::cos(yawRadians), 0.0f};
    const Vec3 up = cross(right, forward);
    const float drop = kViewWeaponDrop * (1.0f - raised) * (1.0f - raised);
    // Each weapon a little longer and wider than the one before, so they tell apart.
    const float size = static_cast<float>(weapons->current);
    const Vec3 body{0.08f + 0.02f * size, 0.018f + 0.003f * size, 0.024f + 0.003f * size};
    const Vec3 hold = eye + forward * (kViewWeaponHold.x - kick) + right * kViewWeaponHold.y +
                      up * (kViewWeaponHold.z - drop);
    // Forward, left and up make a right-handed set, as addOrientedBox's faces are wound for.
    const Vec3 axes[3] = {forward, -right, up};
    addOrientedBox(out, hold, axes, body, kViewWeaponColor);
    const Vec3 barrel{0.04f, body.y * 0.5f, body.z * 0.4f};
    addOrientedBox(out, hold + forward * (body.x + barrel.x) + up * (body.z * 0.3f), axes, barrel,
                   kViewWeaponBarrelColor);
}

Vec3 thirdPersonPosition(const CollisionWorld& world, Vec3 eye, float yawRadians,
                         float pitchRadians) {
    const float cp = std::cos(pitchRadians);
    const Vec3 forward{std::cos(yawRadians) * cp, std::sin(yawRadians) * cp,
                       std::sin(pitchRadians)};
    const Vec3 wanted = eye - forward * kThirdPersonDistance + Vec3{0.0f, 0.0f, kThirdPersonRise};
    return world.traceBox(eye, wanted, kCameraHalfExtents).endPosition;
}

} // namespace ct
