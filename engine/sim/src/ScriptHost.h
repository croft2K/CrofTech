#pragma once

#include "croftech/script/ScriptRunner.h"
#include "croftech/sim/Simulation.h"

#include <string>
#include <string_view>
#include <vector>

namespace ct {

// What scripts need between ticks that isn't state: the runner whose memory every call starts
// afresh in, and what calls had to say until the simulation's owner takes it.
struct ScriptHost {
    ScriptRunner runner;
    std::vector<ScriptMessage> messages;
    // Each behaviour script's last line printed and last error, as the script gave them.
    struct LastMessage {
        std::string script;
        bool error = false;
        std::string text;
    };
    std::vector<LastMessage> lastBehaviourMessages;

    void report(bool error, std::string text);
    // Behaviour scripts run every tick, so a message the same as the script's last of its kind
    // is left out: whether this one is, remembering it if not.
    bool isRepeat(std::string_view script, bool error, std::string_view text);
};

} // namespace ct
