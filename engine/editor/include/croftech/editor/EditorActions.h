#pragma once

#include "croftech/core/Math.h"
#include "croftech/editor/AffineTransform.h"
#include "croftech/editor/EditorDocument.h"
#include "croftech/level/BrushGeometry.h"
#include "croftech/level/EntityClasses.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ct {

// Edits on the selection, shared by the menus, shortcuts, toolbar and inspector. Each is one
// command, however many items are selected.

// "brush", "entity", an entity's class when it is selected with its brushes ("door"), or
// "3 items", for command names: "Delete 3 items".
std::string describeSelection(const EditorDocument& document);

// Selects every brush and entity.
void selectAll(EditorDocument& document);

// What a click on an item does: selects it, along with the rest of its entity when wholeEntity
// (withWholeEntities). With toggle, adds those to the selection instead, or takes them out of it
// if the item was selected.
void selectClicked(EditorDocument& document, LevelItem item, bool toggle, bool wholeEntity);

// Deletes the selected items and clears the selection. Deleting an entity deletes its brushes, and
// an entity whose brushes are all deleted goes with them.
bool deleteSelection(EditorDocument& document);

// Copies the selected items, moved by offset, and selects the copies. Copies of an entity's brushes
// belong to the copy of the entity if it is selected too, and to the entity itself if not. False,
// changing nothing, if a copy would not be a valid brush.
bool duplicateSelection(EditorDocument& document, Vec3 offset);

// Makes the selected brushes into a new entity from the preset (a door, a lift, a trigger), taking
// them out of the world or the entity they were part of; an entity left with no brushes is
// deleted. A mover gets the preset's move, or one up by the brushes' height. Selects the new
// entity with its brushes. False if no brushes are selected or the level is full of entities.
bool createBrushEntity(EditorDocument& document, const EntityPreset& preset);

// Adds an entity from the preset made of one box brush of the preset's size, standing centred on
// the point, and selects it.
bool addPresetEntity(EditorDocument& document, const EntityPreset& preset, Vec3 point);

// Makes the selected brushes world geometry again; an entity left with no brushes is deleted.
// False if none of them belonged to an entity.
bool moveSelectionToWorld(EditorDocument& document);

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

// Moves the selected brushes' corners at the positions by offset (withCornersMoved), or removes
// them (withCornersRemoved). Corners shared by several brushes move together, so their seams stay
// closed. False, changing nothing, if any brush would refuse.
bool moveCorners(EditorDocument& document, std::span<const Vec3> corners, Vec3 offset);
bool removeCorners(EditorDocument& document, std::span<const Vec3> corners);

// Which part of a clipped brush to keep: behind the plane, in front of it, or both as two brushes.
enum class ClipKeep { Back, Front, Both };

// Cuts every selected brush the plane crosses and keeps the chosen part (the front part of Both
// becomes a new brush), selecting what is kept. Brushes the plane misses are left alone. False,
// changing nothing, if the plane crosses no selected brush or a part could not be built.
bool clipSelection(EditorDocument& document, const DPlane& plane, ClipKeep keep);

// Cuts the selected brushes out of every other brush they overlap (doorways, windows), then
// deletes them. False, changing nothing, if they overlap no other brush.
bool subtractSelection(EditorDocument& document);

// Replaces each selected brush with walls thickness thick around an empty inside, and selects
// them. False, changing nothing, if a brush is too thin.
bool hollowSelection(EditorDocument& document, float thickness);

// Replaces two or more selected brushes with one around all their corners (their convex hull).
bool mergeSelection(EditorDocument& document);

} // namespace ct
