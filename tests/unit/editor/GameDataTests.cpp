#include "TestFiles.h"

#include "croftech/core/ConfigFile.h"
#include "croftech/editor/GameData.h"
#include "croftech/sim/BehaviourTree.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr std::string_view kGunner = "# Gunner: shoots.\n"
                                     "\n"
                                     "description     Shoots in bursts\n"
                                     "health          40      # health it starts with\n"
                                     "speed           5       # m/s running\n";

constexpr std::string_view kPistol = "# Pistol: one shot at a time.\n"
                                     "\n"
                                     "damage          12\n"
                                     "fire_interval   0.4     # seconds between shots\n";

constexpr std::string_view kManifest = "title  Test\n"
                                       "weapons pistol   # in number-key order\n"
                                       "enemies gunner   # each is enemies/<name>.cfg\n";

// An engine content folder with the gunner, and a game with its own content folder on top.
struct TestGame {
    ct::test::TempDirectory directory;
    ct::ContentPaths content;

    TestGame() {
        ct::test::writeFile(engine() / "enemies/gunner.cfg", std::string(kGunner));
        ct::test::writeFile(engine() / "weapons/pistol.cfg", std::string(kPistol));
        ct::test::writeFile(manifest(), std::string(kManifest));
        std::filesystem::create_directories(game());
        content.addLayer(engine());
        content.addLayer(game());
    }

    std::filesystem::path engine() const { return directory.path() / "content"; }
    std::filesystem::path game() const { return directory.path() / "games/test/content"; }
    std::filesystem::path manifest() const { return directory.path() / "games/test/game.cfg"; }

    ct::GameData load() const {
        std::string error;
        std::optional<ct::GameData> data = ct::GameData::load(content, manifest(), error);
        REQUIRE_MESSAGE(data.has_value(), error);
        return std::move(*data);
    }
};

const ct::GameConfigFile& fileOf(const ct::GameData& data, std::string_view path) {
    const ct::GameConfigFile* file = data.findFile(path);
    REQUIRE(file != nullptr);
    return *file;
}

const ct::GameDataSetting& settingOf(const std::vector<ct::GameDataSetting>& settings,
                                     std::string_view key) {
    const auto found = std::find_if(settings.begin(), settings.end(),
                                    [key](const ct::GameDataSetting& s) { return s.key == key; });
    REQUIRE(found != settings.end());
    return *found;
}

void set(ct::GameData& data, std::string_view path, std::string_view key, std::string_view value,
         bool merge = false) {
    std::string error;
    REQUIRE_MESSAGE(data.set(fileOf(data, path), key, value, merge, error), error);
}

} // namespace

TEST_CASE("game data reads the settings through the content layers") {
    TestGame game;
    const ct::GameData data = game.load();
    REQUIRE(data.settings().enemies.size() == 1);
    CHECK(data.settings().enemies[0].health == 40);
    CHECK(data.settings().weapons.at(0).name == "pistol");
    CHECK(data.warnings().empty());
    const ct::GameConfigFile& gunner = fileOf(data, "enemies/gunner.cfg");
    CHECK_FALSE(data.isGameOwned(gunner));
    const std::vector<ct::GameDataSetting> settings = data.settingsOf(gunner);
    // The file's order first, then the rest.
    REQUIRE(settings.size() > 3);
    CHECK(settings[0].key == "description");
    CHECK(settings[1].key == "health");
    CHECK(settings[1].origin == ct::SettingOrigin::Base);
    CHECK(settings[1].variable->asInt() == 40);
    CHECK(settingOf(settings, "aim_error").origin == ct::SettingOrigin::Default);
}

