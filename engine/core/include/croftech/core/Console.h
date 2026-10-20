#pragma once

#include "croftech/core/CVar.h"
#include "croftech/core/Content.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ct {

// Console variables and commands, and the command language that drives them. Config files, the
// command line and the developer console all go through here. Results and errors are logged.
// Use it from the main thread only.
//
// Command language: commands are separated by ';' or new lines; words are separated by spaces, and
// "double quotes" keep spaces in a word. "name" prints a variable, "name value" sets it, and any
// other first word runs that command with the remaining words as arguments.
class Console {
public:
    using CommandFn = std::function<void(std::span<const std::string> args)>;

    Console(); // adds help, cvars, commands, echo, reset and toggle
    Console(const Console&) = delete;
    Console& operator=(const Console&) = delete;

    // The reference stays valid for the console's lifetime. Names must be unique.
    const CVar& add(CVar cvar);
    void addCommand(std::string name, std::string description, CommandFn fn);
    void removeCommand(std::string_view name);

    const CVar* find(std::string_view name) const;
    // Returns false (and changes nothing) for unknown names and invalid values.
    bool set(std::string_view name, std::string_view value);

    void execute(std::string_view text);

    // Command and variable names starting with prefix, sorted.
    std::vector<std::string> complete(std::string_view prefix) const;

    // Counts changes to variable values, so callers can cheaply poll for them.
    std::uint64_t changeCount() const { return m_changeCount; }

    // Applies a layered config file to the variables named "<prefix><key>". Every variable under
    // the prefix is reset first, so deleting a line from a file undoes it. Returns one message per
    // unknown key or invalid value, naming its file and line; valid lines are applied regardless.
    std::vector<std::string> applyConfig(std::string_view prefix,
                                         std::span<const LayeredConfig::Layer> layers);

private:
    struct Command {
        std::string description;
        CommandFn fn;
    };

    void executeCommand(std::span<const std::string> words);
    void printVariable(const CVar& cvar) const;
    bool assign(CVar& cvar, std::string_view value);

    std::map<std::string, std::unique_ptr<CVar>, std::less<>> m_cvars;
    std::map<std::string, Command, std::less<>> m_commands;
    std::uint64_t m_changeCount = 0;
};

} // namespace ct
