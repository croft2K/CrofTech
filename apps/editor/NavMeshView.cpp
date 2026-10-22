#include "NavMeshView.h"

#include "croftech/core/Log.h"
#include "croftech/sim/GameLevel.h"

#include <algorithm>
#include <chrono>
#include <span>

namespace ct {

void NavMeshView::update(const Level& level, std::uint64_t changeCount) {
    if (m_build.valid() && m_build.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        m_mesh = m_build.get();
        m_drawing.set(*m_mesh);
        const std::span<const std::string> warnings = m_mesh->warnings();
        if (!std::equal(warnings.begin(), warnings.end(), m_warnings.begin(), m_warnings.end())) {
            m_warnings.assign(warnings.begin(), warnings.end());
            for (const std::string& warning : m_warnings) {
                logWarning("Navigation mesh: {}", warning);
            }
        }
    }
    if (!m_build.valid() && changeCount != m_startedFor) {
        m_startedFor = changeCount;
        m_build = std::async(std::launch::async, [input = navInput(level), agent = m_agent] {
            return std::make_unique<NavMesh>(input, agent);
        });
    }
}

} // namespace ct
