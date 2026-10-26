#include "LogicPanel.h"

#include "SettingWidgets.h"

#include "croftech/core/ConfigFile.h"
#include "croftech/editor/EditorActions.h"
#include "croftech/editor/EntityLinks.h"
#include "croftech/editor/LevelCommands.h"
#include "croftech/level/EntityClasses.h"
#include "croftech/script/ScriptLibrary.h"

#include <imgui-node-editor/imgui_node_editor.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <format>
#include <memory>
#include <span>
#include <utility>

namespace ct {
namespace {

namespace ed = ax::NodeEditor;

constexpr ImVec4 kProblemColour{1.0f, 0.47f, 0.4f, 1.0f};
constexpr ImVec4 kBorderColour{1.0f, 1.0f, 1.0f, 0.25f};
constexpr ImVec4 kLinkColour{0.75f, 0.75f, 0.8f, 0.8f};
constexpr ImVec4 kPinColour{0.85f, 0.85f, 0.9f, 1.0f};
constexpr float kDetailsWidth = 300.0f;
constexpr float kPinGap = 28.0f; // between a row's input and event
constexpr GraphSize kGuessedSize{160.0f, 60.0f};
constexpr std::size_t kNoNode = ~std::size_t{0};
// Nodes are measured as they are drawn, laid out from that the next frame, and the graph knows
// where they went the frame after: only then can it show them all.
constexpr int kFramesToShowAll = 3;

// Ids for the graph, from entity indices so they mean the same entity from frame to frame: a
// node, a pin (input or event, by its place in the node's list) and a link (an output of one
// entity to another).
constexpr std::uintptr_t kMaxPins = 1024;
constexpr std::uintptr_t kMaxOutputs = 4096;
constexpr std::uintptr_t kMaxEntities = std::uintptr_t{1} << 20;

ed::NodeId nodeId(std::size_t entity) { return ed::NodeId(entity * 4 + 1); }
ed::PinId pinId(std::size_t entity, std::size_t pin, bool event) {
    return ed::PinId(((entity * kMaxPins + pin) * 2 + (event ? 1 : 0)) * 4 + 2);
}
ed::LinkId linkId(std::size_t from, std::size_t output, std::size_t to) {
    return ed::LinkId(((from * kMaxOutputs + output) * kMaxEntities + to) * 4 + 3);
}

struct PinRef {
    std::size_t entity = 0;
    std::size_t pin = 0;
    bool event = false;
};

PinRef pinOf(ed::PinId id) {
    std::uintptr_t value = (id.Get() - 2) / 4;
    const bool event = value % 2 == 1;
    value /= 2;
    return {value / kMaxPins, value % kMaxPins, event};
}

// The colour of a class's name: volumes, logic, movers, fighting, items, routes.
ImVec4 classColour(std::string_view className) {
    if (className == "trigger" || className == "checkpoint" || className == "hurt") {
        return {0.4f, 0.85f, 0.85f, 1.0f};
    }
    if (className == "relay" || className == "counter" || className == "timer" ||
        className == kScriptClass) {
        return {0.78f, 0.6f, 1.0f, 1.0f};
    }
    if (className == "mover") {
        return {0.45f, 0.65f, 1.0f, 1.0f};
    }
    if (className == kEnemyClass || className == kSpawnerClass || className == kEncounterClass) {
        return {1.0f, 0.55f, 0.45f, 1.0f};
    }
    if (className == kPickupClass) {
        return {0.5f, 0.88f, 0.55f, 1.0f};
    }
    if (className == kPatrolPointClass) {
        return {0.95f, 0.82f, 0.4f, 1.0f};
    }
    return {0.8f, 0.8f, 0.8f, 1.0f};
}

std::string entityLabel(const Level& level, std::size_t index) {
    const LevelEntity& entity = level.entities[index];
    const std::string* name = findProperty(entity, "name");
    return name != nullptr && !name->empty()
               ? std::format("{} '{}'", entity.className, *name)
               : std::format("{} (entity {})", entity.className, index + 1);
}

std::size_t entitiesNamed(const Level& level, std::string_view name) {
    return static_cast<std::size_t>(
        std::count_if(level.entities.begin(), level.entities.end(), [&](const LevelEntity& e) {
            const std::string* other = findProperty(e, "name");
            return other != nullptr && *other == name;
        }));
}

// The pin of the name, added as one the entity doesn't have if it isn't there.
std::size_t findPin(std::vector<std::string>& names, std::vector<bool>& known,
                    std::string_view name) {
    const auto found = std::find(names.begin(), names.end(), name);
    if (found != names.end()) {
        return static_cast<std::size_t>(found - names.begin());
    }
    names.emplace_back(name);
    known.push_back(false);
    return names.size() - 1;
}

} // namespace

LogicPanel::LogicPanel() {
    ed::Config config;
    config.SettingsFile = nullptr; // places are kept in the level
    m_editor = ed::CreateEditor(&config);
    ed::SetCurrentEditor(m_editor);
    ed::Style& style = ed::GetStyle();
    style.NodeRounding = 5.0f;
    style.NodePadding = {10.0f, 8.0f, 10.0f, 8.0f};
    style.LinkStrength = 80.0f;
    style.Colors[ed::StyleColor_Bg] = {0.11f, 0.11f, 0.13f, 1.0f};
    style.Colors[ed::StyleColor_NodeBg] = {0.17f, 0.17f, 0.2f, 0.96f};
    ed::SetCurrentEditor(nullptr);
}

LogicPanel::~LogicPanel() { ed::DestroyEditor(m_editor); }

void LogicPanel::rebuild(const EditorDocument& document) {
    const std::uint64_t scripts = m_scripts != nullptr ? m_scripts->generation() : 0;
    if (m_seenChange == document.changeCount() && m_seenScripts == scripts &&
        m_seenEverything == m_everything) {
        return;
    }
    m_seenChange = document.changeCount();
    m_seenScripts = scripts;
    m_seenEverything = m_everything;
    const Level& level = document.level();
    m_nodes.clear();
    m_links.clear();
    m_nodeOf.assign(level.entities.size(), kNoNode);
    m_sizes.resize(level.entities.size(), kGuessedSize);
    m_placed.clear();

    // Each node's pins by name, those it has first.
    std::vector<std::vector<std::string>> inputs;
    std::vector<std::vector<std::string>> events;
    std::vector<std::vector<bool>> inputsKnown;
    std::vector<std::vector<bool>> eventsKnown;
    for (const std::size_t entity : logicGraphEntities(level, m_scripts, m_everything)) {
        m_nodeOf[entity] = m_nodes.size();
        m_nodes.push_back({entity, {}, {}, {}});
        inputs.emplace_back();
        events.emplace_back();
        for (const SignalInfo& input : entitySignals(level.entities[entity], m_scripts, false)) {
            inputs.back().emplace_back(input.name);
        }
        for (const SignalInfo& event : entitySignals(level.entities[entity], m_scripts, true)) {
            events.back().emplace_back(event.name);
        }
        inputsKnown.emplace_back(inputs.back().size(), true);
        eventsKnown.emplace_back(events.back().size(), true);
    }
    for (const Wire& wire : allWires(level)) {
        const EntityOutput& output = level.entities[wire.from].outputs[wire.output];
        Link link{wire.from, wire.to, wire.output, m_nodeOf[wire.from], m_nodeOf[wire.to]};
        link.event = findPin(events[link.fromNode], eventsKnown[link.fromNode], output.event);
        link.input = findPin(inputs[link.toNode], inputsKnown[link.toNode], output.input);
        link.known = eventsKnown[link.fromNode][link.event] && inputsKnown[link.toNode][link.input];
        m_links.push_back(link);
    }
    for (std::size_t i = 0; i < m_nodes.size(); ++i) {
        Node& node = m_nodes[i];
        for (const EntityOutput& output : level.entities[node.entity].outputs) {
            if (entitiesNamed(level, output.target) == 0) {
                findPin(events[i], eventsKnown[i], output.event);
                node.problems.push_back(
                    output.target.empty()
                        ? std::format("{} sends {} to no one", output.event, output.input)
                        : std::format("{} -> '{}': no entity has this name", output.event,
                                      output.target));
            }
        }
        for (std::size_t pin = 0; pin < inputs[i].size(); ++pin) {
            node.inputs.push_back({inputs[i][pin], inputsKnown[i][pin]});
        }
        for (std::size_t pin = 0; pin < events[i].size(); ++pin) {
            node.events.push_back({events[i][pin], eventsKnown[i][pin]});
        }
    }
    m_inputAt.assign(m_nodes.size(), {});
    m_eventAt.assign(m_nodes.size(), {});
}

bool LogicPanel::draw(EditorDocument& document) {
    if (!ImGui::Begin("Logic")) {
        m_focused = false;
        ImGui::End();
        return false;
    }
    m_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    rebuild(document);
    drawToolbar(document);
    const float graphWidth = std::max(
        ImGui::GetContentRegionAvail().x - kDetailsWidth - ImGui::GetStyle().ItemSpacing.x, 100.0f);
    ImGui::BeginChild("graph", {graphWidth, 0.0f});
    drawGraph(document);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("details", {0.0f, 0.0f}, ImGuiChildFlags_Borders);
    rebuild(document);
    drawDetails(document);
    ImGui::EndChild();
    ImGui::End();
    return std::exchange(m_frameView, false);
}

void LogicPanel::drawToolbar(EditorDocument& document) {
    ImGui::Checkbox("Every entity with events or inputs", &m_everything);
    ImGui::SetItemTooltip("Otherwise only those wired to something");
    ImGui::SameLine();
    if (ImGui::Button("Tidy up")) {
        const Level& level = document.level();
        std::vector<std::size_t> entities;
        std::vector<GraphSize> sizes;
        for (const Node& node : m_nodes) {
            entities.push_back(node.entity);
            sizes.push_back(m_sizes[node.entity]);
        }
        const std::vector<GraphPoint> places = layoutLogicGraph(level, entities, sizes, false);
        std::vector<std::pair<std::size_t, GraphPoint>> moves;
        for (std::size_t i = 0; i < entities.size(); ++i) {
            moves.emplace_back(entities[i], places[i]);
        }
        setGraphPositions(document, moves, "Tidy up the logic graph");
        m_frameAll = kFramesToShowAll;
    }
    ImGui::SetItemTooltip("Lays every node out again by the wiring (one undo)");
    ImGui::SameLine();
    if (ImGui::Button("Show all")) {
        m_frameAllNow = true;
    }
    ImGui::SetItemTooltip("F in the graph shows what is chosen, or all");
    ImGui::SameLine();
    std::size_t problems = 0;
    for (const Node& node : m_nodes) {
        problems += node.problems.size();
    }
    problems += static_cast<std::size_t>(std::count_if(
        m_links.begin(), m_links.end(), [](const Link& link) { return !link.known; }));
    ImGui::TextDisabled("%zu entities, %zu wires.", m_nodes.size(), m_links.size());
    if (problems > 0) {
        ImGui::SameLine();
        ImGui::TextColored(kProblemColour, "%zu problem%s, in red.", problems,
                           problems == 1 ? "" : "s");
    }
    if (!m_problem.empty()) {
        ImGui::TextColored(kProblemColour, "%s", m_problem.c_str());
    }
}

void LogicPanel::drawGraph(EditorDocument& document) {
    // A window still finding its size has no room yet; the graph starts once it has.
    const ImVec2 room = ImGui::GetContentRegionAvail();
    if (room.x < 50.0f || room.y < 50.0f) {
        return;
    }
    ed::SetCurrentEditor(m_editor);
    // imgui-node-editor 0.9.3 moves into place only the draw commands that start after its Begin;
    // begun on an empty draw list, the graph would join the list's first command and keep a clip
    // rectangle in graph space, and show nothing. Its background, drawn first, keeps them apart.
    const ImVec2 corner = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(
        corner, {corner.x + room.x, corner.y + room.y},
        ImGui::GetColorU32(ed::GetStyle().Colors[ed::StyleColor_Bg]));
    ed::Begin("logic graph");
    const Level& level = document.level();
    std::vector<std::size_t> entities;
    std::vector<GraphSize> sizes;
    for (const Node& node : m_nodes) {
        entities.push_back(node.entity);
        sizes.push_back(m_sizes[node.entity]);
    }
    m_places = layoutLogicGraph(level, entities, sizes, true);

    // A node not where it was put is being dragged; let go, it keeps the place it was dropped.
    const bool held = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const std::size_t count = m_nodes.size();
    std::vector<bool> dragged(count, false);
    std::vector<std::pair<std::size_t, GraphPoint>> dropped;
    if (m_placed.size() == count) {
        for (std::size_t i = 0; i < count; ++i) {
            const ImVec2 at = ed::GetNodePosition(nodeId(m_nodes[i].entity));
            if (at.x != FLT_MAX &&
                (std::abs(at.x - m_placed[i].x) > 0.5f || std::abs(at.y - m_placed[i].y) > 0.5f)) {
                dragged[i] = true;
                if (!held) {
                    dropped.emplace_back(m_nodes[i].entity, GraphPoint{at.x, at.y});
                }
            }
        }
    }
    m_placed.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (dragged[i] && !held) {
            m_placed[i] = ed::GetNodePosition(nodeId(m_nodes[i].entity));
        } else {
            m_placed[i] = {m_places[i].x, m_places[i].y};
        }
        if (!dragged[i]) {
            ed::SetNodePosition(nodeId(m_nodes[i].entity), m_placed[i]);
        }
    }

