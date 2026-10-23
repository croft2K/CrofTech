#include "croftech/editor/TextBuffer.h"

#include <doctest/doctest.h>

#include <ostream> // doctest prints string_view through operator<<
#include <string>

using ct::TextBuffer;
using ct::TextMotion;
using ct::TextPosition;

namespace {

TextPosition at(std::uint32_t line, std::uint32_t column) { return {line, column}; }

} // namespace

TEST_CASE("a text buffer splits lines and joins them again") {
    const TextBuffer buffer("one\r\ntwo\n\nfour");
    REQUIRE(buffer.lineCount() == 4);
    CHECK(buffer.line(1) == "two");
    CHECK(buffer.line(2).empty());
    CHECK(buffer.text() == "one\ntwo\n\nfour");
    CHECK(TextBuffer().lineCount() == 1);
}

TEST_CASE("typing replaces the selection, and a word and its space undo together") {
    TextBuffer buffer("local x");
    buffer.setCursor(at(0, 6), false);
    buffer.setCursor(at(0, 7), true);
    buffer.type("count");
    CHECK(buffer.text() == "local count");
    buffer.move(TextMotion::LineEnd, false);
    for (const char c : std::string(" = 1")) {
        buffer.type(std::string(1, c));
    }
    CHECK(buffer.text() == "local count = 1");
    CHECK(buffer.cursor() == at(0, 15));
    REQUIRE(buffer.undo());
    CHECK(buffer.text() == "local count = ");
    REQUIRE(buffer.undo());
    CHECK(buffer.text() == "local count ");
    REQUIRE(buffer.undo()); // "count" and the space after it
    CHECK(buffer.text() == "local x");
    CHECK(buffer.selectionStart() == at(0, 6)); // the selection comes back with it
    CHECK(buffer.selectionEnd() == at(0, 7));
    CHECK_FALSE(buffer.undo());
    REQUIRE(buffer.redo());
    REQUIRE(buffer.redo());
    CHECK(buffer.text() == "local count = ");
}

TEST_CASE("a new line keeps the indent, one deeper after a line that opens a block") {
    TextBuffer buffer("    if open then");
    buffer.move(TextMotion::LineEnd, false);
    buffer.newLine();
    CHECK(buffer.line(1) == "        ");
    CHECK(buffer.cursor() == at(1, 8));
    buffer.type("x = 1");
    buffer.newLine();
    CHECK(buffer.line(2) == "        ");
    for (const char* opener :
         {"press = function(game)", "local t = {", "for i = 1, 3 do", "else", "repeat"}) {
        TextBuffer block(opener);
        block.move(TextMotion::LineEnd, false);
        block.newLine();
        CAPTURE(opener);
        CHECK(block.line(1) == "    ");
    }
    TextBuffer plain("local done = undone");
    plain.move(TextMotion::LineEnd, false);
    plain.newLine();
    CHECK(plain.line(1).empty());
}

TEST_CASE("backspace in the indentation goes back a whole stop, and joins lines at the start") {
    TextBuffer buffer("a\n        b");
    buffer.setCursor(at(1, 8), false);
    buffer.backspace();
    CHECK(buffer.line(1) == "    b");
    buffer.backspace();
    CHECK(buffer.line(1) == "b");
    buffer.backspace();
    CHECK(buffer.text() == "ab");
    CHECK(buffer.cursor() == at(0, 1));
    // Backspaces in a row are one step of undo.
    REQUIRE(buffer.undo());
    CHECK(buffer.text() == "a\n        b");
    buffer.setCursor(at(0, 1), false);
    buffer.deleteForward();
    CHECK(buffer.text() == "a        b");
}

