#include "croftech/script/ScriptProgram.h"

#include <doctest/doctest.h>

#include <optional>
#include <string>

namespace {

std::optional<ct::ScriptProgram> load(std::string_view source, std::string& error) {
    return ct::ScriptProgram::load("test", source, error);
}

// The error loading the source gives, or "loaded".
std::string loadError(std::string_view source) {
    std::string error;
    return load(source, error) ? "loaded" : error;
}

} // namespace

TEST_CASE("a script declares its state, events, inputs and start function") {
    std::string error;
    const std::optional<ct::ScriptProgram> program = load(R"(
        local function helper(game) game:fire("on_done") end
        return {
            state = { presses = 3, open = false, best = 0.5 },
            events = { "on_done", "on_wrong" },
            inputs = { reset = function(game) end, press = helper },
            start = function(game) end,
        }
    )",
                                                          error);
    REQUIRE_MESSAGE(program, error);
    const ct::ScriptDeclarations& declared = program->declarations();
    REQUIRE(declared.state.size() == 3);
    CHECK(declared.state[0].name == "best"); // sorted by name
    CHECK(declared.state[0].initial == 0.5);
    CHECK(declared.state[1].name == "open");
    CHECK(declared.state[1].flag);
    CHECK(declared.state[1].initial == 0.0);
    CHECK(declared.state[2].name == "presses");
    CHECK_FALSE(declared.state[2].flag);
    CHECK(declared.state[2].initial == 3.0);
    CHECK(declared.events == std::vector<std::string>{"on_done", "on_wrong"}); // as listed
    CHECK(declared.inputs == std::vector<std::string>{"press", "reset"});      // sorted
    CHECK(declared.start);
    CHECK(program->findInput("reset") == 1u);
    CHECK(program->findEvent("on_wrong") == 1u);
    CHECK_FALSE(program->findInput("open"));
}

TEST_CASE("a script needs declare nothing but must return a table") {
    std::string error;
    const std::optional<ct::ScriptProgram> empty = load("return {}", error);
    REQUIRE(empty);
    CHECK(empty->declarations().state.empty());
    CHECK_FALSE(empty->declarations().start);

    CHECK(loadError("local x = 1") ==
          "test must return a table (of state, events, inputs and start)");
    CHECK(loadError("return { stats = {} }").find("has 'stats'") != std::string::npos);
}

TEST_CASE("mistakes in a script's declarations are errors when it loads") {
    CHECK(loadError("return { state = { x = 'text' } }").find("state.x must start as") !=
          std::string::npos);
    CHECK(loadError("return { state = { x = 0/0 } }").find("state.x") != std::string::npos);
    CHECK(loadError("return { state = { [1] = 2 } }").find("a name is") != std::string::npos);
    CHECK(loadError("return { events = { 'a', 'a' } }") == "events has 'a' twice");
    CHECK(loadError("return { events = { a = 'b' } }") == "events must be a list of names");
    CHECK(loadError("return { events = { 'has space' } }").find("events[1]") != std::string::npos);
    CHECK(loadError("return { inputs = { go = 5 } }") == "inputs.go must be a function");
    CHECK(loadError("return { start = true }") == "start must be a function");

    std::string many = "return { state = {";
    for (int i = 0; i <= static_cast<int>(ct::kMaxScriptState); ++i) {
        many += "v" + std::to_string(i) + " = 0,";
    }
    many += "} }";
    CHECK(loadError(many).find("at most 16 values") != std::string::npos);
}

TEST_CASE("errors in a script give its name and the line") {
    CHECK(loadError("return {\n  state = {\n    x = = 1 } }").starts_with("test:3: "));
    CHECK(loadError("local x = 1\nerror('stop')\nreturn {}") == "test:2: stop");
}

TEST_CASE("scripts can't use what would play differently on another platform") {
    CHECK(loadError("local x = 2\nreturn { state = { y = x ^ 3 } }")
              .starts_with("test:2: the ^ operator isn't available"));
    CHECK(loadError("local x = 2\nx ^= 2\nreturn {}").starts_with("test:2: the ^= operator"));
    CHECK(loadError("return { state = { y = math.sin(1) } }").find("math.sin isn't available") !=
          std::string::npos);
    CHECK(loadError("local r = math.random\nreturn {}").find("use game:random()") !=
          std::string::npos);
    CHECK(loadError("--!optimize 2\nreturn {}").find("--!optimize 2 isn't allowed") !=
          std::string::npos);
    CHECK(loadError("--!native\nreturn {}").find("--!native isn't allowed") != std::string::npos);
    CHECK(loadError("return { state = { y = math.floor(1.5) + math.sqrt(4) + math.pi } }") ==
          "loaded");
}

TEST_CASE("scripts get no I/O, no loading of code and no access to their environment") {
    for (const char* missing :
         {"os", "io", "debug", "require", "getfenv", "setfenv", "loadstring", "collectgarbage"}) {
        CAPTURE(missing);
        CHECK(loadError(std::string("local f = ") + missing + "\nreturn {}") ==
              std::string("test:1: ") + missing + " isn't available to scripts");
    }
    // Reached some other way, they still aren't there.
    CHECK(loadError("assert(_G['o' .. 's'] == nil)\nassert(_G.vector == nil)\nreturn {}") ==
          "loaded");
}

TEST_CASE("script names are file names without folders") {
    std::string error;
    CHECK(ct::isValidScriptName("door_puzzle-2"));
    CHECK_FALSE(ct::isValidScriptName("../secret"));
    CHECK_FALSE(ct::isValidScriptName("Upper"));
    CHECK_FALSE(ct::isValidScriptName(""));
    CHECK_FALSE(ct::ScriptProgram::load("a/b", "return {}", error));
}

TEST_CASE("scripts over the size limit don't load") {
    const std::string big(ct::kMaxScriptBytes + 1, ' ');
    CHECK(loadError(big).find("is over") != std::string::npos);
}
