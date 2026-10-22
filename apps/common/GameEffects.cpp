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

} // namespace

void GameEffects::add(std::span<const GameEvent> events, Clock::time_point now) {
    for (const GameEvent& event : events) {
        if (event.kind == GameEventKind::Respawn) {
            m_effects.push_back({event.position, now});
        }
    }
}

void GameEffects::build(Clock::time_point now, std::vector<EffectVertex>& out) {
    std::erase_if(m_effects, [&](const Effect& effect) {
        return std::chrono::duration<float>(now - effect.start).count() > kGlowSeconds;
    });
    for (const Effect& effect : m_effects) {
        // A ring of panels that fades out and narrows, brightest at the bottom.
        const float age = std::chrono::duration<float>(now - effect.start).count() / kGlowSeconds;
        const float fade = 1.0f - age;
        const float radius = kGlowRadius * (1.0f - 0.5f * age);
        for (int side = 0; side < kGlowSides; ++side) {
            const float a0 = static_cast<float>(side) * (2.0f * kPi / kGlowSides);
            const float a1 = static_cast<float>(side + 1) * (2.0f * kPi / kGlowSides);
            const Vec3 p0 = effect.position + Vec3{std::cos(a0), std::sin(a0), 0.0f} * radius;
            const Vec3 p1 = effect.position + Vec3{std::cos(a1), std::sin(a1), 0.0f} * radius;
            const Vec3 up{0.0f, 0.0f, kGlowHeight};
            const EffectVertex b0{p0, kGlowColor, 0.8f * fade};
            const EffectVertex b1{p1, kGlowColor, 0.8f * fade};
            const EffectVertex t0{p0 + up, kGlowColor, 0.0f};
            const EffectVertex t1{p1 + up, kGlowColor, 0.0f};
            out.insert(out.end(), {b0, b1, t1, b0, t1, t0});
        }
    }
}

} // namespace ct
