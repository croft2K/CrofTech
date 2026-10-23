#include "TestFiles.h"

#include "croftech/script/ScriptLibrary.h"

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <ostream> // doctest prints string_view through operator<<
#include <string_view>
#include <vector>

using ct::test::TempDirectory;
using ct::test::writeFile;

TEST_CASE("the script library finds scripts in every layer, a later layer's copy winning") {
    TempDirectory engine;
    TempDirectory game;
    writeFile(engine.path() / "scripts/shared.luau", "return { events = { 'on_engine' } }");
    writeFile(engine.path() / "scripts/broken.luau", "return { state = { x = math.sin(1) } }");
    writeFile(engine.path() / "scripts/Not A Script.luau", "return {}");
    writeFile(engine.path() / "scripts/notes.txt", "not a script");
    writeFile(game.path() / "scripts/shared.luau", "return { events = { 'on_game' } }");
    ct::ContentPaths content;
    content.addLayer(engine.path());
    content.addLayer(game.path());

    ct::ScriptLibrary library(content);
    CHECK(library.refresh());
    CHECK(library.names() == std::vector<std::string_view>{"broken", "shared"});
    const ct::ScriptProgram* shared = library.find("shared");
    REQUIRE(shared != nullptr);
    CHECK(shared->declarations().events == std::vector<std::string>{"on_game"});
    CHECK(library.find("broken") == nullptr);
    CHECK(library.error("broken").find("math.sin isn't available") != std::string_view::npos);
    CHECK(library.find("missing") == nullptr);
    CHECK_FALSE(library.refresh());
    CHECK(library.find("shared") == shared); // unchanged, so not compiled again
}

TEST_CASE("the script library loads a script again when its file changes or goes") {
    TempDirectory root;
    const std::filesystem::path path = root.path() / "scripts/lock.luau";
    writeFile(path, "return { inputs = { a = function() end } }");
    ct::ContentPaths content;
    content.addLayer(root.path());
    ct::ScriptLibrary library(content);
    library.refresh();
    REQUIRE(library.find("lock") != nullptr);
    CHECK(library.find("lock")->declarations().inputs == std::vector<std::string>{"a"});

    writeFile(path, "return { inputs = { a = function() end, b = function() end } }");
    std::filesystem::last_write_time(path, std::filesystem::last_write_time(path) +
                                               std::chrono::seconds(2));
    CHECK(library.refresh());
    REQUIRE(library.find("lock") != nullptr);
    CHECK(library.find("lock")->declarations().inputs == std::vector<std::string>{"a", "b"});

    std::filesystem::remove(path);
    CHECK(library.refresh());
    CHECK(library.names().empty());
}
