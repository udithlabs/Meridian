// Small portable helpers: UTF-8, time formatting, ini parsing.
#pragma once
#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include "astro.h"

namespace util {

inline std::wstring widen(const std::string& s) {
    auto isCont = [&](size_t idx) -> bool { return idx < s.size() && (((unsigned char)s[idx] & 0xC0u) == 0x80u); };
    std::wstring w; w.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = (unsigned char)s[i];
        uint32_t cp = '?'; size_t n = 1;
        if (c < 0x80u) {
            cp = c; n = 1;
        } else if ((c >> 5) == 0x06u && isCont(i + 1)) {
            const uint32_t v = ((uint32_t)(c & 0x1Fu) << 6) | ((unsigned char)s[i + 1] & 0x3Fu);
            if (v >= 0x80u) { cp = v; n = 2; }
        } else if ((c >> 4) == 0x0Eu && isCont(i + 1) && isCont(i + 2)) {
            const uint32_t v = ((uint32_t)(c & 0x0Fu) << 12) | (((unsigned char)s[i + 1] & 0x3Fu) << 6) | ((unsigned char)s[i + 2] & 0x3Fu);
            if (v >= 0x800u && !(v >= 0xD800u && v <= 0xDFFFu)) { cp = v; n = 3; }
        } else if ((c >> 3) == 0x1Eu && isCont(i + 1) && isCont(i + 2) && isCont(i + 3)) {
            const uint32_t v = ((uint32_t)(c & 0x07u) << 18) | (((unsigned char)s[i + 1] & 0x3Fu) << 12) | (((unsigned char)s[i + 2] & 0x3Fu) << 6) | ((unsigned char)s[i + 3] & 0x3Fu);
            if (v >= 0x10000u && v <= 0x10FFFFu) { cp = v; n = 4; }
        }
        i += n;
        if (cp >= 0x10000u) { cp -= 0x10000u; w.push_back((wchar_t)(0xD800u + (cp >> 10))); w.push_back((wchar_t)(0xDC00u + (cp & 0x3FFu))); }
        else w.push_back((wchar_t)cp);
    }
    return w;
}

inline std::string narrow(const std::wstring& w) {
    std::string s;
    for (size_t i = 0; i < w.size(); ++i) {
        uint32_t cp = (uint32_t)w[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < w.size()) {
            uint32_t lo = (uint32_t)w[i + 1];
            if (lo >= 0xDC00 && lo <= 0xDFFF) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); ++i; }
            else cp = 0xFFFD;
        } else if (cp >= 0xD800 && cp <= 0xDFFF) {
            cp = 0xFFFD;
        }
        if (cp < 0x80) s.push_back((char)cp);
        else if (cp < 0x800) { s.push_back((char)(0xC0 | (cp >> 6))); s.push_back((char)(0x80 | (cp & 0x3F))); }
        else if (cp < 0x10000) { s.push_back((char)(0xE0 | (cp >> 12))); s.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); s.push_back((char)(0x80 | (cp & 0x3F))); }
        else { s.push_back((char)(0xF0 | (cp >> 18))); s.push_back((char)(0x80 | ((cp >> 12) & 0x3F))); s.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); s.push_back((char)(0x80 | (cp & 0x3F))); }
    }
    return s;
}

inline std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
inline std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap, ap2;
    va_start(ap, f);
    va_copy(ap2, ap);
    int n = vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return {}; }
    if ((size_t)n < sizeof buf) { va_end(ap2); return std::string(buf, (size_t)n); }
    std::string out((size_t)n + 1, '\0');
    vsnprintf(&out[0], out.size(), f, ap2);
    va_end(ap2);
    out.resize((size_t)n);
    return out;
}

inline std::string upper(std::string s) { for (auto& c : s) if (c >= 'a' && c <= 'z') c -= 32; return s; }
inline std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

