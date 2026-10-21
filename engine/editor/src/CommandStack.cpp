#include "croftech/editor/CommandStack.h"

#include <utility>

namespace ct {

void CommandStack::execute(std::unique_ptr<Command> command, Level& level) {
    command->apply(level);
    m_undone.clear();
    if (m_done.size() == kMaxHistory) {
        m_done.erase(m_done.begin());
    }
    m_done.push_back({std::move(command), m_nextId++});
}

bool CommandStack::undo(Level& level) {
    if (m_done.empty()) {
        return false;
    }
    m_done.back().command->revert(level);
    m_undone.push_back(std::move(m_done.back()));
    m_done.pop_back();
    return true;
}

bool CommandStack::redo(Level& level) {
    if (m_undone.empty()) {
        return false;
    }
    m_undone.back().command->apply(level);
    m_done.push_back(std::move(m_undone.back()));
    m_undone.pop_back();
    return true;
}

std::string_view CommandStack::undoName() const {
    return m_done.empty() ? std::string_view() : m_done.back().command->name();
}

std::string_view CommandStack::redoName() const {
    return m_undone.empty() ? std::string_view() : m_undone.back().command->name();
}

std::uint64_t CommandStack::stateId() const { return m_done.empty() ? 0 : m_done.back().id; }

} // namespace ct
