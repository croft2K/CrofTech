#pragma once

#include "croftech/core/Content.h"
#include "croftech/script/ScriptProgram.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ct {

// Every script in the content layers' scripts folders (a later layer's file wins), compiled, for
// tools that show what scripts declare, such as the editor's inspector. refresh() looks for files
// added, removed and changed, and compiles again only what changed.
class ScriptLibrary {
public:
    explicit ScriptLibrary(const ContentPaths& content) : m_content(content) {}

    // Returns whether anything changed. Programs and errors from before stay valid until then.
    bool refresh();

    // The names of the scripts found, sorted.
    std::vector<std::string_view> names() const;
    // Null if there is no such script or it didn't load (error says why).
    const ScriptProgram* find(std::string_view name) const;
    std::string_view error(std::string_view name) const;

private:
    struct Entry {
        std::string name;
        std::filesystem::path path;
        std::optional<std::filesystem::file_time_type> writeTime;
        std::uintmax_t size = 0;
        std::unique_ptr<ScriptProgram> program;
        std::string error;
    };

    const Entry* entry(std::string_view name) const;

    const ContentPaths& m_content;
    std::vector<Entry> m_entries; // sorted by name
};

} // namespace ct
