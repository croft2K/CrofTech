#pragma once

#include "croftech/core/Console.h"
#include "croftech/core/Content.h"
#include "croftech/sim/GameSettings.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ct {

// Where a setting's value comes from.
enum class SettingOrigin : std::uint8_t {
    Default, // no file sets it: the engine's built-in default
    Base,    // a lower content layer's file (the engine's content)
    Game,    // the game's own content folder, the top layer, which the editor writes
};

struct GameDataSetting {
    std::string key; // without the file's prefix: "aim_error"
    // Its value now, with its type, range and description. Valid until the next change.
    const CVar* variable = nullptr;
    SettingOrigin origin = SettingOrigin::Default;
    // What it would be without the game's line: the lower layers' value, or the default.
    std::string baseValue;
};

// The game's data files as the editor edits them: game.cfg, and the settings files gameConfigFiles
// lists (movement, rules, each weapon and each enemy type), read through the content layers. Edits
// change the copies held here, are undone and redone, and reach the disk when saved; the settings
// they make (settings()) follow every edit, so the editor and play-in-editor use them at once.
//
// Everything is written into the game's own content folder, the top layer. A file the engine's
// content also has gets one there holding only the settings the game changes, so the engine's file
// is left as it is and its other settings still come through.
class GameData {
public:
    // Reads game.cfg (manifest) and the files it leads to. Fails, saying why, if game.cfg can't be
    // read; problems in the settings files are warnings().
    static std::optional<GameData> load(const ContentPaths& content,
                                        const std::filesystem::path& manifest, std::string& error);

    const GameManifest& manifest() const { return m_manifest; }
    const GameSettings& settings() const { return m_settings; }
    // Changes with every change to the files held here, saved or not.
    std::uint64_t changeCount() const { return m_changeCount; }
    // Settings the files give that the game doesn't know or won't take, naming file and line.
    const std::vector<std::string>& warnings() const { return m_warnings; }

    // The settings files, as gameConfigFiles lists them, and one by its path
    // ("enemies/gunner.cfg").
    std::span<const GameConfigFile> files() const { return m_files; }
    const GameConfigFile* findFile(std::string_view path) const;
    // Its settings, in the order its files list them, then the rest by name.
    std::vector<GameDataSetting> settingsOf(const GameConfigFile& file) const;
    // Whether only the game's content folder has the file, so it is all the game's own.
    bool isGameOwned(const GameConfigFile& file) const;
    // Where the file's edits are saved.
    std::filesystem::path savePath(const GameConfigFile& file) const;

    // Sets the file's setting to the value, which must suit its variable. With merge, an edit that
    // follows one of the same setting joins it, so a drag is one undo step. In a file the game
    // shares with the engine, a value the same as the engine's takes the game's line away.
    bool set(GameConfigFile file, std::string_view key, std::string_view value, bool merge,
             std::string& error);
    // Takes the game's line for the setting away, back to the engine's value. Returns whether
    // there was one.
    bool reset(GameConfigFile file, std::string_view key);

    // A new enemy type, enemies/<name>.cfg in the game's content folder with every setting of the
    // type it is made from, which it sounds like, added to game.cfg's enemies after the others.
    bool createEnemyType(std::string_view name, std::string_view from, std::string& error);
    // Takes an enemy type the game owns out of game.cfg, and its file once saved.
    bool deleteEnemyType(std::string_view name, std::string& error);
    // The same for weapons: a new one, weapons/<name>.cfg, takes the next number key.
    bool createWeapon(std::string_view name, std::string_view from, std::string& error);
    bool deleteWeapon(std::string_view name, std::string& error);
    // Moves a weapon to another place in game.cfg's list, which is the number-key order: place 0
    // is key 1.
    bool moveWeapon(std::string_view name, std::size_t place, std::string& error);

