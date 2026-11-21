// A once-a-day schedule (src/daily.h): exactly one moment per local day, the same after a restart, in its stretch of the day. No Windows needed.
#include "daily.h"
#include <cstdio>
#include <cmath>
#include <set>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main() {
    const double LO = 19.0, HI = 23.0, DUR = 2.5; const int SALT = 400;
    const double t0 = 1767225600.0;                       // 2026-01-01 00:00 UTC
    for (int tz : {0, 19800, -18000, 43200, -39600}) {
        std::set<long long> seconds; int fires = 0;
        for (int day = 0; day < 400; ++day) {
            // walk the whole day in 1-second steps would be slow; instead sample every second inside a +-1 minute window around the start and a coarse grid elsewhere
            const double dayStart = t0 + day * 86400.0;
            const long long dn = daily::dayOf(dayStart + 43200.0, tz);
            const double st = daily::startOn(dn, tz, LO, HI, SALT);
            const double local = std::fmod(st + tz, 86400.0) / 3600.0;
            CHECK(local >= LO && local <= HI);                                              // in the evening stretch, in the clock's own time
            CHECK(daily::dayOf(st + DUR, tz) == dn);                                        // and over before midnight
            double s2; CHECK(daily::activeAt(st, tz, DUR, LO, HI, SALT, s2) && s2 == st);   // active at its start ...
            CHECK(daily::activeAt(st + DUR, tz, DUR, LO, HI, SALT, s2));                    // ... to its end
            CHECK(!daily::activeAt(st - 0.01, tz, DUR, LO, HI, SALT, s2) || daily::dayOf(st - 0.01, tz) != dn);
            CHECK(!daily::activeAt(st + DUR + 0.01, tz, DUR, LO, HI, SALT, s2));
            // never twice in a day: a grid of the whole local day finds one flight, no more
            int hits = 0; bool prev = false;
            for (double x = dn * 86400.0 - tz; x < (dn + 1) * 86400.0 - tz; x += 0.5) {
                const bool on = daily::activeAt(x, tz, DUR, LO, HI, SALT, s2);
                if (on && !prev) ++hits;
                prev = on;
            }
            CHECK(hits == 1); fires += hits;
            seconds.insert((long long)std::llround(local * 60.0));                          // minute of the evening it picked
            // the next start is always after t, and today's while it is still to come
            CHECK(daily::nextStart(st - 5.0, tz, LO, HI, SALT) == st);
            CHECK(daily::nextStart(st, tz, LO, HI, SALT) > st + 20 * 3600.0 - (HI - LO) * 3600.0);
            CHECK(daily::nextStart(st, tz, LO, HI, SALT) == daily::startOn(dn + 1, tz, LO, HI, SALT));
        }
        CHECK(fires == 400);
        CHECK(seconds.size() > 100);                                                          // it moves about from day to day, it is not one fixed time
    }
    CHECK(daily::startOn(20000, 19800, LO, HI, SALT) == daily::startOn(20000, 19800, LO, HI, SALT));   // a pure function: same answer every time
    if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
    std::printf("daily ok\n");
    return 0;
}
