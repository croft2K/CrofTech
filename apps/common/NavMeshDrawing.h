#pragma once

#include "croftech/nav/NavMesh.h"
#include "croftech/render/Renderer.h"

#include <vector>

namespace ct {

// A navigation mesh drawn as see-through effects over the world: its floors tinted by area
// (ground green, doors orange, lifts violet, hazards red), each polygon a slightly different shade
// so they can be told apart; its walls and drops outlined; and lift rides as lines between their
// stops. Built once from the mesh, then added to the frame's effects.
class NavMeshDrawing {
public:
    void set(const NavMesh& mesh);
    void clear();
    bool ready() const { return m_ready; }
    void add(std::vector<EffectVertex>& out) const;

private:
    std::vector<EffectVertex> m_vertices;
    bool m_ready = false;
};

} // namespace ct
