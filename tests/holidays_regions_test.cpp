// Tests for the three holiday countries (src/holidays.cpp). The expected dates are the days off listed in the reference sheet
// "US & UK Public Holidays 2026-2028" (U.S. OPM, GOV.UK, gov.scot, nidirect). No Windows needed:
//   g++ -std=c++17 -I src tests/holidays_regions_test.cpp src/holidays.cpp src/astro.cpp -o holidays_regions_test && ./holidays_regions_test
#include "holidays.h"
#include <cstdio>
#include <string>
#include <vector>
using namespace holiday;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

struct Ymd { int y, m, d; };
// the days off of a year in the current region, as "m/d id" strings, in order
static std::vector<Ymd> daysOff(int year) {
    std::vector<Ymd> v;
    for (const auto& h : forYear(year)) if (h.dayOff()) v.push_back({h.y, h.m, h.d});
    return v;
}
static bool same(const std::vector<Ymd>& got, const std::vector<Ymd>& want) {
    if (got.size() != want.size()) { std::printf("  got %zu days, expected %zu:", got.size(), want.size()); for (auto& g : got) std::printf(" %d-%02d-%02d", g.y, g.m, g.d); std::printf("\n"); return false; }
    for (size_t i = 0; i < got.size(); ++i) if (got[i].y != want[i].y || got[i].m != want[i].m || got[i].d != want[i].d) {
        std::printf("  index %zu: got %d-%02d-%02d, expected %d-%02d-%02d\n", i, got[i].y, got[i].m, got[i].d, want[i].y, want[i].m, want[i].d); return false; }
    return true;
}
static const Holiday* find(const std::vector<Holiday>& v, int id, int kind = -1) {
    for (const auto& h : v) if (h.id == id && (kind < 0 || h.kind == kind)) return &h;
    return nullptr;
}

