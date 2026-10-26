#include "BehaviourPanel.h"

#include "ScriptPanel.h"
#include "SettingWidgets.h"

#include "croftech/core/Camera.h"
#include "croftech/core/ConfigFile.h"
#include "croftech/core/Log.h"
#include "croftech/script/ScriptLibrary.h"
#include "croftech/sim/Enemies.h"
#include "croftech/sim/Health.h"

#include <imgui-node-editor/imgui_node_editor.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <format>
#include <utility>

namespace ct {
namespace {

namespace ed = ax::NodeEditor;

constexpr ImVec4 kProblemColour{1.0f, 0.47f, 0.4f, 1.0f};
constexpr ImVec4 kRanColour{0.35f, 1.0f, 0.45f, 1.0f};
constexpr ImVec4 kFailedColour{1.0f, 0.62f, 0.2f, 1.0f};
constexpr ImVec4 kBorderColour{1.0f, 1.0f, 1.0f, 0.25f};
constexpr ImVec4 kLinkColour{0.75f, 0.75f, 0.8f, 0.7f};
constexpr float kDetailsWidth = 300.0f;
constexpr float kColumnGap = 70.0f;
constexpr float kRowGap = 12.0f;
constexpr float kNoteWidth = 230.0f;
constexpr BehaviourBox kGuessedSize{0.0f, 0.0f, 120.0f, 40.0f};
// How long a node stays lit after it last ran, so one that runs for a tick can be seen.
constexpr double kLitSeconds = 0.25;
// Nodes are measured as they are drawn, laid out from that the next frame, and the graph knows
// where they went the frame after: only then can it show them all.
constexpr int kFramesToShowAll = 3;

ImVec4 categoryColour(BehaviourCategory category) {
    switch (category) {
    case BehaviourCategory::Composite:
        return {0.45f, 0.65f, 1.0f, 1.0f};
    case BehaviourCategory::Decorator:
        return {0.78f, 0.6f, 1.0f, 1.0f};
    case BehaviourCategory::Condition:
        return {0.95f, 0.82f, 0.4f, 1.0f};
    case BehaviourCategory::Action:
        break;
    }
    return {0.5f, 0.88f, 0.55f, 1.0f};
}

const char* categoryName(BehaviourCategory category) {
    switch (category) {
    case BehaviourCategory::Composite:
        return "Composites: choose which of their children run";
    case BehaviourCategory::Decorator:
        return "Decorators: change what their one child does";
    case BehaviourCategory::Condition:
        return "Conditions: ask a question";
    case BehaviourCategory::Action:
        break;
    }
    return "Actions: do something";
}

// Ids for the graph: each node has its own, its two pins' and the link to it from its parent.
ed::NodeId nodeId(std::uint32_t node) { return ed::NodeId(std::uintptr_t{node} * 4 + 1); }
ed::PinId inputPin(std::uint32_t node) { return ed::PinId(std::uintptr_t{node} * 4 + 2); }
ed::PinId outputPin(std::uint32_t node) { return ed::PinId(std::uintptr_t{node} * 4 + 3); }
ed::LinkId linkTo(std::uint32_t node) { return ed::LinkId(std::uintptr_t{node} * 4 + 4); }
std::uint32_t nodeOf(std::uintptr_t id) { return static_cast<std::uint32_t>((id - 1) / 4); }
bool isOutputPin(ed::PinId pin) { return (pin.Get() - 1) % 4 == 2; }

BehaviourTree singleNode(BehaviourNodeKind kind) {
    BehaviourTree tree;
    BehaviourNode node;
    node.kind = kind;
    node.value = behaviourNodeInfo(kind).parameter.defaultValue;
    node.end = 1;
    tree.nodes.push_back(std::move(node));
    numberBehaviourMemory(tree);
    return tree;
}

std::string settingText(const BehaviourNode& node) {
    const BehaviourParameter& parameter = behaviourNodeInfo(node.kind).parameter;
    if (parameter.script) {
        return node.script;
    }
    if (parameter.name.empty()) {
        return {};
    }
    return parameter.unit.empty() ? formatFloat(node.value)
                                  : std::format("{} {}", formatFloat(node.value), parameter.unit);
}

// The kinds of node the allowed ones may be, by category, those whose names hold the filter
// (typed in the field at the top). Returns the one clicked.
template <typename Allowed>
std::optional<BehaviourNodeKind> chooseKind(std::string& filter, bool& focusFilter,
                                            Allowed&& allowed) {
    if (focusFilter) {
        ImGui::SetKeyboardFocusHere();
        focusFilter = false;
    }
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputTextWithHint("##filter", "Search", &filter);
    std::optional<BehaviourNodeKind> chosen;
    std::optional<BehaviourCategory> heading;
    for (const BehaviourNodeInfo& info : behaviourNodes()) {
        if (!allowed(info) || info.name.find(filter) == std::string_view::npos) {
            continue;
        }
        if (heading != info.category) {
            heading = info.category;
            ImGui::SeparatorText(categoryName(info.category));
        }
        ImGui::PushStyleColor(ImGuiCol_Text, categoryColour(info.category));
        const bool clicked = ImGui::Selectable(std::string(info.name).c_str());
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
            ImGui::SetNextWindowSize({360.0f, 0.0f});
            ImGui::BeginTooltip();
            ImGui::TextWrapped("%s", std::string(info.description).c_str());
            ImGui::EndTooltip();
        }
        if (clicked) {
            chosen = info.kind;
            filter.clear();
        }
    }
    return chosen;
}

bool hasBit(const std::array<std::uint32_t, kMaxBehaviourNodes / 32>& bits, std::uint32_t node) {
    return node < kMaxBehaviourNodes && ((bits[node / 32] >> (node % 32)) & 1u) != 0;
}

} // namespace