static const char* const MONTHS[12] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
static const char* const MONTHS3[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
static const char* const WEEKDAYS[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
static const char* const WEEKDAYS3[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

struct LocalTime { int y, m, d, hh, mm, ss; int wday; double fracSec; long long dayIndex; };

inline LocalTime toLocal(double unixSec, int tzOffSec) {
    double t = unixSec + tzOffSec;
    long long days = (long long)std::floor(t / 86400.0);
    double rem = t - days * 86400.0;
    if (rem >= 86400.0) { rem -= 86400.0; ++days; }
    else if (rem < 0.0) { rem += 86400.0; --days; if (rem >= 86400.0) { rem = 0.0; ++days; } }
    LocalTime L{};
    astro::civilFromDays(days, L.y, L.m, L.d);
    L.hh = std::max(0, std::min(23, (int)(rem / 3600.0))); rem -= L.hh * 3600.0;
    L.mm = std::max(0, std::min(59, (int)(rem / 60.0))); rem -= L.mm * 60.0;
    L.ss = std::max(0, std::min(59, (int)rem));
    L.fracSec = std::max(0.0, std::min(0.999999999, rem - L.ss));
    L.wday = astro::dayOfWeek(L.y, L.m, L.d);
    L.dayIndex = days;
    return L;
}

inline double localMidnight(double unixSec, int tzOffSec) {
    double t = unixSec + tzOffSec;
    double days = std::floor(t / 86400.0);
    return days * 86400.0 - tzOffSec;
}

// ---------------- calendar dates and times typed by a person (settings, countdowns, theme files)
// One strict checker for all of them: an impossible date such as 31 February or 99:99 is refused, never "fixed" into some other day.
inline bool isLeapYear(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
inline int daysInMonth(int y, int m) {
    static const int d[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m < 1 || m > 12) return 0;
    return (m == 2 && isLeapYear(y)) ? 29 : d[m - 1];
}
inline bool validDate(int y, int m, int d) { return y >= 1900 && y <= 2200 && m >= 1 && m <= 12 && d >= 1 && d <= daysInMonth(y, m); }
// a date that repeats every year: 29 February counts (it is shown on 28 February in years that have no 29th)
inline bool validDayMonth(int m, int d) { return m >= 1 && m <= 12 && d >= 1 && d <= (m == 2 ? 29 : daysInMonth(2001, m)); }
inline bool validTimeOfDay(int hh, int mm) { return hh >= 0 && hh <= 23 && mm >= 0 && mm <= 59; }
inline bool onlySpace(const char* p) { while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p; return *p == 0; }
// "YYYY-MM-DD" and nothing else (spaces at the ends are fine)
inline bool parseDateStrict(const std::string& s, int& y, int& m, int& d) {
    int a = 0, b = 0, c = 0, n = 0;
    if (std::sscanf(s.c_str(), " %d-%d-%d%n", &a, &b, &c, &n) != 3 || !onlySpace(s.c_str() + n) || !validDate(a, b, c)) return false;
    y = a; m = b; d = c; return true;
}
// "YYYY-MM-DD" or "YYYY-MM-DD HH:MM" and nothing else
inline bool parseDateTimeStrict(const std::string& s, int& y, int& m, int& d, int& hh, int& mm) {
    int a = 0, b = 0, c = 0, n = 0, h = 0, mi = 0, n2 = 0;
    if (std::sscanf(s.c_str(), " %d-%d-%d%n", &a, &b, &c, &n) != 3 || !validDate(a, b, c)) return false;
    if (onlySpace(s.c_str() + n)) { y = a; m = b; d = c; hh = 0; mm = 0; return true; }
    if (std::sscanf(s.c_str() + n, " %d:%d%n", &h, &mi, &n2) != 2 || !onlySpace(s.c_str() + n + n2) || !validTimeOfDay(h, mi)) return false;
    y = a; m = b; d = c; hh = h; mm = mi; return true;
}

// The next moment (unix seconds, after 'now') at which the wall clock shows minuteOfDay (minutes after midnight), for a place whose UTC
// offset can change (daylight saving). offsetAt(unix) must give the offset in seconds for ANY instant, not just now.
//   a time that happens twice (clocks go back)  -> the first time it shows
//   a time that never happens (clocks go ahead)  -> the moment the old offset would have shown it (just after the gap)
template <class OffsetAt>
inline double nextLocalClockTime(double now, int minuteOfDay, OffsetAt offsetAt) {
    const long long today = (long long)std::floor((now + offsetAt(now)) / 86400.0);
    for (long long k = -1; k <= 3; ++k) {
        const double w = (double)(today + k) * 86400.0 + minuteOfDay * 60.0;      // that wall-clock reading, as if it were UTC
        const double offBefore = offsetAt(w - 86400.0), offAfter = offsetAt(w + 86400.0);   // the offsets on either side of any change that day
        double best = 0; bool found = false, shows = false;
        for (double u : {w - offBefore, w - offAfter}) {
            if (std::fabs(u + offsetAt(u) - w) > 0.5) continue;                 // the clock does not show w at that instant
            shows = true;
            if (u > now && (!found || u < best)) { best = u; found = true; }
        }
        if (found) return best;
        if (!shows && w - offBefore > now) return w - offBefore;                // the clock never shows w that day (skipped by the change)
    }
    return now + 86400.0;                                                          // cannot happen with real time zones
}

// A date at the start of some text: "2027-03-14 Exam" (happens once) or "12-25 Christmas" (every year: y comes back 0). Returns how many
// characters it used, or 0 when it is not a real date. The date must be followed by a space or the end ("12-25-2027" is refused).
inline int parseEventDatePrefix(const char* p, int& y, int& m, int& d) {
    int a = 0, b = 0, c = 0, n = 0;
    if (std::sscanf(p, " %d-%d-%d%n", &a, &b, &c, &n) == 3 && (p[n] == 0 || p[n] == ' ' || p[n] == '\t') && validDate(a, b, c)) { y = a; m = b; d = c; return n; }
    n = 0;
    if (std::sscanf(p, " %d-%d%n", &a, &b, &n) == 2 && n > 0 && (p[n] == 0 || p[n] == ' ' || p[n] == '\t') && validDayMonth(a, b)) { y = 0; m = a; d = b; return n; }
    return 0;
}

// "9:42 PM" or "21:42"
inline std::string fmtClock(double unixSec, int tzOff, bool h24, bool withAmPm = true) {
    LocalTime L = toLocal(unixSec + 0.5 * 60.0 * 0 + 30.0, tzOff);   // round to nearest minute
    if (h24) return fmt("%02d:%02d", L.hh, L.mm);
    int h = L.hh % 12; if (h == 0) h = 12;
    return withAmPm ? fmt("%d:%02d %s", h, L.mm, L.hh < 12 ? "AM" : "PM") : fmt("%d:%02d", h, L.mm);
}

inline std::string fmtDuration(double sec) {
    long long s = std::llround(std::fabs(sec));
    long long h = s / 3600, m = (s % 3600) / 60;
    if (h > 0) return fmt("%lldh %02lldm", h, m);
    return fmt("%lldm", m);
}

inline std::string fmtDeltaMinSec(double sec) {
    long long s = std::llround(std::fabs(sec));
    long long m = s / 60, r = s % 60;
    if (m > 0) return fmt("%lldm %02llds", m, r);
    return fmt("%llds", r);
}

inline std::string compass16(double az) {
    static const char* n[16] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
    int i = (int)std::floor(astro::norm360(az) / 22.5 + 0.5) % 16;
    return n[i];
}

inline std::string ordinalDay(int d) { return fmt("%d", d); }

// ---------------- ini
struct Ini {
    std::map<std::string, std::map<std::string, std::string>> sections;   // section -> key -> value
    std::vector<std::pair<std::string, std::string>> ordered(const std::string& sec) const {
        std::vector<std::pair<std::string, std::string>> v;
        auto it = orderedData.find(sec);
        if (it != orderedData.end()) v = it->second;
        return v;
    }
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> orderedData;

    static Ini parse(const std::string& text) {
        Ini ini; std::string sec;
        size_t i = (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) ? 3 : 0;
        while (i <= text.size()) {
            size_t e = text.find('\n', i); if (e == std::string::npos) e = text.size();
            std::string line = trim(text.substr(i, e - i));
            i = e + 1;
            if (line.empty() || line[0] == ';' || line[0] == '#') continue;
            if (line[0] == '[' && line.back() == ']') { sec = trim(line.substr(1, line.size() - 2)); continue; }
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
            ini.sections[sec][k] = v;
            ini.orderedData[sec].push_back({k, v});
        }
        return ini;
    }
    std::string get(const std::string& sec, const std::string& key, const std::string& def = "") const {
        auto s = sections.find(sec); if (s == sections.end()) return def;
        auto k = s->second.find(key); if (k == s->second.end()) return def;
        return k->second;
    }
    double getNum(const std::string& sec, const std::string& key, double def) const {
        std::string v = get(sec, key); if (v.empty()) return def; char* end = nullptr; double d = strtod(v.c_str(), &end);
        return (end == v.c_str() || !std::isfinite(d)) ? def : d;
    }
    bool getBool(const std::string& sec, const std::string& key, bool def) const {
        std::string v = upper(get(sec, key)); if (v.empty()) return def;
        return v == "1" || v == "TRUE" || v == "YES" || v == "ON";
    }
};

} // namespace util
