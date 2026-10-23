#pragma once

#include "croftech/editor/CommandStack.h"
#include "croftech/editor/LevelItem.h"
#include "croftech/level/Level.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ct {

// A level open in the editor: its data, file, selection and undo history.
//
// Edits are commands. A drag shows its effect live with preview*(), which changes the level
// without recording anything, then finishes with one command from the state before the drag to
// the state after it, so a whole drag undoes in one step.
class EditorDocument {
public:
    EditorDocument() = default;
    EditorDocument(Level level, std::filesystem::path path);

    const Level& level() const { return m_level; }
    const std::filesystem::path& path() const { return m_path; }

    void execute(std::unique_ptr<Command> command);
    bool undo();
    bool redo();
    const CommandStack& history() const { return m_history; }

    // Changes the brush's shape; it stays in its entity, as with makeSetBrush.
    void previewBrush(std::size_t index, const Brush& brush);
    void previewEntity(std::size_t index, const LevelEntity& entity);

    // Increases with every change to the level, previews included, so views know to rebuild.
    // Each document starts it where no other document in the process has been, so views also
    // rebuild for a different level opened in its place.
    std::uint64_t changeCount() const { return m_changeCount; }
    bool hasUnsavedChanges() const { return m_history.stateId() != m_savedStateId; }

    // Writes a .level file. A level imported from a .map is saved next to it as <name>.level, and
    // the document then refers to that file.
    bool save(std::string& error);

    // The selected items, sorted, with no repeats. Items that undo or redo remove drop out.
    const std::vector<LevelItem>& selection() const { return m_selection; }
    bool isSelected(LevelItem item) const;
    // The item if exactly one is selected.
    std::optional<LevelItem> singleSelected() const;
    void select(LevelItem item); // replaces the selection
    void setSelection(std::vector<LevelItem> items);
    void toggleSelected(LevelItem item);
    void clearSelection() { m_selection.clear(); }

private:
    void changed();

    Level m_level;
    std::filesystem::path m_path;
    CommandStack m_history;
    std::uint64_t m_savedStateId = 0;
    std::uint64_t m_changeCount = 0;
    std::vector<LevelItem> m_selection;
};

} // namespace ct
