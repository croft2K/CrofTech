#pragma once

#include "croftech/level/Level.h"
#include "croftech/script/ScriptProgram.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ct {

// A wire from one entity's event to another's input.
struct ScriptWire {
    std::uint32_t from = 0; // the entity whose output it is
    std::uint32_t to = 0;   // the entity it reaches; kNoEntity if no entity has the name
    std::string target;     // the name it targets
    std::string event;      // of from
    std::string input;      // of to
    float delay = 0.0f;
};

// Something about how a script fits its level: a problem (wiring that names what the script
// doesn't have, a game:send to nothing) or just worth knowing (an input nothing sends).
struct ScriptNote {
    std::uint32_t line = 0; // in the script, counting from 1; 0 for one about the level
    bool problem = true;
    std::string text;
};

// How a script is tied into a level: the script entities that run it, the wires reaching their
// inputs and leaving their events, and the notes on what doesn't line up.
struct ScriptLinks {
    std::vector<std::uint32_t> users; // in level order
    std::vector<ScriptWire> incoming;
    std::vector<ScriptWire> outgoing;
    std::vector<ScriptNote> notes; // problems first, then the rest, each in order found
};

// The program of a script by name, or null; for the inputs of other script entities.
using ScriptLookup = std::function<const ScriptProgram*(std::string_view name)>;

// The script entities that run the script, in level order.
std::vector<std::uint32_t> scriptUsers(const Level& level, std::string_view script);

// For a script that didn't load (program null) there are still users and wires, but nothing to
// check them against.
ScriptLinks scriptLinks(const Level& level, std::string_view script, const ScriptProgram* program,
                        const ScriptLookup& lookup);

// The inputs an entity accepts: its class's, or for a script entity, its script's (none if it
// has none that loaded).
std::vector<std::string> entityInputs(const LevelEntity& entity, const ScriptLookup& lookup);

} // namespace ct