TEST_CASE("an edit to the engine's type goes into the game's folder as just that setting") {
    TestGame game;
    ct::GameData data = game.load();
    const std::uint64_t before = data.changeCount();
    set(data, "enemies/gunner.cfg", "health", "35");
    CHECK(data.changeCount() != before);
    CHECK(data.settings().enemies[0].health == 35);
    CHECK(data.hasUnsavedChanges());
    const ct::GameDataSetting health =
        settingOf(data.settingsOf(fileOf(data, "enemies/gunner.cfg")), "health");
    CHECK(health.origin == ct::SettingOrigin::Game);
    CHECK(health.baseValue == "40");

    std::string error;
    REQUIRE_MESSAGE(data.save(error), error);
    CHECK_FALSE(data.hasUnsavedChanges());
    const std::optional<std::string> saved = ct::readTextFile(game.game() / "enemies/gunner.cfg");
    REQUIRE(saved.has_value());
    const std::vector<ct::ConfigEntry> entries = ct::parseConfig(*saved);
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].key == "health");
    CHECK(entries[0].value == "35");
    CHECK(ct::readTextFile(game.engine() / "enemies/gunner.cfg") == std::string(kGunner));

    // Back to the engine's value takes the line away again.
    set(data, "enemies/gunner.cfg", "health", "40");
    CHECK(data.settings().enemies[0].health == 40);
    REQUIRE_MESSAGE(data.save(error), error);
    CHECK(ct::parseConfig(*ct::readTextFile(game.game() / "enemies/gunner.cfg")).empty());
}

TEST_CASE("setting the engine's value again writes no file") {
    TestGame game;
    ct::GameData data = game.load();
    set(data, "enemies/gunner.cfg", "health", "50");
    set(data, "enemies/gunner.cfg", "health", "40");
    CHECK_FALSE(data.hasUnsavedChanges());
    set(data, "enemies/gunner.cfg", "description", "  Shoots in bursts ");
    CHECK_FALSE(data.hasUnsavedChanges());
}

TEST_CASE("a game setting can be reset to the engine's") {
    TestGame game;
    ct::GameData data = game.load();
    const ct::GameConfigFile& gunner = fileOf(data, "enemies/gunner.cfg");
    CHECK_FALSE(data.reset(gunner, "health"));
    set(data, "enemies/gunner.cfg", "health", "20");
    CHECK(data.reset(fileOf(data, "enemies/gunner.cfg"), "health"));
    CHECK(data.settings().enemies[0].health == 40);
    CHECK(data.undoName() == "Reset gunner health");
    CHECK_FALSE(data.hasUnsavedChanges());
}

TEST_CASE("a drag is one undo step, and undo and redo go back and forth") {
    TestGame game;
    ct::GameData data = game.load();
    set(data, "enemies/gunner.cfg", "speed", "4");
    set(data, "enemies/gunner.cfg", "health", "30");
    set(data, "enemies/gunner.cfg", "health", "31", true);
    set(data, "enemies/gunner.cfg", "health", "32", true);
    CHECK(data.settings().enemies[0].health == 32);
    CHECK(data.undoName() == "Set gunner health");
    REQUIRE(data.undo());
    CHECK(data.settings().enemies[0].health == 40);
    CHECK(data.settings().enemies[0].speed == 4.0f);
    REQUIRE(data.undo());
    CHECK(data.settings().enemies[0].speed == 5.0f);
    CHECK_FALSE(data.canUndo());
    CHECK_FALSE(data.hasUnsavedChanges());
    REQUIRE(data.redo());
    REQUIRE(data.redo());
    CHECK(data.settings().enemies[0].health == 32);
    CHECK_FALSE(data.canRedo());
}

