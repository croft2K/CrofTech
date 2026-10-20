#include "croftech/core/Console.h"
#include "croftech/core/Log.h"

#include <doctest/doctest.h>

#include <string>
#include <vector>

namespace {

// Collects what the console logs while it is alive.
class LogCapture {
public:
    LogCapture()
        : m_id(ct::addLogSink([this](ct::LogLevel level, std::string_view message) {
              (level == ct::LogLevel::Info ? lines : problems).emplace_back(message);
          })) {}
    ~LogCapture() { ct::removeLogSink(m_id); }
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

    std::vector<std::string> lines;
    std::vector<std::string> problems;

private:
    std::uint32_t m_id;
};

} // namespace

TEST_CASE("the console sets and prints variables") {
    ct::Console console;
    const ct::CVar& fov = console.add(
        ct::CVar::makeFloat("client.fov", 100.0f, 60.0f, 140.0f, "horizontal field of view"));
    const std::uint64_t changes = console.changeCount();

    console.execute("client.fov 90");
    CHECK(fov.asFloat() == 90.0f);
    CHECK(console.changeCount() == changes + 1);

    console.execute("client.fov 90");
    CHECK(console.changeCount() == changes + 1); // same value: no change

    LogCapture log;
    console.execute("client.fov");
    REQUIRE(log.lines.size() == 2);
    CHECK(log.lines[0] == "client.fov = 90  [default 100; a number from 60 to 140]");

    console.execute("client.fov 200");
    console.execute("client.fov 90 100");
    console.execute("nonsense");
    CHECK(log.problems.size() == 3);
    CHECK(fov.asFloat() == 90.0f);
}

TEST_CASE("the console splits commands and keeps quoted words together") {
    ct::Console console;
    std::vector<std::vector<std::string>> calls;
    console.addCommand("record", "", [&](std::span<const std::string> args) {
        calls.emplace_back(args.begin(), args.end());
    });
    console.execute("record a  b;record \"c d\" \"\"\nrecord;  ;");
    REQUIRE(calls.size() == 3);
    CHECK(calls[0] == std::vector<std::string>{"a", "b"});
    CHECK(calls[1] == std::vector<std::string>{"c d", ""});
    CHECK(calls[2].empty());

    console.removeCommand("record");
    LogCapture log;
    console.execute("record x");
    CHECK(calls.size() == 3);
    CHECK(log.problems.size() == 1);
}

TEST_CASE("string variables take the rest of the line") {
    ct::Console console;
    const ct::CVar& name = console.add(ct::CVar::makeString("name", "player", ""));
    console.execute("name Ranger   Rick");
    CHECK(name.text() == "Ranger Rick");
}

TEST_CASE("reset and toggle") {
    ct::Console console;
    const ct::CVar& flag = console.add(ct::CVar::makeBool("show", true, ""));
    const ct::CVar& speed = console.add(ct::CVar::makeFloat("speed", 1.0f, 0.0f, 10.0f, ""));
    console.execute("toggle show; speed 5");
    CHECK_FALSE(flag.asBool());
    CHECK(speed.asFloat() == 5.0f);
    console.execute("reset show speed");
    CHECK(flag.asBool());
    CHECK(speed.asFloat() == 1.0f);
}

TEST_CASE("completion lists commands and variables by prefix") {
    ct::Console console;
    console.add(ct::CVar::makeFloat("movement.gravity", 1.0f, 0.0f, 10.0f, ""));
    console.add(ct::CVar::makeFloat("movement.friction", 1.0f, 0.0f, 10.0f, ""));
    console.add(ct::CVar::makeFloat("client.fov", 100.0f, 60.0f, 140.0f, ""));
    CHECK(console.complete("movement.") ==
          std::vector<std::string>{"movement.friction", "movement.gravity"});
    CHECK(console.complete("c") == std::vector<std::string>{"client.fov", "commands", "cvars"});
    CHECK(console.complete("zzz").empty());
}

TEST_CASE("config layers reset their variables then apply in order") {
    ct::Console console;
    const ct::CVar& gravity =
        console.add(ct::CVar::makeFloat("movement.gravity", 20.0f, 0.0f, 100.0f, ""));
    const ct::CVar& friction =
        console.add(ct::CVar::makeFloat("movement.friction", 6.0f, 0.0f, 100.0f, ""));
    const ct::CVar& fov = console.add(ct::CVar::makeFloat("client.fov", 100.0f, 60.0f, 140.0f, ""));

    console.execute("movement.friction 3; client.fov 90");
    const std::vector<ct::LayeredConfig::Layer> layers{
        {"engine/movement.cfg", ct::parseConfig("gravity 10\nfriction 4\n")},
        {"game/movement.cfg", ct::parseConfig("gravity 12\nwarp 9\nfriction lots\n")},
    };
    const auto problems = console.applyConfig("movement.", layers);
    CHECK(gravity.asFloat() == 12.0f); // the game layer wins
    CHECK(friction.asFloat() == 4.0f); // the bad game value leaves the engine value
    CHECK(fov.asFloat() == 90.0f);     // other prefixes are untouched
    REQUIRE(problems.size() == 2);
    CHECK(problems[0] == "game/movement.cfg: line 2: unknown setting 'warp'");

    // A reload without the lines puts the defaults back.
    console.applyConfig("movement.", {});
    CHECK(gravity.asFloat() == 20.0f);
    CHECK(friction.asFloat() == 6.0f);
}
