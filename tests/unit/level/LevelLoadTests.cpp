#include "TestFiles.h"

#include "croftech/level/LevelLoad.h"

#include <doctest/doctest.h>

#include <ostream> // doctest prints string_view through operator<<
#include <string>
#include <vector>

using ct::test::TempDirectory;
using ct::test::writeFile;

TEST_CASE("the levels of every layer are listed once each, as findLevelFile opens them") {
    TempDirectory engine;
    TempDirectory game;
    writeFile(engine.path() / "levels/arena.level", "");
    writeFile(engine.path() / "levels/notes.txt", "");
    writeFile(game.path() / "levels/arena.level", "");
    writeFile(game.path() / "levels/imported.map", "");
    writeFile(game.path() / "levels/imported.level", "");
    ct::ContentPaths content;
    content.addLayer(engine.path());
    content.addLayer(game.path());
    const std::vector<ct::LevelFileListing> levels = ct::listLevelFiles(content);
    REQUIRE(levels.size() == 2);
    CHECK(levels[0].name == "arena");
    CHECK(levels[0].path == game.path() / "levels/arena.level");
    CHECK(levels[1].name == "imported");
    CHECK(levels[1].path == game.path() / "levels/imported.level"); // .level before .map
}
