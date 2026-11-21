// Native test of the JSON parser: g++ -std=c++17 -I src tests/minijson_test.cpp -o minijson_test && ./minijson_test tests/sample_*.json
// (The sample files given on the command line must all parse: they are real provider replies.)
#include "minijson.h"
#include <cstdio>
#include <fstream>
#include <sstream>
int fails = 0;
static void good(const char* json) {
    mj::Value v; std::string err;
    if (!mj::parse(json, v, err)) { printf("FAIL should parse: %s  (%s)\n", json, err.c_str()); ++fails; }
}
static void bad(const std::string& json, const char* why) {
    mj::Value v; std::string err;
    if (mj::parse(json, v, err)) { printf("FAIL should be refused (%s): %.60s\n", why, json.c_str()); ++fails; }
}
static std::string str(const char* json) {
    mj::Value v; std::string err;
    if (!mj::parse(json, v, err) || v.type != mj::Value::String) { printf("FAIL string expected: %s (%s)\n", json, err.c_str()); ++fails; return "?"; }
    return v.str;
}
static void same(const std::string& got, const std::string& want, const char* what) {
    if (got != want) { printf("FAIL %s\n", what); ++fails; }
}
int main(int argc, char** argv) {
    // ---- the whole text must be one value
    good("{}"); good("[]"); good("  {\"a\": [1, 2, {\"b\": null}], \"c\": true}  \n"); good("12"); good("\"x\""); good("null");
    bad("{\"id\":\"qa_trailing\",\"global\":true} THIS IS INVALID JSON", "trailing text (the review's example)");
    bad("{} {}", "two values"); bad("[1,2]x", "text after an array"); bad("1 2", "two numbers"); bad("true false", "two words");
    bad("", "empty"); bad("   ", "only spaces"); bad("{", "open object"); bad("[1,", "open array"); bad("{\"a\":1,}", "trailing comma (themes remove theirs before parsing)");
    bad("{\"a\" 1}", "missing colon"); bad("{a:1}", "unquoted key"); bad("tru", "short literal"); bad("truex", "literal with a tail"); bad("nul", "short null");
    // ---- numbers the JSON way
    good("0"); good("-0"); good("-0.5e-3"); good("1E+2"); good("123456789012345678901234567890"); good("[0.1,2.50,-3]");
    for (const char* n : {"01", "1.", ".5", "-", "1e", "1e+", "+1", "0x10", "-inf", "inf", "NaN", "-nan", "1.5.2", "--1", "1e999", "-1e999", "00"})
        bad(n, "bad number");
    { mj::Value v; std::string e; mj::parse("[1.5e2, -0.25, 7]", v, e); if (v.arr.size() != 3 || v.arr[0].num != 150 || v.arr[1].num != -0.25 || v.arr[2].num != 7) { printf("FAIL number values\n"); ++fails; } }
    // ---- strings
    same(str("\"a\\nb\\t\\\"q\\\" \\\\ \\/ \\b\\f\\r\""), "a\nb\t\"q\" \\ / \b\f\r", "standard escapes");
    same(str("\"\\u00e9\""), "\xC3\xA9", "\\u00e9 is e-acute");
    same(str("\"\\u20AC\""), "\xE2\x82\xAC", "\\u20AC is the euro sign");
    same(str("\"\\ud83d\\ude00\""), "\xF0\x9F\x98\x80", "a surrogate pair becomes one 4-byte character");
    same(str("\"\\uD83D\\uDE00!\""), "\xF0\x9F\x98\x80!", "upper-case pair, text after it");
    same(str("\"\\ud83d\""), "\xEF\xBF\xBD", "a lone high half is the replacement character");
    same(str("\"\\ude00\""), "\xEF\xBF\xBD", "a lone low half is the replacement character");
    same(str("\"\\ud83dx\""), "\xEF\xBF\xBDx", "a high half followed by text");
    same(str("\"\\ud83d\\u0041\""), "\xEF\xBF\xBD" "A", "a high half followed by a normal escape");
    same(str("\"caf\xC3\xA9\""), "caf\xC3\xA9", "raw UTF-8 passes through");
    bad("\"\\q\"", "unknown escape"); bad("\"\\u12G4\"", "bad hex"); bad("\"\\u12\"", "short \\u"); bad("\"\\u\"", "empty \\u"); bad("\"abc", "unterminated");
    bad("\"a\nb\"", "raw line break in a string"); bad(std::string("\"a\tb\""), "raw tab in a string"); bad("\"abc\\", "backslash at the end");
    // ---- depth: refused cleanly, no crash
    { std::string s; for (int i = 0; i < 60; ++i) s += '['; for (int i = 0; i < 60; ++i) s += ']'; good(s.c_str()); }
    { std::string s; for (int i = 0; i < 100; ++i) s += '['; for (int i = 0; i < 100; ++i) s += ']'; bad(s, "100 levels deep"); }
    { std::string s(2000000, '['); bad(s, "two million open brackets"); }
    { std::string s; for (int i = 0; i < 100000; ++i) s += "{\"a\":"; bad(s, "deep objects"); }
    // ---- sample provider replies given on the command line
    for (int i = 1; i < argc; ++i) {
        std::ifstream f(argv[i], std::ios::binary); std::ostringstream ss; ss << f.rdbuf();
        mj::Value v; std::string err;
        if (!mj::parse(ss.str(), v, err)) { printf("FAIL %s: %s\n", argv[i], err.c_str()); ++fails; } else printf("ok   %s\n", argv[i]);
    }
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
