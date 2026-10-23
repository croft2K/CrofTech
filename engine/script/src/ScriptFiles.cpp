#include "croftech/script/ScriptFiles.h"

#include "croftech/core/ConfigFile.h"
#include "croftech/core/Hash.h"
#include "croftech/level/EntityClasses.h"

#include <algorithm>
#include <format>

namespace ct {

std::vector<ScriptSource> loadLevelScripts(const ContentPaths& content, const Level& level,
                                           std::vector<std::string>& warnings) {
    std::vector<std::string> names;
    const EntityClassInfo* scriptClass = findEntityClass(kScriptClass);
    for (const LevelEntity& entity : level.entities) {
        if (entity.className == kScriptClass && scriptClass != nullptr) {
            const std::string_view name = textProperty(entity, *scriptClass, "script");
            if (!name.empty()) {
                names.emplace_back(name);
            }
        }
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());

    std::vector<ScriptSource> scripts;
    for (const std::string& name : names) {
        if (!isValidScriptName(name)) {
            warnings.push_back(std::format(
                "'{}' isn't a script name: lowercase letters, digits, '-' or '_'", name));
            continue;
        }
        const std::string relative = std::format("scripts/{}.luau", name);
        const std::optional<std::filesystem::path> path = content.find(relative);
        if (!path) {
            warnings.push_back(std::format("no script {}", relative));
            continue;
        }
        std::optional<std::string> text = readTextFile(*path, kMaxScriptBytes);
        if (!text) {
            warnings.push_back(std::format("can't read {} (or it is over {} KB)", relative,
                                           kMaxScriptBytes / 1024));
            continue;
        }
        scripts.push_back({name, std::move(*text)});
    }
    return scripts;
}

std::uint64_t scriptSourcesHash(std::span<const ScriptSource> scripts, std::uint64_t seed) {
    std::uint64_t hash = seed;
    for (const ScriptSource& script : scripts) {
        // Lengths first, so no two different lists run together into the same bytes.
        hash = hashString(std::format("{}:{}:", script.name.size(), script.text.size()), hash);
        hash = hashString(script.name, hash);
        hash = hashString(script.text, hash);
    }
    return hash;
}

} // namespace ct
