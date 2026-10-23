#include "croftech/editor/GameData.h"

#include "croftech/core/ConfigFile.h"
#include "croftech/level/Level.h"

#include <algorithm>
#include <format>
#include <system_error>
#include <utility>

namespace ct {
namespace {

constexpr std::size_t kMaxHistory = 1000;

const std::vector<LayeredConfig::Layer> kNoLayers;

// "gunner" for enemies/gunner.cfg, "movement" for config/movement.cfg.
std::string fileLabel(const GameConfigFile& file) {
    return std::filesystem::path(file.path).stem().string();
}

bool hasKey(std::span<const ConfigEntry> entries, std::string_view key) {
    return std::any_of(entries.begin(), entries.end(),
                       [key](const ConfigEntry& entry) { return entry.key == key; });
}

// The value the layers give the key, the last one winning; nullopt if none sets it.
std::optional<std::string> layersValue(std::span<const LayeredConfig::Layer> layers,
                                       std::string_view key) {
    std::optional<std::string> value;
    for (const LayeredConfig::Layer& layer : layers) {
        for (const ConfigEntry& entry : layer.entries) {
            if (entry.key == key) {
                value = entry.value;
            }
        }
    }
    return value;
}

// The text without the comments and blank lines it starts with.
std::string_view withoutHeader(std::string_view text) {
    while (!text.empty()) {
        const std::size_t newline = text.find('\n');
        const std::string_view line = text.substr(0, newline);
        const std::size_t first = line.find_first_not_of(" \t\r");
        if (first != std::string_view::npos && line[first] != '#') {
            break;
        }
        text.remove_prefix(newline == std::string_view::npos ? text.size() : newline + 1);
    }
    return text;
}

std::string_view trimmed(std::string_view text) {
    const std::size_t first = text.find_first_not_of(" 	");
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" 	") - first + 1);
}

std::string joinNames(std::span<const std::string> names) {
    std::string joined;
    for (const std::string& name : names) {
        joined += joined.empty() ? "" : " ";
        joined += name;
    }
    return joined;
}

} // namespace

GameData::GameData(ContentPaths content, std::filesystem::path manifest)
    : m_content(std::move(content)), m_manifestPath(std::move(manifest)) {}

std::optional<GameData> GameData::load(const ContentPaths& content,
                                       const std::filesystem::path& manifest, std::string& error) {
    if (content.layers().empty()) {
        error = "there is no content folder";
        return std::nullopt;
    }
    GameData data(content, manifest);
    if (!data.readAll(error)) {
        return std::nullopt;
    }
    return data;
}

bool GameData::readAll(std::string& error) {
    if (!own(m_manifestPath).saved) {
        std::filesystem::path shown = m_manifestPath;
        error = std::format("can't read {}", shown.make_preferred().string());
        return false;
    }
    return rebuild(error);
}

std::filesystem::path GameData::topPath(const std::string& relative) const {
    return m_content.layers().back() / relative;
}

GameData::OwnFile& GameData::own(const std::filesystem::path& path) {
    auto found = m_own.find(path);
    if (found == m_own.end()) {
        OwnFile file;
        file.saved = readTextFile(path);
        file.text = file.saved;
        found = m_own.emplace(path, std::move(file)).first;
    }
    return found->second;
}

GameData::BaseFile GameData::readBase(const std::string& relative) const {
    BaseFile file;
    const std::vector<std::filesystem::path>& layers = m_content.layers();
    for (std::size_t i = 0; i + 1 < layers.size(); ++i) {
        const std::filesystem::path path = layers[i] / relative;
        if (std::optional<std::string> text = readTextFile(path)) {
            file.layers.push_back({path, parseConfig(*text)});
            file.texts.push_back(std::move(*text));
        }
    }
    return file;
}

const GameData::BaseFile& GameData::base(const std::string& relative) {
    auto found = m_base.find(relative);
    if (found == m_base.end()) {
        found = m_base.emplace(relative, readBase(relative)).first;
    }
    return found->second;
}

bool GameData::rebuild(std::string& error) {
    const std::optional<std::string>& manifestText = own(m_manifestPath).text;
    std::optional<GameManifest> manifest =
        parseGameManifest(manifestText.value_or(""), m_manifestPath.parent_path(), error);
    if (!manifest) {
        return false;
    }
    m_manifest = std::move(*manifest);
    m_files = gameConfigFiles(m_manifest.weapons, m_manifest.enemies);
    auto console = std::make_unique<Console>();
    registerGameCVars(*console, m_manifest.weapons, m_manifest.enemies);
    m_warnings.clear();
    for (const GameConfigFile& file : m_files) {
        std::vector<LayeredConfig::Layer> layers = base(file.path).layers;
        const std::filesystem::path path = topPath(file.path);
        if (const std::optional<std::string>& text = own(path).text) {
            layers.push_back({path, parseConfig(*text)});
        }
        for (std::string& problem : console->applyConfig(file.prefix, layers)) {
            m_warnings.push_back(std::move(problem));
        }
    }
    m_settings = gameSettingsFromCVars(*console, m_manifest.weapons, m_manifest.enemies);
    m_console = std::move(console);
    ++m_changeCount;
    return true;
}