BehaviourPanel::BehaviourPanel(GameData& data) : m_data(data) {
    ed::Config config;
    config.SettingsFile = nullptr; // the layout comes from the tree, so there is nothing to keep
    m_editor = ed::CreateEditor(&config);
    ed::SetCurrentEditor(m_editor);
    ed::Style& style = ed::GetStyle();
    style.NodeRounding = 5.0f;
    style.NodePadding = {10.0f, 8.0f, 10.0f, 8.0f};
    style.LinkStrength = 60.0f;
    style.SourceDirection = {1.0f, 0.0f};
    style.TargetDirection = {-1.0f, 0.0f};
    style.Colors[ed::StyleColor_Bg] = {0.11f, 0.11f, 0.13f, 1.0f};
    style.Colors[ed::StyleColor_NodeBg] = {0.17f, 0.17f, 0.2f, 0.96f};
    ed::SetCurrentEditor(nullptr);
}

BehaviourPanel::~BehaviourPanel() { ed::DestroyEditor(m_editor); }

void BehaviourPanel::open(std::string_view name) {
    m_name = std::string(name);
    m_focusWindow = true;
}

void BehaviourPanel::refresh() {
    const std::uint64_t scripts = m_scripts != nullptr ? m_scripts->generation() : 0;
    if (m_seenChange == m_data.changeCount() && m_seenName == m_name && m_seenScripts == scripts) {
        return;
    }
    const bool otherTree = m_seenName != m_name;
    m_seenChange = m_data.changeCount();
    m_seenScripts = scripts;
    m_seenName = m_name;
    m_tree.reset();
    m_readError.clear();
    m_problems.clear();
    m_scriptProblems.clear();
    const std::optional<std::string> text = m_data.behaviourText(m_name);
    if (!text) {
        m_readError = std::format("There is no behaviours/{}.behaviour.", m_name);
    } else {
        m_tree = parseBehaviourTree(*text, m_name, m_readError, BehaviourReading::Draft);
    }
    const std::size_t count = m_tree ? m_tree->nodes.size() : 0;
    if (m_tree) {
        m_problems = behaviourProblems(*m_tree);
        m_parents = behaviourParents(*m_tree);
        for (std::uint32_t i = 0; i < count && m_scripts != nullptr; ++i) {
            const std::string& script = m_tree->nodes[i].script;
            if (!behaviourNodeInfo(m_tree->nodes[i].kind).parameter.script || script.empty()) {
                continue;
            }
            std::string problem;
            if (const ScriptProgram* program = m_scripts->find(script)) {
                problem = behaviourScriptProblem(*program);
            } else if (m_scripts->path(script).empty()) {
                problem = std::format("there is no scripts/{}.luau", script);
            } else {
                problem = std::format("scripts/{}.luau doesn't load: {}", script,
                                      m_scripts->error(script));
            }
            if (!problem.empty()) {
                m_scriptProblems.push_back({i, std::move(problem)});
            }
        }
    }
    m_shownProblems = m_problems;
    m_shownProblems.insert(m_shownProblems.end(), m_scriptProblems.begin(), m_scriptProblems.end());
    std::stable_sort(
        m_shownProblems.begin(), m_shownProblems.end(),
        [](const BehaviourProblem& a, const BehaviourProblem& b) { return a.node < b.node; });
    m_sizes.resize(count, kGuessedSize);
    m_placed.clear();
    m_litUntil.assign(count, 0.0);
    m_failedUntil.assign(count, 0.0);
    m_lights.assign(count, NodeLight::Off);
    if (otherTree) {
        m_sizes.assign(count, kGuessedSize);
        m_frameAll = kFramesToShowAll;
        m_select = kNoBehaviourParent;
        m_shown = kNoBehaviourParent;
    }
    if (m_shown >= count) {
        m_shown = kNoBehaviourParent;
    }
}

void BehaviourPanel::edit(BehaviourTree tree, std::string name, std::optional<std::uint32_t> select,
                          bool merge) {
    m_pending = PendingEdit{std::move(tree), std::move(name), merge, select};
}

void BehaviourPanel::applyPending() {
    if (!m_pending) {
        return;
    }
    PendingEdit pending = std::move(*m_pending);
    m_pending.reset();
    m_data.setBehaviourText(m_name, formatBehaviourTree(pending.tree), std::move(pending.name),
                            pending.merge);
    if (pending.select) {
        m_select = pending.select;
    }
    refresh();
}

void BehaviourPanel::draw(const PlaySession* play) {
    if (m_focusWindow) {
        ImGui::SetNextWindowFocus();
        m_focusWindow = false;
    }
    if (!ImGui::Begin("Behaviours")) {
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
            std::string error;
            if (!m_data.save(error)) {
                logError("Couldn't save the game data: {}", error);
            }
        }
    }
    refresh();
    watch(play);
    drawToolbar(play);
    drawNewPopup();
    if (!m_tree) {
        ImGui::TextColored(kProblemColour, "behaviours/%s.behaviour: %s", m_name.c_str(),
                           m_readError.c_str());
        ImGui::TextDisabled("Put the file right by hand (%s); it is read again once saved.",
                            m_data.behaviourSavePath(m_name).string().c_str());
        ImGui::End();
        return;
    }

    const float graphWidth = std::max(
        ImGui::GetContentRegionAvail().x - kDetailsWidth - ImGui::GetStyle().ItemSpacing.x, 100.0f);
    ImGui::BeginChild("graph", {graphWidth, 0.0f});
    drawGraph();
    ImGui::EndChild();
    applyPending();
    ImGui::SameLine();
    ImGui::BeginChild("details", {0.0f, 0.0f}, ImGuiChildFlags_Borders);
    if (m_tree) {
        drawDetails(play);
    }
    ImGui::EndChild();
    applyPending();
    ImGui::End();
}

