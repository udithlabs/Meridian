// Tests for the holidays.txt overrides (src/holidays.cpp). No Windows needed:
//   g++ -std=c++17 -I src tests/holidays_test.cpp src/holidays.cpp src/astro.cpp -o holidays_test && ./holidays_test
#include "holidays.h"
#include <cstdio>
#include <string>
using namespace holiday;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

static const Holiday* find(const std::vector<Holiday>& v, int id, bool adhi = false) {
    for (const auto& h : v) if (h.id == id && h.adhi == adhi) return &h;
    return nullptr;
}

int main() {
    // nothing typed: the built-in lists stand, and nothing is marked custom
    CHECK(setOverrides("") == "");
    CHECK(overrideCount() == 0);
    auto base28 = forYear(2028);
    auto base26 = forYear(2026);
    for (const auto& h : base28) CHECK(!h.custom);
    CHECK(find(base26, H_POSON) && find(base26, H_POSON)->m == 6 && find(base26, H_POSON)->d == 29);

    // the example file the app writes must read back as "no lines, no problems"
    CHECK(setOverrides(exampleFile()) == "");
    CHECK(overrideCount() == 0);

    // a typed date replaces that holiday and nothing else
    CHECK(setOverrides("2028-01-12 duruthu\n") == "");
    CHECK(overrideCount() == 1);
    auto v = forYear(2028);
    const Holiday* d = find(v, H_DURUTHU);
    CHECK(d && d->y == 2028 && d->m == 1 && d->d == 12 && d->custom && !d->estimated);
    CHECK(source(*d) == "Your holidays.txt");
    CHECK(v.size() == base28.size());
    const Holiday* other = find(v, H_CHRISTMAS);
    CHECK(other && !other->custom);
    CHECK(forYear(2027).size() == forYear(2027).size());

    // comments, blank lines, capitals, tabs, underscores, slashes, a Notepad BOM and Windows line endings
    CHECK(setOverrides("\xEF\xBB\xBF# a comment\r\n\r\n2028/4/12\tBAK  # trailing note\r\n2028-12-23 Unduvap\r\n2028-05-01 may_day\r\n") == "");
    CHECK(overrideCount() == 3);
    v = forYear(2028);
    CHECK(find(v, H_BAK)->m == 4 && find(v, H_BAK)->d == 12);
    CHECK(find(v, H_UNDUVAP)->m == 12 && find(v, H_UNDUVAP)->d == 23);
    CHECK(find(v, H_MAY_DAY)->custom);

    // it can correct a built-in year too, and years do not leak into each other
    CHECK(setOverrides("2027-06-17 poson\n") == "");
    CHECK(find(forYear(2027), H_POSON)->d == 17 && find(forYear(2027), H_POSON)->custom);
    CHECK(find(forYear(2026), H_POSON)->d == 29 && !find(forYear(2026), H_POSON)->custom);
    CHECK(find(forYear(2028), H_POSON) && !find(forYear(2028), H_POSON)->custom);

    // adhi: matches the extra Poya of that name, and adds it when the year has none
    CHECK(setOverrides("2028-06-07 poson adhi\n") == "");
    v = forYear(2028);
    const Holiday* a = find(v, H_POSON, true);
    CHECK(a && a->adhi && a->m == 6 && a->d == 7 && a->custom);
    CHECK(find(v, H_POSON, false) && !find(v, H_POSON, false)->custom);   // the ordinary Poson is untouched
    CHECK(setOverrides("2026-05-30 poson adhi\n") == "");
    CHECK(forYear(2026).size() == base26.size());                        // replaced, not added twice

    // moving Vesak moves the day after with it, unless that day is given too
    CHECK(setOverrides("2028-05-09 vesak\n") == "");
    v = forYear(2028);
    CHECK(find(v, H_VESAK)->d == 9 && find(v, H_VESAK_AFTER) && find(v, H_VESAK_AFTER)->m == 5 && find(v, H_VESAK_AFTER)->d == 10);
    CHECK(setOverrides("2028-05-09 vesak\n2028-05-11 vesak-after\n") == "");
    CHECK(find(forYear(2028), H_VESAK_AFTER)->d == 11);

    // lists stay sorted by date after an override moves something
    CHECK(setOverrides("2028-12-30 duruthu\n2028-01-02 christmas\n") == "");
    v = forYear(2028);
    for (size_t i = 1; i < v.size(); ++i) CHECK(v[i - 1].m < v[i].m || (v[i - 1].m == v[i].m && v[i - 1].d <= v[i].d));

    // bad lines are skipped, the good ones used, and the first problems are described
    std::string note = setOverrides("2028-02-30 bak\n2028-01-12 duruthu\nhello\n2028-03-01 nonsense\n2028-04-01 bak adhix\n2028-04-02 christmas adhi\n");
    CHECK(overrideCount() == 1);
    CHECK(note.find("line 1") != std::string::npos);
    CHECK(note.find("line 3") != std::string::npos);
    CHECK(note.find("2 more") == std::string::npos || note.find("and ") != std::string::npos);
    CHECK(note.find("and 3 more") != std::string::npos);
    CHECK(find(forYear(2028), H_DURUTHU)->d == 12);
    // dates that are not real, or out of range
    CHECK(setOverrides("2100-12-31 christmas\n") == "" && overrideCount() == 1);
    CHECK(setOverrides("2101-01-01 christmas\n") != "" && overrideCount() == 0);
    CHECK(setOverrides("1989-12-31 christmas\n") != "" && overrideCount() == 0);
    CHECK(setOverrides("2028-13-01 christmas\n") != "");
    CHECK(setOverrides("2028-1-1-1 christmas\n") != "");
    CHECK(setOverrides("20280-01-01 christmas\n") != "");
    CHECK(setOverrides("2028-02-29 christmas\n") == "");                 // 2028 is a leap year
    CHECK(setOverrides("2027-02-29 christmas\n") != "");
    CHECK(setOverrides("x") != "" && setOverrides("2028-01-01\n") != "");
    CHECK(setOverrides(std::string(100000, 'x')) != "");                  // a large junk file is just one bad line

    // clearing the text puts everything back
    CHECK(setOverrides("") == "");
    CHECK(overrideCount() == 0);
    CHECK(forYear(2028).size() == base28.size());
    CHECK(find(forYear(2028), H_DURUTHU)->d == find(base28, H_DURUTHU)->d);

    std::printf(fails ? "%d FAILED\n" : "holidays: all passed\n", fails);
    return fails ? 1 : 0;
}