TEST_CASE("values that don't suit a setting are refused") {
    TestGame game;
    ct::GameData data = game.load();
    const ct::GameConfigFile& gunner = fileOf(data, "enemies/gunner.cfg");
    std::string error;
    CHECK_FALSE(data.set(gunner, "health", "lots", false, error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(data.set(gunner, "health", "0", false, error)); // at least 1
    CHECK_FALSE(data.set(gunner, "description", "number #1", false, error));
    CHECK_FALSE(data.set(gunner, "wings", "2", false, error));
    CHECK_FALSE(data.hasUnsavedChanges());
}

TEST_CASE("a new enemy type copies another and sounds like it") {
    TestGame game;
    ct::GameData data = game.load();
    set(data, "enemies/gunner.cfg", "health", "45");
    std::string error;
    REQUIRE_MESSAGE(data.createEnemyType("calm_gunner", "gunner", error), error);
    REQUIRE(data.settings().enemies.size() == 2);
    const ct::EnemySettings& calm = data.settings().enemies[1];
    CHECK(calm.name == "calm_gunner");
    CHECK(calm.health == 45); // with the game's change to the gunner
    CHECK(calm.speed == 5.0f);
    CHECK(calm.description == "Shoots in bursts");
    CHECK(calm.soundSet() == "gunner");
    const ct::GameConfigFile& file = fileOf(data, "enemies/calm_gunner.cfg");
    CHECK(data.isGameOwned(file));
    CHECK(data.savePath(file) == game.game() / "enemies/calm_gunner.cfg");
    CHECK(data.undoName() == "New enemy type calm_gunner");

    // Its own file, with every setting, to change as it likes.
    set(data, "enemies/calm_gunner.cfg", "health", "40");
    CHECK(data.settings().enemies[1].health == 40);

    CHECK_FALSE(data.createEnemyType("calm_gunner", "gunner", error));
    CHECK_FALSE(data.createEnemyType("Calm", "gunner", error));
    CHECK_FALSE(data.createEnemyType("brute", "nobody", error));

    REQUIRE_MESSAGE(data.save(error), error);
    const std::optional<std::string> text =
        ct::readTextFile(game.game() / "enemies/calm_gunner.cfg");
    REQUIRE(text.has_value());
    CHECK(text->starts_with("# calm_gunner: an enemy type made in the editor from the gunner."));
    const std::optional<std::string> manifest = ct::readTextFile(game.manifest());
    REQUIRE(manifest.has_value());
    CHECK(manifest->find("enemies gunner calm_gunner  # each is enemies/<name>.cfg\n") !=
          std::string::npos);

    // Undone and saved, it is gone again.
    REQUIRE(data.undo());
    REQUIRE(data.undo());
    REQUIRE(data.settings().enemies.size() == 1);
    REQUIRE_MESSAGE(data.save(error), error);
    CHECK_FALSE(std::filesystem::exists(game.game() / "enemies/calm_gunner.cfg"));
}

TEST_CASE("only the game's own enemy types can be deleted") {
    TestGame game;
    ct::GameData data = game.load();
    std::string error;
    CHECK_FALSE(data.deleteEnemyType("gunner", error));
    CHECK_FALSE(data.deleteEnemyType("nobody", error));
    REQUIRE_MESSAGE(data.createEnemyType("grunt", "gunner", error), error);
    REQUIRE_MESSAGE(data.save(error), error);
    REQUIRE(std::filesystem::exists(game.game() / "enemies/grunt.cfg"));
    REQUIRE_MESSAGE(data.deleteEnemyType("grunt", error), error);
    CHECK(data.settings().enemies.size() == 1);
    CHECK(data.findFile("enemies/grunt.cfg") == nullptr);
    REQUIRE_MESSAGE(data.save(error), error);
    CHECK_FALSE(std::filesystem::exists(game.game() / "enemies/grunt.cfg"));
    CHECK(ct::readTextFile(game.manifest())->find("enemies gunner ") != std::string::npos);
}

TEST_CASE("a new weapon copies another, sounds like it and takes the next number key") {
    TestGame game;
    ct::GameData data = game.load();
    set(data, "weapons/pistol.cfg", "damage", "15");
    std::string error;
    REQUIRE_MESSAGE(data.createWeapon("heavy_pistol", "pistol", error), error);
    REQUIRE(data.settings().weapons.size() == 2);
    const ct::WeaponSettings& heavy = data.settings().weapons[1];
    CHECK(heavy.name == "heavy_pistol");
    CHECK(heavy.damage == 15); // with the game's change to the pistol
    CHECK(heavy.fireInterval == 0.4f);
    CHECK(heavy.soundSet() == "pistol");
    CHECK(data.settings().weapons[0].soundSet() == "pistol");
    CHECK(data.isGameOwned(fileOf(data, "weapons/heavy_pistol.cfg")));
    CHECK(data.undoName() == "New weapon heavy_pistol");

    set(data, "weapons/heavy_pistol.cfg", "damage", "30");
    CHECK(data.settings().weapons[1].damage == 30);
    CHECK(data.settings().weapons[0].damage == 15);

    CHECK_FALSE(data.createWeapon("heavy_pistol", "pistol", error));
    CHECK_FALSE(data.createWeapon("Heavy", "pistol", error));
    CHECK_FALSE(data.createWeapon("cannon", "nobody", error));
    // An enemy type's name is no weapon to copy.
    CHECK_FALSE(data.createWeapon("cannon", "gunner", error));

    REQUIRE_MESSAGE(data.save(error), error);
    const std::optional<std::string> text =
        ct::readTextFile(game.game() / "weapons/heavy_pistol.cfg");
    REQUIRE(text.has_value());
    CHECK(text->starts_with("# heavy_pistol: a weapon made in the editor from the pistol. Each "
                            "line is also a console\n# variable named weapon.heavy_pistol."));
    CHECK(text->find("fire_interval   0.4     # seconds between shots\n") != std::string::npos);
    CHECK(ct::readTextFile(game.manifest())->find("weapons pistol heavy_pistol  # in number-key") !=
          std::string::npos);
}

TEST_CASE("a game has at most one weapon for each number key") {
    TestGame game;
    ct::GameData data = game.load();
    std::string error;
    for (std::uint32_t i = 1; i < ct::kMaxWeapons; ++i) {
        REQUIRE_MESSAGE(data.createWeapon(std::format("pistol_{}", i), "pistol", error), error);
    }
    CHECK(data.settings().weapons.size() == ct::kMaxWeapons);
    CHECK_FALSE(data.createWeapon("one_too_many", "pistol", error));
    CHECK(error.find("one for each number key") != std::string::npos);
}

TEST_CASE("weapons move between number keys, and only the game's own are deleted") {
    TestGame game;
    ct::GameData data = game.load();
    std::string error;
    REQUIRE_MESSAGE(data.createWeapon("rifle", "pistol", error), error);
    REQUIRE_MESSAGE(data.createWeapon("cannon", "pistol", error), error);
    set(data, "enemies/gunner.cfg", "weapon", "cannon");
    CHECK(data.settings().enemies[0].weaponIndex == 2);

    REQUIRE_MESSAGE(data.moveWeapon("cannon", 0, error), error);
    CHECK(data.manifest().weapons == std::vector<std::string>{"cannon", "pistol", "rifle"});
    CHECK(data.settings().weapons[0].name == "cannon");
    CHECK(data.settings().enemies[0].weaponIndex == 0); // found by name again
    CHECK(data.undoName() == "Move cannon to key 1");
    CHECK_FALSE(data.moveWeapon("cannon", 3, error));
    CHECK_FALSE(data.moveWeapon("nobody", 0, error));
    const std::uint64_t before = data.changeCount();
    REQUIRE(data.moveWeapon("cannon", 0, error)); // where it is already
    CHECK(data.changeCount() == before);
    REQUIRE(data.undo());
    CHECK(data.manifest().weapons == std::vector<std::string>{"pistol", "rifle", "cannon"});

    CHECK_FALSE(data.deleteWeapon("pistol", error));
    CHECK(error.find("comes with the engine") != std::string::npos);
    CHECK_FALSE(data.deleteWeapon("gunner", error));
    REQUIRE_MESSAGE(data.deleteWeapon("rifle", error), error);
    CHECK(data.manifest().weapons == std::vector<std::string>{"pistol", "cannon"});
    CHECK(data.findFile("weapons/rifle.cfg") == nullptr);
    CHECK(data.settings().enemies[0].weaponIndex == 1);
    CHECK(data.undoName() == "Delete weapon rifle");
    REQUIRE(data.undo());
    CHECK(data.settings().weapons.size() == 3);
    CHECK(data.settings().weapons[1].name == "rifle");
}

TEST_CASE("files changed on disk are read again, unless edits here are unsaved") {
    TestGame game;
    ct::GameData data = game.load();
    CHECK_FALSE(data.reloadIfChanged());
    ct::test::writeFile(game.game() / "enemies/gunner.cfg", "health 10\n");
    CHECK(data.reloadIfChanged());
    CHECK(data.settings().enemies[0].health == 10);

    set(data, "enemies/gunner.cfg", "speed", "3");
    ct::test::writeFile(game.engine() / "enemies/gunner.cfg", "speed 9\n");
    CHECK_FALSE(data.reloadIfChanged());
    CHECK(data.settings().enemies[0].speed == 3.0f);
}

TEST_CASE("unknown settings in the files are warnings") {
    TestGame game;
    ct::test::writeFile(game.game() / "enemies/gunner.cfg", "wings 2\n");
    const ct::GameData data = game.load();
    REQUIRE(data.warnings().size() == 1);
    CHECK(data.warnings()[0].find("wings") != std::string::npos);
}

TEST_CASE("behaviours are edited as whole files, and play as edited") {
    TestGame game;
    ct::test::writeFile(game.engine() / "behaviours/coward.behaviour", "patrol\n");
    ct::GameData data = game.load();
    CHECK(data.behaviourNames() == std::vector<std::string>{"standard", "coward"});
    CHECK(data.behaviourText("standard") == ct::standardBehaviourText());
    CHECK(data.behaviourText("coward") == "patrol\n");
    CHECK_FALSE(data.behaviourText("brave").has_value());
    set(data, "enemies/gunner.cfg", "behaviour", "coward");
    REQUIRE(data.settings().behaviours.size() == 1);
    CHECK(data.settings().behaviours[0].nodes.size() == 1);

    // The game's copy takes the engine's place; changes made one after another join.
    data.setBehaviourText("coward", "sequence\n    see\n    patrol\n", "Edit coward", false);
    data.setBehaviourText("coward", "sequence\n    hear\n    patrol\n", "Edit coward", true);
    CHECK(data.hasGameBehaviour("coward"));
    CHECK(data.hasBaseBehaviour("coward"));
    CHECK(data.isBehaviourUnsaved("coward"));
    CHECK(data.settings().behaviours[0].nodes.size() == 3);
    CHECK(data.settings().behaviours[0].nodes[1].kind == ct::BehaviourNodeKind::Hear);
    // A tree that won't load leaves the type on the standard tree, with a warning.
    data.setBehaviourText("coward", "fallback\n", "Edit coward", false);
    CHECK(data.settings().behaviours.empty());
    CHECK(data.warnings().back().find("has nothing under it") != std::string::npos);
    REQUIRE(data.undo());
    REQUIRE(data.undo());
    CHECK_FALSE(data.hasGameBehaviour("coward"));
    CHECK(data.settings().behaviours[0].nodes.size() == 1);
    REQUIRE(data.redo());

    std::string error;
    REQUIRE_MESSAGE(data.createBehaviour("brave", "coward", error), error);
    CHECK_FALSE(data.createBehaviour("brave", "coward", error));
    CHECK_FALSE(data.createBehaviour("Brave", "coward", error));
    CHECK_FALSE(data.createBehaviour("bold", "nobody", error));
    CHECK(data.behaviourNames() == std::vector<std::string>{"standard", "brave", "coward"});
    CHECK(data.behaviourText("brave") == "sequence\n    hear\n    patrol\n");
    CHECK_FALSE(data.hasBaseBehaviour("brave"));
    REQUIRE_MESSAGE(data.save(error), error);
    CHECK(ct::readTextFile(game.game() / "behaviours/brave.behaviour") ==
          "sequence\n    hear\n    patrol\n");
    CHECK(data.behaviourSavePath("brave") == game.game() / "behaviours/brave.behaviour");

    // Taken away, the game's copy goes; the engine's comes back.
    REQUIRE_MESSAGE(data.removeGameBehaviour("brave", error), error);
    REQUIRE_MESSAGE(data.removeGameBehaviour("coward", error), error);
    CHECK_FALSE(data.removeGameBehaviour("standard", error));
    CHECK(data.behaviourNames() == std::vector<std::string>{"standard", "coward"});
    CHECK(data.behaviourText("coward") == "patrol\n");
    REQUIRE_MESSAGE(data.save(error), error);
    CHECK_FALSE(std::filesystem::exists(game.game() / "behaviours/brave.behaviour"));
    CHECK_FALSE(std::filesystem::exists(game.game() / "behaviours/coward.behaviour"));
}
