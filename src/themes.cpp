// Meridian: loading theme packs (see themes.h and themes/README.txt).
//
// Layout of this file, top to bottom:
//   1. small helpers           reading files, comments in JSON, colours
//   2. the Loader              turns one theme.json into a Pack (palette, fonts, dates, formulas, layers, faces)
//   3. finding packs on disk   scan the themes folders, detect changes
//   4. pictures and fonts      loaded when a pack needs them
//
// Nothing here draws. Drawing is in scene_theme.cpp.
//
// A mistake in a theme file never stops Meridian: a broken pack is listed in Settings with the reason, and a bad layer or
// setting is skipped with a warning (also shown in Settings), so you can fix the file and see the result a few seconds later.
#include "themes.h"
#include "minijson.h"
#include "astro.h"
#include "util.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <set>
#include <cstdio>
#include <cstring>

namespace fs = std::filesystem;

namespace themes {

using gfx::Color;

namespace {

std::vector<fs::path> g_roots;
std::vector<std::unique_ptr<Pack>> g_packs;
std::string g_sig;
size_t g_artBytes = 0;                      // decoded pixels of every picture held for the packs (see image())
constexpr size_t kMaxArtBytes = 192u * 1024 * 1024;
unsigned g_gen = 1;                         // bumped on every rescan, so cached state knows when packs were replaced

// ================================================================ 1. helpers
enum class Read { Ok, Missing, TooBig };
// Reads a whole file, unless it is bigger than maxBytes (then nothing is read at all: a huge file cannot use up the memory).
Read readText(const fs::path& p, std::string& out, size_t maxBytes) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return Read::Missing;
    f.seekg(0, std::ios::end);
    const std::streamoff n = f.tellg();
    if (n < 0) return Read::Missing;
    if ((unsigned long long)n > maxBytes) return Read::TooBig;
    f.seekg(0, std::ios::beg);
    out.assign((size_t)n, '\0');
    if (n > 0 && !f.read(&out[0], n)) { out.clear(); return Read::Missing; }
    if (out.size() >= 3 && (unsigned char)out[0] == 0xEF && (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF) out.erase(0, 3);   // BOM
    return Read::Ok;
}

// theme.json is "JSON with notes": // line comments, /* block comments */ and a trailing comma before ] or } are allowed.
// Comments are replaced by spaces (newlines are kept) so line numbers in error messages stay right.
std::string cleanJson(const std::string& in) {
    std::string a; a.reserve(in.size());
    const size_t n = in.size();
    bool str = false;
    for (size_t i = 0; i < n;) {                                   // pass 1: comments
        const char c = in[i];
        if (str) { a += c; if (c == '\\' && i + 1 < n) { a += in[i + 1]; i += 2; continue; } if (c == '"') str = false; ++i; continue; }
        if (c == '"') { str = true; a += c; ++i; continue; }
        if (c == '/' && i + 1 < n && in[i + 1] == '/') { while (i < n && in[i] != '\n') { a += ' '; ++i; } continue; }
        if (c == '/' && i + 1 < n && in[i + 1] == '*') {
            a += "  "; i += 2;
            while (i < n && !(in[i] == '*' && i + 1 < n && in[i + 1] == '/')) { a += in[i] == '\n' ? '\n' : ' '; ++i; }
            if (i < n) { a += "  "; i += 2; }
            continue;
        }
        a += c; ++i;
    }
    std::string b = a; str = false;
    for (size_t i = 0; i < a.size(); ++i) {                        // pass 2: a comma right before ] or }
        const char c = a[i];
        if (str) { if (c == '\\') ++i; else if (c == '"') str = false; continue; }
        if (c == '"') { str = true; continue; }
        if (c == ',') {
            size_t j = i + 1;
            while (j < a.size() && (a[j] == ' ' || a[j] == '\t' || a[j] == '\r' || a[j] == '\n')) ++j;
            if (j < a.size() && (a[j] == ']' || a[j] == '}')) b[i] = ' ';
        }
    }
    return b;
}

std::string lower(std::string s) { for (auto& c : s) if (c >= 'A' && c <= 'Z') c += 32; return s; }
std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

// "#RGB", "#RRGGBB", "#RRGGBBAA", "transparent", or a palette colour name ("ink", "ink2", "ink3", "accent", "accent2", "line",
// "panel", "plate"). Any of them may end in "/0.4" to multiply the opacity.
bool parseColor(const std::string& in, Col& out) {
    std::string s = trim(in);
    float mul = 1.f;
    const size_t sl = s.find('/');
    if (sl != std::string::npos) {
        const std::string ms = trim(s.substr(sl + 1));
        if (ms.empty()) return false;
        char* end = nullptr;
        const double mv = strtod(ms.c_str(), &end);
        if (end == ms.c_str() || *end != 0 || !std::isfinite(mv) || mv < 0.0 || mv > 1.0) return false;
        mul = (float)mv;
        s = trim(s.substr(0, sl));
    }
    const std::string l = lower(s);
    Col c; c.set = true;
    static const struct { const char* n; Col::Src src; } names[] = {{"ink", Col::Ink}, {"ink2", Col::Ink2}, {"ink3", Col::Ink3}, {"accent", Col::Accent},
        {"accent2", Col::Accent2}, {"line", Col::Line}, {"panel", Col::Panel}, {"plate", Col::Plate}};
    bool found = false;
    for (auto& nm : names) if (l == nm.n) { c.src = nm.src; c.c = {1, 1, 1, 1}; found = true; }
    if (!found) {
        if (l == "transparent") c.c = {0, 0, 0, 0};
        else if (!l.empty() && l[0] == '#') {
            std::vector<int> d;
            for (size_t i = 1; i < l.size(); ++i) { int h = hexv(l[i]); if (h < 0) return false; d.push_back(h); }
            if (d.size() == 3 || d.size() == 4) { c.c = {d[0] / 15.f, d[1] / 15.f, d[2] / 15.f, d.size() == 4 ? d[3] / 15.f : 1.f}; }
            else if (d.size() == 6 || d.size() == 8) {
                c.c = {(d[0] * 16 + d[1]) / 255.f, (d[2] * 16 + d[3]) / 255.f, (d[4] * 16 + d[5]) / 255.f, d.size() == 8 ? (d[6] * 16 + d[7]) / 255.f : 1.f};
            } else return false;
        } else return false;
    }
    c.c.a *= mul;
    out = c;
    return true;
}

const char* const ROLE[gfx::F_COUNT] = {"display", "light", "medium", "serif", "serifItalic", "mono", "monoMedium"};   // order of gfx::FontId

bool identOk(const std::string& s) {
    if (s.empty()) return false;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || (i > 0 && c >= '0' && c <= '9'))) return false;
    }
    return true;
}

// The names the clock provides as words, for {tokens} in text (numbers come from variables instead).
const char* const TOKENS[] = {"hm", "ampm", "sec", "time", "weekday", "weekdayShort", "day", "monthname", "mon", "year", "date", "utc"};
// ... plus, for every countdown named X:  {X_when}  "THURSDAY 19 NOVEMBER 2026"   {X_clock}  "12:00 AM"   {X_date}  "19 NOVEMBER"
//                                          {X_short}  "19 Nov"   {X_dow}  "Thu"          (all in the clock's own time zone;  {utc}  is "UTC+5:30")

// The variables every formula can use. The ORDER matters: scene_theme.cpp fills them in this order. Add new ones at the end.
const char* const BASE_VARS[] = {"t", "W", "H", "s", "portrait", "gloom", "haze", "hour", "sunAlt", "weekday", "day", "month", "year", "minute", "second", "light", "tzHours"};
const char* const CD_SUFFIX[] = {"left", "past", "days", "hrs", "min", "sec", "done", "daysf"};          // per countdown, in this order
// t         seconds since 1970 (grows forever: use it inside sin() or fract())      W H  window size in pixels      s  H / 1080
// portrait  1 when the window is taller than wide                                   gloom haze  0..1 weather light (cloud, fog)
// hour      local time of day, 0..24 (fractional)                                   sunAlt  height of the Sun in degrees
// light     0..1 how bright the weather leaves the scene: (1 - 0.45*gloom) * (1 - 0.55*haze). Handy as a multiplier for opacity.
// tzHours   the clock's distance from UTC in hours (5.5 for India, -5 for New York ...)
// X_left    seconds left until X (0 once reached)       X_past  seconds since X (0 before)         X_done  1 once X has been reached
// X_days X_hrs X_min X_sec  the time left (or since) broken into parts               X_daysf  days left as a fraction

