#include "croftech/editor/LuauHighlight.h"

#include <algorithm>
#include <string>

namespace ct {
namespace {

// Luau's contextual keywords (type, export) are left as names, as they are mostly used as such.
constexpr std::string_view kKeywords[] = {
    "and", "break", "continue", "do", "else",   "elseif", "end",  "for",   "function", "if",
    "in",  "local", "not",      "or", "repeat", "return", "then", "until", "while",
};

constexpr std::string_view kConstants[] = {"false", "nil", "true"};

constexpr std::string_view kLibrary[] = {
    "assert",   "bit32",  "buffer", "coroutine",    "error",  "getmetatable", "ipairs",
    "math",     "next",   "pairs",  "pcall",        "print",  "rawequal",     "rawget",
    "rawlen",   "rawset", "select", "setmetatable", "string", "table",        "tonumber",
    "tostring", "type",   "typeof", "utf8",         "xpcall",
};

bool isNameStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
           static_cast<unsigned char>(c) >= 0x80;
}

bool isNameCharacter(char c) { return isNameStart(c) || (c >= '0' && c <= '9'); }

bool isDigit(char c) { return c >= '0' && c <= '9'; }

template <std::size_t N>
bool contains(const std::string_view (&words)[N], std::string_view word) {
    return std::find(std::begin(words), std::end(words), word) != std::end(words);
}

// At an opening long bracket ([[, [=[, ...): its level, and its length in bytes.
bool longBracket(std::string_view line, std::size_t at, std::uint32_t& level, std::size_t& length) {
    if (at >= line.size() || line[at] != '[') {
        return false;
    }
    std::size_t i = at + 1;
    while (i < line.size() && line[i] == '=') {
        ++i;
    }
    if (i >= line.size() || line[i] != '[') {
        return false;
    }
    level = static_cast<std::uint32_t>(i - at - 1);
    length = i - at + 1;
    return true;
}

// Where the long bracket of the level closes, from at: just past it, or npos.
std::size_t closeOf(std::string_view line, std::size_t at, std::uint32_t level) {
    std::string close = "]";
    close.append(level, '=');
    close += ']';
    const std::size_t found = line.find(close, at);
    return found == std::string_view::npos ? found : found + close.size();
}

} // namespace

HighlightState highlightLuauLine(std::string_view line, HighlightState state,
                                 std::vector<TokenSpan>& spans) {
    spans.clear();
    std::size_t at = 0;
    const auto add = [&](std::size_t begin, std::size_t end, TokenKind kind) {
        if (end > begin) {
            spans.push_back(
                {static_cast<std::uint32_t>(begin), static_cast<std::uint32_t>(end), kind});
        }
    };

    if (state.inside != TokenKind::Text) {
        const std::size_t close = closeOf(line, 0, state.level);
        if (close == std::string_view::npos) {
            add(0, line.size(), state.inside);
            return state;
        }
        add(0, close, state.inside);
        at = close;
        state = {};
    }

    while (at < line.size()) {
        const char c = line[at];
        if (c == ' ' || c == '\t') {
            ++at;
            continue;
        }
        std::uint32_t level = 0;
        std::size_t length = 0;
        if (c == '-' && at + 1 < line.size() && line[at + 1] == '-') {
            if (longBracket(line, at + 2, level, length)) {
                const std::size_t close = closeOf(line, at + 2 + length, level);
                if (close == std::string_view::npos) {
                    add(at, line.size(), TokenKind::Comment);
                    return {TokenKind::Comment, level};
                }
                add(at, close, TokenKind::Comment);
                at = close;
                continue;
            }
            add(at, line.size(), TokenKind::Comment);
            return {};
        }
        if (longBracket(line, at, level, length)) {
            const std::size_t close = closeOf(line, at + length, level);
            if (close == std::string_view::npos) {
                add(at, line.size(), TokenKind::String);
                return {TokenKind::String, level};
            }
            add(at, close, TokenKind::String);
            at = close;
            continue;
        }
        if (c == '"' || c == '\'' || c == '`') {
            std::size_t end = at + 1;
            while (end < line.size() && line[end] != c) {
                end += line[end] == '\\' ? 2 : 1;
            }
            end = std::min(end + 1, line.size());
            add(at, end, TokenKind::String);
            at = end;
            continue;
        }
        if (isDigit(c) || (c == '.' && at + 1 < line.size() && isDigit(line[at + 1]))) {
            std::size_t end = at + 1;
            while (end < line.size()) {
                const char d = line[end];
                const bool exponentSign =
                    (d == '+' || d == '-') && (line[end - 1] == 'e' || line[end - 1] == 'E') &&
                    !(line.size() > at + 1 && (line[at + 1] == 'x' || line[at + 1] == 'X'));
                if (!isNameCharacter(d) && d != '.' && !exponentSign) {
                    break;
                }
                ++end;
            }
            add(at, end, TokenKind::Number);
            at = end;
            continue;
        }
        if (isNameStart(c)) {
            std::size_t end = at + 1;
            while (end < line.size() && isNameCharacter(line[end])) {
                ++end;
            }
            const std::string_view word = line.substr(at, end - at);
            // A field (game.state) or method (game:fire) isn't a library name.
            const bool member = at > 0 && (line[at - 1] == '.' || line[at - 1] == ':') &&
                                !(at > 1 && line[at - 2] == '.');
            TokenKind kind = TokenKind::Text;
            if (contains(kKeywords, word)) {
                kind = TokenKind::Keyword;
            } else if (contains(kConstants, word)) {
                kind = TokenKind::Constant;
            } else if (!member && contains(kLibrary, word)) {
                kind = TokenKind::Library;
            }
            add(at, end, kind);
            at = end;
            continue;
        }
        add(at, at + 1, TokenKind::Operator);
        ++at;
    }
    return state;
}

} // namespace ct
