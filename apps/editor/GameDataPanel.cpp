#include "GameDataPanel.h"

#include "BehaviourPanel.h"
#include "SettingWidgets.h"

#include "croftech/core/ConfigFile.h"
#include "croftech/core/Log.h"
#include "croftech/level/EntityClasses.h"

#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <span>
#include <utility>
#include <vector>

namespace ct {
namespace {

constexpr ImVec4 kGameColour{0.92f, 0.78f, 0.38f, 1.0f};
constexpr ImVec4 kProblemColour{1.0f, 0.47f, 0.4f, 1.0f};
constexpr float kListWidth = 190.0f;

bool isEnemyFile(const GameConfigFile& file) { return file.path.starts_with("enemies/"); }
bool isWeaponFile(const GameConfigFile& file) { return file.path.starts_with("weapons/"); }

std::string fileName(const GameConfigFile& file) {
    return std::filesystem::path(file.path).stem().string();
}

// How many of the level's enemies and spawners are of the type (the first type's when they name
// none).
std::size_t typeUsers(const Level& level, std::string_view type, std::string_view first) {
    std::size_t count = 0;
    for (const LevelEntity& entity : level.entities) {
        if (entity.className == kEnemyClass || entity.className == kSpawnerClass) {
            const std::string* named = findProperty(entity, "type");
            const std::string_view used = named != nullptr && !named->empty() ? *named : first;
            count += used == type ? 1 : 0;
        }
    }
    return count;
}

// What uses the weapon: the level's pickups of it and its enemies and spawners with it as their
// own, and the game's enemy types armed with it.
struct WeaponUsers {
    std::size_t pickups = 0;
    std::size_t enemies = 0;
    std::vector<std::string> types;
};

WeaponUsers weaponUsers(const Level& level, const GameSettings& settings, std::string_view weapon) {
    WeaponUsers users;
    for (const LevelEntity& entity : level.entities) {
        const std::string* named = findProperty(entity, "weapon");
        if (named == nullptr || *named != weapon) {
            continue;
        }
        if (entity.className == kPickupClass) {
            ++users.pickups;
        } else if (entity.className == kEnemyClass || entity.className == kSpawnerClass) {
            ++users.enemies;
        }
    }
    for (const EnemySettings& type : settings.enemies) {
        if (type.weapon == weapon) {
            users.types.push_back(type.name);
        }
    }
    return users;
}

std::string describeUsers(const WeaponUsers& users) {
    std::string text =
        std::format("{} pickup{} and {} enem{} of its own in this level", users.pickups,
                    users.pickups == 1 ? "" : "s", users.enemies, users.enemies == 1 ? "y" : "ies");
    if (!users.types.empty()) {
        text += "; armed with it:";
        for (const std::string& type : users.types) {
            text += " " + type;
        }
    }
    return text;
}

std::optional<Vec3> parseColour(std::string_view text) {
    float rgb[3]{};
    if (!parseFloats(text, rgb)) {
        return std::nullopt;
    }
    return Vec3{rgb[0], rgb[1], rgb[2]};
}

std::string formatColour(const float rgb[3]) {
    const auto channel = [](float value) {
        return formatFloat(std::round(std::clamp(value, 0.0f, 1.0f) * 100.0f) / 100.0f);
    };
    return std::format("{} {} {}", channel(rgb[0]), channel(rgb[1]), channel(rgb[2]));
}

void keyLabel(const GameDataSetting& setting, std::string_view prefix) {
    const CVar& variable = *setting.variable;
    if (setting.origin == SettingOrigin::Game) {
        ImGui::TextColored(kGameColour, "%s", setting.key.c_str());
    } else {
        ImGui::TextUnformatted(setting.key.c_str());
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        std::string range =
            variable.type() == CVar::Type::String ? std::string() : variable.describeRange();
        ImGui::SetTooltip("%s%s%s\nConsole variable %s%s", variable.description().c_str(),
                          range.empty() ? "" : "\n", range.c_str(), std::string(prefix).c_str(),
                          setting.key.c_str());
    }
}

} // namespace

GameDataPanel::GameDataPanel(GameData& data, std::filesystem::path shownRoot)
    : m_data(data), m_shownRoot(std::move(shownRoot)) {
    if (!data.manifest().enemies.empty()) {
        m_selected = std::format("enemies/{}.cfg", data.manifest().enemies.front());
    } else if (!data.files().empty()) {
        m_selected = data.files().front().path;
    }
}

std::string GameDataPanel::shown(const std::filesystem::path& path) const {
    std::filesystem::path relative = path.lexically_relative(m_shownRoot);
    if (relative.empty() || *relative.begin() == "..") {
        relative = path;
    }
    return relative.generic_string();
}

void GameDataPanel::open(std::string_view path) {
    m_selected = std::string(path);
    m_focusWindow = true;
}

void GameDataPanel::askNewName(bool weapon, std::string_view from) {
    m_newWeapon = weapon;
    m_newFrom = std::string(from);
    m_newName.clear();
    m_problem.clear();
    m_openNewPopup = true;
    m_focusWindow = true;
}

bool GameDataPanel::save() {
    const std::vector<std::string> files = m_data.unsavedFiles();
    std::string error;
    if (!m_data.save(error)) {
        logError("Couldn't save the game data: {}", error);
        return false;
    }
    for (const std::string& file : files) {
        logInfo("Saved {}", file);
    }
    return true;
}

void GameDataPanel::applyPending() {
    if (!m_pending) {
        return;
    }
    PendingEdit edit = std::move(*m_pending);
    m_pending.reset();
    if (edit.reset) {
        m_data.reset(edit.file, edit.key);
        m_mergeWith = 0;
        return;
    }
    std::string error;
    if (!m_data.set(edit.file, edit.key, edit.value, m_mergeWith == edit.row, error)) {
        m_problem = error;
        return;
    }
    m_mergeWith = edit.row;
    m_problem.clear();
}

void GameDataPanel::draw(const EditorDocument& document, bool playing) {
    if (m_focusWindow) {
        ImGui::SetNextWindowFocus();
        m_focusWindow = false;
    }
    if (!ImGui::Begin("Game data")) {
        m_focused = false;
        ImGui::End();
        return;
    }
    m_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (!ImGui::IsAnyItemActive()) {
        m_mergeWith = 0;
    }
    if (m_focused && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) {
            m_data.undo();
        } else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) ||
                   ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)) {
            m_data.redo();
        } else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) {
            save();
        }
    }

    ImGui::BeginDisabled(!m_data.canUndo());
    if (ImGui::Button("Undo")) {
        m_data.undo();
    }
    ImGui::SetItemTooltip("Undo %s (Ctrl+Z here)", std::string(m_data.undoName()).c_str());
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_data.canRedo());
    if (ImGui::Button("Redo")) {
        m_data.redo();
    }
    ImGui::SetItemTooltip("Redo %s (Ctrl+Y here)", std::string(m_data.redoName()).c_str());
    ImGui::EndDisabled();
    ImGui::SameLine();
    const std::vector<std::string> unsaved = m_data.unsavedFiles();
    ImGui::BeginDisabled(unsaved.empty());
    if (ImGui::Button("Save")) {
        save();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (unsaved.empty()) {
        ImGui::TextDisabled("Saved. Changes apply at once, in play too.");
    } else {
        std::string list;
        for (const std::string& file : unsaved) {
            list += std::format("{}{}", list.empty() ? "" : ", ", file);
        }
        ImGui::TextDisabled("Not saved: %s", list.c_str());
    }
    for (const std::string& warning : m_data.warnings()) {
        ImGui::TextColored(kProblemColour, "%s", warning.c_str());
    }
    if (!m_problem.empty()) {
        ImGui::TextColored(kProblemColour, "%s", m_problem.c_str());
    }

    ImGui::BeginChild("files", {kListWidth, 0.0f}, ImGuiChildFlags_Borders);
    drawList();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("file");
    if (const GameConfigFile* found = m_data.findFile(m_selected)) {
        // A copy: an edit replaces the list it is in.
        const GameConfigFile file = *found;
        drawFile(file, document, playing);
        applyPending();
    } else {
        ImGui::TextDisabled("Choose an enemy type, weapon or part of the game on the left.");
    }
    ImGui::EndChild();
    drawNewPopup(playing);
    ImGui::End();
}

