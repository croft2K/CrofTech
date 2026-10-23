#include "croftech/editor/TextBuffer.h"

#include <algorithm>

namespace ct {
namespace {

bool isContinuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

std::uint32_t nextCharacter(const std::string& line, std::uint32_t column) {
    const auto size = static_cast<std::uint32_t>(line.size());
    if (column >= size) {
        return size;
    }
    ++column;
    while (column < size && isContinuation(line[column])) {
        ++column;
    }
    return column;
}

std::uint32_t previousCharacter(const std::string& line, std::uint32_t column) {
    if (column == 0) {
        return 0;
    }
    --column;
    while (column > 0 && isContinuation(line[column])) {
        --column;
    }
    return column;
}

enum class CharClass { Space, Word, Other };

CharClass classOf(char c) {
    const auto byte = static_cast<unsigned char>(c);
    if (c == ' ' || c == '\t') {
        return CharClass::Space;
    }
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
        byte >= 0x80) {
        return CharClass::Word;
    }
    return CharClass::Other;
}

bool isSpace(char c) { return c == ' ' || c == '\t'; }

std::uint32_t leadingSpaces(const std::string& line) {
    std::uint32_t count = 0;
    while (count < line.size() && isSpace(line[count])) {
        ++count;
    }
    return count;
}

char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

// Whether a line, up to the cursor, opens a block whose body goes one indent deeper.
bool opensBlock(std::string_view text) {
    while (!text.empty() && isSpace(text.back())) {
        text.remove_suffix(1);
    }
    if (text.empty()) {
        return false;
    }
    if (text.back() == '{' || text.back() == '(') {
        return true;
    }
    for (const std::string_view word : {"then", "do", "else", "repeat"}) {
        if (text.ends_with(word) &&
            (text.size() == word.size() ||
             classOf(text[text.size() - word.size() - 1]) != CharClass::Word)) {
            return true;
        }
    }
    if (text.back() == ')') {
        const std::size_t function = text.find("function");
        return function != std::string_view::npos &&
               (function == 0 || classOf(text[function - 1]) != CharClass::Word);
    }
    return false;
}

} // namespace

TextBuffer::TextBuffer(std::string_view text) {
    std::string line;
    for (const char c : text) {
        if (c == '\n') {
            m_lines.push_back(std::move(line));
            line.clear();
        } else if (c != '\r') {
            line += c;
        }
    }
    m_lines.push_back(std::move(line));
}

std::string TextBuffer::text() const {
    std::string joined;
    for (std::size_t i = 0; i < m_lines.size(); ++i) {
        if (i > 0) {
            joined += '\n';
        }
        joined += m_lines[i];
    }
    return joined;
}

std::string TextBuffer::selectedText() const {
    return textBetween(selectionStart(), selectionEnd());
}

TextPosition TextBuffer::clamp(TextPosition position) const {
    position.line = std::min(position.line, static_cast<std::uint32_t>(m_lines.size() - 1));
    const std::string& line = m_lines[position.line];
    position.column = std::min(position.column, static_cast<std::uint32_t>(line.size()));
    while (position.column > 0 && position.column < line.size() &&
           isContinuation(line[position.column])) {
        --position.column;
    }
    return position;
}

TextPosition TextBuffer::advance(TextPosition start, std::string_view text) const {
    const std::size_t lastBreak = text.rfind('\n');
    if (lastBreak == std::string_view::npos) {
        return {start.line, start.column + static_cast<std::uint32_t>(text.size())};
    }
    const auto breaks = static_cast<std::uint32_t>(std::count(text.begin(), text.end(), '\n'));
    return {start.line + breaks, static_cast<std::uint32_t>(text.size() - lastBreak - 1)};
}

