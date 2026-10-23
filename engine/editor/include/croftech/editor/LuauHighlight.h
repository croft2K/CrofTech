#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace ct {

enum class TokenKind : std::uint8_t {
    Text,     // names, and anything not below
    Keyword,  // local, function, if, end...
    Constant, // nil, true, false
    Library,  // the globals scripts get: game's library tables and functions (math, print...)
    Number,
    String,
    Comment,
    Operator, // punctuation
};

// A run of a line's bytes of one kind.
struct TokenSpan {
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
    TokenKind kind = TokenKind::Text;
};

// Where a line starts: inside a long comment or string ([[ ]], [==[ ]==]) begun on a line before,
// or not. Each line's state is the end state of the line before.
struct HighlightState {
    TokenKind inside = TokenKind::Text; // Comment or String if inside one
    std::uint32_t level = 0;            // the number of '=' in its brackets

    friend constexpr bool operator==(const HighlightState&, const HighlightState&) = default;
};

// The spans of one line of Luau, for colouring code; spaces are left out. Returns the state the
// next line starts in.
HighlightState highlightLuauLine(std::string_view line, HighlightState state,
                                 std::vector<TokenSpan>& spans);

} // namespace ct
