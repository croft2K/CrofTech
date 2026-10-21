#include "croftech/sim/PlayerMovement.h"

#include "croftech/core/Angle.h"
#include "croftech/core/Profile.h"
#include "croftech/sim/CollisionScene.h"
#include "croftech/sim/World.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace ct {
namespace {

constexpr std::size_t kMaxClipPlanes = 5;
constexpr int kMaxSlideIterations = 4;
constexpr float kOverbounce = 1.001f;
constexpr float kGroundProbeDistance = 0.01f;
// Moving away from the ground faster than this (along its normal) means we have left it.
constexpr float kLeaveGroundSpeed = 0.25f;
// Upward speed added to ground moves, as a fraction of the speed (see groundMoveVelocity).
constexpr float kGroundLift = 0.001f;

struct Ground {
    bool onGround = false;
    Vec3 normal{0.0f, 0.0f, 1.0f};
    EntityId entity; // the mover stood on; invalid for the world
};

Ground findGround(const CollisionScene& world, const MovementSettings& settings, Vec3 position,
                  Vec3 velocity) {
    const TraceResult trace = world.traceBox(
        position, position - Vec3{0.0f, 0.0f, kGroundProbeDistance}, settings.halfExtents);
    if (trace.fraction >= 1.0f || trace.allSolid || trace.normal.z < settings.minGroundNormalZ ||
        dot(velocity, trace.normal) > kLeaveGroundSpeed) {
        return {};
    }
    return {true, trace.normal, trace.entity};
}

// Removes the part of the velocity going into the plane, plus a hair more so the next trace
// doesn't touch the same surface.
Vec3 clipVelocity(Vec3 velocity, Vec3 normal) {
    float backoff = dot(velocity, normal);
    backoff = backoff < 0.0f ? backoff * kOverbounce : backoff / kOverbounce;
    return velocity - normal * backoff;
}

// Sliding along a slope too steep to stand on must not lift the player, or pushing into it would
// climb it. Such a slope acts as a vertical wall, unless the player is falling onto it, in which
// case they slide down it as normal.
Vec3 clipAgainstSurface(Vec3 velocity, Vec3 normal, float minGroundNormalZ) {
    const Vec3 clipped = clipVelocity(velocity, normal);
    if (normal.z > 0.0f && normal.z < minGroundNormalZ && clipped.z > std::max(velocity.z, 0.0f)) {
        return clipVelocity(velocity, normalize(Vec3{normal.x, normal.y, 0.0f}));
    }
    return clipped;
}

// Tilts the velocity onto walkable ground, keeping its horizontal direction and its speed (so
// slopes don't slow walking). Clipping along the ground's normal instead would also turn it towards
// running across the slope, and that turn would build up every tick.
Vec3 tiltOntoGround(Vec3 velocity, Vec3 normal) {
    if (normal.z <= 0.0f) {
        return velocity;
    }
    const float speed = length(velocity);
    const Vec3 tilted{velocity.x, velocity.y,
                      -(normal.x * velocity.x + normal.y * velocity.y) / normal.z};
    return normalize(tilted) * speed;
}

// The velocity for a ground move: tilted onto the ground, plus a slight lift so the move never runs
// exactly parallel to a slope. Rounding would then put some ends a hair closer to the surface than
// the start, and the trace would stop dead. Sticking to the ground afterwards removes the lift.
Vec3 groundMoveVelocity(Vec3 velocity, Vec3 normal) {
    return tiltOntoGround(velocity, normal) + Vec3{0.0f, 0.0f, length(velocity) * kGroundLift};
}

void applyFriction(const MovementSettings& settings, Vec3& velocity) {
    const float speed = length(Vec3{velocity.x, velocity.y, 0.0f});
    if (speed < 0.001f) {
        velocity.x = 0.0f;
        velocity.y = 0.0f;
        return;
    }
    const float control = std::max(speed, settings.stopSpeed);
    const float newSpeed = std::max(speed - control * settings.friction * kTickSeconds, 0.0f);
    const float scale = newSpeed / speed;
    velocity.x *= scale;
    velocity.y *= scale;
}

// Speed is only added up to `speedCap` in the wish direction, but the rate of gain uses the full
// wish speed. In the air that pairing is what makes strafe-jumping possible.
void accelerate(Vec3& velocity, Vec3 wishDirection, float wishSpeed, float speedCap,
                float acceleration) {
    const float addSpeed = std::min(wishSpeed, speedCap) - dot(velocity, wishDirection);
    if (addSpeed <= 0.0f) {
        return;
    }
    const float gain = std::min(acceleration * wishSpeed * kTickSeconds, addSpeed);
    velocity += wishDirection * gain;
}

// Moves through the world for one tick, sliding along anything hit. Returns true if blocked.
bool slideMove(const CollisionScene& world, const MovementSettings& settings, const Ground& ground,
               Vec3& position, Vec3& velocity) {
    const Vec3 halfExtents = settings.halfExtents;
    std::array<Vec3, kMaxClipPlanes> planes{};
    std::size_t planeCount = 0;
    if (ground.onGround) {
        planes[planeCount++] = ground.normal;
    }

    const Vec3 originalVelocity = velocity;
    float timeLeft = kTickSeconds;
    bool blocked = false;

    for (int iteration = 0; iteration < kMaxSlideIterations; ++iteration) {
        const TraceResult trace =
            world.traceBox(position, position + velocity * timeLeft, halfExtents);
        if (trace.allSolid) {
            velocity.z = 0.0f;
            return true;
        }
        if (trace.fraction > 0.0f) {
            position = trace.endPosition;
        }
        if (trace.fraction >= 1.0f) {
            break;
        }
        blocked = true;
        timeLeft -= timeLeft * trace.fraction;

        if (planeCount >= kMaxClipPlanes) {
            velocity = {};
            break;
        }
        planes[planeCount++] = trace.normal;

        // Find a velocity that slides along one plane without going into any of the others.
        std::size_t i = 0;
        Vec3 clipped;
        for (; i < planeCount; ++i) {
            clipped = clipAgainstSurface(velocity, planes[i], settings.minGroundNormalZ);
            std::size_t j = 0;
            while (j < planeCount && (j == i || dot(clipped, planes[j]) >= 0.0f)) {
                ++j;
            }
            if (j == planeCount) {
                break;
            }
        }
        if (i < planeCount) {
            velocity = clipped;
        } else {
            // Wedged between planes: slide along the crease of the last two.
            const Vec3 crease = normalize(cross(planes[planeCount - 2], planes[planeCount - 1]));
            velocity = crease * dot(crease, velocity);
        }
        // Never bounce back against the direction we started moving.
        if (dot(velocity, originalVelocity) <= 0.0f) {
            velocity = {};
            break;
        }
    }
    return blocked;
}

float horizontalDistance(Vec3 a, Vec3 b) { return length(Vec3{a.x - b.x, a.y - b.y, 0.0f}); }

// Slides along the ground. If blocked, also tries the same move from stepHeight higher and keeps
// whichever got further, which is how players walk up stairs without jumping.
void groundMove(const CollisionScene& world, const MovementSettings& settings, const Ground& ground,
                Vec3& position, Vec3& velocity) {
    const Vec3 startPosition = position;
    const Vec3 startVelocity = velocity;

    Vec3 flatPosition = position;
    Vec3 flatVelocity = velocity;
    if (!slideMove(world, settings, ground, flatPosition, flatVelocity)) {
        position = flatPosition;
        velocity = flatVelocity;
        return;
    }

    const TraceResult up = world.traceBox(
        startPosition, startPosition + Vec3{0.0f, 0.0f, settings.stepHeight}, settings.halfExtents);
    if (up.allSolid) {
        position = flatPosition;
        velocity = flatVelocity;
        return;
    }
    Vec3 stepPosition = up.endPosition;
    Vec3 stepVelocity = startVelocity;
    slideMove(world, settings, Ground{}, stepPosition, stepVelocity);

    const float raised = up.endPosition.z - startPosition.z;
    const TraceResult down =
        world.traceBox(stepPosition, stepPosition - Vec3{0.0f, 0.0f, raised}, settings.halfExtents);
    const bool landedOnGround =
        !down.allSolid && down.fraction < 1.0f && down.normal.z >= settings.minGroundNormalZ;
    if (landedOnGround && horizontalDistance(down.endPosition, startPosition) >
                              horizontalDistance(flatPosition, startPosition) + 1e-4f) {
        position = down.endPosition;
        velocity = stepVelocity;
        velocity.z = 0.0f;
    } else {
        position = flatPosition;
        velocity = flatVelocity;
    }
}

// Keeps a walking player on the ground over stairs, slopes and the crest of a ramp instead of
// launching off, and turns their velocity to follow the new ground.
void stickToGround(const CollisionScene& world, const MovementSettings& settings, Vec3& position,
                   Vec3& velocity) {
    const TraceResult down = world.traceBox(
        position, position - Vec3{0.0f, 0.0f, settings.stepHeight}, settings.halfExtents);
    if (!down.startSolid && down.fraction < 1.0f && down.normal.z >= settings.minGroundNormalZ) {
        position = down.endPosition;
        velocity = tiltOntoGround(velocity, down.normal);
    }
}

} // namespace

