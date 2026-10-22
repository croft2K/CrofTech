#pragma once

#include "croftech/nav/NavQuery.h"

#include <memory>
#include <utility>
#include <vector>

namespace ct {

// A NavQuery for each navigation mesh the simulation searches, made the first time each is
// needed. Queries keep working memory between searches, so these belong to one Simulation, and so
// to one thread; their results depend only on the mesh and what is asked.
class NavQueries {
public:
    NavQuery& forMesh(const NavMesh& mesh) {
        for (const auto& [owner, query] : m_queries) {
            if (owner == &mesh) {
                return *query;
            }
        }
        return *m_queries.emplace_back(&mesh, std::make_unique<NavQuery>(mesh)).second;
    }

private:
    std::vector<std::pair<const NavMesh*, std::unique_ptr<NavQuery>>> m_queries;
};

} // namespace ct