    for (std::size_t i = 0; i < count; ++i) {
        drawNode(level, i);
    }
    for (const Link& link : m_links) {
        ed::Link(linkId(link.from, link.output, link.to), pinId(link.from, link.event, true),
                 pinId(link.to, link.input, false), link.known ? kLinkColour : kProblemColour,
                 2.0f);
    }
    // Delays, halfway along their links.
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    for (const Link& link : m_links) {
        const float delay = level.entities[link.from].outputs[link.output].delay;
        if (delay <= 0.0f || link.event >= m_eventAt[link.fromNode].size() ||
            link.input >= m_inputAt[link.toNode].size()) {
            continue;
        }
        const ImVec2 a = m_eventAt[link.fromNode][link.event];
        const ImVec2 b = m_inputAt[link.toNode][link.input];
        const std::string text = std::format("{} s", formatFloat(delay));
        const ImVec2 size = ImGui::CalcTextSize(text.c_str());
        const ImVec2 at{(a.x + b.x - size.x) * 0.5f, (a.y + b.y - size.y) * 0.5f};
        drawList->AddRectFilled({at.x - 3.0f, at.y - 1.0f}, {at.x + size.x + 3.0f, at.y + size.y},
                                IM_COL32(28, 28, 33, 230), 3.0f);
        drawList->AddText(at, ImGui::GetColorU32(kLinkColour), text.c_str());
    }

