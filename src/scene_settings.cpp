// Meridian: the hidden settings panel. Opened from the dock or with the comma key.
// Immediate mode: every control draws itself and registers a click handler (App::addAction) for the frame.
#include "scene.h"
#include "saver.h"
#include <shellapi.h>
#include <cmath>
#include <cctype>
#include <chrono>
#include <algorithm>
#include <memory>

namespace app {

using namespace gfx;
using util::fmt;

void App::addAction(float x, float y, float w, float h, std::function<void()> fn) {
    actions.push_back(std::move(fn));
    hits.push_back({x, y, w, h, HIT_ACTION0 + (int)actions.size() - 1});
}

namespace {

// To add a settings tab: add its name to these three lists, write a tabXxx() function below, and add a case in renderSettings().
const int NTABS = 12;
const char* const TAB_NAMES[NTABS] = {"WIDGETS", "ELEMENTS", "THEMES", "ROTATION", "SKY MAP", "QUIET", "SKY EVENTS", "WEATHER", "PLACE", "HOLIDAYS", "AWAY", "ABOUT"};
const char* const TAB_TITLES[NTABS] = {"Widgets", "Elements", "Themes", "Rotation", "Sky map", "Quiet face", "Sky events", "Weather", "Place", "Holidays", "Away", "About"};
const char* const TAB_SUB[NTABS] = {"Choose what the Widgets face shows, and in which order.",
                                "Switch parts of the Sky face and of the window on or off.",
                                "Theme packs you dropped into the themes folder, and how the weather changes the light.",
                                "The Sky panel and the Widgets face take turns, with a transition between them.",
                                "Turn the sky: face any direction you like, or let it rotate freely.",
                                "Clouds, wind, rain, snow and storms drawn behind the clock.",
                                "Meteor showers, planets lining up and eclipses on the sky map.",
                                "Several free sources, tried in order until one of them answers.",
                                "Where the sky, the Sun and Moon times and the weather are for.",
                                "Whose public holidays to show: Sri Lanka, the UK, the US, or any mix.",
                                "Turn Meridian on when the PC is not used, and put the PC to sleep when nobody comes back.",
                                "Files, shortcuts and where the data comes from."};

bool parseNum(const std::string& s, double lo, double hi, double& out) {
    if (s.empty()) return false;
    char* e = nullptr; double v = std::strtod(s.c_str(), &e);
    if (e == s.c_str() || *e) return false;
    if (!(v >= lo && v <= hi)) return false;
    out = v; return true;
}
std::string coordText(const astro::Geo& g) {
    return fmt("%.2f\xC2\xB0%c  %.2f\xC2\xB0%c", std::fabs(g.lat), g.lat >= 0 ? 'N' : 'S', std::fabs(g.lon), g.lon >= 0 ? 'E' : 'W');
}
std::string cleanName(std::string n) {
    for (auto& ch : n) if (ch == '=' || ch == '\n' || ch == '\r') ch = '-';
    n = util::trim(n);
    while (!n.empty() && (n[0] == ';' || n[0] == '#' || n[0] == '[')) n.erase(0, 1);
    return n;
}
// "Today, 10:42 AM" or "Wed 28 Oct, 10:42 AM"
std::string stampText(const App& a, double t) {
    if (t <= 0) return "never";
    const util::LocalTime L = util::toLocal(t, a.tzOff), N = util::toLocal(a.now, a.tzOff);
    const std::string clk = util::fmtClock(t, a.tzOff, a.st.h24);
    if (L.y == N.y && L.m == N.m && L.d == N.d) return "Today, " + clk;
    return fmt("%s %d %s, ", util::WEEKDAYS3[L.wday], L.d, util::MONTHS3[L.m - 1]) + clk;
}
// "20 Dec 2026" from "2026-12-20"
std::string niceDate(const std::string& ymd) {
    int y = 0, m = 0, d = 0;
    if (std::sscanf(ymd.c_str(), "%d-%d-%d", &y, &m, &d) == 3 && m >= 1 && m <= 12) return fmt("%d %s %d", d, util::MONTHS3[m - 1], y);
    return ymd;
}
std::string agoText(double now, double t) {
    if (t <= 0) return "never";
    double d = std::max(0.0, now - t);
    if (d < 90) return "just now";
    if (d < 5400) return fmt("%d min ago", (int)std::lround(d / 60.0));
    if (d < 172800) return fmt("%d h ago", (int)std::lround(d / 3600.0));
    return fmt("%d days ago", (int)std::lround(d / 86400.0));
}

// What a click in a text box needs to find the character under it: the layout that was drawn this frame.
struct ClickMap {
    std::vector<std::wstring> lines;                   // the lines that are showing
    std::vector<size_t> startW;                        // where each of them starts in the whole text (UTF-16 units)
    std::vector<size_t> byteAt;                        // UTF-16 unit -> byte offset
    float x0 = 0, top = 0, lh = 0, fs = 0;
    Gfx* g = nullptr;
    size_t pick(float mx, float my) const {            // byte offset of the character boundary nearest the click
        if (lines.empty()) return 0;
        int li = lh > 0 ? (int)std::floor((my - top) / lh) : 0;
        li = std::max(0, std::min(li, (int)lines.size() - 1));
        const std::wstring& ln = lines[(size_t)li];
        const float rel = mx - x0;
        size_t lo = 0, hi = ln.size();                 // the longest start of the line that is still left of the click
        while (lo < hi) { const size_t mid = (lo + hi + 1) / 2; if (g->textWidth(F_LIGHT, fs, ln.substr(0, mid)) <= rel) lo = mid; else hi = mid - 1; }
        if (lo < ln.size() && rel - g->textWidth(F_LIGHT, fs, ln.substr(0, lo)) > g->textWidth(F_LIGHT, fs, ln.substr(0, lo + 1)) - rel) ++lo;
        return byteAt[std::min(startW[(size_t)li] + lo, byteAt.size() - 1)];
    }
};

struct UI {
    Ctx& c; App& a; Gfx& g; const Palette& P; float s;
    explicit UI(Ctx& cc) : c(cc), a(cc.a), g(cc.g), P(cc.P), s(cc.s) {}

    bool over(float x, float y, float w, float h) const { return a.mouseX >= x && a.mouseX <= x + w && a.mouseY >= y && a.mouseY <= y + h; }
    void act(float x, float y, float w, float h, std::function<void()> f) { a.addAction(x, y, w, h, std::move(f)); }
    float capMid(FontId f, float size, float mid) { return mid + g.capHeight(f, size) * 0.5f; }
    Color acc() const { return tint(P, P.accent); }

    void drawSwitch(float rx, float mid, bool on, uint64_t gkey = 0, bool hovered = false) {
        const float tw = 46 * s, th = 26 * s, x = rx - tw, y = mid - th * 0.5f;
        const uint64_t key = gkey ? gkey : a.nextActionGlassKey();
        const bool hv = hovered || over(x - 4 * s, y - 4 * s, tw + 8 * s, th + 8 * s);
        const glassfx::Pose gp = a.glass.pose(key, x, y, tw, th, th * 0.5f, s, hv, false);
        const glassfx::TogglePose tp = a.glass.stepToggle(key, on);
        const Color offTrack = mulA(P.ink, 0.16f + 0.06f * gp.hoverT);
        const Color onTrack  = withA(acc(), 0.86f + 0.12f * gp.hoverT);
        const Color trackCol = lerp(offTrack, onTrack, tp.pos);
        g.fillRoundRect(gp.x, gp.y, gp.w, gp.h, gp.r, trackCol);
        const Color rimCol = lerp(lerp(P.line, withA(acc(), 0.55f), gp.hoverT),
                                  withA(lerp(acc(), P.ink, 0.25f), 0.72f + 0.20f * gp.hoverT), tp.pos);
        g.strokeRoundRect(gp.x + 0.5f, gp.y + 0.5f, std::max(1.f, gp.w - 1.f), std::max(1.f, gp.h - 1.f), std::max(0.5f, gp.r - 0.5f), rimCol, std::max(1.f, s));
        drawGlassEffects(c, key, gp, tp.pos > 0.5f, false);
        // Liquid-glass gliding droplet knob: elongates smoothly while zipping across the pill track
        const float rBase = std::max(4.f, gp.h * 0.5f - 3.f * s);
        const float travel = std::max(0.f, gp.w - gp.h);
        const float kcx = gp.x + gp.h * 0.5f + travel * tp.pos;
        const float kcy = gp.y + gp.h * 0.5f;
        const float extraW = (6.5f * tp.stretch + 1.8f * gp.holdT) * s;
        const float kw = std::min(gp.w - 6.f * s, 2.f * rBase + extraW);
        const float kh = std::max(6.f, 2.f * rBase - 1.6f * tp.stretch * s);
        const float kx = std::max(gp.x + 3.f * s, std::min(gp.x + gp.w - 3.f * s - kw, kcx - kw * 0.5f));
        const float ky = kcy - kh * 0.5f;
        const Color knobCol = lerp(lerp(P.ink2, P.ink, gp.hoverT), P.plate, tp.pos);
        g.fillRoundRect(kx, ky + 1.f * s, kw, kh, kh * 0.5f, Color{0, 0, 0, P.dark ? 0.22f : 0.12f});
        g.fillRoundRect(kx, ky, kw, kh, kh * 0.5f, knobCol);
        // Crisp glass specular glint on the upper side of the droplet knob
        const Color glintC = tp.pos > 0.5f ? Color{1, 1, 1, P.dark ? 0.28f : 0.45f} : Color{1, 1, 1, P.dark ? 0.36f : 0.55f};
        g.glowEllipse(kx + kw * 0.42f, ky + kh * 0.30f, kw * 0.30f, kh * 0.22f, glintC, 0.75f);
    }

    // a labelled on/off row; returns its height
    float toggle(float x, float y, float w, const char* label, const char* hint, bool& v, std::function<void()> after = {}) {
        const float h = hint ? 62.f * s : 46.f * s;
        const bool hv = over(x - 10 * s, y, w + 20 * s, h);
        if (hv) g.fillRoundRect(x - 10 * s, y + 2 * s, w + 20 * s, h - 4 * s, 14 * s, mulA(P.ink, 0.05f));
        const float mid = y + h * 0.5f;
        const float lw = w - 66 * s;
        if (hint) {
            g.text(F_LIGHT, 19.5f * s, x, mid - 3 * s, g.ellipsize(F_LIGHT, 19.5f * s, Ws(label), lw), P.ink);
            g.text(F_LIGHT, 14.f * s, x, mid + 17 * s, g.ellipsize(F_LIGHT, 14.f * s, Ws(hint), lw), P.ink3);
        } else g.text(F_LIGHT, 19.5f * s, x, capMid(F_LIGHT, 19.5f * s, mid), g.ellipsize(F_LIGHT, 19.5f * s, Ws(label), lw), P.ink);
        const uint64_t gkey = a.nextActionGlassKey();
        drawSwitch(x + w, mid, v, gkey, hv);
        bool* vp = &v; App* ap = &a;
        act(x - 10 * s, y, w + 20 * s, h, [vp, ap, after] { *vp = !*vp; if (after) after(); ap->cmd(CMD_SAVE); });
        return h;
    }

    // like toggle(), for a state that is not one plain bool: flip() makes the change
    float toggleFn(float x, float y, float w, const char* label, const char* hint, bool on, std::function<void()> flip) {
        const float h = hint ? 62.f * s : 46.f * s;
        const bool hv = over(x - 10 * s, y, w + 20 * s, h);
        if (hv) g.fillRoundRect(x - 10 * s, y + 2 * s, w + 20 * s, h - 4 * s, 14 * s, mulA(P.ink, 0.05f));
        const float mid = y + h * 0.5f;
        const float lw = w - 66 * s;
        if (hint) {
            g.text(F_LIGHT, 19.5f * s, x, mid - 3 * s, g.ellipsize(F_LIGHT, 19.5f * s, Ws(label), lw), P.ink);
            g.text(F_LIGHT, 14.f * s, x, mid + 17 * s, g.ellipsize(F_LIGHT, 14.f * s, Ws(hint), lw), P.ink3);
        } else g.text(F_LIGHT, 19.5f * s, x, capMid(F_LIGHT, 19.5f * s, mid), g.ellipsize(F_LIGHT, 19.5f * s, Ws(label), lw), P.ink);
        const uint64_t gkey = a.nextActionGlassKey();
        drawSwitch(x + w, mid, on, gkey, hv);
        App* ap = &a;
        act(x - 10 * s, y, w + 20 * s, h, [ap, flip] { flip(); ap->dayIndex = -1; ap->minuteCacheAt = -1e9; ap->cmd(CMD_SAVE); });
        return h;
    }

    float segmented(float x, float y, const std::vector<std::wstring>& o, int sel, std::function<void(int)> on) {
        const float h = 38 * s, pad = 12 * s, fs = 12.5f * s;
        std::vector<float> ws; float tot = 0;
        for (auto& t : o) { float w = g.textWidth(F_MONO_M, fs, t, 0.1f) + 2 * pad; ws.push_back(w); tot += w; }
        g.fillRoundRect(x, y, tot, h, h * 0.5f, mulA(P.ink, 0.07f));
        float xx = x;
        for (size_t i = 0; i < o.size(); ++i) {
            const bool cur = (int)i == sel;
            const bool hv = over(xx, y, ws[i], h);
            const uint64_t gkey = a.nextActionGlassKey();
            const glassfx::Pose gp = a.glass.pose(gkey, xx + 3 * s, y + 3 * s, std::max(8.f, ws[i] - 6 * s), h - 6 * s, (h - 6 * s) * 0.5f, s, hv, false);
            if (cur) g.fillRoundRect(gp.x, gp.y, gp.w, gp.h, gp.r, withA(acc(), 0.24f));
            else if (gp.hoverT > 0.01f) g.fillRoundRect(gp.x, gp.y, gp.w, gp.h, gp.r, mulA(P.ink, 0.07f * gp.hoverT));
            drawGlassEffects(c, gkey, gp, false, false);
            g.text(F_MONO_M, fs, gp.x + gp.w * 0.5f, capMid(F_MONO_M, fs, gp.y + gp.h * 0.5f), o[i], cur ? P.ink : lerp(P.ink3, P.ink, gp.hoverT), Align::Center, 0.1f);
            int idx = (int)i;
            act(xx, y, ws[i], h, [on, idx] { on(idx); });
            xx += ws[i];
        }
        return tot;
    }

    // label at the left, [-] value [+] at the right (tactile circular Liquid Glass buttons)
    float stepper(float x, float y, float w, const char* label, const std::string& val, std::function<void()> minus, std::function<void()> plus) {
        const float h = 46 * s, r = 16 * s;
        const float mid = y + h * 0.5f;
        g.text(F_LIGHT, 19.5f * s, x, capMid(F_LIGHT, 19.5f * s, mid), Ws(label), P.ink);
        const float pcx = x + w - r, vcx = pcx - r - 40 * s, mcx = vcx - 40 * s - r;
        for (int k = 0; k < 2; ++k) {
            const float cx = k ? pcx : mcx;
            const bool hv = over(cx - r - 3 * s, mid - r - 3 * s, 2 * r + 6 * s, 2 * r + 6 * s);
            const uint64_t gkey = a.nextActionGlassKey();
            const glassfx::Pose gp = a.glass.pose(gkey, cx - r, mid - r, 2 * r, 2 * r, r, s, hv, false);
            const float gcx = gp.x + gp.w * 0.5f, gcy = gp.y + gp.h * 0.5f;
            g.fillRoundRect(gp.x, gp.y, gp.w, gp.h, gp.r, mulA(P.ink, 0.08f + 0.06f * gp.hoverT));
            if (gp.hoverT > 0.01f) g.fillRoundRect(gp.x, gp.y, gp.w, gp.h, gp.r, withA(acc(), (P.dark ? 0.24f : 0.16f) * gp.hoverT));
            g.strokeRoundRect(gp.x + 0.5f, gp.y + 0.5f, std::max(1.f, gp.w - 1.f), std::max(1.f, gp.h - 1.f), std::max(0.5f, gp.r - 0.5f),
                              lerp(P.line, withA(acc(), 0.78f), gp.hoverT), std::max(1.f, s));
            drawGlassEffects(c, gkey, gp, false, false);
            const Color ic = lerp(P.ink2, P.ink, gp.hoverT);
            const float lw = std::max(1.5f, (1.75f + 0.25f * gp.hoverT) * s);
            g.line(gcx - 5.5f * s, gcy, gcx + 5.5f * s, gcy, ic, lw, true);
            if (k) g.line(gcx, gcy - 5.5f * s, gcx, gcy + 5.5f * s, ic, lw, true);
            act(cx - r - 3 * s, mid - r - 3 * s, 2 * r + 6 * s, 2 * r + 6 * s, k ? plus : minus);
        }
        g.text(F_MEDIUM, 19.f * s, vcx, capMid(F_MEDIUM, 19.f * s, mid), Ws(val), P.ink, Align::Center);
        return h;
    }

    float button(float x, float y, float w, float h, const wchar_t* label, std::function<void()> fn, bool primary = false) {
        const bool hv = over(x, y, w, h);
        const uint64_t gkey = a.nextActionGlassKey();
        const glassfx::Pose gp = a.glass.pose(gkey, x, y, w, h, h * 0.5f, s, hv, false);
        if (primary) g.fillRoundRect(gp.x, gp.y, gp.w, gp.h, gp.r, withA(acc(), 0.82f + 0.14f * gp.hoverT));
        else {
            g.fillRoundRect(gp.x, gp.y, gp.w, gp.h, gp.r, mulA(P.ink, 0.06f + 0.07f * gp.hoverT));
            g.strokeRoundRect(gp.x + 0.5f, gp.y + 0.5f, std::max(1.f, gp.w - 1), std::max(1.f, gp.h - 1), std::max(0.5f, gp.r - 0.5f), P.line, std::max(1.f, s));
        }
        drawGlassEffects(c, gkey, gp, primary, false);
        g.text(F_MONO_M, 12.5f * s, gp.x + gp.w * 0.5f, capMid(F_MONO_M, 12.5f * s, gp.y + gp.h * 0.5f), label, primary ? P.plate : lerp(P.ink2, P.ink, gp.hoverT), Align::Center, 0.12f);
        act(x, y, w, h, std::move(fn));
        return h;
    }
    float buttonAuto(float x, float y, const wchar_t* label, std::function<void()> fn, bool primary = false) {
        float w = g.textWidth(F_MONO_M, 12.5f * s, label, 0.12f) + 36 * s;
        button(x, y, w, 36 * s, label, std::move(fn), primary);
        return w;
    }

    // Custom vector "<-- BACK" button (no font arrow glyph so it never shows "?" in any font or theme)
    float backButton(float x, float y, std::function<void()> fn) {
        const float h = 36 * s, fs = 12.5f * s;
        const float tw = g.textWidth(F_MONO_M, fs, L"BACK", 0.12f);
        const float padL = 16 * s, arrowW = 18 * s, gap = 9 * s, padR = 18 * s;
        const float w = padL + arrowW + gap + tw + padR;
        const bool hv = over(x, y, w, h);
        const uint64_t gkey = a.nextActionGlassKey();
        const glassfx::Pose gp = a.glass.pose(gkey, x, y, w, h, h * 0.5f, s, hv, false);
        g.fillRoundRect(gp.x, gp.y, gp.w, gp.h, gp.r, mulA(P.ink, 0.06f + 0.08f * gp.hoverT));
        g.strokeRoundRect(gp.x + 0.5f, gp.y + 0.5f, std::max(1.f, gp.w - 1), std::max(1.f, gp.h - 1), std::max(0.5f, gp.r - 0.5f), lerp(P.line, withA(acc(), 0.55f), gp.hoverT), std::max(1.f, s));
        drawGlassEffects(c, gkey, gp, false, false);
        const float mid = gp.y + gp.h * 0.5f;
        const float bx0 = gp.x + (gp.w - w) * 0.5f;
        const float ax0 = bx0 + padL + 1.5f * s, ax1 = bx0 + padL + arrowW - 0.5f * s;
        const float barbX = ax0 + 5.2f * s, barbY = 4.6f * s;
        const Color col = lerp(P.ink2, P.ink, gp.hoverT);
        const float sw = std::max(1.4f, 1.75f * s);
        g.line(ax0 + 0.6f * s, mid, ax1, mid, col, sw, true);
        g.polyline({{barbX, mid - barbY}, {ax0, mid}, {barbX, mid + barbY}}, false, col, sw, true);
        g.text(F_MONO_M, fs, bx0 + padL + arrowW + gap, capMid(F_MONO_M, fs, mid), L"BACK", col, Align::Left, 0.12f);
        act(x, y, w, h, std::move(fn));
        return w;
    }

    // a pill that flows: advances x, wraps at right
    void chip(float& x, float& y, float x0, float right, const std::string& label, bool on, std::function<void()> fn) {
        const float fs = 15.f * s, h = 34 * s, gap = 8 * s;
        const std::wstring t = Ws(label);
        const float w = g.textWidth(F_LIGHT, fs, t) + 28 * s;
        if (x + w > right && x > x0) { x = x0; y += h + gap; }
        const bool hv = over(x, y, w, h);
        const uint64_t gkey = a.nextActionGlassKey();
        const glassfx::Pose gp = a.glass.pose(gkey, x, y, w, h, h * 0.5f, s, hv, false);
        if (on) { g.fillRoundRect(gp.x, gp.y, gp.w, gp.h, gp.r, withA(acc(), 0.20f + 0.06f * gp.hoverT)); g.strokeRoundRect(gp.x + 0.5f, gp.y + 0.5f, std::max(1.f, gp.w - 1), std::max(1.f, gp.h - 1), std::max(0.5f, gp.r - 0.5f), withA(acc(), 0.8f), std::max(1.f, s)); }
        else { g.fillRoundRect(gp.x, gp.y, gp.w, gp.h, gp.r, mulA(P.ink, 0.05f + 0.06f * gp.hoverT)); }
        drawGlassEffects(c, gkey, gp, false, false);
        g.text(F_LIGHT, fs, gp.x + gp.w * 0.5f, capMid(F_LIGHT, fs, gp.y + gp.h * 0.5f), t, on ? P.ink : lerp(P.ink2, P.ink, gp.hoverT), Align::Center);
        act(x, y, w, h, std::move(fn));
        x += w + gap;
    }

