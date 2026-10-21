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

class InsertEntity final : public Command {
public:
    InsertEntity(std::string name, std::size_t index, LevelEntity entity, bool remove)
        : m_name(std::move(name)), m_index(index), m_entity(std::move(entity)), m_remove(remove) {}

    std::string_view name() const override { return m_name; }
    void apply(Level& level) const override { change(level, !m_remove); }
    void revert(Level& level) const override { change(level, m_remove); }

private:
    void change(Level& level, bool insert) const {
        if (insert) {
            insertEntity(level, m_index, m_entity);
        } else {
            removeEntity(level, m_index);
        }
    }

    std::string m_name;
    std::size_t m_index;
    LevelEntity m_entity;
    bool m_remove;
};

class SetBrushOwner final : public Command {
public:
    SetBrushOwner(std::string name, std::size_t index, BrushOwner before, BrushOwner after)
        : m_name(std::move(name)), m_index(index), m_before(before), m_after(after) {}

    std::string_view name() const override { return m_name; }
    void apply(Level& level) const override { set(level, m_after); }
    void revert(Level& level) const override { set(level, m_before); }

private:
    void set(Level& level, BrushOwner owner) const {
        CT_ASSERT(m_index < level.brushes.size());
        CT_ASSERT(owner.entity == kNoEntity || owner.entity < level.entities.size());
        CT_ASSERT(owner.group == kNoGroup ||
                  (owner.entity == kNoEntity && owner.group < level.groups.size()));
        level.brushes[m_index].entity = owner.entity;
        level.brushes[m_index].group = owner.group;
    }

    std::string m_name;
    std::size_t m_index;
    BrushOwner m_before;
    BrushOwner m_after;
};

class SetBrushName final : public Command {
public:
    SetBrushName(std::string name, std::size_t index, std::string before, std::string after)
        : m_name(std::move(name)), m_index(index), m_before(std::move(before)),
          m_after(std::move(after)) {}

    std::string_view name() const override { return m_name; }
    void apply(Level& level) const override { set(level, m_after); }
    void revert(Level& level) const override { set(level, m_before); }

private:
    void set(Level& level, const std::string& value) const {
        CT_ASSERT(m_index < level.brushes.size());
        level.brushes[m_index].name = value;
    }

    std::string m_name;
    std::size_t m_index;
    std::string m_before;
    std::string m_after;
};

class SetItemGroup final : public Command {
public:
    SetItemGroup(std::string name, LevelItem item, std::uint32_t before, std::uint32_t after)
        : m_name(std::move(name)), m_item(item), m_before(before), m_after(after) {}

    std::string_view name() const override { return m_name; }
    void apply(Level& level) const override { set(level, m_after); }
    void revert(Level& level) const override { set(level, m_before); }

private:
    void set(Level& level, std::uint32_t group) const {
        CT_ASSERT(exists(level, m_item));
        CT_ASSERT(group == kNoGroup || group < level.groups.size());
        if (m_item.kind == LevelItem::Kind::Entity) {
            level.entities[m_item.index].group = group;
        } else {
            CT_ASSERT(level.brushes[m_item.index].entity == kNoEntity);
            level.brushes[m_item.index].group = group;
        }
    }

    std::string m_name;
    LevelItem m_item;
    std::uint32_t m_before;
    std::uint32_t m_after;
};

class InsertGroup final : public Command {
public:
    InsertGroup(std::string name, std::size_t index, LevelGroup group, bool remove)
        : m_name(std::move(name)), m_index(index), m_group(std::move(group)), m_remove(remove) {}

    std::string_view name() const override { return m_name; }
    void apply(Level& level) const override { change(level, !m_remove); }
    void revert(Level& level) const override { change(level, m_remove); }

private:
    void change(Level& level, bool insert) const {
        if (insert) {
            insertGroup(level, m_index, m_group);
        } else {
            removeGroup(level, m_index);
        }
    }

    std::string m_name;
    std::size_t m_index;
    LevelGroup m_group;
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
    after.entity = before.entity;
    after.name = before.name;
    after.group = before.group;
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
    after.group = before.group;
    return std::make_unique<SetElement<LevelEntity>>(std::move(name), &Level::entities, index,
                                                     std::move(before), std::move(after));
}

std::unique_ptr<Command> makeSetBrushOwner(std::string name, std::size_t index, BrushOwner before,
                                           BrushOwner after) {
    return std::make_unique<SetBrushOwner>(std::move(name), index, before, after);
}

std::unique_ptr<Command> makeSetBrushName(std::string name, std::size_t index, std::string before,
                                          std::string after) {
    return std::make_unique<SetBrushName>(std::move(name), index, std::move(before),
                                          std::move(after));
}

std::unique_ptr<Command> makeSetItemGroup(std::string name, LevelItem item, std::uint32_t before,
                                          std::uint32_t after) {
    return std::make_unique<SetItemGroup>(std::move(name), item, before, after);
}

std::unique_ptr<Command> makeInsertGroup(std::string name, std::size_t index, LevelGroup group) {
    return std::make_unique<InsertGroup>(std::move(name), index, std::move(group), false);
}

std::unique_ptr<Command> makeRemoveGroup(std::string name, std::size_t index, LevelGroup group) {
    return std::make_unique<InsertGroup>(std::move(name), index, std::move(group), true);
}

std::unique_ptr<Command> makeSetGroup(std::string name, std::size_t index, LevelGroup before,
                                      LevelGroup after) {
    return std::make_unique<SetElement<LevelGroup>>(std::move(name), &Level::groups, index,
                                                    std::move(before), std::move(after));
}

std::unique_ptr<Command> makeInsertEntity(std::string name, std::size_t index, LevelEntity entity) {
    return std::make_unique<InsertEntity>(std::move(name), index, std::move(entity), false);
}

std::unique_ptr<Command> makeRemoveEntity(std::string name, std::size_t index, LevelEntity entity) {
    return std::make_unique<InsertEntity>(std::move(name), index, std::move(entity), true);
}

std::unique_ptr<Command> makeCompound(std::string name,
                                      std::vector<std::unique_ptr<Command>> commands) {
    return std::make_unique<Compound>(std::move(name), std::move(commands));
}

} // namespace ct
