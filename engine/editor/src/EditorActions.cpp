#include "croftech/editor/EditorActions.h"

#include "croftech/editor/BrushEditing.h"
#include "croftech/editor/BrushOperations.h"
#include "croftech/editor/LevelCommands.h"
#include "croftech/editor/SelectionDrag.h"
#include "croftech/level/BrushGeometry.h"
#include "croftech/level/EntityClasses.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <memory>
#include <utility>
#include <vector>

namespace ct {
namespace {

void runCommands(EditorDocument& document, const std::string& name,
                 std::vector<std::unique_ptr<Command>> commands) {
    document.execute(commands.size() == 1 ? std::move(commands.front())
                                          : makeCompound(name, std::move(commands)));
}

// Rotates or mirrors about the centre of the selection, then snaps its corner to the grid.
bool turnAboutCentre(EditorDocument& document, const AffineTransform& aboutOrigin, float grid,
                     std::string_view verb) {
    const std::optional<Aabb> extent = selectionExtent(document);
    if (!extent) {
        return false;
    }
    const Vec3 centre = (extent->min + extent->max) * 0.5f;
    const AffineTransform turn =
        AffineTransform::moveBy(-centre).then(aboutOrigin).then(AffineTransform::moveBy(centre));
    Vec3 turnedMin = turn.apply(extent->min);
    for (int corner = 1; corner < 8; ++corner) {
        const Vec3 p{(corner & 1) != 0 ? extent->max.x : extent->min.x,
                     (corner & 2) != 0 ? extent->max.y : extent->min.y,
                     (corner & 4) != 0 ? extent->max.z : extent->min.z};
        turnedMin = min(turnedMin, turn.apply(p));
    }
    const AffineTransform onGrid =
        turn.then(AffineTransform::moveBy(snapToGrid(turnedMin, grid) - turnedMin));
    return transformSelection(document, onGrid, verb);
}

// Entities that have brushes now but would have none if each brush i belonged to owners[i]
// (kNoEntity for a brush being deleted), last first. A door with no brushes is nothing, so these
// are deleted along with the change.
std::vector<std::size_t> emptiedEntities(const Level& level,
                                         std::span<const std::uint32_t> owners) {
    std::vector<std::size_t> before(level.entities.size(), 0);
    std::vector<std::size_t> after(level.entities.size(), 0);
    for (std::size_t i = 0; i < level.brushes.size(); ++i) {
        if (level.brushes[i].entity < before.size()) {
            ++before[level.brushes[i].entity];
        }
        if (owners[i] < after.size()) {
            ++after[owners[i]];
        }
    }
    std::vector<std::size_t> emptied;
    for (std::size_t i = level.entities.size(); i-- > 0;) {
        if (before[i] > 0 && after[i] == 0) {
            emptied.push_back(i);
        }
    }
    return emptied;
}

std::vector<std::uint32_t> brushOwners(const Level& level) {
    std::vector<std::uint32_t> owners;
    owners.reserve(level.brushes.size());
    for (const Brush& brush : level.brushes) {
        owners.push_back(brush.entity);
    }
    return owners;
}

using BrushReplacement = std::pair<std::size_t, std::vector<Brush>>;

// Replaces brushes, by index, with lists of brushes as one command: the first of each list takes
// the brush's place, the rest go on the end, and a brush replaced by nothing is removed (with its
// entity, if that was the entity's last brush). Selects
// the replacements plus keep (items the command doesn't move: entities, and brushes when nothing is
// removed).
void replaceBrushes(EditorDocument& document, const std::string& name,
                    std::vector<BrushReplacement> replacements, std::vector<LevelItem> keep) {
    const Level& level = document.level();
    std::size_t end = level.brushes.size();
    std::vector<std::unique_ptr<Command>> commands;
    std::vector<std::size_t> removed;
    std::vector<std::size_t> added; // indices before any removal
    for (auto& [index, brushes] : replacements) {
        if (brushes.empty()) {
            removed.push_back(index);
            continue;
        }
        commands.push_back(makeSetBrush(name, index, level.brushes[index], brushes.front()));
        added.push_back(index);
        for (std::size_t i = 1; i < brushes.size(); ++i) {
            // Pieces stay in the entity and group of the brush they came from.
            brushes[i].entity = level.brushes[index].entity;
            brushes[i].group = level.brushes[index].group;
            brushes[i].name = level.brushes[index].name;
            commands.push_back(makeInsertBrush(name, end, std::move(brushes[i])));
            added.push_back(end++);
        }
    }
    std::sort(removed.begin(), removed.end());
    std::vector<std::uint32_t> owners = brushOwners(level);
    for (auto it = removed.rbegin(); it != removed.rend(); ++it) {
        commands.push_back(makeRemoveBrush(name, *it, level.brushes[*it]));
        owners[*it] = kNoEntity;
    }
    std::vector<std::size_t> emptied = emptiedEntities(level, owners);
    for (const std::size_t index : emptied) {
        commands.push_back(makeRemoveEntity(name, index, level.entities[index]));
    }
    runCommands(document, name, std::move(commands));

    const auto shift = [](const std::vector<std::size_t>& sorted, std::size_t index) {
        return static_cast<std::size_t>(std::lower_bound(sorted.begin(), sorted.end(), index) -
                                        sorted.begin());
    };
    std::sort(emptied.begin(), emptied.end());
    std::vector<LevelItem> selection;
    for (const LevelItem& item : keep) {
        if (item.kind == LevelItem::Kind::Brush) {
            selection.push_back(item);
        } else if (!std::binary_search(emptied.begin(), emptied.end(), item.index)) {
            selection.push_back(LevelItem::entity(item.index - shift(emptied, item.index)));
        }
    }
    for (const std::size_t index : added) {
        selection.push_back(LevelItem::brush(index - shift(removed, index)));
    }
    document.setSelection(std::move(selection));
}

std::vector<std::size_t> selectedBrushes(const EditorDocument& document) {
    std::vector<std::size_t> brushes;
    for (const LevelItem& item : document.selection()) {
        if (item.kind == LevelItem::Kind::Brush) {
            brushes.push_back(item.index);
        }
    }
    return brushes;
}

std::vector<LevelItem> selectedEntities(const EditorDocument& document) {
    std::vector<LevelItem> entities;
    for (const LevelItem& item : document.selection()) {
        if (item.kind == LevelItem::Kind::Entity) {
            entities.push_back(item);
        }
    }
    return entities;
}

// What groups hold among the items: world brushes and entities, the selected ones and the entities
// of selected brushes. Sorted, with no repeats.
std::vector<LevelItem> topLevelItems(const Level& level, std::span<const LevelItem> items) {
    std::vector<LevelItem> result;
    for (const LevelItem& item : items) {
        if (!exists(level, item)) {
            continue;
        }
        const std::uint32_t entity =
            item.kind == LevelItem::Kind::Brush ? level.brushes[item.index].entity : kNoEntity;
        result.push_back(entity < level.entities.size() ? LevelItem::entity(entity) : item);
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

// How many world brushes and entities each group has.
std::vector<std::size_t> groupSizes(const Level& level) {
    std::vector<std::size_t> sizes(level.groups.size(), 0);
    for (const Brush& brush : level.brushes) {
        if (brush.entity == kNoEntity && brush.group < sizes.size()) {
            ++sizes[brush.group];
        }
    }
    for (const LevelEntity& entity : level.entities) {
        if (entity.group < sizes.size()) {
            ++sizes[entity.group];
        }
    }
    return sizes;
}

// Commands removing the groups that had members before and have none after, last first.
void removeEmptiedGroups(const Level& level, std::span<const std::size_t> before,
                         std::span<const std::size_t> after, const std::string& name,
                         std::vector<std::unique_ptr<Command>>& commands) {
    for (std::size_t i = level.groups.size(); i-- > 0;) {
        if (before[i] > 0 && after[i] == 0) {
            commands.push_back(makeRemoveGroup(name, i, level.groups[i]));
        }
    }
}

std::string unusedName(std::span<const std::string> taken, std::string_view base) {
    std::string name(base);
    for (std::size_t n = 2; std::find(taken.begin(), taken.end(), name) != taken.end(); ++n) {
        name = std::format("{} {}", base, n);
    }
    return name;
}

std::vector<std::string> groupNames(const Level& level) {
    std::vector<std::string> names;
    for (const LevelGroup& group : level.groups) {
        names.push_back(group.name);
    }
    return names;
}

std::size_t brushCountAfter(const Level& level, const std::vector<BrushReplacement>& replacements) {
    std::size_t count = level.brushes.size();
    for (const auto& [index, brushes] : replacements) {
        count = count + brushes.size() - 1;
    }
    return count;
}

} // namespace

std::string describeSelection(const EditorDocument& document) {
    const auto& selection = document.selection();
    if (selection.size() == 1) {
        return selection.front().kind == LevelItem::Kind::Brush ? "brush" : "entity";
    }
    // Entities sort after brushes, so a lone entity with its brushes is last.
    const LevelItem last = selection.back();
    if (last.kind == LevelItem::Kind::Entity && selection.size() >= 2 &&
        selection[selection.size() - 2].kind == LevelItem::Kind::Brush) {
        const Level& level = document.level();
        const bool allItsBrushes =
            std::all_of(selection.begin(), selection.end() - 1, [&](const LevelItem& item) {
                return level.brushes[item.index].entity == last.index;
            });
        if (allItsBrushes) {
            return level.entities[last.index].className;
        }
    }
    return std::format("{} items", selection.size());
}

void selectAll(EditorDocument& document) {
    std::vector<LevelItem> items;
    for (std::size_t i = 0; i < document.level().brushes.size(); ++i) {
        items.push_back(LevelItem::brush(i));
    }
    for (std::size_t i = 0; i < document.level().entities.size(); ++i) {
        items.push_back(LevelItem::entity(i));
    }
    document.setSelection(std::move(items));
}

void selectClicked(EditorDocument& document, LevelItem item, bool toggle, SelectScope scope) {
    const std::vector<LevelItem> clicked{item};
    const std::vector<LevelItem> items =
        scope == SelectScope::Group    ? withWholeGroups(document.level(), clicked)
        : scope == SelectScope::Entity ? withWholeEntities(document.level(), clicked)
                                       : clicked;
    if (!toggle) {
        document.setSelection(items);
        return;
    }
    std::vector<LevelItem> selection = document.selection();
    if (document.isSelected(item)) {
        std::erase_if(selection, [&](const LevelItem& selected) {
            return std::find(items.begin(), items.end(), selected) != items.end();
        });
    } else {
        selection.insert(selection.end(), items.begin(), items.end());
    }
    document.setSelection(std::move(selection));
}

void selectGroup(EditorDocument& document, std::uint32_t group, bool toggle) {
    const std::vector<LevelItem> members = groupMembers(document.level(), group);
    if (!toggle) {
        document.setSelection(members);
        return;
    }
    std::vector<LevelItem> selection = document.selection();
    const bool all = std::all_of(members.begin(), members.end(), [&](const LevelItem& member) {
        return document.isSelected(member);
    });
    if (all) {
        std::erase_if(selection, [&](const LevelItem& selected) {
            return std::binary_search(members.begin(), members.end(), selected);
        });
    } else {
        selection.insert(selection.end(), members.begin(), members.end());
    }
    document.setSelection(std::move(selection));
}

std::string unusedGroupName(const Level& level, std::string_view base) {
    return unusedName(groupNames(level), base);
}

bool groupSelection(EditorDocument& document) {
    const Level& level = document.level();
    const std::vector<LevelItem> members = topLevelItems(level, document.selection());
    if (members.empty() || level.groups.size() >= kMaxLevelGroups) {
        return false;
    }
    const std::string name = "Group";
    const auto added = static_cast<std::uint32_t>(level.groups.size());
    const std::vector<std::size_t> before = groupSizes(level);
    std::vector<std::size_t> after = before;
    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(makeInsertGroup(name, added, {unusedGroupName(level, "Group")}));
    for (const LevelItem& member : members) {
        const std::uint32_t old = itemGroup(level, member);
        commands.push_back(makeSetItemGroup(name, member, old, added));
        if (old < after.size()) {
            --after[old];
        }
    }
    // Every removed group comes before the new one, so each removal moves it down one.
    const std::size_t commandsBefore = commands.size();
    removeEmptiedGroups(level, before, after, name, commands);
    const std::size_t removed = commands.size() - commandsBefore;
    runCommands(document, name, std::move(commands));
    document.setSelection(
        groupMembers(document.level(), static_cast<std::uint32_t>(added - removed)));
    return true;
}

bool ungroupSelection(EditorDocument& document) {
    const Level& level = document.level();
    std::vector<bool> ungroup(level.groups.size(), false);
    bool any = false;
    for (const LevelItem& item : document.selection()) {
        const std::uint32_t group = itemGroup(level, item);
        if (group < ungroup.size()) {
            ungroup[group] = true;
            any = true;
        }
    }
    if (!any) {
        return false;
    }
    const std::string name = "Ungroup";
    std::vector<std::unique_ptr<Command>> commands;
    for (std::size_t i = 0; i < level.brushes.size(); ++i) {
        const Brush& brush = level.brushes[i];
        if (brush.entity == kNoEntity && brush.group < ungroup.size() && ungroup[brush.group]) {
            commands.push_back(makeSetItemGroup(name, LevelItem::brush(i), brush.group, kNoGroup));
        }
    }
    for (std::size_t i = 0; i < level.entities.size(); ++i) {
        const std::uint32_t group = level.entities[i].group;
        if (group < ungroup.size() && ungroup[group]) {
            commands.push_back(makeSetItemGroup(name, LevelItem::entity(i), group, kNoGroup));
        }
    }
    for (std::size_t i = level.groups.size(); i-- > 0;) {
        if (ungroup[i]) {
            commands.push_back(makeRemoveGroup(name, i, level.groups[i]));
        }
    }
    runCommands(document, name, std::move(commands));
    return true;
}

bool renameItem(EditorDocument& document, LevelItem item, std::string_view name,
                std::string& problem) {
    const Level& level = document.level();
    if (!exists(level, item)) {
        problem = "nothing to rename";
        return false;
    }
    if (item.kind == LevelItem::Kind::Brush) {
        if (!name.empty() && !isValidLabel(name)) {
            problem = std::format("a name is up to {} characters, with no spaces at either end",
                                  kMaxNameLength);
            return false;
        }
        const Brush& brush = level.brushes[item.index];
        if (brush.name != name) {
            document.execute(
                makeSetBrushName("Rename brush", item.index, brush.name, std::string(name)));
        }
        return true;
    }
    if (!name.empty() && !isValidEntityName(name)) {
        problem = std::format("an entity's name is up to {} letters, digits, '_', '.' or '-'; "
                              "outputs target it",
                              kMaxNameLength);
        return false;
    }
    const LevelEntity& entity = level.entities[item.index];
    LevelEntity renamed = entity;
    if (name.empty()) {
        std::erase_if(renamed.properties,
                      [](const EntityProperty& property) { return property.key == "name"; });
    } else {
        setProperty(renamed, "name", std::string(name));
    }
    if (!(renamed == entity)) {
        document.execute(makeSetEntity("Rename entity", item.index, entity, std::move(renamed)));
    }
    return true;
}

bool renameGroup(EditorDocument& document, std::uint32_t group, std::string_view name,
                 std::string& problem) {
    const Level& level = document.level();
    if (group >= level.groups.size()) {
        problem = "no such group";
        return false;
    }
    if (!isValidLabel(name)) {
        problem = std::format("a group's name is 1 to {} characters, with no spaces at either end",
                              kMaxNameLength);
        return false;
    }
    if (level.groups[group].name == name) {
        return true;
    }
    for (const LevelGroup& other : level.groups) {
        if (other.name == name) {
            problem = "another group has that name";
            return false;
        }
    }
    document.execute(makeSetGroup("Rename group", group, level.groups[group], {std::string(name)}));
    return true;
}

bool deleteSelection(EditorDocument& document) {
    const auto& selection = document.selection();
    if (selection.empty()) {
        return false;
    }
    const Level& level = document.level();
    const std::vector<std::size_t> groupsBefore = groupSizes(level);
    std::vector<std::size_t> groupsAfter = groupsBefore;
    std::vector<bool> deleteEntity(level.entities.size(), false);
    for (const LevelItem& item : selection) {
        if (item.kind == LevelItem::Kind::Entity) {
            deleteEntity[item.index] = true;
        }
    }
    std::vector<std::uint32_t> owners = brushOwners(level);
    std::vector<std::unique_ptr<Command>> commands;
    // Brushes first, then entities once they have none; each list last first, so removing one
    // doesn't shift the indices of the rest.
    for (std::size_t i = level.brushes.size(); i-- > 0;) {
        const std::uint32_t owner = level.brushes[i].entity;
        if (document.isSelected(LevelItem::brush(i)) ||
            (owner < deleteEntity.size() && deleteEntity[owner])) {
            commands.push_back(makeRemoveBrush("Delete brush", i, level.brushes[i]));
            owners[i] = kNoEntity;
            if (owner == kNoEntity && level.brushes[i].group < groupsAfter.size()) {
                --groupsAfter[level.brushes[i].group];
            }
        }
    }
    for (const std::size_t emptied : emptiedEntities(level, owners)) {
        deleteEntity[emptied] = true;
    }
    for (std::size_t i = level.entities.size(); i-- > 0;) {
        if (deleteEntity[i]) {
            commands.push_back(makeRemoveEntity("Delete entity", i, level.entities[i]));
            if (level.entities[i].group < groupsAfter.size()) {
                --groupsAfter[level.entities[i].group];
            }
        }
    }
    removeEmptiedGroups(level, groupsBefore, groupsAfter, "Delete group", commands);
    runCommands(document, "Delete " + describeSelection(document), std::move(commands));
    document.clearSelection();
    return true;
}

bool duplicateSelection(EditorDocument& document, Vec3 offset) {
    const auto& selection = document.selection();
    if (selection.empty()) {
        return false;
    }
    const Level& level = document.level();
    const AffineTransform move = AffineTransform::moveBy(offset);
    std::vector<std::unique_ptr<Command>> commands;
    std::vector<LevelItem> copies;
    std::size_t nextBrush = level.brushes.size();
    std::size_t nextEntity = level.entities.size();

    // Groups first, so copies can be in them: a new group for each group wholly selected.
    std::vector<std::size_t> selectedOfGroup(level.groups.size(), 0);
    for (const LevelItem& item : topLevelItems(level, selection)) {
        const std::uint32_t group = itemGroup(level, item);
        if (group < selectedOfGroup.size()) {
            ++selectedOfGroup[group];
        }
    }
    const std::vector<std::size_t> sizes = groupSizes(level);
    std::vector<std::string> names = groupNames(level);
    std::vector<std::uint32_t> groupCopy(level.groups.size(), kNoGroup);
    for (std::size_t i = 0; i < level.groups.size(); ++i) {
        if (sizes[i] > 0 && selectedOfGroup[i] == sizes[i] && names.size() < kMaxLevelGroups) {
            groupCopy[i] = static_cast<std::uint32_t>(names.size());
            names.push_back(unusedName(names, level.groups[i].name));
            commands.push_back(makeInsertGroup("Duplicate group", groupCopy[i], {names.back()}));
        }
    }
    const auto copyGroup = [&groupCopy](std::uint32_t group) {
        return group < groupCopy.size() && groupCopy[group] != kNoGroup ? groupCopy[group] : group;
    };

    // Entities next, so the brush copies can belong to them.
    std::vector<std::uint32_t> copyOf(level.entities.size(), kNoEntity);
    for (const LevelItem& item : selection) {
        if (item.kind == LevelItem::Kind::Entity) {
            LevelEntity copy = transformedEntity(level.entities[item.index], move);
            copy.group = copyGroup(copy.group);
            commands.push_back(makeInsertEntity("Duplicate entity", nextEntity, std::move(copy)));
            copies.push_back(LevelItem::entity(nextEntity));
            copyOf[item.index] = static_cast<std::uint32_t>(nextEntity++);
        }
    }
    for (const LevelItem& item : selection) {
        if (item.kind == LevelItem::Kind::Brush) {
            std::optional<Brush> copy = transformedBrush(level.brushes[item.index], move);
            if (!copy) {
                return false;
            }
            if (copy->entity < copyOf.size() && copyOf[copy->entity] != kNoEntity) {
                copy->entity = copyOf[copy->entity];
            }
            copy->group = copyGroup(copy->group);
            commands.push_back(makeInsertBrush("Duplicate brush", nextBrush, std::move(*copy)));
            copies.push_back(LevelItem::brush(nextBrush++));
        }
    }
    std::sort(copies.begin(), copies.end());
    runCommands(document, "Duplicate " + describeSelection(document), std::move(commands));
    document.setSelection(std::move(copies));
    return true;
}

void addEntity(EditorDocument& document, LevelEntity entity) {
    const std::size_t index = document.level().entities.size();
    std::string name = "Add " + entity.className;
    document.execute(makeInsertEntity(std::move(name), index, std::move(entity)));
    document.select(LevelItem::entity(index));
}

void addBox(EditorDocument& document, Vec3 min, Vec3 max) {
    const std::size_t index = document.level().brushes.size();
    document.execute(makeInsertBrush("Create box", index, makeBoxBrush(min, max)));
    document.select(LevelItem::brush(index));
}

bool addBrushes(EditorDocument& document, std::vector<Brush> brushes, const std::string& name) {
    const std::size_t first = document.level().brushes.size();
    if (brushes.empty() || brushes.size() > kMaxLevelBrushes - std::min(first, kMaxLevelBrushes)) {
        return false;
    }
    std::vector<std::unique_ptr<Command>> commands;
    std::vector<LevelItem> added;
    for (Brush& brush : brushes) {
        const std::size_t index = first + added.size();
        commands.push_back(makeInsertBrush(name, index, std::move(brush)));
        added.push_back(LevelItem::brush(index));
    }
    runCommands(document, name, std::move(commands));
    document.setSelection(std::move(added));
    return true;
}

bool createBrushEntity(EditorDocument& document, const EntityPreset& preset) {
    const Level& level = document.level();
    const std::vector<std::size_t> brushes = selectedBrushes(document);
    const EntityClassInfo* entityClass = findEntityClass(preset.className);
    if (brushes.empty() || level.entities.size() >= kMaxLevelEntities ||
        (entityClass != nullptr && entityClass->shape == EntityShape::Point)) {
        return false;
    }
    LevelEntity entity = makePresetEntity(preset);
    if (entityClass != nullptr && findPropertyInfo(*entityClass, "move") != nullptr) {
        Vec3 move = preset.move;
        if (preset.moveUpByHeight) {
            std::optional<Aabb> bounds;
            for (const std::size_t index : brushes) {
                Aabb more;
                if (computeBrushBounds(level.brushes[index], more)) {
                    bounds = bounds ? Aabb{min(bounds->min, more.min), max(bounds->max, more.max)}
                                    : more;
                }
            }
            move = bounds ? Vec3{0.0f, 0.0f, bounds->max.z - bounds->min.z} : Vec3{};
        }
        setProperty(entity, "move", formatVec3(move));
    }

    const std::string name = std::format("Make {}", preset.name);
    const auto added = static_cast<std::uint32_t>(level.entities.size());
    entity.group = itemGroup(level, LevelItem::brush(brushes.front()));
    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(makeInsertEntity(name, added, std::move(entity)));
    std::vector<std::uint32_t> owners = brushOwners(level);
    std::vector<LevelItem> selection;
    for (const std::size_t index : brushes) {
        commands.push_back(makeSetBrushOwner(
            name, index, {owners[index], level.brushes[index].group}, {added, kNoGroup}));
        owners[index] = added;
        selection.push_back(LevelItem::brush(index));
    }
    // Every emptied entity comes before the new one, so each removal moves it down one.
    const std::vector<std::size_t> emptied = emptiedEntities(level, owners);
    for (const std::size_t index : emptied) {
        commands.push_back(makeRemoveEntity(name, index, level.entities[index]));
    }
    runCommands(document, name, std::move(commands));
    selection.push_back(LevelItem::entity(added - emptied.size()));
    document.setSelection(std::move(selection));
    return true;
}

bool addPresetEntity(EditorDocument& document, const EntityPreset& preset, Vec3 point) {
    const Level& level = document.level();
    if (level.entities.size() >= kMaxLevelEntities || level.brushes.size() >= kMaxLevelBrushes) {
        return false;
    }
    const EntityClassInfo* entityClass = findEntityClass(preset.className);
    if (entityClass != nullptr && entityClass->shape == EntityShape::Point) {
        LevelEntity entity = makePresetEntity(preset);
        setProperty(entity, "origin", formatVec3(point));
        const std::size_t index = level.entities.size();
        document.execute(
            makeInsertEntity(std::format("Create {}", preset.name), index, std::move(entity)));
        document.select(LevelItem::entity(index));
        return true;
    }
    const Vec3 half{preset.size.x * 0.5f, preset.size.y * 0.5f, 0.0f};
    Brush brush = makeBoxBrush(point - half, point + half + Vec3{0.0f, 0.0f, preset.size.z});
    std::string problem;
    if (!checkBrush(brush, problem)) {
        return false;
    }
    LevelEntity entity = makePresetEntity(preset);
    if (entityClass != nullptr && findPropertyInfo(*entityClass, "move") != nullptr) {
        setProperty(
            entity, "move",
            formatVec3(preset.moveUpByHeight ? Vec3{0.0f, 0.0f, preset.size.z} : preset.move));
    }
    const std::string name = std::format("Create {}", preset.name);
    const auto entityIndex = static_cast<std::uint32_t>(level.entities.size());
    const std::size_t brushIndex = level.brushes.size();
    brush.entity = entityIndex;
    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(makeInsertEntity(name, entityIndex, std::move(entity)));
    commands.push_back(makeInsertBrush(name, brushIndex, std::move(brush)));
    runCommands(document, name, std::move(commands));
    document.setSelection({LevelItem::brush(brushIndex), LevelItem::entity(entityIndex)});
    return true;
}

bool moveSelectionToWorld(EditorDocument& document) {
    const Level& level = document.level();
    const std::string name = "Move to world";
    std::vector<std::uint32_t> owners = brushOwners(level);
    std::vector<std::unique_ptr<Command>> commands;
    std::vector<LevelItem> selection;
    for (const std::size_t index : selectedBrushes(document)) {
        selection.push_back(LevelItem::brush(index));
        if (owners[index] != kNoEntity) {
            commands.push_back(makeSetBrushOwner(name, index,
                                                 {owners[index], level.brushes[index].group},
                                                 {kNoEntity, level.entities[owners[index]].group}));
            owners[index] = kNoEntity;
        }
    }
    if (commands.empty()) {
        return false;
    }
    for (const std::size_t index : emptiedEntities(level, owners)) {
        commands.push_back(makeRemoveEntity(name, index, level.entities[index]));
    }
    runCommands(document, name, std::move(commands));
    document.setSelection(std::move(selection));
    return true;
}

std::optional<Aabb> selectionExtent(const EditorDocument& document) {
    const Level& level = document.level();
    std::optional<Aabb> extent;
    const auto include = [&](const Aabb& box) {
        extent = extent ? Aabb{min(extent->min, box.min), max(extent->max, box.max)} : box;
    };
    for (const LevelItem& item : document.selection()) {
        if (item.kind == LevelItem::Kind::Brush) {
            Aabb bounds;
            if (computeBrushBounds(level.brushes[item.index], bounds)) {
                include(bounds);
            }
        } else if (const std::optional<Vec3> origin = entityOrigin(level.entities[item.index])) {
            include({*origin, *origin});
        }
    }
    return extent;
}

bool transformSelection(EditorDocument& document, const AffineTransform& transform,
                        std::string_view verb) {
    SelectionDrag drag(document);
    if (drag.empty()) {
        return false;
    }
    if (!drag.preview(document, transform)) {
        drag.cancel(document);
        return false;
    }
    return drag.commit(document, std::format("{} {}", verb, describeSelection(document)));
}

bool rotateSelection(EditorDocument& document, std::size_t axis, int quarterTurns, float grid) {
    return turnAboutCentre(document, AffineTransform::rotation(axis, 90.0 * quarterTurns, {}), grid,
                           "Rotate");
}

bool flipSelection(EditorDocument& document, std::size_t axis, float grid) {
    std::array<double, 3> factors{1.0, 1.0, 1.0};
    factors[axis] = -1.0;
    return turnAboutCentre(
        document, AffineTransform::scale({factors[0], factors[1], factors[2]}, {}), grid, "Flip");
}

bool moveCorners(EditorDocument& document, std::span<const Vec3> corners, Vec3 offset) {
    SelectionDrag drag(document);
    if (corners.empty() || !drag.previewCorners(document, corners, offset)) {
        drag.cancel(document);
        return false;
    }
    return drag.commit(document, corners.size() == 1 ? "Move corner" : "Move corners");
}

bool removeCorners(EditorDocument& document, std::span<const Vec3> corners) {
    const std::string name = corners.size() == 1 ? "Delete corner" : "Delete corners";
    const Level& level = document.level();
    std::vector<std::unique_ptr<Command>> commands;
    for (const LevelItem& item : document.selection()) {
        if (item.kind != LevelItem::Kind::Brush) {
            continue;
        }
        const Brush& before = level.brushes[item.index];
        std::optional<Brush> after = withCornersRemoved(before, corners);
        if (!after) {
            return false;
        }
        if (!(*after == before)) {
            commands.push_back(makeSetBrush(name, item.index, before, std::move(*after)));
        }
    }
    if (commands.empty()) {
        return false;
    }
    runCommands(document, name, std::move(commands));
    return true;
}

bool clipSelection(EditorDocument& document, const DPlane& plane, ClipKeep keep) {
    std::vector<BrushReplacement> replacements;
    std::vector<LevelItem> uncut = selectedEntities(document);
    for (const std::size_t index : selectedBrushes(document)) {
        const std::optional<BrushSplit> split = splitBrush(document.level().brushes[index], plane);
        if (!split) {
            return false;
        }
        if (!split->back || !split->front) {
            uncut.push_back(LevelItem::brush(index));
            continue;
        }
        std::vector<Brush> kept;
        if (keep != ClipKeep::Front) {
            kept.push_back(*split->back);
        }
        if (keep != ClipKeep::Back) {
            kept.push_back(*split->front);
        }
        replacements.emplace_back(index, std::move(kept));
    }
    if (replacements.empty() ||
        brushCountAfter(document.level(), replacements) > kMaxLevelBrushes) {
        return false;
    }
    replaceBrushes(document, "Clip " + describeSelection(document), std::move(replacements),
                   std::move(uncut));
    return true;
}

bool subtractSelection(EditorDocument& document) {
    const Level& level = document.level();
    const std::vector<std::size_t> cutters = selectedBrushes(document);
    if (cutters.empty()) {
        return false;
    }
    std::vector<BrushReplacement> replacements;
    for (std::size_t index = 0; index < level.brushes.size(); ++index) {
        if (document.isSelected(LevelItem::brush(index))) {
            continue;
        }
        std::vector<Brush> pieces{level.brushes[index]};
        for (const std::size_t cutter : cutters) {
            std::vector<Brush> next;
            for (const Brush& piece : pieces) {
                const auto left = subtractBrush(piece, level.brushes[cutter]);
                if (!left) {
                    return false;
                }
                next.insert(next.end(), left->begin(), left->end());
            }
            pieces = std::move(next);
        }
        if (!(pieces.size() == 1 && pieces.front() == level.brushes[index])) {
            replacements.emplace_back(index, std::move(pieces));
        }
    }
    if (replacements.empty()) {
        return false;
    }
    const std::string name = "Subtract " + describeSelection(document);
    for (const std::size_t cutter : cutters) {
        replacements.emplace_back(cutter, std::vector<Brush>{});
    }
    if (brushCountAfter(level, replacements) > kMaxLevelBrushes) {
        return false;
    }
    replaceBrushes(document, name, std::move(replacements), {});
    document.clearSelection();
    return true;
}

bool hollowSelection(EditorDocument& document, float thickness) {
    std::vector<BrushReplacement> replacements;
    for (const std::size_t index : selectedBrushes(document)) {
        std::optional<std::vector<Brush>> walls =
            hollowBrush(document.level().brushes[index], thickness);
        if (!walls || walls->empty()) {
            return false;
        }
        replacements.emplace_back(index, std::move(*walls));
    }
    if (replacements.empty() ||
        brushCountAfter(document.level(), replacements) > kMaxLevelBrushes) {
        return false;
    }
    replaceBrushes(document, "Hollow " + describeSelection(document), std::move(replacements),
                   selectedEntities(document));
    return true;
}

bool mergeSelection(EditorDocument& document) {
    const std::vector<std::size_t> indices = selectedBrushes(document);
    if (indices.size() < 2) {
        return false;
    }
    std::vector<Brush> brushes;
    for (const std::size_t index : indices) {
        brushes.push_back(document.level().brushes[index]);
    }
    std::optional<Brush> merged = mergeBrushes(brushes);
    if (!merged) {
        return false;
    }
    std::vector<BrushReplacement> replacements;
    replacements.emplace_back(indices.front(), std::vector<Brush>{std::move(*merged)});
    for (std::size_t i = 1; i < indices.size(); ++i) {
        replacements.emplace_back(indices[i], std::vector<Brush>{});
    }
    replaceBrushes(document, "Merge " + describeSelection(document), std::move(replacements),
                   selectedEntities(document));
    return true;
}

} // namespace ct