// ================================================================ 2. the Loader
// ---- dates without a year (only for packs that say "yearly": true)
static bool monthDayOf(const std::string& s, int& m, int& d) {
    int y = 0, a = 0, b = 0, hh = 0, mm = 0;
    if (util::parseDateTimeStrict(s, y, a, b, hh, mm)) { m = a; d = b; return true; }       // YYYY-MM-DD: its month and day
    int n = 0;
    if (std::sscanf(s.c_str(), " %d-%d%n", &a, &b, &n) == 2 && util::onlySpace(s.c_str() + n) && util::validDayMonth(a, b)) { m = a; d = b; return true; }   // MM-DD
    return false;
}
// "MM-DD" or "MM-DD HH:MM"
static bool monthDayTime(const std::string& s, int& m, int& d, int& hh, int& mi) {
    int a = 0, b = 0, h = 0, k = 0, n = 0, n2 = 0;
    if (std::sscanf(s.c_str(), " %d-%d%n", &a, &b, &n) != 2 || !util::validDayMonth(a, b)) return false;
    if (util::onlySpace(s.c_str() + n)) { m = a; d = b; hh = 0; mi = 0; return true; }
    if (std::sscanf(s.c_str() + n, " %d:%d%n", &h, &k, &n2) != 2 || !util::onlySpace(s.c_str() + n + n2) || !util::validTimeOfDay(h, k)) return false;
    m = a; d = b; hh = h; mi = k; return true;
}
// the local-midnight instant of a month and day in a year (29 February in a year without one: 28 February)
static double instantOf(int y, int m, int d, int tzOff) {
    if (m == 2 && d == 29 && !util::isLeapYear(y)) d = 28;
    return astro::daysFromCivil(y, m, d) * 86400.0 - tzOff;
}
static std::string dateText(double instant, int tzOff) { const util::LocalTime L = util::toLocal(instant + 1.0, tzOff); return util::fmt("%04d-%02d-%02d", L.y, L.m, L.d); }

static bool safeRelPath(const std::string& rel) {
    if (rel.empty() || rel.find("..") != std::string::npos) return false;
    if (rel[0] == '/' || rel[0] == '\\' || rel.find(':') != std::string::npos) return false;
    const fs::path p = fs::u8path(rel);
    if (p.is_absolute() || p.has_root_path() || p.has_root_name() || p.has_root_directory()) return false;
    for (const auto& part : p) if (part == "..") return false;
    return true;
}

struct Loader {
    Pack& pk;
    explicit Loader(Pack& p) : pk(p) {}

    void warn(const std::string& where, const std::string& msg) { pk.warnings.push_back(where.empty() ? msg : where + ": " + msg); }

    // an object being read: remembers which keys were used so leftovers (usually typos) can be reported
    struct Obj {
        const mj::Value& v; std::string where; std::set<std::string> used;
        Obj(const mj::Value& val, const std::string& w) : v(val), where(w) {}
        const mj::Value* get(const char* k) { used.insert(k); return v.get(k); }
    };
    void finish(Obj& o) {
        if (o.v.type != mj::Value::Object) return;
        for (auto& kv : o.v.obj) if (!o.used.count(kv.first) && kv.first != "type" && kv.first != "note" && kv.first != "notes")
            warn(o.where, "unknown setting '" + kv.first + "' (ignored)");
    }

    bool num(Obj& o, const char* key, Num& out) {
        const mj::Value* v = o.get(key);
        if (!v) return false;
        if (v->type == mj::Value::Number) { out.set = true; out.c = v->num; return true; }
        if (v->type == mj::Value::Bool) { out.set = true; out.c = v->b ? 1 : 0; return true; }
        if (v->type == mj::Value::String) {
            auto prog = std::make_shared<expr::Program>(); std::string err;
            if (!prog->compile(v->str, pk.vars, err)) { warn(o.where, std::string("'") + key + "': " + err + "  [" + v->str + "]"); return false; }
            out.set = true;
            if (prog->isConst()) out.c = prog->constValue(); else out.prog = prog;
            return true;
        }
        warn(o.where, std::string("'") + key + "' should be a number or a formula");
        return false;
    }
    bool str(Obj& o, const char* key, std::string& out) {
        const mj::Value* v = o.get(key);
        if (!v) return false;
        if (v->type != mj::Value::String) { warn(o.where, std::string("'") + key + "' should be text"); return false; }
        out = v->str; return true;
    }
    bool boolean(Obj& o, const char* key, bool& out) {
        const mj::Value* v = o.get(key);
        if (!v) return false;
        if (v->type == mj::Value::Bool) { out = v->b; return true; }
        if (v->type == mj::Value::Number) { out = v->num != 0; return true; }
        warn(o.where, std::string("'") + key + "' should be true or false"); return false;
    }
    bool col(Obj& o, const char* key, Col& out) {
        const mj::Value* v = o.get(key);
        if (!v) return false;
        if (v->type != mj::Value::String || !parseColor(v->str, out)) { warn(o.where, std::string("'") + key + "' is not a colour (use \"#RRGGBB\", \"#RRGGBBAA\" or a palette name)"); return false; }
        return true;
    }
    bool cols(Obj& o, const char* key, std::vector<Col>& out) {
        const mj::Value* v = o.get(key);
        if (!v) return false;
        if (v->type != mj::Value::Array) { warn(o.where, std::string("'") + key + "' should be a list of colours"); return false; }
        out.clear();
        for (auto& e : v->arr) { Col c; if (e.type == mj::Value::String && parseColor(e.str, c)) out.push_back(c); else warn(o.where, std::string("'") + key + "' has a value that is not a colour"); }
        return !out.empty();
    }
    bool nums(Obj& o, const char* key, std::vector<double>& out) {
        const mj::Value* v = o.get(key);
        if (!v) return false;
        if (v->type != mj::Value::Array) { warn(o.where, std::string("'") + key + "' should be a list of numbers"); return false; }
        out.clear(); for (auto& e : v->arr) if (e.type == mj::Value::Number) out.push_back(e.num);
        return true;
    }