    handleCreate(document);
    if (!dropped.empty()) {
        setGraphPositions(document, dropped, dropped.size() == 1 ? "Move node" : "Move nodes");
    }
    if (const ed::NodeId clicked = ed::GetDoubleClickedNode()) {
        const std::size_t entity = (clicked.Get() - 1) / 4;
        if (entity < document.level().entities.size()) {
            const LevelItem item = LevelItem::entity(entity);
            document.setSelection(withWholeEntities(document.level(), std::span(&item, 1)));
            m_frameView = true;
        }
    }
    syncSelection(document);
    handleKeys(document);
    m_chosenLinks = selectedLinks();
    m_chosenNodes = selectedNodes();
    m_chosenChange = document.changeCount();
    // A graph first shown in a window still finding its size waits for it to settle.
    const ImVec2 screen = ed::GetScreenSize();
    if (!m_shownAll && (screen.x != m_screenSize.x || screen.y != m_screenSize.y)) {
        m_frameAll = kFramesToShowAll;
    }
    m_screenSize = screen;
    if (m_frameAllNow || (m_frameAll > 0 && --m_frameAll == 0)) {
        ed::NavigateToContent(0.0f);
        m_frameAllNow = false;
        m_shownAll = true;
    }
    ed::End();
    ed::SetCurrentEditor(nullptr);
}

