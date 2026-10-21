#include "EntityShapes.h"

#include "croftech/core/Angle.h"
#include "croftech/sim/Health.h"

#include <array>
#include <cmath>
#include <cstddef>

namespace ct {
namespace {

constexpr float kThirdPersonDistance = 3.0f;
constexpr float kThirdPersonRise = 0.4f;
constexpr Vec3 kCameraHalfExtents{0.15f, 0.15f, 0.15f};

// Player colours by input slot, linear RGB.
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

} // namespace

void addBox(std::vector<WorldVertex>& out, Vec3 centre, Vec3 halfExtents, float yawRadians,
            Vec3 color) {
    const float c = std::cos(yawRadians);
    const float s = std::sin(yawRadians);
    const Vec3 axisX{c, s, 0.0f};
    const Vec3 axisY{-s, c, 0.0f};
    const Vec3 axisZ{0.0f, 0.0f, 1.0f};
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
        addBox(out, transform->position, half, yaw, color);
        const Vec3 forward{std::cos(yaw), std::sin(yaw), 0.0f};
        const Vec3 visor{half.x * 0.25f, half.y * 0.7f, 0.08f};
        addBox(out, feet + Vec3{0.0f, 0.0f, movement.eyeHeight} + forward * (half.x + visor.x),
               visor, yaw, {0.05f, 0.05f, 0.06f});
    }
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