    // ---- text with {tokens}
    bool isToken(const std::string& n) {
        for (const char* t : TOKENS) if (n == t) return true;
        for (const Countdown& c : pk.countdowns)
            if (n == c.id + "_when" || n == c.id + "_clock" || n == c.id + "_date" || n == c.id + "_short" || n == c.id + "_dow") return true;
        return false;
    }
    Tpl tpl(const std::string& s, const std::string& where) {
        Tpl t; std::string lit;
        auto flush = [&] { if (!lit.empty()) { TextPiece p; p.type = TextPiece::Lit; p.s = lit; t.pieces.push_back(p); lit.clear(); } };
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '{') {
                const size_t e = s.find('}', i);
                if (e != std::string::npos) {
                    std::string name = s.substr(i + 1, e - i - 1); int width = 0;
                    const size_t colon = name.find(':');
                    if (colon != std::string::npos) { width = atoi(name.c_str() + colon + 1); name = name.substr(0, colon); }
                    TextPiece p; p.s = name; p.width = std::max(0, std::min(12, width));
                    // words from the clock win over a number of the same name: {weekday} is "THURSDAY" (the formula name 'weekday' is still 0..6)
                    if (isToken(name)) { p.type = TextPiece::Tok; flush(); t.pieces.push_back(p); i = e; continue; }
                    const int slot = pk.vars.find(name);
                    if (slot >= 0) { p.type = TextPiece::Var; p.var = slot; flush(); t.pieces.push_back(p); i = e; continue; }
                    warn(where, "unknown {" + name + "} in text (shown as it is)");
                }
            }
            lit += s[i];
        }
        flush();
        return t;
    }
    // "text": "one"  |  ["one", "two"]  |  [{"when": "...", "text": "one" | ["a", "b"]}, {"text": "otherwise"}]
    bool choice(Obj& o, const char* key, Choice& out) {
        const mj::Value* v = o.get(key);
        if (!v) return false;
        out.cases.clear();
        auto texts = [&](const mj::Value& tv, TextCase& tc) {
            if (tv.type == mj::Value::String) tc.texts.push_back(tpl(tv.str, o.where));
            else if (tv.type == mj::Value::Array) for (auto& e : tv.arr) if (e.type == mj::Value::String) tc.texts.push_back(tpl(e.str, o.where));
        };
        if (v->type == mj::Value::String) { TextCase tc; texts(*v, tc); out.cases.push_back(tc); return true; }
        if (v->type == mj::Value::Array) {
            bool allStr = true; for (auto& e : v->arr) if (e.type != mj::Value::String) allStr = false;
            if (allStr) { TextCase tc; texts(*v, tc); out.cases.push_back(tc); return true; }
            for (auto& e : v->arr) {
                TextCase tc;
                if (e.type == mj::Value::String) texts(e, tc);
                else if (e.type == mj::Value::Object) {
                    Obj eo(e, o.where + " text"); eo.used.insert("when"); eo.used.insert("text");
                    if (const mj::Value* w = e.get("when")) {
                        if (w->type == mj::Value::String) {
                            auto prog = std::make_shared<expr::Program>(); std::string err;
                            if (prog->compile(w->str, pk.vars, err)) { tc.when.set = true; tc.when.prog = prog; }
                            else { warn(o.where, "text 'when': " + err); tc.when.set = true; tc.when.c = 0.0; }   // failed 'when' must evaluate false, never true
                        } else if (w->type == mj::Value::Number || w->type == mj::Value::Bool) { tc.when.set = true; tc.when.c = w->type == mj::Value::Bool ? (w->b ? 1 : 0) : w->num; }
                        else { warn(o.where, "text 'when' should be a formula or number"); tc.when.set = true; tc.when.c = 0.0; }
                    }
                    if (const mj::Value* tv = e.get("text")) texts(*tv, tc);
                    finish(eo);
                }
                if (!tc.texts.empty()) out.cases.push_back(tc);
            }
            return !out.cases.empty();
        }
        warn(o.where, std::string("'") + key + "' should be text, a list of texts, or a list of {when, text}");
        return false;
    }

    void needFile(const std::string& where, const std::string& rel) {
        if (rel.empty()) return;
        std::error_code ec;
        if (!safeRelPath(rel) || !fs::exists(pk.dir / fs::u8path(rel), ec)) warn(where, "file not found: " + rel);
    }

    // ---- one layer
    void loadLayer(const mj::Value& jv, int idx, std::vector<Layer>& out, const std::string& sceneName) {
        const std::string where = sceneName + " layer " + std::to_string(idx + 1);
        if (jv.type != mj::Value::Object) { warn(where, "should be an object { ... }"); return; }
        Obj o(jv, where);
        std::string type; str(o, "type", type); o.used.insert("type");
        type = lower(type);
        static const struct { const char* n; Kind k; } kinds[] = {{"gradient", Kind::Gradient}, {"rect", Kind::Rect}, {"circle", Kind::Circle}, {"image", Kind::Image},
            {"glow", Kind::Glow}, {"stars", Kind::Stars}, {"skyline", Kind::Skyline}, {"streaks", Kind::Streaks}, {"text", Kind::Text}, {"shapes", Kind::Shapes},
            {"banner", Kind::Banner}, {"confetti", Kind::Confetti}, {"shade", Kind::Shade}, {"line", Kind::Line}, {"button", Kind::Button}, {"map", Kind::Map},
            {"traffic", Kind::Traffic}};
        bool known = false; Layer L; L.index = idx;
        for (auto& k : kinds) if (type == k.n) { L.kind = k.k; known = true; }
        if (!known) { warn(where, type.empty() ? "no \"type\" given (layer skipped)" : "unknown type '" + type + "' (layer skipped)"); return; }
        if (jv.get("when") && !num(o, "when", L.when)) { L.when.set = true; L.when.c = 0.0; }     // broken 'when' disables the layer instead of showing it unconditionally
        num(o, "opacity", L.opacity);
        switch (L.kind) {
            case Kind::Gradient: num(o, "y0", L.y0); num(o, "y1", L.y1); cols(o, "colors", L.colors); nums(o, "at", L.at);
                if (L.colors.size() < 2) { warn(where, "a gradient needs 'colors' with at least two colours (layer skipped)"); return; } break;
            case Kind::Rect: num(o, "x", L.x); num(o, "y", L.y); num(o, "w", L.w); num(o, "h", L.h); num(o, "radius", L.radius); col(o, "color", L.color); num(o, "thick", L.thick); break;
            case Kind::Circle: num(o, "x", L.x); num(o, "y", L.y); num(o, "r", L.r); col(o, "color", L.color); col(o, "color2", L.color2); num(o, "thick", L.thick); break;
            case Kind::Image: str(o, "src", L.src); num(o, "x", L.x); num(o, "y", L.y); num(o, "w", L.w); num(o, "h", L.h); str(o, "anchor", L.anchor);
                num(o, "rotate", L.rotate); str(o, "pivot", L.pivot); boolean(o, "flip", L.flip);
                if (L.src.empty()) { warn(where, "an image needs 'src' (layer skipped)"); return; }
                needFile(where, L.src); break;
            case Kind::Glow: num(o, "x", L.x); num(o, "y", L.y); num(o, "r", L.r); num(o, "rx", L.rx); num(o, "ry", L.ry); col(o, "color", L.color); break;
            case Kind::Stars: num(o, "count", L.count); num(o, "seed", L.seed); num(o, "y0", L.y0); num(o, "y1", L.y1); num(o, "size", L.size); num(o, "size2", L.size2);
                num(o, "speed", L.speed); cols(o, "colors", L.colors); num(o, "fade", L.fade); break;
            case Kind::Skyline: num(o, "y", L.y); num(o, "h", L.h); num(o, "seed", L.seed); cols(o, "colors", L.colors); cols(o, "windows", L.windows); num(o, "sides", L.sides); break;
            case Kind::Streaks: num(o, "x", L.x); num(o, "y0", L.y0); num(o, "y1", L.y1); num(o, "count", L.count); num(o, "w", L.w); num(o, "w2", L.w2);
                col(o, "color", L.color); col(o, "color2", L.color2); num(o, "thick", L.thick); num(o, "thick2", L.thick2); num(o, "speed", L.speed); num(o, "sway", L.sway); break;
            case Kind::Text: choice(o, "text", L.text); num(o, "x", L.x); num(o, "y", L.y); str(o, "font", L.font); num(o, "size", L.size); num(o, "cap", L.cap);
                num(o, "maxw", L.maxw); num(o, "period", L.period); str(o, "align", L.align); col(o, "color", L.color); col(o, "color2", L.color2); num(o, "tracking", L.tracking); num(o, "gap", L.gap);      // gap: the width of ':' in a digits text, relative to a digit
                boolean(o, "upper", L.upper); boolean(o, "digits", L.digits); col(o, "neon", L.neon); num(o, "neonStrength", L.neonStrength);
                if (L.text.cases.empty()) { warn(where, "a text layer needs 'text' (layer skipped)"); return; } break;
            case Kind::Shapes: str(o, "shape", L.shape); num(o, "count", L.count); num(o, "x", L.x); num(o, "y", L.y); num(o, "size", L.size); num(o, "gap", L.gap);
                num(o, "lit", L.lit); col(o, "color", L.color); col(o, "color2", L.color2); num(o, "flash", L.flash); col(o, "outline", L.outline);
                num(o, "outlineW", L.outlineW); num(o, "glow", L.glow); break;
            case Kind::Banner: num(o, "y", L.y); num(o, "period", L.period); num(o, "fly", L.fly); num(o, "h", L.h); num(o, "size", L.size); choice(o, "text", L.text);
                str(o, "plane", L.plane); str(o, "font", L.font); num(o, "planeH", L.planeH); col(o, "fill", L.color); col(o, "edge", L.color2); col(o, "ink", L.color3);
                needFile(where, L.plane);
                if (L.text.cases.empty()) { warn(where, "a banner needs 'text' (layer skipped)"); return; } break;
            case Kind::Confetti: num(o, "count", L.count); num(o, "seed", L.seed); cols(o, "colors", L.colors); num(o, "speed", L.speed); num(o, "size", L.size); num(o, "sway", L.sway); break;
            case Kind::Shade: col(o, "color", L.color); num(o, "alpha", L.alpha); col(o, "color2", L.color2); num(o, "alpha2", L.alpha2); break;
            case Kind::Line: num(o, "x", L.x); num(o, "y", L.y); num(o, "x2", L.x2); num(o, "y2", L.y2); num(o, "thick", L.thick); col(o, "color", L.color); break;
            case Kind::Button: {
                num(o, "x", L.x); num(o, "y", L.y); num(o, "w", L.w); num(o, "h", L.h); num(o, "radius", L.radius); col(o, "color", L.color); col(o, "color2", L.color2);
                num(o, "to", L.to); num(o, "hoverTo", L.hoverTo); str(o, "set", L.set); str(o, "hover", L.hover); str(o, "key", L.key);
                auto slotOf = [&](const std::string& name, bool wantHover) { for (const StateVar& sv : pk.states) if (sv.name == name && sv.hover == wantHover) return sv.slot; return -1; };
                if (!L.set.empty()) { L.slotSet = slotOf(L.set, false); if (L.slotSet < 0) warn(where, "'set' names '" + L.set + "', which is not in \"state\" (it will do nothing)"); }
                if (!L.hover.empty()) { L.slotHover = slotOf(L.hover, true); if (L.slotHover < 0) warn(where, "'hover' names '" + L.hover + "', which is not in \"hover\" (it will do nothing)"); }
                if (!L.key.empty()) { L.key = lower(L.key); if (L.key != "tab" && L.key != "space" && L.key != "enter" && L.key != "up" && L.key != "down") { warn(where, "'key' can be tab, space, enter, up or down (ignored)"); L.key.clear(); } }
            } break;
            case Kind::Map: {
                str(o, "src", L.src); num(o, "x", L.x); num(o, "y", L.y); num(o, "w", L.w); col(o, "color", L.color); col(o, "color2", L.color2); num(o, "thick", L.thick); num(o, "glow", L.glow);
                if (const mj::Value* gv = o.get("countries")) {
                    if (gv->type == mj::Value::Array) { for (auto& e : gv->arr) if (e.type == mj::Value::String && L.groups.size() < 400) L.groups.push_back(e.str); }
                    else warn(where, "'countries' should be a list of texts like [\"554\", \"036\"]");
                }
                if (L.src.empty()) { warn(where, "a map needs 'src' (layer skipped)"); return; }
                if (L.groups.empty()) L.groups.push_back("*");
                needFile(where, L.src);
            } break;
            case Kind::Traffic: {
                str(o, "vehicle", L.vehicle); num(o, "count", L.count); num(o, "seed", L.seed); num(o, "y0", L.y0); num(o, "y1", L.y1); num(o, "period", L.period); num(o, "size", L.size);
                num(o, "trail", L.trail); num(o, "dir", L.dir); num(o, "active", L.active); num(o, "fade", L.fade); num(o, "speed", L.speed); num(o, "fly", L.fly);
                col(o, "color", L.color); col(o, "color2", L.color2); col(o, "color3", L.color3);
                L.vehicle = lower(L.vehicle);
                if (L.vehicle != "jet" && L.vehicle != "boat" && L.vehicle != "heli") { warn(where, "a traffic layer needs 'vehicle': \"jet\", \"boat\" or \"heli\" (layer skipped)"); return; }
            } break;
        }
        finish(o);
        out.push_back(std::move(L));
    }

    // does anything in this scene move? (a formula that reads t, state timers X_t, or any user var derived from them, or an inherently animated kind)
    std::vector<bool> animVars() const {
        std::vector<bool> a(pk.vars.names.size(), false);
        if (!a.empty()) a[0] = true;                                      // slot 0 = 't'
        for (const StateVar& sv : pk.states) if (sv.slotT >= 0 && (size_t)sv.slotT < a.size()) a[(size_t)sv.slotT] = true;
        for (const auto& uv : pk.userVars) {
            if (!uv.second || uv.first < 0 || (size_t)uv.first >= a.size()) continue;
            for (size_t i = 0; i < a.size(); ++i) if (a[i] && uv.second->uses((int)i)) { a[(size_t)uv.first] = true; break; }
        }
        return a;
    }
    static bool numAnimated(const Num& n, const std::vector<bool>& a) {
        if (!n.prog) return false;
        for (size_t i = 0; i < a.size(); ++i) if (a[i] && n.prog->uses((int)i)) return true;
        return false;
    }
    static bool layerAnimated(const Layer& L, const std::vector<bool>& a) {
        if (L.kind == Kind::Banner || L.kind == Kind::Confetti || L.kind == Kind::Traffic) return true;
        if (L.kind == Kind::Stars && (L.speed.prog || L.speed.eval(nullptr, 0.5) != 0.0)) return true;
        if (L.kind == Kind::Streaks) return true;
        if (L.kind == Kind::Shapes && (L.flash.prog || (L.flash.set && L.flash.c != 0.0))) return true;
        const Num* all[] = {&L.when, &L.opacity, &L.x, &L.y, &L.x2, &L.y2, &L.w, &L.w2, &L.h, &L.r, &L.rx, &L.ry, &L.size, &L.size2, &L.cap, &L.rotate, &L.thick, &L.thick2,
                            &L.count, &L.seed, &L.speed, &L.lit, &L.flash, &L.gap, &L.maxw, &L.tracking, &L.radius, &L.period, &L.fly, &L.y0, &L.y1, &L.sway, &L.fade,
                            &L.sides, &L.planeH, &L.alpha, &L.alpha2, &L.neonStrength, &L.outlineW, &L.glow, &L.to, &L.hoverTo, &L.trail, &L.dir, &L.active};
        for (const Num* n : all) if (numAnimated(*n, a)) return true;
        for (const TextCase& tc : L.text.cases) {
            if (numAnimated(tc.when, a)) return true;
            for (const Tpl& t : tc.texts) for (const TextPiece& p : t.pieces) if (p.type == TextPiece::Var && p.var >= 0 && (size_t)p.var < a.size() && a[(size_t)p.var]) return true;
        }
        return false;
    }

    // "backdrop": { "fps": 15, "layers": [ ... ] }   or just the list of layers
    void loadScene(const mj::Value* v, Scene& sc, const std::string& name, int defaultFps) {
        if (!v) return;
        const mj::Value* list = nullptr; int fps = -1;
        if (v->type == mj::Value::Array) list = v;
        else if (v->type == mj::Value::Object) {
            list = v->get("layers");
            if (const mj::Value* f = v->get("fps")) if (f->type == mj::Value::Number) fps = (int)std::max(0.0, std::min(60.0, f->num));
            Obj so(*v, name); so.used.insert("layers"); so.used.insert("fps"); so.used.insert("name"); so.used.insert("id"); finish(so);
        } else { warn(name, "should be an object { \"layers\": [...] } or a list of layers"); return; }
        if (!list || list->type != mj::Value::Array) { warn(name, "has no \"layers\" list"); return; }
        if (list->arr.size() > 700) warn(name, "has more than 700 layers: only the first 700 are used");
        for (size_t i = 0; i < list->arr.size() && i < 700; ++i) loadLayer(list->arr[i], (int)i, sc.layers, name);
        const std::vector<bool> av = animVars();
        bool anim = false; for (const Layer& L : sc.layers) if (layerAnimated(L, av)) anim = true;
        sc.fps = fps >= 0 ? fps : (anim ? defaultFps : 0);
    }

    // ---- the whole file
    void loadPalette(const mj::Value* v) {
        if (!v) return;
        if (v->type != mj::Value::Object) { warn("palette", "should be an object { \"bgTop\": \"#...\", ... }"); return; }
        Obj o(*v, "palette");
        auto set = [&](const char* key, Color& dst) {
            Col c;
            if (!col(o, key, c)) return;
            if (c.src != Col::Fixed) { warn("palette", std::string("'") + key + "' must be a #RRGGBB colour, not a palette reference"); return; }
            dst = c.c;
        };
        set("bgTop", pk.pal.bgTop); set("bgBottom", pk.pal.bgBottom); set("ink", pk.pal.ink); set("ink2", pk.pal.ink2); set("ink3", pk.pal.ink3);
        set("line", pk.pal.line); set("accent", pk.pal.accent); set("accent2", pk.pal.accent2); set("panel", pk.pal.panel); set("plate", pk.pal.plate);
        { Num n; if (num(o, "starVis", n)) { if (n.prog) warn("palette", "'starVis' must be a plain number"); else pk.pal.starVis = (float)std::max(0.0, std::min(1.0, n.c)); } }
        { Num n; if (num(o, "neon", n)) { if (n.prog) warn("palette", "'neon' must be a plain number"); else pk.pal.neon = (float)std::max(0.0, std::min(1.0, n.c)); } }
        bool b; if (boolean(o, "dark", b)) pk.pal.dark = b;
        set("neonColor", pk.neonColor);
        {   // the sky map on the Sky face: "skyTop" overhead, "skyHorizon" at the rim. Not given: made from the page's own colours (a touch lighter on a dark
            // page, a touch darker on a light one), so the map always matches the theme
            const Color m = pk.pal.dark ? Color{1.f, 1.f, 1.f, 1.f} : Color{0.f, 0.f, 0.f, 1.f};
            Col cTop, cHor;
            if (col(o, "skyTop", cTop) && cTop.src == Col::Fixed) pk.pal.skyZen = cTop.c; else { pk.pal.skyZen = gfx::lerp(pk.pal.bgTop, m, 0.10f); pk.pal.skyZen.a = 1.f; }
            if (col(o, "skyHorizon", cHor) && cHor.src == Col::Fixed) pk.pal.skyHor = cHor.c; else { pk.pal.skyHor = gfx::lerp(pk.pal.bgBottom, m, 0.16f); pk.pal.skyHor.a = 1.f; }
            // at night the same sky, deeper (a pack can leave this to the sky map: it needs no setting of its own)
            auto deep = [](Color k) { return Color{k.r * 0.45f, k.g * 0.45f, k.b * 0.50f + 0.01f, 1.f}; };
            pk.pal.skyNZen = deep(pk.pal.skyZen); pk.pal.skyNHor = deep(pk.pal.skyHor);
        }
        finish(o);
    }

    void loadPack(const mj::Value& root) {
        Obj o(root, "theme.json");
        // identity
        str(o, "id", pk.id); str(o, "name", pk.name); str(o, "author", pk.author); str(o, "description", pk.description);
        { Num n; if (num(o, "version", n)) pk.version = (int)n.c; }
        str(o, "starts", pk.startsStr); str(o, "expires", pk.expiresStr);
        boolean(o, "weather", pk.weather);                     // true: the light of the weather (cloud, fog) is allowed to tint this pack (default false: its colours never change)
        boolean(o, "global", pk.global);                       // true: can also be picked as a theme for every face (default false: only its own tab(s))
        boolean(o, "yearly", pk.yearly);                       // true: the starts/expires window comes round every year (dates may be written MM-DD)
        {
            int m = 0, d = 0;
            auto okDate = [&](const std::string& s) { return parseLocal(s, 0) > 0 || (pk.yearly && monthDayOf(s, m, d)); };
            if (!pk.startsStr.empty() && !okDate(pk.startsStr)) { warn("theme.json", pk.yearly ? "'starts' is not a date (use YYYY-MM-DD or MM-DD): ignored" : "'starts' is not a date (use YYYY-MM-DD, and a day that exists): ignored"); pk.startsStr.clear(); }
            if (!pk.expiresStr.empty() && !okDate(pk.expiresStr)) { warn("theme.json", pk.yearly ? "'expires' is not a date (use YYYY-MM-DD or MM-DD): ignored" : "'expires' is not a date (use YYYY-MM-DD, and a day that exists): ignored"); pk.expiresStr.clear(); }
            if (pk.yearly && (pk.startsStr.empty() || pk.expiresStr.empty())) { warn("theme.json", "'yearly' needs both 'starts' and 'expires' (it is ignored)"); pk.yearly = false; }
        }
        // palette: start from a built-in one, then change what the file says
        std::string base = "night"; str(o, "base", base); base = lower(base);
        pk.pal = base == "day" ? gfx::PAL_DAY : base == "sunlit" ? gfx::PAL_SUNLIT : base == "sunlit2" ? gfx::PAL_SUNLIT2 : base == "midnight" ? gfx::PAL_MIDNIGHT : base == "galaxy" ? gfx::PAL_GALAXY : base == "dawn" ? gfx::PAL_DAWN : base == "sunset" ? gfx::PAL_SUNSET : base == "dusk" ? gfx::PAL_DUSK : base == "ember" ? gfx::PAL_EMBER : gfx::PAL_NIGHT;
        if (base != "day" && base != "sunlit" && base != "sunlit2" && base != "midnight" && base != "galaxy" && base != "dawn" && base != "sunset" && base != "dusk" && base != "night" && base != "ember") warn("theme.json", "'base' should be day, sunlit, sunlit2, dawn, sunset, dusk, night, midnight, galaxy or ember (using night)");
        pk.pal.neon = 0.f;
        loadPalette(o.get("palette"));
        // fonts
        if (const mj::Value* f = o.get("fonts")) {
            if (f->type != mj::Value::Object) warn("fonts", "should be an object");
            else {
                Obj fo(*f, "fonts");
                for (int i = 0; i < gfx::F_COUNT; ++i) { std::string rel; if (str(fo, ROLE[i], rel)) { pk.fontFile[i] = rel; needFile("fonts", rel); } }
                finish(fo);
            }
        }
        // the variables formulas can use
        for (const char* n : BASE_VARS) pk.vars.add(n);
        if (const mj::Value* cd = o.get("countdowns")) {
            if (cd->type != mj::Value::Object) warn("countdowns", "should be an object like { \"release\": \"2026-11-19 00:00\" }");
            else for (auto& kv : cd->obj) {
                if (pk.countdowns.size() >= 32) { warn("countdowns", "only the first 32 countdowns are used"); break; }
                Countdown c; c.id = kv.first;
                if (!identOk(c.id)) { warn("countdowns", "'" + c.id + "' is not a usable name (letters, digits and _ only)"); continue; }
                if (kv.second.type == mj::Value::String) c.at = kv.second.str;
                else if (kv.second.type == mj::Value::Object) {
                    if (const mj::Value* a = kv.second.get("at")) if (a->type == mj::Value::String) c.at = a->str;
                    if (const mj::Value* u = kv.second.get("utc")) { if (u->type == mj::Value::Number && std::fabs(u->num) <= 14.0) { c.hasUtc = true; c.utc = u->num; } else warn("countdowns", "'" + c.id + "': 'utc' should be a number of hours from -14 to 14"); }
                }
                int m = 0, d = 0, hh = 0, mi = 0;
                if (parseLocal(c.at, 0) <= 0 && !(pk.yearly && monthDayTime(c.at, m, d, hh, mi))) { warn("countdowns", "'" + c.id + "' has no usable date (use \"YYYY-MM-DD HH:MM\", with a day that exists)"); continue; }
                // guard against duplicate countdown IDs (which would otherwise leave firstVar out-of-bounds of pk.vars)
                bool dup = false;
                for (const char* sfx : CD_SUFFIX) if (pk.vars.find(c.id + "_" + sfx) >= 0) dup = true;
                if (dup) { warn("countdowns", "duplicate countdown '" + c.id + "' ignored"); continue; }
                c.firstVar = (int)pk.vars.names.size();
                for (const char* sfx : CD_SUFFIX) pk.vars.add(c.id + "_" + sfx);
                pk.countdowns.push_back(c);
            }
        }
        // numbers the viewer changes by clicking, and numbers that follow the pointer:  "state": { "view": 0 }   "hover": { "hov": -1 }
        for (int pass = 0; pass < 2; ++pass) {
            const char* key = pass == 0 ? "state" : "hover";
            const mj::Value* sv = o.get(key);
            if (!sv) continue;
            if (sv->type != mj::Value::Object) { warn(key, "should be an object like { \"view\": 0 }"); continue; }
            for (auto& kv : sv->obj) {
                if (pk.states.size() >= 40) { warn(key, "only the first 40 names are used"); break; }
                if (!identOk(kv.first)) { warn(key, "'" + kv.first + "' is not a usable name (letters, digits and _ only)"); continue; }
                if (kv.second.type != mj::Value::Number) { warn(key, "'" + kv.first + "' should be a number (its start value)"); continue; }
                if (pk.vars.find(kv.first) >= 0 || (pass == 0 && pk.vars.find(kv.first + "_t") >= 0)) { warn(key, "'" + kv.first + "' is already a name: pick another"); continue; }
                StateVar st; st.name = kv.first; st.def = kv.second.num; st.hover = pass == 1;
                st.slot = pk.vars.add(kv.first);
                if (!st.hover) st.slotT = pk.vars.add(kv.first + "_t");       // seconds since it last changed (animate a change with it)
                pk.states.push_back(st);
            }
        }
        pk.userVarFirst = (int)pk.vars.names.size();
        if (const mj::Value* uv = o.get("vars")) {
            if (uv->type != mj::Value::Object) warn("vars", "should be an object like { \"horizon\": \"portrait ? 0.8 : 0.81\" }");
            else {
                Obj vo(*uv, "vars");
                int count = 0;
                for (auto& kv : uv->obj) {
                    vo.used.insert(kv.first);
                    if (++count > 200) { warn("vars", "only the first 200 variables are used"); break; }
                    if (!identOk(kv.first)) { warn("vars", "'" + kv.first + "' is not a usable name"); continue; }
                    if (pk.vars.find(kv.first) >= 0) { warn("vars", "'" + kv.first + "' is already a name (a built-in variable or a countdown part): pick another"); continue; }
                    auto prog = std::make_shared<expr::Program>(); std::string err; bool ok = false;
                    if (kv.second.type == mj::Value::Number) { const std::string t = util::fmt("%.17g", kv.second.num); ok = prog->compile(t, pk.vars, err); }
                    else if (kv.second.type == mj::Value::String) ok = prog->compile(kv.second.str, pk.vars, err);
                    else err = "should be a number or a formula";
                    if (!ok) { warn("vars", "'" + kv.first + "': " + err); continue; }
                    const int slot = pk.vars.add(kv.first);          // added after compiling, so a variable cannot refer to itself
                    pk.userVars.push_back({slot, prog});
                }
            }
        }
        // scenes and extra tabs
        loadScene(o.get("backdrop"), pk.backdrop, "backdrop", 15);
        loadScene(o.get("backdropLite"), pk.backdropLite, "backdropLite", 15);
        if (const mj::Value* fs_ = o.get("faces")) {
            if (fs_->type != mj::Value::Array) warn("faces", "should be a list of { \"id\", \"name\", \"layers\" }");
            else for (size_t i = 0; i < fs_->arr.size(); ++i) {
                if (i >= 16) { warn("faces", "only the first 16 tabs are used"); break; }
                const mj::Value& fv = fs_->arr[i];
                Face f;
                if (fv.type == mj::Value::Object) {
                    if (const mj::Value* a = fv.get("id")) if (a->type == mj::Value::String) f.id = a->str;
                    if (const mj::Value* a = fv.get("name")) if (a->type == mj::Value::String) f.name = a->str;
                }
                if (f.id.empty()) f.id = "face" + std::to_string(i + 1);
                if (f.name.empty()) f.name = f.id;
                loadScene(&fv, f.scene, "face '" + f.id + "'", 24);
                pk.faces.push_back(std::move(f));
            }
        }
        finish(o);
    }
};

