#include "croftech/editor/LuauHighlight.h"

#include <doctest/doctest.h>

#include <string>
#include <string_view>
#include <vector>

using ct::HighlightState;
using ct::TokenKind;
using ct::TokenSpan;

namespace {

// Each span's text and kind, for comparing.
std::vector<std::pair<std::string, TokenKind>>
tokens(std::string_view line, HighlightState state = {}, HighlightState* next = nullptr) {
    std::vector<TokenSpan> spans;
    const HighlightState after = ct::highlightLuauLine(line, state, spans);
    if (next != nullptr) {
        *next = after;
    }
    std::vector<std::pair<std::string, TokenKind>> result;
    for (const TokenSpan& span : spans) {
        result.emplace_back(std::string(line.substr(span.begin, span.end - span.begin)), span.kind);
    }
    return result;
}

} // namespace

TEST_CASE("Luau lines are split into keywords, names, numbers, strings and operators") {
    const auto found = tokens("local n = math.floor(game.state.count) + 0x1F -- done");
    const std::vector<std::pair<std::string, TokenKind>> expected{
        {"local", TokenKind::Keyword},   {"n", TokenKind::Text},     {"=", TokenKind::Operator},
        {"math", TokenKind::Library},    {".", TokenKind::Operator}, {"floor", TokenKind::Text},
        {"(", TokenKind::Operator},      {"game", TokenKind::Text},  {".", TokenKind::Operator},
        {"state", TokenKind::Text},      {".", TokenKind::Operator}, {"count", TokenKind::Text},
        {")", TokenKind::Operator},      {"+", TokenKind::Operator}, {"0x1F", TokenKind::Number},
        {"-- done", TokenKind::Comment},
    };
    CHECK(found == expected);
}

TEST_CASE("strings keep their escapes, and numbers their exponents and decimals") {
    const auto found = tokens(R"(print("say \"hi\"", 'x', 1.5e-3, nil))");
    REQUIRE(found.size() == 10);
    CHECK(found[0] == std::pair<std::string, TokenKind>{"print", TokenKind::Library});
    CHECK(found[2].first == R"("say \"hi\"")");
    CHECK(found[2].second == TokenKind::String);
    CHECK(found[4].first == "'x'");
    CHECK(found[6] == std::pair<std::string, TokenKind>{"1.5e-3", TokenKind::Number});
    CHECK(found[8] == std::pair<std::string, TokenKind>{"nil", TokenKind::Constant});
}

TEST_CASE("long comments and strings carry on to the lines after") {
    HighlightState state;
    auto found = tokens("x = 1 --[==[ starts here", {}, &state);
    CHECK(found.back() ==
          std::pair<std::string, TokenKind>{"--[==[ starts here", TokenKind::Comment});
    CHECK(state == HighlightState{TokenKind::Comment, 2});
    found = tokens("still ]] inside", state, &state);
    REQUIRE(found.size() == 1);
    CHECK(found[0].second == TokenKind::Comment);
    found = tokens("ends ]==] end", state, &state);
    REQUIRE(found.size() == 2);
    CHECK(found[0] == std::pair<std::string, TokenKind>{"ends ]==]", TokenKind::Comment});
    CHECK(found[1] == std::pair<std::string, TokenKind>{"end", TokenKind::Keyword});
    CHECK(state == HighlightState{});

    found = tokens("local s = [[text", {}, &state);
    CHECK(state == HighlightState{TokenKind::String, 0});
}
