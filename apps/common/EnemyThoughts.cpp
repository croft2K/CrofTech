#include "EnemyThoughts.h"

#include "EntityShapes.h"

#include "croftech/core/Angle.h"
#include "croftech/sim/Enemies.h"
#include "croftech/sim/Health.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

namespace ct {
namespace {

// Raised off the floor so the depth test doesn't hide what lies on it.
constexpr float kLift = 0.08f;
constexpr float kLineWidth = 0.04f;
// The cone is drawn this far out at most, so it doesn't cover the level; it sees to its sight
// range.
constexpr float kConeRadius = 8.0f;
constexpr int kConeSegments = 24;
constexpr int kRingSegments = 40;
constexpr float kPostHeight = 1.2f;
constexpr float kPostCross = 0.25f;
// Away from its post by more than this, a post marks where it is.
constexpr float kAwayFromPost = 1.0f;
// Seen this recently, the target counts as in sight.
constexpr std::uint32_t kInSightTicks = 2;
constexpr float kLabelRange = 50.0f;
constexpr float kLabelRise = 0.3f;

constexpr Vec3 kConeColour{0.9f, 0.8f, 0.2f};
constexpr Vec3 kHearingColour{0.3f, 0.5f, 1.0f};
constexpr Vec3 kPathColour{0.2f, 0.9f, 1.0f};
constexpr Vec3 kTargetColour{1.0f, 0.15f, 0.1f};
constexpr Vec3 kLastSeenColour{1.0f, 0.55f, 0.1f};
constexpr Vec3 kPostColour{0.35f, 0.5f, 1.0f};

const char* stateName(EnemyState state) {
    switch (state) {
    case EnemyState::Idle:
        return "idle";
    case EnemyState::Alerted:
        return "alerted";
    case EnemyState::Chase:
        return "chase";
    case EnemyState::Attack:
        return "attack";
    case EnemyState::Lost:
        return "lost";
    case EnemyState::Search:
        return "search";
    case EnemyState::Cover:
        return "cover";
    case EnemyState::Patrol:
        return "patrol";
    }
    return "?";
}

// Calm states pale, hunting ones warm, hiding cool.
ImU32 stateColour(EnemyState state) {
    switch (state) {
    case EnemyState::Idle:
        return IM_COL32(220, 220, 220, 255);
    case EnemyState::Patrol:
        return IM_COL32(140, 230, 120, 255);
    case EnemyState::Alerted:
        return IM_COL32(255, 220, 60, 255);
    case EnemyState::Chase:
    case EnemyState::Attack:
        return IM_COL32(255, 90, 70, 255);
    case EnemyState::Lost:
    case EnemyState::Search:
        return IM_COL32(255, 160, 60, 255);
    case EnemyState::Cover:
        return IM_COL32(90, 200, 255, 255);
    }
    return IM_COL32_WHITE;
}

bool hunting(EnemyState state) { return state != EnemyState::Idle && state != EnemyState::Patrol; }

// A fan on the floor round centre, facing yaw, halfAngle either side, with its edges outlined.
void addFan(std::vector<EffectVertex>& out, Vec3 centre, float yaw, float halfAngle, float radius) {
    const auto at = [&](float angle) {
        return centre + Vec3{std::cos(angle), std::sin(angle), 0.0f} * radius;
    };
    Vec3 previous = at(yaw - halfAngle);
    for (int i = 1; i <= kConeSegments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kConeSegments);
        const Vec3 point = at(yaw - halfAngle + 2.0f * halfAngle * t);
        out.insert(out.end(), {{centre, kConeColour, 0.25f},
                               {previous, kConeColour, 0.05f},
                               {point, kConeColour, 0.05f}});
        addEffectLine(out, previous, point, kLineWidth, kConeColour, 0.4f);
        previous = point;
    }
    if (halfAngle < kPi) {
        addEffectLine(out, centre, at(yaw - halfAngle), kLineWidth, kConeColour, 0.6f);
        addEffectLine(out, centre, at(yaw + halfAngle), kLineWidth, kConeColour, 0.6f);
    }
}

void addRing(std::vector<EffectVertex>& out, Vec3 centre, float radius, Vec3 colour, float alpha) {
    Vec3 previous = centre + Vec3{radius, 0.0f, 0.0f};
    for (int i = 1; i <= kRingSegments; ++i) {
        const float angle = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(kRingSegments);
        const Vec3 point = centre + Vec3{std::cos(angle), std::sin(angle), 0.0f} * radius;
        addEffectLine(out, previous, point, kLineWidth, colour, alpha);
        previous = point;
    }
}

// A post standing on a spot, with a cross at its foot.
void addPost(std::vector<EffectVertex>& out, Vec3 feet, Vec3 colour) {
    const Vec3 base = feet + Vec3{0.0f, 0.0f, kLift};
    addEffectLine(out, base, base + Vec3{0.0f, 0.0f, kPostHeight}, kLineWidth * 1.5f, colour, 0.9f);
    addEffectLine(out, base - Vec3{kPostCross, 0.0f, 0.0f}, base + Vec3{kPostCross, 0.0f, 0.0f},
                  kLineWidth, colour, 0.9f);
    addEffectLine(out, base - Vec3{0.0f, kPostCross, 0.0f}, base + Vec3{0.0f, kPostCross, 0.0f},
                  kLineWidth, colour, 0.9f);
}

const EnemySettings* typeOf(const LocalGame& game, const Enemy& enemy) {
    const std::vector<EnemySettings>& enemies = game.settings().enemies;
    return enemy.type < enemies.size() ? &enemies[enemy.type] : nullptr;
}

} // namespace

