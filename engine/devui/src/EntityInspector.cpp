#include "croftech/devui/EntityInspector.h"

#include "croftech/core/Angle.h"

#include <imgui.h>

#include <string>

namespace ct {
namespace {

const char* unitSuffix(FieldHint hint) {
    switch (hint) {
    case FieldHint::Distance:
        return " m";
    case FieldHint::Speed:
        return " m/s";
    default:
        return "";
    }
}

std::string flagText(const FieldInfo& field, std::int64_t value) {
    std::string text;
    const auto bits = static_cast<std::uint64_t>(value);
    for (std::size_t bit = 0; bit < 64; ++bit) {
        if ((bits & (1ull << bit)) == 0) {
            continue;
        }
        if (!text.empty()) {
            text += ' ';
        }
        text += bit < field.options.flagNames.size() ? std::string(field.options.flagNames[bit])
                                                     : "bit" + std::to_string(bit);
    }
    return text.empty() ? "-" : text;
}

void drawValue(const FieldInfo& field, std::span<const std::byte> bytes) {
    const char* suffix = unitSuffix(field.options.hint);
    switch (field.type) {
    case FieldType::Float:
        ImGui::Text("%.3f%s", static_cast<double>(readFloat(bytes, field.offset)), suffix);
        return;
    case FieldType::Double:
        ImGui::Text("%.17g%s", readDouble(bytes, field.offset), suffix);
        return;
    case FieldType::Vec3: {
        const Vec3 v = readVec3(bytes, field.offset);
        ImGui::Text("%.3f  %.3f  %.3f%s", static_cast<double>(v.x), static_cast<double>(v.y),
                    static_cast<double>(v.z), suffix);
        return;
    }
    case FieldType::Entity: {
        const EntityRefValue entity = readEntityRef(bytes, field.offset);
        if (entity.index == 0xFFFFFFFFu) {
            ImGui::TextUnformatted("none");
        } else {
            ImGui::Text("entity %u (generation %u)", entity.index, entity.generation);
        }
        return;
    }
    default:
        break;
    }
    const std::int64_t value = readInteger(field, bytes);
    switch (field.options.hint) {
    case FieldHint::Angle:
        ImGui::Text("%.2f deg", static_cast<double>(value) * 360.0 / kAngleUnitsPerTurn);
        return;
    case FieldHint::Flags:
        ImGui::TextUnformatted(flagText(field, value).c_str());
        return;
    default:
        ImGui::Text("%lld", static_cast<long long>(value));
        return;
    }
}

} // namespace

void EntityInspector::draw(std::span<const Entity> entities, bool* open) {
    ImGui::SetNextWindowSize({ImGui::GetFontSize() * 26.0f, ImGui::GetFontSize() * 24.0f},
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Entities", open)) {
        ImGui::End();
        return;
    }
    ImGui::Text("%zu entities", entities.size());
    for (const Entity& entity : entities) {
        ImGui::PushID(static_cast<int>(entity.index));
        if (ImGui::CollapsingHeader(("Entity " + std::to_string(entity.index) + "  (generation " +
                                     std::to_string(entity.generation) + ")")
                                        .c_str(),
                                    ImGuiTreeNodeFlags_DefaultOpen)) {
            for (const Component& component : entity.components) {
                ImGui::SeparatorText(std::string(component.schema->name).c_str());
                if (!ImGui::BeginTable("fields", 2, ImGuiTableFlags_SizingStretchProp)) {
                    continue;
                }
                for (const FieldInfo& field : component.schema->fields) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(field.name.data(),
                                           field.name.data() + field.name.size());
                    if (!field.options.description.empty() && ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%.*s",
                                          static_cast<int>(field.options.description.size()),
                                          field.options.description.data());
                    }
                    ImGui::TableNextColumn();
                    drawValue(field, component.bytes);
                }
                ImGui::EndTable();
            }
        }
        ImGui::PopID();
    }
    ImGui::End();
}

} // namespace ct
