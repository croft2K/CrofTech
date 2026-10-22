#pragma once

// What the nav module's sources share about how it uses Recast and Detour.

#include "croftech/core/Math.h"
#include "croftech/nav/NavMesh.h"

#include <DetourNavMesh.h>

#include <array>
#include <cstdint>
#include <type_traits>

namespace ct::nav {

static_assert(std::is_same_v<dtPolyRef, std::uint64_t>,
              "Detour must be built with 64-bit polygon references (vcpkg-overlays)");

// Recast and Detour have y up; the engine has z up. The turn about x between them keeps the
// winding of everything Recast builds.
inline std::array<float, 3> toDetour(Vec3 v) { return {v.x, v.z, -v.y}; }
inline Vec3 fromDetour(const float* v) { return {v[0], -v[2], v[1]}; }

// Detour's area of each polygon is the NavArea; every polygon has the one flag, since the query
// filter checks areas and movers itself.
inline constexpr std::uint16_t kWalkableFlag = 1;

inline unsigned char detourArea(NavArea area) { return static_cast<unsigned char>(area); }
inline NavArea navArea(unsigned char area) {
    return area <= static_cast<unsigned char>(NavArea::Hazard) ? static_cast<NavArea>(area)
                                                               : NavArea::Ground;
}

} // namespace ct::nav