void movePlayer(const CollisionScene& world, const MovementSettings& settings, const UserCmd& cmd,
                Transform& transform, PlayerMotion& motion) {
    CT_PROFILE_ZONE();
    transform.yaw = cmd.yaw;
    transform.pitch = std::clamp(cmd.pitch, static_cast<std::int16_t>(-kMaxPitch), kMaxPitch);

    const SinCos yaw = sinCos(cmd.yaw);
    const Vec3 forward{yaw.cos, yaw.sin, 0.0f};
    const Vec3 right{yaw.sin, -yaw.cos, 0.0f};
    const Vec3 wish = forward * (static_cast<float>(cmd.forward) / 127.0f) +
                      right * (static_cast<float>(cmd.side) / 127.0f);
    const Vec3 wishDirection = normalize(wish);
    const float wishSpeed = std::min(length(wish), 1.0f) * settings.maxGroundSpeed;

    Vec3& position = transform.position;
    Vec3& velocity = motion.velocity;
    Ground ground = findGround(world, settings, position, velocity);

    const bool jumpHeld = (cmd.buttons & kButtonJump) != 0;
    const bool jumpPressed =
        jumpHeld && (settings.autoJump || (motion.flags & kPlayerJumpHeld) == 0);
    bool jumped = false;
    if (ground.onGround && jumpPressed) {
        velocity.z = settings.jumpSpeed;
        ground = {};
        jumped = true;
    }

    if (ground.onGround) {
        // Friction and acceleration work in the horizontal plane at the player's full speed; the
        // result is tilted back along the ground below.
        const float groundSpeed = length(velocity);
        velocity.z = 0.0f;
        velocity = normalize(velocity) * groundSpeed;
        applyFriction(settings, velocity);
        accelerate(velocity, wishDirection, wishSpeed, wishSpeed, settings.groundAcceleration);
        velocity = groundMoveVelocity(velocity, ground.normal);
        groundMove(world, settings, ground, position, velocity);
        stickToGround(world, settings, position, velocity);
    } else {
        accelerate(velocity, wishDirection, wishSpeed, settings.airWishSpeedCap,
                   settings.airAcceleration);
        // Half the gravity before the move and half after integrates constant acceleration
        // exactly, so jump height doesn't depend on the tick rate.
        velocity.z -= settings.gravity * kTickSeconds * 0.5f;
        slideMove(world, settings, Ground{}, position, velocity);
        velocity.z -= settings.gravity * kTickSeconds * 0.5f;
    }

    ground = findGround(world, settings, position, velocity);
    // Landing stops movement into the ground. Only that part goes: zeroing all downward speed
    // would also tip a player walking down a slope off it.
    if (ground.onGround && !jumped) {
        const float into = dot(velocity, ground.normal);
        if (into < 0.0f) {
            velocity -= ground.normal * into;
        }
    }
    motion.flags = (ground.onGround ? kPlayerOnGround : 0u) | (jumpHeld ? kPlayerJumpHeld : 0u);
}

EntityId standingOn(const CollisionScene& world, const MovementSettings& settings,
                    const Transform& transform, const PlayerMotion& motion) {
    const Ground ground = findGround(world, settings, transform.position, motion.velocity);
    return ground.onGround ? ground.entity : EntityId{};
}

Vec3 eyePosition(const MovementSettings& settings, const Transform& transform) {
    return transform.position + Vec3{0.0f, 0.0f, settings.eyeHeight - settings.halfExtents.z};
}

} // namespace ct