std::string TextBuffer::textBetween(TextPosition start, TextPosition end) const {
    if (start.line == end.line) {
        return m_lines[start.line].substr(start.column, end.column - start.column);
    }
    std::string text = m_lines[start.line].substr(start.column);
    for (std::uint32_t line = start.line + 1; line < end.line; ++line) {
        text += '\n';
        text += m_lines[line];
    }
    text += '\n';
    text += m_lines[end.line].substr(0, end.column);
    return text;
}

TextPosition TextBuffer::replaceRaw(TextPosition start, TextPosition end, std::string_view text) {
    const std::string after = m_lines[end.line].substr(end.column);
    std::vector<std::string> pieces(1, m_lines[start.line].substr(0, start.column));
    for (const char c : text) {
        if (c == '\n') {
            pieces.emplace_back();
        } else {
            pieces.back() += c;
        }
    }
    const TextPosition last{start.line + static_cast<std::uint32_t>(pieces.size() - 1),
                            static_cast<std::uint32_t>(pieces.back().size())};
    pieces.back() += after;
    m_lines.erase(m_lines.begin() + start.line, m_lines.begin() + end.line + 1);
    m_lines.insert(m_lines.begin() + start.line, pieces.begin(), pieces.end());
    ++m_version;
    return last;
}

void TextBuffer::begin(StepKind kind, bool mergeable) {
    const bool merge = mergeable && kind != StepKind::Other && !m_undo.empty() &&
                       m_undo.back().kind == kind && m_undo.back().cursorAfter == m_cursor &&
                       m_undo.back().anchorAfter == m_anchor && m_undo.back().id != m_savedState;
    if (!merge) {
        Step step;
        step.id = m_nextStep++;
        step.kind = kind;
        step.cursorBefore = m_cursor;
        step.anchorBefore = m_anchor;
        m_undo.push_back(std::move(step));
    }
}

void TextBuffer::replace(TextPosition start, TextPosition end, std::string_view text) {
    if (start == end && text.empty()) {
        return;
    }
    m_redo.clear();
    m_undo.back().edits.push_back({start, textBetween(start, end), std::string(text)});
    replaceRaw(start, end, text);
}

void TextBuffer::end() {
    if (m_undo.back().edits.empty()) {
        m_undo.pop_back();
        return;
    }
    m_undo.back().cursorAfter = m_cursor;
    m_undo.back().anchorAfter = m_anchor;
    m_goalColumn = characterColumn(m_cursor);
}

void TextBuffer::apply(const Step& step, bool reverse) {
    if (reverse) {
        for (auto edit = step.edits.rbegin(); edit != step.edits.rend(); ++edit) {
            replaceRaw(edit->start, advance(edit->start, edit->inserted), edit->removed);
        }
        m_cursor = step.cursorBefore;
        m_anchor = step.anchorBefore;
    } else {
        for (const Edit& edit : step.edits) {
            replaceRaw(edit.start, advance(edit.start, edit.removed), edit.inserted);
        }
        m_cursor = step.cursorAfter;
        m_anchor = step.anchorAfter;
    }
    m_goalColumn = characterColumn(m_cursor);
}

bool TextBuffer::undo() {
    if (m_undo.empty()) {
        return false;
    }
    Step step = std::move(m_undo.back());
    m_undo.pop_back();
    apply(step, true);
    m_redo.push_back(std::move(step));
    return true;
}

bool TextBuffer::redo() {
    if (m_redo.empty()) {
        return false;
    }
    Step step = std::move(m_redo.back());
    m_redo.pop_back();
    apply(step, false);
    m_undo.push_back(std::move(step));
    return true;
}

std::uint32_t TextBuffer::characterColumn(TextPosition position) const {
    const std::string& line = m_lines[position.line];
    std::uint32_t characters = 0;
    for (std::uint32_t i = 0; i < position.column && i < line.size(); ++i) {
        characters += isContinuation(line[i]) ? 0 : 1;
    }
    return characters;
}