// ================================================================ 3. finding packs
std::string sanitizeId(const std::string& s) {
    std::string o;
    for (char c : s) { if ((c >= 'A' && c <= 'Z')) c += 32; if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_') o += c; else if (c == ' ') o += '-'; }
    return o;
}

fs::path manifestOf(const fs::path& dir) {
    std::error_code ec;
    for (const char* n : {"theme.json", "theme.jsonc"}) if (fs::is_regular_file(dir / n, ec)) return dir / n;
    return {};
}

// ---- safe start
// A pack that makes Meridian crash while it is being read would otherwise crash it again at every start. So the pack's name, with the
// size and time of its theme.json, is written to "themes-loading.txt" before it is read and removed after. If Meridian dies in between,
// the line is still there at the next start: it is moved to "themes-skipped.txt", and a pack on that list is not loaded again until
// its theme.json changes (a different size or time is a different line), so fixing the file is all it takes.
fs::path g_stateDir;
std::set<std::string> g_skipped;
bool g_stateRead = false;

std::string packStamp(const fs::path& dir) {
    std::error_code ec1, ec2;
    const fs::path mf = manifestOf(dir);
    std::string s = dir.filename().u8string() + "|";
    if (mf.empty()) return s + "0|0";
    const auto sz = (unsigned long long)fs::file_size(mf, ec1);
    const auto mt = (long long)fs::last_write_time(mf, ec2).time_since_epoch().count();
    return s + std::to_string(ec1 ? 0ULL : sz) + "|" + std::to_string(ec2 ? 0LL : mt);
}
std::vector<std::string> lines(const fs::path& p) {
    std::vector<std::string> v; std::ifstream f(p, std::ios::binary); std::string l;
    while (v.size() < 200 && std::getline(f, l)) { while (!l.empty() && (l.back() == '\r' || l.back() == '\n')) l.pop_back(); if (!l.empty() && l.size() < 600) v.push_back(l); }
    return v;
}
void writeLines(const fs::path& p, const std::vector<std::string>& v) {
    std::string out; for (const std::string& l : v) out += l + "\n";
    std::ofstream f(p, std::ios::binary | std::ios::trunc); f << out;
}
void readSafeStartState() {
    if (g_stateRead || g_stateDir.empty()) return;
    g_stateRead = true;
    std::error_code ec;
    const fs::path loading = g_stateDir / "themes-loading.txt", skipped = g_stateDir / "themes-skipped.txt";
    std::vector<std::string> sk = lines(skipped);
    const std::vector<std::string> left = lines(loading);
    if (!left.empty()) {                                      // the last start ended while this pack was being read
        for (const std::string& l : left) if (std::find(sk.begin(), sk.end(), l) == sk.end()) sk.push_back(l);
        while (sk.size() > 50) sk.erase(sk.begin());
        writeLines(skipped, sk);
    }
    fs::remove(loading, ec);
    g_skipped.clear(); for (const std::string& l : sk) g_skipped.insert(l);
}
bool markLoading(const std::string& stamp) {
    if (g_stateDir.empty()) return false;
    std::error_code ec; fs::create_directories(g_stateDir, ec);
    writeLines(g_stateDir / "themes-loading.txt", {stamp});
    return true;
}
void markLoaded() { std::error_code ec; fs::remove(g_stateDir / "themes-loading.txt", ec); }

std::unique_ptr<Pack> loadPackFolder(const fs::path& dir) {
    auto pk = std::make_unique<Pack>();
    pk->dir = dir;
    const std::string rawFolder = dir.filename().u8string();
    pk->id = sanitizeId(rawFolder);
    if (pk->id.empty()) {
        unsigned h = 2166136261u;
        for (unsigned char c : rawFolder) h = (h ^ c) * 16777619u;
        pk->id = util::fmt("pack-%08x", h);
    }
    pk->name = rawFolder;
    const fs::path mf = manifestOf(dir);
    std::string text;
    if (mf.empty()) { pk->error = "no theme.json in this folder"; return pk; }
    const std::string stamp = packStamp(dir);
    if (g_skipped.count(stamp)) { pk->error = "Meridian stopped working while loading this pack, so it was skipped. Save a change to its theme.json to try it again"; return pk; }
    const Read rd = readText(mf, text, kMaxManifestBytes);
    if (rd == Read::TooBig) { pk->error = "theme.json is too big (over 1 MB)"; return pk; }
    if (rd != Read::Ok) { pk->error = "theme.json could not be read"; return pk; }
    struct Marker { bool on; Marker(const std::string& s) : on(markLoading(s)) {} ~Marker() { if (on) markLoaded(); } } marker(stamp);   // see "safe start" below
    const std::string clean = cleanJson(text);
    mj::Value root; std::string err; size_t at = 0;
    if (!mj::parseAt(clean, root, err, at)) {
        int line = 1; for (size_t i = 0; i < at && i < clean.size(); ++i) if (clean[i] == '\n') ++line;
        pk->error = "theme.json, line " + std::to_string(line) + ": " + err;
        return pk;
    }
    if (root.type != mj::Value::Object) { pk->error = "theme.json should start with { and hold an object"; return pk; }
    const std::string folderId = pk->id, folderName = pk->name;
    Loader L(*pk);
    L.loadPack(root);
    pk->id = sanitizeId(pk->id);                                      // an "id" in the file wins over the folder name
    if (pk->id.empty()) pk->id = folderId;
    if (pk->name.empty()) pk->name = folderName;
    pk->ok = true;
    return pk;
}

// A short fingerprint of theme pack manifests and asset folders (names, sizes, times) — skips _source/hidden folders to stay fast.
std::string signatureNow() {
    std::string sig;
    std::error_code ec;
    int budget = 1000;
    for (const fs::path& root : g_roots) {
        sig += root.u8string() + "|";
        if (!fs::is_directory(root, ec)) continue;
        for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; it != end && budget > 0; it.increment(ec), --budget) {
            if (ec) break;
            const std::string fn = it->path().filename().u8string();
            if (it->is_directory(ec) && !fn.empty() && (fn[0] == '_' || fn[0] == '.')) { it.disable_recursion_pending(); continue; }
            const fs::path& p = it->path();
            sig += p.lexically_relative(root).u8string();
            std::error_code ec1, ec2, ec3;
            if (it->is_regular_file(ec1) && !ec1) {
                const auto sz = (unsigned long long)it->file_size(ec2);
                const auto mt = (long long)it->last_write_time(ec3).time_since_epoch().count();
                sig += ":" + std::to_string(ec2 ? 0ULL : sz) + ":" + std::to_string(ec3 ? 0LL : mt);
            }
            sig += ";";
        }
    }
    return sig;
}

} // namespace

