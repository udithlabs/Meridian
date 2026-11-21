// Meridian: the rotating panel of the Sky face (Today, Sky events, Holidays, World clocks, a pinned widget)
// and the holiday hover card. Panels take turns with a slide, rise or fade; dots show where you are.
#include "scene.h"
#include <cmath>
#include <algorithm>

namespace app {

using namespace gfx;
using util::fmt;

int ymdKey(int y, int m, int d) { return y * 10000 + m * 100 + d; }

Color holidayColor(const Palette& P, int group) {
    static const unsigned cols[8] = {0xFFD08A, 0xFF9F7A, 0x9FE0B0, 0xB9C8FF, 0xFF8FA3, 0xFFC24D, 0x8FD3FF, 0xA7E3D6};
    return tint(P, rgb(cols[std::max(0, std::min(7, group))]));
}

void addHolidayHit(App& a, float x, float y, float w, float h, int key) { a.holHits.push_back({x + a.shiftX, y + a.shiftY, w, h, key}); }

void holidaysBeginFrame(App& a) {
    int best = -1; float ax = 0, ay = 0;
    const bool allow = !a.showHelp && !a.showSettings && a.mouseX >= 0 && (a.now - a.lastMouseMove) < 12.0;
    if (allow) {
        for (const auto& h : a.holHits) {
            if (a.mouseX >= h.x && a.mouseX <= h.x + h.w && a.mouseY >= h.y && a.mouseY <= h.y + h.h) { best = h.key; ax = h.x + h.w * 0.5f; ay = h.y + h.h * 0.5f; }
        }
    }
    if (best >= 0) { a.holKey = best; a.holX = ax; a.holY = ay; }
    a.holOn = best >= 0;
    const float tgt = a.holOn ? 1.f : 0.f;
    a.holT += (tgt - a.holT) * (1.f - std::exp(-(float)a.dt * 14.f));
    if (std::fabs(a.holT - tgt) < 0.01f) a.holT = tgt;
    a.holHits.clear();
}

namespace {

std::string relDays(const App& a, int y, int m, int d) {
    util::LocalTime N = util::toLocal(a.now, a.tzOff);
    long long n = (long long)std::llround(astro::daysFromCivil(y, m, d) - astro::daysFromCivil(N.y, N.m, N.d));
    if (n == 0) return "TODAY";
    if (n == 1) return "TOMORROW";
    if (n == -1) return "YESTERDAY";
    return n > 0 ? fmt("IN %lld DAYS", n) : fmt("%lld DAYS AGO", -n);
}

void header(Ctx& c, float x, float y, const char* text, float maxW = 0.f) {
    std::wstring w = Ws(text);
    if (maxW > 0.f) w = c.g.ellipsize(F_MONO, 14.f * c.s, w, maxW, 0.22f);
    c.g.text(F_MONO, 14.f * c.s, x, y - 14 * c.s, w, c.P.ink3, Align::Left, 0.22f);
}

// one row in the style of the Today list: dot + tag line (with a right-hand note), title, detail
void row(Ctx& c, float x, float yy, float w, const Color& col, const std::string& tag, const std::string& note, const std::string& title, const std::string& detail) {
    Gfx& g = c.g; const Palette& P = c.P; const float s = c.s;
    Color k = mulA(col, c.alpha);
    g.fillCircle(x + 4 * s, yy + 17 * s, 3.2f * s, k);
    float nw = note.empty() ? 0.f : g.textWidth(F_MONO, 13.f * s, Ws(note), 0.12f) + 16 * s;
    g.text(F_MONO_M, 14.5f * s, x + 18 * s, yy + 22 * s, g.ellipsize(F_MONO_M, 14.5f * s, Ws(tag), w - 18 * s - nw, 0.16f), k, Align::Left, 0.16f);
    if (!note.empty()) g.text(F_MONO, 13.f * s, x + w, yy + 22 * s, Ws(note), P.ink3, Align::Right, 0.12f);
    g.text(F_LIGHT, 33 * s, x, yy + 60 * s, g.ellipsize(F_LIGHT, 33 * s, Ws(title), w), P.ink);
    if (!detail.empty()) g.text(F_LIGHT, 21 * s, x, yy + 88 * s, g.ellipsize(F_LIGHT, 21 * s, Ws(detail), w), P.ink2);
}

int rowsFor(float h, float s, int cap = 3) { return std::max(1, std::min(cap, (int)std::floor((h + 20 * s) / (112 * s)))); }

} // namespace

// ---------------------------------------------------------------- panels
void drawInfoRow(Ctx& c, float x, float yy, float w, const Color& col, const std::string& tag, const std::string& note, const std::string& title, const std::string& detail) {
    row(c, x, yy, w, col, tag, note, title, detail);
}

void drawEventsPanel(Ctx& c, float x, float y, float w, float h) {
    Gfx& g = c.g; App& a = c.a; const Palette& P = c.P; const float s = c.s;
    const float rowH = 112 * s;
    const int n = rowsFor(h, s);
    if (a.events.empty()) {
        header(c, x, y, "SKY EVENTS", a.st.panelDots ? std::max(80.f * s, w - 180.f * s) : w);
        g.text(F_SERIF_I, 34 * s, x, y + 40 * s, L"Nothing in the sky right now.", P.ink2);
        g.text(F_LIGHT, 21 * s, x, y + 76 * s, Ws(fmt("Events show up %d days before they happen.", a.st.evDays)), P.ink3);
        if (a.hasNextEvent) {
            const skyev::Event& e = a.nextEvent;
            Color k = mulA(tint(P, eventKindColor(e.kind)), c.alpha);
            g.line(x, y + 108 * s, x + w, y + 108 * s, P.line, std::max(1.f, s), false);
            g.text(F_MONO_M, 13.5f * s, x, y + 140 * s, Ws("NEXT  \xC2\xB7  " + eventKindLabel(e.kind)), k, Align::Left, 0.16f);
            g.text(F_LIGHT, 27 * s, x, y + 172 * s, g.ellipsize(F_LIGHT, 27 * s, Ws(e.title), w), P.ink);
            g.text(F_MONO, 13.f * s, x, y + 198 * s, Ws(eventWhenShort(a, e)), P.ink3, Align::Left, 0.12f);
        }
        return;
    }
    header(c, x, y, a.events.size() > 1 ? fmt("SKY EVENTS  \xC2\xB7  %d COMING UP", (int)a.events.size()).c_str() : "SKY EVENTS", a.st.panelDots ? std::max(80.f * s, w - 180.f * s) : w);
    for (int i = 0; i < n && i < (int)a.events.size(); ++i) {
        const skyev::Event& e = a.events[i];
        float yy = y + i * rowH;
        if (i > 0) g.line(x, yy - 18 * s, x + w, yy - 18 * s, P.line, std::max(1.f, s), false);
        row(c, x, yy, w, tint(P, eventKindColor(e.kind)), eventKindLabel(e.kind), eventWhenShort(a, e), e.title, eventText(a, e.subtitle));
        addEventRowHit(a, x - 8 * s, yy - 6 * s, w + 16 * s, 100 * s, (int)(&e - &a.events[0]));
    }
}

// "Sri Lanka", "United Kingdom (Scotland)", or the codes of several countries: "LK + UK + US"
std::string holidayCountriesLabel() {
    const int m = holiday::countries();
    if (m == 0) return "none";
    if ((m & (m - 1)) == 0) return holiday::countryList(m, holiday::ukNation());
    std::string t;
    if (m & holiday::C_LK) t += "LK";
    if (m & holiday::C_UK) t += std::string(t.empty() ? "" : " + ") + "UK";
    if (m & holiday::C_US) t += std::string(t.empty() ? "" : " + ") + "US";
    return t;
}

void drawHolidayPanel(Ctx& c, float x, float y, float w, float h, bool head, int maxRows) {
    Gfx& g = c.g; App& a = c.a; const Palette& P = c.P; const float s = c.s;
    const float rowH = 112 * s;
    const int n = rowsFor(h, s, maxRows);
    const float hw = a.st.panelDots ? std::max(80.f * s, w - 180.f * s) : w;
    if (head) { const std::string ht = "HOLIDAYS  \xC2\xB7  " + util::upper(holidayCountriesLabel()); header(c, x, y, ht.c_str(), hw); }
    util::LocalTime N = util::toLocal(a.now, a.tzOff);
    static long long cDay = -1; static int cMask = -1, cNat = -1, cVer = -1, cN = -1;
    static std::vector<holiday::Day> cDays;
    const int mask = holiday::countries(), nat = holiday::ukNation(), ver = holiday::overrideVersion();
    if (N.dayIndex != cDay || mask != cMask || nat != cNat || ver != cVer || n != cN) {
        cDay = N.dayIndex; cMask = mask; cNat = nat; cVer = ver; cN = n;
        cDays = holiday::upcoming(N.y, N.m, N.d, 500, n);
    }
    const std::vector<holiday::Day>& days = cDays;
    if (days.empty()) { g.text(F_SERIF_I, 34 * s, x, y + 40 * s, holiday::countries() == 0 ? L"No holiday country is on." : L"No holidays listed.", P.ink2); return; }
    const bool multi = (holiday::countries() & (holiday::countries() - 1)) != 0;
    for (size_t i = 0; i < days.size(); ++i) {
        const holiday::Day& dy = days[i];
        const holiday::Holiday& h0 = dy.items[0];
        float yy = y + (float)i * rowH;
        if (i > 0) g.line(x, yy - 18 * s, x + w, yy - 18 * s, P.line, std::max(1.f, s), false);
        int wd = astro::dayOfWeek(dy.y, dy.m, dy.d);
        std::string tag = util::upper(fmt("%s %d %s", util::WEEKDAYS3[wd], dy.d, util::MONTHS3[dy.m - 1]));
        std::string title = holiday::dayTitle(dy, multi);
        std::string detail = holiday::categories(h0);
        if (!holiday::note(h0).empty()) detail += "  \xC2\xB7  " + holiday::note(h0);
        if (h0.estimated) detail += "  \xC2\xB7  estimated date";
        row(c, x, yy, w, holidayColor(P, holiday::group(h0)), tag, relDays(a, dy.y, dy.m, dy.d), title, detail);
        addHolidayHit(a, x - 8 * s, yy - 6 * s, w + 16 * s, 100 * s, ymdKey(dy.y, dy.m, dy.d));
    }
}

// ---------------------------------------------------------------- the holiday card
namespace {
// "Source: ...", or the sources of each country when several share the holiday
std::string sourceLine(const holiday::Merged& m) {
    if (!m.international()) return "Source: " + holiday::source(m.lead());
    std::string t = "Sources: ";
    for (size_t i = 0; i < m.all.size(); ++i) t += (i ? "  |  " : "") + holiday::countryCode(m.all[i]) + " " + holiday::source(m.all[i]);
    return t;
}
struct Sec { holiday::Merged m; std::vector<std::wstring> title, what, who, how; };
}

void drawHolidayTooltip(Ctx& c) {
    App& a = c.a;
    if (a.holT < 0.01f || a.holKey < 0) return;
    const int Y = a.holKey / 10000, M = a.holKey / 100 % 100, D = a.holKey % 100;
    Gfx& g = c.g; const Palette& P = c.P; const float s = c.s, W = c.W, H = c.H;
    const float al = smoothstep(0.f, 1.f, a.holT);
    const float cw = std::min(W - 32 * s, 580 * s), pad = 24 * s, lab = 112 * s;
    const float tfs = 30 * s, wfs = 16.f * s, rfs = 15.f * s;
    const float tw = cw - pad * 2 - 6 * s, vw = cw - pad * 2 - lab;

    static int tcKey = -1, tcCw = -1, tcH = -1, tcS = -1, tcMask = -1, tcNat = -1, tcVer = -1;
    static std::vector<Sec> tcSecs;
    static float tcHgt = 0.f;
    const int qCw = (int)std::lround(cw), qH = (int)std::lround(H), qS = (int)std::lround(s * 100.f);
    const int mask = holiday::countries(), nat = holiday::ukNation(), ver = holiday::overrideVersion();
    if (a.holKey != tcKey || qCw != tcCw || qH != tcH || qS != tcS || mask != tcMask || nat != tcNat || ver != tcVer) {
        tcKey = a.holKey; tcCw = qCw; tcH = qH; tcS = qS; tcMask = mask; tcNat = nat; tcVer = ver;
        tcSecs.clear(); tcHgt = 0.f;
        const std::vector<holiday::Merged> items = holiday::merge(holiday::onDate(Y, M, D));      // one holiday shared by several countries is one section
        if (!items.empty()) {
            // mode 0: everything, 1: "on the day" only for the first holiday, 2: none; the shortest one that fits is used
            auto build = [&](int mode, std::vector<Sec>& out) {
                out.clear();
                float hgt = pad + 12 * s + 22 * s;
                for (size_t i = 0; i < items.size(); ++i) {
                    Sec sc; sc.m = items[i];
                    const holiday::Holiday& lead = items[i].lead();
                    sc.title = g.wrap(F_SERIF, tfs, Ws(holiday::name(lead)), tw, 0, 2);
                    sc.what = g.wrap(F_LIGHT, wfs, Ws(holiday::what(lead)), tw, 0, mode == 0 ? 9 : mode <= 2 ? 6 : mode == 3 ? 4 : 2);
                    // who gets it: one text, or one short text per country when several countries share the holiday
                    const size_t nc = items[i].all.size();
                    const int per = mode <= 2 ? (nc == 1 ? 6 : nc == 2 ? 3 : 2) : mode == 3 ? (nc == 1 ? 3 : 1) : 0;
                    if (per > 0) for (const auto& h : items[i].all) {
                        auto l = g.wrap(F_LIGHT, rfs, Ws((nc > 1 ? holiday::countryCode(h) + "  " : std::string()) + holiday::who(h)), vw, 0, per);
                        sc.who.insert(sc.who.end(), l.begin(), l.end());
                    }
                    if (mode == 0 || (mode == 1 && i == 0)) sc.how = g.wrap(F_LIGHT, rfs, Ws(holiday::how(lead)), vw, 0, 7);
                    if (i > 0) hgt += 22 * s;
                    hgt += 16 * s + sc.title.size() * tfs * 1.1f + 6 * s + 20 * s + sc.what.size() * wfs * 1.42f + 12 * s;
                    if (!sc.who.empty()) hgt += sc.who.size() * rfs * 1.38f + 9 * s;
                    if (items.size() > 1) hgt += 18 * s;                                  // each holiday names its own source when there are several
                    if (!sc.how.empty()) hgt += sc.how.size() * rfs * 1.38f + 9 * s;
                    out.push_back(std::move(sc));
                }
                hgt += (items.size() == 1 ? 30 * s : 0.f) + pad - 6 * s;
                return hgt;
            };
            for (int mode = 0; mode < 5; ++mode) { tcHgt = build(mode, tcSecs); if (tcHgt <= H - 24 * s) break; }
        }
    }
    if (tcSecs.empty()) return;
    const std::vector<Sec>& secs = tcSecs;
    const float hgt = tcHgt;

    float x = a.holX > W * 0.5f ? a.holX - cw - 30 * s : a.holX + 30 * s;
    x = std::max(12 * s, std::min(W - cw - 12 * s, x));
    float y = a.holY - hgt * 0.3f;
    y = std::max(12 * s, std::min(H - hgt - 12 * s, y)) + (1.f - al) * 8 * s;

    Color kc = holidayColor(P, holiday::group(secs[0].m.lead()));
    g.fillRoundRect(x - 2 * s, y + 6 * s, cw + 4 * s, hgt, 22 * s, Color{0, 0, 0, 0.22f * al});
    g.fillRoundRect(x, y, cw, hgt, 20 * s, withA(P.plate, al));
    g.strokeRoundRect(x + 0.5f, y + 0.5f, cw - 1.f, hgt - 1.f, 20 * s, mulA(P.line, al), std::max(1.f, s));
    g.fillRoundRect(x + 10 * s, y + 22 * s, 3.f * s, hgt - 44 * s, 1.5f * s, withA(kc, 0.85f * al));

    float yy = y + pad + 8 * s;
    const int wd = astro::dayOfWeek(Y, M, D);
    // a holiday is a whole calendar day: it starts at 12:00 AM
    const std::string from = a.st.h24 ? "00:00" : "12:00 AM";
    g.text(F_MONO, 11.5f * s, x + pad, yy, Ws(util::upper(fmt("%s, %d %s %d", util::WEEKDAYS[wd], D, util::MONTHS[M - 1], Y)) + "  \xC2\xB7  FROM " + from), mulA(P.ink3, al), Align::Left, 0.12f);
    g.text(F_MONO_M, 11.5f * s, x + cw - pad, yy, Ws(relDays(a, Y, M, D)), withA(kc, al), Align::Right, 0.12f);
    yy += 14 * s;
    for (size_t i = 0; i < secs.size(); ++i) {
        const Sec& sc = secs[i];
        const holiday::Holiday& lh = sc.m.lead();
        Color gc = holidayColor(P, holiday::group(lh));
        if (i > 0) { yy += 8 * s; g.line(x + pad, yy, x + cw - pad, yy, mulA(P.line, al), std::max(1.f, s), false); yy += 14 * s; }
        g.fillCircle(x + pad + 3.5f * s, yy + 10 * s, 3.4f * s, withA(gc, al));
        g.text(F_MONO_M, 11.5f * s, x + pad + 14 * s, yy + 14 * s, Ws(holiday::tag(sc.m)), withA(gc, al), Align::Left, 0.14f);
        yy += 16 * s;
        for (auto& t : sc.title) { yy += tfs * 1.0f; g.text(F_SERIF, tfs, x + pad, yy, t, mulA(P.ink, al)); yy += tfs * 0.1f; }
        yy += 6 * s;
        std::string cat = util::upper(holiday::categories(lh) + " HOLIDAY" + (holiday::note(lh).empty() ? std::string() : "  \xC2\xB7  " + holiday::note(lh)));
        if (sc.m.international()) cat += "  \xC2\xB7  " + holiday::countryCodes(sc.m.mask);
        if (lh.estimated) cat += "  \xC2\xB7  ESTIMATED";
        g.text(F_MONO, 11.5f * s, x + pad, yy + 12 * s, g.ellipsize(F_MONO, 11.5f * s, Ws(cat), tw, 0.12f), mulA(P.ink2, al), Align::Left, 0.12f);
        yy += 20 * s;
        for (auto& t : sc.what) { yy += wfs * 1.42f; g.text(F_LIGHT, wfs, x + pad, yy, t, mulA(P.ink, al)); }
        yy += 12 * s;
        auto block = [&](const wchar_t* label, const std::vector<std::wstring>& lines) {
            if (lines.empty()) return;
            float by = yy + rfs * 1.1f;
            g.text(F_MONO, 11.f * s, x + pad, by - 1 * s, label, mulA(P.ink3, al), Align::Left, 0.1f);
            for (size_t k = 0; k < lines.size(); ++k) g.text(F_LIGHT, rfs, x + pad + lab, by + k * rfs * 1.38f, lines[k], mulA(P.ink2, al));
            yy += lines.size() * rfs * 1.38f + 9 * s;
        };
        block(L"WHO GETS IT", sc.who);
        block(L"ON THE DAY", sc.how);
        if (secs.size() > 1) {
            g.text(F_MONO, 10.5f * s, x + pad, yy + 11 * s, g.ellipsize(F_MONO, 10.5f * s, Ws(util::upper(sourceLine(sc.m))), tw), mulA(P.ink3, 0.8f * al), Align::Left, 0.08f);
            yy += 18 * s;
        }
    }
    if (secs.size() == 1) g.text(F_MONO, 10.5f * s, x + pad, y + hgt - pad * 0.62f, g.ellipsize(F_MONO, 10.5f * s, Ws(util::upper(sourceLine(secs[0].m))), tw), mulA(P.ink3, 0.8f * al), Align::Left, 0.08f);
}

// ---------------------------------------------------------------- the rotor
// A rotor takes turns between pages (ids). The state machine, the transition and the dots are shared by the Sky panel and the Widgets face.
void App::panelNext(int dir) {
    if (st.face == FACE_WIDGETS) wgtRot.pending = dir < 0 ? -1 : 1;
    else skyRot.pending = dir < 0 ? -1 : 1;
}

bool rotorUpdate(App& a, Rotor& r, const std::vector<int>& list, int secs, bool over) {
    const int n = (int)list.size();
    if (n == 0) { r.cur = r.prev = -1; return false; }
    auto idx = [&](int id) { for (int i = 0; i < n; ++i) if (list[i] == id) return i; return -1; };
    if (r.cur < 0 || idx(r.cur) < 0) { r.cur = list[0]; r.prev = -1; r.switchAt = a.now; }
    if (r.prev >= 0 && idx(r.prev) < 0) r.prev = -1;

    int want = -1;
    if (r.want >= 0 && idx(r.want) >= 0 && r.want != r.cur) want = r.want;
    r.want = -1;
    if (r.pending != 0 && n > 1) want = list[(idx(r.cur) + r.pending + n) % n];
    r.pending = 0;

    if (n > 1) {
        if (want >= 0) {
            r.prev = r.cur;
            r.cur = want;
            r.switchAt = a.now;
        } else if (r.prev < 0) {
            if (over) r.switchAt += a.dt;                                       // keep the page while the pointer rests on it
            if (a.now - r.switchAt >= (double)secs) {
                r.prev = r.cur;
                r.cur = list[(idx(r.cur) + 1) % n];
                r.switchAt = a.now;
            }
        }
    }
    return true;
}

// draws the page on show; while one is leaving, both with the chosen transition. travel is how far (in pixels) they slide or rise.
void rotorDraw(Ctx& c0, Rotor& r, float travel, const std::function<void(Ctx&, int)>& draw) {
    App& a = c0.a; Gfx& g = c0.g; const Palette& P = c0.P; const Settings& st = a.st;
    const double DUR = st.panelStyle == 2 ? 0.9 : 1.05;
    float k = 1.f;
    if (r.prev >= 0) {
        k = (float)((a.now - r.switchAt) / DUR);
        if (k >= 1.f) { r.prev = -1; k = 1.f; }
    }
    auto drawOne = [&](int id, float alpha, float dx, float dy) {
        if (alpha < 0.012f) return;
        Palette Pa = P;
        for (Color* col : {&Pa.ink, &Pa.ink2, &Pa.ink3, &Pa.line, &Pa.panel, &Pa.plate, &Pa.accent, &Pa.accent2}) col->a *= alpha;
        Ctx c{g, a, Pa, c0.W, c0.H, c0.s};
        c.alpha = alpha;
        const float sx = a.shiftX, sy = a.shiftY;
        a.shiftX = sx + dx; a.shiftY = sy + dy; g.setOffset(a.shiftX, a.shiftY);
        draw(c, id);
        a.shiftX = sx; a.shiftY = sy; g.setOffset(sx, sy);
    };
    if (r.prev < 0) { drawOne(r.cur, 1.f, 0, 0); return; }
    const float po = clamp01(k / 0.55f), pi = clamp01((k - 0.28f) / 0.72f);
    const float eIn = po * po, eOut = 1.f - (1.f - pi) * (1.f - pi) * (1.f - pi);
    float aOut, aIn, dxO = 0, dyO = 0, dxI = 0, dyI = 0;
    if (st.panelStyle == 2) { aOut = 1.f - smoothstep(0.f, 0.6f, k); aIn = smoothstep(0.35f, 1.f, k); }
    else {
        aOut = 1.f - smoothstep(0.f, 1.f, po); aIn = smoothstep(0.f, 1.f, pi);
        if (st.panelStyle == 1) { dyO = -travel * 0.6f * eIn; dyI = travel * 0.75f * (1.f - eOut); }
        else { dxO = -travel * eIn; dxI = travel * 1.23f * (1.f - eOut); }
    }
    drawOne(r.prev, aOut, dxO, dyO);
    drawOne(r.cur, aIn, dxI, dyI);
}

// page dots flanked by theme-adaptive circular previous (<) and next (>) buttons; align: -1 ends at x, 0 centred on x, 1 starts at x
void rotorDots(Ctx& c, Rotor& r, const std::vector<int>& list, int secs, float x, float cy, int align) {
    App& a = c.a; Gfx& g = c.g; const Palette& P = c.P; const float s = c.s;
    const int n = (int)list.size();
    if (n < 2) return;
    const float dh = std::max(4.f, 5.5f * s), dw = dh, act = 26.f * s, gap = 9.f * s;
    const float d = std::max(24.f, 26.f * s), br = d * 0.5f, btnGap = 10.f * s;
    const float dotsW = (n - 1) * dw + act + (n - 1) * gap;
    const float total = d + btnGap + dotsW + btnGap + d;
    const float leftX = align == 0 ? x - total * 0.5f : (align > 0 ? x : x - total);
    const float mx = a.mouseX - a.shiftX, my = a.mouseY - a.shiftY;
    const bool allowHover = !a.showSettings && !a.showHelp && a.mouseX >= 0;
    Rotor* rp = &r;

    const bool isSkyRot = (&r == &a.skyRot);
    auto drawNavBtn = [&](float bx, int dir) {
        const float by = cy - br;
        // Generous hit area extending outward and vertically so stepping panels never needs pixel-precise aim
        const float padL = dir < 0 ? 6.f * s : 4.f * s;
        const float padR = dir > 0 ? 6.f * s : 4.f * s;
        const float hx = bx - padL, hy = cy - 18.f * s, hw = d + padL + padR, hh = 36.f * s;
        const bool hover = allowHover && mx >= hx && mx <= hx + hw && my >= hy && my <= hy + hh;
        // Dedicated stable HitId so click ripple & jelly bounce survive page/panel slide transitions cleanly!
        const int hitId = isSkyRot ? (dir < 0 ? HIT_PANEL_PREV : HIT_PANEL_NEXT)
                                   : (dir < 0 ? HIT_WGT_PREV : HIT_WGT_NEXT);
        const uint64_t gkey = (uint64_t)hitId;
        const glassfx::Pose gp = a.glass.pose(gkey, bx, by, d, d, br, s, hover, false);

        Color bg = P.panel;
        bg.a = std::max(bg.a, P.dark ? 0.095f : 0.08f) * c.alpha;
        g.fillRoundRect(gp.x, gp.y, gp.w, gp.h, gp.r, bg);

        const Color ac = tint(P, P.accent);
        if (gp.hoverT > 0.01f) g.fillRoundRect(gp.x, gp.y, gp.w, gp.h, gp.r, withA(ac, (P.dark ? 0.26f : 0.18f) * gp.hoverT * c.alpha));

        Color brd = lerp(withA(P.line, std::min(1.f, P.line.a * (P.dark ? 1.50f : 1.40f)) * c.alpha),
                         withA(ac, (P.dark ? 0.85f : 0.78f) * c.alpha), gp.hoverT);
        g.strokeRoundRect(gp.x + 0.5f, gp.y + 0.5f, std::max(1.f, gp.w - 1.f), std::max(1.f, gp.h - 1.f), std::max(0.5f, gp.r - 0.5f), brd, std::max(1.f, s));
        drawGlassEffects(c, gkey, gp, false, false);

        // Symmetric geometric chevron, optically centered inside the true 1:1 circle
        const float ch = gp.h * 0.195f, cw = gp.w * 0.135f;
        const float gcy = gp.y + gp.h * 0.5f;
        const float ccx = gp.x + gp.w * 0.5f + (float)dir * (cw * 0.18f);
        const std::vector<Pt> chev = dir < 0
            ? std::vector<Pt>{{ccx + cw, gcy - ch}, {ccx - cw, gcy}, {ccx + cw, gcy + ch}}
            : std::vector<Pt>{{ccx - cw, gcy - ch}, {ccx + cw, gcy}, {ccx - cw, gcy + ch}};
        const Color col = lerp(mulA(P.ink2, (P.dark ? 0.92f : 0.96f) * c.alpha), mulA(P.ink, c.alpha), gp.hoverT);
        g.polyline(chev, false, col, std::max(1.35f, (1.60f + 0.30f * gp.hoverT) * s), true);

        addHit(a, hx, hy, hw, hh, hitId);
    };

    drawNavBtn(leftX, -1);

    float dx = leftX + d + btnGap;
    const float prog = r.prev >= 0 ? 0.f : clamp01((float)((a.now - r.switchAt) / std::max(1, secs)));
    for (int i = 0; i < n; ++i) {
        const bool on = list[i] == r.cur;
        const float ww = on ? act : dw;
        g.fillRoundRect(dx, cy - dh * 0.5f, ww, dh, dh * 0.5f, on ? mulA(P.ink, 0.22f) : mulA(P.ink, 0.28f));
        if (on) g.fillRoundRect(dx, cy - dh * 0.5f, std::max(dh, ww * prog), dh, dh * 0.5f, mulA(P.accent, 0.95f));
        const int id = list[i];
        a.addAction(dx - 4 * s + a.shiftX, cy - 14 * s + a.shiftY, ww + 8 * s, 28 * s, [rp, id]() { rp->want = id; });
        dx += ww + gap;
    }

    drawNavBtn(leftX + d + btnGap + dotsW + btnGap, +1);
}

void drawSkyPanel(Ctx& c0, float x, float y, float w, float h) {
    App& a = c0.a; Gfx& g = c0.g; const float s = c0.s;
    const Settings& st = a.st;

    std::vector<int> list;
    if (st.elToday) list.push_back(PANEL_TODAY);
    if (st.elEvents && (!a.events.empty() || (!st.elToday && !st.elHolidays && !st.elWorld && st.pinned < 0))) list.push_back(PANEL_EVENTS);
    if (st.elHolidays && holiday::countries() != 0) list.push_back(PANEL_HOLIDAYS);       // no country on: no holiday page
    if (st.elWorld) list.push_back(PANEL_WORLD);
    if (st.pinned >= 0 && !(st.pinned == WG_WORLD && st.elWorld)) list.push_back(PANEL_PINNED);

    const float mx = a.mouseX - a.shiftX, my = a.mouseY - a.shiftY;
    const bool over = st.panelHold && a.mouseX >= 0 && (a.now - a.lastMouseMove) < 8.0 &&
                      mx >= x - 16 * s && mx <= x + w + 20 * s && my >= y - 44 * s && my <= y + h + 8 * s;
    if (!rotorUpdate(a, a.skyRot, list, st.panelSecs, over)) return;

    g.pushClip(x - 60 * s, y - 40 * s, w + 120 * s, h + 52 * s);
    rotorDraw(c0, a.skyRot, 44 * s, [&](Ctx& c, int id) {
        switch (id) {
            case PANEL_TODAY: drawTodayList(c, x, y, w, h); break;
            case PANEL_EVENTS: drawEventsPanel(c, x, y, w, h); break;
            case PANEL_HOLIDAYS: drawHolidayPanel(c, x, y, w, h); break;
            case PANEL_WORLD: drawWorldClocks(c, x, y, w, h); break;
            case PANEL_PINNED: drawWidgetTile(c, st.pinned, x, y - 6 * s, w, h + 6 * s); break;
            default: break;
        }
    });
    g.popClip();
    if (st.panelDots) rotorDots(c0, a.skyRot, list, st.panelSecs, x + w, y - 21 * s, -1);
}

} // namespace app
