#include "croftech/editor/ScriptLinks.h"

#include <doctest/doctest.h>

#include <optional>
#include <ostream> // doctest prints string_view through operator<<
#include <string>
#include <utility>
#include <vector>

namespace {

ct::LevelEntity entity(std::string className, std::vector<ct::EntityProperty> properties,
                       std::vector<ct::EntityOutput> outputs = {}) {
    return {std::move(className), std::move(properties), std::move(outputs)};
}

ct::ScriptProgram load(std::string_view name, std::string_view source) {
    std::string error;
    std::optional<ct::ScriptProgram> program = ct::ScriptProgram::load(name, source, error);
    REQUIRE_MESSAGE(program, error);
    return std::move(*program);
}

bool hasNote(const ct::ScriptLinks& links, std::string_view text, std::uint32_t line = 0) {
    for (const ct::ScriptNote& note : links.notes) {
        if (note.text == text && note.line == line) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("a script's links are its users, the wires in and out, and what doesn't line up") {
    const ct::ScriptProgram lock = load("lock", R"(
        return {
            events = { "on_open", "on_spare" },
            inputs = {
                press = function(game) game:fire("on_open") end,
                tick = function(game) end,
                idle = function(game) game:after(1, "tick") end,
            },
            start = function(game)
                game:send("door", "move_to_end")
                game:send("nobody", "add")
                game:send("door", "explode")
            end,
        }
    )");
    const ct::Level level{
        {},
        {entity("trigger", {{"name", "plate"}},
                {{"on_enter", "lock", "press", 0.0f}, {"on_enter", "lock", "unlock", 0.0f}}),
         entity("script", {{"origin", "0 0 0"}, {"name", "lock"}, {"script", "lock"}},
                {{"on_open", "door", "move_to_end", 1.5f},
                 {"on_open", "gone", "add", 0.0f},
                 {"on_closed", "door", "move_to_start", 0.0f},
                 {"on_open", "door", "fly", 0.0f}}),
         entity("mover", {{"name", "door"}, {"move", "0 0 3"}}),
         entity("script", {{"origin", "0 0 0"}, {"name", "other"}, {"script", "other"}})}};
    const ct::ScriptLinks links = ct::scriptLinks(level, "lock", &lock, {});

    CHECK(links.users == std::vector<std::uint32_t>{1});
    REQUIRE(links.incoming.size() == 2);
    CHECK(links.incoming[0].from == 0);
    CHECK(links.incoming[0].input == "press");
    REQUIRE(links.outgoing.size() == 4);
    CHECK(links.outgoing[0].to == 2);
    CHECK(links.outgoing[0].delay == 1.5f);
    CHECK(links.outgoing[1].to == ct::kNoEntity);

    CHECK(hasNote(links, "'plate' (entity 0) on_enter sends 'unlock', which the script has no "
                         "input for"));
    CHECK(hasNote(links, "on_open: no entity is named 'gone'"));
    CHECK(hasNote(links, "'lock' (entity 1) has an output on 'on_closed', which the script "
                         "doesn't fire"));
    CHECK(hasNote(links, "on_open: 'door' (entity 2) has no input 'fly'"));
    CHECK(hasNote(links, "game:send: no entity is named 'nobody'", 11));
    CHECK(hasNote(links, "game:send: 'door' (entity 2) has no input 'explode'", 12));
    // Worth knowing, not wrong: tick is sent by the script itself, idle by nothing.
    CHECK(hasNote(links, "nothing sends input 'idle'"));
    CHECK_FALSE(hasNote(links, "nothing sends input 'tick'"));
    CHECK(hasNote(links, "nothing is wired to event 'on_spare'"));
    CHECK(links.notes.front().problem);
    CHECK_FALSE(links.notes.back().problem);
}

TEST_CASE("a script entity's inputs come from its script") {
    const ct::ScriptProgram other = load("other", "return { inputs = { go = function() end } }");
    const ct::LevelEntity runner = entity("script", {{"origin", "0 0 0"}, {"script", "other"}});
    const ct::ScriptLookup lookup = [&](std::string_view name) {
        return name == "other" ? &other : nullptr;
    };
    CHECK(ct::entityInputs(runner, lookup) == std::vector<std::string>{"go"});
    CHECK(ct::entityInputs(runner, {}).empty());
    CHECK(ct::entityInputs(entity("counter", {}), {}) == std::vector<std::string>{"add", "reset"});

    const ct::Level level{{}, {runner}};
    const ct::ScriptLinks unused = ct::scriptLinks(level, "lock", nullptr, lookup);
    CHECK(unused.users.empty());
    CHECK(hasNote(unused, "no entity in this level runs this script"));
}
