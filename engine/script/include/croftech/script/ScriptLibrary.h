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
// tools that show what scripts declare, such as the editor. refresh() looks for files added,
// removed and changed, and compiles again only what changed.
//
// A tool editing a script can give the library its unsaved text, which then stands for the
// file's until cleared: what the library says about the script is what is being typed.
class ScriptLibrary {
public:
    explicit ScriptLibrary(const ContentPaths& content) : m_content(content) {}

    // Returns whether anything changed. Programs and errors from before stay valid until then.
    bool refresh();
    // Changes whenever any script's program or error might have, for those who keep something
    // worked out from them.
    std::uint64_t generation() const { return m_generation; }

    // The names of the scripts found, sorted.
    std::vector<std::string_view> names() const;
    // Null if there is no such script or it didn't load (error says why).
    const ScriptProgram* find(std::string_view name) const;
    std::string_view error(std::string_view name) const;
    // The line of the error, counting from 1; 0 if it isn't at a line or there is none.
    std::uint32_t errorLine(std::string_view name) const;
    // The file the script was read from; empty if there is no such script.
    std::filesystem::path path(std::string_view name) const;

    // Compiles the text in place of the file's, at once. The script must exist.
    void setText(std::string_view name, std::string text);
    // Goes back to the file, read again on the next refresh.
    void clearText(std::string_view name);

private:
    struct Entry {
        std::string name;
        std::filesystem::path path;
        std::optional<std::filesystem::file_time_type> writeTime;
        std::uintmax_t size = 0;
        std::optional<std::string> text; // given in place of the file's
        std::unique_ptr<ScriptProgram> program;
        ScriptError error;
    };

    Entry* entry(std::string_view name);
    const Entry* entry(std::string_view name) const;
    static void compile(Entry& entry, std::string_view text);

    const ContentPaths& m_content;
    std::vector<Entry> m_entries; // sorted by name
    std::uint64_t m_generation = 0;
};

} // namespace ct
