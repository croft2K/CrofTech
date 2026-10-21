#pragma once

#include "croftech/editor/CommandStack.h"
#include "croftech/editor/LevelItem.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ct {

// The level edits, by what they do to the brush and entity lists. Each stores what it needs to
// undo itself. Indices refer to the lists at the moment the command runs; the linear history
// guarantees they are the same when it is undone.

// Changes a brush's shape. Its entity, name and group stay as they were (after's are ignored), so
// edits that rebuild brushes from their corners can't drop them out of their door or group.
std::unique_ptr<Command> makeSetBrush(std::string name, std::size_t index, Brush before,
                                      Brush after);

// Where a brush belongs: an entity, or the world (kNoEntity) and maybe a group there.
struct BrushOwner {
    std::uint32_t entity = kNoEntity;
    std::uint32_t group = kNoGroup;
};

// Moves a brush to an entity, or to the world and a group.
std::unique_ptr<Command> makeSetBrushOwner(std::string name, std::size_t index, BrushOwner before,
                                           BrushOwner after);
std::unique_ptr<Command> makeSetBrushName(std::string name, std::size_t index, std::string before,
                                          std::string after);
std::unique_ptr<Command> makeInsertBrush(std::string name, std::size_t index, Brush brush);
std::unique_ptr<Command> makeRemoveBrush(std::string name, std::size_t index, Brush brush);

// Its group stays as it was (after.group is ignored); makeSetItemGroup changes that.
std::unique_ptr<Command> makeSetEntity(std::string name, std::size_t index, LevelEntity before,
                                       LevelEntity after);
// Renumber the brushes of the entities after the index (insertEntity and removeEntity in Level.h).
// A removed entity must have no brushes left.
std::unique_ptr<Command> makeInsertEntity(std::string name, std::size_t index, LevelEntity entity);
std::unique_ptr<Command> makeRemoveEntity(std::string name, std::size_t index, LevelEntity entity);

// Puts a world brush or an entity into a group, or takes it out with kNoGroup.
std::unique_ptr<Command> makeSetItemGroup(std::string name, LevelItem item, std::uint32_t before,
                                          std::uint32_t after);
// Renumber the groups after the index (insertGroup and removeGroup in Level.h). A removed group
// must have no members left.
std::unique_ptr<Command> makeInsertGroup(std::string name, std::size_t index, LevelGroup group);
std::unique_ptr<Command> makeRemoveGroup(std::string name, std::size_t index, LevelGroup group);
std::unique_ptr<Command> makeSetGroup(std::string name, std::size_t index, LevelGroup before,
                                      LevelGroup after);

// Several commands as one: applied in order and reverted in reverse, so an edit of many items
// undoes in one step. Each command's indices refer to the lists as the commands before it left
// them.
std::unique_ptr<Command> makeCompound(std::string name,
                                      std::vector<std::unique_ptr<Command>> commands);

} // namespace ct
