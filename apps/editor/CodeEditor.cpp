#include "CodeEditor.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace ct {
namespace {

constexpr ImU32 kBackground = IM_COL32(24, 26, 31, 255);
constexpr ImU32 kGutterBackground = IM_COL32(30, 32, 38, 255);
constexpr ImU32 kLineNumber = IM_COL32(110, 116, 128, 255);
constexpr ImU32 kCurrentLineNumber = IM_COL32(200, 204, 212, 255);
constexpr ImU32 kSelection = IM_COL32(60, 90, 140, 170);
constexpr ImU32 kCursor = IM_COL32(235, 235, 240, 255);
constexpr ImU32 kErrorLine = IM_COL32(200, 50, 40, 55);
constexpr ImU32 kWarningLine = IM_COL32(210, 150, 30, 40);
constexpr ImU32 kErrorText = IM_COL32(255, 120, 100, 255);
constexpr ImU32 kWarningText = IM_COL32(235, 180, 80, 255);

ImU32 tokenColour(TokenKind kind) {
    switch (kind) {
    case TokenKind::Keyword:
        return IM_COL32(198, 120, 221, 255);
    case TokenKind::Constant:
    case TokenKind::Number:
        return IM_COL32(209, 154, 102, 255);
    case TokenKind::Library:
        return IM_COL32(97, 175, 239, 255);
    case TokenKind::String:
        return IM_COL32(152, 195, 121, 255);
    case TokenKind::Comment:
        return IM_COL32(120, 128, 140, 255);
    case TokenKind::Operator:
        return IM_COL32(171, 178, 191, 255);
    case TokenKind::Text:
        break;
    }
    return IM_COL32(220, 223, 228, 255);
}

float textWidth(std::string_view text) {
    return text.empty() ? 0.0f : ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
}

bool isContinuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

// The column of the line nearest x pixels from its start.
std::uint32_t columnAt(const std::string& line, float x) {
    float at = 0.0f;
    std::size_t column = 0;
    while (column < line.size()) {
        std::size_t next = column + 1;
        while (next < line.size() && isContinuation(line[next])) {
            ++next;
        }
        const float width = textWidth(std::string_view(line).substr(column, next - column));
        if (x < at + width * 0.5f) {
            break;
        }
        at += width;
        column = next;
    }
    return static_cast<std::uint32_t>(column);
}

} // namespace

void CodeEditor::refresh(const TextBuffer& buffer) {
    if (m_version == buffer.version()) {
        return;
    }
    m_version = buffer.version();
    m_spans.resize(buffer.lineCount());
    HighlightState state;
    m_widest = 0.0f;
    for (std::size_t i = 0; i < buffer.lineCount(); ++i) {
        state = highlightLuauLine(buffer.line(i), state, m_spans[i]);
        m_widest = std::max(m_widest, textWidth(buffer.line(i)));
    }
}

void CodeEditor::goToLine(TextBuffer& buffer, std::uint32_t line) {
    buffer.setCursor({line > 0 ? line - 1 : 0, 0}, false);
    buffer.move(TextMotion::LineStart, false);
    m_scrollToCursor = true;
    m_takeFocus = true;
}

void CodeEditor::drawFindBar(TextBuffer& buffer) {
    if (m_focusFind) {
        ImGui::SetKeyboardFocusHere();
        m_focusFind = false;
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
    if (ImGui::InputTextWithHint("##find", "find", &m_find, ImGuiInputTextFlags_EnterReturnsTrue)) {
        buffer.findNext(m_find);
        m_scrollToCursor = true;
        m_focusFind = true; // Enter again finds the next
    }
    m_findFocused = ImGui::IsItemActive();
    if (m_findFocused && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        m_finding = false;
        m_takeFocus = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Next")) {
        buffer.findNext(m_find);
        m_scrollToCursor = true;
    }
    ImGui::SetItemTooltip("F3, or Enter in the field");
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
        m_finding = false;
        m_findFocused = false;
        m_takeFocus = true;
    }
}

