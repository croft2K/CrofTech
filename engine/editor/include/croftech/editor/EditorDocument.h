#pragma once

#include "croftech/editor/CommandStack.h"
#include "croftech/level/Level.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace ct {

struct Selection {
    enum class Kind : std::uint8_t { None, Brush, Entity };

    Kind kind = Kind::None;
    std::size_t index = 0;

    static Selection brush(std::size_t index) { return {Kind::Brush, index}; }
    static Selection entity(std::size_t index) { return {Kind::Entity, index}; }
    friend bool operator==(const Selection&, const Selection&) = default;
};

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

    void previewBrush(std::size_t index, const Brush& brush);
    void previewEntity(std::size_t index, const LevelEntity& entity);

    // Increases with every change to the level, previews included, so views know to rebuild.
    std::uint64_t changeCount() const { return m_changeCount; }
    bool hasUnsavedChanges() const { return m_history.stateId() != m_savedStateId; }

    // Writes a .level file. A level imported from a .map is saved next to it as <name>.level, and
    // the document then refers to that file.
    bool save(std::string& error);

    Selection selection() const { return m_selection; }
    void select(Selection selection);

private:
    void changed();

    Level m_level;
    std::filesystem::path m_path;
    CommandStack m_history;
    std::uint64_t m_savedStateId = 0;
    std::uint64_t m_changeCount = 0;
    Selection m_selection;
};

} // namespace ct
