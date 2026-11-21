// Meridian: widgets (world clocks, month calendar, system, note, countdown, moon phases, week ahead) and the Widgets face.
#include "scene.h"
#include <cmath>
#include <algorithm>

namespace app {

using namespace gfx;
using util::fmt;

// ---------------------------------------------------------------- catalogue
const char* widgetName(int id) {
    static const char* n[WG_COUNT] = {"World clocks", "Month calendar", "System", "Note", "Countdown", "Moon phases", "Week ahead",
                                      "Today", "Holidays", "Weather", "Daylight", "Sky tonight", "Conditions", "Year"};
    return (id >= 0 && id < WG_COUNT) ? n[id] : "";
}
const char* widgetBlurb(int id) {
    static const char* n[WG_COUNT] = {"Time in up to four other cities, daylight saving included",
                                      "This month with the holidays of the countries you chose, Poya days and your own dates; hover a date for details",
                                      "Processor, memory and battery of this PC",
                                      "A line or two of text you write yourself",
                                      "Days left until a date you pick",
                                      "The next four phases of the Moon",
                                      "Five-day forecast from the weather sources",
                                      "What is special today: Poya, holidays, lunar events, seasons, your own dates",
                                      "The next holidays of the countries you chose, with hover details",
                                      "The weather now, with the temperature curve and chance of rain for the next twelve hours",
                                      "Day length, sunrise, solar noon and sunset, with the day laid out as a strip",
                                      "The Moon, the planets that are up, and the next sky event",
                                      "Humidity, wind, pressure, UV, air quality and cloud cover",
                                      "How far through the year it is, the week number and the next season"};
    return (id >= 0 && id < WG_COUNT) ? n[id] : "";
}

namespace {

struct City { const char* name; const wchar_t* key; int stdMin; };
const City CITIES[] = {
    {"London", L"GMT Standard Time", 0}, {"New York", L"Eastern Standard Time", -300}, {"Tokyo", L"Tokyo Standard Time", 540},
    {"Dubai", L"Arabian Standard Time", 240}, {"Singapore", L"Singapore Standard Time", 480}, {"Sydney", L"AUS Eastern Standard Time", 600},
    {"Los Angeles", L"Pacific Standard Time", -480}, {"Paris", L"Romance Standard Time", 60}, {"Berlin", L"W. Europe Standard Time", 60},
    {"Delhi", L"India Standard Time", 330}, {"Moscow", L"Russian Standard Time", 180}, {"Beijing", L"China Standard Time", 480},
    {"Seoul", L"Korea Standard Time", 540}, {"Cairo", L"Egypt Standard Time", 120}, {"Johannesburg", L"South Africa Standard Time", 120},
    {"Sao Paulo", L"E. South America Standard Time", -180}, {"Chicago", L"Central Standard Time", -360}, {"Auckland", L"New Zealand Standard Time", 720},
    {"Istanbul", L"Turkey Standard Time", 180}, {"Riyadh", L"Arab Standard Time", 180}, {"Karachi", L"Pakistan Standard Time", 300},
    {"Dhaka", L"Bangladesh Standard Time", 360}, {"Bangkok", L"SE Asia Standard Time", 420}, {"Colombo", L"Sri Lanka Standard Time", 330},
    {"San Francisco", L"Pacific Standard Time", -480}, {"Denver", L"Mountain Standard Time", -420}, {"Phoenix", L"US Mountain Standard Time", -420},
    {"Toronto", L"Eastern Standard Time", -300}, {"Zurich", L"W. Europe Standard Time", 60}, {"Hong Kong", L"China Standard Time", 480},
    {"Melbourne", L"AUS Eastern Standard Time", 600},
};
const int NCITY = (int)(sizeof(CITIES) / sizeof(CITIES[0]));

// UTC offset of a city at a moment, from the Windows time zone database (Dynamic DST per year when present, else TZI; fixed offset as fallback)
int zoneOffset(int idx, double unixSec) {
    static int cachedYear[64] = {};
    static bool ok[64] = {};
    static TIME_ZONE_INFORMATION tz[64];
    if (idx < 0 || idx >= NCITY) return 0;
    const util::LocalTime utcNow = util::toLocal(unixSec, 0);
    if (cachedYear[idx] != utcNow.y) {
        cachedYear[idx] = utcNow.y;
        ok[idx] = false;
        auto parseTzi = [](const unsigned char* buf, TIME_ZONE_INFORMATION& out) {
            memset(&out, 0, sizeof(TIME_ZONE_INFORMATION));
            LONG bias, sb, db; memcpy(&bias, buf, 4); memcpy(&sb, buf + 4, 4); memcpy(&db, buf + 8, 4);
            out.Bias = bias; out.StandardBias = sb; out.DaylightBias = db;
            memcpy(&out.StandardDate, buf + 12, 16); memcpy(&out.DaylightDate, buf + 28, 16);
        };
        wchar_t path[256];
        _snwprintf(path, 255, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Time Zones\\%ls\\Dynamic DST", CITIES[idx].key);
        HKEY h;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &h) == ERROR_SUCCESS) {
            wchar_t yrKey[16];
            _snwprintf(yrKey, 15, L"%d", utcNow.y);
            unsigned char buf[44]; DWORD sz = sizeof buf, type = 0;
            if (RegQueryValueExW(h, yrKey, nullptr, &type, buf, &sz) == ERROR_SUCCESS && sz >= 44) {
                parseTzi(buf, tz[idx]);
                ok[idx] = true;
            }
            RegCloseKey(h);
        }
        if (!ok[idx]) {
            _snwprintf(path, 255, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Time Zones\\%ls", CITIES[idx].key);
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &h) == ERROR_SUCCESS) {
                unsigned char buf[44]; DWORD sz = sizeof buf, type = 0;
                if (RegQueryValueExW(h, L"TZI", nullptr, &type, buf, &sz) == ERROR_SUCCESS && sz >= 44) {
                    parseTzi(buf, tz[idx]);
                    ok[idx] = true;
                }
                RegCloseKey(h);
            }
        }
    }
    if (ok[idx]) {
        unsigned long long t100 = (unsigned long long)((unixSec + 11644473600.0) * 1e7);
        FILETIME ft; ft.dwLowDateTime = (DWORD)(t100 & 0xFFFFFFFFu); ft.dwHighDateTime = (DWORD)(t100 >> 32);
        SYSTEMTIME ut, lt;
        if (FileTimeToSystemTime(&ft, &ut) && SystemTimeToTzSpecificLocalTime(&tz[idx], &ut, &lt)) {
            double a = astro::daysFromCivil(ut.wYear, ut.wMonth, ut.wDay) * 86400.0 + ut.wHour * 3600 + ut.wMinute * 60 + ut.wSecond;
            double b = astro::daysFromCivil(lt.wYear, lt.wMonth, lt.wDay) * 86400.0 + lt.wHour * 3600 + lt.wMinute * 60 + lt.wSecond;
            return (int)std::lround(b - a);
        }
    }
    return CITIES[idx].stdMin * 60;
}

std::string offsetText(int sec) {
    int m = (int)std::lround(std::fabs((double)sec) / 60.0);
    if (m == 0) return "same time";
    return fmt("%s%d:%02d", sec < 0 ? "-" : "+", m / 60, m % 60);
}

void tile(Ctx& c, float x, float y, float w, float h, const char* title, float& cx, float& cy, float& cw, float& ch) {
    Gfx& g = c.g; const Palette& P = c.P; const float s = c.s;
    const float pad = 22.f * s;
    g.fillRoundRect(x, y, w, h, 26 * s, P.panel);
    g.strokeRoundRect(x + 0.5f, y + 0.5f, w - 1.f, h - 1.f, 26 * s, P.line, std::max(1.f, s));
    g.text(F_MONO, 12.5f * s, x + pad, y + pad + 9 * s, Ws(util::upper(title)), P.ink3, Align::Left, 0.16f);
    cx = x + pad; cy = y + pad + 30 * s; cw = w - 2 * pad; ch = h - 2 * pad - 30 * s;
}