    void sectionLabel(float x, float y, const char* t) { g.text(F_MONO, 12.f * s, x, y + 10 * s, Ws(util::upper(t)), P.ink3, Align::Left, 0.18f); }

    // text input. Returns the height used (label + box).
    // The box shows the caret and the selection of a.ted (see textedit.h). A click puts the caret where you clicked, a double click selects all.
    // Not focused, it shows the start of the text; focused, it scrolls to keep the caret in view.
    float field(float x, float y, float w, float boxH, const char* label, std::string& t, size_t maxLen, bool numeric, bool multi,
                std::function<void()> onChange = {}, const char* placeholder = "") {
        const float lh = label && *label ? 22 * s : 0.f;
        if (lh > 0) g.text(F_MONO, 11.5f * s, x, y + 11 * s, Ws(util::upper(label)), P.ink3, Align::Left, 0.14f);
        const float by = y + lh;
        const bool foc = a.textTarget == &t;
        g.fillRoundRect(x, by, w, boxH, 12 * s, mulA(P.ink, foc ? 0.10f : 0.06f));
        g.strokeRoundRect(x + 0.5f, by + 0.5f, w - 1, boxH - 1, 12 * s, foc ? withA(acc(), 0.9f) : P.line, std::max(1.f, s));
        const float fs = 18.f * s, padX = 14 * s, avail = w - 2 * padX;
        const std::wstring full = Ws(t);
        auto map = std::make_shared<ClickMap>();
        map->byteAt = textedit::wideToByte(t); map->x0 = x + padX; map->fs = fs; map->g = &g;
        if (map->byteAt.size() != full.size() + 1) { map->byteAt.assign(full.size() + 1, t.size()); for (size_t i = 0; i < full.size(); ++i) map->byteAt[i] = std::min(i, t.size()); }   // (cannot happen: both read the text the same way)
        if (foc) textedit::clamp(t, a.ted);
        const size_t cW = foc ? textedit::byteToWide(map->byteAt, a.ted.caret) : 0, aW = foc ? textedit::byteToWide(map->byteAt, a.ted.anchor) : 0;
        const size_t selLo = std::min(cW, aW), selHi = std::max(cW, aW);
        const Color selCol = withA(acc(), 0.34f);
        const auto isLow = [&](size_t i) { return i < full.size() && full[i] >= 0xDC00 && full[i] < 0xE000; };      // second half of a surrogate pair
        float caretX = x + padX, caretTop = by + boxH * 0.5f - 11 * s, caretBot = by + boxH * 0.5f + 11 * s;
        if (!multi) {
            const float base = capMid(F_LIGHT, fs, by + boxH * 0.5f);
            auto W = [&](size_t from, size_t len) { return g.textWidth(F_LIGHT, fs, full.substr(from, len)); };
            size_t sc = 0;
            if (foc) {
                sc = std::min(a.textScroll, full.size());
                if (cW < sc) sc = cW;
                while (sc < cW && W(sc, cW - sc) > avail - 2 * s) ++sc;                                           // scroll right until the caret is in view
                if (sc > 0 && W(sc, std::wstring::npos) < avail) while (sc > 0 && W(sc - 1, std::wstring::npos) <= avail) --sc;   // text got shorter: no gap on the right
                if (isLow(sc)) ++sc;
                a.textScroll = sc;
            }
            size_t lo = sc, hi = full.size();                                                                    // how much of it fits
            while (lo < hi) { const size_t mid = (lo + hi + 1) / 2; if (W(sc, mid - sc) <= avail) lo = mid; else hi = mid - 1; }
            size_t eW = lo; if (eW > sc && eW < full.size() && isLow(eW)) --eW;
            if (full.empty()) g.text(F_LIGHT, fs, x + padX, base, Ws(placeholder), mulA(P.ink3, 0.8f));
            if (foc && selHi > selLo) {
                const size_t s0 = std::max(selLo, sc), s1 = std::min(selHi, eW);
                if (s1 > s0) { const float xa = W(sc, s0 - sc), xb = W(sc, s1 - sc); g.fillRect(x + padX + xa, by + boxH * 0.5f - 12 * s, xb - xa, 24 * s, selCol); }
            }
            g.text(F_LIGHT, fs, x + padX, base, full.substr(sc, eW - sc), P.ink);
            if (foc) caretX = x + padX + W(sc, std::min(cW, eW) - sc);
            map->lines = {full.substr(sc, eW - sc)}; map->startW = {sc}; map->top = by; map->lh = boxH;
        } else {
            std::vector<std::wstring> lines; std::vector<size_t> startW;                                         // exact source-preserving wrap so caret/selection stay aligned
            auto wrapExact = [&](const std::wstring& part, size_t baseOff) {
                if (part.empty()) { lines.push_back(L""); startW.push_back(baseOff); return; }
                size_t pos = 0; const size_t n = part.size();
                while (pos < n) {
                    size_t lo = pos, hi = n;
                    while (lo < hi) {
                        const size_t mid = (lo + hi + 1) / 2;
                        if (g.textWidth(F_LIGHT, fs, part.substr(pos, mid - pos)) <= avail) lo = mid; else hi = mid - 1;
                    }
                    if (lo == pos) { lo = pos + 1; if (lo < n && isLow(baseOff + lo)) ++lo; }
                    else if (lo < n && isLow(baseOff + lo)) { --lo; if (lo == pos) lo = std::min(n, pos + 2); }
                    size_t end = lo;
                    if (end < n) {
                        size_t sp = end;
                        while (sp > pos && part[sp - 1] != L' ') --sp;
                        if (sp > pos) end = sp;
                    }
                    lines.push_back(part.substr(pos, end - pos));
                    startW.push_back(baseOff + pos);
                    pos = end;
                }
            };
            size_t i0 = 0;
            for (;;) {
                const size_t nl = full.find(L'\n', i0);
                const std::wstring part = full.substr(i0, nl == std::wstring::npos ? std::wstring::npos : nl - i0);
                wrapExact(part, i0);
                if (nl == std::wstring::npos) break;
                i0 = nl + 1;
            }
            const float lhgt = fs * 1.4f;
            const int fit = std::max(1, (int)((boxH - 22 * s) / lhgt)), nLines = (int)lines.size();
            int ci = 0; while (ci + 1 < nLines && startW[(size_t)ci + 1] <= cW) ++ci;                            // the line the caret is on
            int first = 0;
            if (foc) {
                first = (int)std::min<size_t>(a.textScroll, (size_t)std::max(0, nLines - 1));
                if (ci < first) first = ci;
                if (ci >= first + fit) first = ci - fit + 1;
                first = std::max(0, std::min(first, std::max(0, nLines - fit)));
                a.textScroll = (size_t)first;
                a.textLines.resize(startW.size()); for (size_t i = 0; i < startW.size(); ++i) a.textLines[i] = map->byteAt[std::min(startW[i], map->byteAt.size() - 1)];   // for Up / Down / Home / End
            }
            if (full.empty()) g.text(F_LIGHT, fs, x + padX, by + 14 * s + fs, Ws(placeholder), mulA(P.ink3, 0.8f));
            const float y0 = by + 12 * s + fs;
            const int last = std::min(nLines, first + fit);
            for (int i = first; i < last; ++i) {
                const float yy = y0 + (float)(i - first) * lhgt;
                const std::wstring& ln = lines[(size_t)i]; const size_t st = startW[(size_t)i];
                if (foc && selHi > selLo) {                                                                      // the part of the selection on this line
                    const size_t s0 = std::max(selLo, st), s1 = std::min(selHi, st + ln.size());
                    if (s1 > s0) { const float xa = g.textWidth(F_LIGHT, fs, ln.substr(0, s0 - st)), xb = g.textWidth(F_LIGHT, fs, ln.substr(0, s1 - st)); g.fillRect(x + padX + xa, yy - fs * 0.85f, xb - xa, lhgt * 0.95f, selCol); }
                    else if (ln.empty() && selLo <= st && st < selHi) g.fillRect(x + padX, yy - fs * 0.85f, 5 * s, lhgt * 0.95f, selCol);          // an empty line inside the selection
                }
                g.text(F_LIGHT, fs, x + padX, yy, ln, P.ink);
            }
            if (foc) {
                const std::wstring& cl = lines[(size_t)ci];
                caretX = x + padX + g.textWidth(F_LIGHT, fs, cl.substr(0, std::min(cl.size(), cW - std::min(cW, startW[(size_t)ci]))));
                caretBot = y0 + (float)(ci - first) * lhgt + fs * 0.35f; caretTop = caretBot - fs * 1.05f;
            }
            for (int i = first; i < last; ++i) { map->lines.push_back(lines[(size_t)i]); map->startW.push_back(startW[(size_t)i]); }
            map->top = by + 12 * s; map->lh = lhgt;
        }
        if (foc && std::fmod(a.now - a.textFocusAt, 1.0) < 0.62) g.line(caretX + 2 * s, caretTop, caretX + 2 * s, caretBot, acc(), std::max(1.5f, 1.6f * s), false);
        std::string* tp = &t; App* ap = &a;
        act(x, by, w, boxH, [ap, tp, maxLen, numeric, multi, onChange, map] {
            const bool same = ap->textTarget == tp;
            ap->textTarget = tp; ap->textMulti = multi; ap->textMax = maxLen; ap->textNumeric = numeric; ap->textChanged = onChange; ap->textClicked = true;
            if (!same) { ap->ted.reset(0); ap->textScroll = 0; ap->textLines.clear(); }
            ap->textFocusAt = ap->now;
            if (ap->dblClick) textedit::selectAll(*tp, ap->ted);
            else textedit::moveTo(*tp, ap->ted, map->pick(ap->mouseX, ap->mouseY), false);
        });
        return lh + boxH + 12 * s;
    }

