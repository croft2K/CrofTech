#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ct {

// A script is a Luau file, scripts/<name>.luau, that a level's script entities run. It returns a
// table declaring what it keeps and what it can be wired to:
//
//     return {
//         state = { presses = 0, open = false }, -- numbers and booleans, kept between calls
//         events = { "on_solved" },              -- fired with game:fire, wired like any event
//         inputs = {                             -- sent by other entities' outputs
//             press = function(game) ... end,
//         },
//         start = function(game) ... end,        -- optional: once, when play begins
//     }
//
// A script that a behaviour tree's script node names (sim/BehaviourTree.h) instead returns a table
// with just a decide function, which answers for the enemy whose tree reached it:
//
//     return {
//         decide = function(game, me) return me.health < 30 and me.target ~= nil end,
//     }
//
// Every call runs in a fresh sandbox (ScriptRunner), so nothing survives a call except the
// declared state, which the game keeps in the simulated state; that is how scripts are saved,
// rewound and sent like everything else.

// The text of a script, by its name (the file scripts/<name>.luau without folder or extension).
struct ScriptSource {
    std::string name;
    std::string text;
};

// Limits on scripts, which may come with a level downloaded from a server.
inline constexpr std::size_t kMaxScriptBytes = 256 * 1024;
inline constexpr std::uint32_t kMaxScriptState = 16;
inline constexpr std::uint32_t kMaxScriptEvents = 64;
inline constexpr std::uint32_t kMaxScriptInputs = 64;

struct ScriptVariable {
    std::string name;
    bool flag = false; // a boolean, kept as 1 or 0; otherwise a number
    double initial = 0.0;
};

struct ScriptDeclarations {
    std::vector<ScriptVariable> state; // sorted by name
    std::vector<std::string> events;   // as listed
    std::vector<std::string> inputs;   // sorted by name
    bool start = false;
    bool decide = false; // a behaviour script's
};

// Whether the name is one a script may have: 1 to 64 lowercase letters, digits, '-' or '_', like
// weapons and enemies. It is part of a file path, so nothing else is allowed.
bool isValidScriptName(std::string_view name);

// Why a script didn't load.
struct ScriptError {
    std::string text;       // "name:line: message", or without the line when there is none
    std::uint32_t line = 0; // counting from 1; 0 when the problem isn't at a line
};

enum class ScriptCallKind : std::uint8_t { Fire, After, Send };

// A call of game:fire, game:after or game:send written with its names in quotes, as most are, so
// they can be checked before the script runs: fire and after against what the script declares
// (when it loads), send against the level (by the game as it builds the level, and by tools).
struct ScriptCallSite {
    ScriptCallKind kind = ScriptCallKind::Fire;
    std::string name;  // the event (Fire), the script's own input (After), the entity (Send)
    std::string input; // Send: the input it sends
    std::uint32_t line = 0;
};

// A script compiled for the sandbox, with what it declares. Loading rejects anything that would
// play differently on different platforms: the ^ operator (pow differs between C libraries),
// maths library functions other than the exact ones, and the optimize and native comments (which
// fold builtins with the compiling machine's maths). It also rejects game:fire and game:after
// calls naming an event or input in quotes that the script doesn't declare, which could only
// fail.
class ScriptProgram {
public:
    // Compiles and checks the source, then runs it once to read its declarations.
    static std::optional<ScriptProgram> load(std::string_view name, std::string_view source,
                                             ScriptError& error);
    static std::optional<ScriptProgram> load(std::string_view name, std::string_view source,
                                             std::string& error);

    const std::string& name() const { return m_name; }
    const std::string& bytecode() const { return m_bytecode; }
    const ScriptDeclarations& declarations() const { return m_declarations; }
    // The calls on a table named game (the name scripts give it) with names in quotes, in the
    // order they are written.
    const std::vector<ScriptCallSite>& callSites() const { return m_callSites; }
    std::optional<std::uint32_t> findInput(std::string_view input) const;
    std::optional<std::uint32_t> findEvent(std::string_view event) const;

private:
    std::string m_name;
    std::string m_bytecode;
    ScriptDeclarations m_declarations;
    std::vector<ScriptCallSite> m_callSites;
};

} // namespace ct
