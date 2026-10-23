#include "croftech/script/ScriptProgram.h"
#include "croftech/script/ScriptRunner.h"

#include <doctest/doctest.h>

#include <array>
#include <optional>
#include <ostream> // doctest prints string_view through operator<<
#include <string>
#include <vector>

namespace {

ct::ScriptProgram loadOrFail(std::string_view source) {
    std::string error;
    std::optional<ct::ScriptProgram> program = ct::ScriptProgram::load("test", source, error);
    REQUIRE_MESSAGE(program, error);
    return std::move(*program);
}

// A call with the script's starting state.
struct Caller {
    explicit Caller(std::string_view source) : program(loadOrFail(source)) {
        const std::vector<ct::ScriptVariable>& state = program.declarations().state;
        for (std::size_t i = 0; i < state.size(); ++i) {
            values[i] = state[i].initial;
        }
    }

    bool call(std::string_view input) {
        ct::ScriptCall call;
        call.program = &program;
        call.input = input == "start" ? ct::kScriptStart : *program.findInput(input);
        call.state = std::span(values).first(program.declarations().state.size());
        call.random = random;
        call.time = time;
        const bool finished = runner.run(call);
        random = call.random;
        return finished;
    }

    ct::ScriptProgram program;
    ct::ScriptRunner runner;
    std::array<double, ct::kMaxScriptState> values{};
    std::uint32_t random = 0;
    double time = 0.0;
};

} // namespace

TEST_CASE("a call changes the state and asks for effects in order") {
    Caller caller(R"(
        return {
            state = { count = 0, done = false },
            events = { "on_counted", "on_done" },
            inputs = {
                add = function(game)
                    game.state.count += 1
                    game:fire("on_counted")
                    if game.state.count == 2 then
                        game.state.done = true
                        game:after(1.5, "add")
                        game:send("door", "move_to_end", 0.25)
                        game.fire("on_done")
                    end
                end,
            },
        }
    )");
    REQUIRE(caller.call("add"));
    CHECK(caller.values[0] == 1.0); // count
    CHECK(caller.values[1] == 0.0); // done
    REQUIRE(caller.runner.effects().size() == 1);
    CHECK(caller.runner.effects()[0].kind == ct::ScriptEffectKind::Fire);
    CHECK(caller.runner.effects()[0].index == 0);

    REQUIRE(caller.call("add"));
    CHECK(caller.values[0] == 2.0);
    CHECK(caller.values[1] == 1.0);
    const std::span<const ct::ScriptEffect> effects = caller.runner.effects();
    REQUIRE(effects.size() == 4);
    CHECK(effects[1].kind == ct::ScriptEffectKind::After);
    CHECK(effects[1].index == 0);
    CHECK(effects[1].delay == 1.5);
    CHECK(effects[2].kind == ct::ScriptEffectKind::Send);
    CHECK(effects[2].target.view() == "door");
    CHECK(effects[2].targetInput.view() == "move_to_end");
    CHECK(effects[2].delay == 0.25);
    CHECK(effects[3].index == 1);
}

TEST_CASE("a call that fails changes nothing") {
    Caller caller(R"(
        return {
            state = { count = 5 },
            events = { "on_x" },
            inputs = {
                bad = function(game)
                    game.state.count = 99
                    game:fire("on_x")
                    local nothing = nil
                    return nothing.field
                end,
                unknown = function(game) game:fire("on_" .. "y") end,
            },
        }
    )");
    CHECK_FALSE(caller.call("bad"));
    CHECK(caller.values[0] == 5.0);
    CHECK(caller.runner.effects().empty());
    CHECK(caller.runner.error().starts_with("test:10: attempt to index nil"));

    CHECK_FALSE(caller.call("unknown"));
    CHECK(caller.runner.error().find("no event 'on_y'") != std::string_view::npos);
}

TEST_CASE("the state keeps its declared values and kinds") {
    Caller caller(R"(
        return {
            state = { n = 1, flag = true },
            inputs = {
                text = function(game) game.state.n = "one" end,
                extra = function(game) game.state.other = 1 end,
                infinite = function(game) game.state.n = math.huge end,
                gone = function(game) game.state.flag = nil end,
                replaced = function(game) game.state = { n = 7, flag = false } end,
            },
        }
    )");
    CHECK_FALSE(caller.call("text"));
    CHECK(caller.runner.error() == "state.n must be a number");
    CHECK_FALSE(caller.call("extra"));
    CHECK(caller.runner.error().find("state.other isn't declared") != std::string_view::npos);
    CHECK_FALSE(caller.call("infinite"));
    CHECK_FALSE(caller.call("gone"));
    CHECK(caller.runner.error() == "state.flag must be true or false");
    REQUIRE(caller.call("replaced"));
    CHECK(caller.values[0] == 0.0); // flag
    CHECK(caller.values[1] == 7.0); // n
}

