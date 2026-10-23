#include "croftech/editor/ScriptLinks.h"

#include "croftech/level/EntityClasses.h"

#include <algorithm>
#include <format>

namespace ct {
namespace {

std::string_view nameOf(const LevelEntity& entity) {
    const std::string* name = findProperty(entity, "name");
    return name != nullptr ? std::string_view(*name) : std::string_view();
}

std::string describe(const Level& level, std::uint32_t index) {
    const std::string_view name = nameOf(level.entities[index]);
    if (!name.empty()) {
        return std::format("'{}' (entity {})", name, index);
    }
    return std::format("{} (entity {})", level.entities[index].className, index);
}

std::string_view scriptOf(const LevelEntity& entity) {
    const EntityClassInfo* entityClass = findEntityClass(entity.className);
    if (entityClass == nullptr || !entityClass->scripted ||
        findPropertyInfo(*entityClass, "script") == nullptr) {
        return {};
    }
    return textProperty(entity, *entityClass, "script");
}

bool contains(const std::vector<std::string>& names, std::string_view name) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

} // namespace

std::vector<std::uint32_t> scriptUsers(const Level& level, std::string_view script) {
    std::vector<std::uint32_t> users;
    for (std::uint32_t i = 0; i < level.entities.size(); ++i) {
        if (!script.empty() && scriptOf(level.entities[i]) == script) {
            users.push_back(i);
        }
    }
    return users;
}

std::vector<std::string> entityInputs(const LevelEntity& entity, const ScriptLookup& lookup) {
    const EntityClassInfo* entityClass = findEntityClass(entity.className);
    if (entityClass == nullptr) {
        return {};
    }
    if (entityClass->scripted) {
        const ScriptProgram* program = lookup ? lookup(scriptOf(entity)) : nullptr;
        return program != nullptr ? program->declarations().inputs : std::vector<std::string>{};
    }
    std::vector<std::string> inputs;
    for (const SignalInfo& input : entityClass->inputs) {
        inputs.emplace_back(input.name);
    }
    return inputs;
}

ScriptLinks scriptLinks(const Level& level, std::string_view script, const ScriptProgram* program,
                        const ScriptLookup& lookup) {
    ScriptLinks links;
    links.users = scriptUsers(level, script);
    const auto isUser = [&](std::uint32_t index) {
        return std::find(links.users.begin(), links.users.end(), index) != links.users.end();
    };
    std::vector<ScriptNote> problems;
    std::vector<ScriptNote> remarks;

    for (std::uint32_t from = 0; from < level.entities.size(); ++from) {
        for (const EntityOutput& output : level.entities[from].outputs) {
            bool reached = false;
            for (std::uint32_t to = 0; to < level.entities.size(); ++to) {
                if (nameOf(level.entities[to]) != output.target) {
                    continue;
                }
                reached = true;
                const ScriptWire wire{from,         to,           output.target,
                                      output.event, output.input, output.delay};
                if (isUser(to)) {
                    links.incoming.push_back(wire);
                    if (program != nullptr && !program->findInput(output.input)) {
                        problems.push_back(
                            {0, true,
                             std::format("{} {} sends '{}', which the script has no input for",
                                         describe(level, from), output.event, output.input)});
                    }
                }
                if (isUser(from)) {
                    links.outgoing.push_back(wire);
                    const std::vector<std::string> inputs =
                        entityInputs(level.entities[to], lookup);
                    // Scripts that didn't load have no inputs to check against.
                    const bool unknown = inputs.empty() && !scriptOf(level.entities[to]).empty();
                    if (!unknown && !contains(inputs, output.input)) {
                        problems.push_back({0, true,
                                            std::format("{}: {} has no input '{}'", output.event,
                                                        describe(level, to), output.input)});
                    }
                }
            }
            if (isUser(from) && !reached) {
                links.outgoing.push_back(
                    {from, kNoEntity, output.target, output.event, output.input, output.delay});
                problems.push_back(
                    {0, true,
                     std::format("{}: no entity is named '{}'", output.event, output.target)});
            }
            if (isUser(from) && program != nullptr && !program->findEvent(output.event)) {
                problems.push_back(
                    {0, true,
                     std::format("{} has an output on '{}', which the script doesn't fire",
                                 describe(level, from), output.event)});
            }
        }
    }

    if (program != nullptr) {
        for (const ScriptCallSite& site : program->callSites()) {
            if (site.kind != ScriptCallKind::Send) {
                continue;
            }
            bool named = false;
            for (std::uint32_t to = 0; to < level.entities.size(); ++to) {
                if (nameOf(level.entities[to]) != site.name) {
                    continue;
                }
                named = true;
                const std::vector<std::string> inputs = entityInputs(level.entities[to], lookup);
                const bool unknown = inputs.empty() && !scriptOf(level.entities[to]).empty();
                if (!unknown && !contains(inputs, site.input)) {
                    problems.push_back({site.line, true,
                                        std::format("game:send: {} has no input '{}'",
                                                    describe(level, to), site.input)});
                }
            }
            if (!named) {
                problems.push_back({site.line, true,
                                    std::format("game:send: no entity is named '{}'", site.name)});
            }
        }
    }

    if (links.users.empty()) {
        remarks.push_back({0, false, "no entity in this level runs this script"});
    } else if (program != nullptr) {
        const ScriptDeclarations& declared = program->declarations();
        for (const std::string& input : declared.inputs) {
            const bool wired =
                std::any_of(links.incoming.begin(), links.incoming.end(),
                            [&](const ScriptWire& wire) { return wire.input == input; });
            const bool sentByItself =
                std::any_of(program->callSites().begin(), program->callSites().end(),
                            [&](const ScriptCallSite& site) {
                                return site.kind == ScriptCallKind::After && site.name == input;
                            });
            if (!wired && !sentByItself) {
                remarks.push_back({0, false, std::format("nothing sends input '{}'", input)});
            }
        }
        for (const std::string& event : declared.events) {
            const bool wired =
                std::any_of(links.outgoing.begin(), links.outgoing.end(),
                            [&](const ScriptWire& wire) { return wire.event == event; });
            if (!wired) {
                remarks.push_back({0, false, std::format("nothing is wired to event '{}'", event)});
            }
        }
    }
    links.notes = std::move(problems);
    links.notes.insert(links.notes.end(), remarks.begin(), remarks.end());
    return links;
}

} // namespace ct
