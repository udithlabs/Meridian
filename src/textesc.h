// How a note (several lines of text) is stored on ONE line of settings.ini, and read back. Portable, so it can be tested on its own.
#pragma once
#include <string>

// A note is one line in settings.ini, so a line break is written as \n. A backslash has to be escaped too (as \\), or a note that really
// contains the characters  \n  (a Windows path such as C:\notes) would come back with a line break in it. Files from before this fix
// have no "note_format" line: their notes are read the old way (only \n is a line break, backslashes are left alone).
inline std::string escapeNl(const std::string& v) {
    std::string o;
    for (char ch : v) { if (ch == '\\') o += "\\\\"; else if (ch == '\n') o += "\\n"; else if (ch != '\r') o += ch; }
    return o;
}
inline std::string unescapeNl(const std::string& v, bool current) {
    std::string o;
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == '\\' && i + 1 < v.size()) {
            if (v[i + 1] == 'n') { o += '\n'; ++i; continue; }
            if (current && v[i + 1] == '\\') { o += '\\'; ++i; continue; }
        }
        o += v[i];
    }
    return o;
}