// ---- World clocks
void drawWorld(Ctx& c, float x, float y, float w, float h) {
    Gfx& g = c.g; App& a = c.a; const Palette& P = c.P; const float s = c.s;
    const std::vector<int>& cl = a.st.clocks;
    int n = (int)std::min<size_t>(4, cl.size());
    if (n == 0) { g.text(F_LIGHT, 18.f * s, x, y + 30 * s, L"Pick cities in Settings", P.ink3); return; }
    const float rowH = std::min(h / n, 132.f * s);
    y += std::max(0.f, (h - rowH * n) * 0.2f);
    for (int i = 0; i < n; ++i) {
        const int idx = cl[i];
        const int off = zoneOffset(idx, a.now);
        util::LocalTime L = util::toLocal(a.now, off);
        const float ry = y + i * rowH;
        if (i > 0) g.line(x, ry, x + w, ry, P.line, std::max(1.f, s), false);
        const float nfs = std::min(36.f * s, rowH * 0.36f), sfs = std::min(14.f * s, rowH * 0.18f);
        const float mid = ry + rowH * 0.5f;
        g.text(F_LIGHT, nfs, x, mid - 1 * s, Ws(CITIES[idx].name), P.ink, Align::Left);
        std::string sub = std::string(util::WEEKDAYS3[L.wday]) + "  \xC2\xB7  " + (off == a.tzOff ? std::string("same time") : offsetText(off - a.tzOff));
        g.text(F_MONO, sfs, x, mid + sfs + 7 * s, Ws(util::upper(sub)), P.ink3, Align::Left, 0.12f);
        // the time
        std::string ts; std::wstring ap;
        if (a.st.h24) ts = fmt("%02d:%02d", L.hh, L.mm);
        else { int hh = L.hh % 12; if (hh == 0) hh = 12; ts = fmt("%d:%02d", hh, L.mm); ap = L.hh < 12 ? L"AM" : L"PM"; }
        const float tfs = std::min(70.f * s, rowH * 0.62f);
        std::wstring wt = Ws(ts);
        float apw = ap.empty() ? 0.f : g.textWidth(F_MONO_M, 12.f * s, ap, 0.12f) + 8 * s;
        float tw = g.timeWidth(F_DISPLAY, tfs, wt);
        float capH = g.capHeight(F_DISPLAY, tfs);
        float tx = x + w - apw - tw;
        g.timeText(F_DISPLAY, tfs, tx, mid + capH * 0.5f, wt, P.ink, lerp(P.ink, P.ink2, 0.5f), Align::Left);
        if (!ap.empty()) g.text(F_MONO_M, 12.f * s, x + w, mid + capH * 0.5f, ap, P.ink3, Align::Right, 0.12f);
        // day or night
        bool day = L.hh >= 6 && L.hh < 18;
        g.fillCircle(x + w - apw - tw - 14 * s, mid, 3.2f * s, withA(day ? P.accent : P.accent2, 0.9f));
    }
}

// ---- Month calendar
void chevron(Gfx& g, float cx, float cy, float r, int dir, const Color& col, float lw) {
    g.line(cx + dir * r * 0.35f, cy - r * 0.62f, cx - dir * r * 0.35f, cy, col, lw, true);
    g.line(cx - dir * r * 0.35f, cy, cx + dir * r * 0.35f, cy + r * 0.62f, col, lw, true);
}

void drawMonth(Ctx& c, float x, float y, float w, float h) {
    Gfx& g = c.g; App& a = c.a; const Palette& P = c.P; const float s = c.s;
    util::LocalTime N = util::toLocal(a.now, a.tzOff);
    const int mon0 = a.st.weekMon ? 1 : 0;
    a.calOffset = std::max(-240, std::min(240, a.calOffset));
    const int mo = N.y * 12 + (N.m - 1) + a.calOffset;
    const int Y = mo / 12, M = mo % 12 + 1;
    const bool thisMonth = a.calOffset == 0;
    const float headH = 38.f * s;
    g.text(F_SERIF, std::min(30.f * s, headH * 0.85f), x, y + headH * 0.72f, Ws(std::string(util::MONTHS[M - 1]) + " " + fmt("%d", Y)), P.ink, Align::Left);
    const float mx = a.mouseX - a.shiftX, my = a.mouseY - a.shiftY;
    // browse months: previous, next, and a way back
    {
        const float bs = 30.f * s, by = y + headH * 0.5f - bs * 0.5f;
        float bx = x + w - bs;
        const bool h1 = a.mouseX >= 0 && mx >= bx && mx <= bx + bs && my >= by && my <= by + bs;
        chevron(g, bx + bs * 0.5f, by + bs * 0.5f, bs * 0.34f, -1, h1 ? P.ink : P.ink2, std::max(1.5f, 1.8f * s));
        a.addAction(bx + a.shiftX, by + a.shiftY, bs, bs, [&a]() { a.calOffset++; });
        bx -= bs;
        const bool h2 = a.mouseX >= 0 && mx >= bx && mx <= bx + bs && my >= by && my <= by + bs;
        chevron(g, bx + bs * 0.5f, by + bs * 0.5f, bs * 0.34f, 1, h2 ? P.ink : P.ink2, std::max(1.5f, 1.8f * s));
        a.addAction(bx + a.shiftX, by + a.shiftY, bs, bs, [&a]() { a.calOffset--; });
        if (!thisMonth) {
            const std::wstring t = L"TODAY";
            const float tw = g.textWidth(F_MONO_M, 12.f * s, t, 0.14f) + 22 * s, tx = bx - 8 * s - tw;
            const bool h3 = a.mouseX >= 0 && mx >= tx && mx <= tx + tw && my >= by && my <= by + bs;
            g.fillRoundRect(tx, by + 2 * s, tw, bs - 4 * s, (bs - 4 * s) * 0.5f, h3 ? withA(P.accent, 0.95f) : mulA(P.ink, 0.12f));
            g.text(F_MONO_M, 12.f * s, tx + tw * 0.5f, by + bs * 0.5f + g.capHeight(F_MONO_M, 12.f * s) * 0.5f, t, h3 ? P.plate : P.ink2, Align::Center, 0.14f);
            a.addAction(tx + a.shiftX, by + a.shiftY, tw, bs, [&a]() { a.calOffset = 0; });
        }
    }
    const float gy = y + headH + 4 * s;
    const float cw = w / 7.f, rowH = std::min((h - headH - 4 * s) / 7.f, cw * 0.95f);
    static const char* WD[7] = {"S", "M", "T", "W", "T", "F", "S"};
    for (int i = 0; i < 7; ++i) g.text(F_MONO, 13.f * s, x + cw * (i + 0.5f), gy + rowH * 0.62f, Ws(WD[(i + mon0) % 7]), P.ink3, Align::Center, 0.1f);
    int dim = 31; { static const int d[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}; dim = d[M - 1] + (M == 2 && astro::isLeap(Y) ? 1 : 0); }

    // Sri Lankan holidays of this month (official list or estimate); without them, the full-moon dates are marked as before
    std::vector<holiday::Holiday> hol[32];
    static long long cachedKey = -1; static std::vector<int> poyaDays;
    if (a.st.holCal) {
        for (const auto& hh : holiday::forYear(Y)) if (hh.m == M && hh.d >= 1 && hh.d <= 31) hol[hh.d].push_back(hh);
    } else {
        long long key = ((long long)(Y * 12 + M) << 20) + (a.tzOff / 60 + 2000);
        if (key != cachedKey) {
            cachedKey = key; poyaDays.clear();
            double m0 = astro::daysFromCivil(Y, M, 1) * 86400.0 - a.tzOff;
            for (double t = astro::nextPhaseTime(m0 - 2 * 86400.0, 2); t < m0 + 32 * 86400.0; t = astro::nextPhaseTime(t + 5 * 86400.0, 2)) {
                util::LocalTime L = util::toLocal(t, a.tzOff);
                if (L.y == Y && L.m == M) poyaDays.push_back(L.d);
            }
        }
    }
    int first = (astro::dayOfWeek(Y, M, 1) - mon0 + 7) % 7;
    bool anyMine = false, anyPoya = false, anyOther = false;
    for (int d = 1; d <= dim; ++d) {
        int cell = first + d - 1, col = cell % 7, row = cell / 7;
        float px = x + cw * (col + 0.5f), py = gy + rowH * (row + 1) + rowH * 0.62f;
        bool today = thisMonth && d == N.d;
        bool poyaOld = !a.st.holCal && std::find(poyaDays.begin(), poyaDays.end(), d) != poyaDays.end();
        bool mine = false;
        for (const auto& e : a.st.events) if (e.on(Y, M, d)) mine = true;
        float fs = std::min(18.f * s, rowH * 0.5f);
        const float rr = std::min(rowH * 0.46f, cw * 0.42f);
        const auto& hs = hol[d];
        // hover: a soft disc, and the holiday card
        if (!hs.empty()) {
            const float cx0 = x + cw * col, cy0 = gy + rowH * (row + 1);
            const bool hv = a.mouseX >= 0 && mx >= cx0 && mx < cx0 + cw && my >= cy0 && my < cy0 + rowH;
            Color hc = holidayColor(P, holiday::group(hs[0]));
            if (!today) g.fillCircle(px, py - fs * 0.33f, rr, withA(hc, hv ? 0.30f : 0.13f));
            else if (hv) g.strokeCircle(px, py - fs * 0.33f, rr + 3 * s, withA(hc, 0.9f), std::max(1.5f, 2.f * s));
            addHolidayHit(a, cx0, cy0, cw, rowH, ymdKey(Y, M, d));
        }
        if (today) g.fillCircle(px, py - fs * 0.33f, rr, mulA(P.accent, 0.95f));
        g.text(F_MEDIUM, fs, px, py, Ws(fmt("%d", d)), today ? P.plate : (col == (7 - mon0) % 7 || col == (6 - mon0 + 7) % 7 ? P.ink2 : P.ink), Align::Center);
        if (poyaOld) { anyPoya = true; g.fillCircle(px, py + rowH * (today ? 0.22f : 0.2f), 2.4f * s, today ? P.plate : withA(P.accent2, 0.95f)); }
        if (!hs.empty()) {
            const int nd = (int)std::min<size_t>(2, hs.size());
            for (int k = 0; k < nd; ++k) {
                const bool pya = hs[k].poya();
                (pya ? anyPoya : anyOther) = true;
                float dx = (k - (nd - 1) * 0.5f) * 7.f * s;
                g.fillCircle(px + dx, py + rowH * 0.2f, 2.5f * s, today ? P.plate : withA(holidayColor(P, holiday::group(hs[k])), 0.98f));
            }
        }
        if (mine) { anyMine = true; g.fillCircle(px + cw * 0.3f, py - fs * 0.85f, 2.2f * s, withA(P.accent, 0.95f)); }
    }
    // legend, when there is room under the grid
    {
        const float used = headH + 4 * s + rowH * (float)((first + dim + 6) / 7 + 1);
        if (used + 22 * s <= h) {
            const float ly = y + used + 16 * s;
            float lx = x + 4 * s;
            auto item = [&](const Color& col, const wchar_t* t) {
                g.fillCircle(lx + 3 * s, ly - 4 * s, 3.f * s, col);
                g.text(F_MONO, 11.f * s, lx + 12 * s, ly, t, P.ink3, Align::Left, 0.12f);
                lx += 12 * s + g.textWidth(F_MONO, 11.f * s, t, 0.12f) + 18 * s;
            };
            if (anyPoya) item(a.st.holCal ? holidayColor(P, holiday::G_BUDDHIST) : withA(P.accent2, 0.95f), L"POYA");
            if (anyOther) item(holidayColor(P, holiday::G_NEWYEAR), L"HOLIDAY");
            if (anyMine) item(withA(P.accent, 0.95f), L"YOURS");
            if (a.st.holCal && (anyPoya || anyOther)) g.text(F_MONO, 11.f * s, x + w, ly, L"HOVER A DATE", mulA(P.ink3, 0.75f), Align::Right, 0.12f);
        }
    }
}