void LogicPanel::drawNode(const Level& level, std::size_t index) {
    const Node& node = m_nodes[index];
    const LevelEntity& entity = level.entities[node.entity];
    const auto unknown = [](const Pin& pin) { return !pin.known; };
    const bool wrong = !node.problems.empty() ||
                       std::any_of(node.inputs.begin(), node.inputs.end(), unknown) ||
                       std::any_of(node.events.begin(), node.events.end(), unknown);
    ed::PushStyleColor(ed::StyleColor_NodeBorder, wrong ? kProblemColour : kBorderColour);
    ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, wrong ? 2.5f : 1.0f);
    ed::BeginNode(nodeId(node.entity));
    const ImVec2 start = ImGui::GetCursorScreenPos();
    ImGui::TextColored(classColour(entity.className), "%s", entity.className.c_str());
    if (const std::string* name = findProperty(entity, "name"); name != nullptr && !name->empty()) {
        ImGui::SameLine();
        ImGui::TextUnformatted(name->c_str());
    }
    const float titleWidth = ImGui::GetItemRectMax().x - start.x;
    float inputWidth = 0.0f;
    float eventWidth = 0.0f;
    for (const Pin& pin : node.inputs) {
        inputWidth = std::max(inputWidth, ImGui::CalcTextSize(pin.name.c_str()).x);
    }
    for (const Pin& pin : node.events) {
        eventWidth = std::max(eventWidth, ImGui::CalcTextSize(pin.name.c_str()).x);
    }
    const float inner = std::max(titleWidth, inputWidth + kPinGap + eventWidth);
    const ImVec4 padding = ed::GetStyle().NodePadding;
    const float leftEdge = start.x - padding.x;
    const float rightEdge = start.x + inner + padding.z;
    const float rowHeight = ImGui::GetTextLineHeight();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const std::size_t rows = std::max(node.inputs.size(), node.events.size());
    m_inputAt[index].assign(node.inputs.size(), {});
    m_eventAt[index].assign(node.events.size(), {});

    // A row for each input and event, side by side; each pin reaches to the node's edge.
    for (std::size_t row = 0; row < rows; ++row) {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::Dummy({inner, rowHeight});
        const float middle = at.y + rowHeight * 0.5f;
        if (row < node.inputs.size()) {
            const Pin& pin = node.inputs[row];
            drawList->AddText({start.x, at.y},
                              ImGui::GetColorU32(pin.known ? kPinColour : kProblemColour),
                              pin.name.c_str());
            ed::BeginPin(pinId(node.entity, row, false), ed::PinKind::Input);
            ed::PinRect({leftEdge, at.y},
                        {start.x + ImGui::CalcTextSize(pin.name.c_str()).x, at.y + rowHeight});
            ed::PinPivotRect({leftEdge, middle}, {leftEdge, middle});
            ed::EndPin();
            m_inputAt[index][row] = {leftEdge, middle};
        }
        if (row < node.events.size()) {
            const Pin& pin = node.events[row];
            const float width = ImGui::CalcTextSize(pin.name.c_str()).x;
            drawList->AddText({start.x + inner - width, at.y},
                              ImGui::GetColorU32(pin.known ? kPinColour : kProblemColour),
                              pin.name.c_str());
            ed::BeginPin(pinId(node.entity, row, true), ed::PinKind::Output);
            ed::PinRect({start.x + inner - width, at.y}, {rightEdge, at.y + rowHeight});
            ed::PinPivotRect({rightEdge, middle}, {rightEdge, middle});
            ed::EndPin();
            m_eventAt[index][row] = {rightEdge, middle};
        }
    }
    if (!node.problems.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + inner);
        for (const std::string& problem : node.problems) {
            ImGui::TextColored(kProblemColour, "%s", problem.c_str());
        }
        ImGui::PopTextWrapPos();
    }
    ed::EndNode();
    ed::PopStyleVar();
    ed::PopStyleColor();

    const ImVec2 size = ed::GetNodeSize(nodeId(node.entity));
    if (size.x > 0.0f) {
        m_sizes[node.entity] = {size.x, size.y};
    }
    if (ImDrawList* background = ed::GetNodeBackgroundDrawList(nodeId(node.entity))) {
        const ImVec2 top = ed::GetNodePosition(nodeId(node.entity));
        if (size.x > 0.0f) {
            background->AddRectFilled(top, {top.x + size.x, top.y + 4.0f},
                                      ImGui::GetColorU32(classColour(entity.className)),
                                      ed::GetStyle().NodeRounding, ImDrawFlags_RoundCornersTop);
        }
        for (const ImVec2& pin : m_inputAt[index]) {
            background->AddCircleFilled(pin, 4.0f, ImGui::GetColorU32(kLinkColour));
        }
        for (const ImVec2& pin : m_eventAt[index]) {
            background->AddCircleFilled(pin, 4.0f, ImGui::GetColorU32(kLinkColour));
        }
    }
}