    // Behaviour trees: behaviours/<name>.behaviour through the content layers, which the
    // Behaviours window edits as whole texts, with the same undo and saving. Enemy types run them
    // as edited. As with other files that aren't settings, the game's copy of one takes the place
    // of the engine's whole.
    // Every behaviour the content has or that was made here, by name, the standard one first.
    std::vector<std::string> behaviourNames() const;
    // Its text as edited; the built-in one for the standard behaviour if no layer has it.
    std::optional<std::string> behaviourText(std::string_view name);
    // Whether the game's content folder has it, saved or not.
    bool hasGameBehaviour(std::string_view name);
    // Whether a lower layer (the engine's content) has it, so taking the game's copy away goes
    // back to that one.
    bool hasBaseBehaviour(std::string_view name) const;
    bool isBehaviourUnsaved(std::string_view name) const;
    std::filesystem::path behaviourSavePath(std::string_view name) const;
    // Sets the behaviour's text in the game's folder, as an undo step named editName. With merge,
    // it joins the step before if that has the same name, so a drag is one undo step.
    void setBehaviourText(std::string_view name, std::string text, std::string editName,
                          bool merge);
    // A new behaviour of the game's own, a copy of another.
    bool createBehaviour(std::string_view name, std::string_view from, std::string& error);
    // Takes the game's copy away (back to the engine's, if it has one).
    bool removeGameBehaviour(std::string_view name, std::string& error);

    bool canUndo() const { return !m_done.empty(); }
    bool canRedo() const { return !m_undone.empty(); }
    std::string_view undoName() const;
    std::string_view redoName() const;
    bool undo();
    bool redo();

    bool hasUnsavedChanges() const;
    bool isUnsaved(const GameConfigFile& file) const;
    // The files that would change, relative to the game's folder where they are in it.
    std::vector<std::string> unsavedFiles() const;
    bool save(std::string& error);
    // Reads the files again if any changed on disk (written by hand alongside the editor), unless
    // edits here are unsaved, which would be lost. Returns whether it did. Undo starts afresh.
    bool reloadIfChanged();

private:
    // A file of the game's that the editor writes. Nullopt texts are files that don't exist.
    struct OwnFile {
        std::optional<std::string> saved; // as on disk
        std::optional<std::string> text;  // as edited
    };
    struct Change {
        std::filesystem::path path;
        std::optional<std::string> before;
        std::optional<std::string> after;
    };
    struct Edit {
        std::string name;
        std::vector<Change> changes;
        std::string mergeKey; // file and setting; empty never merges
    };
    // A settings file's copies in the lower layers, lowest first, which the editor only reads.
    struct BaseFile {
        std::vector<LayeredConfig::Layer> layers;
        std::vector<std::string> texts;
    };

    // What game.cfg lists, each a settings file in a folder of its own.
    enum class Listed : std::uint8_t { Enemy, Weapon };

    GameData(ContentPaths content, std::filesystem::path manifest);

    bool readAll(std::string& error);
    const std::vector<std::string>& listed(Listed kind) const;
    bool createListed(Listed kind, std::string_view name, std::string_view from,
                      std::string& error);
    bool deleteListed(Listed kind, std::string_view name, std::string& error);
    // Records game.cfg's list of the kind as names, with the file changes given, as one edit.
    void recordList(Listed kind, const std::vector<std::string>& names, std::string editName,
                    std::vector<Change> changes);
    // Builds the settings again from the edited texts.
    bool rebuild(std::string& error);
    void apply(const Edit& edit, bool undoing);
    void record(Edit edit, bool merge);
    BaseFile readBase(const std::string& relative) const;
    // The file's lower layers, read once and kept.
    const BaseFile& base(const std::string& relative);
    std::filesystem::path topPath(const std::string& relative) const;
    // The text of a lower layer's copy of the file, the highest that has one.
    std::optional<std::string> baseText(const std::string& relative) const;
    // The game's copy of the file, read from disk the first time.
    OwnFile& own(const std::filesystem::path& path);

    ContentPaths m_content;
    std::filesystem::path m_manifestPath;
    GameManifest m_manifest;
    std::map<std::filesystem::path, OwnFile> m_own;
    std::map<std::string, BaseFile> m_base;
    std::vector<GameConfigFile> m_files;
    std::unique_ptr<Console> m_console;
    GameSettings m_settings;
    std::vector<std::string> m_warnings;
    std::vector<Edit> m_done;
    std::vector<Edit> m_undone;
    std::uint64_t m_changeCount = 0;
};

} // namespace ct
