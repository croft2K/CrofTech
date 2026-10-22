#include "GameHud.h"

#include "croftech/sim/Damage.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <string_view>

namespace ct {
namespace {

constexpr float kFlashSeconds = 0.5f;
constexpr float kHitMarkSeconds = 0.25f;
constexpr std::int32_t kLowHealth = 25;
constexpr ImU32 kLabelColour = IM_COL32(255, 255, 255, 160);

// "rocket_launcher" as "ROCKET LAUNCHER".
std::string displayName(std::string_view name) {
    std::string text(name);
    for (char& c : text) {
        c = c == '_' || c == '-' ? ' '
                                 : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return text;
}

// Four short lines around the middle with a gap, outlined so they show on any background.
void drawCrosshair(ImDrawList& draw, ImVec2 centre, float size, ImU32 colour) {
    const float gap = size * 0.35f;
    const ImVec2 directions[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int pass = 0; pass < 2; ++pass) {
        const ImU32 c = pass == 0 ? IM_COL32(0, 0, 0, 140) : colour;
        const float thickness = pass == 0 ? 4.0f : 2.0f;
        for (const ImVec2 d : directions) {
            draw.AddLine({centre.x + d.x * gap, centre.y + d.y * gap},
                         {centre.x + d.x * size, centre.y + d.y * size}, c, thickness);
        }
    }
}

// Four diagonal ticks further out, briefly, when a shot of yours hurts someone.
void drawHitMark(ImDrawList& draw, ImVec2 centre, float size, float alpha) {
    const ImU32 colour = ImGui::GetColorU32({1.0f, 1.0f, 1.0f, alpha});
    for (const ImVec2 d : {ImVec2{1, 1}, ImVec2{-1, 1}, ImVec2{1, -1}, ImVec2{-1, -1}}) {
        draw.AddLine({centre.x + d.x * size * 0.7f, centre.y + d.y * size * 0.7f},
                     {centre.x + d.x * size * 1.3f, centre.y + d.y * size * 1.3f}, colour, 2.0f);
    }
}

const char* deathMessage(DamageKind cause) {
    switch (cause) {
    case DamageKind::Hazard:
        return "Killed by a hazard";
    case DamageKind::OutOfWorld:
        return "You fell out of the world";
    case DamageKind::Weapon:
        return "You were shot";
    case DamageKind::Melee:
        return "You were struck down";
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

bool GameHud::update(const State* latest, EntityId player, const GameSettings& settings,
                     Clock::time_point now) {
    const RulesSettings& rules = settings.rules;
    const Health* health = latest != nullptr ? latest->world.healths.get(player) : nullptr;
    if (health == nullptr) {
        m_known = false;
        return false;
    }
    const bool wasDead = m_known && m_dead;
    const bool wasKnownBefore = m_known;
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

    const World& world = latest->world;
    // A loaded save goes back in time; hits are counted again from there, and the ones already
    // in it make no mark.
    const bool rewound = latest->tick < m_seenTick;
    if (rewound) {
        m_hitTick = 0;
    }
    m_seenTick = latest->tick;
    const std::span<const EntityId> hurtable = world.healths.entities();
    for (std::size_t i = 0; i < hurtable.size(); ++i) {
        const Health& other = world.healths.components()[i];
        if (hurtable[i] != player && other.attacker == player && other.hurtTick > m_hitTick) {
            if (wasKnownBefore && !rewound) {
                m_hitStart = now;
            }
            m_hitTick = other.hurtTick;
        }
    }

    const Weapons* weapons = world.weapons.get(player);
    m_weaponCount =
        static_cast<std::uint32_t>(std::min<std::size_t>(settings.weapons.size(), kMaxWeapons));
    m_armed = weapons != nullptr && weapons->owned != 0 && m_weaponCount > 0;
    if (m_armed) {
        m_current = weapons->current;
        m_owned = weapons->owned;
        for (std::uint32_t i = 0; i < m_weaponCount; ++i) {
            const WeaponSettings& weapon = settings.weapons[i];
            m_ammo[i] = weapons->ammo[i];
            m_endless[i] = weapon.ammoPerShot <= 0;
            m_empty[i] = !m_endless[i] && weapons->ammo[i] < weapon.ammoPerShot;
        }
    }
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

    if (!m_dead) {
        const ImVec2 centre{min.x + width * 0.5f, min.y + height * 0.5f};
        const float size = std::max(height * 0.012f, 7.0f);
        drawCrosshair(draw, centre, size, IM_COL32(255, 255, 255, 220));
        const float hitAge = std::chrono::duration<float>(now - m_hitStart).count();
        if (hitAge < kHitMarkSeconds) {
            drawHitMark(draw, centre, size * 1.6f, 1.0f - hitAge / kHitMarkSeconds);
        }
        if (m_armed) {
            drawWeapons(draw, max.x - margin, max.y - margin, big);
        }
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

void GameHud::drawWeapons(ImDrawList& draw, float right, float bottom, float big) const {
    ImFont* font = ImGui::GetFont();
    const float small = big * 0.4f;
    char text[32];
    if (m_current < m_weaponCount) {
        if (m_endless[m_current]) {
            std::snprintf(text, sizeof text, "--");
        } else {
            std::snprintf(text, sizeof text, "%d", static_cast<int>(m_ammo[m_current]));
        }
        const float ammoWidth = font->CalcTextSizeA(big, FLT_MAX, 0.0f, text).x;
        const ImU32 ammoColour =
            m_empty[m_current] ? IM_COL32(255, 70, 60, 255) : IM_COL32(255, 255, 255, 235);
        draw.AddText(font, big, {right - ammoWidth, bottom - big}, ammoColour, text);
        const std::string name = m_current < m_weaponNames.size()
                                     ? displayName(m_weaponNames[m_current])
                                     : std::string("WEAPON");
        const float nameWidth = font->CalcTextSizeA(small, FLT_MAX, 0.0f, name.c_str()).x;
        draw.AddText(font, small, {right - nameWidth, bottom - big - small}, kLabelColour,
                     name.c_str());
    }

    // A box per weapon carried, numbered by its key; the one in hand is lit, empty ones dim.
    const float box = small * 1.6f;
    const float gap = box * 0.25f;
    const float top = bottom - big - small * 2.0f - box;
    float x = right - static_cast<float>(m_weaponCount) * (box + gap) + gap;
    for (std::uint32_t i = 0; i < m_weaponCount; ++i, x += box + gap) {
        if ((m_owned & (1u << i)) == 0) {
            continue;
        }
        const bool inHand = i == m_current;
        draw.AddRectFilled({x, top}, {x + box, top + box},
                           inHand ? IM_COL32(255, 255, 255, 200) : IM_COL32(0, 0, 0, 110), 3.0f);
        std::snprintf(text, sizeof text, "%u", static_cast<unsigned>(i + 1));
        const ImVec2 extent = font->CalcTextSizeA(small, FLT_MAX, 0.0f, text);
        const ImU32 colour = inHand       ? IM_COL32(0, 0, 0, 255)
                             : m_empty[i] ? IM_COL32(255, 255, 255, 70)
                                          : IM_COL32(255, 255, 255, 220);
        draw.AddText(font, small, {x + (box - extent.x) * 0.5f, top + (box - extent.y) * 0.5f},
                     colour, text);
    }
}

} // namespace ct