// ================================================================ public: dates
double parseLocal(const std::string& s, int tzOff) {
    int y = 0, m = 0, d = 0, hh = 0, mm = 0;
    if (!util::parseDateTimeStrict(s, y, m, d, hh, mm) || y < 2000) return 0;
    return astro::daysFromCivil(y, m, d) * 86400.0 + hh * 3600.0 + mm * 60.0 - tzOff;
}


Pack::Window Pack::window(double now, int tzOff) const {
    Window w;
    if (!yearly) {
        w.start = startsStr.empty() ? 0 : parseLocal(startsStr, tzOff); w.end = expiresStr.empty() ? 0 : parseLocal(expiresStr, tzOff);
        w.startText = startsStr; w.endText = expiresStr;
        return w;
    }
    int sm = 0, sd = 0, em = 0, ed = 0;
    if (!monthDayOf(startsStr, sm, sd) || !monthDayOf(expiresStr, em, ed)) return w;
    const int year = util::toLocal(now, tzOff).y;
    double best = 0, bestEnd = 0;
    for (int y = year - 1; y <= year + 1; ++y) {                         // the window that starts in each of these years (it may run into the next year)
        const double a = instantOf(y, sm, sd, tzOff);
        const double b = instantOf(((em > sm || (em == sm && ed >= sd)) ? y : y + 1), em, ed, tzOff);
        if (now >= a && now < b) { best = a; bestEnd = b; break; }      // the one that is on now
        if (a > now) { best = a; bestEnd = b; break; }                  // else the very next upcoming window
        best = a; bestEnd = b;                                          // fallback: most recent past window
    }
    w.start = best; w.end = bestEnd; w.startText = dateText(best, tzOff); w.endText = dateText(bestEnd, tzOff);
    return w;
}