void BehaviourPanel::drawToolbar(const PlaySession* play) {
    ImGui::SetNextItemWidth(180.0f);
    const std::string preview =
        std::format("{}{}", m_name, m_data.isBehaviourUnsaved(m_name) ? " *" : "");
    if (ImGui::BeginCombo("##behaviour", preview.c_str())) {
        for (const std::string& name : m_data.behaviourNames()) {
            const std::string label =
                std::format("{}{}", name, m_data.isBehaviourUnsaved(name) ? " *" : "");
            if (ImGui::Selectable(label.c_str(), name == m_name)) {
                m_name = name;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("The behaviour shown: behaviours/<name>.behaviour");
    ImGui::SameLine();
    if (ImGui::Button("New from this...")) {
        m_newName.clear();
        m_problem.clear();
        m_openNewPopup = true;
    }
    ImGui::SetItemTooltip("A copy of %s for the game, to change as you like", m_name.c_str());
    if (m_data.hasGameBehaviour(m_name)) {
        ImGui::SameLine();
        const bool engines = m_data.hasBaseBehaviour(m_name) || m_name == "standard";
        if (ImGui::Button(engines ? "Back to the engine's" : "Delete")) {
            std::string error;
            if (m_data.removeGameBehaviour(m_name, error)) {
                if (!m_data.behaviourText(m_name)) {
                    m_name = "standard";
                }
            } else {
                m_problem = error;
            }
        }
        ImGui::SetItemTooltip(engines ? "Takes the game's copy away, back to the engine's"
                                      : "Deletes the game's behaviour (once saved)");
    }
    ImGui::SameLine();
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
    ImGui::BeginDisabled(!m_data.hasUnsavedChanges());
    if (ImGui::Button("Save")) {
        std::string error;
        if (!m_data.save(error)) {
            logError("Couldn't save the game data: {}", error);
        }
    }
    ImGui::SetItemTooltip("Saves the game data, behaviours and all (Ctrl+S here)");
    ImGui::EndDisabled();

    std::string users;
    for (const EnemySettings& type : m_data.settings().enemies) {
        if ((type.behaviour.empty() ? std::string_view("standard") : type.behaviour) == m_name) {
            users += std::format("{}{}", users.empty() ? "" : ", ", type.name);
        }
    }
    if (users.empty()) {
        ImGui::TextDisabled("No enemy type uses it: set a type's behaviour in the Game data "
                            "window.");
    } else {
        ImGui::TextDisabled("Used by %s.", users.c_str());
    }
    ImGui::SameLine();
    if (m_tree && !m_problems.empty()) {
        ImGui::TextColored(kProblemColour,
                           "%zu problem%s: until they are put right, its types "
                           "use the standard tree.",
                           m_problems.size(), m_problems.size() == 1 ? "" : "s");
    } else if (m_tree && !m_scriptProblems.empty()) {
        ImGui::TextColored(kProblemColour,
                           "%zu script problem%s: until they are put right, those nodes fail.",
                           m_scriptProblems.size(), m_scriptProblems.size() == 1 ? "" : "s");
    } else if (m_tree) {
        ImGui::TextDisabled("Changes play at once.");
    }
    if (play != nullptr && m_tree) {
        ImGui::SameLine();
        ImGui::TextUnformatted("Watching");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(170.0f);
        const std::string watching = m_watched ? m_watchedName : std::string("no one");
        const std::string shown =
            m_watchChosen ? watching : std::format("{} (looked at)", watching);
        if (ImGui::BeginCombo("##watch", shown.c_str())) {
            if (ImGui::Selectable("The one looked at", !m_watchChosen)) {
                m_watchChosen.reset();
            }
            const State* state = play->game().latestState();
            if (state != nullptr) {
                const World& world = state->world;
                for (std::size_t i = 0; i < world.enemies.size(); ++i) {
                    const EntityId id = world.enemies.entities()[i];
                    const Enemy& enemy = world.enemies.components()[i];
                    if (behaviourOf(play->settings(), enemy.type).nodes != m_tree->nodes) {
                        continue;
                    }
                    const std::vector<EnemySettings>& types = play->settings().enemies;
                    const std::string label = std::format(
                        "{} {}", enemy.type < types.size() ? types[enemy.type].name : "enemy",
                        id.index);
                    if (ImGui::Selectable(label.c_str(), m_watchChosen == id)) {
                        m_watchChosen = id;
                    }
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("The enemy whose last tick lights up the nodes: green ran, orange "
                              "was tried and failed");
    }
    if (!m_problem.empty()) {
        ImGui::TextColored(kProblemColour, "%s", m_problem.c_str());
    }
}

void BehaviourPanel::drawNewPopup() {
    if (m_openNewPopup) {
        ImGui::OpenPopup("New behaviour");
        m_openNewPopup = false;
    }
    if (!ImGui::BeginPopupModal("New behaviour", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::Text("A new behaviour for the game, made from %s.", m_name.c_str());
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(220.0f);
    const bool entered = ImGui::InputText("Name", &m_newName, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::TextDisabled("Lowercase letters, digits, '-' and '_'. Then set an enemy type's "
                        "behaviour to it\nin the Game data window.");
    if (!m_problem.empty()) {
        ImGui::TextColored(kProblemColour, "%s", m_problem.c_str());
    }
    if (ImGui::Button("Make it") || entered) {
        std::string error;
        if (m_data.createBehaviour(m_newName, m_name, error)) {
            m_name = m_newName;
            m_problem.clear();
            ImGui::CloseCurrentPopup();
        } else {
            m_problem = error;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        m_problem.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void BehaviourPanel::watch(const PlaySession* play) {
    const std::size_t count = m_tree ? m_tree->nodes.size() : 0;
    m_lights.assign(count, NodeLight::Off);
    m_watched.reset();
    if (play == nullptr || !m_tree) {
        return;
    }
    const State* state = play->game().latestState();
    if (state == nullptr) {
        return;
    }
    const World& world = state->world;
    const GameSettings& settings = play->settings();
    const Camera camera = play->camera();
    const Vec3 forward = cameraBasis(camera).forward;
    std::optional<EntityId> lookedAt;
    float bestFacing = -2.0f;
    for (std::size_t i = 0; i < world.enemies.size(); ++i) {
        const EntityId id = world.enemies.entities()[i];
        const Enemy& enemy = world.enemies.components()[i];
        const Transform* transform = world.transforms.get(id);
        const Health* health = world.healths.get(id);
        if (transform == nullptr || (health != nullptr && isDead(*health)) ||
            behaviourOf(settings, enemy.type).nodes != m_tree->nodes) {
            continue;
        }
        if (m_watchChosen == id) {
            m_watched = id;
        }
        // The one nearest the middle of the view.
        const Vec3 to = transform->position - camera.position;
        const float distance = length(to);
        const float facing = distance > 0.0f ? dot(to, forward) / distance : 1.0f;
        if (facing > bestFacing) {
            bestFacing = facing;
            lookedAt = id;
        }
    }
    if (!m_watched) {
        m_watched = lookedAt;
    }
    if (!m_watched) {
        return;
    }
    const Enemy* enemy = world.enemies.get(*m_watched);
    const std::vector<EnemySettings>& types = settings.enemies;
    m_watchedName = std::format(
        "{} {}", enemy != nullptr && enemy->type < types.size() ? types[enemy->type].name : "enemy",
        m_watched->index);
    const EnemyBehaviour* behaviour = world.enemyBehaviours.get(*m_watched);
    if (behaviour == nullptr) {
        return;
    }
    const double now = ImGui::GetTime();
    for (std::uint32_t node = 0; node < count; ++node) {
        if (hasBit(behaviour->active, node)) {
            m_litUntil[node] = now + kLitSeconds;
        } else if (hasBit(behaviour->reached, node)) {
            m_failedUntil[node] = now + kLitSeconds;
        }
        m_lights[node] = now < m_litUntil[node]      ? NodeLight::Ran
                         : now < m_failedUntil[node] ? NodeLight::Failed
                                                     : NodeLight::Off;
    }
}

void BehaviourPanel::drawGraph() {
    ed::SetCurrentEditor(m_editor);
    ed::Begin("behaviour graph");
    const BehaviourTree& tree = *m_tree;
    const std::uint32_t count = static_cast<std::uint32_t>(tree.nodes.size());
    m_boxes = layoutBehaviourTree(tree, m_sizes, kColumnGap, kRowGap);

    // A node not where it was put is being dragged, and what is under it follows. Let go, it
    // goes where it was dropped among the nodes beside it.
    const bool held = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    std::vector<ImVec2> offsets(count, {0.0f, 0.0f});
    std::vector<bool> dragged(count, false);
    if (m_placed.size() == count) {
        for (std::uint32_t node = 0; node < count; ++node) {
            const ImVec2 at = ed::GetNodePosition(nodeId(node));
            if (at.x != FLT_MAX && (std::abs(at.x - m_placed[node].x) > 0.5f ||
                                    std::abs(at.y - m_placed[node].y) > 0.5f)) {
                dragged[node] = true;
                offsets[node] = {at.x - m_boxes[node].x, at.y - m_boxes[node].y};
            } else if (node > 0) {
                offsets[node] = offsets[m_parents[node]];
            }
        }
    }
    std::optional<std::uint32_t> drop; // the top dragged node
    for (std::uint32_t node = 1; node < count && !drop; ++node) {
        if (dragged[node] && !dragged[m_parents[node]]) {
            drop = node;
        }
    }
    m_placed.resize(count);
    for (std::uint32_t node = 0; node < count; ++node) {
        const bool follows = held && (offsets[node].x != 0.0f || offsets[node].y != 0.0f);
        // Whole pixels, as the graph keeps them, so a node left alone is where it was put.
        m_placed[node] = {std::floor(m_boxes[node].x + (follows ? offsets[node].x : 0.0f)),
                          std::floor(m_boxes[node].y + (follows ? offsets[node].y : 0.0f))};
        if (!(held && dragged[node])) {
            ed::SetNodePosition(nodeId(node), m_placed[node]);
        }
    }
    for (std::uint32_t node = 0; node < count; ++node) {
        drawNode(node, m_shownProblems);
    }
    drawLinks();

    if (drop) {
        const std::uint32_t parent = m_parents[*drop];
        const float middle = m_boxes[*drop].y + offsets[*drop].y + m_boxes[*drop].height * 0.5f;
        const std::uint32_t position = behaviourDropPosition(tree, m_boxes, parent, *drop, middle);
        const std::vector<std::uint32_t> children = behaviourChildren(tree, parent);
        const auto current = std::find(children.begin(), children.end(), *drop) - children.begin();
        if (held) {
            // Where it would go: a line between the nodes it would go between.
            std::vector<std::uint32_t> others = children;
            std::erase(others, *drop);
            float y = m_boxes[parent].y + m_boxes[parent].height * 0.5f;
            if (!others.empty()) {
                y = position == 0 ? m_boxes[others.front()].y - kRowGap * 0.5f
                                  : m_boxes[others[position - 1]].y +
                                        m_boxes[others[position - 1]].height + kRowGap * 0.5f;
            }
            const float x = m_boxes[*drop].x;
            ImGui::GetWindowDrawList()->AddLine({x - 8.0f, y}, {x + m_boxes[*drop].width, y},
                                                ImGui::GetColorU32(kRanColour), 3.0f);
        } else if (position != static_cast<std::uint32_t>(current)) {
            BehaviourTree moved = tree;
            const std::uint32_t at = moveBehaviourSubtree(moved, *drop, parent, position);
            edit(std::move(moved),
                 std::format("Move {}", behaviourNodeInfo(tree.nodes[*drop].kind).name), at);
        }
    }

    if (m_select) {
        ed::ClearSelection();
        if (*m_select < count) {
            ed::SelectNode(nodeId(*m_select));
        }
        m_shown = *m_select;
        m_select.reset();
    } else {
        const std::vector<std::uint32_t> selected = selectedNodes();
        m_shown = selected.empty() ? kNoBehaviourParent : selected.front();
    }

    handleCreate();
    handleDelete();
    handleShortcuts();
    drawMenus();
    if (m_frameAll > 0 && --m_frameAll == 0) {
        ed::NavigateToContent(0.0f);
    }
    ed::End();
    ed::SetCurrentEditor(nullptr);
}

void BehaviourPanel::drawNode(std::uint32_t node, const std::vector<BehaviourProblem>& problems) {
    const BehaviourNode& shown = m_tree->nodes[node];
    const BehaviourNodeInfo& info = behaviourNodeInfo(shown.kind);
    const bool wrong =
        std::any_of(problems.begin(), problems.end(),
                    [node](const BehaviourProblem& problem) { return problem.node == node; });
    const NodeLight light = node < m_lights.size() ? m_lights[node] : NodeLight::Off;
    ed::PushStyleColor(ed::StyleColor_NodeBorder, wrong                        ? kProblemColour
                                                  : light == NodeLight::Ran    ? kRanColour
                                                  : light == NodeLight::Failed ? kFailedColour
                                                                               : kBorderColour);
    ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, wrong || light != NodeLight::Off ? 2.5f : 1.0f);
    ed::BeginNode(nodeId(node));
    ImGui::BeginGroup();
    ImGui::TextColored(categoryColour(info.category), "%s", std::string(info.name).c_str());
    if (const std::string setting = settingText(shown); !setting.empty()) {
        ImGui::SameLine();
        ImGui::TextUnformatted(setting.c_str());
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kNoteWidth);
    if (!shown.note.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted(shown.note.c_str());
        ImGui::PopStyleColor();
    }
    for (const BehaviourProblem& problem : problems) {
        if (problem.node == node) {
            ImGui::PushStyleColor(ImGuiCol_Text, kProblemColour);
            ImGui::TextUnformatted(problem.text.c_str());
            ImGui::PopStyleColor();
        }
    }
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();

    // Pins along the node's left and right edges: in from its parent, out to its children.
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const ImVec4 padding = ed::GetStyle().NodePadding;
    const float middle = (min.y + max.y) * 0.5f;
    const ImVec2 left{min.x - padding.x, middle};
    const ImVec2 right{max.x + padding.z, middle};
    ImGui::SetCursorScreenPos(min);
    if (node > 0) {
        ed::BeginPin(inputPin(node), ed::PinKind::Input);
        ed::PinRect({min.x - padding.x, min.y - padding.y}, {min.x + 6.0f, max.y + padding.w});
        ed::PinPivotRect(left, left);
        ed::EndPin();
    }
    if (takesChildren(shown.kind)) {
        ImGui::SetCursorScreenPos(min);
        ed::BeginPin(outputPin(node), ed::PinKind::Output);
        ed::PinRect({max.x - 6.0f, min.y - padding.y}, {max.x + padding.z, max.y + padding.w});
        ed::PinPivotRect(right, right);
        ed::EndPin();
    }
    ed::EndNode();
    ed::PopStyleVar();
    ed::PopStyleColor();

    const ImVec2 size = ed::GetNodeSize(nodeId(node));
    if (size.x > 0.0f && node < m_sizes.size()) {
        m_sizes[node].width = size.x;
        m_sizes[node].height = size.y;
    }
    // Its category's colour along the top, and the pins as dots.
    const ImVec2 at = ed::GetNodePosition(nodeId(node));
    if (ImDrawList* background = ed::GetNodeBackgroundDrawList(nodeId(node));
        background != nullptr && size.x > 0.0f) {
        const ImU32 colour = ImGui::GetColorU32(categoryColour(info.category));
        background->AddRectFilled(at, {at.x + size.x, at.y + 4.0f}, colour,
                                  ed::GetStyle().NodeRounding, ImDrawFlags_RoundCornersTop);
        const float dotY = at.y + size.y * 0.5f;
        if (node > 0) {
            background->AddCircleFilled({at.x, dotY}, 4.0f, ImGui::GetColorU32(kLinkColour));
        }
        if (takesChildren(shown.kind)) {
            background->AddCircleFilled({at.x + size.x, dotY}, 4.0f, colour);
        }
    }
}

void BehaviourPanel::drawLinks() {
    const BehaviourTree& tree = *m_tree;
    for (std::uint32_t node = 1; node < tree.nodes.size(); ++node) {
        const NodeLight light = node < m_lights.size() ? m_lights[node] : NodeLight::Off;
        const ImVec4 colour = light == NodeLight::Ran      ? kRanColour
                              : light == NodeLight::Failed ? kFailedColour
                                                           : kLinkColour;
        ed::Link(linkTo(node), outputPin(m_parents[node]), inputPin(node), colour,
                 light == NodeLight::Off ? 1.5f : 3.0f);
    }
}

void BehaviourPanel::handleCreate() {
    const BehaviourTree& tree = *m_tree;
    if (ed::BeginCreate(kRanColour, 2.0f)) {
        ed::PinId start;
        ed::PinId end;
        if (ed::QueryNewLink(&start, &end) && start && end) {
            if (!isOutputPin(start)) {
                std::swap(start, end);
            }
            const std::uint32_t parent = nodeOf(start.Get());
            const std::uint32_t child = nodeOf(end.Get());
            std::string refusal;
            if (!isOutputPin(start) || isOutputPin(end)) {
                refusal = "Link a node's right edge to another's left edge";
            } else if (isUnder(tree, parent, child)) {
                refusal = "A node can't go under itself or what is under it";
            } else if (m_parents[child] == parent) {
                refusal = "It is already under it";
            } else if (behaviourNodeInfo(tree.nodes[parent].kind).category ==
                           BehaviourCategory::Decorator &&
                       tree.nodes[parent].end > parent + 1) {
                refusal = std::format("{} takes one node under it",
                                      behaviourNodeInfo(tree.nodes[parent].kind).name);
            }
            if (!refusal.empty()) {
                ed::Suspend();
                ImGui::SetTooltip("%s", refusal.c_str());
                ed::Resume();
                ed::RejectNewItem(kProblemColour, 2.0f);
            } else {
                ed::Suspend();
                ImGui::SetTooltip(
                    "Move %s under %s",
                    std::string(behaviourNodeInfo(tree.nodes[child].kind).name).c_str(),
                    std::string(behaviourNodeInfo(tree.nodes[parent].kind).name).c_str());
                ed::Resume();
                if (ed::AcceptNewItem(kRanColour, 3.0f)) {
                    const float middle = m_boxes[child].y + m_boxes[child].height * 0.5f;
                    const std::uint32_t position =
                        behaviourDropPosition(tree, m_boxes, parent, child, middle);
                    BehaviourTree moved = tree;
                    const std::uint32_t at = moveBehaviourSubtree(moved, child, parent, position);
                    edit(std::move(moved),
                         std::format("Move {}", behaviourNodeInfo(tree.nodes[child].kind).name),
                         at);
                }
            }
        }
        ed::PinId pin;
        if (ed::QueryNewNode(&pin) && pin) {
            const std::uint32_t node = nodeOf(pin.Get());
            ed::Suspend();
            ImGui::SetTooltip(isOutputPin(pin) ? "Add a node under it" : "Put a node above it");
            ed::Resume();
            if (ed::AcceptNewItem()) {
                m_addAt = node;
                m_addAbove = !isOutputPin(pin);
                m_addY = ed::ScreenToCanvas(ImGui::GetMousePos()).y;
                m_filter.clear();
                m_focusFilter = true;
                ed::Suspend();
                ImGui::OpenPopup("Add node");
                ed::Resume();
            }
        }
    }
    ed::EndCreate();
}

void BehaviourPanel::handleDelete() {
    std::vector<std::uint32_t> removed;
    if (ed::BeginDelete()) {
        ed::LinkId link;
        while (ed::QueryDeletedLink(&link)) {
            ed::RejectDeletedItem(); // a node without its link would be under nothing
        }
        ed::NodeId node;
        while (ed::QueryDeletedNode(&node)) {
            const std::uint32_t index = nodeOf(node.Get());
            if (index == 0 || index >= m_tree->nodes.size()) {
                ed::RejectDeletedItem();
            } else if (ed::AcceptDeletedItem(false)) {
                removed.push_back(index);
            }
        }
    }
    ed::EndDelete();
    if (!removed.empty()) {
        remove(std::move(removed));
    }
}

void BehaviourPanel::handleShortcuts() {
    if (ed::BeginShortcut()) {
        const std::vector<std::uint32_t> selected = selectedNodes();
        if (!selected.empty()) {
            if (ed::AcceptCopy()) {
                copy(selected.front());
            } else if (ed::AcceptCut()) {
                copy(selected.front());
                remove({selected.front()});
            } else if (ed::AcceptPaste()) {
                paste(selected.front());
            } else if (ed::AcceptDuplicate()) {
                duplicate(selected.front());
            }
        } else if (ed::AcceptPaste()) {
            paste(0);
        }
    }
    ed::EndShortcut();
}

std::vector<std::uint32_t> BehaviourPanel::selectedNodes() const {
    std::vector<ed::NodeId> ids(
        static_cast<std::size_t>(std::max(ed::GetSelectedObjectCount(), 0)));
    ids.resize(
        static_cast<std::size_t>(ed::GetSelectedNodes(ids.data(), static_cast<int>(ids.size()))));
    std::vector<std::uint32_t> nodes;
    for (const ed::NodeId id : ids) {
        if (const std::uint32_t node = nodeOf(id.Get()); node < m_tree->nodes.size()) {
            nodes.push_back(node);
        }
    }
    std::sort(nodes.begin(), nodes.end());
    return nodes;
}

void BehaviourPanel::copy(std::uint32_t node) {
    ImGui::SetClipboardText(formatBehaviourTree(behaviourSubtree(*m_tree, node)).c_str());
}

void BehaviourPanel::paste(std::uint32_t node) {
    const char* clipboard = ImGui::GetClipboardText();
    std::string error;
    const std::optional<BehaviourTree> pasted = parseBehaviourTree(
        clipboard != nullptr ? clipboard : "", m_name, error, BehaviourReading::Draft);
    if (!pasted) {
        m_problem = std::format("The clipboard holds no behaviour nodes: {}", error);
        return;
    }
    BehaviourTree tree = *m_tree;
    std::uint32_t parent = node;
    std::uint32_t position = kNoBehaviourParent; // last
    if (!takesChildren(tree.nodes[node].kind)) {
        if (node == 0) {
            m_problem = "Nothing can go under the top node, which is an action or condition";
            return;
        }
        parent = m_parents[node];
        const std::vector<std::uint32_t> children = behaviourChildren(tree, parent);
        position = static_cast<std::uint32_t>(std::find(children.begin(), children.end(), node) -
                                              children.begin() + 1);
    }
    const std::uint32_t at = insertBehaviourSubtree(tree, parent, position, *pasted);
    m_problem.clear();
    edit(std::move(tree), "Paste", at);
}

void BehaviourPanel::duplicate(std::uint32_t node) {
    if (node == 0) {
        return;
    }
    BehaviourTree tree = *m_tree;
    const std::uint32_t parent = m_parents[node];
    const std::vector<std::uint32_t> children = behaviourChildren(tree, parent);
    const auto position = static_cast<std::uint32_t>(
        std::find(children.begin(), children.end(), node) - children.begin() + 1);
    const std::uint32_t at =
        insertBehaviourSubtree(tree, parent, position, behaviourSubtree(tree, node));
    std::string name = std::format("Duplicate {}", behaviourNodeInfo(tree.nodes[at].kind).name);
    edit(std::move(tree), std::move(name), at);
}

void BehaviourPanel::remove(std::vector<std::uint32_t> nodes) {
    std::sort(nodes.begin(), nodes.end());
    BehaviourTree tree = *m_tree;
    std::vector<std::uint32_t> tops; // those not under another being removed
    for (const std::uint32_t node : nodes) {
        if (node > 0 && (tops.empty() || !isUnder(tree, node, tops.back()))) {
            tops.push_back(node);
        }
    }
    if (tops.empty()) {
        return;
    }
    const std::string name =
        tops.size() == 1
            ? std::format("Delete {}", behaviourNodeInfo(tree.nodes[tops.front()].kind).name)
            : std::format("Delete {} nodes", tops.size());
    for (auto node = tops.rbegin(); node != tops.rend(); ++node) {
        removeBehaviourSubtree(tree, *node);
    }
    edit(std::move(tree), name, kNoBehaviourParent);
}

void BehaviourPanel::drawMenus() {
    const BehaviourTree& tree = *m_tree;
    ed::Suspend();
    ed::NodeId menuNode;
    if (ed::ShowNodeContextMenu(&menuNode) && nodeOf(menuNode.Get()) < tree.nodes.size()) {
        m_menuNode = nodeOf(menuNode.Get());
        m_filter.clear();
        ImGui::OpenPopup("Node menu");
    } else if (ed::ShowBackgroundContextMenu()) {
        ImGui::OpenPopup("Graph menu");
    }

    if (ImGui::BeginPopup("Node menu")) {
        const std::uint32_t node =
            std::min<std::uint32_t>(m_menuNode, static_cast<std::uint32_t>(tree.nodes.size() - 1));
        const BehaviourNodeInfo& info = behaviourNodeInfo(tree.nodes[node].kind);
        const std::string name(info.name);
        ImGui::TextColored(categoryColour(info.category), "%s", name.c_str());
        ImGui::Separator();
        if (takesChildren(tree.nodes[node].kind) && ImGui::BeginMenu("Add under it")) {
            if (const auto kind = chooseKind(m_filter, m_focusFilter,
                                             [](const BehaviourNodeInfo&) { return true; })) {
                BehaviourTree added = tree;
                const std::uint32_t at =
                    insertBehaviourSubtree(added, node, kNoBehaviourParent, singleNode(*kind));
                edit(std::move(added), std::format("Add {}", behaviourNodeInfo(*kind).name), at);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Put above it")) {
            if (const auto kind =
                    chooseKind(m_filter, m_focusFilter, [](const BehaviourNodeInfo& each) {
                        return takesChildren(each.kind);
                    })) {
                BehaviourTree wrapped = tree;
                wrapBehaviourNode(wrapped, node, *kind);
                edit(std::move(wrapped),
                     std::format("Put {} above {}", behaviourNodeInfo(*kind).name, name), node);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Change to")) {
            if (const auto kind =
                    chooseKind(m_filter, m_focusFilter, [&](const BehaviourNodeInfo& each) {
                        return each.kind != tree.nodes[node].kind &&
                               canChangeBehaviourKind(tree, node, each.kind);
                    })) {
                BehaviourTree changed = tree;
                changeBehaviourKind(changed, node, *kind);
                edit(std::move(changed),
                     std::format("Change {} to {}", name, behaviourNodeInfo(*kind).name), node);
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Take away, keeping what is under it", nullptr, false,
                            canUnwrapBehaviourNode(tree, node))) {
            BehaviourTree unwrapped = tree;
            unwrapBehaviourNode(unwrapped, node);
            edit(std::move(unwrapped), std::format("Take away {}", name),
                 node < unwrapped.nodes.size() ? node : kNoBehaviourParent);
        }
        if (node > 0) {
            const std::uint32_t parent = m_parents[node];
            const std::vector<std::uint32_t> children = behaviourChildren(tree, parent);
            const auto index = static_cast<std::uint32_t>(
                std::find(children.begin(), children.end(), node) - children.begin());
            if (ImGui::MenuItem("Move up", nullptr, false, index > 0)) {
                BehaviourTree moved = tree;
                const std::uint32_t at = moveBehaviourSubtree(moved, node, parent, index - 1);
                edit(std::move(moved), std::format("Move {}", name), at);
            }
            if (ImGui::MenuItem("Move down", nullptr, false, index + 1 < children.size())) {
                BehaviourTree moved = tree;
                const std::uint32_t at = moveBehaviourSubtree(moved, node, parent, index + 1);
                edit(std::move(moved), std::format("Move {}", name), at);
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Copy", "Ctrl+C")) {
            copy(node);
        }
        if (ImGui::MenuItem("Cut", "Ctrl+X", false, node > 0)) {
            copy(node);
            remove({node});
        }
        if (ImGui::MenuItem(takesChildren(tree.nodes[node].kind) ? "Paste under it"
                                                                 : "Paste after it",
                            "Ctrl+V", false, takesChildren(tree.nodes[node].kind) || node > 0)) {
            paste(node);
        }
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, node > 0)) {
            duplicate(node);
        }
        if (ImGui::MenuItem("Delete, with what is under it", "Del", false, node > 0)) {
            remove({node});
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("Graph menu")) {
        if (ImGui::MenuItem("Show the whole tree", "F")) {
            ed::NavigateToContent();
        }
        if (ImGui::MenuItem("Paste under the top node", "Ctrl+V", false,
                            takesChildren(tree.nodes[0].kind))) {
            paste(0);
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("Add node")) {
        const std::uint32_t node =
            std::min<std::uint32_t>(m_addAt, static_cast<std::uint32_t>(tree.nodes.size() - 1));
        ImGui::TextDisabled(m_addAbove ? "Put above %s:" : "Add under %s:",
                            std::string(behaviourNodeInfo(tree.nodes[node].kind).name).c_str());
        const bool above = m_addAbove;
        if (const auto kind =
                chooseKind(m_filter, m_focusFilter, [above](const BehaviourNodeInfo& each) {
                    return !above || takesChildren(each.kind);
                })) {
            BehaviourTree changed = tree;
            std::uint32_t at = node;
            if (above) {
                wrapBehaviourNode(changed, node, *kind);
            } else if (takesChildren(tree.nodes[node].kind)) {
                at = insertBehaviourSubtree(
                    changed, node,
                    behaviourDropPosition(tree, m_boxes, node, kNoBehaviourParent, m_addY),
                    singleNode(*kind));
            }
            edit(std::move(changed), std::format("Add {}", behaviourNodeInfo(*kind).name), at);
        }
        ImGui::EndPopup();
    }
    ed::Resume();
}

void BehaviourPanel::drawDetails(const PlaySession* play) {
    const BehaviourTree& tree = *m_tree;
    if (m_shown >= tree.nodes.size()) {
        ImGui::TextUnformatted(std::format("{}.behaviour", m_name).c_str());
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", m_data.hasGameBehaviour(m_name)
                                      ? "The game's own; saved to its content folder."
                                      : "The engine's; a change is saved as the game's copy.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        for (const BehaviourProblem& problem : m_shownProblems) {
            const std::string label = std::format(
                "{} (node {}): {}", behaviourNodeInfo(tree.nodes[problem.node].kind).name,
                problem.node + 1, problem.text);
            ImGui::PushStyleColor(ImGuiCol_Text, kProblemColour);
            if (ImGui::Selectable(label.c_str())) {
                m_select = problem.node;
            }
            ImGui::PopStyleColor();
        }
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled(
            "The tree runs from the top every tick, each node's children in order down the "
            "column to its right.\n\n"
            "Drag from a node's right edge to add a node under it, or onto another node to move "
            "that one under it. Drag a node up or down to reorder it among those beside it. "
            "Right-click a node for the rest.\n\n"
            "Delete takes a node away with what is under it; Ctrl+C, X, V and D copy, cut, "
            "paste and duplicate (as the file's text). F shows the whole tree.\n\n"
            "docs/Behaviours.md says what each node does.");
        ImGui::PopTextWrapPos();
        return;
    }

    const std::uint32_t node = m_shown;
    const BehaviourNode& shown = tree.nodes[node];
    const BehaviourNodeInfo& info = behaviourNodeInfo(shown.kind);
    ImGui::TextColored(categoryColour(info.category), "%s", std::string(info.name).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("node %u", node + 1);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(std::string(info.description).c_str());
    ImGui::PopTextWrapPos();
    ImGui::PushTextWrapPos(0.0f);
    for (const BehaviourProblem& problem : m_shownProblems) {
        if (problem.node == node) {
            ImGui::TextColored(kProblemColour, "%s", problem.text.c_str());
        }
    }
    ImGui::PopTextWrapPos();
    if (play != nullptr && m_watched && node < m_lights.size()) {
        const NodeLight light = m_lights[node];
        ImGui::TextColored(light == NodeLight::Ran ? kRanColour
                           : light == NodeLight::Failed
                               ? kFailedColour
                               : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
                           "%s: %s", m_watchedName.c_str(),
                           light == NodeLight::Ran      ? "ran (succeeded or still running)"
                           : light == NodeLight::Failed ? "tried it, and it failed"
                                                        : "not reached");
    }
    ImGui::Spacing();

    const BehaviourParameter& parameter = info.parameter;
    if (parameter.script) {
        drawScriptSetting(node);
    } else if (!parameter.name.empty()) {
        float value = shown.value;
        const std::string label = parameter.unit.empty()
                                      ? std::string(parameter.name)
                                      : std::format("{} ({})", parameter.name, parameter.unit);
        ImGui::SetNextItemWidth(120.0f);
        if (dragNumber(label.c_str(), value, parameter.minimum, parameter.maximum)) {
            const ImGuiID id = ImGui::GetItemID();
            BehaviourTree changed = tree;
            changed.nodes[node].value = std::clamp(value, parameter.minimum, parameter.maximum);
            edit(std::move(changed), std::format("Set {} {}", info.name, parameter.name), node,
                 m_mergeWith == id);
            m_mergeWith = id;
        }
    }

    ImGui::TextUnformatted("Note");
    // Taken from the tree again unless being typed, so undo shows.
    if (!m_typingNote || m_noteNode != node) {
        m_note = shown.note;
        m_noteNode = node;
    }
    const bool typed =
        ImGui::InputTextMultiline("##note", &m_note, {-1.0f, ImGui::GetTextLineHeight() * 6.0f});
    m_typingNote = ImGui::IsItemActive();
    if (typed) {
        const ImGuiID id = ImGui::GetItemID();
        BehaviourTree changed = tree;
        changed.nodes[node].note = m_note;
        edit(std::move(changed), std::format("Note on {}", info.name), node, m_mergeWith == id);
        m_mergeWith = id;
    }
    ImGui::TextDisabled("Written as # comments above it in the file.");
}

void BehaviourPanel::drawScriptSetting(std::uint32_t node) {
    const BehaviourTree& tree = *m_tree;
    const std::string& current = tree.nodes[node].script;
    const auto name = [&](std::string script) {
        BehaviourTree changed = tree;
        changed.nodes[node].script = std::move(script);
        edit(std::move(changed), "Set script", node);
    };
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::BeginCombo("script", current.empty() ? "(none)" : current.c_str())) {
        for (const std::string_view script :
             m_scripts != nullptr ? m_scripts->names() : std::vector<std::string_view>{}) {
            const ScriptProgram* program = m_scripts->find(script);
            const bool usable = program != nullptr && behaviourScriptProblem(*program).empty();
            if (!usable) {
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            }
            if (ImGui::Selectable(std::string(script).c_str(), script == current)) {
                name(std::string(script));
            }
            if (!usable) {
                ImGui::PopStyleColor();
                ImGui::SetItemTooltip("Not a behaviour script: one has a decide function and "
                                      "nothing else");
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("scripts/<name>.luau, whose decide function this node asks");
    if (m_scriptPanel != nullptr && m_scripts != nullptr && !current.empty() &&
        !m_scripts->path(current).empty()) {
        ImGui::SameLine();
        if (ImGui::Button("Edit")) {
            m_scriptPanel->open(current);
        }
        ImGui::SetItemTooltip("Opens it in the Scripts window");
    }
    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputTextWithHint("##newscript", "new script's name", &m_newScript);
    ImGui::SameLine();
    ImGui::BeginDisabled(m_newScript.empty() || m_scriptPanel == nullptr);
    if (ImGui::Button("New")) {
        std::string error;
        if (m_scriptPanel->create(m_newScript, error, ScriptTemplate::Behaviour)) {
            name(m_newScript);
            m_newScript.clear();
            m_problem.clear();
        } else {
            m_problem = error;
        }
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Writes scripts/<name>.luau into the game's content folder with a "
                          "decide function to start from, names it here and opens it");
    ImGui::Spacing();
}

} // namespace ct