void LogicPanel::handleCreate(EditorDocument& document) {
    if (ed::BeginCreate(kLinkColour, 2.0f)) {
        ed::PinId startId;
        ed::PinId endId;
        if (ed::QueryNewLink(&startId, &endId) && startId && endId) {
            const PinRef a = pinOf(startId);
            const PinRef b = pinOf(endId);
            const PinRef& event = a.event ? a : b;
            const PinRef& input = a.event ? b : a;
            const std::size_t fromNode =
                event.entity < m_nodeOf.size() ? m_nodeOf[event.entity] : kNoNode;
            const std::size_t toNode =
                input.entity < m_nodeOf.size() ? m_nodeOf[input.entity] : kNoNode;
            const bool fits = a.event != b.event && fromNode != kNoNode && toNode != kNoNode &&
                              event.pin < m_nodes[fromNode].events.size() &&
                              input.pin < m_nodes[toNode].inputs.size() &&
                              m_nodes[fromNode].events[event.pin].known &&
                              m_nodes[toNode].inputs[input.pin].known;
            if (!fits) {
                ed::RejectNewItem(kProblemColour, 2.0f);
            } else if (ed::AcceptNewItem(kLinkColour, 2.5f)) {
                if (!connectEntities(document, event.entity,
                                     m_nodes[fromNode].events[event.pin].name, input.entity,
                                     m_nodes[toNode].inputs[input.pin].name)) {
                    m_problem = "They are wired so already.";
                } else {
                    m_problem.clear();
                }
            }
        }
    }
    ed::EndCreate();
}