bool CodeEditor::handleKeys(TextBuffer& buffer, std::uint32_t pageLines) {
    const ImGuiIO& io = ImGui::GetIO();
    const bool ctrl = io.KeyCtrl;
    const bool shift = io.KeyShift;
    const auto pressed = [](ImGuiKey key) { return ImGui::IsKeyPressed(key, true); };
    const std::uint64_t before = buffer.version();
    bool moved = true;
    if (pressed(ImGuiKey_LeftArrow)) {
        buffer.move(ctrl ? TextMotion::WordLeft : TextMotion::Left, shift);
    } else if (pressed(ImGuiKey_RightArrow)) {
        buffer.move(ctrl ? TextMotion::WordRight : TextMotion::Right, shift);
    } else if (pressed(ImGuiKey_UpArrow)) {
        buffer.move(TextMotion::Up, shift);
    } else if (pressed(ImGuiKey_DownArrow)) {
        buffer.move(TextMotion::Down, shift);
    } else if (pressed(ImGuiKey_Home)) {
        buffer.move(ctrl ? TextMotion::DocumentStart : TextMotion::LineStart, shift);
    } else if (pressed(ImGuiKey_End)) {
        buffer.move(ctrl ? TextMotion::DocumentEnd : TextMotion::LineEnd, shift);
    } else if (pressed(ImGuiKey_PageUp)) {
        buffer.move(TextMotion::PageUp, shift, pageLines);
    } else if (pressed(ImGuiKey_PageDown)) {
        buffer.move(TextMotion::PageDown, shift, pageLines);
    } else if (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter)) {
        buffer.newLine();
    } else if (pressed(ImGuiKey_Backspace)) {
        buffer.backspace();
    } else if (pressed(ImGuiKey_Delete)) {
        buffer.deleteForward();
    } else if (pressed(ImGuiKey_Tab)) {
        shift ? buffer.unindent() : buffer.indent();
    } else if (ctrl && pressed(ImGuiKey_A)) {
        buffer.selectAll();
    } else if (ctrl && pressed(ImGuiKey_C)) {
        if (buffer.hasSelection()) {
            ImGui::SetClipboardText(buffer.selectedText().c_str());
        }
    } else if (ctrl && pressed(ImGuiKey_X)) {
        if (buffer.hasSelection()) {
            ImGui::SetClipboardText(buffer.cut().c_str());
        }
    } else if (ctrl && pressed(ImGuiKey_V)) {
        if (const char* text = ImGui::GetClipboardText()) {
            buffer.paste(text);
        }
    } else if (ctrl && pressed(ImGuiKey_Z)) {
        shift ? buffer.redo() : buffer.undo();
    } else if (ctrl && pressed(ImGuiKey_Y)) {
        buffer.redo();
    } else if (ctrl && pressed(ImGuiKey_F)) {
        // A one-line selection is what to find.
        if (buffer.hasSelection() && buffer.selectionStart().line == buffer.selectionEnd().line) {
            m_find = buffer.selectedText();
        }
        m_finding = true;
        m_focusFind = true;
        moved = false;
    } else if (pressed(ImGuiKey_F3)) {
        buffer.findNext(m_find);
    } else {
        moved = false;
    }

    // Typed characters, as UTF-8. Control characters come as keys instead.
    std::string typed;
    for (const ImWchar c : io.InputQueueCharacters) {
        if (c >= 32 && c != 127) {
            char utf8[5] = {};
            ImTextCharToUtf8(utf8, c);
            typed += utf8;
        }
    }
    ImGui::GetIO().InputQueueCharacters.resize(0);
    if (!typed.empty()) {
        buffer.type(typed);
        moved = true;
    }
    m_scrollToCursor = m_scrollToCursor || moved;
    return buffer.version() != before;
}

