#pragma once

#include "croftech/core/Math.h"
#include "croftech/editor/EditorDocument.h"

namespace ct {

// Edits on the selection, shared by the menus, shortcuts and inspector. Each is one command.

// Deletes the selected brush or entity and clears the selection.
bool deleteSelection(EditorDocument& document);

// Copies the selected brush or entity, moved by offset, and selects the copy.
bool duplicateSelection(EditorDocument& document, Vec3 offset);

// Adds an entity at the end of the list and selects it.
void addEntity(EditorDocument& document, LevelEntity entity);

// Adds a box brush at the end of the list and selects it.
void addBox(EditorDocument& document, Vec3 min, Vec3 max);

} // namespace ct
