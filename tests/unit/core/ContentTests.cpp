#include "TestFiles.h"

#include "croftech/core/Content.h"

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using ct::test::TempDirectory;
using ct::test::writeFile;

// Pushes the timestamp forward so change detection doesn't depend on filesystem time resolution.
void touch(const fs::path& path) {
    fs::last_write_time(path, fs::last_write_time(path) + std::chrono::seconds(5));
}

} // namespace

TEST_CASE("find returns the highest layer's copy of a file") {
    TempDirectory temp;
    writeFile(temp.path() / "engine/levels/a.txt", "engine");
    writeFile(temp.path() / "game/levels/a.txt", "game");
    writeFile(temp.path() / "engine/levels/b.txt", "engine only");

    ct::ContentPaths content;
    content.addLayer(temp.path() / "engine");
    content.addLayer(temp.path() / "game");

    CHECK(content.find("levels/a.txt") == temp.path() / "game/levels/a.txt");
    CHECK(content.find("levels/b.txt") == temp.path() / "engine/levels/b.txt");
    CHECK_FALSE(content.find("levels/missing.txt").has_value());
}

TEST_CASE("layered config returns every layer lowest first, then only after changes") {
    TempDirectory temp;
    const fs::path engineFile = temp.path() / "engine/config/test.cfg";
    const fs::path gameFile = temp.path() / "game/config/test.cfg";
    writeFile(engineFile, "speed 1\ngravity 2\n");

    ct::ContentPaths content;
    content.addLayer(temp.path() / "engine");
    content.addLayer(temp.path() / "game");
    ct::LayeredConfig config(content, "config/test.cfg");

    auto layers = config.reloadIfChanged();
    REQUIRE(layers.has_value());
    REQUIRE(layers->size() == 1);
    CHECK(layers->front().entries.size() == 2);
    CHECK_FALSE(config.reloadIfChanged().has_value());

    // A game layer appearing counts as a change.
    writeFile(gameFile, "speed 3\n");
    layers = config.reloadIfChanged();
    REQUIRE(layers.has_value());
    REQUIRE(layers->size() == 2);
    CHECK(layers->at(0).path == engineFile);
    CHECK(layers->at(1).path == gameFile);
    CHECK(layers->at(1).entries.front().value == "3");

    // So does editing one, and deleting it.
    writeFile(gameFile, "speed 4\n");
    touch(gameFile);
    layers = config.reloadIfChanged();
    REQUIRE(layers.has_value());
    CHECK(layers->at(1).entries.front().value == "4");

    fs::remove(gameFile);
    layers = config.reloadIfChanged();
    REQUIRE(layers.has_value());
    CHECK(layers->size() == 1);
}

TEST_CASE("game manifests load from games/<name>/game.cfg") {
    TempDirectory temp;
    writeFile(temp.path() / "sandbox/game.cfg",
              "title  My Sandbox\nstart_map  arena\nweapons  pistol\tsuper-shotgun  rail_gun\n");
    writeFile(temp.path() / "unarmed/game.cfg", "title  Unarmed\n");
    writeFile(temp.path() / "badweapon/game.cfg", "weapons  pistol ../escape\n");

    std::string error;
    const auto game = ct::loadGameManifest(temp.path(), "sandbox", error);
    REQUIRE(game.has_value());
    CHECK(game->title == "My Sandbox");
    CHECK(game->startMap == "arena");
    CHECK(game->root == temp.path() / "sandbox");
    CHECK(game->weapons == std::vector<std::string>{"pistol", "super-shotgun", "rail_gun"});
    CHECK(ct::loadGameManifest(temp.path(), "unarmed", error)->weapons.empty());
    CHECK_FALSE(ct::loadGameManifest(temp.path(), "badweapon", error).has_value());
    CHECK(error.find("../escape") != std::string::npos);

    CHECK_FALSE(ct::loadGameManifest(temp.path(), "missing", error).has_value());
    CHECK_FALSE(ct::loadGameManifest(temp.path(), "../escape", error).has_value());
    CHECK_FALSE(ct::loadGameManifest(temp.path(), "", error).has_value());
}

TEST_CASE("the sandbox game exists and its start map is in its content") {
    std::string error;
    const auto game = ct::loadGameManifest(CROFTECH_SOURCE_DIR "/games", "sandbox", error);
    REQUIRE_MESSAGE(game.has_value(), error);
    CHECK(game->title == "CrofTech Sandbox");

    ct::ContentPaths content;
    content.addLayer(CROFTECH_SOURCE_DIR "/content");
    content.addLayer(game->root / "content");
    CAPTURE(game->startMap);
    CHECK((content.find("levels/" + game->startMap + ".level") ||
           content.find("levels/" + game->startMap + ".map")));
    CHECK_FALSE(game->weapons.empty());
    for (const std::string& weapon : game->weapons) {
        CAPTURE(weapon);
        CHECK(content.find("weapons/" + weapon + ".cfg"));
        CHECK(content.find("sounds/" + weapon + "_fire.wav"));
    }
}
