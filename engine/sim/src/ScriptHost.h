#pragma once

#include "croftech/script/ScriptRunner.h"
#include "croftech/sim/Simulation.h"

#include <string>
#include <vector>

namespace ct {

// What scripts need between ticks that isn't state: the runner whose memory every call starts
// afresh in, and what calls had to say until the simulation's owner takes it.
struct ScriptHost {
    ScriptRunner runner;
    std::vector<ScriptMessage> messages;

    void report(bool error, std::string text);
};

} // namespace ct