void addEnemyThoughts(std::vector<EffectVertex>& out, const LocalGame& game,
                      LocalGame::Clock::time_point now) {
    const State* state = game.latestState();
    if (state == nullptr) {
        return;
    }
    const World& world = state->world;
    const Vec3 lift{0.0f, 0.0f, kLift};
    for (std::size_t i = 0; i < world.enemies.size(); ++i) {
        const EntityId id = world.enemies.entities()[i];
        const Enemy& enemy = world.enemies.components()[i];
        // Its type's settings with its own.
        const std::optional<EnemyTuning> type = enemyTuning(world, id, game.settings());
        const std::optional<Transform> transform = game.transform(id, now);
        const Health* health = world.healths.get(id);
        if (!type || !transform || (health != nullptr && isDead(*health))) {
            continue;
        }
        const MovementSettings& movement = type->movement;
        const Vec3 feet = transform->position - Vec3{0.0f, 0.0f, movement.halfExtents.z};
        const Vec3 eye = feet + Vec3{0.0f, 0.0f, movement.eyeHeight};
        const EnemyState current = enemyState(enemy);

        if (!hunting(current)) {
            const float halfAngle = std::min(type->sightAngle, 360.0f) * 0.5f * (kPi / 180.0f);
            addFan(out, feet + lift, angleToRadians(transform->yaw), halfAngle,
                   std::min(type->sightRange, kConeRadius));
            if ((enemy.flags & kEnemyAmbush) == 0) {
                addRing(out, feet + lift, type->footstepRange, kHearingColour, 0.5f);
            }
        }

        if (current != EnemyState::Idle && current != EnemyState::Alerted &&
            current != EnemyState::Attack) {
            if ((enemy.flags & kEnemyHasPath) != 0 && (enemy.flags & kEnemyAtGoal) == 0) {
                addEffectLine(out, feet + lift, enemy.waypoint + lift, kLineWidth * 1.5f,
                              kPathColour, 0.9f);
                addEffectLine(out, enemy.waypoint + lift, enemy.goal + lift, kLineWidth,
                              kPathColour, 0.35f);
            }
            addPost(out, enemy.goal, kPathColour);
        }

        if (const Transform* target = world.transforms.get(enemy.target); target != nullptr) {
            if (enemy.seenTick + kInSightTicks >= state->tick) {
                addEffectLine(out, eye, target->position, kLineWidth, kTargetColour, 0.8f);
            } else if (hunting(current)) {
                addEffectLine(out, eye, enemy.lastSeen, kLineWidth, kLastSeenColour, 0.5f);
                addPost(out,
                        enemy.lastSeen - Vec3{0.0f, 0.0f, game.settings().movement.halfExtents.z},
                        kLastSeenColour);
            }
        }

        const Vec3 fromPost = feet - enemy.home;
        if (enemy.patrolPoint == kNoEntity &&
            dot(fromPost, fromPost) > kAwayFromPost * kAwayFromPost) {
            addPost(out, enemy.home, kPostColour);
        }
    }
}