bool CodeEditor::draw(TextBuffer& buffer, std::span<const CodeMark> marks) {
    ImGuiContext& g = *ImGui::GetCurrentContext();
    const ImGuiIO& io = ImGui::GetIO();
    if (m_finding) {
        drawFindBar(buffer);
    } else {
        m_findFocused = false;
    }
    refresh(buffer);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, kBackground);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("code", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoNavInputs |
                          ImGuiWindowFlags_NoMove);
    ImGui::PopStyleVar();
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImGuiID id = window->GetID("text");
    const float lineHeight = std::floor(ImGui::GetTextLineHeight() + 2.0f);
    const float charWidth = textWidth("0");
    const std::string lastNumber = std::to_string(buffer.lineCount());
    const float gutter =
        charWidth * static_cast<float>(std::max<std::size_t>(lastNumber.size(), 3) + 3);
    const ImRect inner = window->InnerRect;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const auto lineCount = static_cast<std::uint32_t>(buffer.lineCount());
    // Room to scroll the last line up to the middle.
    ImGui::Dummy(ImVec2(gutter + m_widest + charWidth * 8.0f,
                        static_cast<float>(lineCount) * lineHeight + inner.GetHeight() * 0.5f));
    const auto textX = [&](std::uint32_t line, std::uint32_t column) {
        return textWidth(std::string_view(buffer.line(line)).substr(0, column));
    };
    const auto positionAt = [&](ImVec2 point) {
        const float row = (point.y - origin.y) / lineHeight;
        const auto line =
            static_cast<std::uint32_t>(std::clamp(row, 0.0f, static_cast<float>(lineCount - 1)));
        return TextPosition{line, columnAt(buffer.line(line), point.x - origin.x - gutter)};
    };

    // The mouse: a click takes the keyboard and places the cursor, Shift-click extends, a
    // double-click selects a word, and dragging selects.
    const bool overWindow = ImGui::IsWindowHovered();
    const bool overText = overWindow && inner.Contains(io.MousePos);
    if (overText) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
    }
    if ((overText && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) || m_takeFocus) {
        ImGui::SetActiveID(id, window);
        ImGui::SetFocusID(id, window);
        ImGui::FocusWindow(window);
        if (!m_takeFocus) {
            const TextPosition at = positionAt(io.MousePos);
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                buffer.selectWord(at);
            } else {
                buffer.setCursor(at, io.KeyShift);
                m_selecting = true;
            }
        }
        m_takeFocus = false;
    } else if (g.ActiveId == id && !overWindow && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ImGui::ClearActiveID();
    }
    if (m_selecting) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            buffer.setCursor(positionAt(io.MousePos), true);
            m_scrollToCursor = !inner.Contains(io.MousePos);
        } else {
            m_selecting = false;
        }
    }

    m_active = g.ActiveId == id;
    bool changed = false;
    if (m_active) {
        ImGui::KeepAliveID(id);
        ImGui::SetActiveIdUsingAllKeyboardKeys();
        const auto pageLines = static_cast<std::uint32_t>(inner.GetHeight() / lineHeight);
        changed = handleKeys(buffer, std::max(pageLines, 1u));
        refresh(buffer);
    }

    const TextPosition cursor = buffer.cursor();
    if (m_scrollToCursor) {
        const float y = static_cast<float>(cursor.line) * lineHeight;
        const float x = textX(cursor.line, cursor.column);
        const float height = inner.GetHeight();
        const float width = inner.GetWidth() - gutter;
        if (y < ImGui::GetScrollY()) {
            ImGui::SetScrollY(y);
        } else if (y + lineHeight > ImGui::GetScrollY() + height) {
            ImGui::SetScrollY(y + lineHeight - height);
        }
        if (x < ImGui::GetScrollX()) {
            ImGui::SetScrollX(std::max(0.0f, x - charWidth * 4.0f));
        } else if (x > ImGui::GetScrollX() + width - charWidth * 2.0f) {
            ImGui::SetScrollX(x - width + charWidth * 4.0f);
        }
        m_scrollToCursor = false;
    }
    if (m_active) {
        // Asks the platform for text input (SDL sends characters only then), with the IME at the
        // cursor.
        g.PlatformImeData.WantVisible = true;
        g.PlatformImeData.WantTextInput = true;
        g.PlatformImeData.InputPos =
            ImVec2(origin.x + gutter + textX(cursor.line, cursor.column),
                   origin.y + static_cast<float>(cursor.line) * lineHeight);
        g.PlatformImeData.InputLineHeight = lineHeight;
        g.PlatformImeData.ViewportId = window->Viewport->ID;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const auto first = static_cast<std::uint32_t>(
        std::max(0.0f, std::floor((inner.Min.y - origin.y) / lineHeight)));
    const std::uint32_t last =
        std::min(lineCount, first + static_cast<std::uint32_t>(inner.GetHeight() / lineHeight) + 2);
    const float textLeft = inner.Min.x + gutter;
    const auto lineTop = [&](std::uint32_t line) {
        return origin.y + static_cast<float>(line) * lineHeight;
    };
    const auto markOf = [&](std::uint32_t line) -> const CodeMark* {
        // Errors win over warnings on the same line.
        const CodeMark* found = nullptr;
        for (const CodeMark& mark : marks) {
            if (mark.line == line + 1 && (found == nullptr || mark.error)) {
                found = &mark;
            }
        }
        return found;
    };

    drawList->PushClipRect(ImVec2(textLeft, inner.Min.y), inner.Max, true);
    const TextPosition selectionStart = buffer.selectionStart();
    const TextPosition selectionEnd = buffer.selectionEnd();
    for (std::uint32_t line = first; line < last; ++line) {
        const float top = lineTop(line);
        const std::string& text = buffer.line(line);
        if (const CodeMark* mark = markOf(line)) {
            drawList->AddRectFilled(ImVec2(inner.Min.x, top), ImVec2(inner.Max.x, top + lineHeight),
                                    mark->error ? kErrorLine : kWarningLine);
        }
        if (buffer.hasSelection() && line >= selectionStart.line && line <= selectionEnd.line) {
            const float from =
                line == selectionStart.line ? textX(line, selectionStart.column) : 0.0f;
            const float to = line == selectionEnd.line ? textX(line, selectionEnd.column)
                                                       : textWidth(text) + charWidth;
            drawList->AddRectFilled(ImVec2(origin.x + gutter + from, top),
                                    ImVec2(origin.x + gutter + to, top + lineHeight), kSelection);
        }
        float x = origin.x + gutter;
        std::uint32_t drawn = 0;
        for (const TokenSpan& span : m_spans[line]) {
            x += textWidth(std::string_view(text).substr(drawn, span.begin - drawn));
            const char* begin = text.data() + span.begin;
            const char* end = text.data() + span.end;
            drawList->AddText(ImVec2(x, top + 1.0f), tokenColour(span.kind), begin, end);
            x += textWidth(std::string_view(begin, span.end - span.begin));
            drawn = span.end;
        }
        if (const CodeMark* mark = markOf(line)) {
            const float lineEnd = origin.x + gutter + textWidth(text);
            drawList->AddText(ImVec2(lineEnd + charWidth * 3.0f, top + 1.0f),
                              mark->error ? kErrorText : kWarningText, mark->text.c_str());
        }
    }
    if (m_active && std::fmod(ImGui::GetTime(), 1.0) < 0.6) {
        const float x = origin.x + gutter + textX(cursor.line, cursor.column);
        const float top = lineTop(cursor.line);
        drawList->AddLine(ImVec2(x, top), ImVec2(x, top + lineHeight), kCursor, 1.5f);
    }
    drawList->PopClipRect();

    // The gutter stays put while the text scrolls sideways.
    drawList->PushClipRect(inner.Min, ImVec2(textLeft, inner.Max.y), true);
    drawList->AddRectFilled(inner.Min, ImVec2(textLeft - charWidth, inner.Max.y),
                            kGutterBackground);
    for (std::uint32_t line = first; line < last; ++line) {
        const float top = lineTop(line);
        const std::string number = std::to_string(line + 1);
        const float numberX = textLeft - charWidth * 2.0f - textWidth(number);
        drawList->AddText(ImVec2(numberX, top + 1.0f),
                          line == cursor.line ? kCurrentLineNumber : kLineNumber, number.c_str());
        if (const CodeMark* mark = markOf(line)) {
            const ImVec2 centre(inner.Min.x + charWidth, top + lineHeight * 0.5f);
            drawList->AddCircleFilled(centre, charWidth * 0.4f,
                                      mark->error ? kErrorText : kWarningText);
            if (overWindow && io.MousePos.y >= top && io.MousePos.y < top + lineHeight &&
                io.MousePos.x < textLeft) {
                ImGui::SetTooltip("%s", mark->text.c_str());
            }
        }
    }
    drawList->PopClipRect();

    ImGui::EndChild();
    ImGui::PopStyleColor();
    return changed;
}

} // namespace ct