double Pack::countdownAt(const Countdown& cd, double now, int tzOff) const {
    if (cd.hasUtc) tzOff = (int)std::lround(cd.utc * 3600.0);           // the date is the local time of that place, wherever the clock is
    if (const double fixedDate = parseLocal(cd.at, tzOff); fixedDate > 0) return fixedDate;
    int m = 0, d = 0, hh = 0, mi = 0;
    if (!yearly || !monthDayTime(cd.at, m, d, hh, mi)) return 0;
    // a date without a year: the first time it comes round after the pack's window starts (so a Halloween countdown always counts to this October)
    const Window w = window(now, tzOff);
    const int y = util::toLocal(w.start > 0 ? w.start + 1.0 : now, tzOff).y;
    double at = instantOf(y, m, d, tzOff) + hh * 3600.0 + mi * 60.0;
    if (w.start > 0 && at < w.start) at = instantOf(y + 1, m, d, tzOff) + hh * 3600.0 + mi * 60.0;
    return at;
}

bool Pack::live(double now, int tzOff) const {
    if (!ok || disabled) return false;
    if (startsStr.empty() && expiresStr.empty()) return true;
    const Window w = window(now, tzOff);
    if (w.start > 0 && now < w.start) return false;
    if (w.end > 0 && now >= w.end) return false;
    return true;
}