const GameConfigFile* GameData::findFile(std::string_view path) const {
    const auto found =
        std::find_if(m_files.begin(), m_files.end(),
                     [path](const GameConfigFile& file) { return file.path == path; });
    return found != m_files.end() ? &*found : nullptr;
}

bool GameData::isGameOwned(const GameConfigFile& file) const {
    const auto found = m_base.find(file.path);
    return found == m_base.end() || found->second.layers.empty();
}

std::filesystem::path GameData::savePath(const GameConfigFile& file) const {
    return topPath(file.path);
}

std::vector<GameDataSetting> GameData::settingsOf(const GameConfigFile& file) const {
    const auto foundBase = m_base.find(file.path);
    const std::span<const LayeredConfig::Layer> baseLayers =
        foundBase != m_base.end() ? std::span(foundBase->second.layers) : std::span(kNoLayers);
    std::vector<ConfigEntry> ownEntries;
    if (const auto found = m_own.find(topPath(file.path));
        found != m_own.end() && found->second.text) {
        ownEntries = parseConfig(*found->second.text);
    }

    // The order the files list them in, then the rest by name.
    std::vector<std::string> keys;
    const auto addKey = [&](std::string_view key) {
        if (std::find(keys.begin(), keys.end(), key) == keys.end() &&
            m_console->find(file.prefix + std::string(key)) != nullptr) {
            keys.emplace_back(key);
        }
    };
    for (const LayeredConfig::Layer& layer : baseLayers) {
        for (const ConfigEntry& entry : layer.entries) {
            addKey(entry.key);
        }
    }
    for (const ConfigEntry& entry : ownEntries) {
        addKey(entry.key);
    }
    for (const std::string& name : m_console->complete(file.prefix)) {
        addKey(std::string_view(name).substr(file.prefix.size()));
    }

    std::vector<GameDataSetting> settings;
    settings.reserve(keys.size());
    for (std::string& key : keys) {
        GameDataSetting setting;
        setting.variable = m_console->find(file.prefix + key);
        const std::optional<std::string> baseValue = layersValue(baseLayers, key);
        setting.baseValue = baseValue.value_or(setting.variable->defaultText());
        setting.origin = hasKey(ownEntries, key) ? SettingOrigin::Game
                         : baseValue             ? SettingOrigin::Base
                                                 : SettingOrigin::Default;
        setting.key = std::move(key);
        settings.push_back(std::move(setting));
    }
    return settings;
}

bool GameData::set(GameConfigFile file, std::string_view key, std::string_view value, bool merge,
                   std::string& error) {
    const CVar* variable = m_console->find(file.prefix + std::string(key));
    if (variable == nullptr) {
        error = std::format("{} has no setting '{}'", fileLabel(file), key);
        return false;
    }
    if (!isConfigValue(value)) {
        error = std::format("{} can't hold '#' or line breaks", key);
        return false;
    }
    CVar probe = *variable;
    if (!probe.set(value)) {
        error = std::format("{} must be {}", key, probe.describeRange());
        return false;
    }
    // As the file will read back: numbers in their shortest form, text without spaces at the ends.
    const bool text = variable->type() == CVar::Type::String;
    const std::string written = text ? std::string(trimmed(value)) : probe.text();

    const std::filesystem::path path = topPath(file.path);
    OwnFile& ownFile = own(path);
    const std::optional<std::string> before = ownFile.text;
    std::optional<std::string> after;
    const BaseFile& baseFile = base(file.path);
    if (baseFile.layers.empty()) {
        after = setConfigValue(before.value_or(""), key, written);
    } else {
        // The engine's file has the rest; the game's says only what differs.
        CVar baseProbe = *variable;
        const std::string baseValue =
            layersValue(baseFile.layers, key).value_or(variable->defaultText());
        const bool same =
            baseProbe.set(baseValue) && (text ? baseValue : baseProbe.text()) == written;
        if (same) {
            after = before ? std::optional(removeConfigValue(*before, key)) : std::nullopt;
        } else {
            after = setConfigValue(
                before.value_or(std::format(
                    "# {}'s changes to the {} of the engine's content: each line here wins over "
                    "the engine's.\n\n",
                    m_manifest.name, fileLabel(file))),
                key, written);
        }
        if (after && !ownFile.saved && parseConfig(*after).empty()) {
            after.reset(); // nothing left to say, and no file to keep
        }
    }
    if (after == before) {
        return true;
    }
    record({std::format("Set {} {}", fileLabel(file), key),
            {{path, before, std::move(after)}},
            std::format("{}|{}", file.path, key)},
           merge);
    return true;
}

