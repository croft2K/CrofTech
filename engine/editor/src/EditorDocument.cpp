#include "croftech/editor/EditorDocument.h"

#include "croftech/core/Assert.h"
#include "croftech/level/LevelFile.h"

#include <utility>

namespace ct {

EditorDocument::EditorDocument(Level level, std::filesystem::path path)
    : m_level(std::move(level)), m_path(std::move(path)) {}

void EditorDocument::execute(std::unique_ptr<Command> command) {
    m_history.execute(std::move(command), m_level);
    changed();
}

bool EditorDocument::undo() {
    if (!m_history.undo(m_level)) {
        return false;
    }
    changed();
    return true;
}

bool EditorDocument::redo() {
    if (!m_history.redo(m_level)) {
        return false;
    }
    changed();
    return true;
}

void EditorDocument::previewBrush(std::size_t index, const Brush& brush) {
    CT_ASSERT(index < m_level.brushes.size());
    m_level.brushes[index] = brush;
    ++m_changeCount;
}

void EditorDocument::previewEntity(std::size_t index, const LevelEntity& entity) {
    CT_ASSERT(index < m_level.entities.size());
    m_level.entities[index] = entity;
    ++m_changeCount;
}

bool EditorDocument::save(std::string& error) {
    std::filesystem::path target = m_path;
    if (target.extension() != kLevelFileExtension) {
        target.replace_extension(kLevelFileExtension);
    }
    if (!saveLevelFile(m_level, target, error)) {
        return false;
    }
    m_path = std::move(target);
    m_savedStateId = m_history.stateId();
    return true;
}

void EditorDocument::select(Selection selection) {
    const bool valid =
        (selection.kind == Selection::Kind::Brush && selection.index < m_level.brushes.size()) ||
        (selection.kind == Selection::Kind::Entity && selection.index < m_level.entities.size());
    m_selection = valid ? selection : Selection{};
}

void EditorDocument::changed() {
    ++m_changeCount;
    select(m_selection); // drops a selection that undo or redo removed
}

} // namespace ct
