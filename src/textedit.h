// Editing one text field of the settings panel: caret, selection, word moves, undo. Portable (no Windows, no drawing), so it can be
// tested on its own: tests/textedit_test.cpp.
//
// The text is UTF-8 in a std::string. The caret and the selection anchor are BYTE offsets that always sit between two characters.
// Nothing here knows about the screen: main.cpp turns key presses into these calls, scene_settings.cpp draws the result.
#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include <cstddef>

namespace textedit {

struct Snap { std::string text; size_t caret = 0, anchor = 0; };

struct State {
    size_t caret = 0, anchor = 0;            // anchor != caret: the text between them is selected
    std::vector<Snap> undo, redo;
    int lastKind = 0;                        // what the last edit was (1 typing, 2 backspace, 3 delete), so a run of the same kind is ONE undo step
    bool hasSel() const { return caret != anchor; }
    size_t lo() const { return std::min(caret, anchor); }
    size_t hi() const { return std::max(caret, anchor); }
    void reset(size_t at) { caret = anchor = at; undo.clear(); redo.clear(); lastKind = 0; }
};

// ---------------------------------------------------------------- characters
inline bool isCont(unsigned char c) { return (c & 0xC0) == 0x80; }
// the start of the character that contains byte i (i itself when it is already a start)
inline size_t snap(const std::string& t, size_t i) {
    if (i >= t.size()) return t.size();
    while (i > 0 && isCont((unsigned char)t[i])) --i;
    return i;
}
inline size_t prevChar(const std::string& t, size_t i) {
    i = snap(t, i);
    if (i == 0) return 0;
    --i;
    while (i > 0 && isCont((unsigned char)t[i])) --i;
    return i;
}
inline size_t nextChar(const std::string& t, size_t i) {
    i = snap(t, i);
    if (i >= t.size()) return t.size();
    ++i;
    while (i < t.size() && isCont((unsigned char)t[i])) ++i;
    return i;
}
// 0 space/newline, 1 letters digits and anything non-ASCII, 2 punctuation
inline int charClass(const std::string& t, size_t i) {
    const unsigned char c = (unsigned char)t[i];
    if (c == ' ' || c == '\t' || c == '\n') return 0;
    if (c >= 0x80 || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') return 1;
    return 2;
}
inline size_t wordLeft(const std::string& t, size_t i) {
    i = snap(t, i);
    while (i > 0 && charClass(t, prevChar(t, i)) == 0) i = prevChar(t, i);
    if (i == 0) return 0;
    const int c = charClass(t, prevChar(t, i));
    while (i > 0 && charClass(t, prevChar(t, i)) == c) i = prevChar(t, i);
    return i;
}
inline size_t wordRight(const std::string& t, size_t i) {
    i = snap(t, i);
    if (i >= t.size()) return t.size();
    const int c = charClass(t, i);
    if (c != 0) while (i < t.size() && charClass(t, i) == c) i = nextChar(t, i);
    while (i < t.size() && charClass(t, i) == 0) i = nextChar(t, i);
    return i;
}
// start / end of the line (between newlines) that holds byte i
inline size_t lineStart(const std::string& t, size_t i) { i = std::min(i, t.size()); const size_t p = i == 0 ? std::string::npos : t.rfind('\n', i - 1); return p == std::string::npos ? 0 : p + 1; }
inline size_t lineEnd(const std::string& t, size_t i) { i = std::min(i, t.size()); const size_t p = t.find('\n', i); return p == std::string::npos ? t.size() : p; }

// Keeps the caret and the anchor inside the text and between characters (the text may have been changed from outside).
inline void clamp(const std::string& t, State& s) { s.caret = snap(t, std::min(s.caret, t.size())); s.anchor = snap(t, std::min(s.anchor, t.size())); }

// ---------------------------------------------------------------- what may be typed
// Cleans text before it goes in: invalid UTF-8 is dropped, control characters go (a newline stays only in a multi-line field),
// and a number field only keeps digits and  . - + : /
inline std::string sanitize(const std::string& in, bool multi, bool numeric) {
    std::string out;
    for (size_t i = 0; i < in.size();) {
        const unsigned char c = (unsigned char)in[i];
        size_t n = 1; unsigned cp = c;
        if (c >= 0x80) {
            const int need = (c >= 0xC2 && c < 0xE0) ? 1 : (c >= 0xE0 && c < 0xF0) ? 2 : (c >= 0xF0 && c <= 0xF4) ? 3 : -1;
            if (need < 0 || i + (size_t)need >= in.size()) { ++i; continue; }          // not a start byte, or cut off
            bool ok = true; cp = need == 1 ? (c & 0x1Fu) : need == 2 ? (c & 0x0Fu) : (c & 0x07u);
            for (int k = 1; k <= need; ++k) { const unsigned char d = (unsigned char)in[i + k]; if (!isCont(d)) { ok = false; break; } cp = (cp << 6) | (d & 0x3Fu); }
            if (!ok || (need == 2 && (cp < 0x800 || (cp >= 0xD800 && cp < 0xE000))) || (need == 3 && (cp < 0x10000 || cp > 0x10FFFF))) { ++i; continue; }
            n = (size_t)need + 1;
        }
        if (n == 1) {
            if (c == '\r') { ++i; continue; }
            if (c == '\n') { if (!multi) { ++i; continue; } }
            else if (c < 32 || c == 127) { ++i; continue; }
            if (numeric && !((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+' || c == ':' || c == '/')) { ++i; continue; }
        } else if (numeric) { i += n; continue; }
        out.append(in, i, n);
        i += n;
    }
    return out;
}

// ---------------------------------------------------------------- moving
// Puts the caret at pos; extend keeps the anchor where it was (Shift held), otherwise there is no selection.
inline void moveTo(const std::string& t, State& s, size_t pos, bool extend) {
    s.caret = snap(t, std::min(pos, t.size()));
    if (!extend) s.anchor = s.caret;
    s.lastKind = 0;
}
inline void selectAll(const std::string& t, State& s) { s.anchor = 0; s.caret = t.size(); s.lastKind = 0; }

// Visual lines (a long line is wrapped on screen): 'starts' holds the byte offset where each visual line begins, ascending, starts[0] == 0.
// Empty when the layout is not known (then a line is a line between newlines).
inline size_t lineIndexOf(const std::vector<size_t>& starts, size_t pos) {
    size_t k = 0;
    while (k + 1 < starts.size() && starts[k + 1] <= pos) ++k;
    return k;
}
inline size_t visualLineStart(const std::string& t, const std::vector<size_t>& starts, size_t pos) {
    return starts.empty() ? lineStart(t, pos) : std::min(starts[lineIndexOf(starts, pos)], t.size());
}
inline size_t visualLineEnd(const std::string& t, const std::vector<size_t>& starts, size_t pos) {
    if (starts.empty()) return lineEnd(t, pos);
    const size_t k = lineIndexOf(starts, pos);
    if (k + 1 >= starts.size()) return t.size();
    const size_t nx = std::min(starts[k + 1], t.size());
    // a line that ends at a newline or at the space it was wrapped on ends BEFORE that character
    return (nx > 0 && (t[nx - 1] == '\n' || t[nx - 1] == ' ' || t[nx - 1] == '\t')) ? nx - 1 : nx;
}
// One visual line up (dir -1) or down (+1), keeping the column (counted in characters). On the first line Up goes to the start, on the last Down to the end.
inline size_t verticalTarget(const std::string& t, const std::vector<size_t>& starts, size_t caret, int dir) {
    std::vector<size_t> own;
    const std::vector<size_t>* v = &starts;
    if (starts.empty()) {                                  // no layout: lines between newlines
        own.push_back(0);
        for (size_t i = 0; i < t.size(); ++i) if (t[i] == '\n') own.push_back(i + 1);
        v = &own;
    }
    const size_t k = lineIndexOf(*v, caret);
    size_t col = 0;
    for (size_t i = (*v)[k]; i < caret; i = nextChar(t, i)) ++col;
    if (dir < 0 && k == 0) return 0;
    if (dir > 0 && k + 1 >= v->size()) return t.size();
    const size_t to = dir < 0 ? k - 1 : k + 1;
    const size_t end = visualLineEnd(t, *v, (*v)[to]);
    size_t pos = (*v)[to];
    for (size_t c = 0; c < col && pos < end; ++c) pos = nextChar(t, pos);
    return std::min(pos, end);
}

// ---------------------------------------------------------------- changing
inline void pushUndo(const std::string& t, State& s, int kind) {
    if (kind == 0 || kind != s.lastKind || s.undo.empty()) {
        s.undo.push_back({t, s.caret, s.anchor});
        if (s.undo.size() > 100) s.undo.erase(s.undo.begin());
    }
    s.redo.clear();
    s.lastKind = kind;
}
inline std::string selected(const std::string& t, const State& s) { return t.substr(std::min(s.lo(), t.size()), std::min(s.hi(), t.size()) - std::min(s.lo(), t.size())); }

// Replaces the selection (or inserts at the caret) with 'text' (already sanitized). The whole field never gets longer than maxBytes: what does not
// fit is left out, at a character boundary. kind: 1 for typing a character (a run of those is one undo step), 0 for a paste.
inline bool insert(std::string& t, State& s, const std::string& text, size_t maxBytes, int kind = 1) {
    clamp(t, s);
    if (text.empty()) return false;
    const size_t lo = s.lo(), hi = s.hi();
    const size_t keep = t.size() - (hi - lo);
    if (keep >= maxBytes) return false;
    std::string ins = text;
    if (keep + ins.size() > maxBytes) ins.erase(snap(ins, maxBytes - keep));
    if (ins.empty()) return false;
    pushUndo(t, s, kind);
    t.replace(lo, hi - lo, ins);
    s.caret = s.anchor = lo + ins.size();
    return true;
}
inline bool deleteSelection(std::string& t, State& s) {
    clamp(t, s);
    if (!s.hasSel()) return false;
    pushUndo(t, s, 0);
    t.erase(s.lo(), s.hi() - s.lo());
    s.caret = s.anchor = std::min(s.lo(), t.size());
    return true;
}
inline bool backspace(std::string& t, State& s, bool word) {
    clamp(t, s);
    if (s.hasSel()) return deleteSelection(t, s);
    if (s.caret == 0) return false;
    const size_t from = word ? wordLeft(t, s.caret) : prevChar(t, s.caret);
    pushUndo(t, s, word ? 0 : 2);
    t.erase(from, s.caret - from);
    s.caret = s.anchor = from;
    return true;
}
inline bool del(std::string& t, State& s, bool word) {
    clamp(t, s);
    if (s.hasSel()) return deleteSelection(t, s);
    if (s.caret >= t.size()) return false;
    const size_t to = word ? wordRight(t, s.caret) : nextChar(t, s.caret);
    pushUndo(t, s, word ? 0 : 3);
    t.erase(s.caret, to - s.caret);
    s.anchor = s.caret;
    return true;
}
inline bool undo(std::string& t, State& s) {
    if (s.undo.empty()) return false;
    s.redo.push_back({t, s.caret, s.anchor});
    Snap k = s.undo.back(); s.undo.pop_back();
    t = k.text; s.caret = k.caret; s.anchor = k.anchor; s.lastKind = 0;
    clamp(t, s);
    return true;
}
inline bool redo(std::string& t, State& s) {
    if (s.redo.empty()) return false;
    s.undo.push_back({t, s.caret, s.anchor});
    Snap k = s.redo.back(); s.redo.pop_back();
    t = k.text; s.caret = k.caret; s.anchor = k.anchor; s.lastKind = 0;
    clamp(t, s);
    return true;
}

// ---------------------------------------------------------------- UTF-16 positions (the drawing code measures text as wide strings)
// byteAt[i] = the byte offset of the character that holds UTF-16 unit i; byteAt.back() == t.size().
inline std::vector<size_t> wideToByte(const std::string& t) {
    std::vector<size_t> v;
    for (size_t i = 0; i < t.size();) {
        const unsigned char c = (unsigned char)t[i];
        size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
        if (i + n > t.size()) n = 1;                                   // the same reading as util::widen: a cut-off sequence is one '?'
        unsigned cp = c;
        if (n == 4) cp = ((c & 7u) << 18) | (((unsigned char)t[i + 1] & 0x3Fu) << 12) | (((unsigned char)t[i + 2] & 0x3Fu) << 6) | ((unsigned char)t[i + 3] & 0x3Fu);
        v.push_back(i);
        if (n == 4 && cp >= 0x10000) v.push_back(i);                   // outside the BMP: two UTF-16 units
        i += n;
    }
    v.push_back(t.size());
    return v;
}
// the first UTF-16 unit of the character that starts at (or holds) byte offset 'pos'
inline size_t byteToWide(const std::vector<size_t>& byteAt, size_t pos) {
    return (size_t)(std::lower_bound(byteAt.begin(), byteAt.end(), pos) - byteAt.begin());
}

} // namespace textedit
