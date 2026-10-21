#pragma once

#include "croftech/level/Level.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace ct {

// One undoable change to a level. Every edit the editor makes goes through a command, from the
// first edit, so undo and redo always cover everything.
class Command {
public:
    virtual ~Command() = default;
    virtual std::string_view name() const = 0; // shown in the Edit menu: "Undo Move brush"
    virtual void apply(Level& level) const = 0;
    virtual void revert(Level& level) const = 0;
};

// Linear undo history. Executing a command discards anything that was undone.
class CommandStack {
public:
    static constexpr std::size_t kMaxHistory = 1000;

    void execute(std::unique_ptr<Command> command, Level& level);
    bool undo(Level& level);
    bool redo(Level& level);

    bool canUndo() const { return !m_done.empty(); }
    bool canRedo() const { return !m_undone.empty(); }
    std::string_view undoName() const;
    std::string_view redoName() const;

    // Identifies the level's current version: equal values mean the same edits are applied. Used
    // to tell whether there are unsaved changes, including after undoing back to the saved state.
    std::uint64_t stateId() const;

private:
    struct Entry {
        std::unique_ptr<Command> command;
        std::uint64_t id = 0;
    };

    std::vector<Entry> m_done;
    std::vector<Entry> m_undone;
    std::uint64_t m_nextId = 1;
};

} // namespace ct