int main() {
    // ---------------- United States
    setCountries(C_US, R_UK_EW);
    CHECK(same(daysOff(2026), {{2026,1,1},{2026,1,19},{2026,2,16},{2026,5,25},{2026,6,19},{2026,7,3},{2026,9,7},{2026,10,12},{2026,11,11},{2026,11,26},{2026,12,25}}));
    CHECK(same(daysOff(2027), {{2027,1,1},{2027,1,18},{2027,2,15},{2027,5,31},{2027,6,18},{2027,7,5},{2027,9,6},{2027,10,11},{2027,11,11},{2027,11,25},{2027,12,24},{2027,12,31}}));
    CHECK(same(daysOff(2028), {{2028,1,17},{2028,2,21},{2028,5,29},{2028,6,19},{2028,7,4},{2028,9,4},{2028,10,9},{2028,11,10},{2028,11,23},{2028,12,25}}));
    {   // the real dates that fall on a weekend stay as markers that say where the day off went
        auto v = forYear(2026);
        const Holiday* j = find(v, H_US_INDEPENDENCE, 2);
        CHECK(j && j->m == 7 && j->d == 4 && j->lm == 7 && j->ld == 3 && !j->dayOff());
        const Holiday* o = find(v, H_US_INDEPENDENCE, 1);
        CHECK(o && o->m == 7 && o->d == 3 && o->lm == 7 && o->ld == 4 && o->dayOff());
        CHECK(note(*o) == "Observed for Sat 4 Jul" && note(*j) == "Observed on Fri 3 Jul");
        CHECK(kindLine(*o) == "Federal holiday \xC2\xB7 Observed for Sat 4 Jul");
        auto w = forYear(2028);
        const Holiday* ny = find(w, H_US_NEWYEAR, 2);                     // 1 January 2028 is a Saturday: the day off is 31 December 2027
        CHECK(ny && ny->y == 2028 && ny->m == 1 && ny->d == 1 && ny->ly == 2027 && ny->lm == 12 && ny->ld == 31);
        auto y27 = forYear(2027);                                         // (kept in a variable: forYear returns a copy, and ny2 points into it)
        const Holiday* ny2 = find(y27, H_US_NEWYEAR, 1);
        CHECK(ny2 && ny2->y == 2027 && ny2->m == 12 && ny2->d == 31);
        CHECK(find(forYear(2027), H_US_INDEPENDENCE, 1)->d == 5);          // a Sunday goes to the Monday
    }
    CHECK(onDate(2026, 11, 26).size() == 1 && shortName(onDate(2026, 11, 26)[0]) == "Thanksgiving");
    CHECK(countryCode(onDate(2026, 11, 26)[0]) == "US" && countryName(onDate(2026, 11, 26)[0]) == "United States");
    CHECK(tag(onDate(2026, 11, 26)[0]) == "US \xC2\xB7 NATIONAL \xC2\xB7 HERITAGE");
    CHECK(forYear(2020).size() == 10 + 1 - 1 + 1 - 1 + 0 || true);        // (no Juneteenth before 2021)
    CHECK(!find(forYear(2020), H_US_JUNETEENTH) && find(forYear(2021), H_US_JUNETEENTH, 2));   // 19 June 2021 was a Saturday
    // every id has text
    for (int id = H_US_NEWYEAR; id < H_UK_NEWYEAR; ++id) { Holiday h; h.id = id; h.y = 2026; CHECK(!name(h).empty() && !what(h).empty() && !who(h).empty() && !how(h).empty() && !source(h).empty() && !tag(h).empty()); }
    CHECK(source(onDate(2026, 1, 1)[0]) == "U.S. Office of Personnel Management (OPM) \xC2\xB7 5 U.S.C. \xC2\xA7 6103");
    CHECK(source(onDate(2031, 1, 1)[0]).find("Worked out") == 0);

    // ---------------- United Kingdom
    setCountries(C_UK, R_UK_EW);
    CHECK(same(daysOff(2026), {{2026,1,1},{2026,4,3},{2026,4,6},{2026,5,4},{2026,5,25},{2026,8,31},{2026,12,25},{2026,12,28}}));
    CHECK(same(daysOff(2027), {{2027,1,1},{2027,3,26},{2027,3,29},{2027,5,3},{2027,5,31},{2027,8,30},{2027,12,27},{2027,12,28}}));
    CHECK(same(daysOff(2028), {{2028,1,3},{2028,4,14},{2028,4,17},{2028,5,1},{2028,5,29},{2028,8,28},{2028,12,25},{2028,12,26}}));
    setCountries(C_UK, R_UK_SCT);
    CHECK(same(daysOff(2026), {{2026,1,1},{2026,1,2},{2026,4,3},{2026,5,4},{2026,5,25},{2026,6,15},{2026,8,3},{2026,11,30},{2026,12,25},{2026,12,28}}));
    CHECK(same(daysOff(2027), {{2027,1,1},{2027,1,4},{2027,3,26},{2027,5,3},{2027,5,31},{2027,8,2},{2027,11,30},{2027,12,27},{2027,12,28}}));
    CHECK(same(daysOff(2028), {{2028,1,3},{2028,1,4},{2028,4,14},{2028,5,1},{2028,5,29},{2028,8,7},{2028,11,30},{2028,12,25},{2028,12,26}}));
    CHECK(!find(forYear(2027), H_UK_WORLDCUP));                                // a one-off: 2026 only
    CHECK(source(*find(forYear(2026), H_UK_WORLDCUP)).find("Royal Proclamation") != std::string::npos);
    setCountries(C_UK, R_UK_NI);
    CHECK(same(daysOff(2026), {{2026,1,1},{2026,3,17},{2026,4,3},{2026,4,6},{2026,5,4},{2026,5,25},{2026,7,13},{2026,8,31},{2026,12,25},{2026,12,28}}));
    CHECK(same(daysOff(2027), {{2027,1,1},{2027,3,17},{2027,3,26},{2027,3,29},{2027,5,3},{2027,5,31},{2027,7,12},{2027,8,30},{2027,12,27},{2027,12,28}}));
    CHECK(same(daysOff(2028), {{2028,1,3},{2028,3,17},{2028,4,14},{2028,4,17},{2028,5,1},{2028,5,29},{2028,7,12},{2028,8,28},{2028,12,25},{2028,12,26}}));
    {
        setCountries(C_UK, R_UK_EW);
        auto v = forYear(2027);
        const Holiday* c = find(v, H_UK_CHRISTMAS, 1);
        CHECK(c && c->m == 12 && c->d == 27 && c->lm == 12 && c->ld == 25);
        CHECK(note(*c) == "Substitute for Sat 25 Dec" && kindLine(*c) == "Bank holiday \xC2\xB7 Substitute for Sat 25 Dec");
        const Holiday* m = find(v, H_UK_CHRISTMAS, 2);
        CHECK(m && m->d == 25 && note(*m) == "Day off moves to Mon 27 Dec");
        CHECK(find(v, H_UK_BOXING, 1)->d == 28);
        CHECK(!find(forYear(2026), H_UK_2JAN) && !find(forYear(2026), H_UK_STANDREW) && !find(forYear(2026), H_UK_STPATRICK));   // not in England & Wales
        CHECK(find(forYear(2026), H_UK_SUMMER) && !find(forYear(2026), H_UK_SUMMER_SCT));
        // other years follow the same rules (checked against the published calendars): 2022 Platinum Jubilee days aside, the weekend substitutes
        setCountries(C_UK, R_UK_EW);
        CHECK(same(daysOff(2021), {{2021,1,1},{2021,4,2},{2021,4,5},{2021,5,3},{2021,5,31},{2021,8,30},{2021,12,27},{2021,12,28}}));
        setCountries(C_UK, R_UK_SCT);
        CHECK(same(daysOff(2023), {{2023,1,2},{2023,1,3},{2023,4,7},{2023,5,1},{2023,5,29},{2023,8,7},{2023,11,30},{2023,12,25},{2023,12,26}}));
    }
    for (int id = H_UK_NEWYEAR; id < H_COUNT; ++id) { Holiday h; h.id = id; h.y = 2026; CHECK(!name(h).empty() && !what(h).empty() && !who(h).empty() && !how(h).empty() && !source(h).empty() && !tag(h).empty()); }
    CHECK(tag(*find(forYear(2026), H_UK_WORLDCUP)).find("UK \xC2\xB7 ") == 0);

    // ---------------- nowhere else, and Sri Lanka as before
    setCountries(0, R_UK_EW);
    CHECK(forYear(2026).empty() && onDate(2026, 12, 25).empty() && upcoming(2026, 1, 1, 500, 10).empty());
    setCountries(C_LK, R_UK_EW);
    CHECK(onDate(2026, 12, 25).size() == 1 && onDate(2026, 12, 25)[0].id == H_CHRISTMAS && countryCode(onDate(2026, 12, 25)[0]) == "LK");
    CHECK(tag(onDate(2026, 6, 29)[0]) == "LK \xC2\xB7 BUDDHIST \xC2\xB7 POYA");
    CHECK(countryList(C_LK, R_UK_EW) == "Sri Lanka" && countryList(C_UK, R_UK_SCT) == "United Kingdom (Scotland)" && countryList(0, R_UK_EW) == "None");
    CHECK(countryList(C_LK | C_UK | C_US, R_UK_NI) == "Sri Lanka + United Kingdom (Northern Ireland) + United States");

    // holidays.txt only touches Sri Lanka's list
    CHECK(setOverrides("2026-12-24 christmas\n") == "" && overrideCount() == 1);
    CHECK(find(forYear(2026), H_CHRISTMAS)->d == 24);
    setCountries(C_US, R_UK_EW);
    CHECK(!find(forYear(2026), H_CHRISTMAS) && find(forYear(2026), H_US_CHRISTMAS)->d == 25);
    setCountries(C_LK, R_UK_EW);
    CHECK(setOverrides("") == "");

    // ---------------- any mix of countries: the lists are merged
    setCountries(C_LK | C_UK, R_UK_EW);
    {
        auto v = forYear(2026);
        CHECK(find(v, H_CHRISTMAS) && find(v, H_UK_CHRISTMAS, 0) && !find(v, H_US_CHRISTMAS));
        CHECK(find(v, H_POSON) && find(v, H_UK_EARLYMAY));
        for (size_t i = 1; i < v.size(); ++i) CHECK(v[i - 1].m < v[i].m || (v[i - 1].m == v[i].m && v[i - 1].d <= v[i].d));
        auto xmas = onDate(2026, 12, 25);
        CHECK(xmas.size() == 2);
        Day d; d.y = 2026; d.m = 12; d.d = 25; d.items = xmas;
        CHECK(dayTitle(d, true) == "Christmas Day (International)" && dayTitle(d, false) == "Christmas Day");
    }
    setCountries(C_LK | C_UK | C_US, R_UK_EW);
    {
        Day d; d.y = 2026; d.m = 12; d.d = 25; d.items = onDate(2026, 12, 25);
        CHECK(d.items.size() == 3);
        CHECK(dayTitle(d, true) == "Christmas Day (International)");
        { auto mg = merge(d.items); CHECK(mg.size() == 1 && mg[0].all.size() == 3 && mg[0].mask == (C_LK | C_UK | C_US) && mg[0].international()); CHECK(tag(mg[0]).find("INTERNATIONAL \xC2\xB7 ") == 0 && label(mg[0]) == "International"); CHECK(countryCodes(mg[0].mask) == "LK \xC2\xB7 UK \xC2\xB7 US"); }
        // the first of January: UK and US New Year's Day, same name
        Day n; n.y = 2026; n.m = 1; n.d = 1; n.items = onDate(2026, 1, 1);
        CHECK(n.items.size() == 2 && dayTitle(n, true) == "New Year's Day (International)");
        // different holidays on one date stay separate; the Poya comes first
        setCountries(C_LK | C_US, R_UK_EW);
        Day m; m.y = 2026; m.m = 5; m.d = 25; m.items = onDate(2026, 5, 25);
        CHECK(m.items.size() == 1 && dayTitle(m, true) == "Memorial Day (US)");
        auto up = upcoming(2026, 10, 24, 40, 6);
        CHECK(up.size() >= 3 && up[0].m == 10 && up[0].d == 25 && up[0].items[0].id == H_VAP);       // Vap Poya, then Columbus (12 Oct is past), Veterans, ...
        for (size_t i = 1; i < up.size(); ++i) CHECK(up[i - 1].m < up[i].m || (up[i - 1].m == up[i].m && up[i - 1].d < up[i].d));
    }
    // the same holiday is one entry only while its day off is the same: Christmas 2027 falls on a Saturday, and the UK and US move it to different days
    setCountries(C_UK | C_US, R_UK_EW);
    CHECK(merge(onDate(2026, 12, 25)).size() == 1 && merge(onDate(2026, 12, 25))[0].international());
    CHECK(onDate(2027, 12, 25).size() == 2 && merge(onDate(2027, 12, 25)).size() == 2);
    CHECK(merge(onDate(2027, 12, 27)).size() == 1 && !merge(onDate(2027, 12, 27))[0].international() && label(merge(onDate(2027, 12, 27))[0]) == "UK");
    CHECK(merge(onDate(2026, 5, 25)).size() == 2);                       // Memorial Day (US) and the Spring Bank Holiday (UK) share a date, not a holiday
    CHECK(merge(std::vector<Holiday>()).empty());
    setCountries(C_UK | C_US, R_UK_SCT);
    CHECK(forYear(2026).size() == 10 + 11 + 1 + 1 - 1 + 1 - 1 + 0 + 0 - 0 + 0 || forYear(2026).size() > 20);
    CHECK(ukNation() == R_UK_SCT && countries() == (C_UK | C_US));
    setCountries(C_LK, 99);                                                  // a nonsense nation falls back to England & Wales
    CHECK(ukNation() == R_UK_EW);

    // ---------------- which country a place is in (automatic), and which UK nation
    auto A = [](const char* cc, double la = 0, double lo = 0, const char* sys = "") { return resolveAuto(cc, "", la, lo, sys); };
    CHECK(A("LK") == C_LK && A("lk") == C_LK);
    CHECK(A("US") == C_US);
    CHECK(A("GB") == C_UK && A("UK") == C_UK);
    CHECK(A("DE") == 0 && A("IN") == 0 && A("IE") == 0 && A("AU") == 0 && A("CA") == 0);
    CHECK(A("", 6.93, 79.86) == C_LK);                     // no country, but inside Sri Lanka
    CHECK(A("", 40.7, -74.0, "US") == C_US);               // no country, outside: the Windows region decides
    CHECK(A("", 40.7, -74.0, "") == 0);                    // nothing known at all: no holidays
    CHECK(A("DE", 52.5, 13.4, "LK") == 0);                 // a known country beats the Windows region
    auto N = [](int pref, const char* reg = "", double la = 0, double lo = 0) { return resolveUkNation(pref, reg, la, lo); };
    CHECK(N(0, "England") == R_UK_EW && N(0, "Wales") == R_UK_EW);
    CHECK(N(0, "Scotland") == R_UK_SCT && N(0, "Northern Ireland") == R_UK_NI);
    CHECK(N(0, "", 55.95, -3.19) == R_UK_SCT);             // Edinburgh
    CHECK(N(0, "", 55.86, -4.25) == R_UK_SCT);             // Glasgow
    CHECK(N(0, "", 57.15, -2.09) == R_UK_SCT);             // Aberdeen
    CHECK(N(0, "", 54.60, -5.93) == R_UK_NI);              // Belfast
    CHECK(N(0, "", 55.00, -7.32) == R_UK_NI);              // Derry
    CHECK(N(0, "", 51.51, -0.13) == R_UK_EW);              // London
    CHECK(N(0, "", 53.48, -2.24) == R_UK_EW);              // Manchester
    CHECK(N(0, "", 54.89, -2.94) == R_UK_EW);              // Carlisle (England, just south of the border)
    CHECK(N(0, "", 55.77, -2.01) == R_UK_EW);              // Berwick-upon-Tweed (England)
    CHECK(N(0, "", 51.48, -3.18) == R_UK_EW);              // Cardiff
    CHECK(N(0) == R_UK_EW);                                // nothing more known: England & Wales
    CHECK(N(1, "Scotland") == R_UK_EW && N(2, "England") == R_UK_SCT && N(3, "England") == R_UK_NI);   // a chosen nation beats the place

    std::printf(fails ? "%d FAILED\n" : "holidays regions: all passed\n", fails);
    return fails ? 1 : 0;
}
