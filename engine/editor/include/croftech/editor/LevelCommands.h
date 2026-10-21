#pragma once

#include "croftech/editor/CommandStack.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace ct {

// The level edits, by what they do to the brush and entity lists. Each stores what it needs to
// undo itself. Indices refer to the lists at the moment the command runs; the linear history
// guarantees they are the same when it is undone.

std::unique_ptr<Command> makeSetBrush(std::string name, std::size_t index, Brush before,
                                      Brush after);
std::unique_ptr<Command> makeInsertBrush(std::string name, std::size_t index, Brush brush);
std::unique_ptr<Command> makeRemoveBrush(std::string name, std::size_t index, Brush brush);

std::unique_ptr<Command> makeSetEntity(std::string name, std::size_t index, LevelEntity before,
                                       LevelEntity after);
std::unique_ptr<Command> makeInsertEntity(std::string name, std::size_t index, LevelEntity entity);
std::unique_ptr<Command> makeRemoveEntity(std::string name, std::size_t index, LevelEntity entity);

// Several commands as one: applied in order and reverted in reverse, so an edit of many items
// undoes in one step. Each command's indices refer to the lists as the commands before it left
// them.
std::unique_ptr<Command> makeCompound(std::string name,
                                      std::vector<std::unique_ptr<Command>> commands);

} // namespace ct