// ---- System
void drawSystem(Ctx& c, float x, float y, float w, float h) {
    Gfx& g = c.g; App& a = c.a; const Palette& P = c.P; const float s = c.s;
    const SysInfo& S = a.sys;
    struct G3 { const char* name; int v; bool ac; } gs[3] = {{"CPU", S.cpu, false}, {"MEMORY", S.ram, false}, {"BATTERY", S.hasBattery ? S.battery : -1, !S.hasBattery}};
    const float r = std::max(18.f * s, std::min(h * 0.34f, w / 6.4f));
    const float cy = y + r + 6 * s, lw = std::max(4.f, 7.f * s);
    for (int i = 0; i < 3; ++i) {
        float cx = x + w * (i + 0.5f) / 3.f;
        g.strokeCircle(cx, cy, r, mulA(P.ink, 0.12f), lw);
        if (gs[i].v >= 0) {
            Color col = i == 2 ? (gs[i].v < 20 && !S.charging ? rgb(0xFF7A52) : P.accent2) : (gs[i].v > 85 ? rgb(0xFF9F7A) : P.accent);
            g.arc(cx, cy, r, 0.f, std::min(359.f, std::max(2.f, 3.6f * gs[i].v)), tint(P, col), lw, true);
        }
        std::wstring v = gs[i].v >= 0 ? Ws(fmt("%d", gs[i].v)) : (gs[i].ac ? L"AC" : L"—");
        float fs = r * 0.62f;
        g.text(F_LIGHT, fs, cx, cy + g.capHeight(F_LIGHT, fs) * 0.5f, v, P.ink, Align::Center);
        g.text(F_MONO, 11.5f * s, cx, cy + r + 24 * s, Ws(gs[i].name), P.ink3, Align::Center, 0.14f);
        if (i == 2 && S.charging && S.hasBattery) g.text(F_MONO, 10.5f * s, cx, cy + r + 40 * s, L"CHARGING", withA(P.accent2, 0.9f), Align::Center, 0.12f);
    }
    int up = (int)S.uptimeSec, d = up / 86400, hh = up % 86400 / 3600, mm = up % 3600 / 60;
    std::string us = d > 0 ? fmt("UP  %dD %dH", d, hh) : fmt("UP  %dH %02dM", hh, mm);
    g.text(F_MONO, 12.f * s, x + w, y + h - 2 * s, Ws(us), P.ink3, Align::Right, 0.14f);
}

// ---- Note
void drawNote(Ctx& c, float x, float y, float w, float h) {
    Gfx& g = c.g; App& a = c.a; const Palette& P = c.P; const float s = c.s;
    if (a.st.note.empty()) { g.text(F_SERIF_I, 22.f * s, x, y + 30 * s, L"Write a note in Settings", P.ink3); return; }
    std::wstring t = Ws(a.st.note);
    for (int pt = 40; pt >= 16; pt -= 2) {
        const float fs = (float)pt * s;
        std::vector<std::wstring> lines;
        // honour explicit line breaks
        size_t i0 = 0;
        for (;;) {
            size_t nl = t.find(L'\n', i0);
            std::wstring part = t.substr(i0, nl == std::wstring::npos ? std::wstring::npos : nl - i0);
            auto wl = g.wrap(F_SERIF_I, fs, part, w, 0, 50);
            if (wl.empty()) lines.push_back(L"");
            for (auto& l : wl) lines.push_back(l);
            if (nl == std::wstring::npos) break;
            i0 = nl + 1;
        }
        if (lines.size() * fs * 1.22f <= h + 2 || pt == 16) {
            float yy = y + fs * 0.95f;
            for (auto& l : lines) { if (yy - fs > y + h) break; g.text(F_SERIF_I, fs, x, yy, l, P.ink); yy += fs * 1.22f; }
            return;
        }
    }
}

