#pragma once

#include "croftech/editor/LuauHighlight.h"
#include "croftech/editor/TextBuffer.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ct {

// A line of code to mark: where the script fails to load, or a problem with how it fits the level.
struct CodeMark {
    std::uint32_t line = 0; // counting from 1
    bool error = true;      // red; otherwise amber
    std::string text;
};

// Draws a TextBuffer as Luau code and edits it with the mouse and keyboard: colours, line numbers,
// marked lines with their messages, the clipboard, undo, and Ctrl+F to find. It takes the keyboard
// when clicked, as ImGui's text fields do, and lets it go when something else is clicked.
class CodeEditor {
public:
    // Fills what is left of the window. Returns whether the text changed.
    bool draw(TextBuffer& buffer, std::span<const CodeMark> marks);
    // Whether it has the keyboard, or its find field does, so shortcuts elsewhere should wait.
    bool hasKeyboard() const { return m_active || m_findFocused; }
    // Puts the cursor at the start of the line (counting from 1), shows it and takes the keyboard.
    void goToLine(TextBuffer& buffer, std::uint32_t line);
    void focus() { m_takeFocus = true; }

private:
    void refresh(const TextBuffer& buffer);
    bool handleKeys(TextBuffer& buffer, std::uint32_t pageLines);
    void drawFindBar(TextBuffer& buffer);

    std::uint64_t m_version = ~std::uint64_t{0}; // of the text the caches below are for
    std::vector<std::vector<TokenSpan>> m_spans; // per line
    float m_widest = 0.0f;                       // pixels
    bool m_active = false;
    bool m_takeFocus = false;
    bool m_selecting = false; // dragging out a selection
    bool m_scrollToCursor = false;
    bool m_finding = false;
    bool m_focusFind = false;
    bool m_findFocused = false;
    std::string m_find;
};

} // namespace ct