TextPosition TextBuffer::atCharacterColumn(std::uint32_t line, std::uint32_t column) const {
    const std::string& text = m_lines[line];
    std::uint32_t byte = 0;
    for (std::uint32_t i = 0; i < column && byte < text.size(); ++i) {
        byte = nextCharacter(text, byte);
    }
    return {line, byte};
}

void TextBuffer::setCursor(TextPosition position, bool extend) {
    m_cursor = clamp(position);
    if (!extend) {
        m_anchor = m_cursor;
    }
    m_goalColumn = characterColumn(m_cursor);
}

void TextBuffer::move(TextMotion motion, bool extend, std::uint32_t pageLines) {
    const auto last = static_cast<std::uint32_t>(m_lines.size() - 1);
    TextPosition at = m_cursor;
    const std::string& line = m_lines[at.line];
    bool vertical = false;
    switch (motion) {
    case TextMotion::Left:
        if (!extend && hasSelection()) {
            at = selectionStart();
        } else if (at.column > 0) {
            at.column = previousCharacter(line, at.column);
        } else if (at.line > 0) {
            at = {at.line - 1, static_cast<std::uint32_t>(m_lines[at.line - 1].size())};
        }
        break;
    case TextMotion::Right:
        if (!extend && hasSelection()) {
            at = selectionEnd();
        } else if (at.column < line.size()) {
            at.column = nextCharacter(line, at.column);
        } else if (at.line < last) {
            at = {at.line + 1, 0};
        }
        break;
    case TextMotion::WordLeft:
        if (at.column == 0) {
            if (at.line > 0) {
                at = {at.line - 1, static_cast<std::uint32_t>(m_lines[at.line - 1].size())};
            }
            break;
        }
        while (at.column > 0 && isSpace(line[at.column - 1])) {
            --at.column;
        }
        if (at.column > 0) {
            const CharClass run = classOf(line[previousCharacter(line, at.column)]);
            while (at.column > 0 && classOf(line[previousCharacter(line, at.column)]) == run) {
                at.column = previousCharacter(line, at.column);
            }
        }
        break;
    case TextMotion::WordRight:
        if (at.column >= line.size()) {
            if (at.line < last) {
                at = {at.line + 1, 0};
            }
            break;
        }
        if (const CharClass run = classOf(line[at.column]); run != CharClass::Space) {
            while (at.column < line.size() && classOf(line[at.column]) == run) {
                at.column = nextCharacter(line, at.column);
            }
        }
        while (at.column < line.size() && isSpace(line[at.column])) {
            ++at.column;
        }
        break;
    case TextMotion::Up:
    case TextMotion::PageUp: {
        vertical = true;
        const std::uint32_t lines = motion == TextMotion::Up ? 1 : std::max(pageLines, 1u);
        at = at.line == 0 ? TextPosition{}
                          : atCharacterColumn(at.line - std::min(lines, at.line), m_goalColumn);
        break;
    }
    case TextMotion::Down:
    case TextMotion::PageDown: {
        vertical = true;
        const std::uint32_t lines = motion == TextMotion::Down ? 1 : std::max(pageLines, 1u);
        at = at.line == last ? TextPosition{last, static_cast<std::uint32_t>(m_lines[last].size())}
                             : atCharacterColumn(std::min(at.line + lines, last), m_goalColumn);
        break;
    }
    case TextMotion::LineStart: {
        const std::uint32_t first = leadingSpaces(line);
        at.column = at.column == first ? 0 : first;
        break;
    }
    case TextMotion::LineEnd:
        at.column = static_cast<std::uint32_t>(line.size());
        break;
    case TextMotion::DocumentStart:
        at = {};
        break;
    case TextMotion::DocumentEnd:
        at = {last, static_cast<std::uint32_t>(m_lines[last].size())};
        break;
    }
    m_cursor = at;
    if (!extend) {
        m_anchor = at;
    }
    if (!vertical) {
        m_goalColumn = characterColumn(m_cursor);
    }
}