void drawEnemyLabels(const LocalGame& game, LocalGame::Clock::time_point now, const Camera& camera,
                     float x, float y, float width, float height) {
    const State* state = game.latestState();
    if (state == nullptr || width <= 0.0f || height <= 0.0f) {
        return;
    }
    const Mat4 m = makeViewProjection(camera, width / height);
    ImDrawList& draw = *ImGui::GetBackgroundDrawList();
    const float line = ImGui::GetFontSize();
    const World& world = state->world;
    for (std::size_t i = 0; i < world.enemies.size(); ++i) {
        const EntityId id = world.enemies.entities()[i];
        const Enemy& enemy = world.enemies.components()[i];
        const EnemySettings* type = typeOf(game, enemy);
        const std::optional<Transform> transform = game.transform(id, now);
        if (type == nullptr || !transform) {
            continue;
        }
        const Vec3 head =
            transform->position +
            Vec3{0.0f, 0.0f, movementOf(world, id, game.settings()).halfExtents.z + kLabelRise};
        const Vec3 offset = head - camera.position;
        if (dot(offset, offset) > kLabelRange * kLabelRange) {
            continue;
        }
        // Rows 0, 1 and 3 of the column-major matrix; depth isn't needed.
        const float cx = m.m[0] * head.x + m.m[4] * head.y + m.m[8] * head.z + m.m[12];
        const float cy = m.m[1] * head.x + m.m[5] * head.y + m.m[9] * head.z + m.m[13];
        const float cw = m.m[3] * head.x + m.m[7] * head.y + m.m[11] * head.z + m.m[15];
        if (cw < kCameraNearPlane) {
            continue;
        }
        const ImVec2 at{x + (cx / cw * 0.5f + 0.5f) * width, y + (cy / cw * 0.5f + 0.5f) * height};
        if (at.x < x || at.y < y || at.x > x + width || at.y > y + height) {
            continue;
        }

        const Health* health = world.healths.get(id);
        const bool dead = health != nullptr && isDead(*health);
        const EnemyState current = enemyState(enemy);
        const auto seconds = [&](std::uint32_t from, std::uint32_t to) {
            return static_cast<double>(to > from ? to - from : 0u) *
                   static_cast<double>(kTickSeconds);
        };
        const std::string title = dead
                                      ? std::format("{} dead", type->name)
                                      : std::format("{} {} {:.1f}s", type->name, stateName(current),
                                                    seconds(enemy.stateTick, state->tick));
        std::string details = std::format("{} hp", health != nullptr ? health->health : 0);
        if (!dead) {
            if (enemy.waitTick > state->tick) {
                details += std::format("  waits {:.1f}s", seconds(state->tick, enemy.waitTick));
            }
            if (enemy.burstLeft > 0) {
                details += std::format("  burst {}", enemy.burstLeft);
            }
            if ((enemy.flags & kEnemyAmbush) != 0) {
                details += "  ambush";
            }
            if (world.enemyOverrides.get(id) != nullptr) {
                details += "  own settings";
            }
        }
        const ImVec2 titleSize = ImGui::CalcTextSize(title.c_str());
        const ImVec2 detailsSize = ImGui::CalcTextSize(details.c_str());
        const float boxWidth = std::max(titleSize.x, detailsSize.x) + 8.0f;
        const ImVec2 min{at.x - boxWidth * 0.5f, at.y - line * 2.0f - 6.0f};
        draw.AddRectFilled(min, {min.x + boxWidth, at.y}, IM_COL32(0, 0, 0, 150), 3.0f);
        draw.AddText({at.x - titleSize.x * 0.5f, min.y + 2.0f},
                     dead ? IM_COL32(150, 150, 150, 255) : stateColour(current), title.c_str());
        draw.AddText({at.x - detailsSize.x * 0.5f, min.y + 3.0f + line},
                     IM_COL32(200, 200, 200, 255), details.c_str());
    }
}

} // namespace ct
