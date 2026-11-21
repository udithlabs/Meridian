// Meridian: a once-a-day schedule. Pure arithmetic, no drawing (the test is tests/daily_test.cpp).
//
// One moment per local day, worked out from the date alone. That makes it the same on every run and after every restart (the screen saver starts a new
// copy of the program each time), so nothing has to be saved anywhere to keep it to once a day. `lo`..`hi` is the stretch of the local day, in hours
// (0 to 24), the moment falls in; the day is the one the clock shows (tzOff seconds from UTC).
#pragma once
#include <cmath>

namespace daily {

inline double hash01(long long day, int salt) {                       // a steady, well-mixed number in [0, 1) for a day
    unsigned long long x = (unsigned long long)day * 0x9E3779B97F4A7C15ull + (unsigned long long)(unsigned)salt * 0xBF58476D1CE4E5B9ull;
    x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ull; x ^= x >> 27; x *= 0x94D049BB133111EBull; x ^= x >> 31;
    return (double)(x >> 11) * (1.0 / 9007199254740992.0);
}
inline long long dayOf(double t, int tzOff) { return (long long)std::floor((t + tzOff) / 86400.0); }      // the local day number (days since 1970-01-01 there)
inline double startOn(long long day, int tzOff, double lo, double hi, int salt) {                         // its moment on that day, in unix seconds
    return (double)day * 86400.0 - tzOff + (lo + (hi - lo) * hash01(day, salt)) * 3600.0;
}
// is t inside today's moment (it lasts `dur` seconds)? gives the start
inline bool activeAt(double t, int tzOff, double dur, double lo, double hi, int salt, double& start) {
    start = startOn(dayOf(t, tzOff), tzOff, lo, hi, salt);
    return t >= start && t <= start + dur;
}
// the first moment that starts after t (today's if it is still to come, else tomorrow's)
inline double nextStart(double t, int tzOff, double lo, double hi, int salt) {
    const long long d = dayOf(t, tzOff);
    const double s = startOn(d, tzOff, lo, hi, salt);
    return s > t ? s : startOn(d + 1, tzOff, lo, hi, salt);
}

// Weekly alternating meteor direction on a 6-week loop (Week 7 resets cleanly back to Week 1):
//   Week 1 (cycle 0): right-to-left (-1), 22 deg     Week 4 (cycle 3): left-to-right (+1), 30 deg
//   Week 2 (cycle 1): left-to-right (+1), 26 deg     Week 5 (cycle 4): right-to-left (-1), 25 deg
//   Week 3 (cycle 2): right-to-left (-1), 22 deg     Week 6 (cycle 5): left-to-right (+1), 21 deg
//   Week 7 (cycle 6 -> 0): resets to Week 1!
inline int meteorWeekCycle(long long day) {
    constexpr long long kEpochDay = 20454;                            // 2026-01-01 = Day 0 of Week 1
    const long long diff = day - kEpochDay;
    const long long week = diff >= 0 ? (diff / 7) : ((diff - 6) / 7);
    return (int)(((week % 6) + 6) % 6);
}
inline void meteorWeekPattern(long long day, float& sideOut, float& baseAngleDegOut) {
    static constexpr float kSide[6]  = {-1.f, +1.f, -1.f, +1.f, -1.f, +1.f};
    static constexpr float kAngle[6] = {22.f, 26.f, 22.f, 30.f, 25.f, 21.f};
    const int cyc = meteorWeekCycle(day);
    sideOut = kSide[cyc];
    baseAngleDegOut = kAngle[cyc];
}

} // namespace daily