    void divider(float x, float y, float w) { g.line(x, y, x + w, y, P.line, std::max(1.f, s), false); }
};

// ---------------------------------------------------------------- widgets
void toggleWidget(App* a, int id) {
    auto& v = a->st.widgets;
    auto it = std::find(v.begin(), v.end(), id);
    if (it != v.end()) v.erase(it);
    else v.push_back(id);
    a->cmd(CMD_SAVE);
}
void moveWidget(App* a, int id, int dir) {
    auto& v = a->st.widgets;
    auto it = std::find(v.begin(), v.end(), id);
    if (it == v.end()) return;
    int i = (int)(it - v.begin()), j = i + dir;
    if (j < 0 || j >= (int)v.size()) return;
    std::swap(v[i], v[j]);
    a->cmd(CMD_SAVE);
}

void tabWidgets(UI& u, float x, float y, float w, float h) {
    App& a = u.a; Gfx& g = u.g; const Palette& P = u.P; const float s = u.s; App* ap = &a;
    int sel = a.widgetSel;
    if (sel < 0 || sel >= WG_COUNT) sel = a.st.widgets.empty() ? 0 : a.st.widgets[0];
    a.widgetSel = sel;
    const float lw = w * 0.47f, gap = 40 * s, rx = x + lw + gap, rw = w - lw - gap;

    // left: the catalogue
    u.sectionLabel(x, y, "All widgets");
    float yy = y + 26 * s;
    const float rowH = 41 * s;
    for (int id = 0; id < WG_COUNT; ++id) {
        const auto& v = a.st.widgets;
        auto it = std::find(v.begin(), v.end(), id);
        const bool on = it != v.end();
        const int order = on ? (int)(it - v.begin()) + 1 : 0;
        const bool cur = id == sel;
        if (cur) { g.fillRoundRect(x, yy, lw, rowH - 6 * s, 16 * s, mulA(P.ink, 0.06f)); g.strokeRoundRect(x + 0.5f, yy + 0.5f, lw - 1, rowH - 6 * s - 1, 16 * s, withA(u.acc(), 0.55f), std::max(1.f, s)); }
        else if (u.over(x, yy, lw, rowH - 6 * s)) g.fillRoundRect(x, yy, lw, rowH - 6 * s, 16 * s, mulA(P.ink, 0.04f));
        const float mid = yy + (rowH - 6 * s) * 0.5f;
        u.act(x, yy, lw, rowH - 6 * s, [ap, id] { ap->widgetSel = id; });
        // order badge
        if (on) {
            g.fillCircle(x + 26 * s, mid, 12 * s, withA(u.acc(), 0.22f));
            g.text(F_MONO_M, 12.5f * s, x + 26 * s, u.capMid(F_MONO_M, 12.5f * s, mid), Ws(fmt("%d", order)), P.ink, Align::Center);
        } else g.strokeCircle(x + 26 * s, mid, 11.5f * s, mulA(P.ink, 0.2f), std::max(1.f, s));
        g.text(F_LIGHT, 20.f * s, x + 52 * s, u.capMid(F_LIGHT, 20.f * s, mid), Ws(widgetName(id)), on ? P.ink : P.ink2);
        if (a.st.pinned == id) g.text(F_MONO, 10.5f * s, x + 52 * s + g.textWidth(F_LIGHT, 20.f * s, Ws(widgetName(id))) + 12 * s, u.capMid(F_MONO, 10.5f * s, mid), L"ON SKY", withA(P.accent2, 0.95f), Align::Left, 0.14f);
        // arrows and switch
        const float swR = x + lw - 14 * s;
        u.drawSwitch(swR, mid, on, a.nextActionGlassKey(), u.over(swR - 52 * s, mid - 20 * s, 56 * s, 40 * s));
        u.act(swR - 52 * s, mid - 20 * s, 56 * s, 40 * s, [ap, id] { toggleWidget(ap, id); });
        if (on) {
            for (int k = 0; k < 2; ++k) {
                const float ax = swR - 52 * s - 44 * s * (2 - k) + 10 * s;     // up, down
                const bool can = k == 0 ? order > 1 : order < (int)v.size();
                const bool hv = u.over(ax - 14 * s, mid - 16 * s, 28 * s, 32 * s) && can;
                const float rBtn = 13.f * s;
                const uint64_t aKey = can ? a.nextActionGlassKey() : 0;
                const glassfx::Pose gpA = a.glass.pose(aKey, ax - rBtn, mid - rBtn, 2.f * rBtn, 2.f * rBtn, rBtn, s, hv, false);
                if (can) {
                    g.fillRoundRect(gpA.x, gpA.y, gpA.w, gpA.h, gpA.r, mulA(P.ink, 0.06f + 0.06f * gpA.hoverT));
                    if (gpA.hoverT > 0.01f) g.fillRoundRect(gpA.x, gpA.y, gpA.w, gpA.h, gpA.r, withA(u.acc(), 0.22f * gpA.hoverT));
                    g.strokeRoundRect(gpA.x + 0.5f, gpA.y + 0.5f, std::max(1.f, gpA.w - 1.f), std::max(1.f, gpA.h - 1.f), std::max(0.5f, gpA.r - 0.5f), lerp(P.line, withA(u.acc(), 0.72f), gpA.hoverT), std::max(1.f, s));
                    drawGlassEffects(u.c, aKey, gpA, false, false);
                }
                const float gcx = gpA.x + gpA.w * 0.5f, gcy = gpA.y + gpA.h * 0.5f;
                Color col = can ? lerp(P.ink2, P.ink, gpA.hoverT) : mulA(P.ink3, 0.35f);
                const float d = k == 0 ? 1.f : -1.f;           // up chevron points up, down chevron points down
                g.polyline({{gcx - 5.5f * s, gcy + d * 2.8f * s}, {gcx, gcy - d * 2.8f * s}, {gcx + 5.5f * s, gcy + d * 2.8f * s}}, false, col, std::max(1.4f, 1.7f * s), true);
                if (can) u.act(ax - 14 * s, mid - 16 * s, 28 * s, 32 * s, [ap, id, k] { moveWidget(ap, id, k == 0 ? -1 : 1); });
            }
        }
        yy += rowH;
    }
    g.text(F_LIGHT, 14.f * s, x, yy + 18 * s, L"Open them from the dock, or press 5. They take turns, a few at a time (Rotation tab).", P.ink3);

    // right: options of the selected widget
    u.divider(rx - gap * 0.5f, y, 0); g.line(rx - gap * 0.5f, y, rx - gap * 0.5f, y + h - 10 * s, P.line, std::max(1.f, s), false);
    float ry = y;
    g.text(F_SERIF, 32.f * s, rx, ry + 30 * s, Ws(widgetName(sel)), P.ink);
    ry += 46 * s;
    {
        auto lines = g.wrap(F_LIGHT, 16.f * s, Ws(widgetBlurb(sel)), rw, 0, 3);
        for (auto& l : lines) { g.text(F_LIGHT, 16.f * s, rx, ry + 16 * s, l, P.ink2); ry += 22 * s; }
    }
    ry += 14 * s;
    {
        bool pin = a.st.pinned == sel;
        const float hh = 62.f * s;
        const float mid = ry + hh * 0.5f;
        const bool hvPin = u.over(rx - 10 * s, ry, rw + 20 * s, hh);
        if (hvPin) g.fillRoundRect(rx - 10 * s, ry + 2 * s, rw + 20 * s, hh - 4 * s, 14 * s, mulA(P.ink, 0.05f));
        g.text(F_LIGHT, 19.5f * s, rx, mid - 3 * s, L"Add to the rotating panel", P.ink);
        g.text(F_LIGHT, 14.f * s, rx, mid + 17 * s, L"Takes the place of the Today list", P.ink3);
        u.drawSwitch(rx + rw, mid, pin, a.nextActionGlassKey(), hvPin);
        u.act(rx - 10 * s, ry, rw + 20 * s, hh, [ap, sel] { ap->st.pinned = ap->st.pinned == sel ? -1 : sel; ap->cmd(CMD_SAVE); });
        ry += hh + 10 * s;
    }
    u.divider(rx, ry, rw); ry += 18 * s;

    switch (sel) {
        case WG_WORLD: {
            u.sectionLabel(rx, ry, "Cities (up to four)");
            float cx = rx, cy = ry + 28 * s;
            for (int i = 0; i < cityCount(); ++i) {
                const bool on = std::find(a.st.clocks.begin(), a.st.clocks.end(), i) != a.st.clocks.end();
                u.chip(cx, cy, rx, rx + rw, cityName(i), on, [ap, i] {
                    auto& v = ap->st.clocks; auto it = std::find(v.begin(), v.end(), i);
                    if (it != v.end()) v.erase(it);
                    else if (v.size() < 4) v.push_back(i);
                    else ap->setToast(L"Up to four cities");
                    ap->cmd(CMD_SAVE);
                });
            }
            break;
        }
        case WG_MONTH: {
            ry += u.toggle(rx, ry, rw, "Week starts on Monday", nullptr, a.st.weekMon);
            u.sectionLabel(rx, ry + 6 * s, "Your dates");
            ry += 32 * s;
            // add: "12-25 Christmas" repeats every year, "2027-03-14 Exam" happens once
            const float bw = 84 * s;
            u.field(rx, ry, rw - bw - 12 * s, 46 * s, "", a.evField, 60, false, false, {}, "12-25 Christmas   or   2027-03-14 Exam");
            u.button(rx + rw - bw, ry + 5 * s, bw, 36 * s, L"ADD", [ap] {
                int y = 0, m = 0, d = 0; const char* p = ap->evField.c_str();
                const int n = util::parseEventDatePrefix(p, y, m, d);
                if (!n) { ap->setToast(L"Not a real date. Try 12-25 Christmas or 2027-03-14 Exam"); return; }
                const std::string title = cleanName(std::string(p + n));
                if (title.empty()) { ap->setToast(L"Add a name after the date, like 12-25 Christmas"); return; }
                if (ap->st.events.size() >= 200) { ap->setToast(L"That is plenty of dates (200). Remove one first"); return; }
                today::CustomEvent e; e.y = y; e.m = m; e.d = d; e.title = title;
                ap->st.events.push_back(e); ap->evField.clear(); ap->dayIndex = -1; ap->textTarget = nullptr; ap->cmd(CMD_SAVE); ap->setToast(L"Date added");
            }, true);
            ry += 58 * s;
            // four dates to a page, with Back / Next when there are more (every date can be reached and removed here)
            const int per = 4, total = (int)a.st.events.size(), pages = std::max(1, (total + per - 1) / per);
            ap->evPage = std::max(0, std::min(ap->evPage, pages - 1));
            const int first = ap->evPage * per;
            for (int i = first; i < total && i < first + per; ++i) {
                const auto& e = a.st.events[i];
                std::string when = e.y ? fmt("%d %s %d", e.d, util::MONTHS3[e.m - 1], e.y) : fmt("%d %s", e.d, util::MONTHS3[e.m - 1]);
                g.text(F_MONO, 12.f * s, rx, u.capMid(F_MONO, 12.f * s, ry + 17 * s), Ws(util::upper(when)), P.accent2, Align::Left, 0.1f);
                g.text(F_LIGHT, 17.f * s, rx + 112 * s, u.capMid(F_LIGHT, 17.f * s, ry + 17 * s), g.ellipsize(F_LIGHT, 17.f * s, Ws(e.title), rw - 112 * s - 40 * s), P.ink);
                const float xx = rx + rw - 14 * s, mid = ry + 17 * s;
                const bool hv = u.over(xx - 16 * s, mid - 16 * s, 32 * s, 32 * s);
                g.line(xx - 5 * s, mid - 5 * s, xx + 5 * s, mid + 5 * s, hv ? P.ink : P.ink3, std::max(1.5f, 1.7f * s), true);
                g.line(xx - 5 * s, mid + 5 * s, xx + 5 * s, mid - 5 * s, hv ? P.ink : P.ink3, std::max(1.5f, 1.7f * s), true);
                u.act(xx - 16 * s, mid - 16 * s, 32 * s, 32 * s, [ap, i] { if (i < (int)ap->st.events.size()) { ap->st.events.erase(ap->st.events.begin() + i); ap->dayIndex = -1; ap->cmd(CMD_SAVE); } });
                ry += 36 * s;
            }
            if (pages > 1) {
                const float bw = 64 * s, bh = 32 * s;
                if (ap->evPage > 0) u.button(rx, ry + 2 * s, bw, bh, L"BACK", [ap] { --ap->evPage; });
                if (ap->evPage < pages - 1) u.button(rx + rw - bw, ry + 2 * s, bw, bh, L"NEXT", [ap] { ++ap->evPage; });
                g.text(F_MONO, 11.f * s, rx + rw * 0.5f, u.capMid(F_MONO, 11.f * s, ry + 2 * s + bh * 0.5f), Ws(fmt("PAGE %d OF %d \xC2\xB7 %d DATES", ap->evPage + 1, pages, total)), P.ink3, Align::Center, 0.1f);
            }
            break;
        }
        case WG_NOTE:
            u.field(rx, ry, rw, std::min(190.f * s, h - (ry - y) - 40 * s), "Your note", a.st.note, 400, false, true, [ap] { ap->cmd(CMD_SAVE); }, "Type here. Enter starts a new line.");
            break;
        case WG_COUNTDOWN: {
            ry += u.field(rx, ry, rw, 46 * s, "What are you counting down to", a.st.cdLabel, 40, false, false, [ap] { ap->cmd(CMD_SAVE); }, "Exams, Trip, Birthday");
            ry += u.field(rx, ry, rw, 46 * s, "Date  (YYYY-MM-DD)", a.st.cdDate, 10, true, false, [ap] { ap->cmd(CMD_SAVE); }, "2027-01-15");
            int yy_ = 0, mm_ = 0, dd_ = 0;
            bool okd = util::parseDateStrict(a.st.cdDate, yy_, mm_, dd_);                   // 2027-02-31 is not a date
            if (okd) {
                util::LocalTime N = util::toLocal(a.now, a.tzOff);
                long long days = (long long)std::llround(astro::daysFromCivil(yy_, mm_, dd_) - astro::daysFromCivil(N.y, N.m, N.d));
                g.text(F_LIGHT, 16.f * s, rx, ry + 14 * s, Ws(days == 0 ? std::string("That is today") : days > 0 ? fmt("%lld day%s from today", days, days == 1 ? "" : "s") : fmt("%lld day%s ago", -days, days == -1 ? "" : "s")), P.accent2);
            } else if (!a.st.cdDate.empty()) g.text(F_LIGHT, 16.f * s, rx, ry + 14 * s, L"Not a real date yet", rgb(0xFF9F7A));
            break;
        }
        default:
            g.text(F_LIGHT, 16.f * s, rx, ry + 16 * s, L"Nothing to set for this one.", P.ink3);
            break;
    }
}

// ---------------------------------------------------------------- elements
void tabElements(UI& u, float x, float y, float w, float h) {
    App& a = u.a; Settings& st = a.st; Gfx& g = u.g; const float s = u.s; App* ap = &a;
    const float colW = (w - 56 * s) * 0.5f, x2 = x + colW + 56 * s;
    float yy = y;
    u.sectionLabel(x, yy, "Sky face"); yy += 28 * s;
    yy += u.toggle(x, yy, colW, "Weather block", "Current weather and the hours ahead", st.elWeather);
    yy += u.toggle(x, yy, colW, "Sun and Moon lines", "Rise, set, phase and distance", st.elSunMoon);
    yy += u.toggle(x, yy, colW, "Day band", "Sun and Moon up across the day", st.elBand);
    yy += u.toggle(x, yy, colW, "Planet and star names", nullptr, st.elLabels);
    yy += u.toggle(x, yy, colW, "Constellation lines", nullptr, st.elConst);
    yy += u.toggle(x, yy, colW, "Milky Way", nullptr, st.elMilky);
    yy += u.toggle(x, yy, colW, "Ecliptic line", nullptr, st.elEcliptic);
    yy += u.toggle(x, yy, colW, "Clouds on the sky map", "From the weather", st.elClouds);
    yy += u.toggle(x, yy, colW, "Sun and Moon tracks", "Today's path across the sky", st.elTracks);
    yy += 10 * s;
    u.divider(x, yy, colW); yy += 16 * s;
    u.sectionLabel(x, yy, "Sound"); yy += 26 * s;
    u.toggle(x, yy, colW, "Soft hourly chime", "Gentle bell on the hour (silent during bedtime)", st.hourlyChime);

    yy = y;
    u.sectionLabel(x2, yy, "Clock and window"); yy += 28 * s;
    yy += u.toggle(x2, yy, colW, "Seconds", nullptr, st.seconds);
    yy += u.toggle(x2, yy, colW, "24-hour clock", nullptr, st.h24, [ap] { ap->minuteCacheAt = -1e9; ap->dayIndex = -1; });
    yy += u.toggle(x2, yy, colW, "Fahrenheit", nullptr, st.useF);
    yy += u.toggle(x2, yy, colW, "Holidays in Today", "Public holidays of the countries you chose, and Poya days", st.poya, [ap] { ap->dayIndex = -1; });
    yy += u.toggle(x2, yy, colW, "Burn-in shift", "Moves the picture a few pixels now and then", st.pixelShift);
    yy += u.toggle(x2, yy, colW, "Keep the screen awake", nullptr, st.keepAwake, [ap] { ap->cmd(CMD_KEEPAWAKE); });
    yy += u.toggle(x2, yy, colW, "Always on top", nullptr, st.topmost, [ap] { ap->cmd(CMD_TOPMOST); });
    yy += u.toggle(x2, yy, colW, "Red night mode at bedtime", nullptr, st.emberAtNight);
    if (st.emberAtNight) {
        yy += u.stepper(x2, yy, colW, "From", fmt("%02d:00", st.emberFrom), [ap] { ap->st.emberFrom = (ap->st.emberFrom + 23) % 24; ap->cmd(CMD_SAVE); }, [ap] { ap->st.emberFrom = (ap->st.emberFrom + 1) % 24; ap->cmd(CMD_SAVE); });
        yy += u.stepper(x2, yy, colW, "Until", fmt("%02d:00", st.emberTo), [ap] { ap->st.emberTo = (ap->st.emberTo + 23) % 24; ap->cmd(CMD_SAVE); }, [ap] { ap->st.emberTo = (ap->st.emberTo + 1) % 24; ap->cmd(CMD_SAVE); });
    }
    yy += 10 * s;
    g.text(F_LIGHT, 19.5f * s, x2, u.capMid(F_LIGHT, 19.5f * s, yy + 19 * s), L"Theme", u.P.ink);
    {
        std::vector<std::wstring> names = {L"AUTO", L"DAY", L"SUNLIT", L"SUNLIT II", L"DAWN", L"SUNSET", L"DUSK", L"NIGHT", L"MIDNIGHT", L"GALAXY", L"EMBER"};
        std::vector<float> cw; float tot = 0;
        for (auto& n : names) { cw.push_back(g.textWidth(F_MONO_M, 12.5f * s, n, 0.1f) + 24 * s); tot += cw.back(); }
        const bool narrow = tot + g.textWidth(F_LIGHT, 19.5f * s, L"Theme") + 24 * s > colW;     // not enough room beside the label
        if (narrow) yy += 44 * s;
        const int cur = st.themePack.empty() ? themeSlot(st.theme) : -1;                           // -1: a theme pack is in use
        if (tot <= colW) {
            u.segmented(narrow ? x2 : x2 + colW - tot, yy, names, cur,
                        [ap](int i) { ap->st.themePack.clear(); ap->st.theme = THEME_ORDER[i]; ap->cmd(CMD_SAVE); });
        } else {                                                                                   // too many for one row: even rows that each fit
            const int rows = (int)std::ceil(tot / colW);
            const float target = tot / rows;
            size_t i = 0;
            while (i < names.size()) {
                float rw = 0; size_t j = i;
                while (j < names.size() && (j == i || (rw + cw[j] <= colW && rw + cw[j] * 0.5f <= target))) rw += cw[j++];
                const int off = (int)i, end = (int)j;
                std::vector<std::wstring> row(names.begin() + i, names.begin() + j);
                u.segmented(x2, yy, row, (cur >= off && cur < end) ? cur - off : -1,
                            [ap, off](int k) { ap->st.themePack.clear(); ap->st.theme = THEME_ORDER[off + k]; ap->cmd(CMD_SAVE); });
                if (j < names.size()) yy += 44 * s;
                i = j;
            }
        }
    }
    yy += 52 * s;
    u.stepper(x2, yy, colW, "Brightness", fmt("%.0f%%", st.brightness * 100.f),
              [ap] { ap->st.brightness = std::max(0.15f, ap->st.brightness - 0.05f); ap->cmd(CMD_SAVE); },
              [ap] { ap->st.brightness = std::min(1.0f, ap->st.brightness + 0.05f); ap->cmd(CMD_SAVE); });
    (void)h;
}

// ---------------------------------------------------------------- themes (theme packs)
// A theme pack is a folder in the "themes" folder next to Meridian.exe (see themes/README.txt). This tab lists what was found.
void tabThemes(UI& u, float x, float y, float w, float h) {
    App& a = u.a; Settings& st = a.st; Gfx& g = u.g; const Palette& P = u.P; const float s = u.s; App* ap = &a;
    const float lw = w * 0.55f, gap = 44 * s, rx = x + lw + gap, rw = w - lw - gap;
    const auto& all = themes::all();
    themes::Pack* sel = nullptr;
    for (const auto& p : all) if (p->id == a.packSel) sel = p.get();
    if (!sel && !all.empty()) { sel = all[0].get(); a.packSel = sel->id; }

    // ---- left: every pack found
    u.sectionLabel(x, y, "Theme packs found");
    float yy = y + 28 * s;
    if (all.empty()) {
        auto lines = g.wrap(F_LIGHT, 16.f * s, L"None yet. Drop a theme folder into the themes folder (button on the right) and it shows up here within a few seconds.", lw, 0, 4);
        for (auto& l : lines) { g.text(F_LIGHT, 16.f * s, x, yy + 16 * s, l, P.ink3); yy += 23 * s; }
        yy += 16 * s;
    }
    int shown = 0;
    for (const auto& up : all) {
        if (shown++ >= 5) break;
        themes::Pack* p = up.get();
        const float rowH = 74 * s, bh = rowH - 6 * s, mid = yy + bh * 0.5f;
        const bool cur = p == sel;
        if (cur) { g.fillRoundRect(x - 8 * s, yy, lw + 16 * s, bh, 16 * s, mulA(P.ink, 0.06f)); g.strokeRoundRect(x - 7.5f * s, yy + 0.5f, lw + 15 * s, bh - 1, 16 * s, withA(u.acc(), 0.5f), std::max(1.f, s)); }
        else if (u.over(x - 8 * s, yy, lw + 16 * s, bh)) g.fillRoundRect(x - 8 * s, yy, lw + 16 * s, bh, 16 * s, mulA(P.ink, 0.04f));
        const std::string id = p->id;
        u.act(x - 8 * s, yy, lw - 60 * s, bh, [ap, id] { ap->packSel = id; });

        // status: ON, OFF, BROKEN, WAITING (before its start date) or EXPIRED
        const themes::Pack::Window win = p->window(a.now, a.tzOff);       // (a yearly pack: the window it is in, or the next one)
        const double tStart = win.start, tEnd = win.end;
        std::string status; Color dot = P.accent2;
        if (!p->ok) { status = "COULD NOT LOAD"; dot = rgb(0xFF9F7A); }
        else if (p->disabled) { status = "SWITCHED OFF"; dot = mulA(P.ink, 0.25f); }
        else if (tEnd > 0 && a.now >= tEnd) { status = "EXPIRED " + util::upper(niceDate(win.endText)); dot = mulA(P.ink, 0.25f); }
        else if (tStart > 0 && a.now < tStart) { status = "STARTS " + util::upper(niceDate(win.startText)); dot = mulA(P.ink, 0.4f); }
        else {
            status = "ON";
            if (tEnd > 0) { const int days = (int)std::ceil((tEnd - a.now) / 86400.0); status += "  \xC2\xB7  HIDES ITSELF " + util::upper(niceDate(win.endText)) + fmt(" (%d DAY%s)", days, days == 1 ? "" : "S"); }
        }
        g.fillCircle(x + 8 * s, mid - 10 * s, 5 * s, dot);
        const float tw = lw - 30 * s - (p->ok ? 66 * s : 0);
        g.text(F_LIGHT, 20.5f * s, x + 28 * s, mid - 12 * s, g.ellipsize(F_LIGHT, 20.5f * s, Ws(p->name.empty() ? p->id : p->name), tw), p->ok && !p->disabled ? P.ink : P.ink3);
        std::string sub;
        if (!p->ok) sub = p->error;
        else {
            if (!p->faces.empty()) { sub = std::string("adds the tab ") + p->faces[0].name; for (size_t i = 1; i < p->faces.size(); ++i) sub += ", " + p->faces[i].name; }
            else sub = "a theme";
            if (!p->author.empty()) sub += "  \xC2\xB7  by " + p->author;
        }
        g.text(F_LIGHT, 14.f * s, x + 28 * s, mid + 8 * s, g.ellipsize(F_LIGHT, 14.f * s, Ws(sub), tw), p->ok ? P.ink3 : rgb(0xFF9F7A));
        g.text(F_MONO, 11.f * s, x + 28 * s, mid + 26 * s, g.ellipsize(F_MONO, 11.f * s, Ws(status), tw), P.ink3, Align::Left, 0.1f);
        if (p->ok) {                                                  // switch it off without deleting it
            const float swR = x + lw - 6 * s;
            u.drawSwitch(swR, mid, !p->disabled, a.nextActionGlassKey(), u.over(swR - 56 * s, mid - 20 * s, 60 * s, 40 * s));
            u.act(swR - 56 * s, mid - 20 * s, 60 * s, 40 * s, [ap, id] {
                auto& v = ap->st.packsOff; auto it = std::find(v.begin(), v.end(), id);
                if (it != v.end()) v.erase(it); else v.push_back(id);
                ap->syncPacks(); ap->cmd(CMD_SAVE);
            });
        }
        yy += rowH;
    }
    if ((int)all.size() > 5) { g.text(F_LIGHT, 14.f * s, x, yy + 8 * s, Ws(fmt("and %d more in the themes folder", (int)all.size() - 5)), P.ink3); yy += 26 * s; }

    // ---- left, below: the weather changes the light on every tab
    yy += 10 * s; u.divider(x, yy, lw); yy += 18 * s;
    u.sectionLabel(x, yy, "Light and weather"); yy += 28 * s;
    yy += u.toggle(x, yy, lw, "Light follows the weather", "Night gets deeper, dusk and dawn richer. Packs never change", st.weatherMood);
    yy += u.toggle(x, yy, lw, "Galaxy sky", "Stars, nebulae, meteors, a comet and an evening visitor", st.spaceFx);
    if (st.weatherMood)
        u.stepper(x, yy, lw, "How strongly", fmt("%.0f%%", st.moodStrength * 100.f),
                  [ap] { ap->st.moodStrength = std::max(0.5f, std::round((ap->st.moodStrength - 0.25f) * 4.f) / 4.f); ap->cmd(CMD_SAVE); },
                  [ap] { ap->st.moodStrength = std::min(1.5f, std::round((ap->st.moodStrength + 0.25f) * 4.f) / 4.f); ap->cmd(CMD_SAVE); });

    // ---- right: the chosen pack, then where packs live
    g.line(rx - gap * 0.5f, y, rx - gap * 0.5f, y + h - 10 * s, P.line, std::max(1.f, s), false);
    float ry = y;
    if (sel) {
        g.text(F_SERIF, 32.f * s, rx, ry + 30 * s, g.ellipsize(F_SERIF, 32.f * s, Ws(sel->name.empty() ? sel->id : sel->name), rw), P.ink);
        ry += 44 * s;
        if (!sel->description.empty()) {
            auto lines = g.wrap(F_LIGHT, 15.5f * s, Ws(sel->description), rw, 0, 3);
            for (auto& l : lines) { g.text(F_LIGHT, 15.5f * s, rx, ry + 15 * s, l, P.ink2); ry += 22 * s; }
        }
        ry += 8 * s;
        auto fact = [&](const char* k, const std::string& v) {
            g.text(F_MONO_M, 11.5f * s, rx, ry + 12 * s, Ws(k), P.ink3, Align::Left, 0.14f);
            auto lines = g.wrap(F_LIGHT, 15.5f * s, Ws(v), rw - 92 * s, 0, 3);
            float ly = ry + 13 * s;
            for (auto& l : lines) { g.text(F_LIGHT, 15.5f * s, rx + 92 * s, ly, l, P.ink); ly += 21 * s; }
            ry += std::max(26.f * s, (float)lines.size() * 21 * s + 5 * s);
        };
        if (sel->ok) {
            fact("AFFECTS", sel->global ? "The whole clock, if you choose it as the theme" : "Only its own tab. Everything else stays as it is.");
            const themes::Pack::Window sw = sel->window(a.now, a.tzOff);
            if (!sw.startText.empty()) fact("STARTS", niceDate(sw.startText) + (sel->yearly ? "  (comes back every year)" : ""));
            if (!sw.endText.empty()) fact("ENDS", niceDate(sw.endText) + (sel->yearly ? "  (hides itself, back next year)" : "  (removes itself)"));
            fact("FOLDER", util::narrow(sel->dir.filename().wstring()));
            ry += 6 * s;
            float bx = rx;
            themes::Pack* sp = sel;
            if (sp->live(a.now, a.tzOff) && !sp->faces.empty()) {
                const std::string key = themes::faceKey(*sp, sp->faces[0]);
                bx += u.buttonAuto(bx, ry, L"OPEN ITS TAB", [ap, key] {
                    ap->st.face = FACE_PACK; ap->st.facePack = key; ap->faceChangedAt = ap->now; ap->showSettings = false; ap->textTarget = nullptr; ap->cmd(CMD_SAVE);
                }, true) + 10 * s;
            }
            if (sp->global && sp->live(a.now, a.tzOff)) {
                const bool using_ = st.themePack == sp->id; const std::string id = sp->id;
                bx += u.buttonAuto(bx, ry, using_ ? L"STOP USING AS THEME" : L"USE AS THEME", [ap, id, using_] { ap->st.themePack = using_ ? std::string() : id; ap->cmd(CMD_SAVE); }) + 10 * s;
            }
            ry += 50 * s;
        }
        // notes from loading (a skipped layer, a missing picture ...)
        if (!sel->warnings.empty()) {
            g.text(F_MONO_M, 11.5f * s, rx, ry + 12 * s, Ws(fmt("%d NOTE%s FROM LOADING", (int)sel->warnings.size(), sel->warnings.size() == 1 ? "" : "S")), rgb(0xFF9F7A), Align::Left, 0.12f);
            ry += 22 * s;
            for (size_t i = 0; i < sel->warnings.size() && i < 3; ++i) {
                auto lines = g.wrap(F_LIGHT, 13.5f * s, Ws(sel->warnings[i]), rw, 0, 2);
                for (auto& l : lines) { g.text(F_LIGHT, 13.5f * s, rx, ry + 12 * s, l, P.ink3); ry += 19 * s; }
            }
            ry += 6 * s;
        }
        u.divider(rx, ry, rw); ry += 18 * s;
    }
    u.sectionLabel(rx, ry, "Where themes live"); ry += 28 * s;
    {
        const auto& r = themes::roots();
        std::wstring where = r.empty() ? std::wstring(L"(no folder)") : r[0].wstring();
        if (g.textWidth(F_MONO, 11.5f * s, where) > rw) {                       // a long path: keep the end, which is the part that matters
            while (where.size() > 4 && g.textWidth(F_MONO, 11.5f * s, L"\u2026" + where) > rw) where.erase(0, 1);
            where = L"\u2026" + where;
        }
        g.text(F_MONO, 11.5f * s, rx, ry + 12 * s, where, P.ink2); ry += 26 * s;
        float bx = rx;
        bx += u.buttonAuto(bx, ry, L"OPEN THEMES FOLDER", [ap] { ap->cmd(CMD_OPEN_THEMES); }, sel == nullptr) + 10 * s;
        u.buttonAuto(bx, ry, L"RESCAN", [ap] { ap->cmd(CMD_RESCAN_THEMES); });
        ry += 54 * s;
        auto note = g.wrap(F_LIGHT, 14.f * s, L"To add a theme, put its folder in there. To remove one, delete its folder. Meridian notices by itself. The built-in themes are part of the program and are never touched. themes/README.txt explains how to make your own.", rw, 0, 6);
        for (auto& l : note) { g.text(F_LIGHT, 14.f * s, rx, ry + 12 * s, l, P.ink3); ry += 20 * s; }
    }
}

// ---------------------------------------------------------------- the rotating panel
void tabPanel(UI& u, float x, float y, float w, float h) {
    App& a = u.a; Settings& st = a.st; Gfx& g = u.g; const Palette& P = u.P; const float s = u.s; App* ap = &a;
    const float lw = w * 0.5f, gap = 48 * s, rx = x + lw + gap, rw = w - lw - gap;
    static const int SECS[] = {5, 8, 10, 12, 15, 20, 30, 45, 60, 90};
    static constexpr int NS = (int)(sizeof SECS / sizeof SECS[0]);
    auto nearest = [](int v) { int best = 0; for (int i = 0; i < NS; ++i) if (std::abs(SECS[i] - v) < std::abs(SECS[best] - v)) best = i; return best; };
    u.segmented(x, y, {L"SKY FACE", L"WIDGETS FACE"}, a.rotSub, [ap](int i) { ap->rotSub = i; });
    const float top = y + 58 * s;
    h -= 58 * s;
    float yy = top;
    if (a.rotSub == 0) {
        u.sectionLabel(x, yy, "Show on the Sky face"); yy += 28 * s;
        yy += u.toggle(x, yy, lw, "Today", "Holidays, Poya days, the Moon and other special days", st.elToday);
        yy += u.toggle(x, yy, lw, "Sky events", "Showers, planets, eclipses, when one is close", st.elEvents, [ap] { ap->refreshEvents(true); });
        yy += u.toggle(x, yy, lw, "Holidays", "The next holidays of the countries you chose, hover for details", st.elHolidays);
        yy += u.toggle(x, yy, lw, "World clocks", "The cities from the Widgets tab", st.elWorld);
        if (st.pinned >= 0) {
            g.text(F_LIGHT, 15.5f * s, x, yy + 22 * s, Ws(std::string("Pinned widget: ") + widgetName(st.pinned)), P.accent2);
            yy += 34 * s;
        }
        yy += 10 * s;
        auto lines = g.wrap(F_LIGHT, 15.5f * s, L"Tick one to keep it on screen. Tick several and they take turns. Press Tab on the Sky face to skip to the next one.", lw, 0, 5);
        for (auto& l : lines) { g.text(F_LIGHT, 15.5f * s, x, yy + 14 * s, l, P.ink3); yy += 23 * s; }
    } else {
        u.sectionLabel(x, yy, "Pages of the Widgets face"); yy += 28 * s;
        {
            const float h2 = 52 * s;
            g.text(F_LIGHT, 19.5f * s, x, u.capMid(F_LIGHT, 19.5f * s, yy + h2 * 0.5f), L"Widgets per page", P.ink);
            std::vector<std::wstring> names = {L"1", L"2", L"3", L"4", L"6"};
            static const int PER[5] = {1, 2, 3, 4, 6};
            float tot = 0; for (auto& n : names) tot += g.textWidth(F_MONO_M, 12.5f * s, n, 0.1f) + 24 * s;
            const int cur = st.wgPerPage <= 0 ? 4 : (st.wgPerPage >= 6 ? 4 : std::max(0, std::min(3, st.wgPerPage - 1)));
            u.segmented(x + lw - tot, yy + (h2 - 38 * s) * 0.5f, names, cur, [ap](int i) { ap->st.wgPerPage = PER[i]; ap->cmd(CMD_SAVE); });
            yy += h2;
        }
        yy += u.stepper(x, yy, lw, "Each page stays", fmt("%d s", st.wgSecs),
                        [ap, nearest] { ap->st.wgSecs = SECS[std::max(0, nearest(ap->st.wgSecs) - 1)]; ap->cmd(CMD_SAVE); },
                        [ap, nearest] { ap->st.wgSecs = SECS[std::min(NS - 1, nearest(ap->st.wgSecs) + 1)]; ap->cmd(CMD_SAVE); });
        yy += 14 * s;
        const int n = (int)st.widgets.size(), per = st.wgPerPage <= 0 ? 6 : std::min(6, st.wgPerPage), pages = n == 0 ? 0 : (n + per - 1) / per;
        g.text(F_LIGHT, 17.f * s, x, yy + 14 * s, Ws(n == 0 ? std::string("No widgets chosen yet") : fmt("%d widget%s on %d page%s", n, n == 1 ? "" : "s", pages, pages == 1 ? "" : "s")), P.accent2);
        yy += 34 * s;
        auto lines = g.wrap(F_LIGHT, 15.5f * s, L"Pick the widgets and their order on the Widgets tab. With everything on one page nothing rotates. Press Tab on the Widgets face to skip to the next page.", lw, 0, 5);
        for (auto& l : lines) { g.text(F_LIGHT, 15.5f * s, x, yy + 14 * s, l, P.ink3); yy += 23 * s; }
    }

    g.line(rx - gap * 0.5f, top, rx - gap * 0.5f, top + h - 10 * s, P.line, std::max(1.f, s), false);
    float ry = top;
    if (a.rotSub == 0) {
        u.sectionLabel(rx, ry, "Timing"); ry += 28 * s;
        ry += u.stepper(rx, ry, rw, "Each panel stays", fmt("%d s", st.panelSecs),
                        [ap, nearest] { ap->st.panelSecs = SECS[std::max(0, nearest(ap->st.panelSecs) - 1)]; ap->cmd(CMD_SAVE); },
                        [ap, nearest] { ap->st.panelSecs = SECS[std::min(NS - 1, nearest(ap->st.panelSecs) + 1)]; ap->cmd(CMD_SAVE); });
    } else { u.sectionLabel(rx, ry, "Transition (shared with the Sky face)"); ry += 28 * s; }
    {
        const float h2 = 52 * s;
        g.text(F_LIGHT, 19.5f * s, rx, u.capMid(F_LIGHT, 19.5f * s, ry + h2 * 0.5f), L"Transition", P.ink);
        std::vector<std::wstring> names = {L"SLIDE", L"RISE", L"FADE"};
        float tot = 0; for (auto& n : names) tot += g.textWidth(F_MONO_M, 12.5f * s, n, 0.1f) + 24 * s;
        u.segmented(rx + rw - tot, ry + (h2 - 38 * s) * 0.5f, names, st.panelStyle, [ap](int i) { ap->st.panelStyle = i; ap->cmd(CMD_SAVE); });
        ry += h2;
    }
    ry += u.toggle(rx, ry, rw, "Pause while the pointer is on it", "So a card you are reading does not leave", st.panelHold);
    ry += u.toggle(rx, ry, rw, "Page dots", nullptr, st.panelDots);
    (void)yy; (void)ry;
}


// ---------------------------------------------------------------- holidays: which countries
void tabHolidays(UI& u, float x, float y, float w, float h) {
    App& a = u.a; Settings& st = a.st; Gfx& g = u.g; const Palette& P = u.P; const float s = u.s; App* ap = &a;
    (void)h;
    const float lw = w * 0.5f, gap = 48 * s, rx = x + lw + gap, rw = w - lw - gap;
    g.line(rx - gap * 0.5f, y, rx - gap * 0.5f, y + h - 10 * s, P.line, std::max(1.f, s), false);
    const int cur = holiday::countries();
    const char* const nationName = holiday::ukNation() == holiday::R_UK_SCT ? "Scotland" : holiday::ukNation() == holiday::R_UK_NI ? "Northern Ireland" : "England & Wales";

    float yy = y;
    u.sectionLabel(x, yy, "Whose holidays"); yy += 28 * s;
    {
        const std::string hint = st.holAuto ? (cur ? "Now: " + holiday::countryList(cur, holiday::ukNation()) : std::string("Your place has no list, so no holidays"))
                                            : std::string("Off: you choose the countries below");
        yy += u.toggleFn(x, yy, lw, "Pick by my place", hint.c_str(), st.holAuto, [ap] {
            ap->st.holAuto = !ap->st.holAuto;
            if (!ap->st.holAuto) ap->st.holMask = holiday::countries();       // switching to "by hand" keeps what is on now
        });
    }
    auto country = [&](int bit, const char* label, const char* hint) {
        yy += u.toggleFn(x, yy, lw, label, hint, (holiday::countries() & bit) != 0, [ap, bit] {
            ap->st.holAuto = false; ap->st.holMask = holiday::countries() ^ bit;
        });
    };
    country(holiday::C_LK, "Sri Lanka", "LK  \xC2\xB7  public holidays and Poya days");
    country(holiday::C_UK, "United Kingdom", "UK  \xC2\xB7  bank holidays");
    country(holiday::C_US, "United States", "US  \xC2\xB7  federal holidays");
    if (cur & holiday::C_UK) {
        yy += 6 * s;
        g.text(F_LIGHT, 19.5f * s, x, u.capMid(F_LIGHT, 19.5f * s, yy + 14 * s), L"UK list for", P.ink);
        g.text(F_LIGHT, 14.f * s, x + lw, yy + 20 * s, Ws(std::string(st.ukPref == 0 ? "from your place: " : "using: ") + nationName), P.ink3, Align::Right);
        yy += 36 * s;
        std::vector<std::wstring> names = {L"AUTO", L"ENG & WALES", L"SCOTLAND", L"N. IRELAND"};
        u.segmented(x, yy, names, st.ukPref, [ap](int i) { ap->st.ukPref = i; ap->dayIndex = -1; ap->minuteCacheAt = -1e9; ap->cmd(CMD_SAVE); });
        yy += 46 * s;
    }
    yy += 10 * s;
    auto lines = g.wrap(F_LIGHT, 14.f * s, L"Switch on as many countries as you like. Their holidays are merged, and every holiday carries its country code (LK, UK, US). With Pick by my place on, Meridian uses the country of your place (FIND MY LOCATION, Place tab). A place in any other country gets no holidays.", lw, 0, 6);
    for (auto& l : lines) { g.text(F_LIGHT, 14.f * s, x, yy + 12 * s, l, P.ink3); yy += 20 * s; }

    float ry = y;
    u.sectionLabel(rx, ry, "Where they show"); ry += 28 * s;
    ry += u.toggle(rx, ry, rw, "On the month calendar", "Hover a date for the full story", st.holCal);
    {
        auto t = g.wrap(F_LIGHT, 14.f * s, L"The same countries are used in Today (Elements tab), on the Holidays panel (Rotation tab) and in the Holidays widget.", rw, 0, 3);
        for (auto& l : t) { g.text(F_LIGHT, 14.f * s, rx, ry + 12 * s, l, P.ink3); ry += 20 * s; }
    }
    ry += 18 * s;
    u.sectionLabel(rx, ry, "Dates"); ry += 28 * s;
    {
        auto t = g.wrap(F_LIGHT, 14.f * s, L"UK bank holidays and US federal holidays follow the law's rules and match the official 2026 to 2028 calendars. A holiday on a weekend shows the day off it moves to.", rw, 0, 4);
        for (auto& l : t) { g.text(F_LIGHT, 14.f * s, rx, ry + 12 * s, l, P.ink3); ry += 20 * s; }
    }
    ry += 10 * s;
    {
        auto t = g.wrap(F_LIGHT, 14.f * s, L"Sri Lanka follows the Government's published lists for 2026 and 2027; other years are estimated until they are gazetted. When a new list comes out, type its dates into holidays.txt (Sri Lanka only) and they replace the estimates, with no restart.", rw, 0, 6);
        for (auto& l : t) { g.text(F_LIGHT, 14.f * s, rx, ry + 12 * s, l, P.ink3); ry += 20 * s; }
    }
    ry += 10 * s;
    u.buttonAuto(rx, ry, L"OPEN HOLIDAYS.TXT", [ap] { ap->cmd(CMD_OPEN_HOLIDAYS); });
    ry += 52 * s;
    if (!a.holStatus.empty()) {
        auto bad = g.wrap(F_LIGHT, 14.f * s, Ws("holidays.txt: " + a.holStatus), rw, 0, 3);
        for (auto& l : bad) { g.text(F_LIGHT, 14.f * s, rx, ry + 12 * s, l, P.accent); ry += 20 * s; }
    } else if (a.holCount > 0) {
        g.text(F_LIGHT, 14.f * s, rx, ry + 12 * s, Ws(util::fmt("%d date%s read from holidays.txt", a.holCount, a.holCount == 1 ? "" : "s")), P.ink3);
        ry += 20 * s;
    }
    (void)yy; (void)ry;
}

// ---------------------------------------------------------------- sky map orientation
const char* compassWord(double deg) {
    static const char* n[16] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
    return n[(int)std::floor(astro::norm360(deg) / 22.5 + 0.5) % 16];
}

void tabSkyMap(UI& u, float x, float y, float w, float h) {
    App& a = u.a; Settings& st = a.st; Gfx& g = u.g; const Palette& P = u.P; const float s = u.s; App* ap = &a;
    const float lw = w * 0.5f, gap = 48 * s, rx = x + lw + gap, rw = w - lw - gap;
    const double hd = a.skyHeadingCur;

    // ---- left: which direction is at the top of the map
    u.sectionLabel(x, y, "The top of the map faces"); float yy = y + 34 * s;
    const float R = 96 * s, dcx = x + R + 14 * s, dcy = yy + R + 10 * s;
    g.fillCircle(dcx, dcy, R, mulA(P.ink, 0.05f));
    g.strokeCircle(dcx, dcy, R, P.line, std::max(1.f, s));
    g.strokeCircle(dcx, dcy, R * 0.55f, mulA(P.ink, 0.07f), std::max(1.f, s));
    for (int i = 0; i < 72; ++i) {
        const float ang = (float)astro::rad(i * 5.0), len = (i % 18 == 0) ? 12.f * s : (i % 6 == 0 ? 8.f * s : 4.f * s);
        g.line(dcx + (R - len) * std::sin(ang), dcy - (R - len) * std::cos(ang), dcx + R * std::sin(ang), dcy - R * std::cos(ang), mulA(P.ink, (i % 18 == 0) ? 0.45f : 0.2f), std::max(1.f, s), false);
    }
    {
        struct L { const wchar_t* t; float ang; } ls[4] = {{L"N", 0}, {L"E", 90}, {L"S", 180}, {L"W", 270}};
        for (auto& l : ls) {
            const float ang = (float)astro::rad(l.ang), lr = R - 28 * s, fs = 15.f * s;
            g.text(F_MONO_M, fs, dcx + lr * std::sin(ang), dcy - lr * std::cos(ang) + g.capHeight(F_MONO_M, fs) * 0.5f, l.t, l.ang == 0 ? P.accent : P.ink2, Align::Center, 0.f);
        }
        const float ang = (float)astro::rad(hd), nr = R - 6 * s;
        g.line(dcx, dcy, dcx + nr * std::sin(ang), dcy - nr * std::cos(ang), u.acc(), std::max(2.f, 2.6f * s), true);
        g.fillCircle(dcx + nr * std::sin(ang), dcy - nr * std::cos(ang), 6.5f * s, u.acc());
        g.fillCircle(dcx, dcy, 4.f * s, mulA(P.ink, 0.5f));
        u.act(dcx - R, dcy - R, 2 * R, 2 * R, [ap, dcx, dcy] {
            const double ang = astro::deg(std::atan2((double)(ap->mouseX - dcx), -(double)(ap->mouseY - dcy)));
            ap->skyHeadingSet(std::round(ang)); ap->edHeading.clear(); ap->cmd(CMD_SAVE);
        });
    }
    {   // readout
        const float tx = dcx + R + 36 * s;
        std::string deg = fmt("%.0f", st.skySpin ? std::floor(hd) : std::round(hd));
        g.text(F_LIGHT, 62.f * s, tx, dcy - 4 * s, Ws(deg) + L"°", P.ink, Align::Left);
        g.text(F_MONO_M, 15.f * s, tx, dcy + 26 * s, Ws(std::string(compassWord(hd))), u.acc(), Align::Left, 0.14f);
        g.text(F_LIGHT, 14.f * s, tx, dcy + 52 * s, st.skySpin ? L"turning right now" : L"is at the top of the map", P.ink3, Align::Left);
    }
    yy = dcy + R + 22 * s;
    {   // quick directions
        static const struct { const char* n; double d; } dirs[8] = {{"N", 0}, {"NE", 45}, {"E", 90}, {"SE", 135}, {"S", 180}, {"SW", 225}, {"W", 270}, {"NW", 315}};
        float cx = x, cy = yy;
        for (auto& d : dirs) {
            const bool on = !st.skySpin && std::fabs(astro::norm180(st.skyHeading - d.d)) < 0.5;
            const double dd = d.d;
            u.chip(cx, cy, x, x + lw, d.n, on, [ap, dd] { ap->skyHeadingSet(dd); ap->edHeading.clear(); ap->cmd(CMD_SAVE); });
        }
        yy = cy + 34 * s + 18 * s;
    }
    {   // an exact angle
        if (a.textTarget != &a.edHeading) { if (a.edHeading.empty()) a.edHeading = fmt("%.0f", st.skyHeading); }
        {   // clicking into the box starts a fresh number instead of appending to the old one
            static bool wasFocus = false;
            const bool foc = a.textTarget == &a.edHeading;
            if (foc && !wasFocus) a.edHeading.clear();
            wasFocus = foc;
        }
        const float fw = 150 * s;
        u.field(x, yy, fw, 46 * s, "Or type an angle  (0 to 359)", a.edHeading, 5, true, false, [ap] {
            double v; if (parseNum(ap->edHeading, 0, 360, v)) { ap->skyHeadingSet(v >= 360 ? 0 : v); ap->cmd(CMD_SAVE); }
        }, "124");
        u.buttonAuto(x + fw + 16 * s, yy + 22 * s + 5 * s, L"RESET TO NORTH", [ap] { ap->skyHeadingSet(0); ap->edHeading.clear(); ap->textTarget = nullptr; ap->cmd(CMD_SAVE); });
    }

    // ---- right: free rotation
    g.line(rx - gap * 0.5f, y, rx - gap * 0.5f, y + h - 10 * s, P.line, std::max(1.f, s), false);
    float ry = y;
    u.sectionLabel(rx, ry, "Free rotate"); ry += 28 * s;
    ry += u.toggle(rx, ry, rw, "Turn the sky slowly", "Everything turns together, so it stays true", st.skySpin, [ap] { ap->skySpinChanged(); ap->edHeading.clear(); });
    static const int SPINS[] = {20, 30, 60, 120, 300, 600, 1800, 3600};
    static constexpr int NSP = (int)(sizeof SPINS / sizeof SPINS[0]);
    auto spinIdx = [](int v) { int best = 0; for (int i = 0; i < NSP; ++i) if (std::abs(SPINS[i] - v) < std::abs(SPINS[best] - v)) best = i; return best; };
    auto spinText = [](int sec) { return sec < 60 ? fmt("%d s", sec) : sec < 3600 ? fmt("%d min", sec / 60) : fmt("%d hour", sec / 3600); };
    ry += u.stepper(rx, ry, rw, "One full turn takes", spinText(st.skySpinSecs),
                    [ap, spinIdx] { ap->skySpinChanged(); ap->st.skySpinSecs = SPINS[std::max(0, spinIdx(ap->st.skySpinSecs) - 1)]; ap->skySpinChanged(); ap->cmd(CMD_SAVE); },
                    [ap, spinIdx] { ap->skySpinChanged(); ap->st.skySpinSecs = SPINS[std::min(NSP - 1, spinIdx(ap->st.skySpinSecs) + 1)]; ap->skySpinChanged(); ap->cmd(CMD_SAVE); });
    {
        const float h2 = 52 * s;
        g.text(F_LIGHT, 19.5f * s, rx, u.capMid(F_LIGHT, 19.5f * s, ry + h2 * 0.5f), L"Turns", P.ink);
        std::vector<std::wstring> names = {L"CLOCKWISE", L"COUNTER-CLOCKWISE"};
        float tot = 0; for (auto& n : names) tot += g.textWidth(F_MONO_M, 12.5f * s, n, 0.1f) + 24 * s;
        const bool narrow = tot + g.textWidth(F_LIGHT, 19.5f * s, L"Turns") + 24 * s > rw;
        if (narrow) ry += 40 * s;
        u.segmented(narrow ? rx : rx + rw - tot, narrow ? ry : ry + (h2 - 38 * s) * 0.5f, names, st.skySpinDir > 0 ? 0 : 1, [ap](int i) { ap->skySpinChanged(); ap->st.skySpinDir = i == 0 ? 1 : -1; ap->skySpinChanged(); ap->cmd(CMD_SAVE); });
        ry += h2;
    }
    ry += 14 * s;
    {
        auto lines = g.wrap(F_LIGHT, 14.5f * s, L"The Sun, Moon, planets, stars and the compass letters all turn together, so nothing moves relative to anything else. Turn it off and the map stays facing the direction it stopped at. Free rotate redraws the sky 30 times a second, which uses a little more power than the usual once a second.", rw, 0, 8);
        for (auto& l : lines) { g.text(F_LIGHT, 14.5f * s, rx, ry + 12 * s, l, P.ink3); ry += 21 * s; }
    }
    ry += 16 * s;
    u.divider(rx, ry, rw); ry += 18 * s;
    u.sectionLabel(rx, ry, "Star patterns"); ry += 26 * s;
    ry += u.toggle(rx, ry, rw, "Show star patterns during the day", "Off by default — constellations appear only at night", st.dayStars);
    (void)h;
}

// ---------------------------------------------------------------- the Quiet face
void tabQuiet(UI& u, float x, float y, float w, float h) {
    App& a = u.a; Settings& st = a.st; Gfx& g = u.g; const Palette& P = u.P; const float s = u.s; App* ap = &a;
    const float lw = w * 0.5f, gap = 48 * s, rx = x + lw + gap, rw = w - lw - gap;
    float yy = y;
    u.sectionLabel(x, yy, "Weather behind the clock"); yy += 28 * s;
    yy += u.toggle(x, yy, lw, "Show the weather", "Drawn behind the time, with one cloud drifting across it", st.quietFx);
    yy += u.stepper(x, yy, lw, "Strength", fmt("%.0f%%", st.quietStrength * 100.f),
                    [ap] { ap->st.quietStrength = std::max(0.5f, std::round((ap->st.quietStrength - 0.25f) * 4.f) / 4.f); ap->cmd(CMD_SAVE); },
                    [ap] { ap->st.quietStrength = std::min(2.0f, std::round((ap->st.quietStrength + 0.25f) * 4.f) / 4.f); ap->cmd(CMD_SAVE); });
    yy += u.toggle(x, yy, lw, "Sun flare", "On the Quiet and Sky faces, at sunrise and sunset", st.quietFlare);
    yy += u.toggle(x, yy, lw, "Rain & snow on the Sky tab", "Falls behind the map, never over the labels", st.skyRain);
    yy += u.toggle(x, yy, lw, "Smooth motion", "Rain, snow and wind at refresh rate (off: 30 fps)", st.quietSmooth);
    yy += 8 * s;
    u.sectionLabel(x, yy, "Preview"); yy += 22 * s;
    {
        std::vector<std::wstring> names = {L"LIVE", L"CLOUDS", L"FOG", L"WIND", L"GALE", L"BOTH", L"DRIZZLE", L"RAIN", L"SLEET", L"HAIL", L"SNOW", L"BLIZZARD", L"THUNDER", L"T-STORM", L"TORNADO", L"SUN FLARE"};
        static const int previewIdx[16] = {0, 1, 8, 2, 15, 3, 12, 4, 13, 14, 10, 11, 6, 7, 5, 9};
        float cx = x, cy = yy;
        const float cfs = 10.6f * s, ch = 25 * s, cgap = 3.6f * s;
        for (size_t i = 0; i < names.size(); ++i) {
            int idx = previewIdx[i];
            const bool on = st.quietPreview == idx;
            const float cw = g.textWidth(F_LIGHT, cfs, names[i]) + 8.5f * s;
            if (cx + cw > x + lw + 1.f && cx > x) { cx = x; cy += ch + cgap; }
            const bool hv = u.over(cx, cy, cw, ch);
            const uint64_t gkey = a.nextActionGlassKey();
            const glassfx::Pose gp = a.glass.pose(gkey, cx, cy, cw, ch, ch * 0.5f, s, hv, false);
            if (on) { g.fillRoundRect(gp.x, gp.y, gp.w, gp.h, gp.r, withA(u.acc(), 0.20f + 0.06f * gp.hoverT)); g.strokeRoundRect(gp.x + 0.5f, gp.y + 0.5f, std::max(1.f, gp.w - 1.f), std::max(1.f, gp.h - 1.f), std::max(0.5f, gp.r - 0.5f), withA(u.acc(), 0.8f), std::max(1.f, s)); }
            else { g.fillRoundRect(gp.x, gp.y, gp.w, gp.h, gp.r, mulA(P.ink, 0.05f + 0.06f * gp.hoverT)); }
            drawGlassEffects(u.c, gkey, gp, false, false);
            g.text(F_LIGHT, cfs, gp.x + gp.w * 0.5f, u.capMid(F_LIGHT, cfs, gp.y + gp.h * 0.5f), names[i], on ? P.ink : lerp(P.ink2, P.ink, gp.hoverT), Align::Center);
            u.act(cx, cy, cw, ch, [ap, idx] { ap->st.quietPreview = idx; ap->st.quietFx = true; if (idx == 9) ap->st.quietFlare = true; ap->cmd(CMD_SAVE); ap->setToast(idx == 0 ? L"Quiet face follows the live weather" : L"Previewing: open the Quiet face with 2"); });
            cx += cw + cgap;
        }
        yy = cy + ch + 8 * s;
    }
    {
        auto lines = g.wrap(F_LIGHT, 13.4f * s, L"Pick one to preview on Quiet (key 2). Set back to Live to follow real weather.", lw, 0, 2);
        for (auto& l : lines) { g.text(F_LIGHT, 13.4f * s, x, yy + 10 * s, l, P.ink3); yy += 18 * s; }
    }
    yy += 8 * s;
    u.sectionLabel(x, yy, "Named storms"); yy += 24 * s;
    yy += u.toggle(x, yy, lw, "Show storms near me", "Cyclone, typhoon or hurricane name, distance, direction", st.stormWatch, [ap] { ap->cmd(CMD_STORM_CFG); });
    {
        static const int steps[6] = {300, 500, 1000, 1500, 2500, 4000};
        auto idxOf = [](int km) { int b = 0; for (int i = 0; i < 6; ++i) if (std::abs(steps[i] - km) < std::abs(steps[b] - km)) b = i; return b; };
        yy += u.stepper(x, yy, lw, "Radius in km", fmt("%d", st.stormRadius),
                        [ap, idxOf] { ap->st.stormRadius = steps[std::max(0, idxOf(ap->st.stormRadius) - 1)]; ap->cmd(CMD_SAVE); },
                        [ap, idxOf] { ap->st.stormRadius = steps[std::min(5, idxOf(ap->st.stormRadius) + 1)]; ap->cmd(CMD_SAVE); });
    }
    if (st.stormWatch) {                                       // when the storm reports were fetched, from where, and a button to ask again
        const double stormAt = a.stormData.fetched > 0 ? a.stormData.fetched : a.stormOk;
        const std::string line = a.stormBusy ? std::string("Asking for storm reports\xE2\x80\xA6")
                               : stormAt > 0 ? "Fetched " + stampText(a, stormAt) + (a.stormData.source.empty() ? std::string() : " from " + a.stormData.source)
                               : std::string("No storm report yet");
        g.text(F_LIGHT, 14.5f * s, x, yy + 14 * s, g.ellipsize(F_LIGHT, 14.5f * s, Ws(line), lw - 130 * s), P.ink2);
        u.buttonAuto(x + lw - (g.textWidth(F_MONO_M, 12.5f * s, L"REFRESH", 0.12f) + 36 * s), yy - 2 * s, L"REFRESH", [ap] { ap->cmd(CMD_STORM_REFRESH); });
        yy += 36 * s;
    }
    {
        std::string note;
        if (!st.stormWatch) note = "Off. No storm names are downloaded.";
        else if (!a.stormData.valid) note = "Waiting for the first storm report (GDACS, then NASA EONET).";
        else if (a.stormNear.empty()) note = fmt("No named storm within %d km. %d active in the world. Source: %s.", st.stormRadius, (int)a.stormData.list.size(), a.stormData.source.c_str());
        else { note = storms::title(a.stormNear[0]) + ", " + storms::where(a.stormNear[0]); const std::string w = storms::winds(a.stormNear[0]); if (!w.empty()) note += ", " + w; note += ". Source: " + a.stormData.source + "."; }
        auto lines = g.wrap(F_LIGHT, 14.5f * s, Ws(note), lw, 0, 3);
        for (auto& l : lines) { g.text(F_LIGHT, 14.5f * s, x, yy + 14 * s, l, st.stormWatch && !a.stormNear.empty() ? u.acc() : P.ink3); yy += 21 * s; }
    }

    g.line(rx - gap * 0.5f, y, rx - gap * 0.5f, y + h - 10 * s, P.line, std::max(1.f, s), false);
    float ry = y;
    u.sectionLabel(rx, ry, "Right now"); ry += 30 * s;
    {
        std::string now = quietFxDescribe(a);
        auto lines = g.wrap(F_SERIF_I, 26.f * s, Ws(now), rw, 0, 3);
        for (auto& l : lines) { g.text(F_SERIF_I, 26.f * s, rx, ry + 26 * s, l, P.ink); ry += 34 * s; }
        if (a.wx.valid) {
            const auto& c = a.wx.cur;
            std::string d = fmt("Cloud %.0f%%  \xC2\xB7  wind %.0f km/h  \xC2\xB7  gusts %.0f km/h", c.cloud, c.wind, c.gust);
            g.text(F_MONO, 12.5f * s, rx, ry + 14 * s, Ws(util::upper(d)), P.ink3, Align::Left, 0.1f);
            ry += 30 * s;
        }
    }
    ry += 14 * s; u.divider(rx, ry, rw); ry += 20 * s;
    u.sectionLabel(rx, ry, "What shows up"); ry += 30 * s;
    struct Row { const wchar_t* k; const wchar_t* d; };
    static const Row rows[] = {
        {L"CLOUDS", L"sculpted cirrus wisps, cumulus towers and stratocumulus decks with directional light, plus a soft foreground veil"},
        {L"FOG", L"layered valley mist banks, diffused sky glow, curling gossamer tendrils and floating dew-light motes"},
        {L"WIND & GALE", L"painterly breeze ribbons from 12 km/h; strong gusts unleash sweeping gale streams and swirling leaf whirlwinds"},
        {L"DRIZZLE", L"silky ultra-fine mist rain falling lazily with whisper-thin surface ripples and damp low haze"},
        {L"RAIN", L"streaks down in three depths with splashes where drops land, and falls behind the Sky face"},
        {L"SLEET & HAIL", L"freezing icy streaks mixed with wet snow for sleet; faceted ice stones with energetic ground bounces for hail"},
        {L"SNOW", L"calm layered snowflakes drift softly; strong winter winds whip them into a driving blizzard"},
        {L"STORMS", L"dark cloud and lightning for thunder; a twisting tornado for severe warm gusts of 70 km/h or more"},
        {L"SUN FLARE", L"a soft golden flare from the Sun's side while it is low; clear and calm skies show nothing at all"}};
    for (const Row& r : rows) {
        g.text(F_MONO_M, 11.3f * s, rx, ry + 13 * s, r.k, u.acc(), Align::Left, 0.10f);
        auto lines = g.wrap(F_LIGHT, 14.2f * s, r.d, rw - 118 * s, 0, 3);
        for (auto& l : lines) { g.text(F_LIGHT, 14.2f * s, rx + 118 * s, ry + 13 * s, l, P.ink2); ry += 19.5f * s; }
        ry += 4 * s;
    }
}

// ---------------------------------------------------------------- sky events
Color kindColor(UI& u, int kind) {
    switch (kind) {
        case skyev::METEOR: return u.P.accent2;
        case skyev::SOLAR_ECLIPSE: case skyev::LUNAR_ECLIPSE: return rgb(0xFF9F7A);
        default: return u.acc();
    }
}
void tabSky(UI& u, float x, float y, float w, float h) {
    App& a = u.a; Settings& st = a.st; Gfx& g = u.g; const Palette& P = u.P; const float s = u.s; App* ap = &a;
    const float lw = w * 0.5f, gap = 48 * s, rx = x + lw + gap, rw = w - lw - gap;
    auto refresh = [ap] { ap->refreshEvents(true); };
    float yy = y;
    u.sectionLabel(x, yy, "Show"); yy += 28 * s;
    yy += u.toggle(x, yy, lw, "Meteor showers", "A shower of streaks, with its radiant point", st.evMeteors, refresh);
    yy += u.toggle(x, yy, lw, "Planets lining up", "Meetings, oppositions and planet parades", st.evPlanets, refresh);
    yy += u.toggle(x, yy, lw, "Eclipses", "Solar and lunar, drawn on the Sun and Moon", st.evEclipses, refresh);
    yy += u.toggle(x, yy, lw, "Event chips under the map", "Tap or hover them for details", st.evChips);
    yy += u.toggle(x, yy, lw, "Animate meteors", "Off keeps the screen perfectly still", st.evAnimate);
    yy += 8 * s;
    yy += u.stepper(x, yy, lw, "Show events within", fmt("%d day%s", st.evDays, st.evDays == 1 ? "" : "s"),
                    [ap] { ap->st.evDays = std::max(1, ap->st.evDays - 1); ap->refreshEvents(true); ap->cmd(CMD_SAVE); },
                    [ap] { ap->st.evDays = std::min(30, ap->st.evDays + 1); ap->refreshEvents(true); ap->cmd(CMD_SAVE); });

    g.line(rx - gap * 0.5f, y, rx - gap * 0.5f, y + h - 10 * s, P.line, std::max(1.f, s), false);
    u.sectionLabel(rx, y, "Coming up");
    float ry = y + 30 * s;
    if (a.events.empty()) {
        auto lines = g.wrap(F_SERIF_I, 24.f * s, Ws(fmt("Nothing in the next %d day%s.", st.evDays, st.evDays == 1 ? "" : "s")), rw, 0, 3);
        for (auto& l : lines) { g.text(F_SERIF_I, 24.f * s, rx, ry + 24 * s, l, P.ink2); ry += 32 * s; }
    }
    int n = 0;
    for (const auto& e : a.events) {
        if (n++ >= 7) break;
        const double d = e.daysAway(a.now);
        std::string when = e.active(a.now) ? "NOW" : d < 1.0 ? fmt("IN %d H", std::max(1, (int)std::lround(d * 24.0))) : fmt("IN %d D", (int)std::lround(d));
        const float mid = ry + 22 * s;
        g.fillCircle(rx + 6 * s, mid, 4.5f * s, kindColor(u, e.kind));
        g.text(F_LIGHT, 18.5f * s, rx + 24 * s, u.capMid(F_LIGHT, 18.5f * s, mid), g.ellipsize(F_LIGHT, 18.5f * s, Ws(e.title), rw - 24 * s - 96 * s), P.ink);
        g.text(F_MONO_M, 12.5f * s, rx + rw, u.capMid(F_MONO_M, 12.5f * s, mid), Ws(when), P.ink3, Align::Right, 0.12f);
        ry += 44 * s;
    }
    {
        auto lines = g.wrap(F_LIGHT, 14.f * s, L"Worked out on this PC from the Sun, Moon and planets. No network needed.", rw, 0, 3);
        float fy = y + h - 12 * s - (float)(lines.size() - 1) * 20 * s;
        for (auto& l : lines) { g.text(F_LIGHT, 14.f * s, rx, fy, l, P.ink3); fy += 20 * s; }
    }
}

// ---------------------------------------------------------------- weather
// Left: all eight sources, optional free API keys, and optional MET Norway / NWS contact.
// Right: when weather, air quality, and storms were last fetched and from where.
void tabWeather(UI& u, float x, float y, float w, float h) {
    App& a = u.a; Settings& st = a.st; Gfx& g = u.g; const Palette& P = u.P; const float s = u.s; App* ap = &a;
    const float lw = w * 0.52f, gap = 44 * s, rx = x + lw + gap, rw = w - lw - gap;
    u.sectionLabel(x, y, "Sources, in the order they are tried");
    float yy = y + 24 * s;
    const float rowH = 48 * s;
    auto hasKey = [](const std::string& k) { for (unsigned char c : k) if (c > ' ') return true; return false; };
    for (int i = 0; i < weather::PV_COUNT; ++i) {
        const weather::ProviderStatus& ps = a.wxProv[i];
        const bool en = st.provEnabled[i];
        const bool needsKey = weather::providerNeedsKey(i);
        const bool keyOk = !needsKey || (i == weather::PV_WEATHERAPI ? hasKey(st.weatherApiKey) : hasKey(st.openWeatherKey));
        const bool hvProv = u.over(x - 10 * s, yy, lw + 20 * s, rowH);
        if (hvProv) g.fillRoundRect(x - 10 * s, yy + 2 * s, lw + 20 * s, rowH - 4 * s, 12 * s, mulA(P.ink, 0.05f));
        const float mid = yy + rowH * 0.5f;
        const bool inUse = a.wx.valid && a.wx.source == weather::providerName(i);
        Color dotC = !en ? mulA(P.ink, 0.25f) : (ps.ok || inUse) ? P.accent2 : (needsKey && !keyOk) ? mulA(P.ink, 0.35f) : ps.tried ? rgb(0xFF9F7A) : mulA(P.ink, 0.45f);
        g.fillCircle(x + 8 * s, mid, 4.5f * s, dotC);
        g.text(F_LIGHT, 17.5f * s, x + 28 * s, mid - 2 * s, Ws(weather::providerName(i)), en ? P.ink : P.ink3);
        std::string status;
        if (!en) status = "off";
        else if (inUse) status = "in use \xC2\xB7 " + agoText(a.now, a.wx.fetched);
        else if (needsKey && !keyOk) status = "optional \xC2\xB7 add free API key below";
        else if (ps.ok) status = "working \xC2\xB7 " + agoText(a.now, ps.lastOk);
        else if (ps.tried) status = ps.err.empty() ? "no answer" : ps.err;
        else status = i == 0 ? "waiting for the first answer" : (i == weather::PV_BRIGHTSKY ? "backup \xC2\xB7 DWD / Europe" : (i == weather::PV_NWS ? "backup \xC2\xB7 US / NOAA" : "kept as a backup"));
        g.text(F_LIGHT, 13.f * s, x + 28 * s, mid + 15.5f * s, g.ellipsize(F_LIGHT, 13.f * s, Ws(status), lw - 28 * s - 86 * s), (ps.ok || inUse) && en ? P.accent2 : P.ink3);
        u.drawSwitch(x + lw, mid, en, a.nextActionGlassKey(), hvProv);
        bool* vp = &st.provEnabled[i];
        u.act(x - 10 * s, yy, lw + 20 * s, rowH, [vp, ap] { *vp = !*vp; ap->cmd(CMD_WEATHER_CFG); ap->cmd(CMD_WEATHER_REFRESH); ap->cmd(CMD_SAVE); });
        yy += rowH;
    }
    yy += 10 * s;
    {
        const float kg = 12 * s, kw = (lw - kg) * 0.5f;
        const float fh = u.field(x, yy, kw, 38 * s, "WeatherAPI key  (optional)", st.weatherApiKey, 96, false, false, [ap] { ap->cmd(CMD_WEATHER_CFG); ap->cmd(CMD_SAVE); }, "free API key");
        u.field(x + kw + kg, yy, kw, 38 * s, "OpenWeather key  (optional)", st.openWeatherKey, 96, false, false, [ap] { ap->cmd(CMD_WEATHER_CFG); ap->cmd(CMD_SAVE); }, "free API key");
        yy += fh + 10 * s;
    }
    yy += u.field(x, yy, lw, 38 * s, "Contact for MET Norway / NWS  (optional)", st.contact, 80, false, false, [ap] { ap->cmd(CMD_WEATHER_CFG); ap->cmd(CMD_SAVE); }, "an email or a website");
    {
        auto lines = g.wrap(F_LIGHT, 12.8f * s, L"Keys & contact stay local in settings.ini. WeatherAPI and OpenWeatherMap also serve as Air Quality backups.", lw, 0, 2);
        float ty = yy + 10 * s;
        for (auto& l : lines) { g.text(F_LIGHT, 12.8f * s, x, ty + 11 * s, l, P.ink3); ty += 17 * s; }
    }

    // ---- right: last refreshed
    g.line(rx - gap * 0.5f, y, rx - gap * 0.5f, y + h - 10 * s, P.line, std::max(1.f, s), false);
    float ry = y;
    u.sectionLabel(rx, ry, "Weather, last fetched"); ry += 34 * s;
    {
        const bool have = a.wx.valid && a.wx.fetched > 0;
        g.text(F_SERIF, 34.f * s, rx, ry + 26 * s, g.ellipsize(F_SERIF, 34.f * s, Ws(have ? stampText(a, a.wx.fetched) : std::string("Nothing yet")), rw), P.ink);
        ry += 40 * s;
        std::string from = have ? (a.wx.source.empty() ? std::string("from a saved copy") : "from " + a.wx.source) : std::string();
        std::string line = have ? agoText(a.now, a.wx.fetched) + "  \xC2\xB7  " + from : (a.wxOffline ? a.wxStatus : std::string("Waiting for the first answer"));
        if (have && a.wxOffline) line += "  \xC2\xB7  " + a.wxStatus;
        g.text(F_LIGHT, 16.5f * s, rx, ry + 14 * s, g.ellipsize(F_LIGHT, 16.5f * s, Ws(line), rw), have && !a.wxOffline ? P.ink2 : rgb(0xFF9F7A));
        ry += 28 * s;
        if (have) {
            const std::string aqSrc = a.wx.airSource.empty() ? std::string("Open-Meteo") : a.wx.airSource;
            const std::string air = a.wx.hasAir
                ? fmt("Air quality from %s  \xC2\xB7  US AQI %.0f (%s)", aqSrc.c_str(), a.wx.aqi, weather::aqiLabel(a.wx.aqi))
                : std::string("Air quality  \xC2\xB7  waiting or unavailable (retrying automatically)");
            g.text(F_LIGHT, 14.5f * s, rx, ry + 14 * s, g.ellipsize(F_LIGHT, 14.5f * s, Ws(air), rw), P.ink3);
            ry += 26 * s;
        }
        if (a.wxStatus == "Refreshing\xE2\x80\xA6") { g.text(F_LIGHT, 14.5f * s, rx, ry + 14 * s, L"Asking the sources…", u.acc()); ry += 24 * s; }
        ry += 8 * s;
        float bx = rx;
        bx += u.buttonAuto(bx, ry, L"REFRESH WEATHER", [ap] { ap->cmd(CMD_WEATHER_REFRESH); }, true) + 10 * s;
        u.buttonAuto(bx, ry, L"OPEN LOG", [ap] { ap->cmd(CMD_OPEN_LOG); });
        ry += 56 * s;
    }
    u.divider(rx, ry, rw); ry += 22 * s;
    u.sectionLabel(rx, ry, "Cyclones and storms, last fetched"); ry += 34 * s;
    {
        const double stormAt = a.stormData.fetched > 0 ? a.stormData.fetched : a.stormOk;
        const bool have = stormAt > 0;
        if (!st.stormWatch) {
            g.text(F_SERIF, 30.f * s, rx, ry + 24 * s, L"Switched off", P.ink2); ry += 38 * s;
            g.text(F_LIGHT, 15.f * s, rx, ry + 14 * s, L"Turn on \"Show storms near me\" on the Quiet tab. Nothing is downloaded while it is off.", P.ink3); ry += 30 * s;
        } else {
            g.text(F_SERIF, 30.f * s, rx, ry + 24 * s, g.ellipsize(F_SERIF, 30.f * s, Ws(have ? stampText(a, stormAt) : std::string("Nothing yet")), rw), P.ink);
            ry += 38 * s;
            std::string line = a.stormBusy ? std::string("Asking for storm reports\xE2\x80\xA6")
                             : have ? agoText(a.now, stormAt) + "  \xC2\xB7  from " + (a.stormData.source.empty() ? std::string("GDACS") : a.stormData.source) + fmt("  \xC2\xB7  %d active in the world", (int)a.stormData.list.size())
                             : std::string("Waiting for the first report (GDACS, then NASA EONET)");
            { auto ls = g.wrap(F_LIGHT, 16.5f * s, Ws(line), rw, 0, 2); for (auto& l : ls) { g.text(F_LIGHT, 16.5f * s, rx, ry + 14 * s, l, P.ink2); ry += 23 * s; } ry += 5 * s; }
            if (!a.stormErr.empty() && !a.stormBusy) { g.text(F_LIGHT, 14.5f * s, rx, ry + 14 * s, g.ellipsize(F_LIGHT, 14.5f * s, Ws("Last try failed: " + a.stormErr), rw), rgb(0xFF9F7A)); ry += 24 * s; }
            ry += 8 * s;
            u.buttonAuto(rx, ry, L"REFRESH STORMS", [ap] { ap->cmd(CMD_STORM_REFRESH); }, true);
            ry += 54 * s;
        }
    }
    (void)h;
}

// ---------------------------------------------------------------- place
// the line under the location name: where it came from and how sure it is
std::string locStatusText(const App& a) {
    if (a.locBusy) return "Asking Windows for your position (GPS or Wi-Fi), then your internet address. This can take up to 20 seconds.";
    if (!a.located) return "Not found yet. Press the button. Until then Meridian shows its built-in default place.";
    if (a.locSource.empty()) return "Saved in settings.ini. Press the button to find where you are right now.";
    if (a.locSource == "set by hand") return "Set by hand. Kept until you press FIND MY LOCATION.";
    std::string t = "Found by " + a.locSource;
    if (a.locAcc >= 0) t += a.locAcc < 1000 ? fmt(", within about %d m", (int)std::lround(std::max(10.0, a.locAcc))) : fmt(", within about %.0f km", a.locAcc / 1000.0);
    else if (a.locSource == "IP address") t += " (city level only)";
    t += "  \xC2\xB7  " + stampText(a, a.locAt) + ". Kept until you press the button again.";
    if (a.locSource == "IP address") t += " An internet address can be off by a whole city: if it looks wrong, type the place below.";
    return t;
}
void applyHomeEdit(App* ap) {
    double la, lo; std::string nm = cleanName(ap->edName);
    if (nm.empty() || !parseNum(ap->edLat, -89, 89, la) || !parseNum(ap->edLon, -180, 180, lo)) return;
    ap->home.name = nm; ap->home.lat = la; ap->home.lon = lo;
    ap->homeCountry.clear(); ap->homeRegion.clear();       // a place typed by hand: its country is not known, the holidays work it out (Sri Lanka by position, else the Windows region)
    ap->located = true; ap->locSource = "set by hand"; ap->locAcc = -1; ap->locAt = 0; ap->locErr.clear();
    if (ap->placeIdx < 0) ap->cmd(CMD_GEO_SOON);
    ap->cmd(CMD_SAVE);
}
void tabPlace(UI& u, float x, float y, float w, float h) {
    App& a = u.a; Gfx& g = u.g; const Palette& P = u.P; const float s = u.s; App* ap = &a;
    if (!a.edInit) { a.edName = a.home.name; a.edLat = fmt("%.4f", a.home.lat); a.edLon = fmt("%.4f", a.home.lon); a.edInit = true; }
    const float lw = w * 0.44f, gap = 48 * s, rx = x + lw + gap, rw = w - lw - gap;
    auto onHome = [ap] { applyHomeEdit(ap); };
    float yy = y;
    u.sectionLabel(x, yy, "Your location"); yy += 28 * s;
    u.buttonAuto(x, yy, a.locBusy ? L"LOOKING\u2026" : L"FIND MY LOCATION", [ap] { ap->cmd(CMD_LOCATE); }, true);
    yy += 48 * s;
    g.text(F_SERIF, 30.f * s, x, yy + 24 * s, g.ellipsize(F_SERIF, 30.f * s, Ws(a.home.name), lw), P.ink); yy += 36 * s;
    {
        auto ls = g.wrap(F_LIGHT, 14.5f * s, Ws(locStatusText(a)), lw, 0, 4);
        for (auto& l : ls) { g.text(F_LIGHT, 14.5f * s, x, yy + 12 * s, l, P.ink2); yy += 20 * s; }
        if (!a.locErr.empty() && !a.locBusy) {
            auto es = g.wrap(F_LIGHT, 14.5f * s, Ws(a.locErr), lw, 0, 5);
            yy += 4 * s;
            for (auto& l : es) { g.text(F_LIGHT, 14.5f * s, x, yy + 12 * s, l, rgb(0xFF9F7A)); yy += 20 * s; }
            yy += 6 * s;
            u.buttonAuto(x, yy, L"OPEN WINDOWS LOCATION SETTINGS", [ap] { ap->cmd(CMD_OPEN_LOCATION_SETTINGS); });
            yy += 44 * s;
        }
        yy += 8 * s;
    }
    u.divider(x, yy, lw); yy += 16 * s;
    u.sectionLabel(x, yy, "Or set it by hand"); yy += 28 * s;
    yy += u.field(x, yy, lw, 46 * s, "Name", a.edName, 40, false, false, onHome);
    const float half = (lw - 14 * s) * 0.5f;
    u.field(x, yy, half, 46 * s, "Latitude", a.edLat, 12, true, false, onHome);
    yy += u.field(x + half + 14 * s, yy, half, 46 * s, "Longitude", a.edLon, 12, true, false, onHome);
    double t1, t2;
    const bool bad = !parseNum(a.edLat, -89, 89, t1) || !parseNum(a.edLon, -180, 180, t2);
    g.text(F_LIGHT, 14.5f * s, x, yy + 6 * s, bad ? L"Latitude -89 to 89, longitude -180 to 180" : L"Positive is north and east, negative is south and west.", bad ? rgb(0xFF9F7A) : P.ink3);
    yy += 36 * s;
    if (yy + 62 * s < y + h) {
        auto lines = g.wrap(F_LIGHT, 14.5f * s, L"Tip: right-click a spot in Google Maps and the first line of the menu is its latitude and longitude.", lw, 0, 3);
        for (auto& l : lines) { g.text(F_LIGHT, 14.5f * s, x, yy + 12 * s, l, P.ink3); yy += 20 * s; }
    }

    g.line(rx - gap * 0.5f, y, rx - gap * 0.5f, y + h - 10 * s, P.line, std::max(1.f, s), false);
    float ry = y;
    u.sectionLabel(rx, ry, "Showing now and saved places"); ry += 28 * s;
    auto row = [&](const std::string& name, const std::string& sub, bool current, std::function<void()> use, std::function<void()> del) {
        const float rh = 54 * s, mid = ry + rh * 0.5f;
        if (current) g.fillRoundRect(rx - 8 * s, ry + 2 * s, rw + 16 * s, rh - 4 * s, 14 * s, mulA(P.ink, 0.06f));
        g.text(F_LIGHT, 19.f * s, rx, mid - 2 * s, g.ellipsize(F_LIGHT, 19.f * s, Ws(name), rw - 150 * s), P.ink);
        g.text(F_MONO, 11.5f * s, rx, mid + 17 * s, Ws(sub), P.ink3, Align::Left, 0.08f);
        float bx = rx + rw;
        if (del) {
            bx -= 28 * s;
            const bool hv = u.over(bx - 14 * s, mid - 16 * s, 32 * s, 32 * s);
            g.line(bx - 5 * s, mid - 5 * s, bx + 5 * s, mid + 5 * s, hv ? P.ink : P.ink3, std::max(1.5f, 1.7f * s), true);
            g.line(bx - 5 * s, mid + 5 * s, bx + 5 * s, mid - 5 * s, hv ? P.ink : P.ink3, std::max(1.5f, 1.7f * s), true);
            u.act(bx - 14 * s, mid - 16 * s, 32 * s, 32 * s, std::move(del));
            bx -= 10 * s;
        }
        if (current) { g.text(F_MONO_M, 11.5f * s, bx, u.capMid(F_MONO_M, 11.5f * s, mid), L"SHOWING", withA(P.accent2, 0.95f), Align::Right, 0.14f); }
        else { const float bw = 64 * s; u.button(bx - bw, mid - 17 * s, bw, 34 * s, L"USE", std::move(use)); }
        ry += rh;
    };
    row(a.home.name + "  \xC2\xB7  your location", coordText(a.home), a.placeIdx < 0, [ap] { ap->placeIdx = -1; ap->cmd(CMD_GEO); ap->cmd(CMD_SAVE); }, nullptr);
    const int shown = (int)std::min<size_t>(3, a.st.places.size());
    for (int i = 0; i < shown; ++i) {
        const astro::Geo& p = a.st.places[i];
        row(p.name, coordText(p), a.placeIdx == i,
            [ap, i] { ap->placeIdx = i; ap->cmd(CMD_GEO); ap->cmd(CMD_SAVE); },
            [ap, i] {
                if (i >= (int)ap->st.places.size()) return;
                ap->st.places.erase(ap->st.places.begin() + i);
                if (ap->placeIdx == i) { ap->placeIdx = -1; ap->cmd(CMD_GEO); }
                else if (ap->placeIdx > i) ap->placeIdx--;
                ap->cmd(CMD_SAVE);
            });
    }
    if ((int)a.st.places.size() > shown) { g.text(F_LIGHT, 14.f * s, rx, ry + 14 * s, Ws(fmt("and %d more in settings.ini", (int)a.st.places.size() - shown)), P.ink3); ry += 26 * s; }
    ry += 8 * s;
    ry += u.toggle(rx, ry, rw, "Use saved place's local time", "Show a saved place's own clock when cycling with N", a.st.placeLocalTime, [ap] { ap->dayIndex = -1; ap->minuteCacheAt = -1e9; });
    ry += 10 * s;
    u.divider(rx, ry, rw); ry += 16 * s;
    u.sectionLabel(rx, ry, "Add a place"); ry += 26 * s;
    ry += u.field(rx, ry, rw, 46 * s, "", a.addName, 40, false, false, {}, "Name, for example Tokyo");
    const float h2 = (rw - 14 * s - 96 * s - 14 * s) * 0.5f;
    u.field(rx, ry, h2, 46 * s, "", a.addLat, 12, true, false, {}, "Latitude");
    u.field(rx + h2 + 14 * s, ry, h2, 46 * s, "", a.addLon, 12, true, false, {}, "Longitude");
    u.button(rx + rw - 96 * s, ry + 5 * s, 96 * s, 36 * s, L"ADD", [ap] {
        double la, lo; std::string nm = cleanName(ap->addName);
        if (nm.empty() || !parseNum(ap->addLat, -89, 89, la) || !parseNum(ap->addLon, -180, 180, lo)) { ap->setToast(L"Enter a name, a latitude and a longitude"); return; }
        astro::Geo gg; gg.name = nm; gg.lat = la; gg.lon = lo;
        ap->st.places.push_back(gg); ap->addName.clear(); ap->addLat.clear(); ap->addLon.clear(); ap->textTarget = nullptr;
        ap->cmd(CMD_SAVE); ap->setToast(L"Place added. Press N to switch between places");
    }, true);
}

// ---------------------------------------------------------------- about
// WHAT'S NEW: edit this list when you make a new version (and change MERIDIAN_VERSION in app.h). Newest first. Give each version its release date too.
// date: the day the version came out, as "15 Nov 2026". Leave it "" (or leave it off) when you do not know it and nothing is shown.
struct Release { const char* version; const char* name; std::vector<const char*> notes; const char* date = ""; };
const std::vector<Release>& releaseNotes() {
    static const std::vector<Release> r = {
        {"2.9.9", "Nimbus", {
            "Nimbus. Completely remade high-end sculpted Clouds and cinematic atmospheric Fog, four brand-new artistic weather systems (Drizzle, Sleet, Hail, and Gale), the Deep-Space Cosmos theme renamed to Galaxy, and a brand-new intentional Prussian-velvet & moonlit champagne-gold Midnight theme.",
            "Galaxy & Brand-New Artistic Midnight Theme: the deep-space cosmos theme (Milky Way, emission nebulae, Andromeda, ringed Saturn, meteors, comet, and U-key UFO visitor) is now named Galaxy, while a brand-new Midnight theme brings a gallery-grade Prussian-velvet to moonlit indigo-cerulean palette, moon-pearl typography, champagne-gold & cerulean accents, frosted sapphire-crystal panels, subtle lunar airglow, and crisp zenith stars engineered to flatter every face and every weather mode.",
            "Remade Sculpted Volumetric Clouds: replaces the old uniform circle blobs with three intentional sky-altitude archetypes painted with smooth directional elliptical gradients (zero polka-dot artifacts) — feathery wind-swept cirrus wisps high overhead, asymmetric cauliflower cumulus towers with flat shadowed bases and crisp sunlit/moonlit crown rims, and sweeping coastal/storm stratocumulus decks sorted cleanly back-to-front. Harmonized across all 11 built-in themes, paired with a sculpted storm canopy and an ethereal feathered foreground cloud veil.",
            "Remade Cinematic Atmospheric Fog: transforms fog from a flat uniform haze into intentional four-tone layered valley mist with luminous contrast on both dark and light themes — a cached diffused upper-sky light bloom, three sculpted rolling mist-bank sprites with cool troughs beneath bright crests, counter-drifting thermal depth strata, slow velvety vapor tendrils, floating dew-light micro-motes, and a whisper-soft low foreground mist veil on Quiet.",
            "Four New Artistic Weather Effects — Drizzle, Sleet, Hail & Gale: Drizzle weaves a poetic, ultra-fine misty micro-rain veil with delicate ground ripples and damp low haze; Sleet blends brisk translucent ice-beaded freezing streaks with tumbling wet snowflakes and icy ground flecks; Hail sends faceted glacier-lit ice stones plunging into kinetic two-hop parabolic ground bounces with crystalline shard sparks; Gale unleashes Meridian's painterly wind language into sweeping double-curled storm ribbons, racing gust-shear mist plumes, motion-streaked spray, and swirling leaf whirlwinds.",
            "Live Weather, Sky & Focus Studio Integration: Drizzle, Sleet, Hail, and Gale trigger automatically from live WMO weather codes and gale-force wind gusts, join the Sky tab's behind-the-dome precipitation, add dedicated preview chips in Settings > Quiet, and expand the Focus Studio Ambience Orb to 14 curated studio backdrops (Off, Live, Drizzle, Rain, Storm, Hail, Sleet, Snow, Blizzard, Gale, Clouds, Fog, Midnight, and Stars) while applying the active Midnight or Galaxy sky directly behind Focus Studio.",
        }, "22 Nov 2026"},
        {"2.9.8", "Glass", {
            "Glass. Tactile Liquid Glass physics across every button, sliding segmented pills, touch-reactive Focus Studio cards, a dramatic artistic Blizzard snowstorm, and a completely remade Deep-Space Cosmos for Midnight & Stars.",
            "Liquid Glass buttons everywhere: every button across Timer (mode tabs, duration presets, Start & Reset), Focus Studio (duration presets, Start & Reset, Ambience & Theme orb), the bottom navigation dock, Sky & Widgets < / > page arrows and Month Calendar arrows, and all Settings controls (including liquid-droplet gliding toggle switches and frosted -/+ steppers) now responds with smooth spring-eased hover bloom, cursor-tracking specular glass sheen, tactile press-and-hold compression, elastic liquid-jelly rebound on release, and an expanding refractive caustic ripple from the exact point you click.",
            "Gliding segmented pills: active selection capsules on Timer (Timer / Stopwatch / Alarm and 1m–1h presets), Focus Studio (5m–1h presets), and the bottom navigation dock now glide and stretch fluidly from one button to the next instead of snapping.",
            "Touch-reactive glass cards & Sky AQI badge: the Focus Studio top-left Clock & Date card, top-right Weather & AQI card, and the Sky face Air Quality pill badge all respond to hover and click with volume-preserving liquid-glass jelly wobble, cursor-tracking glass sheen, and a dual-ring shimmering caustic ripple — purely tactile motion when touched, with zero unintended side effects.",
            "Artistic Blizzard / Harsh Snowstorm: alongside the existing calm snowfall, Meridian now renders a fierce yet deeply artistic driving snowstorm — four organic blowing-snow variants across three parallax depths, billowing whiteout squall curtains, curling frosted spindrift ribbons, racing ground snowdrifts, and fast foreground wisps that keep every digit crisp. Triggers automatically during real winter snowstorms and thundersnow, previews with a new BLIZZARD chip in Settings > Quiet, falls cleanly behind the Sky map, and joins the Focus Studio Ambience Orb.",
            "Remade Deep-Space Cosmos (Midnight & Focus Stars): a museum-grade deep-sky backdrop featuring a sculpted Milky Way with its meandering Great Dark Rift, multi-layered Orion/Carina-inspired organic emission nebula ridges, ~1,400 spectral O–M stars with 4-point optical diffraction starbursts, the Pleiades reflection cluster, a feathered logarithmic-arm Andromeda spiral galaxy with companion dwarf galaxy, an axially tilted banded Saturn with Cassini Division gap and orbiting moons, a distant backlit crescent ice-giant, 44 living scintillating jewel stars, whisper-slow galactic stardust, a breathing nebula veil, and weather-immune dual shooting-star streams & comets in Focus Studio STARS.",
            "Windowed z-order & zero idle overhead: fixed normal/fullscreen windows staying stuck above other apps when clicking away (only Keep on top / P or screen saver stays topmost, with a clear title-bar indicator when pinned). All Liquid Glass springs, ripples, toggles, and sliding pills settle cleanly back to 1 FPS idle within half a second.",
        }, "22 Nov 2026"},
        {"2.9.7", "Snowfall", {
            "Snowfall. Meditative layered snowfall, Focus Studio ambient backdrops, Daylight Saving awareness, shooting-star physics, Air Quality emblem, Golden and Blue Hour, and precision polish across every face.",
            "Calm snowfall: three soft depths of drifting snowflakes with gentle dual-sine sway, soft foreground bokeh and a subtle frosted ground glow now appear behind both the Quiet and Sky faces whenever snow is falling (plus a SNOW preview chip in Settings > Quiet).",
            "Focus tab: a brand-new dedicated Focus Studio tab (key 6) pairs an independent focus timer (default 25m Pomodoro) with a smaller live clock, live weather card, frosted focus medallion, calm breathing halo and a collapsible side Ambience & Theme orb that smoothly expands on hover (Off, Live, Rain, Snow, Clouds, Fog, Storm or Stars) — completely clutter-free at rest, clear of the main bottom dock, and leaving the classic Timer tab untouched.",
            "Daylight Saving Time: zones that observe DST now stay accurate across spring-forward and fall-back nights, display a subtle DST indicator when active, show a 48-hour clock-change notice in Today, and use per-year Windows Dynamic DST rules across 31 World Clock cities — while countries without DST stay completely untouched.",
            "Air quality, weather sources & Golden Hour: the Sky weather block displays a custom air-waves emblem and color-coded AQI badge backed by staggered multi-source AQI (Open-Meteo, WeatherAPI.com, OpenWeatherMap and WAQI); four new weather fallbacks (Bright Sky, NWS / NOAA, WeatherAPI.com and OpenWeatherMap) join hardened WinHTTP recovery (longer timeouts, request staggering and TLS 1.2 + HTTP/1.1 retry on E_ABORT 2147500036); Almanac adds exact morning and evening Golden Hour and Blue Hour spans.",
            "Living night sky, 42-constellation atlas & compass fix: the Sky map now uses natural compass East/West orientation (N up, E right, S down, W left), deepens Midnight/Night/Ember dome skies to true dark cosmos, and renders 42 accurate year-round constellations across both hemispheres with spectral star colors, trimmed filigree threads and diamond glints — visible strictly at night by default (even on dark themes), with an optional daytime toggle in Settings > Sky Map; shooting stars ignite, flare and stream across a 6-week alternating cycle.",
            "Quality of life & precision polish: fluid liquid-glass pill-to-dock hover expansion (hovering the bottom-center pill smoothly morphs it into the full navigation dock at screen refresh rate without accidental corner triggers), optional Soft hourly chime (off by default in Settings > Elements), optional Saved place local time in Settings > Place, global Celsius and km defaults, optically centered circular panel arrows, a custom vector Back button in About, and case-insensitive hidden soundtracks and jukebox controls inside Settings > About.",
        }, "21 Nov 2026"},
        {"2.9.6", "Compass", {
            "Compass. Effortless navigation through rotating panels and widget pages, neutral privacy defaults, and hidden soundtracks waiting inside About.",
            "Panel and page arrows: crisp previous and next buttons now flank the page dots on both the Sky panel and the Widgets face, with generous click targets so you never need pixel-perfect aim. They follow every theme, daylight shift, weather mood, red-night tint and theme pack automatically.",
            "Privacy and neutrality: Meridian now defaults to the Prime Meridian (Greenwich) with no personal names, hometown coordinates or local build paths in the app, source or installer.",
            "Hidden jukebox: just like classic cheat codes, Settings > About quietly listens with no text box in sight — know the right words and you can play the full unedited stereo track or the setup music right inside Meridian, compactly stored and streamed straight from memory.",
        }, "21 Nov 2026"},
        {"2.9.5", "Anchor", {
            "Anchor. A hardening pass from an independent Windows review — upgrades recover cleanly, readings always belong to the place on screen, and Settings works from the keyboard.",
            "Upgrades and uninstall: setup now unpacks and verifies every file into a staging folder first, backs up the old files before swapping, and restores them all if anything is locked. Moving to a new folder retires the old binaries cleanly, an old uninstaller can no longer erase a newer copy's Apps entry, and silent uninstall waits and reports the real exit code.",
            "Weather, air quality, storms and places: air quality caches now carry their coordinates and time so another city's AQI can never leak in; incomplete weather replies with null temperature or condition codes are rejected so backup providers take over; stale storm warnings expire after 72 hours even while offline; and two saved places with the same name stay distinct after a restart.",
            "Screen saver and privacy: screen saver mode can no longer reset its crash-restart guard after ten minutes, and an automatic first-start lookup now respects an explicit Windows Location denial instead of silently calling IP geolocation (FIND MY LOCATION still falls back when you ask for it).",
            "Settings and builds: Tab and Shift+Tab now step through Settings controls with a focus ring (Space or Enter activates), small windows such as 640x400 expand the Settings card for legibility, and dev builds write to build-dev so CI always ships the clean release binary.",
        }, "21 Nov 2026"},
        {"2.9.4", "Visitor", {
            "Visitor. A quiet way to hush the Midnight saucer, and fresh polish across the About page and setup.",
            "If you ever decide that visitors from outer space simply do not exist, someone who knows the right words to whisper right here on this page — with no text box in sight — can make the saucer and its music vanish for good, and call them back again whenever they change their mind.",
            "The About page and the setup wizard now carry the release's name, new tips while installing, and the complete version history all the way back to 1.x.",
        }, "21 Nov 2026"},
        {"2.9.3", "Flyby", {
            "Flyby. The little flying saucer in the Midnight sky now brings a tune of its own as it crosses your screen.",
            "Instead of darting past in a blink, it now takes an unhurried nine-second glide across the night — staying in view for about seven seconds, wobbling and hopping sideways along the way.",
            "As it goes, the music moves with it across your speakers: starting on the side it enters from, swelling as it passes the middle of your screen, and fading out cleanly on the far side — whether it flies left to right or right to left.",
            "Press U on the Midnight theme (Sky or Quiet face) whenever you want to summon it for a closer listen. It stays silent in screen saver mode and fades out gently if you switch themes, change faces, minimise, or when an alarm rings.",
        }, "21 Nov 2026"},
        {"2.9.2", "Clear", {
            "Clear. A thorough round of fixes and polish across the clock, the weather, the sky, theme packs and the setup.",
            "Weather and saving: long weather requests and settings lines can no longer be cut short; 7Timer snow and rain rates, Pacific time zones and Open-Meteo air quality caching are fixed; downloads now handle compression, refresh system proxies and stop at once when you close Meridian.",
            "Sky, faces and Settings: the equation of time, polar day lengths, yearly meteor showers, three-planet lineups and the Halloween countdown are tightened up; Settings steppers, multi-line notes, burn-in hover alignment, portrait and 4:3 layouts, and the Skylight dome edge all behave cleanly.",
            "Setup and stability: Direct2D resources shut down in the right order, idle redraws in Settings, Help and Setup are trimmed back, and the setup writes files safely with rollback and clean screen-saver restore.",
        }, "20 Nov 2026"},
        {"2.9.1", "Skylight", {
            "Skylight. The sky map on the Sky face now wears your theme.",
            "Before, the round sky map was only ever day blue or night blue, whatever theme you picked, which looked out of place on Sunset, Dusk, Dawn and the rest. Now every theme paints its own sky: Sunset is a warm glow of orange going up into plum, Dusk a soft violet, Dawn a rosy blue, Night a deep navy, and so on. The rim ring takes on the sky's colour too.",
            "The real time of day still counts. Late in the evening the sky deepens into the theme's night colours and the stars come out, with the constellation lines tinted to match. Dark themes keep a few faint stars even at midday.",
            "Auto is unchanged: it still follows the real Sun, from day blue through dusk and dawn to night. Weather still shows too: cloud greys a light theme's sky, fog hazes it, while the twilight and night themes keep their colour.",
            "Your own theme packs get a sky for free, made from their background colours. To choose it yourself, add \"skyTop\" and \"skyHorizon\" to the palette in theme.json (see themes\\README.txt).",
        }, "15 Nov 2026"},
        {"2.9.0", "Reveal", {
            "Reveal. Holidays for three countries, a gentler start for the screen saver, and a What's new that shows every version.",
            "Holidays by country: Sri Lanka, the United Kingdom (England and Wales, Scotland, Northern Ireland) and the United States, each as its own list. Settings has a new Holidays tab: switch on any mix, or let Meridian pick the country your place is in. A place in any other country gets no holidays.",
            "A holiday that countries share, Christmas Day for one, is shown once and marked International. Every holiday carries its country code, and a holiday that falls on a weekend shows the day off it moves to (Independence Day observed on Fri 3 Jul, Christmas Day substitute on Mon 27 Dec).",
            "The screen saver no longer pops on. It starts dark, a soft circle of light opens from the middle while the picture fades up and settles into place, in about two seconds. Starting Meridian by hand is unchanged.",
            "Settings > About > What's new now scrolls through every version, newest first: mouse wheel, arrow keys, PageUp and PageDown, or click the bar.",
            "UK and US dates follow the law's rules and match the official 2026 to 2028 calendars. holidays.txt still replaces Sri Lanka's estimates and stays Sri Lanka only.",
        }, "15 Nov 2026"},
        {"2.8.1", "Tidy", {
            "Tidy. A round of fixes from a code review, and a few things that make Meridian easier to look after. Nothing looks different.",
            "Fixes: a graphics card reset no longer leaves the window blank (it retries once a second). Holding a key no longer flips settings back and forth. A touchpad flick no longer jumps the brightness or a timer. Typing digits no longer resets a running countdown.",
            "Privacy: weather services get your position rounded to about a kilometre, and the contact line in Settings > Weather goes only to MET Norway.",
            "New: holidays.txt. Type a new government list's dates in and they replace the estimates, live, with no restart (Rotation tab, OPEN HOLIDAYS.TXT). The built-in lists still end with 2027.",
            "New: a crash now also saves a small dump file next to crash.log. settings.ini records its layout and keeps a copy of a file that came from a newer Meridian. The timer's shortest length is one second everywhere.",
            "Setup and the .bat files: C:folder is refused, Uninstall can be tried again if a file was in use, 08 and 09 minutes work, and your old screen saver comes back exactly as it was. Theme pictures may use 192 MB at most.",
        }, "13 Nov 2026"},
        {"2.8.0", "Away", {
            "Away. Meridian can turn itself on when you are away and put the PC to sleep when nobody comes back. Settings has a new tab, Away, and the setup has a new page, While you are away, with the same two switches.",
            "Turn Meridian on when I am away: after 1 to 60 minutes of not touching the PC, Meridian starts full screen as your screen saver. Any key, click or mouse move closes it.",
            "Then put the PC to sleep: if nobody comes back within another 20 to 120 minutes, Meridian closes and Windows goes to sleep. With 10 and 60, for example, the PC sleeps after 70 minutes of no use.",
            "A small timeline in both places shows how it plays out. Switching it on makes Meridian.scr next to Meridian.exe and remembers your earlier screen saver; switching it off puts that one back.",
            "Windows' own screen and sleep timers (Settings, System, Power) still apply: if one is shorter, it wins. The sleep step only runs while Meridian is the screen saver.",
            "Setup: the screen saver choice moved to its own page. New flags /SleepAfter=N and /NoSleep, and /ScreenSaver=N now takes 1 to 60. Install-ScreenSaver.bat takes 1 to 60 too.",
        }, "11 Nov 2026"},
        {"2.7.0", "Sonnet", {
            "The Sonnet Edition. Meridian now has an edition name, shown here and in the setup, the way other hand-made lines of the app have theirs.",
            "A real setup program: Meridian-Setup.exe. A dark, graphical wizard in Meridian's own style: it greets you, shows tonight's real Moon, and installs for you alone (no administrator needed).",
            "In the setup you choose the folder, shortcuts, starting with Windows, and the screen saver with its minutes. The Moon in it fills up as it installs. It shows what is new. Open it again later and it offers Update (your settings stay) or Uninstall.",
            "Uninstalling (Settings > Apps) puts your old screen saver back and asks whether to keep your settings, places and notes. Nothing else of yours is touched.",
            "Meridian itself is unchanged apart from the name: same clock, same faces, same files.",
        }, "11 Nov 2026"},
        {"2.6.1", "Sturdy", {
            "Fixes from a code review. A theme file that is nested absurdly deep, or has stray text after its end, can no longer crash Meridian; it is refused with the line to fix.",
            "Minimised, Meridian now idles instead of spinning, and a timer or alarm that goes off still gets your attention (the taskbar button flashes).",
            "The alarm is right on the night the clocks change. A date such as 31 February or a time such as 99:99 is refused instead of turning into another day.",
            "Settings text boxes have a caret: arrow keys, Shift to select, Ctrl+A / C / X / V / Z, click to place it, double-click to select all.",
            "Changing the place can no longer be overwritten by the old place's weather. Closing Meridian while it is loading weather no longer leaves it running. A note keeps its backslashes.",
            "If saving fails, Meridian tells you and tries again. The Halloween theme now comes back every year, and every date you add can be reached, four to a page.",
        }, "7 Nov 2026"},
        {"2.6.0", "Saver", {
            "Meridian can be your screen saver. Run Install-ScreenSaver.bat (in the Meridian folder) and Windows starts Meridian full screen after 5 minutes of not touching the PC. Any key, click or mouse move closes it.",
            "The screen saver shows Meridian as you left it (same tab, theme, place and weather) and never changes your settings. Other monitors go black. Meridian.exe itself works exactly as before.",
        }},
        {"2.5.1", "Steady", {
            "Built to run for months: settings and the forecast are saved so that a power cut can no longer leave a half-written file.",
            "Only one Meridian runs at a time: opening it again brings the first one to the front.",
            "If Meridian ever crashes it writes a line to crash.log (in %LOCALAPPDATA%\\Meridian) and starts itself again, up to three times in a row.",
            "The window comes back on the monitor it was on, not always the main one.",
            "If the PC's time is corrected or changed, a running countdown or stopwatch keeps its place, and the screen no longer waits for the old time to come round again.",
            "Right-click opens a small menu (face, theme, fullscreen, keep on top, settings, exit) instead of changing the theme. A long press does the same on a touch screen.",
            "New in the menu: Start with Windows, and Dim with the sun (the brightness eases down from sunset to the end of twilight and back up at dawn).",
            "On battery, Keep screen awake lets go until the charger is back. A covered window or a screen that is off no longer draws frames.",
            "The Moon is no longer shown twice on the Sky and Almanac faces. The backup weather source 7Timer no longer falls back to plain http.",
        }},
        {"2.5.0", "Midnight", {
            "New theme, Midnight: a deep night sky behind the Quiet and Sky tabs, with dim stars, the Milky Way, a ringed planet, a far galaxy, passing meteors and the odd comet. Cloud, fog and rain dim the stars.",
            "New theme, Sunlit II: a deeper azure sky over a golden horizon, with soft rays from the Sun and wisps of cloud. Sunlit stays as it was.",
            "Rain and wind are smooth now: drawn from pre-made pictures at your screen's own refresh rate (Settings > Quiet > Smooth motion). Wind has the same art style as the rain.",
            "Rain brings a few clouds, even at night, and thunderstorm lightning comes out of them. A few drops fall in front of the time. Rain also falls on the Sky tab, behind the map, so every label stays readable.",
        }},
        {"2.4.0", "Sunlit", {
            "A new daylight theme, Sunlit: a clear blue sky above a warm cream horizon, made for the sun flare. The old Day theme stays as the plain light mode. Find it in Settings > Themes.",
            "In Sunlit the Sun glows all day: it comes in from the left edge in the morning, walks across the top of the screen and sets at the right edge, softest at noon and warmest when it is low.",
            "The sun flare is now on the Sky tab as well as the Quiet tab (it stays off the Timer, Almanac and Widgets tabs). It sits behind the clock and around the dome, never over it.",
            "Theme packs can use Sunlit as their base: \"base\": \"sunlit\" in theme.json.",
        }},
        {"2.3.0", "Rain and flare", {
            "Rain on the Quiet tab is much more noticeable: longer, brighter streaks in three depths, over a wet ground.",
            "Every drop that reaches the ground splashes there: a ring that opens out, a flash, a small spout and a crown of droplets thrown up and falling back.",
            "A soft sun flare comes in from the side the Sun is on while it is low: from the left in the morning, from the right in the evening. Cloud, fog and rain take it away.",
            "Settings > Quiet has a Sun flare switch to turn it off, and a SUN FLARE preview button to see it at any hour.",
        }},
        {"2.2.1", "Stays put", {
            "The place you choose now stays after a restart. Before, a saved place you had picked (or one you typed) could jump back to the place found by Wi-Fi or your internet address, such as Colombo.",
            "The lookup that runs by itself on a brand new install can no longer replace a place you set while it was still working.",
            "weather.log now starts each run with a line saying which place is showing.",
        }},
        {"2.2.0", "Deep night", {
            "Night is now deep dark under cloud and fog: blue-black, the way it is on a clear night, no longer turning grey. The cloud and fog on the Quiet tab turn into dim shapes at night too.",
            "Dusk, Dawn and Sunset show more of their own colours when it is cloudy or foggy: the purple and the orange come out instead of fading to grey.",
            "Theme packs such as GTA VI and Halloween are no longer touched by the weather. Their colours and scenes always stay exactly the same. (A pack can still ask for the weather with \"weather\": true in its theme.json.)",
        }},
        {"2.1.0", "Find my location", {
            "Settings > Place has a FIND MY LOCATION button. It asks Windows for your real position (GPS, or nearby Wi-Fi), falls back to your internet address, and keeps the answer until you press the button again.",
            "A brand new install looks up its location once, at its first start. After that nothing looks it up by itself, so the place never wanders.",
            "The Halloween theme pack, and the plane on the GTA VI tab now flies behind the numbers.",
        }},
        {"2.0.0", "Theme packs", {
            "Themes are now modular. A theme pack is a folder in the themes folder, with its own tab, fonts, artwork and animation. Drop it in to add it, delete it to remove it.",
            "The first pack is GTA VI: a neon countdown tab to the 19 November 2026 release. It only adds its own tab, and removes itself on 20 December 2026.",
            "Fog on the Quiet tab, and the light now follows the weather on every theme: heavy cloud and storms darken it, fog flattens it.",
            "Changing the theme no longer makes the clock stutter.",
            "Settings > Weather shows when the weather and the storm reports were last fetched and which source they came from, with refresh buttons.",
            "Quiet tab: wind now blows small bright particles with trailing tails. The clouds no longer jump around while you resize the window.",
            "Almanac tab: the Moon and the text keep clear of each other when the window is resized. What is special today shows again.",
            "A version number, and this page.",
        }},
        {"1.x", "Earlier", {
            "Widgets face: world clocks, month calendar, countdown, note and more, in pages.",
            "Dawn and Sunset themes, Red night mode, burn-in shift.",
            "Quiet tab weather: clouds, wind, rain, thunder and tornado, softer and lighter.",
            "Named cyclones, typhoons and hurricanes near you, from GDACS and NASA EONET.",
            "Holidays for Sri Lanka, the UK and the US, Poya days, meteor showers, eclipses and planets lining up.",
        }},
    };
    return r;
}

// "VERSION 2.9.4  ·  VISITOR  ·  SONNET EDITION  ·  21 NOV 2026": the line under the name on the About page
static std::string versionLine() {
    std::string cn = MERIDIAN_CODENAME; for (char& c : cn) c = (char)std::toupper((unsigned char)c);
    std::string o = std::string("VERSION ") + MERIDIAN_VERSION + "  \xC2\xB7  " + cn + "  \xC2\xB7  " + MERIDIAN_EDITION;
    const auto& rel = releaseNotes();
    if (!rel.empty() && rel[0].date && *rel[0].date) { std::string d = rel[0].date; for (char& c : d) c = (char)std::toupper((unsigned char)c); o += "  \xC2\xB7  " + d; }
    return o;
}

void tabAbout(UI& u, float x, float y, float w, float h) {
    App& a = u.a; Gfx& g = u.g; const Palette& P = u.P; const float s = u.s; App* ap = &a;
    if (a.aboutSub == 1) {                                                   // ---- features and what's new
        u.backButton(x, y, [ap] { ap->aboutSub = 0; ap->aboutScroll = 0; });
        g.text(F_MONO_M, 12.5f * s, x + w, u.capMid(F_MONO_M, 12.5f * s, y + 18 * s), Ws(versionLine()), P.ink3, Align::Right, 0.14f);
        const float top = y + 58 * s, lw = w * 0.42f, gap = 44 * s, rx = x + lw + gap, rw = w - lw - gap;
        u.sectionLabel(x, top, "What it does");
        float yy = top + 30 * s;
        struct F { const wchar_t* k; const wchar_t* d; };
        static const F fs[] = {
            {L"SKY", L"The live sky map with the Sun, Moon, planets, stars and eclipses, plus weather, air quality, sunrise, sunset and Today."},
            {L"QUIET", L"The big clock with the weather behind it: clouds, fog, drizzle, rain, sleet, hail, snow, blizzard, gale and storms."},
            {L"TIMER", L"Timer, stopwatch and alarm."},
            {L"ALMANAC", L"The Moon, Golden and Blue Hour, the day and the year, and what is special today."},
            {L"WIDGETS", L"World clocks, a month calendar, a countdown and a note."},
            {L"FOCUS", L"Lo-fi focus timer with smaller live clock, weather and ambient backdrops."},
            {L"THEMES", L"Auto, Day, Sunlit, Sunlit II, Dawn, Sunset, Dusk, Night, Midnight, Galaxy and Ember, plus theme packs you drop in."},
        };
        for (const F& f : fs) {
            g.text(F_MONO_M, 12.f * s, x, yy + 13 * s, f.k, u.acc(), Align::Left, 0.14f);
            auto lines = g.wrap(F_LIGHT, 15.f * s, f.d, lw - 96 * s, 0, 4);
            for (auto& l : lines) { g.text(F_LIGHT, 15.f * s, x + 96 * s, yy + 13 * s, l, P.ink2); yy += 21 * s; }
            yy += 9 * s;
        }
        g.line(rx - gap * 0.5f, top, rx - gap * 0.5f, y + h - 10 * s, P.line, std::max(1.f, s), false);
        // What's new: every version, newest first, in a column that scrolls (wheel, arrow keys, PageUp / PageDown, or click the bar)
        u.sectionLabel(rx, top, "What's new");
        const auto& rel = releaseNotes();
        g.text(F_MONO, 11.f * s, x + w, top + 9 * s, Ws(util::fmt("%d VERSIONS  \xC2\xB7  SCROLL FOR EARLIER ONES", (int)rel.size())), P.ink3, Align::Right, 0.12f);
        const float viewTop = top + 30 * s, viewBot = y + h - 6 * s, viewH = viewBot - viewTop;
        const float textW = rw - 34 * s;
        static float cachedW = -1, cachedS = -1; static std::vector<std::vector<std::vector<std::wstring>>> lineCache;     // the wrapped lines of every note, kept while the width and scale stay the same
        if (cachedW != textW || cachedS != s || lineCache.size() != rel.size()) {
            lineCache.assign(rel.size(), {});
            for (size_t i = 0; i < rel.size(); ++i) for (const char* n : rel[i].notes) lineCache[i].push_back(g.wrap(F_LIGHT, 15.f * s, Ws(n), textW, 0, 16));
            cachedW = textW; cachedS = s;
        }
        a.aboutScroll = std::max(0.f, std::min(a.aboutScrollMax, a.aboutScroll));
        g.pushClip(rx - 10 * s, viewTop, rw + 20 * s, viewH);
        float ry = viewTop - a.aboutScroll * s;
        for (size_t i = 0; i < rel.size(); ++i) {
            const Release& r = rel[i];
            if (ry + 40 * s > viewTop && ry < viewBot) {
                const std::wstring title = Ws(std::string(r.version) + "  \xC2\xB7  " + r.name);
                g.text(F_SERIF, 28.f * s, rx, ry + 22 * s, title, P.ink);
                float hx = rx + g.textWidth(F_SERIF, 28.f * s, title) + 14 * s;                // then the date, then CURRENT on the newest one
                if (r.date && *r.date) {
                    std::string d = r.date; for (char& c : d) c = (char)std::toupper((unsigned char)c);
                    const std::wstring dw = Ws(d);
                    g.text(F_MONO, 11.f * s, hx, ry + 20 * s, dw, P.ink3, Align::Left, 0.12f);
                    hx += g.textWidth(F_MONO, 11.f * s, dw, 0.12f) + 14 * s;
                }
                if (i == 0) g.text(F_MONO_M, 11.f * s, hx, ry + 20 * s, L"CURRENT", u.acc(), Align::Left, 0.14f);
            }
            ry += 36 * s;
            for (const auto& lines : lineCache[i]) {
                const float hN = (float)lines.size() * 21 * s;
                if (ry + hN > viewTop && ry < viewBot) {
                    g.fillCircle(rx + 4 * s, ry + 10 * s, 2.6f * s, u.acc());
                    for (size_t k = 0; k < lines.size(); ++k) g.text(F_LIGHT, 15.f * s, rx + 20 * s, ry + 14 * s + (float)k * 21 * s, lines[k], P.ink2);
                }
                ry += hN + 6 * s;
            }
            ry += 12 * s;
        }
        g.popClip();
        const float contentH = (ry - (viewTop - a.aboutScroll * s)) - 12 * s;
        a.aboutScrollMax = std::max(0.f, (contentH - viewH) / s);
        if (a.aboutScrollMax > 0) {                                           // the scroll bar: click anywhere on it to jump there
            const float bx = x + w - 5 * s, bw = 4 * s;
            g.fillRoundRect(bx, viewTop, bw, viewH, bw * 0.5f, mulA(P.ink, 0.07f));
            const float th = std::max(34 * s, viewH * viewH / contentH);
            const float ty = viewTop + (viewH - th) * (a.aboutScroll / a.aboutScrollMax);
            g.fillRoundRect(bx, ty, bw, th, bw * 0.5f, mulA(P.ink, 0.35f));
            const float vt = viewTop, vh = viewH, tt = th;
            u.act(bx - 12 * s, viewTop, bw + 24 * s, viewH, [ap, vt, vh, tt] {
                const float f = std::max(0.f, std::min(1.f, (ap->mouseY - vt - tt * 0.5f) / std::max(1.f, vh - tt)));
                ap->aboutScroll = f * ap->aboutScrollMax;
            });
        }
        return;
    }
    g.text(F_SERIF_I, 56.f * s, x, y + 50 * s, L"Meridian", P.ink);
    g.text(F_MONO_M, 13.f * s, x + g.textWidth(F_SERIF_I, 56.f * s, L"Meridian") + 18 * s, y + 48 * s, Ws(versionLine()), u.acc(), Align::Left, 0.14f);
    g.text(F_LIGHT, 18.f * s, x, y + 82 * s, L"An always-on clock with the sky, the Moon, the weather and what is special today.", P.ink2);
    g.text(F_MONO, 11.5f * s, x, y + 106 * s, L"MERIDIAN  \xB7  SONNET EDITION", P.ink3, Align::Left, 0.12f);
    float yy = y + 120 * s;
    float bx = x;
    bx += u.buttonAuto(bx, yy, L"ABOUT FEATURES  \xB7  WHAT'S NEW", [ap] { ap->aboutSub = 1; ap->aboutScroll = 0; }, true) + 10 * s;
    bx += u.buttonAuto(bx, yy, L"OPEN SETTINGS.INI", [ap] { ap->cmd(CMD_OPEN_INI); }) + 10 * s;
    u.buttonAuto(bx, yy, L"OPEN WEATHER LOG", [ap] { ap->cmd(CMD_OPEN_LOG); });
    yy += 64 * s;
    u.divider(x, yy, w); yy += 24 * s;
    struct K { const wchar_t* k; const wchar_t* d; };
    static const K ks[] = {
        {L"Comma", L"Open and close these settings"}, {L"?", L"Every keyboard shortcut"}, {L"1  2  3  4  5  6", L"Sky, Quiet, Timer, Almanac, Widgets, Focus (then theme packs)"},
        {L"T", L"Cycle the theme"}, {L"F", L"Fullscreen"}, {L"G", L"Open settings.ini for your places and dates"},
    };
    for (const K& k : ks) {
        g.text(F_MONO_M, 15.f * s, x, yy + 12 * s, k.k, P.accent, Align::Left, 0.08f);
        g.text(F_LIGHT, 18.f * s, x + 190 * s, yy + 12 * s, k.d, P.ink);
        yy += 38 * s;
    }
    yy += 12 * s;
    u.divider(x, yy, w); yy += 26 * s;
    auto lines = g.wrap(F_LIGHT, 15.5f * s, L"The Sun, Moon, planets, eclipses and meteor showers are worked out on this PC. Weather comes from Open-Meteo, MET Norway, wttr.in and 7Timer, whichever answers first. Everything you change here is saved automatically to settings.ini.", w, 0, 5);
    for (auto& l : lines) { g.text(F_LIGHT, 15.5f * s, x, yy + 12 * s, l, P.ink3); yy += 23 * s; }
    (void)h;
}

// ---- Away: Meridian on when the PC is not used (Windows' screen saver), then the PC to sleep (see saver.h). What Windows has is read again about
// once a second, because it can be changed in Windows as well; every click here is written straight away.
struct AwayView {
    saver::Away cur;
    bool have = false, sleepOn = false;
    int lastSleep = 60;                                           // what comes back when the sleep switch is turned on again
    std::chrono::steady_clock::time_point readAt;
} gAway;

int awayStep(int m, int dir) {                                    // 1 to 60 minutes: single steps up to 10, then fives
    if (dir > 0) return std::min(saver::kAwayMaxMinutes, m < 10 ? m + 1 : (m / 5 + 1) * 5);
    return std::max(saver::kAwayMinMinutes, m <= 10 ? m - 1 : ((m - 1) / 5) * 5);
}
int sleepStep(int m, int dir) {                                   // 20 to 120 minutes, in fives
    if (dir > 0) return std::min(saver::kSleepMaxMinutes, (m / 5 + 1) * 5);
    return std::max(saver::kSleepMinMinutes, ((m - 1) / 5) * 5);
}

void tabAway(UI& u, float x, float y, float w, float h) {
    App& a = u.a; Gfx& g = u.g; const Palette& P = u.P; const float s = u.s; App* ap = &a;
    AwayView& v = gAway;
    const auto now = std::chrono::steady_clock::now();
    if (!v.have || now - v.readAt > std::chrono::milliseconds(1000)) {
        v.cur = saver::readAway(); v.have = true; v.readAt = now;
        if (v.cur.sleepMinutes > 0) v.lastSleep = v.cur.sleepMinutes;
    }
    v.sleepOn = v.cur.sleepMinutes > 0;
    const float lw = w * 0.5f, gap = 48 * s, rx = x + lw + gap, rw = w - lw - gap;
    float yy = y;

    u.sectionLabel(x, yy, "Turn Meridian on"); yy += 28 * s;
    yy += u.toggle(x, yy, lw, "Turn Meridian on when I am away", "It becomes your screen saver. Any key or click closes it.", v.cur.on, [ap] {
        std::wstring problem;
        if (!saver::setAway(gAway.cur.on, gAway.cur.minutes, &problem)) { gAway.cur.on = !gAway.cur.on; ap->setToast(problem, 6.0); }
        gAway.readAt = std::chrono::steady_clock::now();
    });
    if (v.cur.on) {
        yy += u.stepper(x, yy, lw, "After no use for", fmt("%d min", v.cur.minutes),
                        [ap] { gAway.cur.minutes = awayStep(gAway.cur.minutes, -1); saver::setAwayMinutes(gAway.cur.minutes); gAway.readAt = std::chrono::steady_clock::now(); },
                        [ap] { gAway.cur.minutes = awayStep(gAway.cur.minutes, +1); saver::setAwayMinutes(gAway.cur.minutes); gAway.readAt = std::chrono::steady_clock::now(); });
    }
    yy += 14 * s;
    u.sectionLabel(x, yy, "Then put the PC to sleep"); yy += 28 * s;
    if (v.cur.on) {
        yy += u.toggle(x, yy, lw, "Put the PC to sleep afterwards", "Meridian closes and the PC sleeps if nobody is back.", v.sleepOn, [ap] {
            gAway.cur.sleepMinutes = gAway.sleepOn ? gAway.lastSleep : 0;
            saver::setSleepAfter(gAway.cur.sleepMinutes);
            gAway.readAt = std::chrono::steady_clock::now();
        });
        if (v.sleepOn) {
            yy += u.stepper(x, yy, lw, "Sleep after another", fmt("%d min", v.cur.sleepMinutes),
                            [ap] { gAway.cur.sleepMinutes = gAway.lastSleep = sleepStep(gAway.cur.sleepMinutes, -1); saver::setSleepAfter(gAway.cur.sleepMinutes); gAway.readAt = std::chrono::steady_clock::now(); },
                            [ap] { gAway.cur.sleepMinutes = gAway.lastSleep = sleepStep(gAway.cur.sleepMinutes, +1); saver::setSleepAfter(gAway.cur.sleepMinutes); gAway.readAt = std::chrono::steady_clock::now(); });
        }
    } else {
        auto lines = g.wrap(F_LIGHT, 15.f * s, L"Switch Meridian on above first: the PC can only be put to sleep while Meridian is the screen saver.", lw, 0, 3);
        for (auto& l : lines) { g.text(F_LIGHT, 15.f * s, x, yy + 14 * s, l, P.ink3); yy += 22 * s; }
    }

    // the right side: the same thing as a timeline, and in words
    g.line(rx - gap * 0.5f, y, rx - gap * 0.5f, y + h - 10 * s, P.line, std::max(1.f, s), false);
    float ry = y;
    u.sectionLabel(rx, ry, "How it plays out"); ry += 34 * s;
    const int m1 = v.cur.minutes, m2 = v.cur.sleepMinutes;
    const bool saverOn = v.cur.on, sleepOn = saverOn && m2 > 0;
    {
        const float by = ry + 44 * s, bh = 12 * s, bx = rx, bw = rw;
        g.fillRoundRect(bx, by, bw, bh, bh * 0.5f, mulA(P.ink, 0.10f));
        if (saverOn) {
            const float x1 = bx + bw * (float)m1 / (float)(m1 + (sleepOn ? m2 : std::max(m1, 20)));
            g.fillRoundRect(x1, by, bx + bw - x1, bh, bh * 0.5f, withA(u.acc(), sleepOn ? 0.85f : 0.40f));
            g.fillCircle(x1, by + bh * 0.5f, 8 * s, u.acc());
            const std::wstring on = Ws(fmt("MERIDIAN ON  \xC2\xB7  %d MIN", m1));
            const float tw = g.textWidth(F_MONO_M, 12.f * s, on, 0.12f);
            g.text(F_MONO_M, 12.f * s, std::max(bx, std::min(bx + bw - tw, x1 - tw * 0.5f)), by - 16 * s, on, P.ink, Align::Left, 0.12f);
            if (sleepOn) {
                g.fillCircle(bx + bw - bh * 0.5f, by + bh * 0.5f, 8 * s, P.ink2);
                g.text(F_MONO_M, 12.f * s, bx + bw, by + bh + 28 * s, Ws(fmt("PC SLEEPS  \xC2\xB7  %d MIN IN ALL", m1 + m2)), P.ink2, Align::Right, 0.12f);
            }
        }
        g.text(F_MONO, 11.5f * s, bx, by + bh + 28 * s, L"NO ONE AT THE PC", P.ink3, Align::Left, 0.12f);
        ry = by + bh + 52 * s;
    }
    {
        std::wstring t;
        if (!saverOn) t = L"Meridian stays an ordinary window. Switch on the first option and Windows starts Meridian full screen once the PC has not been used for the time you choose.";
        else if (!sleepOn) t = Ws(fmt("After %d minute%s without touching the PC, Meridian takes over the screen. Any key, click or mouse move gives it back. The PC stays awake until Windows' own power settings say otherwise.", m1, m1 == 1 ? "" : "s"));
        else t = Ws(fmt("After %d minute%s without touching the PC, Meridian takes over the screen. If nobody comes back within another %d minutes (%d in all), Meridian closes and the PC goes to sleep.", m1, m1 == 1 ? "" : "s", m2, m1 + m2));
        auto lines = g.wrap(F_LIGHT, 16.f * s, t, rw, 0, 6);
        for (auto& l : lines) { g.text(F_LIGHT, 16.f * s, rx, ry + 14 * s, l, P.ink2); ry += 24 * s; }
        ry += 10 * s;
        auto note = g.wrap(F_LIGHT, 14.f * s, L"Windows' own screen and sleep timers (Settings > System > Power) still apply: if one of them is shorter, it comes first. Meridian keeps a copy of itself called Meridian.scr next to Meridian.exe, because Windows only starts files with that name.", rw, 0, 5);
        for (auto& l : note) { g.text(F_LIGHT, 14.f * s, rx, ry + 12 * s, l, P.ink3); ry += 20 * s; }
        ry += 14 * s;
        u.buttonAuto(rx, ry, L"WINDOWS SCREEN SAVER SETTINGS", [] { ShellExecuteW(nullptr, L"open", L"control.exe", L"desk.cpl,,1", nullptr, SW_SHOWNORMAL); });
    }
}

} // namespace

// ---------------------------------------------------------------- the panel
bool App::inAboutTab() const {
    return showSettings && !showHelp && settingsTab == NTABS - 1;
}

void App::settingsWheel(int delta) {
    if (settingsTab != NTABS - 1 || aboutSub != 1) return;
    aboutScroll = std::max(0.f, std::min(aboutScrollMax, aboutScroll - (float)delta / 120.f * 78.f));      // 78 layout units per wheel notch
}

bool App::settingsKey(unsigned vk, bool shift) {
    if (vk == 0x09) {                                         // Tab / Shift+Tab: move keyboard focus through interactive Settings controls
        const int n = (int)actions.size();
        if (n <= 2) { settingsFocus = -1; return true; }
        if (settingsFocus < 2 || settingsFocus >= n) settingsFocus = shift ? n - 1 : 2;
        else {
            const int span = n - 2;
            settingsFocus = 2 + (((settingsFocus - 2 + (shift ? -1 : 1)) % span) + span) % span;
        }
        return true;
    }
    if ((vk == 0x0D || vk == 0x20) && settingsFocus >= 2 && settingsFocus < (int)actions.size()) {
        std::function<void()> fn = actions[(size_t)settingsFocus];
        if (fn) fn();
        return true;
    }
    if (settingsTab != NTABS - 1 || aboutSub != 1) return false;
    float v = aboutScroll;
    switch (vk) {
        case 0x26: v -= 60; break;                    // up
        case 0x28: v += 60; break;                    // down
        case 0x21: v -= 360; break;                   // page up
        case 0x22: v += 360; break;                   // page down
        case 0x24: v = 0; break;                      // home
        case 0x23: v = aboutScrollMax; break;         // end
        default: return false;
    }
    aboutScroll = std::max(0.f, std::min(aboutScrollMax, v));
    return true;
}

void App::renderSettings(Gfx& g, int w, int h) {
    const float W = (float)w, H = (float)h;
    // Expand the card on compact windows (e.g. 640x400) instead of leaving wasted scrim margin around tiny text
    const float sFit = std::min((W - 16.f) / 1240.f, (H - 16.f) / 850.f);
    const float s = std::max(layoutScale(w, h), std::min(1.f, sFit));
    Ctx c{g, *this, pal, W, H, s};
    UI u(c);
    const Palette& P = pal;
    dockT = 0; dockTarget = 0;
    if (settingsTab < 0 || settingsTab >= NTABS) settingsTab = 0;

    // scrim; a click outside the card closes the panel
    g.fillRect(0, 0, W, H, withA(P.plate, 0.84f));
    addAction(0, 0, W, H, [this] { showSettings = false; textTarget = nullptr; settingsFocus = -1; cmd(CMD_SAVE); });
    const float pad = std::min(36.f * s, std::max(8.f, std::min(W, H) * 0.02f));
    const float cw = std::min(W - pad, 1240.f * s), ch = std::min(H - pad, 850.f * s);
    const float cx = (W - cw) * 0.5f, cy = (H - ch) * 0.5f;
    addAction(cx, cy, cw, ch, [this] { textTarget = nullptr; });          // clicks on the card itself do nothing
    g.fillRoundRect(cx, cy + 6 * s, cw, ch, 34 * s, Color{0, 0, 0, 0.22f});
    g.fillRoundRect(cx, cy, cw, ch, 34 * s, withA(P.plate, 0.985f));
    g.fillRoundRect(cx, cy, cw, ch, 34 * s, mulA(P.ink, 0.025f));
    g.strokeRoundRect(cx + 0.5f, cy + 0.5f, cw - 1, ch - 1, 34 * s, P.line, std::max(1.f, s));

    // sidebar
    const float sw = std::max(122.f, 232.f * s);
    const float sideFs = std::max(9.5f, 13.5f * s);
    g.text(F_SERIF_I, 46.f * s, cx + 38 * s, cy + 78 * s, L"Settings", P.ink);
    g.line(cx + sw, cy + 34 * s, cx + sw, cy + ch - 34 * s, P.line, std::max(1.f, s), false);
    float ty = cy + 122 * s;
    for (int i = 0; i < NTABS; ++i) {
        const float th = 41 * s;
        const bool cur = i == settingsTab;
        const float tx = cx + 22 * s, tw = sw - 44 * s;
        const bool hv = u.over(tx, ty, tw, th);
        const uint64_t gkey = nextActionGlassKey();
        const glassfx::Pose gp = glass.pose(gkey, tx, ty, tw, th, th * 0.5f, s, hv, false);
        if (cur) {
            g.fillRoundRect(gp.x, gp.y, gp.w, gp.h, gp.r, withA(u.acc(), 0.16f + 0.05f * gp.hoverT));
            g.fillRoundRect(gp.x, gp.y + gp.h * 0.5f - 9 * s, 3.5f * s, 18 * s, 2 * s, u.acc());
        } else if (gp.hoverT > 0.01f) {
            g.fillRoundRect(gp.x, gp.y, gp.w, gp.h, gp.r, mulA(P.ink, 0.06f * gp.hoverT));
        }
        drawGlassEffects(c, gkey, gp, false, false);
        g.text(F_MONO_M, sideFs, cx + std::max(18.f, 46.f * s) + (gp.x - tx) * 0.25f, u.capMid(F_MONO_M, sideFs, gp.y + gp.h * 0.5f), Ws(TAB_NAMES[i]), cur ? P.ink : lerp(P.ink2, P.ink, gp.hoverT), Align::Left, 0.10f);
        addAction(tx, ty, tw, th, [this, i] { settingsTab = i; textTarget = nullptr; edInit = false; aboutSub = 0; aboutScroll = 0; });
        ty += th + 2 * s;
    }
    const float footX = cx + std::max(14.f, 46.f * s);
    const float footFs = std::max(7.5f, 11.5f * s);
    const float footTr = s < 0.6f ? 0.04f : 0.14f;
    g.text(F_MONO, footFs, footX, cy + ch - 40 * s, g.ellipsize(F_MONO, footFs, L"ESC OR COMMA TO CLOSE", std::max(40.f, cx + sw - footX - 8.f), footTr), P.ink3, Align::Left, footTr);

    // content
    const float x0 = cx + sw + 44 * s, cwid = cw - sw - 44 * s - 40 * s;
    g.text(F_SERIF, 40.f * s, x0, cy + 74 * s, Ws(TAB_TITLES[settingsTab]), P.ink);
    g.text(F_LIGHT, 17.f * s, x0, cy + 106 * s, Ws(TAB_SUB[settingsTab]), P.ink3);
    u.button(cx + cw - 40 * s - 96 * s, cy + 44 * s, 96 * s, 40 * s, L"DONE", [this] { showSettings = false; textTarget = nullptr; settingsFocus = -1; cmd(CMD_SAVE); }, true);
    const float y0 = cy + 144 * s, hh = ch - 144 * s - 30 * s;
    g.pushClip(x0 - 16 * s, y0 - 6 * s, cwid + 32 * s, hh + 12 * s);
    switch (settingsTab) {
        case 0: tabWidgets(u, x0, y0, cwid, hh); break;
        case 1: tabElements(u, x0, y0, cwid, hh); break;
        case 2: tabThemes(u, x0, y0, cwid, hh); break;
        case 3: tabPanel(u, x0, y0, cwid, hh); break;
        case 4: tabSkyMap(u, x0, y0, cwid, hh); break;
        case 5: tabQuiet(u, x0, y0, cwid, hh); break;
        case 6: tabSky(u, x0, y0, cwid, hh); break;
        case 7: tabWeather(u, x0, y0, cwid, hh); break;
        case 8: tabPlace(u, x0, y0, cwid, hh); break;
        case 9: tabHolidays(u, x0, y0, cwid, hh); break;
        case 10: tabAway(u, x0, y0, cwid, hh); break;
        default: tabAbout(u, x0, y0, cwid, hh); break;
    }
    g.popClip();
    if (settingsFocus >= 2 && settingsFocus < (int)actions.size()) {
        const int wantId = HIT_ACTION0 + settingsFocus;
        for (const auto& hr : hits) {
            if (hr.id == wantId && hr.w > 2.f && hr.h > 2.f) {
                const float r = std::min(16.f * s, std::min(hr.w, hr.h) * 0.5f);
                g.strokeRoundRect(hr.x - 2.f, hr.y - 2.f, hr.w + 4.f, hr.h + 4.f, r, withA(u.acc(), 0.92f), std::max(1.5f, 2.f * s));
                break;
            }
        }
    } else if (settingsFocus >= (int)actions.size()) settingsFocus = -1;
}

} // namespace app
