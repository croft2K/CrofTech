#pragma once

// Tracy instrumentation. Zones compile to nothing unless CROFTECH_PROFILE is on
// (the windows-profile preset), so they are free to leave in hot code.

#if defined(CROFTECH_PROFILE)
#include <tracy/Tracy.hpp>
#define CT_PROFILE_ZONE() ZoneScoped
#define CT_PROFILE_ZONE_NAMED(name) ZoneScopedN(name)
#define CT_PROFILE_FRAME() FrameMark
#define CT_PROFILE_FRAME_NAMED(name) FrameMarkNamed(name)
#else
#define CT_PROFILE_ZONE() static_cast<void>(0)
#define CT_PROFILE_ZONE_NAMED(name) static_cast<void>(0)
#define CT_PROFILE_FRAME() static_cast<void>(0)
#define CT_PROFILE_FRAME_NAMED(name) static_cast<void>(0)
#endif
