// g++ -std=c++17 -I src tests/textedit_test.cpp -o textedit_test && ./textedit_test
#include "textedit.h"
#include <cstdio>
using namespace textedit;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
#define EQ(a, b) do { std::string x_ = (a), y_ = (b); if (x_ != y_) { printf("FAIL line %d: '%s' != '%s'\n", __LINE__, x_.c_str(), y_.c_str()); ++fails; } } while (0)
static void type(std::string& t, State& s, const char* txt, size_t max = 1000) { for (const char* p = txt; *p; ++p) insert(t, s, std::string(1, *p), max, 1); }

int main() {
    // ---- typing, caret in the middle, selection replace
    { std::string t; State s; type(t, s, "hello"); EQ(t, "hello"); CHECK(s.caret == 5);
      moveTo(t, s, 2, false); type(t, s, "XY"); EQ(t, "heXYllo"); CHECK(s.caret == 4);
      moveTo(t, s, 1, false); moveTo(t, s, 4, true); CHECK(s.hasSel()); EQ(selected(t, s), "eXY");
      insert(t, s, "-", 100, 1); EQ(t, "h-llo"); CHECK(s.caret == 2 && !s.hasSel()); }
    // ---- backspace / delete (single, word, selection)
    { std::string t = "one two three"; State s; moveTo(t, s, t.size(), false);
      backspace(t, s, true); EQ(t, "one two "); backspace(t, s, true); EQ(t, "one ");
      backspace(t, s, false); EQ(t, "one"); moveTo(t, s, 0, false); del(t, s, false); EQ(t, "ne"); del(t, s, true); EQ(t, "");
      CHECK(!backspace(t, s, false)); CHECK(!del(t, s, false)); }
    { std::string t = "hello world"; State s; moveTo(t, s, 0, false); moveTo(t, s, 5, true); backspace(t, s, false); EQ(t, " world"); CHECK(s.caret == 0); }
    // ---- word moves
    { std::string t = "foo  bar.baz qux"; CHECK(wordRight(t, 0) == 5); CHECK(wordRight(t, 5) == 8); CHECK(wordRight(t, 8) == 9); CHECK(wordRight(t, 9) == 13); CHECK(wordRight(t, 13) == 16); CHECK(wordRight(t, 16) == 16);
      CHECK(wordLeft(t, 16) == 13); CHECK(wordLeft(t, 13) == 9); CHECK(wordLeft(t, 9) == 8); CHECK(wordLeft(t, 8) == 5); CHECK(wordLeft(t, 5) == 0); CHECK(wordLeft(t, 0) == 0); }
    // ---- UTF-8: caret never lands inside a character; 2-, 3- and 4-byte characters
    { std::string t = "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80z";   // a é € 😀 z  (1+2+3+4+1 bytes)
      State s; CHECK(nextChar(t, 0) == 1); CHECK(nextChar(t, 1) == 3); CHECK(nextChar(t, 3) == 6); CHECK(nextChar(t, 6) == 10); CHECK(nextChar(t, 10) == 11); CHECK(nextChar(t, 11) == 11);
      CHECK(prevChar(t, 11) == 10); CHECK(prevChar(t, 10) == 6); CHECK(prevChar(t, 6) == 3); CHECK(prevChar(t, 3) == 1); CHECK(prevChar(t, 1) == 0); CHECK(prevChar(t, 0) == 0);
      CHECK(prevChar(t, 8) == 3); CHECK(nextChar(t, 7) == 10); CHECK(snap(t, 8) == 6); CHECK(snap(t, 2) == 1);
      moveTo(t, s, 8, false); CHECK(s.caret == 6);
      moveTo(t, s, 10, false); backspace(t, s, false); EQ(t, "a\xC3\xA9\xE2\x82\xAC" "z"); CHECK(s.caret == 6);
      moveTo(t, s, 1, false); del(t, s, false); EQ(t, "a\xE2\x82\xAC" "z"); }
    // ---- maximum length (bytes), never splits a character, replacing a selection frees room
    { std::string t; State s; insert(t, s, "abcd", 4, 0); EQ(t, "abcd"); CHECK(!insert(t, s, "e", 4, 1)); EQ(t, "abcd");
      moveTo(t, s, 1, false); moveTo(t, s, 3, true); CHECK(insert(t, s, "XYZ", 4, 0)); EQ(t, "aXYd"); }
    { std::string t; State s; insert(t, s, "ab\xC3\xA9", 3, 0); EQ(t, "ab"); }                      // é needs 2 bytes, only 1 left
    { std::string t; State s; insert(t, s, "\xF0\x9F\x98\x80\xF0\x9F\x98\x80", 6, 0); EQ(t, "\xF0\x9F\x98\x80"); }
    // ---- undo / redo; a run of typing is one step
    { std::string t; State s; type(t, s, "hello"); moveTo(t, s, t.size(), false); type(t, s, " world"); EQ(t, "hello world");
      CHECK(undo(t, s)); EQ(t, "hello"); CHECK(undo(t, s)); EQ(t, ""); CHECK(!undo(t, s));
      CHECK(redo(t, s)); EQ(t, "hello"); CHECK(redo(t, s)); EQ(t, "hello world"); CHECK(!redo(t, s));
      undo(t, s); type(t, s, "!"); CHECK(!redo(t, s)); }                                                // a new edit throws the redo list away
    { std::string t = "abcdef"; State s; moveTo(t, s, 6, false); backspace(t, s, false); backspace(t, s, false); backspace(t, s, false); EQ(t, "abc");
      CHECK(undo(t, s)); EQ(t, "abcdef"); CHECK(s.caret == 6); }                                         // three backspaces: one step
    { std::string t = "abc"; State s; selectAll(t, s); deleteSelection(t, s); EQ(t, ""); CHECK(undo(t, s)); EQ(t, "abc"); CHECK(s.hasSel()); }
    { std::string t; State s; for (int i = 0; i < 300; ++i) { insert(t, s, "x", 1000, 0); } CHECK(s.undo.size() <= 100); }
    // ---- clamp when the text was changed from outside
    { std::string t = "abc"; State s; s.caret = 99; s.anchor = 50; clamp(t, s); CHECK(s.caret == 3 && s.anchor == 3); t.clear(); clamp(t, s); CHECK(s.caret == 0); }
    // ---- sanitize
    EQ(sanitize("a\r\nb\tc\x01" "d", true, false), "a\nbcd");
    EQ(sanitize("a\r\nb", false, false), "ab");
    EQ(sanitize("12:30-4.5/x+y \xC3\xA9", false, true), "12:30-4.5/+");
    EQ(sanitize("caf\xC3\xA9 \xF0\x9F\x98\x80", false, false), "caf\xC3\xA9 \xF0\x9F\x98\x80");
    EQ(sanitize("a\x80" "b", false, false), "ab");                      // a lone continuation byte
    EQ(sanitize("a\xC3", false, false), "a");                           // cut off
    EQ(sanitize("a\xC0\xAF" "b", false, false), "ab");                  // overlong
    EQ(sanitize("a\xED\xA0\x80" "b", false, false), "ab");              // a UTF-16 surrogate written as UTF-8
    EQ(sanitize("a\xF5\x80\x80\x80" "b", false, false), "ab");          // beyond U+10FFFF
    EQ(sanitize("\x7F", false, false), "");
    // ---- up / down in a field with newlines (no layout known)
    { std::string t = "abc\nde\n\nfghij"; State s;
      CHECK(verticalTarget(t, {}, 2, -1) == 0); CHECK(verticalTarget(t, {}, 2, +1) == 6);     // 'ab|c' down -> 'de|' ends at the line end (col 2)
      CHECK(verticalTarget(t, {}, 6, +1) == 7);                                                // 'de|' down -> the empty line
      CHECK(verticalTarget(t, {}, 7, +1) == 8); CHECK(verticalTarget(t, {}, 10, -1) == 7); CHECK(verticalTarget(t, {}, 10, +1) == t.size());
      CHECK(verticalTarget(t, {}, 3, +1) == 6);                                                // end of 'abc' (col 3) down -> end of 'de'
      CHECK(verticalTarget(t, {}, 8, -1) == 7); (void)s; }
    // ---- with wrapped lines: "hello world" wrapped as "hello " / "world"
    { std::string t = "hello world"; std::vector<size_t> st = {0, 6};
      CHECK(visualLineStart(t, st, 8) == 6); CHECK(visualLineEnd(t, st, 2) == 5); CHECK(visualLineEnd(t, st, 8) == 11); CHECK(visualLineStart(t, st, 3) == 0);
      CHECK(verticalTarget(t, st, 8, -1) == 2);                                                // 'wo|rld' up -> 'he|llo'
      CHECK(verticalTarget(t, st, 1, +1) == 7);
      CHECK(verticalTarget(t, st, 5, +1) == 11);                                               // end of 'hello' down -> the end of 'world'
      CHECK(verticalTarget(t, st, 11, -1) == 5); }                                             // end of 'world' (col 5) up -> end of 'hello' (before the space)
    { std::string t = "abcdefghij"; std::vector<size_t> st = {0, 4, 8};                       // a word broken in the middle, nothing dropped
      CHECK(visualLineEnd(t, st, 1) == 4); CHECK(verticalTarget(t, st, 9, -1) == 5); CHECK(verticalTarget(t, st, 5, +1) == 9); }
    // ---- UTF-16 positions
    { std::string t = "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80z"; auto v = wideToByte(t);        // units: a é € (😀 = 2 units) z -> 6 units
      CHECK(v.size() == 7); CHECK(v[0] == 0 && v[1] == 1 && v[2] == 3 && v[3] == 6 && v[4] == 6 && v[5] == 10 && v[6] == 11);
      CHECK(byteToWide(v, 0) == 0); CHECK(byteToWide(v, 3) == 2); CHECK(byteToWide(v, 6) == 3); CHECK(byteToWide(v, 10) == 5); CHECK(byteToWide(v, 11) == 6); }
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