TEST_CASE("the cursor moves by character, word, line and page, keeping its column up and down") {
    TextBuffer buffer("game:fire(\"on_done\")\nx\n    longer line here");
    buffer.move(TextMotion::WordRight, false);
    CHECK(buffer.cursor() == at(0, 4));
    buffer.move(TextMotion::WordRight, false);
    CHECK(buffer.cursor() == at(0, 5));
    buffer.move(TextMotion::WordRight, false);
    CHECK(buffer.cursor() == at(0, 9));
    buffer.move(TextMotion::WordLeft, false);
    CHECK(buffer.cursor() == at(0, 5));
    buffer.setCursor(at(0, 12), false);
    buffer.move(TextMotion::Down, false);
    CHECK(buffer.cursor() == at(1, 1));
    buffer.move(TextMotion::Down, false);
    CHECK(buffer.cursor() == at(2, 12)); // the column it started from
    buffer.move(TextMotion::LineStart, false);
    CHECK(buffer.cursor() == at(2, 4)); // past the indentation first...
    buffer.move(TextMotion::LineStart, false);
    CHECK(buffer.cursor() == at(2, 0)); // ...then to the very start
    buffer.move(TextMotion::PageUp, true, 10);
    CHECK(buffer.cursor() == at(0, 0));
    CHECK(buffer.anchor() == at(2, 0));
    buffer.move(TextMotion::Right, false); // collapses the selection to its end
    CHECK(buffer.cursor() == at(2, 0));
    buffer.move(TextMotion::Left, false);
    CHECK(buffer.cursor() == at(1, 1));
    buffer.move(TextMotion::DocumentEnd, false);
    CHECK(buffer.cursor() == at(2, 20));
}

TEST_CASE("the cursor steps over whole UTF-8 characters") {
    TextBuffer buffer("a\xC3\xA9z"); // a, e-acute, z
    buffer.move(TextMotion::Right, false);
    buffer.move(TextMotion::Right, false);
    CHECK(buffer.cursor() == at(0, 3));
    buffer.backspace();
    CHECK(buffer.text() == "az");
    buffer.setCursor(at(0, 2), false);
    buffer.paste("\xC3\xA9");
    buffer.setCursor(at(0, 3), false); // inside the character: held to its start
    CHECK(buffer.cursor() == at(0, 2));
}

TEST_CASE("cut, paste and selecting words and everything") {
    TextBuffer buffer("local alpha = beta");
    buffer.selectWord(at(0, 8));
    CHECK(buffer.selectedText() == "alpha");
    const std::string cut = buffer.cut();
    CHECK(cut == "alpha");
    CHECK(buffer.text() == "local  = beta");
    buffer.paste("one\r\ntwo");
    CHECK(buffer.text() == "local one\ntwo = beta");
    CHECK(buffer.cursor() == at(1, 3));
    buffer.selectAll();
    CHECK(buffer.selectedText() == buffer.text());
}

TEST_CASE("tab indents the selected lines, or inserts spaces to the next stop") {
    TextBuffer buffer("a\nb\nc");
    buffer.setCursor(at(0, 0), false);
    buffer.setCursor(at(2, 0), true); // ending at the start of c leaves c alone
    buffer.indent();
    CHECK(buffer.text() == "    a\n    b\nc");
    buffer.unindent();
    CHECK(buffer.text() == "a\nb\nc");
    TextBuffer one("ab");
    one.setCursor(at(0, 1), false);
    one.indent();
    CHECK(one.text() == "a   b");
    one.setCursor(at(0, 4), false);
    one.unindent(); // nothing to take off the front of a line that starts with text
    CHECK(one.text() == "a   b");
}

TEST_CASE("find selects the next match, ignoring case, and goes round to the start") {
    TextBuffer buffer("Door\nthe door\nnothing");
    CHECK(buffer.findNext("door"));
    CHECK(buffer.selectionStart() == at(0, 0));
    CHECK(buffer.findNext("door"));
    CHECK(buffer.selectionStart() == at(1, 4));
    CHECK(buffer.findNext("DOOR"));
    CHECK(buffer.selectionStart() == at(0, 0));
    CHECK_FALSE(buffer.findNext("window"));
}

TEST_CASE("a buffer knows whether it differs from what was saved, even through undo") {
    TextBuffer buffer("x");
    CHECK_FALSE(buffer.modified());
    buffer.move(TextMotion::LineEnd, false);
    buffer.type("y");
    CHECK(buffer.modified());
    buffer.markSaved();
    CHECK_FALSE(buffer.modified());
    buffer.type("z"); // not merged into the saved step
    CHECK(buffer.modified());
    buffer.undo();
    CHECK_FALSE(buffer.modified());
    CHECK(buffer.text() == "xy");
    buffer.undo();
    CHECK(buffer.modified());
    const std::uint64_t version = buffer.version();
    buffer.redo();
    CHECK(buffer.version() != version);
    CHECK_FALSE(buffer.modified());
}
