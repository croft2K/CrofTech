#pragma once

#include "croftech/editor/CommandStack.h"

#include <cstddef>
#include <memory>
#include <string>

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

} // namespace ct
