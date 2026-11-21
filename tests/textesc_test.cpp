// g++ -std=c++17 -I src tests/textesc_test.cpp -o textesc_test && ./textesc_test
#include "textesc.h"
#include <cstdio>
int fails = 0;
static void roundTrip(const std::string& note, const char* what) {
    const std::string stored = escapeNl(note);
    if (stored.find('\n') != std::string::npos || stored.find('\r') != std::string::npos) { printf("FAIL %s: stored text has a raw line break\n", what); ++fails; }
    const std::string back = unescapeNl(stored, true);
    std::string want; for (char c : note) if (c != '\r') want += c;           // carriage returns are dropped on purpose
    if (back != want) { printf("FAIL %s: '%s' came back as '%s'\n", what, want.c_str(), back.c_str()); ++fails; }
}
static void legacy(const std::string& stored, const std::string& want, const char* what) {
    const std::string got = unescapeNl(stored, false);
    if (got != want) { printf("FAIL legacy %s: '%s' read as '%s'\n", what, stored.c_str(), got.c_str()); ++fails; }
}
int main() {
    roundTrip("", "empty");
    roundTrip("plain text", "plain");
    roundTrip("two\nlines\nthree", "lines");
    roundTrip("C:\\notes", "the review's example: a path");
    roundTrip("C:\\new\\table.txt", "path with \\n and \\t in it");
    roundTrip("literal \\n here", "literal backslash-n");
    roundTrip("double \\\\ slash and \\\\n", "double backslashes");
    roundTrip("ends with a slash\\", "trailing backslash");
    roundTrip("\\", "only a backslash");
    roundTrip("\n\n", "only line breaks");
    roundTrip("mix \\ and \n and \\n and \\\\ ", "everything together");
    roundTrip("caf\xC3\xA9 \xF0\x9F\x98\x80\nnext", "unicode");
    roundTrip("windows\r\nline ends", "CRLF");
    // files from before the fix: only \n means a line break, a backslash stays as it is
    legacy("a\\nb", "a\nb", "line break");
    legacy("C:\\temp\\x", "C:\\temp\\x", "path without \\n");
    legacy("two \\\\ slashes", "two \\\\ slashes", "two backslashes stay two");
    legacy("trailing\\", "trailing\\", "trailing backslash");
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
