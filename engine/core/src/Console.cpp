#include "croftech/core/Console.h"

#include "croftech/core/Assert.h"
#include "croftech/core/Log.h"

#include <algorithm>
#include <format>
#include <utility>

namespace ct {
namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }

// Splits text into commands, and each command into words.
std::vector<std::vector<std::string>> tokenize(std::string_view text) {
    std::vector<std::vector<std::string>> commands(1);
    std::string word;
    bool inWord = false;
    bool quoted = false;
    const auto endWord = [&] {
        if (inWord) {
            commands.back().push_back(std::move(word));
            word.clear();
            inWord = false;
        }
    };
    for (const char c : text) {
        if (quoted && c != '"' && c != '\n') {
            word.push_back(c);
        } else if (c == '"') {
            quoted = !quoted;
            inWord = true;
        } else if (c == ';' || c == '\n') {
            quoted = false;
            endWord();
            commands.emplace_back();
        } else if (isSpace(c)) {
            endWord();
        } else {
            word.push_back(c);
            inWord = true;
        }
    }
    endWord();
    std::erase_if(commands, [](const auto& words) { return words.empty(); });
    return commands;
}

std::string join(std::span<const std::string> words) {
    std::string joined;
    for (const std::string& word : words) {
        if (!joined.empty()) {
            joined.push_back(' ');
        }
        joined += word;
    }
    return joined;
}

} // namespace

Console::Console() {
    addCommand("help", "How to use the console", [](std::span<const std::string>) {
        logInfo("Type a variable's name to see it, or 'name value' to change it. 'cvars [text]' "
                "lists variables, 'commands' lists commands, and Tab completes names. Separate "
                "several commands with ';'.");
    });
    addCommand("cvars", "List variables, optionally only those whose name contains some text",
               [this](std::span<const std::string> args) {
                   const std::string_view filter =
                       args.empty() ? std::string_view() : std::string_view(args[0]);
                   int count = 0;
                   for (const auto& [name, cvar] : m_cvars) {
                       if (name.find(filter) != std::string::npos) {
                           logInfo("  {} = {}{}", name, cvar->text(),
                                   cvar->isDefault() ? "" : "  (changed)");
                           ++count;
                       }
                   }
                   logInfo("{} variables", count);
               });
    addCommand("commands", "List commands", [this](std::span<const std::string>) {
        for (const auto& [name, command] : m_commands) {
            logInfo("  {} - {}", name, command.description);
        }
    });
    addCommand("echo", "Print the arguments",
               [](std::span<const std::string> args) { logInfo("{}", join(args)); });
    addCommand("reset", "Put variables back to their defaults: reset <name>...",
               [this](std::span<const std::string> args) {
                   for (const std::string& name : args) {
                       const auto found = m_cvars.find(name);
                       if (found == m_cvars.end()) {
                           logWarning("unknown variable '{}'", name);
                       } else {
                           assign(*found->second, found->second->defaultText());
                       }
                   }
               });
    addCommand("toggle", "Flip a 0/1 variable: toggle <name>",
               [this](std::span<const std::string> args) {
                   const auto found = args.size() == 1 ? m_cvars.find(args[0]) : m_cvars.end();
                   if (found == m_cvars.end() || found->second->type() != CVar::Type::Bool) {
                       logWarning("usage: toggle <name of a 0/1 variable>");
                       return;
                   }
                   assign(*found->second, found->second->asBool() ? "0" : "1");
               });
}

const CVar& Console::add(CVar cvar) {
    CT_ASSERT(!m_cvars.contains(cvar.name()) && !m_commands.contains(cvar.name()));
    std::string name = cvar.name();
    auto stored = std::make_unique<CVar>(std::move(cvar));
    const CVar& reference = *stored;
    m_cvars.emplace(std::move(name), std::move(stored));
    return reference;
}

void Console::addCommand(std::string name, std::string description, CommandFn fn) {
    CT_ASSERT(!m_cvars.contains(name) && !m_commands.contains(name));
    m_commands.emplace(std::move(name), Command{std::move(description), std::move(fn)});
}

void Console::removeCommand(std::string_view name) {
    const auto found = m_commands.find(name);
    if (found != m_commands.end()) {
        m_commands.erase(found);
    }
}

const CVar* Console::find(std::string_view name) const {
    const auto found = m_cvars.find(name);
    return found == m_cvars.end() ? nullptr : found->second.get();
}

bool Console::set(std::string_view name, std::string_view value) {
    const auto found = m_cvars.find(name);
    return found != m_cvars.end() && assign(*found->second, value);
}

bool Console::assign(CVar& cvar, std::string_view value) {
    const std::string before = cvar.text();
    if (!cvar.set(value)) {
        return false;
    }
    if (cvar.text() != before) {
        ++m_changeCount;
    }
    return true;
}

void Console::execute(std::string_view text) {
    for (const std::vector<std::string>& words : tokenize(text)) {
        executeCommand(words);
    }
}

void Console::executeCommand(std::span<const std::string> words) {
    const std::string& name = words[0];
    const std::span<const std::string> args = words.subspan(1);

    if (const auto command = m_commands.find(name); command != m_commands.end()) {
        // Copied so a command can safely remove itself or add others.
        const CommandFn fn = command->second.fn;
        fn(args);
        return;
    }

    const auto found = m_cvars.find(name);
    if (found == m_cvars.end()) {
        logWarning("unknown command or variable '{}'", name);
        return;
    }
    CVar& cvar = *found->second;
    if (args.empty()) {
        printVariable(cvar);
        return;
    }
    // Strings take the rest of the line; anything else must be a single word.
    if (cvar.type() != CVar::Type::String && args.size() > 1) {
        logWarning("{} takes one value ({})", name, cvar.describeRange());
        return;
    }
    if (!assign(cvar, cvar.type() == CVar::Type::String ? join(args) : args[0])) {
        logWarning("invalid value '{}' for {} (expected {})", join(args), name,
                   cvar.describeRange());
    }
}

void Console::printVariable(const CVar& cvar) const {
    logInfo("{} = {}  [default {}; {}]", cvar.name(), cvar.text(), cvar.defaultText(),
            cvar.describeRange());
    if (!cvar.description().empty()) {
        logInfo("  {}", cvar.description());
    }
}

std::vector<std::string> Console::complete(std::string_view prefix) const {
    std::vector<std::string> names;
    const auto collect = [&](const auto& map) {
        for (auto it = map.lower_bound(prefix); it != map.end() && it->first.starts_with(prefix);
             ++it) {
            names.push_back(it->first);
        }
    };
    collect(m_cvars);
    collect(m_commands);
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<std::string> Console::applyConfig(std::string_view prefix,
                                              std::span<const LayeredConfig::Layer> layers) {
    for (auto it = m_cvars.lower_bound(prefix);
         it != m_cvars.end() && it->first.starts_with(prefix); ++it) {
        assign(*it->second, it->second->defaultText());
    }
    std::vector<std::string> problems;
    for (const LayeredConfig::Layer& layer : layers) {
        for (const ConfigEntry& entry : layer.entries) {
            const auto found = m_cvars.find(std::string(prefix) + entry.key);
            if (found == m_cvars.end()) {
                problems.push_back(std::format("{}: line {}: unknown setting '{}'",
                                               layer.path.string(), entry.line, entry.key));
            } else if (!assign(*found->second, entry.value)) {
                problems.push_back(std::format(
                    "{}: line {}: invalid value '{}' for {} (expected {})", layer.path.string(),
                    entry.line, entry.value, entry.key, found->second->describeRange()));
            }
        }
    }
    return problems;
}

} // namespace ct
