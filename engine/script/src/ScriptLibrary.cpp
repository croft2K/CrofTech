#include "croftech/script/ScriptLibrary.h"

#include "croftech/core/ConfigFile.h"

#include <algorithm>
#include <format>
#include <system_error>

namespace ct {

void ScriptLibrary::compile(Entry& entry, std::string_view text) {
    entry.program.reset();
    entry.error = {};
    if (std::optional<ScriptProgram> program = ScriptProgram::load(entry.name, text, entry.error)) {
        entry.program = std::make_unique<ScriptProgram>(std::move(*program));
    }
}

bool ScriptLibrary::refresh() {
    // Where each script is now: the highest layer's copy.
    std::vector<std::pair<std::string, std::filesystem::path>> found;
    for (const std::filesystem::path& root : m_content.layers()) {
        std::error_code error;
        std::filesystem::directory_iterator it(root / "scripts", error);
        for (; !error && it != std::filesystem::directory_iterator(); it.increment(error)) {
            const std::filesystem::path& path = it->path();
            const std::string name = path.stem().string();
            if (path.extension() != ".luau" || !isValidScriptName(name) ||
                !it->is_regular_file(error)) {
                continue;
            }
            const auto same = std::find_if(found.begin(), found.end(), [&](const auto& script) {
                return script.first == name;
            });
            if (same != found.end()) {
                same->second = path;
            } else {
                found.emplace_back(name, path);
            }
        }
    }
    std::sort(found.begin(), found.end());

    bool changed = found.size() != m_entries.size();
    std::vector<Entry> entries;
    for (auto& [name, path] : found) {
        Entry next;
        const auto old = std::find_if(m_entries.begin(), m_entries.end(),
                                      [&](const Entry& e) { return e.name == name; });
        const bool known = old != m_entries.end();
        if (known) {
            next = std::move(*old);
        } else {
            next.name = name;
        }
        std::error_code error;
        std::optional<std::filesystem::file_time_type> writeTime =
            std::filesystem::last_write_time(path, error);
        if (error) {
            writeTime.reset();
        }
        const std::uintmax_t size = std::filesystem::file_size(path, error);
        if (!known || next.path != path || next.writeTime != writeTime || next.size != size) {
            changed = true;
            next.path = path;
            next.writeTime = writeTime;
            next.size = size;
            // Text being edited stands for the file until it is cleared.
            if (!next.text) {
                if (const std::optional<std::string> text = readTextFile(path, kMaxScriptBytes)) {
                    compile(next, *text);
                } else {
                    next.program.reset();
                    next.error = {std::format("can't read {} (or it is over {} KB)", path.string(),
                                              kMaxScriptBytes / 1024)};
                    // Probably caught mid-save; forget its time so the next refresh tries again.
                    next.writeTime.reset();
                }
            }
        }
        entries.push_back(std::move(next));
    }
    m_entries = std::move(entries);
    if (changed) {
        ++m_generation;
    }
    return changed;
}

std::vector<std::string_view> ScriptLibrary::names() const {
    std::vector<std::string_view> names;
    for (const Entry& entry : m_entries) {
        names.push_back(entry.name);
    }
    return names;
}

ScriptLibrary::Entry* ScriptLibrary::entry(std::string_view name) {
    const auto it = std::lower_bound(
        m_entries.begin(), m_entries.end(), name,
        [](const Entry& entry, std::string_view wanted) { return entry.name < wanted; });
    return it != m_entries.end() && it->name == name ? &*it : nullptr;
}

const ScriptLibrary::Entry* ScriptLibrary::entry(std::string_view name) const {
    return const_cast<ScriptLibrary*>(this)->entry(name);
}

const ScriptProgram* ScriptLibrary::find(std::string_view name) const {
    const Entry* found = entry(name);
    return found != nullptr ? found->program.get() : nullptr;
}

std::string_view ScriptLibrary::error(std::string_view name) const {
    const Entry* found = entry(name);
    if (found == nullptr) {
        return "there is no such script in the scripts folders";
    }
    return found->error.text;
}

std::uint32_t ScriptLibrary::errorLine(std::string_view name) const {
    const Entry* found = entry(name);
    return found != nullptr ? found->error.line : 0;
}

std::filesystem::path ScriptLibrary::path(std::string_view name) const {
    const Entry* found = entry(name);
    return found != nullptr ? found->path : std::filesystem::path();
}

void ScriptLibrary::setText(std::string_view name, std::string text) {
    if (Entry* found = entry(name)) {
        compile(*found, text);
        found->text = std::move(text);
        ++m_generation;
    }
}

void ScriptLibrary::clearText(std::string_view name) {
    if (Entry* found = entry(name); found != nullptr && found->text) {
        found->text.reset();
        found->writeTime.reset();
    }
}

} // namespace ct
