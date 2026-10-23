#pragma once

#include <compare>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ct {

// A place in a text: a line, and a byte offset into it, always at the start of a UTF-8 character.
struct TextPosition {
    std::uint32_t line = 0;
    std::uint32_t column = 0;

    friend constexpr auto operator<=>(const TextPosition&, const TextPosition&) = default;
};

enum class TextMotion : std::uint8_t {
    Left,
    Right,
    WordLeft,
    WordRight,
    Up,
    Down,
    LineStart, // the first character that isn't a space, or the very start if already there
    LineEnd,
    PageUp,
    PageDown,
    DocumentStart,
    DocumentEnd,
};

// The text a code editor edits: lines, a cursor and the selection it makes with its anchor, the
// edits a code editor offers, and undo. Every edit goes through one replacement, which undo
// reverses, so undo is exact. Characters typed one after another are one step of undo, broken
// after the space that ends a word; so are backspaces in a row.
class TextBuffer {
public:
    static constexpr std::uint32_t kIndent = 4; // spaces

    explicit TextBuffer(std::string_view text = {});

    // The lines joined with "\n".
    std::string text() const;
    std::size_t lineCount() const { return m_lines.size(); }
    const std::string& line(std::size_t index) const { return m_lines[index]; }

    TextPosition cursor() const { return m_cursor; }
    TextPosition anchor() const { return m_anchor; }
    bool hasSelection() const { return m_cursor != m_anchor; }
    TextPosition selectionStart() const { return m_cursor < m_anchor ? m_cursor : m_anchor; }
    TextPosition selectionEnd() const { return m_cursor < m_anchor ? m_anchor : m_cursor; }
    std::string selectedText() const;

    // Moves the cursor, held to the text and to the start of a character. Extending keeps the
    // anchor, as Shift does; otherwise the selection goes.
    void setCursor(TextPosition position, bool extend);
    void move(TextMotion motion, bool extend, std::uint32_t pageLines = 1);
    void selectAll();
    // The word (or run of spaces, or of other characters) at the position, as a double-click does.
    void selectWord(TextPosition position);

    // Each replaces the selection. Line ends in pasted text may be "\r\n".
    void type(std::string_view characters);
    void paste(std::string_view text);
    // Keeps the line's indentation, one deeper after a line that opens a block (ending in then,
    // do, else, repeat, function(...), "{" or "(").
    void newLine();
    void backspace();
    void deleteForward();
    // Removes the selection and returns it, for the clipboard.
    std::string cut();
    // Tab: with lines selected, indents them; otherwise inserts spaces to the next stop.
    void indent();
    // Shift+Tab: takes one indent off the selected lines, or the cursor's.
    void unindent();

    bool undo();
    bool redo();

    // Selects the next match of the text after the selection, ignoring ASCII case, going round to
    // the start. False if there is none. Matches don't cross lines.
    bool findNext(std::string_view needle);

    // Changes with every edit, undo and redo, for those who keep something worked out from the
    // text.
    std::uint64_t version() const { return m_version; }
    // Whether the text differs from when markSaved was last called (or it was made): undoing back
    // to that point counts as unmodified.
    bool modified() const { return currentState() != m_savedState; }
    void markSaved() { m_savedState = currentState(); }

private:
    struct Edit {
        TextPosition start;
        std::string removed;
        std::string inserted;
    };
    enum class StepKind : std::uint8_t { Other, Typing, Deleting };
    struct Step {
        std::uint64_t id = 0;
        StepKind kind = StepKind::Other;
        std::vector<Edit> edits;
        TextPosition cursorBefore;
        TextPosition anchorBefore;
        TextPosition cursorAfter;
        TextPosition anchorAfter;
    };

    std::uint64_t currentState() const { return m_undo.empty() ? 0 : m_undo.back().id; }
    TextPosition clamp(TextPosition position) const;
    TextPosition advance(TextPosition start, std::string_view text) const;
    std::string textBetween(TextPosition start, TextPosition end) const;
    // Replaces the text between the positions, without recording it; returns the end of what
    // went in.
    TextPosition replaceRaw(TextPosition start, TextPosition end, std::string_view text);
    // Starts a step, or carries on the last one if it is of this kind and the cursor hasn't
    // moved since, and replaces as part of it.
    void begin(StepKind kind, bool mergeable);
    void replace(TextPosition start, TextPosition end, std::string_view text);
    void end();
    void apply(const Step& step, bool reverse);
    // The column, in characters, the cursor is at.
    std::uint32_t characterColumn(TextPosition position) const;
    TextPosition atCharacterColumn(std::uint32_t line, std::uint32_t column) const;

    std::vector<std::string> m_lines;
    TextPosition m_cursor;
    TextPosition m_anchor;
    std::uint32_t m_goalColumn = 0; // in characters, kept by up and down
    std::vector<Step> m_undo;
    std::vector<Step> m_redo;
    std::uint64_t m_nextStep = 1;
    std::uint64_t m_savedState = 0;
    std::uint64_t m_version = 0;
};

} // namespace ct
