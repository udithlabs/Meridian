// g++ -std=c++17 -I src tests/dates_test.cpp src/astro.cpp -o dates_test && ./dates_test
// The date checks every typed date goes through (custom dates, the countdown date, theme files).
#include "util.h"
#include "today.h"
#include <cstdio>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
static int ev(const char* s, int& y, int& m, int& d) { return util::parseEventDatePrefix(s, y, m, d); }
int main() {
    int y = -1, m = -1, d = -1;
    // real dates
    CHECK(util::validDate(2026, 11, 7)); CHECK(util::validDate(2028, 2, 29)); CHECK(util::validDate(2000, 2, 29)); CHECK(util::validDate(1900, 1, 1));
    // not real
    CHECK(!util::validDate(2027, 2, 29)); CHECK(!util::validDate(1900, 2, 29)); CHECK(!util::validDate(2027, 2, 30)); CHECK(!util::validDate(2027, 4, 31));
    CHECK(!util::validDate(2027, 13, 1)); CHECK(!util::validDate(2027, 0, 1)); CHECK(!util::validDate(2027, 1, 0)); CHECK(!util::validDate(1899, 1, 1)); CHECK(!util::validDate(2201, 1, 1));
    CHECK(util::validDayMonth(2, 29)); CHECK(!util::validDayMonth(2, 30)); CHECK(!util::validDayMonth(4, 31)); CHECK(util::validDayMonth(12, 31)); CHECK(!util::validDayMonth(0, 5));
    CHECK(util::validTimeOfDay(0, 0)); CHECK(util::validTimeOfDay(23, 59)); CHECK(!util::validTimeOfDay(24, 0)); CHECK(!util::validTimeOfDay(12, 60)); CHECK(!util::validTimeOfDay(-1, 0)); CHECK(!util::validTimeOfDay(99, 99));

    // countdown date: YYYY-MM-DD and nothing else
    CHECK(util::parseDateStrict("2027-01-15", y, m, d) && y == 2027 && m == 1 && d == 15);
    CHECK(util::parseDateStrict("  2027-01-15  ", y, m, d));
    CHECK(!util::parseDateStrict("2027-02-31", y, m, d)); CHECK(!util::parseDateStrict("2027-01-15x", y, m, d)); CHECK(!util::parseDateStrict("2027-01", y, m, d));
    CHECK(!util::parseDateStrict("", y, m, d)); CHECK(!util::parseDateStrict("hello", y, m, d)); CHECK(!util::parseDateStrict("2027-1-15 10:00", y, m, d));
    // with a time
    int hh, mi;
    CHECK(util::parseDateTimeStrict("2026-12-25 18:30", y, m, d, hh, mi) && hh == 18 && mi == 30);
    CHECK(util::parseDateTimeStrict("2026-12-25", y, m, d, hh, mi) && hh == 0 && mi == 0);
    CHECK(!util::parseDateTimeStrict("2026-12-25 24:00", y, m, d, hh, mi)); CHECK(!util::parseDateTimeStrict("2026-12-25 99:99", y, m, d, hh, mi));
    CHECK(!util::parseDateTimeStrict("2026-12-25 18:30 extra", y, m, d, hh, mi)); CHECK(!util::parseDateTimeStrict("2026-02-30 10:00", y, m, d, hh, mi));

    // "Your dates": the start of what was typed
    int n;
    n = ev("12-25 Christmas", y, m, d); CHECK(n == 5 && y == 0 && m == 12 && d == 25);
    n = ev("2027-03-14 Exam", y, m, d); CHECK(n == 10 && y == 2027 && m == 3 && d == 14);
    n = ev("  2027-03-14 Exam", y, m, d); CHECK(n == 12 && y == 2027);
    n = ev("02-29 Leap day", y, m, d); CHECK(n == 5 && y == 0 && m == 2 && d == 29);
    n = ev("2028-02-29 Leap day", y, m, d); CHECK(n > 0 && y == 2028);
    n = ev("12-25", y, m, d); CHECK(n == 5);                                        // no name: the caller refuses it
    CHECK(ev("02-30 Nope", y, m, d) == 0); CHECK(ev("2027-02-29 Nope", y, m, d) == 0); CHECK(ev("13-01 Nope", y, m, d) == 0); CHECK(ev("04-31 Nope", y, m, d) == 0);
    CHECK(ev("12-25-2027 Nope", y, m, d) == 0); CHECK(ev("Christmas 12-25", y, m, d) == 0); CHECK(ev("", y, m, d) == 0); CHECK(ev("-", y, m, d) == 0);
    CHECK(ev("12-25Christmas", y, m, d) == 0); CHECK(ev("99-99 x", y, m, d) == 0); CHECK(ev("2027-13-01 x", y, m, d) == 0);

    // a yearly 29 February is on 28 February in a year with no 29th
    today::CustomEvent leap; leap.y = 0; leap.m = 2; leap.d = 29;
    CHECK(leap.on(2028, 2, 29)); CHECK(!leap.on(2028, 2, 28));                      // leap year: on the 29th only
    CHECK(leap.on(2027, 2, 28)); CHECK(!leap.on(2027, 3, 1)); CHECK(!leap.on(2027, 2, 27));
    CHECK(leap.on(2100, 2, 28));                                                    // 2100 is not a leap year
    today::CustomEvent once; once.y = 2028; once.m = 2; once.d = 29;
    CHECK(once.on(2028, 2, 29)); CHECK(!once.on(2032, 2, 29)); CHECK(!once.on(2027, 2, 28));
    today::CustomEvent xmas; xmas.y = 0; xmas.m = 12; xmas.d = 25;
    CHECK(xmas.on(2026, 12, 25)); CHECK(xmas.on(2031, 12, 25)); CHECK(!xmas.on(2026, 12, 24));
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