void LogicPanel::handleKeys(EditorDocument& document) {
    if (!m_focused || ImGui::GetIO().WantTextInput) {
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
        if (ed::GetSelectedObjectCount() > 0) {
            ed::NavigateToSelection(false, 0.0f);
        } else {
            ed::NavigateToContent(0.0f);
        }
    }
    if (!ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        return;
    }
    const std::vector<std::size_t> links = selectedLinks();
    if (links.empty()) {
        deleteSelection(
            document); // the chosen nodes' entities, as the graph's selection is the level's
        return;
    }
    // Each entity once, its outputs taken from the last so the others stay where they are.
    const Level& level = document.level();
    std::vector<std::pair<std::size_t, std::size_t>> outputs;
    for (const std::size_t link : links) {
        outputs.emplace_back(m_links[link].from, m_links[link].output);
    }
    std::sort(outputs.begin(), outputs.end());
    outputs.erase(std::unique(outputs.begin(), outputs.end()), outputs.end());
    std::vector<std::unique_ptr<Command>> commands;
    for (std::size_t i = 0; i < outputs.size();) {
        const std::size_t entity = outputs[i].first;
        LevelEntity changed = level.entities[entity];
        std::size_t end = i;
        while (end < outputs.size() && outputs[end].first == entity) {
            ++end;
        }
        for (std::size_t j = end; j-- > i;) {
            changed.outputs.erase(changed.outputs.begin() +
                                  static_cast<std::ptrdiff_t>(outputs[j].second));
        }
        commands.push_back(
            makeSetEntity("Remove wire", entity, level.entities[entity], std::move(changed)));
        i = end;
    }
    document.execute(commands.size() == 1 ? std::move(commands.front())
                                          : makeCompound("Remove wires", std::move(commands)));
    ed::ClearSelection();
}

