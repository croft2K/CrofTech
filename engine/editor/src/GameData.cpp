#include "croftech/editor/GameData.h"

#include "croftech/core/ConfigFile.h"
#include "croftech/level/Level.h"
#include "croftech/sim/BehaviourTree.h"

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

constexpr std::string_view kStandardBehaviour = "standard";

std::string behaviourPath(std::string_view name) {
    return std::format("behaviours/{}.behaviour", name);
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

std::optional<std::string> GameData::baseText(const std::string& relative) const {
    const std::vector<std::filesystem::path>& layers = m_content.layers();
    for (std::size_t i = layers.size() - 1; i-- > 0;) {
        if (std::optional<std::string> text = readTextFile(layers[i] / relative)) {
            return text;
        }
    }
    return std::nullopt;
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
    // Behaviours play as edited, saved or not.
    std::vector<BehaviourText> behaviours;
    const std::filesystem::path behaviourFolder = topPath("behaviours");
    for (const auto& [path, file] : m_own) {
        if (file.text && path.parent_path() == behaviourFolder &&
            path.extension() == ".behaviour") {
            behaviours.push_back({path.stem().string(), *file.text});
        }
    }
    for (std::string& problem : loadBehaviourTrees(m_content, m_settings, behaviours)) {
        m_warnings.push_back(std::move(problem));
    }
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

namespace {

// How each kind game.cfg lists is named and kept, by GameData::Listed.
struct ListedInfo {
    std::string_view folder;  // and game.cfg's key
    std::string_view noun;    // "enemy type"
    std::string_view article; // "an"
    std::string_view prefix;  // of its console variables
    std::size_t most;
};

constexpr ListedInfo kListedInfo[] = {
    {"enemies", "enemy type", "an", "enemy", kMaxEnemyTypes},
    {"weapons", "weapon", "a", "weapon", kMaxWeapons},
};

} // namespace

bool GameData::createEnemyType(std::string_view name, std::string_view from, std::string& error) {
    return createListed(Listed::Enemy, name, from, error);
}

bool GameData::deleteEnemyType(std::string_view name, std::string& error) {
    return deleteListed(Listed::Enemy, name, error);
}

bool GameData::createWeapon(std::string_view name, std::string_view from, std::string& error) {
    return createListed(Listed::Weapon, name, from, error);
}

bool GameData::deleteWeapon(std::string_view name, std::string& error) {
    return deleteListed(Listed::Weapon, name, error);
}

bool GameData::moveWeapon(std::string_view name, std::size_t place, std::string& error) {
    std::vector<std::string> names = m_manifest.weapons;
    const auto found = std::find(names.begin(), names.end(), name);
    if (found == names.end()) {
        error = std::format("The game has no weapon '{}'.", name);
        return false;
    }
    if (place >= names.size()) {
        error = std::format("The game has only {} weapons.", names.size());
        return false;
    }
    if (static_cast<std::size_t>(found - names.begin()) == place) {
        return true;
    }
    std::string moved = std::move(*found);
    names.erase(found);
    names.insert(names.begin() + static_cast<std::ptrdiff_t>(place), std::move(moved));
    recordList(Listed::Weapon, names, std::format("Move {} to key {}", name, place + 1), {});
    return true;
}

const std::vector<std::string>& GameData::listed(Listed kind) const {
    return kind == Listed::Enemy ? m_manifest.enemies : m_manifest.weapons;
}

bool GameData::createListed(Listed kind, std::string_view name, std::string_view from,
                            std::string& error) {
    const ListedInfo& info = kListedInfo[static_cast<std::size_t>(kind)];
    const std::vector<std::string>& names = listed(kind);
    if (!isContentName(name) || name.size() > kMaxNameLength) {
        error = "A name is lowercase letters, digits, '-' and '_'.";
        return false;
    }
    if (std::find(names.begin(), names.end(), name) != names.end()) {
        error =
            std::format("The game already has {} {} named '{}'.", info.article, info.noun, name);
        return false;
    }
    if (names.size() >= info.most) {
        error = std::format("A game has at most {} {}s{}.", info.most, info.noun,
                            kind == Listed::Weapon ? ", one for each number key" : "");
        return false;
    }
    const std::string relative = std::format("{}/{}.cfg", info.folder, name);
    const std::filesystem::path path = topPath(relative);
    if (!readBase(relative).layers.empty() || own(path).text) {
        error = std::format("There is already a file {} in the content.", relative);
        return false;
    }
    const auto fromName = std::find(names.begin(), names.end(), from);
    const GameConfigFile* fromFile = findFile(std::format("{}/{}.cfg", info.folder, from));
    if (fromName == names.end() || fromFile == nullptr) {
        error = std::format("There is no {} '{}' to make it from.", info.noun, from);
        return false;
    }

    // The lowest file of the one it is made from, with every higher layer's settings written in,
    // so the new one has every setting the other has, and keeps its comments.
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
    text = std::format("# {}: {} {} made in the editor from the {}. Each line is also a console\n"
                       "# variable named {}.{}.<setting>.\n\n{}",
                       name, info.article, info.noun, from, info.prefix, name, withoutHeader(text));
    // It sounds like the one it is made from.
    const std::size_t fromIndex = static_cast<std::size_t>(fromName - names.begin());
    std::string sounds;
    if (kind == Listed::Enemy && fromIndex < m_settings.enemies.size() &&
        m_settings.enemies[fromIndex].sounds.empty()) {
        sounds = m_settings.enemies[fromIndex].soundSet();
    } else if (kind == Listed::Weapon && fromIndex < m_settings.weapons.size() &&
               m_settings.weapons[fromIndex].sounds.empty()) {
        sounds = m_settings.weapons[fromIndex].soundSet();
    }
    if (!sounds.empty()) {
        text = setConfigValue(text, "sounds", sounds);
    }

    std::vector<std::string> added = names;
    added.emplace_back(name);
    std::vector<Change> changes;
    changes.push_back({path, std::nullopt, std::move(text)});
    recordList(kind, added, std::format("New {} {}", info.noun, name), std::move(changes));
    return true;
}

bool GameData::deleteListed(Listed kind, std::string_view name, std::string& error) {
    const ListedInfo& info = kListedInfo[static_cast<std::size_t>(kind)];
    const std::vector<std::string>& names = listed(kind);
    const GameConfigFile* file = findFile(std::format("{}/{}.cfg", info.folder, name));
    if (std::find(names.begin(), names.end(), name) == names.end() || file == nullptr) {
        error = std::format("The game has no {} '{}'.", info.noun, name);
        return false;
    }
    if (!isGameOwned(*file)) {
        error = std::format("The {} comes with the engine; only the game's own {}s can be deleted.",
                            name, info.noun);
        return false;
    }
    std::vector<std::string> kept;
    for (const std::string& each : names) {
        if (each != name) {
            kept.push_back(each);
        }
    }
    const std::filesystem::path path = topPath(file->path);
    std::vector<Change> changes;
    changes.push_back({path, own(path).text, std::nullopt});
    recordList(kind, kept, std::format("Delete {} {}", info.noun, name), std::move(changes));
    return true;
}

void GameData::recordList(Listed kind, const std::vector<std::string>& names, std::string editName,
                          std::vector<Change> changes) {
    const std::string_view key = kListedInfo[static_cast<std::size_t>(kind)].folder;
    const std::optional<std::string> manifestBefore = own(m_manifestPath).text;
    std::string manifestAfter = setConfigValue(manifestBefore.value_or(""), key, joinNames(names));
    changes.push_back({m_manifestPath, manifestBefore, std::move(manifestAfter)});
    record({std::move(editName), std::move(changes), {}}, false);
}

std::vector<std::string> GameData::behaviourNames() const {
    std::vector<std::string> names;
    const auto add = [&names](std::string name) {
        if (std::find(names.begin(), names.end(), name) == names.end()) {
            names.push_back(std::move(name));
        }
    };
    const std::filesystem::path behaviourFolder = topPath("behaviours");
    for (const std::filesystem::path& layer : m_content.layers()) {
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(layer / "behaviours", error)) {
            const std::filesystem::path& path = entry.path();
            const std::string name = path.stem().string();
            // A file of the game's taken away here, and not yet saved, is gone.
            const auto own = m_own.find(path);
            if (path.extension() == ".behaviour" && isContentName(name) &&
                (own == m_own.end() || own->second.text)) {
                add(name);
            }
        }
    }
    for (const auto& [path, file] : m_own) {
        if (file.text && path.parent_path() == behaviourFolder &&
            path.extension() == ".behaviour") {
            add(path.stem().string());
        }
    }
    std::erase(names, kStandardBehaviour);
    std::sort(names.begin(), names.end());
    names.insert(names.begin(), std::string(kStandardBehaviour));
    return names;
}

std::optional<std::string> GameData::behaviourText(std::string_view name) {
    const std::string relative = behaviourPath(name);
    if (const std::optional<std::string>& text = own(topPath(relative)).text) {
        return text;
    }
    if (std::optional<std::string> text = baseText(relative)) {
        return text;
    }
    if (name == kStandardBehaviour) {
        return std::string(standardBehaviourText());
    }
    return std::nullopt;
}

bool GameData::hasGameBehaviour(std::string_view name) {
    return own(topPath(behaviourPath(name))).text.has_value();
}

bool GameData::hasBaseBehaviour(std::string_view name) const {
    return baseText(behaviourPath(name)).has_value();
}

bool GameData::isBehaviourUnsaved(std::string_view name) const {
    const auto found = m_own.find(topPath(behaviourPath(name)));
    return found != m_own.end() && found->second.text != found->second.saved;
}

std::filesystem::path GameData::behaviourSavePath(std::string_view name) const {
    return topPath(behaviourPath(name));
}

void GameData::setBehaviourText(std::string_view name, std::string text, std::string editName,
                                bool merge) {
    const std::filesystem::path path = topPath(behaviourPath(name));
    std::optional<std::string> before = own(path).text;
    if (before == text) {
        return;
    }
    std::string mergeKey = std::format("{}|{}", behaviourPath(name), editName);
    record({std::move(editName), {{path, std::move(before), std::move(text)}}, std::move(mergeKey)},
           merge);
}

bool GameData::createBehaviour(std::string_view name, std::string_view from, std::string& error) {
    if (!isContentName(name) || name.size() > kMaxNameLength) {
        error = "A behaviour's name is lowercase letters, digits, '-' and '_'.";
        return false;
    }
    if (behaviourText(name)) {
        error = std::format("There is already a behaviour named '{}'.", name);
        return false;
    }
    std::optional<std::string> text = behaviourText(from);
    if (!text) {
        error = std::format("There is no behaviour '{}' to make it from.", from);
        return false;
    }
    record({std::format("New behaviour {}", name),
            {{topPath(behaviourPath(name)), std::nullopt, std::move(*text)}},
            {}},
           false);
    return true;
}

bool GameData::removeGameBehaviour(std::string_view name, std::string& error) {
    const std::filesystem::path path = topPath(behaviourPath(name));
    std::optional<std::string> before = own(path).text;
    if (!before) {
        error = std::format("The game has no {} of its own.", behaviourPath(name));
        return false;
    }
    record({std::format("{} behaviour {}", hasBaseBehaviour(name) ? "Reset" : "Delete", name),
            {{path, std::move(before), std::nullopt}},
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
