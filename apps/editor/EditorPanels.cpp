#include "EditorPanels.h"

#include "croftech/editor/EditorActions.h"
#include "croftech/editor/LevelCommands.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <format>
#include <string>
#include <utility>

namespace ct {
namespace {

std::string brushLabel(const Brush& brush, std::size_t index) {
    if (const auto box = boxBounds(brush)) {
        const Vec3 size = box->max - box->min;
        return std::format("Brush {}  box {:g} x {:g} x {:g} m##{}", index, size.x, size.y, size.z,
                           index);
    }
    return std::format("Brush {}  {} planes##{}", index, brush.planes.size(), index);
}

std::string commandLabel(const char* verb, std::string_view name, const char* shortcut) {
    return name.empty() ? std::format("{}##{}", verb, shortcut) : std::format("{} {}", verb, name);
}

} // namespace

MenuRequests EditorPanels::drawMenuBar(EditorDocument& document, EditorSettings& settings,
                                       LevelViewport& viewport) {
    MenuRequests requests;
    if (!ImGui::BeginMainMenuBar()) {
        return requests;
    }
    const bool hasSelection = document.selection().kind != Selection::Kind::None;
    if (ImGui::BeginMenu("File")) {
        requests.save = ImGui::MenuItem("Save", "Ctrl+S");
        ImGui::Separator();
        requests.quit = ImGui::MenuItem("Quit");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        const CommandStack& history = document.history();
        if (ImGui::MenuItem(commandLabel("Undo", history.undoName(), "u").c_str(), "Ctrl+Z", false,
                            history.canUndo())) {
            document.undo();
        }
        if (ImGui::MenuItem(commandLabel("Redo", history.redoName(), "r").c_str(), "Ctrl+Y", false,
                            history.canRedo())) {
            document.redo();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, hasSelection)) {
            duplicateSelection(document, {settings.grid(), settings.grid(), 0.0f});
        }
        if (ImGui::MenuItem("Delete", "Del", false, hasSelection)) {
            deleteSelection(document);
        }
        if (ImGui::MenuItem("Deselect", "Esc", false, hasSelection)) {
            document.select({});
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Create")) {
        if (ImGui::MenuItem("Box brush", "B", viewport.tool() == EditorTool::Box)) {
            viewport.setTool(EditorTool::Box);
        }
        requests.addPlayerSpawn = ImGui::MenuItem("Player spawn");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        requests.frameSelection = ImGui::MenuItem("Frame selection", "F", false, hasSelection);
        if (ImGui::BeginMenu("Grid")) {
            for (std::size_t i = 0; i < kGridSizes.size(); ++i) {
                if (ImGui::MenuItem(std::format("{:g} m", kGridSizes[i]).c_str(), nullptr,
                                    settings.gridIndex == i)) {
                    settings.gridIndex = i;
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    // Status on the right: tool, grid and what to do next.
    const char* tool = viewport.tool() == EditorTool::Box ? "Box: drag on a surface" : "Select";
    const std::string status =
        std::format("{}   grid {:g} m  ([ ])   right-drag to fly", tool, settings.grid());
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(status.c_str()).x -
                    ImGui::GetStyle().ItemSpacing.x * 2.0f);
    ImGui::TextDisabled("%s", status.c_str());
    ImGui::EndMainMenuBar();
    return requests;
}

void EditorPanels::drawHierarchy(EditorDocument& document) {
    if (!ImGui::Begin("Hierarchy")) {
        ImGui::End();
        return;
    }
    const Level& level = document.level();
    const Selection selected = document.selection();
    if (ImGui::CollapsingHeader(std::format("Brushes ({})###brushes", level.brushes.size()).c_str(),
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(level.brushes.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto i = static_cast<std::size_t>(row);
                if (ImGui::Selectable(brushLabel(level.brushes[i], i).c_str(),
                                      selected == Selection::brush(i))) {
                    document.select(Selection::brush(i));
                }
            }
        }
    }
    if (ImGui::CollapsingHeader(
            std::format("Entities ({})###entities", level.entities.size()).c_str(),
            ImGuiTreeNodeFlags_DefaultOpen)) {
        for (std::size_t i = 0; i < level.entities.size(); ++i) {
            const std::string label =
                std::format("Entity {}  {}##entity{}", i, level.entities[i].className, i);
            if (ImGui::Selectable(label.c_str(), selected == Selection::entity(i))) {
                document.select(Selection::entity(i));
            }
        }
    }
    ImGui::End();
}

void EditorPanels::drawInspector(EditorDocument& document, const EditorSettings& settings) {
    if (!(document.selection() == m_editing)) {
        m_editing = document.selection();
        m_brushBefore.reset();
        m_activeField = -1;
        m_problem.clear();
    }
    if (ImGui::Begin("Inspector")) {
        switch (document.selection().kind) {
        case Selection::Kind::Brush:
            drawBrushInspector(document, settings);
            break;
        case Selection::Kind::Entity:
            drawEntityInspector(document);
            break;
        case Selection::Kind::None:
            ImGui::TextDisabled("Nothing selected.");
            ImGui::TextDisabled("Click a brush or entity in the view or the hierarchy.");
            break;
        }
        if (!m_problem.empty()) {
            ImGui::Spacing();
            ImGui::TextColored({1.0f, 0.45f, 0.35f, 1.0f}, "%s", m_problem.c_str());
        }
    }
    ImGui::End();
}

void EditorPanels::drawBrushInspector(EditorDocument& document, const EditorSettings& settings) {
    const std::size_t index = document.selection().index;
    const Brush& brush = document.level().brushes[index];
    ImGui::Text("Brush %zu", index);
    ImGui::Separator();

    if (const auto box = boxBounds(brush)) {
        Aabb edited = *box;
        bool changed =
            ImGui::DragFloat3("Min", &edited.min.x, settings.grid() * 0.25f, 0.0f, 0.0f, "%.3f m");
        const bool activated = ImGui::IsItemActivated();
        const bool finished = ImGui::IsItemDeactivatedAfterEdit();
        changed |=
            ImGui::DragFloat3("Max", &edited.max.x, settings.grid() * 0.25f, 0.0f, 0.0f, "%.3f m");
        if (activated || ImGui::IsItemActivated()) {
            m_brushBefore = brush;
        }
        if (changed) {
            const Brush resized = makeBoxBrush(edited.min, edited.max);
            std::string problem;
            if (checkBrush(resized, problem)) {
                document.previewBrush(index, resized);
                m_problem.clear();
            } else {
                m_problem = problem;
            }
        }
        if ((finished || ImGui::IsItemDeactivatedAfterEdit()) && m_brushBefore) {
            const Brush& after = document.level().brushes[index];
            if (!(after == *m_brushBefore)) {
                document.execute(makeSetBrush("Resize brush", index, *m_brushBefore, after));
            }
            m_brushBefore.reset();
        }
        const Vec3 size = edited.max - edited.min;
        ImGui::TextDisabled("Size %.3f x %.3f x %.3f m", static_cast<double>(size.x),
                            static_cast<double>(size.y), static_cast<double>(size.z));
    } else {
        ImGui::TextDisabled("%zu planes (normal, distance):", brush.planes.size());
        for (const Plane& plane : brush.planes) {
            ImGui::Text("  %.3f %.3f %.3f   %.3f", static_cast<double>(plane.normal.x),
                        static_cast<double>(plane.normal.y), static_cast<double>(plane.normal.z),
                        static_cast<double>(plane.distance));
        }
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Drag to move, Alt-drag to raise or lower,");
    ImGui::TextDisabled("Shift-drag a face to resize.");
    ImGui::Spacing();
    if (ImGui::Button("Duplicate")) {
        duplicateSelection(document, {settings.grid(), settings.grid(), 0.0f});
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        deleteSelection(document);
    }
}

void EditorPanels::commitEntity(EditorDocument& document, std::size_t index,
                                const LevelEntity& edited, const char* name) {
    std::string problem;
    if (!checkEntity(edited, problem)) {
        m_problem = problem;
        return;
    }
    m_problem.clear();
    document.execute(makeSetEntity(name, index, document.level().entities[index], edited));
}

bool EditorPanels::textField(const char* label, int field, const std::string& current,
                             std::string& result) {
    const bool editing = m_activeField == field;
    std::string scratch = current;
    std::string& text = editing ? m_fieldText : scratch;
    ImGui::InputText(label, &text);
    if (ImGui::IsItemActivated()) {
        m_activeField = field;
        m_fieldText = text;
    }
    if (!ImGui::IsItemDeactivated() || !editing) {
        return false;
    }
    m_activeField = -1;
    if (m_fieldText == current) {
        return false;
    }
    result = std::move(m_fieldText);
    return true;
}

void EditorPanels::drawEntityInspector(EditorDocument& document) {
    const std::size_t index = document.selection().index;
    const LevelEntity& entity = document.level().entities[index];
    ImGui::Text("Entity %zu", index);
    ImGui::Separator();

    // Text fields apply when focus leaves them, as one command each.
    constexpr int kClassField = -2;
    std::string className;
    if (textField("Class", kClassField, entity.className, className)) {
        LevelEntity edited = entity;
        edited.className = className;
        commitEntity(document, index, edited, "Change entity class");
        return;
    }

    if (ImGui::BeginTable("properties", 3, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("remove", ImGuiTableColumnFlags_WidthFixed);
        for (std::size_t i = 0; i < entity.properties.size(); ++i) {
            const EntityProperty& property = entity.properties[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(property.key.c_str());
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            std::string value;
            const bool apply = textField("##value", static_cast<int>(i), property.value, value);
            ImGui::TableNextColumn();
            const bool remove = ImGui::SmallButton("x");
            ImGui::PopID();
            if (apply || remove) {
                LevelEntity edited = entity;
                if (remove) {
                    edited.properties.erase(edited.properties.begin() +
                                            static_cast<std::ptrdiff_t>(i));
                } else {
                    edited.properties[i].value = value;
                }
                commitEntity(document, index, edited,
                             remove ? "Remove entity property" : "Edit entity property");
                ImGui::EndTable();
                return;
            }
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
    ImGui::InputTextWithHint("##newKey", "key", &m_newKey);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    ImGui::InputTextWithHint("##newValue", "value", &m_newValue);
    ImGui::SameLine();
    if (ImGui::Button("Add") && !m_newKey.empty()) {
        if (findProperty(entity, m_newKey) != nullptr) {
            m_problem = std::format("'{}' is already set", m_newKey);
        } else {
            LevelEntity edited = entity;
            edited.properties.push_back({m_newKey, m_newValue});
            commitEntity(document, index, edited, "Add entity property");
            if (m_problem.empty()) {
                m_newKey.clear();
                m_newValue.clear();
            }
        }
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Drag in the view to move, Alt-drag to raise or lower.");
    if (ImGui::Button("Duplicate")) {
        duplicateSelection(document, {1.0f, 0.0f, 0.0f});
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        deleteSelection(document);
    }
}

} // namespace ct