TEST_CASE("a call that runs too long fails, even if the script catches the error") {
    Caller caller(R"(
        return {
            state = { n = 0 },
            inputs = {
                forever = function(game) while true do game.state.n += 1 end end,
                caught = function(game)
                    pcall(function() while true do end end)
                    game.state.n = 1
                end,
                patterns = function(game)
                    string.find(string.rep("a", 40), string.rep("a?", 40) .. string.rep("a", 40))
                end,
            },
        }
    )");
    CHECK_FALSE(caller.call("forever"));
    CHECK(caller.runner.error() == "took more than 200000 steps");
    CHECK(caller.values[0] == 0.0);
    CHECK_FALSE(caller.call("caught"));
    CHECK(caller.values[0] == 0.0);
    CHECK_FALSE(caller.call("patterns"));
    CHECK(caller.runner.error() == "took more than 200000 steps");
}

TEST_CASE("a call that uses too much memory fails") {
    Caller caller(R"(
        return {
            inputs = {
                grow = function(game)
                    local t = {}
                    for i = 1, 100000 do t[i] = { i } end
                end,
                big = function(game) local s = string.rep("x", 8 * 1024 * 1024) end,
            },
        }
    )");
    CHECK_FALSE(caller.call("grow"));
    CHECK(caller.runner.error() == "used more than 4 MB of memory");
    CHECK_FALSE(caller.call("big"));
    // Memory a call lets go of serves it again, so a call can make far more garbage than that.
    Caller garbage(R"(
        return {
            inputs = {
                churn = function(game)
                    for i = 1, 100000 do local t = { i, i, i, i } end
                end,
            },
        }
    )");
    CHECK_MESSAGE(garbage.call("churn"), garbage.runner.error());
}

TEST_CASE("game:random draws from the counter, the same numbers every time") {
    const char* source = R"(
        return {
            state = { a = 0, b = 0 },
            inputs = {
                draw = function(game)
                    game.state.a = game:random()
                    game.state.b = game:random(1, 6)
                end,
            },
        }
    )";
    Caller first(source);
    Caller second(source);
    REQUIRE(first.call("draw"));
    REQUIRE(second.call("draw"));
    CHECK(first.random == 2);
    CHECK(first.values[0] == second.values[0]);
    CHECK(first.values[1] == second.values[1]);
    CHECK(first.values[0] >= 0.0);
    CHECK(first.values[0] < 1.0);
    for (int i = 0; i < 50; ++i) {
        REQUIRE(first.call("draw"));
        CHECK(first.values[1] >= 1.0);
        CHECK(first.values[1] <= 6.0);
        CHECK(first.values[1] == static_cast<double>(static_cast<int>(first.values[1])));
    }
}

TEST_CASE("tables keyed by tables iterate in the same order in every runner") {
    // Pointers are hashed by address, which the arena makes the same for the same calls.
    const char* source = R"(
        return {
            state = { order = 0, name = 0 },
            inputs = {
                go = function(game)
                    local keys, set = {}, {}
                    for i = 1, 40 do keys[i] = {}; set[keys[i]] = i end
                    local order = 0
                    for key, i in pairs(set) do order = (order * 31 + i) % 1000000007 end
                    game.state.order = order
                    local text = tostring(keys[7]) .. string.format("%s", print) .. tostring(go)
                    local hash = 0
                    for i = 1, #text do hash = (hash * 31 + string.byte(text, i)) % 1000000007 end
                    game.state.name = hash
                end,
            },
        }
    )";
    Caller first(source);
    Caller second(source);
    REQUIRE(first.call("go"));
    REQUIRE(second.call("go"));
    CHECK(first.values[1] == second.values[1]);
    CHECK(first.values[0] == second.values[0]);
    REQUIRE(first.call("go"));
    CHECK(first.values[1] == second.values[1]);
}

TEST_CASE("print writes lines the caller can log") {
    Caller caller(R"(
        return {
            inputs = {
                say = function(game) print("hello", 1, true, nil) print(game.time) end,
            },
        }
    )");
    caller.time = 2.5;
    REQUIRE(caller.call("say"));
    REQUIRE(caller.runner.printed().size() == 2);
    CHECK(caller.runner.printed()[0].view() == "hello 1 true nil");
    CHECK(caller.runner.printed()[1].view() == "2.5");
}

TEST_CASE("the start function is called as an input") {
    Caller caller(R"(
        return {
            state = { started = false },
            start = function(game) game.state.started = true end,
        }
    )");
    REQUIRE(caller.call("start"));
    CHECK(caller.values[0] == 1.0);
}

TEST_CASE("game functions check what they are given") {
    Caller caller(R"(
        return {
            inputs = {
                late = function(game) game:after(-1, "late") end,
                nobody = function(game) game:send("no such/name", "open") end,
                many = function(game) for i = 1, 100 do game:send("x", "y") end end,
                dice = function(game) game:random(6, 1) end,
            },
        }
    )");
    CHECK_FALSE(caller.call("late"));
    CHECK(caller.runner.error().find("a delay is 0 to") != std::string_view::npos);
    CHECK_FALSE(caller.call("nobody"));
    CHECK_FALSE(caller.call("many"));
    CHECK(caller.runner.error().find("more than 64 things") != std::string_view::npos);
    CHECK_FALSE(caller.call("dice"));
}
