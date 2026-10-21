#include "croftech/editor/EditorActions.h"

#include "croftech/editor/BrushEditing.h"
#include "croftech/editor/LevelCommands.h"

#include <optional>
#include <string>
#include <utility>

namespace ct {

bool deleteSelection(EditorDocument& document) {
    const Selection selection = document.selection();
    const Level& level = document.level();
    switch (selection.kind) {
    case Selection::Kind::Brush:
        document.execute(
            makeRemoveBrush("Delete brush", selection.index, level.brushes[selection.index]));
        break;
    case Selection::Kind::Entity:
        document.execute(
            makeRemoveEntity("Delete entity", selection.index, level.entities[selection.index]));
        break;
    case Selection::Kind::None:
        return false;
    }
    document.select({});
    return true;
}

bool duplicateSelection(EditorDocument& document, Vec3 offset) {
    const Selection selection = document.selection();
    const Level& level = document.level();
    if (selection.kind == Selection::Kind::Brush) {
        Brush copy = translatedBrush(level.brushes[selection.index], offset);
        std::string problem;
        if (!checkBrush(copy, problem)) {
            return false;
        }
        const std::size_t index = level.brushes.size();
        document.execute(makeInsertBrush("Duplicate brush", index, std::move(copy)));
        document.select(Selection::brush(index));
        return true;
    }
    if (selection.kind == Selection::Kind::Entity) {
        LevelEntity copy = level.entities[selection.index];
        if (const std::optional<Vec3> origin = entityOrigin(copy)) {
            setProperty(copy, "origin", formatVec3(*origin + offset));
        }
        const std::size_t index = level.entities.size();
        document.execute(makeInsertEntity("Duplicate entity", index, std::move(copy)));
        document.select(Selection::entity(index));
        return true;
    }
    return false;
}

void addEntity(EditorDocument& document, LevelEntity entity) {
    const std::size_t index = document.level().entities.size();
    std::string name = "Add " + entity.className;
    document.execute(makeInsertEntity(std::move(name), index, std::move(entity)));
    document.select(Selection::entity(index));
}

void addBox(EditorDocument& document, Vec3 min, Vec3 max) {
    const std::size_t index = document.level().brushes.size();
    document.execute(makeInsertBrush("Create box", index, makeBoxBrush(min, max)));
    document.select(Selection::brush(index));
}

} // namespace ct
