#pragma once

#include "croftech/editor/AffineTransform.h"
#include "croftech/editor/EditorDocument.h"

#include <cstddef>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace ct {

// Moves, rotates or scales the selected items, or moves corners of the selected brushes, while
// the mouse drags them: each frame shows the items changed from where they started, and the end
// records one command for the lot.
class SelectionDrag {
public:
    // Remembers the selected items as they are now.
    explicit SelectionDrag(const EditorDocument& document);

    // False, keeping the previous preview, if a brush would become invalid (say, outside the
    // world).
    bool preview(EditorDocument& document, const AffineTransform& transform);
    // Shows the selected brushes' corners at the positions moved by offset (withCornersMoved).
    // False, keeping the previous preview, if any brush would refuse the move.
    bool previewCorners(EditorDocument& document, std::span<const Vec3> corners, Vec3 offset);
    // Puts every item back where it started.
    void cancel(EditorDocument& document);
    // Records the change as one command. False if nothing moved.
    bool commit(EditorDocument& document, std::string name);

    bool empty() const { return m_brushes.empty() && m_entities.empty(); }

private:
    std::vector<std::pair<std::size_t, Brush>> m_brushes;
    std::vector<std::pair<std::size_t, LevelEntity>> m_entities;
};

} // namespace ct