bool GameData::reset(GameConfigFile file, std::string_view key) {
    const std::filesystem::path path = topPath(file.path);
    OwnFile& ownFile = own(path);
    if (!ownFile.text || !hasKey(parseConfig(*ownFile.text), key)) {
        return false;
    }
    std::optional<std::string> after = removeConfigValue(*ownFile.text, key);
    if (!base(file.path).layers.empty() && !ownFile.saved && parseConfig(*after).empty()) {
        after.reset();
    }
    record({std::format("Reset {} {}", fileLabel(file), key), {{path, ownFile.text, after}}, {}},
           false);
    return true;
}

bool GameData::createEnemyType(std::string_view name, std::string_view from, std::string& error) {
    if (!isContentName(name) || name.size() > kMaxNameLength) {
        error = "A type's name is lowercase letters, digits, '-' and '_'.";
        return false;
    }
    const std::vector<std::string>& enemies = m_manifest.enemies;
    if (std::find(enemies.begin(), enemies.end(), name) != enemies.end()) {
        error = std::format("The game already has an enemy type named '{}'.", name);
        return false;
    }
    if (enemies.size() >= kMaxEnemyTypes) {
        error = std::format("A game has at most {} enemy types.", kMaxEnemyTypes);
        return false;
    }
    const std::string relative = std::format("enemies/{}.cfg", name);
    const std::filesystem::path path = topPath(relative);
    if (!readBase(relative).layers.empty() || own(path).text) {
        error = std::format("There is already a file {} in the content.", relative);
        return false;
    }
    const auto fromName = std::find(enemies.begin(), enemies.end(), from);
    const GameConfigFile* fromFile = findFile(std::format("enemies/{}.cfg", from));
    if (fromName == enemies.end() || fromFile == nullptr) {
        error = std::format("There is no enemy type '{}' to make it from.", from);
        return false;
    }

    // The lowest file of the type it is made from, with every higher layer's settings written in,
    // so the new type has every setting the other has, and keeps its comments.
    const BaseFile& fromBase = base(fromFile->path);
    std::vector<std::span<const ConfigEntry>> higher;
    std::string text;
    const std::optional<std::string>& fromOwn = own(topPath(fromFile->path)).text;
    std::vector<ConfigEntry> fromOwnEntries;
    if (fromOwn) {
        fromOwnEntries = parseConfig(*fromOwn);
    }
    if (!fromBase.texts.empty()) {
        text = fromBase.texts.front();
        for (std::size_t i = 1; i < fromBase.layers.size(); ++i) {
            higher.emplace_back(fromBase.layers[i].entries);
        }
        higher.emplace_back(fromOwnEntries);
    } else {
        text = fromOwn.value_or("");
    }
    for (const std::span<const ConfigEntry> entries : higher) {
        for (const ConfigEntry& entry : entries) {
            text = setConfigValue(text, entry.key, entry.value);
        }
    }
    text = std::format("# {}: an enemy type made in the editor from the {}. Each line is also a "
                       "console\n# variable named enemy.{}.<setting>.\n\n{}",
                       name, from, name, withoutHeader(text));
    const std::size_t fromIndex = static_cast<std::size_t>(fromName - enemies.begin());
    if (fromIndex < m_settings.enemies.size() && m_settings.enemies[fromIndex].sounds.empty()) {
        text = setConfigValue(text, "sounds", m_settings.enemies[fromIndex].soundSet());
    }

    std::vector<std::string> names = enemies;
    names.emplace_back(name);
    const std::optional<std::string> manifestBefore = own(m_manifestPath).text;
    std::string manifestAfter =
        setConfigValue(manifestBefore.value_or(""), "enemies", joinNames(names));
    record({std::format("New enemy type {}", name),
            {{path, std::nullopt, std::move(text)},
             {m_manifestPath, manifestBefore, std::move(manifestAfter)}},
            {}},
           false);
    return true;
}

bool GameData::deleteEnemyType(std::string_view name, std::string& error) {
    const std::vector<std::string>& enemies = m_manifest.enemies;
    const GameConfigFile* file = findFile(std::format("enemies/{}.cfg", name));
    if (std::find(enemies.begin(), enemies.end(), name) == enemies.end() || file == nullptr) {
        error = std::format("The game has no enemy type '{}'.", name);
        return false;
    }
    if (!isGameOwned(*file)) {
        error = std::format("The {} comes with the engine; only the game's own types can be "
                            "deleted.",
                            name);
        return false;
    }
    std::vector<std::string> names;
    for (const std::string& enemy : enemies) {
        if (enemy != name) {
            names.push_back(enemy);
        }
    }
    const std::filesystem::path path = topPath(file->path);
    const std::optional<std::string> manifestBefore = own(m_manifestPath).text;
    std::string manifestAfter =
        setConfigValue(manifestBefore.value_or(""), "enemies", joinNames(names));
    record({std::format("Delete enemy type {}", name),
            {{path, own(path).text, std::nullopt},
             {m_manifestPath, manifestBefore, std::move(manifestAfter)}},
            {}},
           false);
    return true;
}