// ---- Countdown
void drawCountdown(Ctx& c, float x, float y, float w, float h) {
    Gfx& g = c.g; App& a = c.a; const Palette& P = c.P; const float s = c.s;
    int yy_ = 0, mm_ = 0, dd_ = 0;
    if (!util::parseDateStrict(a.st.cdDate, yy_, mm_, dd_)) {
        g.text(F_SERIF_I, 22.f * s, x, y + 30 * s, L"Pick a date in Settings", P.ink3);
        return;
    }
    double target = astro::daysFromCivil(yy_, mm_, dd_);
    util::LocalTime N = util::toLocal(a.now, a.tzOff);
    double today = astro::daysFromCivil(N.y, N.m, N.d);
    long long days = (long long)std::llround(target - today);
    std::string label = a.st.cdLabel.empty() ? "Countdown" : a.st.cdLabel;
    float big = std::min(h * 0.62f, 150.f * s);
    std::wstring num = days == 0 ? L"Today" : Ws(fmt("%lld", days < 0 ? -days : days));
    if (days == 0) big *= 0.55f;
    float capH = g.capHeight(F_DISPLAY, big);
    g.text(F_DISPLAY, big, x, y + capH + 2 * s, num, P.ink, Align::Left);
    float nw = g.textWidth(F_DISPLAY, big, num);
    if (days != 0) {
        std::string unit = (days == 1 || days == -1) ? "DAY" : "DAYS";
        unit += days > 0 ? " LEFT" : " AGO";
        g.text(F_MONO_M, 13.f * s, x + nw + 14 * s, y + capH + 2 * s, Ws(unit), P.accent, Align::Left, 0.16f);
        if (days > 14) g.text(F_MONO, 12.f * s, x + nw + 14 * s, y + capH - 18 * s, Ws(fmt("%.1f WEEKS", std::fabs((double)days) / 7.0)), P.ink3, Align::Left, 0.12f);
    }
    g.text(F_SERIF_I, std::min(30.f * s, h * 0.2f), x, y + h - 6 * s, g.ellipsize(F_SERIF_I, std::min(30.f * s, h * 0.2f), Ws(label), w), P.ink2, Align::Left);
}

// ---- Moon phases
void buildPhaseBitmaps(Gfx& g, App& a) {
    if (a.phaseBuilt || !moonsprite::ready()) return;
    struct Ph { double angle, pa; } ph[4] = {{180, 270}, {90, 270}, {0, 270}, {90, 90}};
    for (int i = 0; i < 4; ++i) {
        moonsprite::Params p; p.size = 112; p.phaseAngleDeg = ph[i].angle; p.brightLimbPA = ph[i].pa; p.earthshine = i == 0 ? 0.07 : 0.045;
        std::vector<uint8_t> px; moonsprite::render(p, px);
        g.freeBitmap(a.phaseBmp[i]);
        a.phaseBmp[i] = g.makeBitmap(px.data(), p.size, p.size);
    }
    a.phaseBuilt = true;
}

void drawPhases(Ctx& c, float x, float y, float w, float h) {
    Gfx& g = c.g; App& a = c.a; const Palette& P = c.P; const float s = c.s;
    buildPhaseBitmaps(g, a);
    const MoonDetail& md = a.moonDetail;
    struct Ph { const char* n; double t; int bmp; } ph[4] = {{"NEW MOON", md.nextNew, 0}, {"FIRST QUARTER", md.nextFQ, 1}, {"FULL MOON", md.nextFull, 2}, {"LAST QUARTER", md.nextLQ, 3}};
    std::sort(ph, ph + 4, [](const Ph& p, const Ph& q) { return p.t < q.t; });
    const float sizeRow = std::min(w / 4.f * 0.72f, h * 0.52f), sizeGrid = std::min(w / 2.f * 0.72f, h / 2.f * 0.52f);
    const bool grid = sizeGrid > sizeRow * 1.15f;
    const int cols = grid ? 2 : 4, rows = grid ? 2 : 1;
    const float cw = w / cols, rh = h / rows;
    for (int i = 0; i < 4; ++i) {
        float cx = x + cw * ((i % cols) + 0.5f), top = y + (i / cols) * rh;
        float size = std::min(cw * 0.72f, rh * 0.52f);
        float off = std::max(0.f, (rh - (size + 100 * s)) * 0.5f);
        float cy = top + off + size * 0.5f + 2 * s;
        if (a.phaseBmp[ph[i].bmp].bmp) g.drawBitmap(a.phaseBmp[ph[i].bmp], cx, cy, size, 0.f, 1.f);
        float ty = cy + size * 0.5f + 20 * s;
        g.text(F_MONO_M, 11.5f * s, cx, ty, Ws(ph[i].n), P.accent2, Align::Center, 0.12f);
        util::LocalTime L = util::toLocal(ph[i].t, a.tzOff);
        g.text(F_LIGHT, std::min(18.f * s, rh * 0.15f), cx, ty + 22 * s, Ws(fmt("%s %d", util::MONTHS3[L.m - 1], L.d)), P.ink, Align::Center);
        double dd = (ph[i].t - a.now) / 86400.0;
        g.text(F_MONO, 11.f * s, cx, ty + 40 * s, Ws(dd < 1.0 ? fmt("IN %.0f H", std::max(1.0, dd * 24.0)) : fmt("IN %.0f D", std::floor(dd))), P.ink3, Align::Center, 0.12f);
    }
}

// ---- Week ahead
void drawWeek(Ctx& c, float x, float y, float w, float h) {
    Gfx& g = c.g; App& a = c.a; const Palette& P = c.P; const float s = c.s;
    if (!a.wx.valid || a.wx.daily.empty()) { g.text(F_LIGHT, 18.f * s, x, y + 30 * s, Ws(a.wxOffline ? "Weather unavailable right now" : "Waiting for the weather"), P.ink3); return; }
    const double d0 = util::localMidnight(a.now, a.wx.utcOffset);
    size_t start = 0; double bestD = 1e18;
    for (size_t i = 0; i < a.wx.daily.size(); ++i) {
        double diff = std::fabs(a.wx.daily[i].t0 - d0);
        if (diff < bestD && diff < 43200.0) { bestD = diff; start = i; }
    }
    const int n = (int)std::min<size_t>(5, a.wx.daily.size() - start);
    const float cw = w / n;
    {
        const float off = std::max(0.f, (h - (std::min(cw * 0.6f, h * 0.34f) + 170 * s)) * 0.5f);
        y += off; h -= off;
    }
    for (int i = 0; i < n; ++i) {
        const weather::Day& d = a.wx.daily[start + i];
        float cx = x + cw * (i + 0.5f);
        util::LocalTime L = util::toLocal(d.t0 + 7200, a.wx.utcOffset);
        g.text(F_MONO_M, 12.f * s, cx, y + 12 * s, Ws(i == 0 ? "TODAY" : util::upper(util::WEEKDAYS3[L.wday])), i == 0 ? P.accent : P.ink2, Align::Center, 0.14f);
        float isz = std::min(cw * 0.6f, (h + 0) * 0.34f);
        g.weatherIcon(weather::describe(d.code, true).icon, cx, y + 26 * s + isz * 0.5f, isz, P.ink, P.accent, P.panel);
        float ty = y + 26 * s + isz + 38 * s;
        g.text(F_LIGHT, std::min(38.f * s, cw * 0.26f), cx, ty, Ws(tempStr(a, d.tmax)), P.ink, Align::Center);
        g.text(F_LIGHT, std::min(26.f * s, cw * 0.19f), cx, ty + 30 * s, Ws(tempStr(a, d.tmin)), P.ink3, Align::Center);
        // chance of rain
        float bw = cw * 0.5f, by = ty + 50 * s;
        if (by + 14 * s < y + h) {
            g.fillRoundRect(cx - bw * 0.5f, by, bw, 4.f * s, 2.f * s, mulA(P.ink, 0.12f));
            g.fillRoundRect(cx - bw * 0.5f, by, bw * clamp01((float)d.precipProbMax / 100.f), 4.f * s, 2.f * s, tint(P, P.accent2));
            g.text(F_MONO, 10.5f * s, cx, by + 18 * s, Ws(fmt("%.0f%%", d.precipProbMax)), P.ink3, Align::Center, 0.08f);
        }
    }
}

