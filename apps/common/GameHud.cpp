#include "GameHud.h"

#include "croftech/sim/Damage.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <string_view>

namespace ct {
namespace {

constexpr float kFlashSeconds = 0.5f;
constexpr std::int32_t kLowHealth = 25;

const char* deathMessage(DamageKind cause) {
    switch (cause) {
    case DamageKind::Hazard:
        return "Killed by a hazard";
    case DamageKind::OutOfWorld:
        return "You fell out of the world";
    case DamageKind::Generic:
        break;
    }
    return "You died";
}

void drawCentredText(ImDrawList& draw, float size, ImVec2 centre, ImU32 colour,
                     std::string_view text) {
    ImFont* font = ImGui::GetFont();
    const ImVec2 extent =
        font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.data(), text.data() + text.size());
    draw.AddText(font, size, {centre.x - extent.x * 0.5f, centre.y - extent.y * 0.5f}, colour,
                 text.data(), text.data() + text.size());
}

} // namespace

bool GameHud::update(const State* latest, EntityId player, const RulesSettings& rules,
                     Clock::time_point now) {
    const Health* health = latest != nullptr ? latest->world.healths.get(player) : nullptr;
    if (health == nullptr) {
        m_known = false;
        return false;
    }
    const bool wasDead = m_known && m_dead;
    if (m_known && health->hurtTick != m_hurtTick && health->health < m_health) {
        const float lost = static_cast<float>(m_health - health->health);
        m_flashStart = now;
        m_flashStrength = std::clamp(0.2f + lost / 100.0f, 0.2f, 0.6f);
    }
    m_known = true;
    m_dead = isDead(*health);
    m_canRespawn = canRespawn(*health, rules, latest->tick);
    m_health = health->health;
    m_armour = health->armour;
    m_hurtTick = health->hurtTick;
    m_cause = static_cast<DamageKind>(health->hurtKind);
    return wasDead && !m_dead;
}

void GameHud::draw(float x, float y, float width, float height, Clock::time_point now) const {
    if (!m_known) {
        return;
    }
    ImDrawList& draw = *ImGui::GetBackgroundDrawList();
    const ImVec2 min{x, y};
    const ImVec2 max{x + width, y + height};

    const float flashAge = std::chrono::duration<float>(now - m_flashStart).count();
    if (flashAge < kFlashSeconds) {
        const float alpha = m_flashStrength * (1.0f - flashAge / kFlashSeconds);
        draw.AddRectFilled(min, max, ImGui::GetColorU32({0.8f, 0.0f, 0.0f, alpha}));
    }

    const float big = std::max(height * 0.06f, 20.0f);
    const float margin = big * 0.6f;
    char text[32];
    std::snprintf(text, sizeof text, "%d", static_cast<int>(std::max(m_health, 0)));
    const ImU32 healthColour =
        m_health <= kLowHealth ? IM_COL32(255, 70, 60, 255) : IM_COL32(255, 255, 255, 235);
    ImFont* font = ImGui::GetFont();
    const float small = big * 0.4f;
    draw.AddText(font, small, {min.x + margin, max.y - margin - big - small},
                 IM_COL32(255, 255, 255, 160), "HEALTH");
    draw.AddText(font, big, {min.x + margin, max.y - margin - big}, healthColour, text);
    if (m_armour > 0) {
        const float armourX = min.x + margin + big * 3.0f;
        std::snprintf(text, sizeof text, "%d", static_cast<int>(m_armour));
        draw.AddText(font, small, {armourX, max.y - margin - big - small},
                     IM_COL32(255, 255, 255, 160), "ARMOUR");
        draw.AddText(font, big, {armourX, max.y - margin - big}, IM_COL32(120, 190, 255, 235),
                     text);
    }

    if (m_dead) {
        draw.AddRectFilled(min, max, IM_COL32(60, 0, 0, 110));
        const ImVec2 centre{min.x + width * 0.5f, min.y + height * 0.42f};
        drawCentredText(draw, big, centre, IM_COL32(255, 255, 255, 255), deathMessage(m_cause));
        if (m_canRespawn) {
            drawCentredText(draw, big * 0.5f, {centre.x, centre.y + big * 1.2f},
                            IM_COL32(255, 255, 255, 200), "Press fire to respawn");
        }
    }
}

} // namespace ct
