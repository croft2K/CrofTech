#include "croftech/editor/SelectionDrag.h"

#include "croftech/editor/LevelCommands.h"

#include <memory>
#include <optional>

namespace ct {

SelectionDrag::SelectionDrag(const EditorDocument& document) {
    const Level& level = document.level();
    for (const LevelItem& item : document.selection()) {
        if (item.kind == LevelItem::Kind::Brush) {
            m_brushes.emplace_back(item.index, level.brushes[item.index]);
        } else {
            m_entities.emplace_back(item.index, level.entities[item.index]);
        }
    }
}

bool SelectionDrag::preview(EditorDocument& document, const AffineTransform& transform) {
    std::vector<Brush> brushes;
    brushes.reserve(m_brushes.size());
    for (const auto& [index, before] : m_brushes) {
        std::optional<Brush> after = transformedBrush(before, transform);
        if (!after) {
            return false;
        }
        brushes.push_back(std::move(*after));
    }
    for (std::size_t i = 0; i < m_brushes.size(); ++i) {
        document.previewBrush(m_brushes[i].first, brushes[i]);
    }
    for (const auto& [index, before] : m_entities) {
        document.previewEntity(index, transformedEntity(before, transform));
    }
    return true;
}

void SelectionDrag::cancel(EditorDocument& document) {
    for (const auto& [index, before] : m_brushes) {
        document.previewBrush(index, before);
    }
    for (const auto& [index, before] : m_entities) {
        document.previewEntity(index, before);
    }
}

bool SelectionDrag::commit(EditorDocument& document, std::string name) {
    const Level& level = document.level();
    std::vector<std::unique_ptr<Command>> commands;
    for (const auto& [index, before] : m_brushes) {
        if (!(level.brushes[index] == before)) {
            commands.push_back(makeSetBrush(name, index, before, level.brushes[index]));
        }
    }
    for (const auto& [index, before] : m_entities) {
        if (!(level.entities[index] == before)) {
            commands.push_back(makeSetEntity(name, index, before, level.entities[index]));
        }
    }
    if (commands.empty()) {
        return false;
    }
    document.execute(commands.size() == 1 ? std::move(commands.front())
                                          : makeCompound(std::move(name), std::move(commands)));
    return true;
}

} // namespace ct
