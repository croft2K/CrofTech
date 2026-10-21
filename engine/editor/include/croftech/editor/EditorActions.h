#pragma once

#include "croftech/core/Math.h"
#include "croftech/editor/AffineTransform.h"
#include "croftech/editor/EditorDocument.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ct {

// Edits on the selection, shared by the menus, shortcuts, toolbar and inspector. Each is one
// command, however many items are selected.

// "brush", "entity" or "3 items", for command names: "Delete 3 items".
std::string describeSelection(const EditorDocument& document);

// Selects every brush and entity.
void selectAll(EditorDocument& document);

// Deletes the selected items and clears the selection.
bool deleteSelection(EditorDocument& document);

// Copies the selected items, moved by offset, and selects the copies. False, changing nothing, if
// a copy would not be a valid brush.
bool duplicateSelection(EditorDocument& document, Vec3 offset);

// Adds an entity at the end of the list and selects it.
void addEntity(EditorDocument& document, LevelEntity entity);

// Adds a box brush at the end of the list and selects it.
void addBox(EditorDocument& document, Vec3 min, Vec3 max);

// Adds brushes at the end of the list as one command named name, and selects them. False, adding
// nothing, if there are none or the level would have more than kMaxLevelBrushes.
bool addBrushes(EditorDocument& document, std::vector<Brush> brushes, const std::string& name);

// The box around the selected brushes and entity origins: what rotations turn about and the scale
// tool stretches. Nullopt with nothing selected.
std::optional<Aabb> selectionExtent(const EditorDocument& document);

// Applies the transform to every selected item. False, changing nothing, if nothing is selected
// or a brush would become invalid. verb names the command: "Move" gives "Move brush".
bool transformSelection(EditorDocument& document, const AffineTransform& transform,
                        std::string_view verb);

// Quarter turns about a world axis (0 = X, 1 = Y, 2 = Z), and mirroring along one, about the
// centre of the selection. The result is then moved so its extent starts on a grid line, so a
// selection on the grid stays on it.
bool rotateSelection(EditorDocument& document, std::size_t axis, int quarterTurns, float grid);
bool flipSelection(EditorDocument& document, std::size_t axis, float grid);

} // namespace ct
