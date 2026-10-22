#include "GameEffects.h"

#include <algorithm>
#include <cmath>

namespace ct {
namespace {

constexpr float kGlowSeconds = 0.8f;
constexpr float kGlowRadius = 0.6f;
constexpr float kGlowHeight = 2.4f;
constexpr int kGlowSides = 8;
constexpr Vec3 kGlowColor{0.35f, 0.75f, 1.0f};

constexpr float kFlashSeconds = 0.45f;
constexpr float kFlashStartRadius = 0.4f;
constexpr float kFlashEndRadius = 2.6f;
constexpr int kFlashSides = 12;
constexpr Vec3 kFlashColor{1.0f, 0.55f, 0.15f};
constexpr Vec3 kFlashCoreColor{1.0f, 0.9f, 0.6f};

float lifetime(GameEventKind kind) {
    return kind == GameEventKind::Explosion ? kFlashSeconds : kGlowSeconds;
}

// A ring of panels that fades out and narrows, brightest at the bottom.
void addGlow(Vec3 position, float age, std::vector<EffectVertex>& out) {
    const float fade = 1.0f - age;
    const float radius = kGlowRadius * (1.0f - 0.5f * age);
    for (int side = 0; side < kGlowSides; ++side) {
        const float a0 = static_cast<float>(side) * (2.0f * kPi / kGlowSides);
        const float a1 = static_cast<float>(side + 1) * (2.0f * kPi / kGlowSides);
        const Vec3 p0 = position + Vec3{std::cos(a0), std::sin(a0), 0.0f} * radius;
        const Vec3 p1 = position + Vec3{std::cos(a1), std::sin(a1), 0.0f} * radius;
        const Vec3 up{0.0f, 0.0f, kGlowHeight};
        const EffectVertex b0{p0, kGlowColor, 0.8f * fade};
        const EffectVertex b1{p1, kGlowColor, 0.8f * fade};
        const EffectVertex t0{p0 + up, kGlowColor, 0.0f};
        const EffectVertex t1{p1 + up, kGlowColor, 0.0f};
        out.insert(out.end(), {b0, b1, t1, b0, t1, t0});
    }
}

// Three crossed discs that swell and fade, hot in the middle and clear at the edge, so the flash
// looks round from any side.
void addFlash(Vec3 position, float age, std::vector<EffectVertex>& out) {
    const float fade = (1.0f - age) * (1.0f - age);
    const float radius = kFlashStartRadius + (kFlashEndRadius - kFlashStartRadius) * std::sqrt(age);
    const Vec3 axes[3][2] = {{{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
                             {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
                             {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}};
    const EffectVertex centre{position, kFlashCoreColor, fade};
    for (const auto& axis : axes) {
        for (int side = 0; side < kFlashSides; ++side) {
            const float a0 = static_cast<float>(side) * (2.0f * kPi / kFlashSides);
            const float a1 = static_cast<float>(side + 1) * (2.0f * kPi / kFlashSides);
            const Vec3 p0 = position + (axis[0] * std::cos(a0) + axis[1] * std::sin(a0)) * radius;
            const Vec3 p1 = position + (axis[0] * std::cos(a1) + axis[1] * std::sin(a1)) * radius;
            out.insert(out.end(), {centre, {p0, kFlashColor, 0.0f}, {p1, kFlashColor, 0.0f}});
        }
    }
}

} // namespace

void GameEffects::add(std::span<const GameEvent> events, Clock::time_point now) {
    for (const GameEvent& event : events) {
        if (event.kind == GameEventKind::Respawn || event.kind == GameEventKind::Explosion) {
            m_effects.push_back({event.kind, event.position, now});
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
        if (effect.kind == GameEventKind::Explosion) {
            addFlash(effect.position, age, out);
        } else {
            addGlow(effect.position, age, out);
        }
    }
}

} // namespace ct
