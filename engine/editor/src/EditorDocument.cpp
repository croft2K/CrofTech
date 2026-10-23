#include "croftech/editor/EditorDocument.h"

#include "croftech/core/Assert.h"
#include "croftech/level/LevelFile.h"

#include <algorithm>
#include <atomic>
#include <utility>

namespace ct {
namespace {

// Far enough apart that no document's changes reach the next one's start.
std::uint64_t firstChangeCount() {
    static std::atomic<std::uint64_t> documents{0};
    return documents.fetch_add(1) << 40;
}

} // namespace

EditorDocument::EditorDocument(Level level, std::filesystem::path path)
    : m_level(std::move(level)), m_path(std::move(path)), m_changeCount(firstChangeCount()) {}

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
    const std::uint32_t entity = m_level.brushes[index].entity;
    m_level.brushes[index] = brush;
    m_level.brushes[index].entity = entity;
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

bool EditorDocument::isSelected(LevelItem item) const {
    return std::binary_search(m_selection.begin(), m_selection.end(), item);
}

std::optional<LevelItem> EditorDocument::singleSelected() const {
    return m_selection.size() == 1 ? std::optional<LevelItem>(m_selection.front()) : std::nullopt;
}

void EditorDocument::select(LevelItem item) { setSelection({item}); }

void EditorDocument::setSelection(std::vector<LevelItem> items) {
    std::erase_if(items, [&](LevelItem item) { return !exists(m_level, item); });
    std::sort(items.begin(), items.end());
    items.erase(std::unique(items.begin(), items.end()), items.end());
    m_selection = std::move(items);
}

void EditorDocument::toggleSelected(LevelItem item) {
    const auto position = std::lower_bound(m_selection.begin(), m_selection.end(), item);
    if (position != m_selection.end() && *position == item) {
        m_selection.erase(position);
    } else if (exists(m_level, item)) {
        m_selection.insert(position, item);
    }
}

void EditorDocument::changed() {
    ++m_changeCount;
    // Drops items that undo or redo removed.
    std::erase_if(m_selection, [&](LevelItem item) { return !exists(m_level, item); });
}

} // namespace ct
