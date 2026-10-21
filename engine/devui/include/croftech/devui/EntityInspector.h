#pragma once

#include "croftech/core/Schema.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ct {

// A developer window listing entities and every field of their components, drawn entirely from
// component schemas: a new component or field shows up here with no inspector code. Read-only;
// the editor will add editing through commands, since only the simulation writes its state.
class EntityInspector {
public:
    struct Component {
        const TypeSchema* schema = nullptr;
        std::span<const std::byte> bytes; // schema->size bytes
    };
    struct Entity {
        std::uint32_t index = 0;
        std::uint32_t generation = 0;
        std::vector<Component> components;
    };

    // Call between DevUi::beginFrame() and endFrame(). Clears *open when the window is closed.
    void draw(std::span<const Entity> entities, bool* open);
};

} // namespace ct