void LogicPanel::syncSelection(EditorDocument& document) {
    if (document.selection() != m_syncedLevel) {
        // Chosen elsewhere: the graph follows.
        ed::ClearSelection();
        m_syncedGraph.clear();
        for (const LevelItem& item : document.selection()) {
            if (item.kind == LevelItem::Kind::Entity && item.index < m_nodeOf.size() &&
                m_nodeOf[item.index] != kNoNode) {
                ed::SelectNode(nodeId(item.index), true);
                m_syncedGraph.push_back(item.index);
            }
        }
        m_syncedLevel = document.selection();
        return;
    }
    const std::vector<std::size_t> chosen = selectedNodes();
    if (chosen == m_syncedGraph) {
        return;
    }
    // Chosen in the graph: the level follows.
    std::vector<LevelItem> items;
    for (const std::size_t node : chosen) {
        items.push_back(LevelItem::entity(m_nodes[node].entity));
    }
    document.setSelection(withWholeEntities(document.level(), items));
    m_syncedGraph.clear();
    for (const std::size_t node : chosen) {
        m_syncedGraph.push_back(m_nodes[node].entity);
    }
    m_syncedLevel = document.selection();
}

std::vector<std::size_t> LogicPanel::selectedNodes() const {
    std::vector<ed::NodeId> ids(
        static_cast<std::size_t>(std::max(ed::GetSelectedObjectCount(), 0)));
    ids.resize(
        static_cast<std::size_t>(ed::GetSelectedNodes(ids.data(), static_cast<int>(ids.size()))));
    std::vector<std::size_t> nodes;
    for (const ed::NodeId id : ids) {
        const std::size_t entity = (id.Get() - 1) / 4;
        if (entity < m_nodeOf.size() && m_nodeOf[entity] != kNoNode) {
            nodes.push_back(m_nodeOf[entity]);
        }
    }
    std::sort(nodes.begin(), nodes.end());
    return nodes;
}

std::vector<std::size_t> LogicPanel::selectedLinks() const {
    std::vector<ed::LinkId> ids(
        static_cast<std::size_t>(std::max(ed::GetSelectedObjectCount(), 0)));
    ids.resize(
        static_cast<std::size_t>(ed::GetSelectedLinks(ids.data(), static_cast<int>(ids.size()))));
    std::vector<std::size_t> links;
    for (const ed::LinkId id : ids) {
        for (std::size_t i = 0; i < m_links.size(); ++i) {
            if (linkId(m_links[i].from, m_links[i].output, m_links[i].to) == id) {
                links.push_back(i);
            }
        }
    }
    std::sort(links.begin(), links.end());
    return links;
}

void LogicPanel::drawDetails(EditorDocument& document) {
    const Level& level = document.level();
    // As chosen in the graph this frame, if the level hasn't changed since.
    const bool current = m_chosenChange == document.changeCount();
    const std::vector<std::size_t> links = current ? m_chosenLinks : std::vector<std::size_t>{};
    const std::vector<std::size_t> nodes = current ? m_chosenNodes : std::vector<std::size_t>{};
    if (!links.empty()) {
        drawLinkDetails(document, m_links[links.front()]);
        return;
    }
    ImGui::PushTextWrapPos(0.0f);
    if (nodes.empty()) {
        ImGui::TextDisabled(
            "The level's wiring: each entity's inputs on its left, its events on its right, and "
            "a wire from an event to every entity the output names, sending it the input.\n\n"
            "Drag from an event to an input to wire them; an entity without a name is given "
            "one. Click a wire to change its delay or input; Delete takes the chosen wires away "
            "(or the chosen entities).\n\n"
            "Choosing a node chooses its entity in the level, and double-clicking it shows it "
            "in the view. Nodes stay where they are dragged, saved with the level. F shows "
            "what is chosen, or everything.\n\n"
            "The inspector's outputs table is the same wiring as text.");
        ImGui::PopTextWrapPos();
        return;
    }
    for (const std::size_t index : nodes) {
        const Node& node = m_nodes[index];
        const LevelEntity& entity = level.entities[node.entity];
        ImGui::SeparatorText(entityLabel(level, node.entity).c_str());
        if (const EntityClassInfo* info = findEntityClass(entity.className)) {
            ImGui::TextDisabled("%s", std::string(info->description).c_str());
        }
        for (const EntityOutput& output : entity.outputs) {
            const std::string delay =
                output.delay > 0.0f ? std::format(" after {} s", formatFloat(output.delay)) : "";
            ImGui::TextUnformatted(
                std::format("{} -> {} {}{}", output.event, output.target, output.input, delay)
                    .c_str());
        }
        for (const std::string& problem : node.problems) {
            ImGui::TextColored(kProblemColour, "%s", problem.c_str());
        }
    }
    ImGui::TextDisabled("Its settings and outputs are in the inspector.");
    ImGui::PopTextWrapPos();
}

