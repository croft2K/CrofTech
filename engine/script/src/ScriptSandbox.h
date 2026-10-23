#pragma once

#include <span>
#include <string_view>

namespace ct {

// What the sandbox gives scripts, for ScriptProgram to reject what it doesn't when a script is
// loaded, with a clearer error than the nil the script would otherwise meet when it runs.

// The maths library: functions whose results are exact, so the same on every platform, and the
// constants pi and huge.
std::span<const std::string_view> scriptMathNames();

// Globals of Luau's standard library that scripts don't get: I/O and the operating system,
// loading code, the environment, the collector and debugging.
std::span<const std::string_view> scriptMissingGlobals();

} // namespace ct