void TextBuffer::selectAll() {
    m_anchor = {};
    m_cursor = {static_cast<std::uint32_t>(m_lines.size() - 1),
                static_cast<std::uint32_t>(m_lines.back().size())};
    m_goalColumn = characterColumn(m_cursor);
}

void TextBuffer::selectWord(TextPosition position) {
    position = clamp(position);
    const std::string& line = m_lines[position.line];
    if (line.empty()) {
        setCursor(position, false);
        return;
    }
    std::uint32_t at =
        position.column < line.size() ? position.column : previousCharacter(line, position.column);
    const CharClass run = classOf(line[at]);
    std::uint32_t start = at;
    while (start > 0 && classOf(line[previousCharacter(line, start)]) == run) {
        start = previousCharacter(line, start);
    }
    while (at < line.size() && classOf(line[at]) == run) {
        at = nextCharacter(line, at);
    }
    m_anchor = {position.line, start};
    m_cursor = {position.line, at};
    m_goalColumn = characterColumn(m_cursor);
}

void TextBuffer::type(std::string_view characters) {
    if (characters.empty()) {
        return;
    }
    // A word and the spaces after it are one step; the next word starts another.
    bool mergeable = !hasSelection();
    if (mergeable && !m_undo.empty() && !m_undo.back().edits.empty()) {
        const std::string& typed = m_undo.back().edits.back().inserted;
        mergeable = typed.empty() || !isSpace(typed.back()) || isSpace(characters.front());
    }
    begin(StepKind::Typing, mergeable);
    const TextPosition start = selectionStart();
    replace(start, selectionEnd(), characters);
    m_cursor = m_anchor = advance(start, characters);
    end();
}

void TextBuffer::paste(std::string_view text) {
    std::string clean;
    clean.reserve(text.size());
    for (const char c : text) {
        if (c != '\r') {
            clean += c;
        }
    }
    if (clean.empty() && !hasSelection()) {
        return;
    }
    begin(StepKind::Other, false);
    const TextPosition start = selectionStart();
    replace(start, selectionEnd(), clean);
    m_cursor = m_anchor = advance(start, clean);
    end();
}

void TextBuffer::newLine() {
    const TextPosition start = selectionStart();
    const std::string& line = m_lines[start.line];
    const std::uint32_t kept = std::min(leadingSpaces(line), start.column);
    std::string text = "\n" + line.substr(0, kept);
    if (opensBlock(std::string_view(line).substr(0, start.column))) {
        text.append(kIndent, ' ');
    }
    begin(StepKind::Other, false);
    replace(start, selectionEnd(), text);
    m_cursor = m_anchor = advance(start, text);
    end();
}

void TextBuffer::backspace() {
    begin(StepKind::Deleting, !hasSelection());
    if (hasSelection()) {
        const TextPosition start = selectionStart();
        replace(start, selectionEnd(), "");
        m_cursor = m_anchor = start;
    } else if (m_cursor.column > 0) {
        const std::string& line = m_lines[m_cursor.line];
        TextPosition start{m_cursor.line, previousCharacter(line, m_cursor.column)};
        // In the indentation, back to the previous stop.
        if (leadingSpaces(line) >= m_cursor.column && line[m_cursor.column - 1] == ' ') {
            start.column = (m_cursor.column - 1) / kIndent * kIndent;
        }
        replace(start, m_cursor, "");
        m_cursor = m_anchor = start;
    } else if (m_cursor.line > 0) {
        const TextPosition start{m_cursor.line - 1,
                                 static_cast<std::uint32_t>(m_lines[m_cursor.line - 1].size())};
        replace(start, m_cursor, "");
        m_cursor = m_anchor = start;
    }
    end();
}