// ================================================================ public: registry
void setRoots(const std::vector<fs::path>& r) { g_roots = r; }
const std::vector<fs::path>& roots() { return g_roots; }
bool changed() { return signatureNow() != g_sig; }
const std::vector<std::unique_ptr<Pack>>& all() { return g_packs; }
Pack* find(const std::string& id) { for (auto& p : g_packs) if (p->id == id) return p.get(); return nullptr; }
std::string faceKey(const Pack& p, const Face& f) { return p.id + "/" + f.id; }
bool findFace(const std::string& key, Pack*& pack, Face*& face) {
    const size_t sl = key.find('/');
    if (sl == std::string::npos) return false;
    Pack* p = find(key.substr(0, sl));
    if (!p || !p->ok) return false;
    const std::string fid = key.substr(sl + 1);
    for (Face& f : p->faces) if (f.id == fid) { pack = p; face = &f; return true; }
    return false;
}

void setStateDir(const fs::path& dir) { g_stateDir = dir; g_stateRead = false; }

void rescan(gfx::Gfx* g) {
    readSafeStartState();
    if (g) { syncFonts(*g, nullptr); release(*g); }
    ++g_gen;
    g_packs.clear();
    std::set<std::string> seen;
    std::error_code ec;
    for (const fs::path& root : g_roots) {
        if (!fs::is_directory(root, ec)) continue;
        std::vector<fs::path> dirs;
        for (fs::directory_iterator it(root, ec), end; it != end; it.increment(ec)) {
            if (ec) break;
            const std::string nm = it->path().filename().u8string();
            if (it->is_directory(ec) && !nm.empty() && nm[0] != '_' && nm[0] != '.') dirs.push_back(it->path());   // folders starting with _ or . are ignored (templates, disabled packs)
        }
        std::sort(dirs.begin(), dirs.end());
        for (const fs::path& d : dirs) {
            auto pk = loadPackFolder(d);
            if (seen.count(pk->id)) { pk->ok = false; pk->error = "another pack with the id '" + pk->id + "' was found first"; pk->id += "#" + std::to_string(g_packs.size()); }
            seen.insert(pk->id);
            g_packs.push_back(std::move(pk));
        }
    }
    std::stable_sort(g_packs.begin(), g_packs.end(), [](const std::unique_ptr<Pack>& a, const std::unique_ptr<Pack>& b) { return lower(a->name) < lower(b->name); });
    g_sig = signatureNow();
}