void GameData::record(Edit edit, bool merge) {
    if (merge && !edit.mergeKey.empty() && m_undone.empty() && !m_done.empty() &&
        m_done.back().mergeKey == edit.mergeKey) {
        Edit& last = m_done.back();
        for (Change& change : edit.changes) {
            const auto same = std::find_if(last.changes.begin(), last.changes.end(),
                                           [&](const Change& c) { return c.path == change.path; });
            if (same != last.changes.end()) {
                same->after = change.after;
            } else {
                last.changes.push_back(change);
            }
        }
        apply(edit, false);
        return;
    }
    apply(edit, false);
    m_undone.clear();
    m_done.push_back(std::move(edit));
    if (m_done.size() > kMaxHistory) {
        m_done.erase(m_done.begin());
    }
}

void GameData::apply(const Edit& edit, bool undoing) {
    for (const Change& change : edit.changes) {
        own(change.path).text = undoing ? change.before : change.after;
    }
    // Edits made here always leave game.cfg readable; a bad one from disk keeps the last settings.
    std::string ignored;
    rebuild(ignored);
}

std::string_view GameData::undoName() const {
    return m_done.empty() ? std::string_view() : std::string_view(m_done.back().name);
}

std::string_view GameData::redoName() const {
    return m_undone.empty() ? std::string_view() : std::string_view(m_undone.back().name);
}

bool GameData::undo() {
    if (m_done.empty()) {
        return false;
    }
    Edit edit = std::move(m_done.back());
    m_done.pop_back();
    // In reverse, so a file changed twice ends as it began.
    for (auto change = edit.changes.rbegin(); change != edit.changes.rend(); ++change) {
        own(change->path).text = change->before;
    }
    std::string ignored;
    rebuild(ignored);
    m_undone.push_back(std::move(edit));
    return true;
}

bool GameData::redo() {
    if (m_undone.empty()) {
        return false;
    }
    Edit edit = std::move(m_undone.back());
    m_undone.pop_back();
    apply(edit, false);
    m_done.push_back(std::move(edit));
    return true;
}

bool GameData::hasUnsavedChanges() const {
    return std::any_of(m_own.begin(), m_own.end(),
                       [](const auto& entry) { return entry.second.text != entry.second.saved; });
}

bool GameData::isUnsaved(const GameConfigFile& file) const {
    const auto found = m_own.find(topPath(file.path));
    return found != m_own.end() && found->second.text != found->second.saved;
}

std::vector<std::string> GameData::unsavedFiles() const {
    std::vector<std::string> files;
    const std::filesystem::path root = m_manifestPath.parent_path();
    for (const auto& [path, file] : m_own) {
        if (file.text != file.saved) {
            std::filesystem::path shown = path.lexically_relative(root);
            if (shown.empty() || *shown.begin() == "..") {
                shown = path;
            }
            files.push_back(shown.make_preferred().string());
        }
    }
    return files;
}

bool GameData::save(std::string& error) {
    for (auto& [path, file] : m_own) {
        if (file.text == file.saved) {
            continue;
        }
        if (file.text) {
            std::error_code ignored;
            std::filesystem::create_directories(path.parent_path(), ignored);
            if (!writeTextFile(path, *file.text, error)) {
                return false;
            }
        } else {
            std::error_code removeError;
            std::filesystem::remove(path, removeError);
            if (removeError) {
                std::filesystem::path shown = path;
                error =
                    std::format("{}: {}", shown.make_preferred().string(), removeError.message());
                return false;
            }
        }
        file.saved = file.text;
    }
    return true;
}

bool GameData::reloadIfChanged() {
    if (hasUnsavedChanges()) {
        return false;
    }
    bool changed = false;
    for (const auto& [path, file] : m_own) {
        changed = changed || readTextFile(path) != file.saved;
    }
    for (const auto& [relative, file] : m_base) {
        changed = changed || readBase(relative).texts != file.texts;
    }
    if (!changed) {
        return false;
    }
    m_own.clear();
    m_base.clear();
    m_done.clear();
    m_undone.clear();
    std::string error;
    if (!readAll(error)) {
        // game.cfg went missing or bad on disk: the settings stay as they were until it is put
        // right.
        m_warnings = {std::move(error)};
        ++m_changeCount;
    }
    return true;
}

} // namespace ct