void TextBuffer::deleteForward() {
    begin(StepKind::Other, false);
    if (hasSelection()) {
        const TextPosition start = selectionStart();
        replace(start, selectionEnd(), "");
        m_cursor = m_anchor = start;
    } else if (m_cursor.column < m_lines[m_cursor.line].size()) {
        replace(m_cursor, {m_cursor.line, nextCharacter(m_lines[m_cursor.line], m_cursor.column)},
                "");
    } else if (m_cursor.line + 1 < m_lines.size()) {
        replace(m_cursor, {m_cursor.line + 1, 0}, "");
    }
    end();
}

std::string TextBuffer::cut() {
    std::string text = selectedText();
    if (hasSelection()) {
        begin(StepKind::Other, false);
        const TextPosition start = selectionStart();
        replace(start, selectionEnd(), "");
        m_cursor = m_anchor = start;
        end();
    }
    return text;
}

void TextBuffer::indent() {
    const TextPosition start = selectionStart();
    const TextPosition finish = selectionEnd();
    begin(StepKind::Other, false);
    if (start.line == finish.line) {
        const std::string spaces(kIndent - characterColumn(start) % kIndent, ' ');
        replace(start, finish, spaces);
        m_cursor = m_anchor = advance(start, spaces);
        end();
        return;
    }
    // A selection ending at the start of a line leaves that line alone.
    const std::uint32_t lastLine = finish.column == 0 ? finish.line - 1 : finish.line;
    const std::string spaces(kIndent, ' ');
    for (std::uint32_t line = start.line; line <= lastLine; ++line) {
        replace({line, 0}, {line, 0}, spaces);
    }
    for (TextPosition* position : {&m_cursor, &m_anchor}) {
        if (position->line >= start.line && position->line <= lastLine && position->column > 0) {
            position->column += kIndent;
        }
    }
    end();
}

void TextBuffer::unindent() {
    const TextPosition start = selectionStart();
    const TextPosition finish = selectionEnd();
    const std::uint32_t lastLine =
        finish.line > start.line && finish.column == 0 ? finish.line - 1 : finish.line;
    begin(StepKind::Other, false);
    for (std::uint32_t line = start.line; line <= lastLine; ++line) {
        const std::string& text = m_lines[line];
        std::uint32_t removed = 0;
        if (!text.empty() && text[0] == '\t') {
            removed = 1;
        } else {
            while (removed < kIndent && removed < text.size() && text[removed] == ' ') {
                ++removed;
            }
        }
        if (removed == 0) {
            continue;
        }
        replace({line, 0}, {line, removed}, "");
        for (TextPosition* position : {&m_cursor, &m_anchor}) {
            if (position->line == line) {
                position->column -= std::min(removed, position->column);
            }
        }
    }
    end();
}

bool TextBuffer::findNext(std::string_view needle) {
    if (needle.empty()) {
        return false;
    }
    const auto matchesAt = [&](const std::string& line, std::size_t at) {
        if (at + needle.size() > line.size()) {
            return false;
        }
        for (std::size_t i = 0; i < needle.size(); ++i) {
            if (lower(line[at + i]) != lower(needle[i])) {
                return false;
            }
        }
        return true;
    };
    const TextPosition from = selectionEnd();
    const std::size_t count = m_lines.size();
    // Every line once, from the selection's, then round to its start again.
    for (std::size_t step = 0; step <= count; ++step) {
        const std::size_t index = (from.line + step) % count;
        const std::string& line = m_lines[index];
        const std::size_t first = step == 0 ? from.column : 0;
        const std::size_t limit = step == count ? from.column : line.size();
        for (std::size_t at = first; at <= limit && at < line.size(); ++at) {
            if (matchesAt(line, at)) {
                const auto row = static_cast<std::uint32_t>(index);
                m_anchor = {row, static_cast<std::uint32_t>(at)};
                m_cursor = {row, static_cast<std::uint32_t>(at + needle.size())};
                m_goalColumn = characterColumn(m_cursor);
                return true;
            }
        }
    }
    return false;
}

} // namespace ct
