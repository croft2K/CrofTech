#include "GameEffects.h"

#include "croftech/level/EntityClasses.h"

#include <algorithm>
#include <cmath>

namespace ct {
namespace {

constexpr float kGlowSeconds = 0.8f;
constexpr float kGlowRadius = 0.6f;
constexpr float kGlowHeight = 2.4f;
constexpr int kGlowSides = 8;
constexpr Vec3 kGlowColor{0.35f, 0.75f, 1.0f};
constexpr float kPickupGlowScale = 0.45f;

constexpr float kFlashSeconds = 0.45f;
constexpr float kFlashStartRadius = 0.4f;
constexpr float kFlashEndRadius = 2.6f;
constexpr int kFlashSides = 12;
constexpr Vec3 kFlashColor{1.0f, 0.55f, 0.15f};
constexpr Vec3 kFlashCoreColor{1.0f, 0.9f, 0.6f};

constexpr float kSparkSeconds = 0.18f;
constexpr float kSparkRadius = 0.16f;
constexpr Vec3 kSparkColor{1.0f, 0.85f, 0.5f};
constexpr Vec3 kBloodColor{0.9f, 0.08f, 0.05f};
constexpr float kTracerSeconds = 0.07f;
constexpr float kTracerWidth = 0.012f;
constexpr Vec3 kTracerColor{1.0f, 0.9f, 0.6f};
// Tracers start this far along the shot, so they don't fill the shooter's own view, and offset to
// where a held weapon would be.
constexpr float kTracerStart = 0.6f;
constexpr float kTracerDrop = 0.12f;

float lifetime(GameEventKind kind) {
    switch (kind) {
    case GameEventKind::Explosion:
        return kFlashSeconds;
    case GameEventKind::Impact:
        return kSparkSeconds;
    case GameEventKind::Fire:
        return kTracerSeconds;
    default:
        return kGlowSeconds;
    }
}

// A ring of panels that fades out and narrows, brightest at the bottom.
void addGlow(Vec3 position, float age, float scale, std::vector<EffectVertex>& out) {
    const float fade = 1.0f - age;
    const float radius = kGlowRadius * scale * (1.0f - 0.5f * age);
    for (int side = 0; side < kGlowSides; ++side) {
        const float a0 = static_cast<float>(side) * (2.0f * kPi / kGlowSides);
        const float a1 = static_cast<float>(side + 1) * (2.0f * kPi / kGlowSides);
        const Vec3 p0 = position + Vec3{std::cos(a0), std::sin(a0), 0.0f} * radius;
        const Vec3 p1 = position + Vec3{std::cos(a1), std::sin(a1), 0.0f} * radius;
        const Vec3 up{0.0f, 0.0f, kGlowHeight * scale};
        const EffectVertex b0{p0, kGlowColor, 0.8f * fade};
        const EffectVertex b1{p1, kGlowColor, 0.8f * fade};
        const EffectVertex t0{p0 + up, kGlowColor, 0.0f};
        const EffectVertex t1{p1 + up, kGlowColor, 0.0f};
        out.insert(out.end(), {b0, b1, t1, b0, t1, t0});
    }
}

// Two crossed thin quads along the pellet's path, fading from the far end.
void addTracer(Vec3 origin, Vec3 end, float age, std::vector<EffectVertex>& out) {
    const Vec3 path = end - origin;
    const float distance = length(path);
    if (distance <= kTracerStart * 2.0f) {
        return;
    }
    const Vec3 direction = path / distance;
    const Vec3 side = normalize(cross(direction, Vec3{0.0f, 0.0f, 1.0f}));
    const Vec3 lift = cross(side, direction);
    const Vec3 start = origin + direction * kTracerStart - Vec3{0.0f, 0.0f, kTracerDrop};
    const float alpha = 0.7f * (1.0f - age);
    for (const Vec3 across : {side, lift}) {
        const Vec3 w = across * kTracerWidth;
        const EffectVertex a0{start - w, kTracerColor, alpha * 0.3f};
        const EffectVertex a1{start + w, kTracerColor, alpha * 0.3f};
        const EffectVertex b0{end - w, kTracerColor, alpha};
        const EffectVertex b1{end + w, kTracerColor, alpha};
        out.insert(out.end(), {a0, a1, b1, a0, b1, b0});
    }
}

// Three crossed discs that swell and fade, hot in the middle and clear at the edge, so the flash
// looks round from any side.
void addFlash(Vec3 position, float age, float startRadius, float endRadius, Vec3 core, Vec3 rim,
              std::vector<EffectVertex>& out) {
    const float fade = (1.0f - age) * (1.0f - age);
    const float radius = startRadius + (endRadius - startRadius) * std::sqrt(age);
    const Vec3 axes[3][2] = {{{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
                             {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
                             {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}};
    const EffectVertex centre{position, core, fade};
    for (const auto& axis : axes) {
        for (int side = 0; side < kFlashSides; ++side) {
            const float a0 = static_cast<float>(side) * (2.0f * kPi / kFlashSides);
            const float a1 = static_cast<float>(side + 1) * (2.0f * kPi / kFlashSides);
            const Vec3 p0 = position + (axis[0] * std::cos(a0) + axis[1] * std::sin(a0)) * radius;
            const Vec3 p1 = position + (axis[0] * std::cos(a1) + axis[1] * std::sin(a1)) * radius;
            out.insert(out.end(), {centre, {p0, rim, 0.0f}, {p1, rim, 0.0f}});
        }
    }
}

} // namespace

void GameEffects::add(std::span<const GameEvent> events, Clock::time_point now) {
    for (const GameEvent& event : events) {
        Effect effect;
        effect.kind = event.kind;
        effect.position = event.position;
        effect.start = now;
        switch (event.kind) {
        case GameEventKind::Respawn:
        case GameEventKind::Explosion:
            m_effects.push_back(effect);
            break;
        case GameEventKind::PickupRespawn:
            // From the floor it rests on.
            effect.position.z -= kPickupHalfExtents.z;
            m_effects.push_back(effect);
            break;
        case GameEventKind::Impact:
            effect.hit = event.strength > 0.0f;
            m_effects.push_back(effect);
            // The tracer leads to the impact, and goes as fast.
            effect.kind = GameEventKind::Fire;
            effect.origin = event.origin;
            m_effects.push_back(effect);
            break;
        default:
            break;
        }
    }
}

void GameEffects::build(Clock::time_point now, std::vector<EffectVertex>& out) {
    std::erase_if(m_effects, [&](const Effect& effect) {
        return std::chrono::duration<float>(now - effect.start).count() > lifetime(effect.kind);
    });
    for (const Effect& effect : m_effects) {
        const float age = std::clamp(std::chrono::duration<float>(now - effect.start).count() /
                                         lifetime(effect.kind),
                                     0.0f, 1.0f);
        switch (effect.kind) {
        case GameEventKind::Explosion:
            addFlash(effect.position, age, kFlashStartRadius, kFlashEndRadius, kFlashCoreColor,
                     kFlashColor, out);
            break;
        case GameEventKind::Impact:
            addFlash(effect.position, age, kSparkRadius * 0.4f, kSparkRadius,
                     effect.hit ? kBloodColor : kSparkColor, effect.hit ? kBloodColor : kSparkColor,
                     out);
            break;
        case GameEventKind::Fire:
            addTracer(effect.origin, effect.position, age, out);
            break;
        case GameEventKind::PickupRespawn:
            addGlow(effect.position, age, kPickupGlowScale, out);
            break;
        default:
            addGlow(effect.position, age, 1.0f, out);
            break;
        }
    }
}

} // namespace ct