// ---- helpers for the list-style widgets
int fitRows(float h, float s, int maxN) { return std::max(1, std::min(maxN, (int)std::floor((h + 20 * s) / (112 * s)))); }

void note(Ctx& c, float x, float y, const std::string& t, const char* sub = nullptr) {
    c.g.text(F_SERIF_I, 26.f * c.s, x, y + 30 * c.s, Ws(t), c.P.ink3);
    if (sub) c.g.text(F_LIGHT, 18.f * c.s, x, y + 58 * c.s, Ws(sub), c.P.ink3);
}

// ---- Today
void drawTodayW(Ctx& c, float x, float y, float w, float h) {
    Gfx& g = c.g; App& a = c.a; const Palette& P = c.P; const float s = c.s;
    if (a.todayItems.empty()) { g.text(F_SERIF_I, 32.f * s, x, y + 36 * s, L"A quiet day.", P.ink2); return; }
    const int n = fitRows(h, s, 5);
    for (int i = 0; i < n && i < (int)a.todayItems.size(); ++i) {
        today::TodayItem it = a.todayItems[i];
        std::string tag = it.tag, title = it.title, detail = it.detail;
        if (tag == "ON THIS DAY") { tag += " \xC2\xB7 " + title; title = detail; detail.clear(); }
        const float yy = y + i * 112 * s;
        if (i > 0) g.line(x, yy - 18 * s, x + w, yy - 18 * s, P.line, std::max(1.f, s), false);
        drawInfoRow(c, x, yy, w, P.accent, tag, "", title, detail);
    }
}

// ---- Weather: the Sky face weather block (now, the summary sentence, the next hours), centred a little when the tile is tall
void drawWeatherW(Ctx& c, float x, float y, float w, float h) {
    const float s = c.s;
    if (!c.a.wx.valid) { drawWeatherBlock(c, x, y, w, h); return; }
    const float hh = std::min(h, 600.f * s);
    drawWeatherBlock(c, x, y + (h - hh) * 0.2f, w, hh);
}

// ---- Daylight
void drawDaylight(Ctx& c, float x, float y, float w, float h) {
    Gfx& g = c.g; App& a = c.a; const Palette& P = c.P; const float s = c.s;
    const astro::DayEvents& D = a.dayEv;
    if (D.dayLengthSec <= 0 || D.dayLengthSec >= 86400.0) {
        note(c, x, y, D.dayLengthSec <= 0 ? "The Sun stays below the horizon today" : "The Sun stays above the horizon all day");
        return;
    }
    const float bigFs = std::min(72.f * s, h * 0.24f);
    const int secs = (int)std::lround(D.dayLengthSec);
    std::string len = fmt("%dh %02dm", secs / 3600, secs % 3600 / 60);
    const float base = y + g.capHeight(F_LIGHT, bigFs) + 2 * s;
    g.text(F_LIGHT, bigFs, x, base, Ws(len), P.ink);
    std::string dl = a.dayLenText, cmp;
    { size_t p = dl.find(" \xC2\xB7 "); if (p != std::string::npos) cmp = dl.substr(p + 4); }
    g.text(F_MONO_M, 12.5f * s, x + g.textWidth(F_LIGHT, bigFs, Ws(len)) + 14 * s, base, L"OF DAYLIGHT", P.accent, Align::Left, 0.16f);
    if (!cmp.empty()) g.text(F_LIGHT, 19.f * s, x, base + 30 * s, g.ellipsize(F_LIGHT, 19.f * s, Ws(cmp), w), P.ink2);
    const float ry = base + (cmp.empty() ? 40.f : 82.f) * s;
    // three times
    struct Cell { const wchar_t* lab; std::string val; Color col; } cells[3] = {
        {L"SUNRISE", D.sun.hasRise ? clockStr(a, D.sun.rise) : "\xE2\x80\x94", P.accent},
        {L"SOLAR NOON", D.noon > 0 ? clockStr(a, D.noon) : "\xE2\x80\x94", P.ink2},
        {L"SUNSET", D.sun.hasSet ? clockStr(a, D.sun.set) : "\xE2\x80\x94", P.accent2}};
    const float cw3 = w / 3.f, vfs = std::min(34.f * s, cw3 * 0.2f);
    for (int i = 0; i < 3; ++i) {
        const float cxx = x + cw3 * i;
        g.text(F_MONO, 11.5f * s, cxx, ry, cells[i].lab, P.ink3, Align::Left, 0.14f);
        g.text(F_LIGHT, vfs, cxx, ry + vfs + 6 * s, Ws(cells[i].val), P.ink, Align::Left);
        g.fillCircle(cxx + g.textWidth(F_LIGHT, vfs, Ws(cells[i].val)) + 10 * s, ry + vfs * 0.55f + 6 * s, 2.8f * s, cells[i].col);
    }
    const float below = ry + vfs + 30 * s;                              // free space starts here
    const float bandH = 20.f * s, bandY = y + h - bandH - 34 * s;
    const bool band = bandY > below + 10 * s;
    // the Sun's path through the whole day: height above the horizon against the time of day
    const float cTop = below + 6 * s, cBot = (band ? bandY - 22 * s : y + h - 6 * s);
    if (a.bandSunAlt.size() == 288 && cBot - cTop > 110 * s) {
        const float R = cBot - cTop;
        double maxAlt = 5.0; for (float v : a.bandSunAlt) maxAlt = std::max(maxAlt, (double)v);
        const float hz = cTop + R * 0.80f, arcH = R * 0.74f, dip = R * 0.17f;
        auto yAt = [&](double alt) { return alt >= 0 ? hz - (float)(alt / maxAlt) * arcH : hz + (float)(std::max(alt, -18.0) / -18.0) * dip; };
        std::vector<Pt> all; all.reserve(288);
        for (int k = 0; k < 288; ++k) all.push_back({x + w * k / 287.f, yAt(a.bandSunAlt[k])});
        std::vector<Pt> area;
        for (const Pt& p : all) area.push_back({p.x, std::min(p.y, hz)});
        area.push_back({x + w, hz}); area.push_back({x, hz});
        g.fillPolygon(area, mulA(P.accent, 0.13f));
        g.line(x, hz, x + w, hz, P.line, std::max(1.f, s), false);
        for (int k = 0; k < 288;) {                                      // the part below the horizon, quiet; deep night (past -18 degrees) is left out
            if (a.bandSunAlt[k] <= -18.f) { ++k; continue; }
            int e = k; while (e < 288 && a.bandSunAlt[e] > -18.f) ++e;
            std::vector<Pt> run(all.begin() + k, all.begin() + e);
            if (run.size() > 1) g.polyline(run, false, mulA(P.ink3, 0.55f), std::max(1.5f, 1.8f * s), true);
            k = e;
        }
        for (int k = 0; k < 288;) {                                      // the part above the horizon, in colour
            if (a.bandSunAlt[k] < 0) { ++k; continue; }
            int e = k; while (e < 288 && a.bandSunAlt[e] >= 0) ++e;
            std::vector<Pt> run(all.begin() + k, all.begin() + e);
            if (run.size() > 1) g.polyline(run, false, P.accent, std::max(2.f, 2.6f * s), true);
            k = e;
        }
        // now
        double since = std::fmod(a.now + a.tzOff, 86400.0); if (since < 0) since += 86400.0;
        const double fk = since / 86400.0 * 287.0; const int k0 = std::max(0, std::min(286, (int)fk)); const double fr = fk - k0;
        const double alt = a.bandSunAlt[k0] * (1.0 - fr) + a.bandSunAlt[k0 + 1] * fr;
        const float nx = x + w * (float)(since / 86400.0), ny = yAt(alt);
        g.line(nx, ny, nx, hz, mulA(P.ink, 0.25f), std::max(1.f, s), false);
        g.fillCircle(nx, ny, 9.f * s, mulA(alt > 0 ? P.accent : P.ink2, 0.28f));
        g.fillCircle(nx, ny, 5.2f * s, alt > 0 ? P.accent : P.ink2);
        g.text(F_MONO, 11.5f * s, nx + (nx > x + w * 0.8f ? -12 * s : 12 * s), ny - 12 * s, Ws(fmt("%.0f\xC2\xB0", alt)), P.ink2, nx > x + w * 0.8f ? Align::Right : Align::Left, 0.06f);
    }
    if (band) drawDayBand(c, x, bandY, w, bandH);
}

