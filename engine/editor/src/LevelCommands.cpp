#include "croftech/editor/LevelCommands.h"

#include "croftech/core/Assert.h"

#include <iterator>
#include <utility>
#include <vector>

namespace ct {
namespace {

template <typename T>
using LevelList = std::vector<T> Level::*;

template <typename T>
class SetElement final : public Command {
public:
    SetElement(std::string name, LevelList<T> list, std::size_t index, T before, T after)
        : m_name(std::move(name)), m_list(list), m_index(index), m_before(std::move(before)),
          m_after(std::move(after)) {}

    std::string_view name() const override { return m_name; }
    void apply(Level& level) const override { set(level, m_after); }
    void revert(Level& level) const override { set(level, m_before); }

private:
    void set(Level& level, const T& value) const {
        std::vector<T>& list = level.*m_list;
        CT_ASSERT(m_index < list.size());
        list[m_index] = value;
    }

    std::string m_name;
    LevelList<T> m_list;
    std::size_t m_index;
    T m_before;
    T m_after;
};

// Inserts on apply and removes on revert, or the other way round.
template <typename T>
class InsertElement final : public Command {
public:
    InsertElement(std::string name, LevelList<T> list, std::size_t index, T value, bool remove)
        : m_name(std::move(name)), m_list(list), m_index(index), m_value(std::move(value)),
          m_remove(remove) {}

    std::string_view name() const override { return m_name; }
    void apply(Level& level) const override { change(level, !m_remove); }
    void revert(Level& level) const override { change(level, m_remove); }

private:
    void change(Level& level, bool insert) const {
        std::vector<T>& list = level.*m_list;
        const auto position = list.begin() + static_cast<std::ptrdiff_t>(m_index);
        if (insert) {
            CT_ASSERT(m_index <= list.size());
            list.insert(position, m_value);
        } else {
            CT_ASSERT(m_index < list.size());
            list.erase(position);
        }
    }

    std::string m_name;
    LevelList<T> m_list;
    std::size_t m_index;
    T m_value;
    bool m_remove;
};

class Compound final : public Command {
public:
    Compound(std::string name, std::vector<std::unique_ptr<Command>> commands)
        : m_name(std::move(name)), m_commands(std::move(commands)) {}

    std::string_view name() const override { return m_name; }
    void apply(Level& level) const override {
        for (const std::unique_ptr<Command>& command : m_commands) {
            command->apply(level);
        }
    }
    void revert(Level& level) const override {
        for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it) {
            (*it)->revert(level);
        }
    }

private:
    std::string m_name;
    std::vector<std::unique_ptr<Command>> m_commands;
};

} // namespace

std::unique_ptr<Command> makeSetBrush(std::string name, std::size_t index, Brush before,
                                      Brush after) {
    return std::make_unique<SetElement<Brush>>(std::move(name), &Level::brushes, index,
                                               std::move(before), std::move(after));
}

std::unique_ptr<Command> makeInsertBrush(std::string name, std::size_t index, Brush brush) {
    return std::make_unique<InsertElement<Brush>>(std::move(name), &Level::brushes, index,
                                                  std::move(brush), false);
}

std::unique_ptr<Command> makeRemoveBrush(std::string name, std::size_t index, Brush brush) {
    return std::make_unique<InsertElement<Brush>>(std::move(name), &Level::brushes, index,
                                                  std::move(brush), true);
}

std::unique_ptr<Command> makeSetEntity(std::string name, std::size_t index, LevelEntity before,
                                       LevelEntity after) {
    return std::make_unique<SetElement<LevelEntity>>(std::move(name), &Level::entities, index,
                                                     std::move(before), std::move(after));
}

std::unique_ptr<Command> makeInsertEntity(std::string name, std::size_t index, LevelEntity entity) {
    return std::make_unique<InsertElement<LevelEntity>>(std::move(name), &Level::entities, index,
                                                        std::move(entity), false);
}

std::unique_ptr<Command> makeRemoveEntity(std::string name, std::size_t index, LevelEntity entity) {
    return std::make_unique<InsertElement<LevelEntity>>(std::move(name), &Level::entities, index,
                                                        std::move(entity), true);
}

std::unique_ptr<Command> makeCompound(std::string name,
                                      std::vector<std::unique_ptr<Command>> commands) {
    return std::make_unique<Compound>(std::move(name), std::move(commands));
}

} // namespace ct
