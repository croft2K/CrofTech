#pragma once

#include "croftech/editor/AffineTransform.h"
#include "croftech/editor/EditorDocument.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace ct {

// Moves, rotates or scales the selected items while the mouse drags them: each frame shows the
// items transformed from where they started, and the end records one command for the lot.
class SelectionDrag {
public:
    // Remembers the selected items as they are now.
    explicit SelectionDrag(const EditorDocument& document);

    // False, keeping the previous preview, if a brush would become invalid (say, outside the
    // world).
    bool preview(EditorDocument& document, const AffineTransform& transform);
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
