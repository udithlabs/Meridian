// "What's special today": observances, lunar events, seasons, meteor showers, sky events. Portable.
#pragma once
#include <string>
#include <vector>
#include "astro.h"
#include "util.h"

namespace today {

struct TodayItem {
    std::string tag;      // short caps label, e.g. "POYA", "SKY"
    std::string title;
    std::string detail;
    int priority = 0;
};

struct CustomEvent {                         // y == 0: every year
    int y = 0; int m = 1; int d = 1; std::string title;
    // Is it on this calendar day? A yearly 29 February is kept on 28 February in the years that have no 29th.
    bool on(int Y, int M, int D) const {
        if (y != 0 && y != Y) return false;
        if (m == M && d == D) return true;
        return y == 0 && m == 2 && d == 29 && M == 2 && D == 28 && !util::isLeapYear(Y);
    }
};

struct StormLine { std::string title, detail; };   // a named storm near the place, shown first

struct Input {
    double now = 0;
    int tzOff = 0;
    int (*tzAt)(double unix) = nullptr;     // optional per-timestamp UTC offset (enables DST transition notices strictly in DST zones)
    astro::Geo geo;
    bool h24 = false;
    bool poya = true;                       // holidays (of the countries switched on) and Poya naming in the Today list
    bool omitMoonFill = false;              // leave out the "Moon, % lit" line that pads a quiet day (the Sky and Almanac faces show the Moon already)
    std::vector<CustomEvent> custom;
    std::vector<StormLine> storms;
};

struct YearInfo { int doy, daysInYear, daysLeft, week, isoYear; double progress; };
YearInfo yearInfo(const util::LocalTime& L);

bool inSriLanka(const astro::Geo& g);
std::vector<TodayItem> buildToday(const Input& in);        // sorted by priority, high first
std::string poyaName(int month);                           // Gregorian month 1-12
std::string dayLengthText(const Input& in);                // "11h 58m · 1m 05s shorter than yesterday"

} // namespace today
