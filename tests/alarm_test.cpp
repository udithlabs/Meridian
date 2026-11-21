// g++ -std=c++17 -I src tests/alarm_test.cpp src/astro.cpp -o alarm_test && ./alarm_test
// Checks util::nextLocalClockTime against a fake place that follows US daylight-saving rules (UTC-5 standard, UTC-4 summer;
// 2026: clocks go ahead on 8 March at 02:00 local, back on 1 November at 02:00 local) and one with no daylight saving.
#include "util.h"
#include <cstdio>
static int fails = 0;
static double utc(int y, int m, int d, int hh, int mm) { return (double)astro::daysFromCivil(y, m, d) * 86400.0 + hh * 3600.0 + mm * 60.0; }
// US Eastern 2026
static int eastern(double t) {
    const double start = utc(2026, 3, 8, 7, 0);      // 02:00 EST = 07:00 UTC
    const double end   = utc(2026, 11, 1, 6, 0);     // 02:00 EDT = 06:00 UTC
    return (t >= start && t < end) ? -4 * 3600 : -5 * 3600;
}
static int colombo(double) { return 5 * 3600 + 1800; }
static int kathmandu(double) { return 5 * 3600 + 2700; }
template <class F> static void check(const char* what, double now, int minutes, F off, double want) {
    double got = util::nextLocalClockTime(now, minutes, off);
    if (std::fabs(got - want) > 0.5) {
        util::LocalTime g = util::toLocal(got, off(got)), w = util::toLocal(want, off(want));
        printf("FAIL %s: got %04d-%02d-%02d %02d:%02d local (unix %.0f), wanted %04d-%02d-%02d %02d:%02d (unix %.0f)\n", what, g.y, g.m, g.d, g.hh, g.mm, got, w.y, w.m, w.d, w.hh, w.mm, want);
        ++fails;
    }
}
int main() {
    // plain day, no daylight saving
    check("colombo later today", utc(2026, 11, 7, 3, 0), 7 * 60 + 0, colombo, utc(2026, 11, 7, 1, 30) + 86400.0);   // 03:00Z = 08:30 local -> next 07:00 is tomorrow
    check("colombo this morning", utc(2026, 11, 6, 22, 0), 7 * 60, colombo, utc(2026, 11, 7, 1, 30));               // 03:30 local now -> 07:00 local today
    check("kathmandu", utc(2026, 11, 7, 0, 0), 6 * 60 + 30, kathmandu, utc(2026, 11, 7, 0, 45));                   // 05:45 local now, 06:30 local = 00:45Z
    check("exactly now moves to tomorrow", utc(2026, 11, 7, 1, 30), 7 * 60, colombo, utc(2026, 11, 7, 1, 30) + 86400.0);

    // the night the clocks go ahead (8 March 2026): the old code (midnight + 7 h + 86400) was an hour late or early
    // alarm 07:00, armed at 22:00 EST on 7 March. 07:00 EDT on the 8th = 11:00 UTC (EST would have been 12:00 UTC)
    check("spring forward, 07:00", utc(2026, 3, 8, 3, 0), 7 * 60, eastern, utc(2026, 3, 8, 11, 0));
    // armed at 00:30 EST on the 8th, before the change: still 07:00 EDT
    check("spring forward, armed after midnight", utc(2026, 3, 8, 5, 30), 7 * 60, eastern, utc(2026, 3, 8, 11, 0));
    // 02:30 on 8 March never happens: it rings when the old clock would have shown it (07:30 UTC = 03:30 EDT)
    check("spring forward, skipped 02:30", utc(2026, 3, 8, 3, 0), 2 * 60 + 30, eastern, utc(2026, 3, 8, 7, 30));
    // the day after the change is plain again
    check("after spring forward", utc(2026, 3, 8, 12, 0), 7 * 60, eastern, utc(2026, 3, 9, 11, 0));
    // the time that is right on the edge: 03:00 EDT exists, 02:00 does not
    check("spring forward, 03:00", utc(2026, 3, 8, 3, 0), 3 * 60, eastern, utc(2026, 3, 8, 7, 0));
    check("spring forward, 02:00 (the first skipped minute)", utc(2026, 3, 8, 3, 0), 2 * 60, eastern, utc(2026, 3, 8, 7, 0));

    // the night the clocks go back (1 November 2026)
    // 07:00 EST on the 1st = 12:00 UTC (EDT would have been 11:00 UTC)
    check("fall back, 07:00", utc(2026, 11, 1, 3, 0), 7 * 60, eastern, utc(2026, 11, 1, 12, 0));
    // 01:30 happens twice on the 1st: first at 05:30 UTC (EDT), then at 06:30 UTC (EST). Armed before it -> the first one
    check("fall back, 01:30 first time", utc(2026, 11, 1, 3, 0), 1 * 60 + 30, eastern, utc(2026, 11, 1, 5, 30));
    // armed between the two -> the second one
    check("fall back, 01:30 second time", utc(2026, 11, 1, 6, 0), 1 * 60 + 30, eastern, utc(2026, 11, 1, 6, 30));
    // armed after both -> tomorrow (01:30 EST on the 2nd = 06:30 UTC)
    check("fall back, 01:30 after both", utc(2026, 11, 1, 7, 0), 1 * 60 + 30, eastern, utc(2026, 11, 2, 6, 30));
    // the evening before: 23:00 on 31 October is still EDT = 03:00 UTC on the 1st
    check("fall back, evening before", utc(2026, 10, 31, 20, 0), 23 * 60, eastern, utc(2026, 11, 1, 3, 0));

    // sweep: every minute of the day, armed at a spread of moments over a whole year, the answer is always in the future, within
    // 25 hours, and (when that wall-clock minute exists) the clock really shows it
    int bad = 0;
    for (int m = 0; m < 1440; m += 7)
        for (double now = utc(2026, 1, 1, 0, 0); now < utc(2027, 1, 1, 0, 0); now += 3 * 3600.0 + 1234.0) {
            double got = util::nextLocalClockTime(now, m, eastern);
            if (got <= now || got - now > 25 * 3600.0 + 1) { ++bad; continue; }
            util::LocalTime L = util::toLocal(got, eastern(got));
            const bool gapMinute = (got >= utc(2026, 3, 8, 7, 0) && got < utc(2026, 3, 8, 8, 0));   // the hour right after the skipped one
            if (L.hh * 60 + L.mm != m && !gapMinute) ++bad;
        }
    if (bad) { printf("FAIL sweep: %d bad answers\n", bad); ++fails; }

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