void GameDataPanel::drawList() {
    const auto row = [&](const GameConfigFile& file, const std::string& label) {
        const std::string text =
            std::format("{}{}###{}", label, m_data.isUnsaved(file) ? " *" : "", file.path);
        if (ImGui::Selectable(text.c_str(), m_selected == file.path)) {
            m_selected = file.path;
        }
    };
    ImGui::SeparatorText("Enemy types");
    for (const GameConfigFile& file : m_data.files()) {
        if (isEnemyFile(file)) {
            row(file, fileName(file));
            if (!m_data.isGameOwned(file)) {
                ImGui::SetItemTooltip("Comes with the engine; the game's changes are its own");
            }
        }
    }
    if (ImGui::SmallButton("New enemy type...")) {
        const GameConfigFile* shownFile = m_data.findFile(m_selected);
        newEnemyType(shownFile != nullptr && isEnemyFile(*shownFile)
                         ? fileName(*shownFile)
                         : (m_data.manifest().enemies.empty() ? std::string()
                                                              : m_data.manifest().enemies.front()));
    }
    ImGui::SeparatorText("Weapons");
    std::size_t key = 0;
    for (const GameConfigFile& file : m_data.files()) {
        if (isWeaponFile(file)) {
            ++key;
            row(file, std::format("{}  {}", key, fileName(file)));
            ImGui::SetItemTooltip("Number key %zu%s", key,
                                  m_data.isGameOwned(file)
                                      ? ""
                                      : "\nComes with the engine; the game's changes are its own");
        }
    }
    if (ImGui::SmallButton("New weapon...")) {
        const GameConfigFile* shownFile = m_data.findFile(m_selected);
        const std::vector<std::string>& weapons = m_data.manifest().weapons;
        askNewName(true, shownFile != nullptr && isWeaponFile(*shownFile)
                             ? fileName(*shownFile)
                             : (weapons.empty() ? std::string() : weapons.front()));
    }
    ImGui::SeparatorText("Game");
    for (const GameConfigFile& file : m_data.files()) {
        if (file.path.starts_with("config/")) {
            row(file, fileName(file));
        }
    }
}

