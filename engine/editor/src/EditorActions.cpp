#include "croftech/editor/EditorActions.h"

#include "croftech/editor/BrushEditing.h"
#include "croftech/editor/LevelCommands.h"
#include "croftech/editor/SelectionDrag.h"
#include "croftech/level/BrushGeometry.h"

#include <array>
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

} // namespace

std::string describeSelection(const EditorDocument& document) {
    const auto& selection = document.selection();
    if (selection.size() == 1) {
        return selection.front().kind == LevelItem::Kind::Brush ? "brush" : "entity";
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

bool deleteSelection(EditorDocument& document) {
    const auto& selection = document.selection();
    if (selection.empty()) {
        return false;
    }
    // Last first, so removing one doesn't shift the indices of the rest.
    const Level& level = document.level();
    std::vector<std::unique_ptr<Command>> commands;
    for (auto it = selection.rbegin(); it != selection.rend(); ++it) {
        if (it->kind == LevelItem::Kind::Brush) {
            commands.push_back(
                makeRemoveBrush("Delete brush", it->index, level.brushes[it->index]));
        } else {
            commands.push_back(
                makeRemoveEntity("Delete entity", it->index, level.entities[it->index]));
        }
    }
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
    for (const LevelItem& item : selection) {
        if (item.kind == LevelItem::Kind::Brush) {
            std::optional<Brush> copy = transformedBrush(level.brushes[item.index], move);
            if (!copy) {
                return false;
            }
            commands.push_back(makeInsertBrush("Duplicate brush", nextBrush, std::move(*copy)));
            copies.push_back(LevelItem::brush(nextBrush++));
        } else {
            commands.push_back(
                makeInsertEntity("Duplicate entity", nextEntity,
                                 transformedEntity(level.entities[item.index], move)));
            copies.push_back(LevelItem::entity(nextEntity++));
        }
    }
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

} // namespace ct