// ================================================================ 4. pictures and fonts
const gfx::Bitmap* image(gfx::Gfx& g, Pack& p, const std::string& rel, bool allowLoad) {
    auto it = p.images.find(rel);
    if (it != p.images.end()) return it->second.bmp ? &it->second : nullptr;
    if (!allowLoad || p.imageFailed.count(rel)) return nullptr;
    if (!safeRelPath(rel)) { p.imageFailed[rel] = true; return nullptr; }
    { std::error_code ec; const auto sz = fs::file_size(p.dir / fs::u8path(rel), ec);
      if (!ec && sz > kMaxPictureBytes) { p.imageFailed[rel] = true; p.warnings.push_back("the picture " + rel + " is too big (over 32 MB)"); return nullptr; } }
    // If other inactive packs have filled the art budget, evict their decoded images first so switching packs stays clean.
    if (g_artBytes > kMaxArtBytes / 2) {
        for (auto& op : g_packs) {
            if (op.get() == &p || op->images.empty()) continue;
            for (auto& kv : op->images) {
                if (kv.second.bmp) g_artBytes -= std::min(g_artBytes, (size_t)kv.second.w * kv.second.h * 4);
                g.freeBitmap(kv.second);
            }
            op->images.clear();
        }
    }
    // All the pictures of all packs together may use only kMaxArtBytes of memory once decoded (a single 8192 x 8192 picture alone is 256 MB).
    bool tooLarge = false;
    gfx::Bitmap b = g.loadImage((p.dir / fs::u8path(rel)).wstring(), kMaxArtBytes > g_artBytes ? kMaxArtBytes - g_artBytes : 1, &tooLarge);
    if (!b.bmp) {
        p.imageFailed[rel] = true;
        p.warnings.push_back(tooLarge ? "the picture " + rel + " was left out: the pictures of a theme may use only 192 MB of memory between them once opened (make them smaller)"
                                      : "could not read the picture " + rel + " (is it a PNG, JPG, BMP or GIF?)");
        return nullptr;
    }
    g_artBytes += (size_t)b.w * b.h * 4;
    p.images[rel] = b;
    return &p.images[rel];
}

// A map file: { "aspect": 2.4, "groups": [ { "id": "554", "rings": [ [x, y, x, y, ...], ... ] }, ... ] }   x and y run 0..1 over the map's box.
const MapData* mapData(Pack& p, const std::string& rel) {
    auto it = p.maps.find(rel);
    if (it != p.maps.end()) return it->second.get();
    std::shared_ptr<MapData> md;
    std::string bytes;
    const Read r = !safeRelPath(rel) ? Read::Missing : readText(p.dir / fs::u8path(rel), bytes, kMaxMapBytes);
    if (r != Read::Ok) p.warnings.push_back(r == Read::TooBig ? "the map " + rel + " is too big (over 4 MB)" : "could not read the map " + rel);
    else {
        mj::Value root; std::string err;
        if (!mj::parse(bytes, root, err) || root.type != mj::Value::Object) p.warnings.push_back("the map " + rel + " is not valid JSON: " + err);
        else {
            md = std::make_shared<MapData>();
            if (const mj::Value* a = root.get("aspect")) if (a->type == mj::Value::Number && a->num > 0.2 && a->num < 20.0) md->aspect = a->num;
            const mj::Value* gs = root.get("groups");
            size_t points = 0;
            if (gs && gs->type == mj::Value::Array) for (const mj::Value& gv : gs->arr) {
                if (gv.type != mj::Value::Object || md->groups.size() >= 1000) continue;
                MapData::Group g;
                if (const mj::Value* id = gv.get("id")) if (id->type == mj::Value::String) g.id = id->str;
                if (const mj::Value* op = gv.get("open")) g.open = (op->type == mj::Value::Bool && op->b) || (op->type == mj::Value::Number && op->num != 0);
                const mj::Value* rs = gv.get("rings");
                if (g.id.empty() || !rs || rs->type != mj::Value::Array) continue;
                g.x0 = g.y0 = 1e9f; g.x1 = g.y1 = -1e9f;
                for (const mj::Value& rv : rs->arr) {
                    if (rv.type != mj::Value::Array || rv.arr.size() < 4 || points > 400000) continue;
                    std::vector<gfx::Pt> ring; ring.reserve(rv.arr.size() / 2);
                    for (size_t i = 0; i + 1 < rv.arr.size(); i += 2) {
                        if (rv.arr[i].type != mj::Value::Number || rv.arr[i + 1].type != mj::Value::Number) continue;
                        const double dx = rv.arr[i].num, dy = rv.arr[i + 1].num;
                        if (!std::isfinite(dx) || !std::isfinite(dy) || std::fabs(dx) > 1e6 || std::fabs(dy) > 1e6) continue;
                        ring.push_back({(float)dx, (float)dy});
                    }
                    points += ring.size();
                    if (ring.size() >= 2) {
                        for (const auto& pt : ring) { g.x0 = std::min(g.x0, pt.x); g.x1 = std::max(g.x1, pt.x); g.y0 = std::min(g.y0, pt.y); g.y1 = std::max(g.y1, pt.y); }
                        g.rings.push_back(std::move(ring));
                    }
                }
                if (!g.rings.empty()) md->groups.push_back(std::move(g));
            }
            if (md->groups.empty()) { p.warnings.push_back("the map " + rel + " has no usable \"groups\""); md.reset(); }
        }
    }
    p.maps[rel] = md;
    return md.get();
}

static const Pack* g_fontPack = nullptr;
static unsigned g_fontGen = 0;

void syncFonts(gfx::Gfx& g, Pack* want) {
    if (want == g_fontPack && g_fontGen == g_gen) return;
    for (int i = 0; i < gfx::F_COUNT; ++i) g.clearFontOverride((gfx::FontId)i);
    g_fontPack = want; g_fontGen = g_gen;
    if (!want) return;
    for (int i = 0; i < gfx::F_COUNT; ++i) {
        if (want->fontFile[i].empty()) continue;
        const std::string& rel = want->fontFile[i];
        if (want->fontData[i].empty()) {
            std::string bytes;
            const Read fr = !safeRelPath(rel) ? Read::Missing : readText(want->dir / fs::u8path(rel), bytes, kMaxFontBytes);
            if (fr == Read::TooBig) { want->warnings.push_back(std::string("the font ") + rel + " is too big (over 32 MB)"); continue; }
            if (fr != Read::Ok || bytes.size() < 64) { want->warnings.push_back(std::string("could not read the font ") + rel); continue; }
            want->fontData[i].assign(bytes.begin(), bytes.end());
        }
        if (!g.setFontOverride((gfx::FontId)i, want->fontData[i].data(), want->fontData[i].size())) want->warnings.push_back(std::string("the font ") + rel + " could not be used (it must be a TrueType or OpenType file)");
    }
}

void release(gfx::Gfx& g) {
    for (auto& p : g_packs) {
        for (auto& kv : p->images) g.freeBitmap(kv.second);
        p->images.clear(); p->imageFailed.clear();
        auto freeScene = [&](Scene& sc) { for (Layer& L : sc.layers) { g.freeBitmap(L.sprite); L.spriteKey = 0; } };
        freeScene(p->backdrop); freeScene(p->backdropLite);
        for (Face& f : p->faces) freeScene(f.scene);
    }
    g_artBytes = 0;
}

} // namespace themes