void LogicPanel::drawLinkDetails(EditorDocument& document, const Link& link) {
    const Level& level = document.level();
    if (link.from >= level.entities.size() ||
        link.output >= level.entities[link.from].outputs.size()) {
        return;
    }
    // Copies: the edits below replace them in the level.
    const LevelEntity source = level.entities[link.from];
    const EntityOutput output = source.outputs[link.output];
    ImGui::SeparatorText("Wire");
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(std::format("When {} {}, send {} to {}.", entityLabel(level, link.from),
                                       output.event, output.input, entityLabel(level, link.to))
                               .c_str());
    if (const std::size_t named = entitiesNamed(level, output.target); named > 1) {
        ImGui::TextDisabled("It goes to all %zu entities named '%s'.", named,
                            output.target.c_str());
    }
    if (!link.known) {
        const auto has = [](const std::vector<SignalInfo>& signals, std::string_view name) {
            return std::any_of(signals.begin(), signals.end(),
                               [&](const SignalInfo& signal) { return signal.name == name; });
        };
        if (!has(entitySignals(source, m_scripts, true), output.event)) {
            ImGui::TextColored(kProblemColour, "%s has no event '%s'.",
                               entityLabel(level, link.from).c_str(), output.event.c_str());
        }
        if (!has(entitySignals(level.entities[link.to], m_scripts, false), output.input)) {
            ImGui::TextColored(kProblemColour, "%s has no input '%s'.",
                               entityLabel(level, link.to).c_str(), output.input.c_str());
        }
    }
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    const auto commit = [&](EntityOutput changed, const char* name) {
        LevelEntity edited = source;
        edited.outputs[link.output] = std::move(changed);
        document.execute(makeSetEntity(name, link.from, source, std::move(edited)));
    };
    const auto signalCombo =
        [&](const char* label, const std::string& current,
            const std::vector<SignalInfo>& signals) -> std::optional<std::string> {
        std::optional<std::string> chosen;
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::BeginCombo(label, current.c_str())) {
            for (const SignalInfo& signal : signals) {
                const std::string name(signal.name);
                if (ImGui::Selectable(name.c_str(), name == current)) {
                    chosen = name;
                }
                if (!signal.description.empty()) {
                    ImGui::SetItemTooltip("%s", std::string(signal.description).c_str());
                }
            }
            ImGui::EndCombo();
        }
        return chosen;
    };
    if (const std::optional<std::string> event =
            signalCombo("when", output.event, entitySignals(source, m_scripts, true))) {
        EntityOutput changed = output;
        changed.event = *event;
        commit(std::move(changed), "Change wire's event");
    }
    if (const std::optional<std::string> input = signalCombo(
            "input", output.input, entitySignals(level.entities[link.to], m_scripts, false))) {
        EntityOutput changed = output;
        changed.input = *input;
        commit(std::move(changed), "Change wire's input");
    }
    // Dragged live, and one command when let go.
    float delay = output.delay;
    ImGui::SetNextItemWidth(160.0f);
    const bool dragged = dragNumber("delay (s)", delay, 0.0f, kMaxOutputDelay);
    if (ImGui::IsItemActivated()) {
        m_entityBefore = source;
    }
    if (dragged) {
        LevelEntity preview = source;
        preview.outputs[link.output].delay = std::clamp(delay, 0.0f, kMaxOutputDelay);
        document.previewEntity(link.from, preview);
    }
    if (ImGui::IsItemDeactivated() && m_entityBefore) {
        const LevelEntity after = document.level().entities[link.from];
        if (after != *m_entityBefore) {
            document.execute(
                makeSetEntity("Change wire's delay", link.from, *m_entityBefore, after));
        }
        m_entityBefore.reset();
    }
    ImGui::Spacing();
    if (ImGui::Button("Take the wire away")) {
        LevelEntity edited = source;
        edited.outputs.erase(edited.outputs.begin() + static_cast<std::ptrdiff_t>(link.output));
        document.execute(makeSetEntity("Remove wire", link.from, source, std::move(edited)));
    }
    ImGui::SetItemTooltip("Delete in the graph");
}

} // namespace ct
