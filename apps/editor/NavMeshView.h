#pragma once

#include "NavMeshDrawing.h"

#include "croftech/level/Level.h"
#include "croftech/nav/NavMesh.h"

#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace ct {

// The navigation mesh of the level being edited, rebuilt on a worker thread whenever the level
// changes, so editing never waits for it. The last mesh is drawn until the next is ready.
class NavMeshView {
public:
    explicit NavMeshView(const NavAgent& agent) : m_agent(agent) {}
    // Waits for a build in progress.
    ~NavMeshView() = default;
    NavMeshView(const NavMeshView&) = delete;
    NavMeshView& operator=(const NavMeshView&) = delete;

    // Takes a finished build, and starts the next if the level has changed since the last began.
    void update(const Level& level, std::uint64_t changeCount);
    const NavMeshDrawing& drawing() const { return m_drawing; }
    bool building() const { return m_build.valid(); }
    // Of the last mesh built; 0 before the first.
    std::size_t polygonCount() const { return m_mesh ? m_mesh->polygonCount() : 0; }

private:
    NavAgent m_agent;
    std::future<std::unique_ptr<NavMesh>> m_build;
    std::uint64_t m_startedFor = ~0ull; // the change count of the last build started
    std::unique_ptr<NavMesh> m_mesh;
    NavMeshDrawing m_drawing;
    std::vector<std::string> m_warnings; // logged when they change
};

} // namespace ct