// ---- Sky tonight
void drawSkyTonight(Ctx& c, float x, float y, float w, float h) {
    Gfx& g = c.g; App& a = c.a; const Palette& P = c.P; const float s = c.s;
    const int n = fitRows(h, s, 5);
    int shown = 0;
    auto at = [&]() { const float yy = y + shown * 112 * s; if (shown > 0) g.line(x, yy - 18 * s, x + w, yy - 18 * s, P.line, std::max(1.f, s), false); return yy; };
    // Moon
    {
        const astro::MoonInfo& m = a.moon;
        std::string l1 = fmt("%s \xC2\xB7 %.0f%% lit", astro::phaseName(m.elongation), m.illum * 100.0), l2;
        const astro::RiseSet& rs = a.moonDetail.rs;
        if (m.appAlt > -0.7) {
            l2 = fmt("%.0f\xC2\xB0 %s", m.appAlt, util::compass16(m.hor.az).c_str());
            if (rs.hasSet && rs.set > a.now) l2 += "  \xC2\xB7  sets " + clockStrDay(a, rs.set);
        } else {
            if (rs.hasRise && rs.rise > a.now) l2 = "Rises " + clockStrDay(a, rs.rise);
            else l2 = "Below the horizon";
            if (rs.hasRise && rs.rise > a.now && rs.hasSet && rs.set > rs.rise) l2 += "  \xC2\xB7  sets " + clockStrDay(a, rs.set);
        }
        drawInfoRow(c, x, at(), w, P.accent2, "MOON", "", l1, l2);
        ++shown;
    }
    // planets that are up (brightest first), or the next one to rise
    if (shown < n) {
        std::vector<const PlanetRow*> up;
        for (const auto& r : a.planets) if (r.info.hor.alt > 1.0) up.push_back(&r);
        std::sort(up.begin(), up.end(), [](const PlanetRow* p, const PlanetRow* q) { return p->info.mag < q->info.mag; });
        const float yy = at();
        if (!up.empty()) {
            std::string names;
            for (size_t i = 0; i < up.size() && i < 3; ++i) names += (i ? ", " : "") + std::string(up[i]->info.name);
            const PlanetRow& r = *up[0];
            std::string d = fmt("%s %.0f\xC2\xB0 %s  \xC2\xB7  mag %+.1f", r.info.name, r.info.appAlt, util::compass16(r.info.hor.az).c_str(), r.info.mag);
            drawInfoRow(c, x, yy, w, P.accent, "PLANETS UP", "", names, d);
        } else {
            const PlanetRow* nx = nullptr;
            for (const auto& r : a.planets) if (r.rs.hasRise && r.info.mag < 3.0 && (!nx || r.rs.rise < nx->rs.rise)) nx = &r;
            if (nx) drawInfoRow(c, x, yy, w, P.accent, "PLANETS", "", "None up right now", std::string(nx->info.name) + " rises " + clockStrDay(a, nx->rs.rise));
            else drawInfoRow(c, x, yy, w, P.accent, "PLANETS", "", "None up right now", "");
        }
        ++shown;
    }
    // coming events: up to two, or the next one beyond the window
    if (shown < n) {
        if (a.events.empty()) {
            const float yy = at();
            if (a.hasNextEvent) { const skyev::Event& e = a.nextEvent; drawInfoRow(c, x, yy, w, tint(P, eventKindColor(e.kind)), "NEXT  \xC2\xB7  " + eventKindLabel(e.kind), eventWhenShort(a, e), e.title, eventText(a, e.subtitle)); }
            else drawInfoRow(c, x, yy, w, P.ink3, "SKY EVENTS", "", "Nothing coming up", fmt("Events show up %d days before they happen.", a.st.evDays));
            ++shown;
        } else {
            const int room = n - shown, take = std::min<int>((int)a.events.size(), std::min(2, room > 2 ? room - 1 : room));
            for (int i = 0; i < take; ++i) {
                const skyev::Event& e = a.events[i];
                const float yy = at();
                drawInfoRow(c, x, yy, w, tint(P, eventKindColor(e.kind)), eventKindLabel(e.kind), eventWhenShort(a, e), e.title, eventText(a, e.subtitle));
                addEventRowHit(a, x - 8 * s, yy - 6 * s, w + 16 * s, 100 * s, i);
                ++shown;
            }
        }
    }
    // the Sun
    if (shown < n) {
        const double alt = a.sun.appAlt;
        std::string l1 = alt > 0 ? fmt("%.0f\xC2\xB0 up \xC2\xB7 %s", alt, util::compass16(a.sun.hor.az).c_str()) : fmt("%.0f\xC2\xB0 below the horizon", -alt), l2;
        if (a.dayEv.sun.hasRise) l2 += "Rise " + clockStr(a, a.dayEv.sun.rise);
        if (a.dayEv.sun.hasSet) l2 += (l2.empty() ? "" : "  \xC2\xB7  ") + std::string("Set ") + clockStr(a, a.dayEv.sun.set);
        drawInfoRow(c, x, at(), w, P.accent, "SUN", "", l1, l2);
        ++shown;
    }
}