void GameDataPanel::drawFile(const GameConfigFile& file, const EditorDocument& document,
                             bool playing) {
    const std::string name = fileName(file);
    const bool owned = m_data.isGameOwned(file);
    const bool enemy = isEnemyFile(file);
    const bool weapon = isWeaponFile(file);
    ImGui::Text("%s", name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", enemy ? "enemy type" : weapon ? "weapon" : "game settings");
    if (owned) {
        ImGui::TextDisabled("The game's own: %s", shown(m_data.savePath(file)).c_str());
    } else {
        ImGui::TextDisabled("From the engine's content; the game's changes (in gold) are saved to "
                            "%s",
                            shown(m_data.savePath(file)).c_str());
    }

    if (enemy) {
        drawEnemyButtons(name, owned, document, playing);
    } else if (weapon) {
        drawWeaponButtons(name, owned, document, playing);
    }

    const std::vector<GameDataSetting> settings = m_data.settingsOf(file);
    const auto table = [&](const char* id, auto&& rows) {
        constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
        if (ImGui::BeginTable(id, 3, kFlags)) {
            ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("From", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            rows();
            ImGui::EndTable();
        }
    };
    if (!enemy) {
        table("settings", [&] {
            for (const GameDataSetting& setting : settings) {
                drawSetting(file, setting, owned);
            }
        });
        return;
    }
    std::vector<bool> drawn(settings.size(), false);
    const auto drawGroup = [&](std::string_view title, auto&& wanted) {
        ImGui::SeparatorText(std::string(title).c_str());
        table(std::string(title).c_str(), [&] {
            for (std::size_t i = 0; i < settings.size(); ++i) {
                if (!drawn[i] && wanted(settings[i].key)) {
                    drawn[i] = true;
                    drawSetting(file, settings[i], owned);
                }
            }
        });
    };
    for (const SettingGroup& group : enemySettingGroups()) {
        drawGroup(group.title, [&](std::string_view key) {
            return std::find(group.keys.begin(), group.keys.end(), key) != group.keys.end();
        });
    }
    if (std::find(drawn.begin(), drawn.end(), false) != drawn.end()) {
        drawGroup("Other", [](std::string_view) { return true; });
    }
}

void GameDataPanel::drawEnemyButtons(const std::string& name, bool owned,
                                     const EditorDocument& document, bool playing) {
    const std::vector<std::string>& enemies = m_data.manifest().enemies;
    const std::size_t users =
        typeUsers(document.level(), name, enemies.empty() ? "" : enemies.front());
    ImGui::BeginDisabled(playing);
    if (ImGui::Button("New type from this...")) {
        askNewName(false, name);
    }
    ImGui::SetItemTooltip("A new enemy type with every setting of the %s, to change as you like",
                          name.c_str());
    if (owned) {
        ImGui::SameLine();
        if (ImGui::Button("Delete type...")) {
            ImGui::OpenPopup("Delete");
        }
    }
    ImGui::EndDisabled();
    if (m_behaviours != nullptr) {
        std::string behaviour = "standard";
        for (const EnemySettings& type : m_data.settings().enemies) {
            if (type.name == name && !type.behaviour.empty()) {
                behaviour = type.behaviour;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Edit behaviour")) {
            m_behaviours->open(behaviour);
        }
        ImGui::SetItemTooltip("Shows the %s's behaviour tree, %s, in the Behaviours window",
                              name.c_str(), behaviour.c_str());
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu in this level%s", users,
                        playing ? "; types can't be made or deleted while playing" : "");
    drawDeletePopup(document);
}

void GameDataPanel::drawWeaponButtons(const std::string& name, bool owned,
                                      const EditorDocument& document, bool playing) {
    const std::vector<std::string>& weapons = m_data.manifest().weapons;
    const std::size_t place =
        static_cast<std::size_t>(std::find(weapons.begin(), weapons.end(), name) - weapons.begin());
    ImGui::BeginDisabled(playing);
    if (ImGui::Button("New weapon from this...")) {
        askNewName(true, name);
    }
    ImGui::SetItemTooltip("A new weapon with every setting of the %s, to change as you like",
                          name.c_str());
    if (owned) {
        ImGui::SameLine();
        if (ImGui::Button("Delete weapon...")) {
            ImGui::OpenPopup("Delete");
        }
    }
    // The list's order is the number keys'.
    const auto move = [&](std::size_t to) {
        std::string error;
        if (!m_data.moveWeapon(name, to, error)) {
            m_problem = error;
        }
    };
    ImGui::SameLine();
    ImGui::BeginDisabled(place == 0 || place >= weapons.size());
    if (ImGui::ArrowButton("earlier", ImGuiDir_Up)) {
        move(place - 1);
    }
    ImGui::SetItemTooltip("Number key %zu", place);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(place + 1 >= weapons.size());
    if (ImGui::ArrowButton("later", ImGuiDir_Down)) {
        move(place + 1);
    }
    ImGui::SetItemTooltip("Number key %zu", place + 2);
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("Number key %zu%s", place + 1,
                        playing ? "; weapons can't be made, deleted or moved while playing" : "");
    ImGui::TextDisabled(
        "%s", describeUsers(weaponUsers(document.level(), m_data.settings(), name)).c_str());
    drawDeletePopup(document);
}

void GameDataPanel::drawSetting(const GameConfigFile& file, const GameDataSetting& setting,
                                bool owned) {
    ImGui::PushID(setting.key.c_str());
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    keyLabel(setting, file.prefix);
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
    std::string value;
    const ImGuiID row = ImGui::GetID("row");
    if (drawValue(file, setting, value)) {
        m_pending = PendingEdit{file, setting.key, std::move(value), false, row};
    }
    ImGui::TableNextColumn();
    if (setting.origin == SettingOrigin::Game && !owned) {
        if (ImGui::SmallButton("Reset")) {
            m_pending = PendingEdit{file, setting.key, {}, true, row};
        }
        ImGui::SetItemTooltip("Back to the engine's value: %s",
                              setting.baseValue.empty() ? "(empty)" : setting.baseValue.c_str());
    } else {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", setting.origin == SettingOrigin::Game   ? "its file"
                                  : setting.origin == SettingOrigin::Base ? "engine"
                                                                          : "default");
    }
    ImGui::PopID();
}

bool GameDataPanel::drawValue(const GameConfigFile& file, const GameDataSetting& setting,
                              std::string& value) {
    const CVar& variable = *setting.variable;
    const bool limited =
        variable.minimum() > -CVar::kNoLimit && variable.maximum() < CVar::kNoLimit;
    switch (variable.type()) {
    case CVar::Type::Bool: {
        bool on = variable.asBool();
        if (ImGui::Checkbox("##value", &on)) {
            value = on ? "1" : "0";
            return true;
        }
        return false;
    }
    case CVar::Type::Int: {
        int number = variable.asInt();
        if (dragWholeNumber("##value", number, limited ? static_cast<int>(variable.minimum()) : 0,
                            limited ? static_cast<int>(variable.maximum()) : 0)) {
            value = std::format("{}", number);
            return true;
        }
        return false;
    }
    case CVar::Type::Float: {
        float number = variable.asFloat();
        if (dragNumber("##value", number, limited ? variable.minimum() : 0.0f,
                       limited ? variable.maximum() : 0.0f)) {
            value = formatFloat(number);
            return true;
        }
        return false;
    }
    case CVar::Type::String:
        break;
    }

    const bool enemy = isEnemyFile(file);
    if (enemy && setting.key == "colour") {
        const std::optional<Vec3> colour = parseColour(variable.text());
        float rgb[3]{0.5f, 0.5f, 0.5f};
        if (colour) {
            rgb[0] = colour->x;
            rgb[1] = colour->y;
            rgb[2] = colour->z;
        }
        bool changed = ImGui::ColorEdit3("##value", rgb, ImGuiColorEditFlags_Float);
        if (changed) {
            value = formatColour(rgb);
        }
        if (!colour) {
            ImGui::SameLine();
            ImGui::TextDisabled("(picked by its place in the list)");
        }
        return changed;
    }
    const bool sounds = setting.key == "sounds" && (enemy || isWeaponFile(file));
    if ((enemy && setting.key == "weapon") || sounds) {
        const bool weapon = !sounds;
        std::vector<std::string> names;
        if (weapon) {
            for (const WeaponSettings& each : m_data.settings().weapons) {
                names.push_back(each.name);
            }
        } else {
            names = enemy ? m_data.manifest().enemies : m_data.manifest().weapons;
        }
        const std::string own = std::format("(its own: {})", fileName(file));
        const char* none = weapon ? "(none: no ranged attack)" : own.c_str();
        const std::string& current = variable.text();
        bool changed = false;
        if (ImGui::BeginCombo("##value", current.empty() ? none : current.c_str())) {
            if (ImGui::Selectable(none, current.empty())) {
                value.clear();
                changed = true;
            }
            for (const std::string& name : names) {
                if (ImGui::Selectable(name.c_str(), name == current)) {
                    value = name;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    // Text, kept as typed while the field has the keyboard.
    const std::string id = file.path + "|" + setting.key;
    std::string text = m_textKey == id ? m_text : variable.text();
    const bool changed = ImGui::InputText("##value", &text);
    if (ImGui::IsItemActivated()) {
        m_textKey = id;
    }
    if (m_textKey == id) {
        m_text = text;
        if (ImGui::IsItemDeactivated()) {
            m_textKey.clear();
        }
    }
    if (changed) {
        value = text;
    }
    return changed;
}

void GameDataPanel::drawNewPopup(bool playing) {
    if (m_openNewPopup) {
        ImGui::OpenPopup("New");
        m_openNewPopup = false;
    }
    if (!ImGui::BeginPopup("New")) {
        return;
    }
    const char* noun = m_newWeapon ? "weapon" : "enemy type";
    if (playing) {
        ImGui::TextDisabled("Stop playing first: play numbers the %ss as it starts.", noun);
        ImGui::EndPopup();
        return;
    }
    ImGui::SeparatorText(m_newWeapon ? "New weapon" : "New enemy type");
    ImGui::TextUnformatted("Name (lowercase letters, digits, '-' and '_'):");
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    const bool entered =
        ImGui::InputText("##name", &m_newName, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::TextUnformatted("Made from:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::BeginCombo("##from", m_newFrom.c_str())) {
        for (const std::string& type :
             m_newWeapon ? m_data.manifest().weapons : m_data.manifest().enemies) {
            if (ImGui::Selectable(type.c_str(), type == m_newFrom)) {
                m_newFrom = type;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::TextDisabled("It gets every setting of the %s and sounds like it.", m_newFrom.c_str());
    if (m_newWeapon) {
        ImGui::TextDisabled("It takes number key %zu.", m_data.manifest().weapons.size() + 1);
    }
    if (!m_problem.empty()) {
        ImGui::TextColored(kProblemColour, "%s", m_problem.c_str());
    }
    if ((ImGui::Button("Make") || entered) && !m_newName.empty()) {
        std::string error;
        const bool made = m_newWeapon ? m_data.createWeapon(m_newName, m_newFrom, error)
                                      : m_data.createEnemyType(m_newName, m_newFrom, error);
        if (made) {
            m_selected = std::format("{}/{}.cfg", m_newWeapon ? "weapons" : "enemies", m_newName);
            m_problem.clear();
            logInfo("New {} {}, from the {}; save the game data to keep it", noun, m_newName,
                    m_newFrom);
            ImGui::CloseCurrentPopup();
        } else {
            m_problem = error;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        m_problem.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void GameDataPanel::drawDeletePopup(const EditorDocument& document) {
    if (!ImGui::BeginPopup("Delete")) {
        return;
    }
    const GameConfigFile* file = m_data.findFile(m_selected);
    if (file == nullptr) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    const std::string name = fileName(*file);
    const bool weapon = isWeaponFile(*file);
    ImGui::Text("Delete the %s? Its file goes when the game data is saved.", name.c_str());
    if (weapon) {
        const WeaponUsers users = weaponUsers(document.level(), m_data.settings(), name);
        if (users.pickups + users.enemies > 0 || !users.types.empty()) {
            ImGui::TextColored(kProblemColour, "%s.", describeUsers(users).c_str());
            ImGui::TextColored(kProblemColour,
                               "Those pickups give nothing, and those enemies don't shoot.");
        }
        ImGui::TextDisabled("The weapons after it move down a number key.");
    } else {
        const std::vector<std::string>& enemies = m_data.manifest().enemies;
        const std::size_t users =
            typeUsers(document.level(), name, enemies.empty() ? "" : enemies.front());
        if (users > 0) {
            ImGui::TextColored(kProblemColour,
                               "%zu enemies and spawners in this level use it; they won't play.",
                               users);
        }
    }
    if (ImGui::Button("Delete")) {
        std::string error;
        const bool deleted =
            weapon ? m_data.deleteWeapon(name, error) : m_data.deleteEnemyType(name, error);
        if (deleted) {
            const std::vector<std::string>& left =
                weapon ? m_data.manifest().weapons : m_data.manifest().enemies;
            m_selected = left.empty() ? std::string()
                                      : std::format("{}/{}.cfg", weapon ? "weapons" : "enemies",
                                                    left.front());
        } else {
            m_problem = error;
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace ct