// ---- Conditions
void drawConditions(Ctx& c, float x, float y, float w, float h) {
    Gfx& g = c.g; App& a = c.a; const Palette& P = c.P; const float s = c.s;
    const weather::Data& d = a.wx;
    if (!d.valid) { note(c, x, y, a.wxOffline ? "Weather unavailable right now" : "Waiting for the weather"); return; }
    const weather::Current& cu = d.cur;
    double uv = -1;
    if (d.hasUv && !d.daily.empty()) {
        double d0 = util::localMidnight(a.now, d.utcOffset);
        size_t di = 0; double bestD = 1e18;
        for (size_t i = 0; i < d.daily.size(); ++i) {
            double diff = std::fabs(d.daily[i].t0 - d0);
            if (diff < bestD && diff < 43200.0) { bestD = diff; di = i; }
        }
        uv = d.daily[di].uvMax;
    }
    struct Cell { const char* lab; std::string val, unit, sub; float frac; };
    std::vector<Cell> cells;
    cells.push_back({"HUMIDITY", cu.hasHumidity ? fmt("%.0f", cu.humidity) : "\xE2\x80\x94", cu.hasHumidity ? "%" : "", cu.hasHumidity ? (cu.humidity < 30 ? "Dry" : cu.humidity < 60 ? "Comfortable" : cu.humidity < 80 ? "Humid" : "Very humid") : "Not reported", cu.hasHumidity ? (float)cu.humidity / 100.f : 0.f});
    cells.push_back({"WIND", cu.hasWind ? fmt("%.0f", cu.wind) : "\xE2\x80\x94", cu.hasWind ? "km/h" : "", cu.hasWind ? (util::compass16(cu.windDir) + (cu.gust > cu.wind + 5 ? fmt("  \xC2\xB7  gusts %.0f", cu.gust) : "")) : "Not reported", cu.hasWind ? (float)std::min(1.0, cu.wind / 60.0) : 0.f});
    const bool hasPres = cu.pressure > 100.0;
    cells.push_back({"PRESSURE", hasPres ? fmt("%.0f", cu.pressure) : "\xE2\x80\x94", hasPres ? "hPa" : "", hasPres ? (cu.pressure < 1005 ? "Low" : cu.pressure > 1022 ? "High" : "Steady") : "Not reported", hasPres ? (float)clamp01((float)((cu.pressure - 980.0) / 60.0)) : 0.f});
    cells.push_back({"UV INDEX", uv >= 0 ? fmt("%.0f", uv) : "\xE2\x80\x94", "", uv >= 0 ? weather::uvLabel(uv) : "Not reported", uv >= 0 ? (float)std::min(1.0, uv / 11.0) : 0.f});
    cells.push_back({"AIR QUALITY", d.hasAir ? fmt("%.0f", d.aqi) : "\xE2\x80\x94", d.hasAir ? "AQI" : "", d.hasAir ? weather::aqiLabel(d.aqi) : "Not reported", d.hasAir ? (float)std::min(1.0, d.aqi / 200.0) : 0.f});
    cells.push_back({"CLOUD COVER", cu.hasCloud ? fmt("%.0f", cu.cloud) : "\xE2\x80\x94", cu.hasCloud ? "%" : "", cu.hasCloud ? (cu.cloud < 15 ? "Clear" : cu.cloud < 50 ? "Partly cloudy" : cu.cloud < 85 ? "Mostly cloudy" : "Overcast") : "Not reported", cu.hasCloud ? (float)cu.cloud / 100.f : 0.f});
    const int cols = w >= 380 * s ? 3 : 2;
    int rows = ((int)cells.size() + cols - 1) / cols;
    const float cw = w / cols, rh = h / rows;
    const float vfs = std::min(76.f * s, std::min(cw * 0.3f, rh * 0.34f));
    for (size_t i = 0; i < cells.size(); ++i) {
        const int col = (int)i % cols, row = (int)i / cols;
        const float cx0 = x + cw * col, cy0 = y + rh * row + std::max(0.f, (rh - (vfs + 120 * s)) * 0.25f);
        const float pad = 14.f * s, iw = cw - pad;
        const Cell& C = cells[i];
        g.text(F_MONO, 11.5f * s, cx0, cy0 + 12 * s, Ws(C.lab), P.ink3, Align::Left, 0.14f);
        float base = cy0 + 14 * s + vfs + 4 * s;
        g.text(F_LIGHT, vfs, cx0, base, Ws(C.val), P.ink, Align::Left);
        if (!C.unit.empty()) g.text(F_MONO, 13.5f * s, cx0 + g.textWidth(F_LIGHT, vfs, Ws(C.val)) + 8 * s, base, Ws(util::upper(C.unit)), P.ink3, Align::Left, 0.1f);
        const float sfs = std::min(22.f * s, cw * 0.1f);
        g.text(F_LIGHT, sfs, cx0, base + sfs + 8 * s, g.ellipsize(F_LIGHT, sfs, Ws(C.sub), iw), P.ink2, Align::Left);
        const float by = base + sfs + 26 * s;
        if (by + 4 * s < cy0 + rh - 4 * s) {
            g.fillRoundRect(cx0, by, iw - 6 * s, 5.f * s, 2.5f * s, mulA(P.ink, 0.12f));
            if (C.frac > 0.f) g.fillRoundRect(cx0, by, std::max(5.f * s, (iw - 6 * s) * clamp01(C.frac)), 5.f * s, 2.5f * s, P.accent);
        }
    }
}

// ---- Year
void drawYear(Ctx& c, float x, float y, float w, float h) {
    Gfx& g = c.g; App& a = c.a; const Palette& P = c.P; const float s = c.s;
    const util::LocalTime L = util::toLocal(a.now, a.tzOff);
    const today::YearInfo Y = today::yearInfo(L);
    const bool stack = w < h * 1.05f;
    const float barH = 34.f * s;                                        // the twelve months strip along the bottom
    const float ah = std::max(40.f * s, h - barH - 16 * s);
    float r = std::min(ah * 0.5f - 6 * s, (stack ? w * 0.34f : w * 0.22f));
    r = std::max(r, 28.f * s);
    const float lw = std::max(5.f, std::min(10.f * s, r * 0.14f));
    const float rcx = stack ? x + w * 0.5f : x + r + 8 * s, rcy = stack ? y + r + 4 * s : y + ah * 0.5f;
    g.strokeCircle(rcx, rcy, r, mulA(P.ink, 0.12f), lw);
    g.arc(rcx, rcy, r, 0.f, std::min(359.f, std::max(2.f, (float)(360.0 * Y.progress))), P.accent, lw, true);
    {
        const float fs = r * 0.5f;
        g.text(F_LIGHT, fs, rcx, rcy + g.capHeight(F_LIGHT, fs) * 0.5f, Ws(fmt("%d%%", (int)std::floor(Y.progress * 100.0))), P.ink, Align::Center);
    }
    // the words
    const float tx = stack ? x : rcx + r + 34 * s, tw = x + w - tx;
    float ty = stack ? rcy + r + 36 * s : y + std::max(0.f, ah * 0.5f - 78 * s);
    g.text(F_LIGHT, std::min(48.f * s, tw * 0.13f), tx, ty + 24 * s, Ws(fmt("Day %d of %d", Y.doy, Y.daysInYear)), P.ink, Align::Left);
    g.text(F_MONO_M, 15.f * s, tx, ty + 62 * s, Ws(fmt("WEEK %d  \xC2\xB7  %d DAYS LEFT", Y.week, Y.daysLeft)), P.accent, Align::Left, 0.14f);
    // the next season marker
    {
        static const char* SN[4] = {"March equinox", "June solstice", "September equinox", "December solstice"};
        double bestT = 1e18; int best = 0;
        for (int yy = L.y; yy <= L.y + 1; ++yy)
            for (int k = 0; k < 4; ++k) { double t = astro::seasonInstant(yy, k); if (t > a.now && t < bestT) { bestT = t; best = k; } }
        if (bestT < 1e17) {
            int dd = (int)std::floor((bestT - a.now) / 86400.0);
            util::LocalTime S = util::toLocal(bestT, a.tzOff);
            g.text(F_LIGHT, std::min(25.f * s, tw * 0.07f), tx, ty + 100 * s, g.ellipsize(F_LIGHT, std::min(25.f * s, tw * 0.07f), Ws(fmt("%s \xC2\xB7 %d %s \xC2\xB7 in %d days", SN[best], S.d, util::MONTHS3[S.m - 1], dd)), tw), P.ink2, Align::Left);
        }
    }
    // twelve months: full ones filled, the current one partly
    {
        const float by = y + h - 14.f * s - 14 * s, bg = 5.f * s, bw = (w - 11 * bg) / 12.f;
        static const int dm[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        for (int mI = 0; mI < 12; ++mI) {
            const float bx = x + mI * (bw + bg);
            g.fillRoundRect(bx, by, bw, 8.f * s, 4.f * s, mulA(P.ink, 0.12f));
            float f = mI + 1 < L.m ? 1.f : (mI + 1 == L.m ? (float)((L.d - 1 + (L.hh * 60 + L.mm) / 1440.0) / (dm[mI] + (mI == 1 && astro::isLeap(L.y) ? 1 : 0))) : 0.f);
            if (f > 0.f) g.fillRoundRect(bx, by, std::max(4.f * s, bw * f), 8.f * s, 4.f * s, mI + 1 == L.m ? P.accent : withA(P.accent, 0.55f * P.accent.a));
            g.text(F_MONO, 10.5f * s, bx + bw * 0.5f, by + 26 * s, Ws(std::string(1, util::MONTHS3[mI][0])), mI + 1 == L.m ? P.ink : P.ink3, Align::Center, 0.08f);
        }
    }
}

} // namespace

int cityCount() { return NCITY; }
const char* cityName(int i) { return (i >= 0 && i < NCITY) ? CITIES[i].name : ""; }

void App::pollSystem() {
    if (now - sys.polledAt < 2.0 && now >= sys.polledAt) return;
    sys.polledAt = now;
    FILETIME idle, kern, user;
    if (GetSystemTimes(&idle, &kern, &user)) {
        auto u = [](const FILETIME& f) { return ((unsigned long long)f.dwHighDateTime << 32) | f.dwLowDateTime; };
        unsigned long long I = u(idle), K = u(kern), U = u(user);
        if (sys.kern0) {
            unsigned long long dI = I - sys.idle0, dT = (K - sys.kern0) + (U - sys.user0);
            if (dT > 0) sys.cpu = (int)std::lround(100.0 * (1.0 - (double)dI / (double)dT));
        }
        sys.idle0 = I; sys.kern0 = K; sys.user0 = U;
    }
    MEMORYSTATUSEX ms; ms.dwLength = sizeof ms;
    if (GlobalMemoryStatusEx(&ms)) sys.ram = (int)ms.dwMemoryLoad;
    SYSTEM_POWER_STATUS ps;
    if (GetSystemPowerStatus(&ps)) {
        sys.hasBattery = ps.BatteryFlag != 128 && ps.BatteryLifePercent <= 100;
        sys.battery = ps.BatteryLifePercent <= 100 ? ps.BatteryLifePercent : -1;
        sys.charging = ps.ACLineStatus == 1;
    }
    sys.uptimeSec = (double)GetTickCount64() / 1000.0;
}

void drawWorldClocks(Ctx& c, float x, float y, float w, float h) {
    c.g.text(F_MONO, 14.f * c.s, x, y - 14 * c.s, L"WORLD CLOCKS", c.P.ink3, Align::Left, 0.22f);
    drawWorld(c, x, y + 4 * c.s, w, h - 4 * c.s);
}

void drawWidgetTile(Ctx& c, int id, float x, float y, float w, float h) {
    float cx, cy, cw, ch;
    tile(c, x, y, w, h, widgetName(id), cx, cy, cw, ch);
    c.g.pushClip(x + 2, y + 2, w - 4, h - 4);
    switch (id) {
        case WG_WORLD: drawWorld(c, cx, cy, cw, ch); break;
        case WG_MONTH: drawMonth(c, cx, cy, cw, ch); break;
        case WG_SYSTEM: drawSystem(c, cx, cy, cw, ch); break;
        case WG_NOTE: drawNote(c, cx, cy, cw, ch); break;
        case WG_COUNTDOWN: drawCountdown(c, cx, cy, cw, ch); break;
        case WG_PHASES: drawPhases(c, cx, cy, cw, ch); break;
        case WG_WEEK: drawWeek(c, cx, cy, cw, ch); break;
        case WG_TODAY: drawTodayW(c, cx, cy, cw, ch); break;
        case WG_HOLIDAYS: drawHolidayPanel(c, cx, cy, cw, ch, false, 5); break;
        case WG_HOURLY: drawWeatherW(c, cx, cy, cw, ch); break;
        case WG_DAYLIGHT: drawDaylight(c, cx, cy, cw, ch); break;
        case WG_SKY: drawSkyTonight(c, cx, cy, cw, ch); break;
        case WG_AIR: drawConditions(c, cx, cy, cw, ch); break;
        case WG_YEAR: drawYear(c, cx, cy, cw, ch); break;
        default: break;
    }
    c.g.popClip();
}

// ---------------------------------------------------------------- the Widgets face
void App::renderWidgets(Gfx& g, int w, int h) {
    const float W = (float)w, H = (float)h, s = layoutScale(w, h);
    const bool portrait = W < H * 0.90f;
    Ctx c{g, *this, pal, W, H, s};
    const Palette& P = pal;
    const util::LocalTime L = util::toLocal(now, tzOff);
    const ClockParts cp = clockParts(*this);
    const float m = (portrait ? 40.f : 64.f) * s;

    // header: the time at the left, the date at the right
    float em = portrait ? std::min(0.11f * H, (W - 2 * m) * 0.34f) : 0.17f * H;
    float capH = g.capHeight(F_DISPLAY, em);
    float base = m * 0.75f + capH;
    float twA = g.timeWidth(F_DISPLAY, em, cp.hm);
    g.timeText(F_DISPLAY, em, m, base, cp.hm, P.ink, lerp(P.ink, P.ink2, 0.55f), Align::Left);
    if (!cp.ampm.empty()) { float fs = 0.1f * em; g.text(F_MONO_M, fs, m + twA + 0.05f * em, base - capH + g.capHeight(F_MONO_M, fs), cp.ampm, P.ink2, Align::Left, 0.14f); }
    {
        float fs1 = std::min(46.f * s, em * 0.34f), fs2 = fs1 * 0.72f;
        g.text(F_SERIF, fs1, W - m, base - fs1 * 0.55f, Ws(util::WEEKDAYS[L.wday]), P.ink, Align::Right);
        g.text(F_SERIF_I, fs2, W - m, base + fs2 * 0.5f, Ws(fmt("%d %s", L.d, util::MONTHS[L.m - 1])), P.ink2, Align::Right);
    }

    // pages of widgets: wgPerPage at a time (up to six), taking turns with the same transition as the Sky panel
    pollSystem();
    std::vector<int> list;
    for (int id : st.widgets) if (id >= 0 && id < WG_COUNT && std::find(list.begin(), list.end(), id) == list.end()) list.push_back(id);
    const float top = base + (portrait ? 34.f : 52.f) * s, bottom = H - m * 0.9f;
    const float gap = 22.f * s;
    if (list.empty()) {
        wgPages = 0; wgtRot.cur = wgtRot.prev = -1;
        g.text(F_SERIF_I, 40.f * s, W * 0.5f, (top + bottom) * 0.5f, L"No widgets yet", P.ink2, Align::Center);
        g.text(F_LIGHT, 20.f * s, W * 0.5f, (top + bottom) * 0.5f + 42 * s, L"Open Settings from the dock, or press the comma key, and add some.", P.ink3, Align::Center);
        return;
    }
    const int per = st.wgPerPage <= 0 ? 6 : std::max(1, std::min(6, st.wgPerPage));
    const int total = (int)list.size();
    wgPages = (total + per - 1) / per;
    std::vector<int> pages;
    for (int i = 0; i < wgPages; ++i) pages.push_back(i);

    const float mx = mouseX - shiftX, my = mouseY - shiftY;
    const bool over = st.panelHold && mouseX >= 0 && (now - lastMouseMove) < 8.0 && mx >= m - 10 * s && mx <= W - m + 10 * s && my >= top - 40 * s && my <= bottom + 10 * s;
    rotorUpdate(*this, wgtRot, pages, st.wgSecs, over);

    auto drawPage = [&](Ctx& cc, int page) {
        const int i0 = page * per, n = std::min(per, total - i0);
        if (n <= 0) return;
        const int cols = portrait ? (n >= 5 ? 2 : 1) : (n == 1 ? 1 : n == 2 ? 2 : n == 3 ? 3 : n == 4 ? 2 : 3);
        const int rows = (n + cols - 1) / cols;
        float aw = W - 2 * m, ah = bottom - top;
        float tw = (aw - gap * (cols - 1)) / cols, th = (ah - gap * (rows - 1)) / rows;
        if (n == 1 && !portrait) tw = std::min(tw, std::max(ah * 1.55f, 760.f * s));       // one widget alone should not stretch across the whole screen
        for (int i = 0; i < n; ++i) {
            const int r = i / cols, col = i % cols;
            const int inRow = std::min(cols, n - r * cols);            // centre a short last row
            const float rowW = inRow * tw + (inRow - 1) * gap;
            const float x0 = m + (aw - rowW) * 0.5f + col * (tw + gap);
            drawWidgetTile(cc, list[i0 + i], x0, top + r * (th + gap), tw, th);
        }
    };
    g.pushClip(0, top - 30 * s, W, bottom - top + 60 * s);
    rotorDraw(c, wgtRot, 84 * s, [&](Ctx& cc, int page) { drawPage(cc, page); });
    g.popClip();
    if (st.panelDots && wgPages > 1) rotorDots(c, wgtRot, pages, st.wgSecs, m + 4 * s, top - (portrait ? 16.f : 22.f) * s, 1);       // under the time, clear of the dock
}

} // namespace app
