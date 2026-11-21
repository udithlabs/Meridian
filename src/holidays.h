// Public holidays of three places: Sri Lanka (the official 2026 and 2027 lists, estimates for other years, dates you type into holidays.txt),
// the United Kingdom (England & Wales, Scotland, Northern Ireland: bank holidays) and the United States (federal holidays). Any mix of the three can
// be switched on (setCountries); resolveAuto() works out the one that matches where you are. Each comes with plain-language descriptions. Portable.
#pragma once
#include <string>
#include <vector>

namespace holiday {

enum Group { G_BUDDHIST = 0, G_HINDU, G_MUSLIM, G_CHRISTIAN, G_NATIONAL, G_NEWYEAR, G_LABOUR, G_BANK };

// The UK has a list per nation.
enum Region { R_NONE = 0, R_LK, R_UK_EW, R_UK_SCT, R_UK_NI, R_US };

// The first twelve ids are the Poya days in calendar order: id == Gregorian month - 1 of a normal year.
enum Id { H_DURUTHU = 0, H_NAVAM, H_MEDIN, H_BAK, H_VESAK, H_POSON, H_ESALA, H_NIKINI, H_BINARA, H_VAP, H_IL, H_UNDUVAP,
          H_VESAK_AFTER, H_PONGAL, H_INDEPENDENCE, H_SIVARATHRI, H_EID_FITR, H_GOOD_FRIDAY, H_NY_EVE, H_NY_DAY, H_MAY_DAY,
          H_EID_ADHA, H_MILAD, H_DEEPAVALI, H_CHRISTMAS,
          H_LK_COUNT,                       // the Sri Lankan ids end here (holidays.txt knows only these)
          H_US_NEWYEAR = H_LK_COUNT, H_US_MLK, H_US_PRESIDENTS, H_US_MEMORIAL, H_US_JUNETEENTH, H_US_INDEPENDENCE, H_US_LABOR,
          H_US_COLUMBUS, H_US_VETERANS, H_US_THANKSGIVING, H_US_CHRISTMAS,
          H_UK_NEWYEAR, H_UK_2JAN, H_UK_STPATRICK, H_UK_GOODFRIDAY, H_UK_EASTERMON, H_UK_EARLYMAY, H_UK_SPRING, H_UK_WORLDCUP,
          H_UK_BOYNE, H_UK_SUMMER_SCT, H_UK_SUMMER, H_UK_STANDREW, H_UK_CHRISTMAS, H_UK_BOXING, H_COUNT,
          H_US_COUNT = H_UK_NEWYEAR - H_US_NEWYEAR, H_UK_COUNT = H_COUNT - H_UK_NEWYEAR };

struct Holiday {
    int y = 0, m = 0, d = 0;                // the date, where the holiday is
    int id = 0;
    bool adhi = false;                      // the extra Poya of a leap lunar month ("Adhi Poson")
    bool estimated = false;                 // not (yet) from a government list
    bool custom = false;                    // the date comes from the user's holidays.txt
    // UK and US: a holiday on a Saturday or Sunday moves to a weekday. The day off is the substitute (kind 1); the real date stays as a
    // marker (kind 2) that says where the day off went. ly/lm/ld is the other of the two dates.
    int kind = 0;                           // 0 an ordinary day, 1 the substitute / observed day off, 2 the real date, which falls on a weekend
    int ly = 0, lm = 0, ld = 0;
    bool poya() const { return id <= H_UNDUVAP; }
    bool dayOff() const { return kind != 2; }
};

struct Day { int y = 0, m = 0, d = 0; std::vector<Holiday> items; };

// ---- whose holidays
// Any mix of the three countries can be on at once: the lists are merged. The UK has one list per nation (R_UK_EW, R_UK_SCT, R_UK_NI).
enum Country { C_LK = 1, C_UK = 2, C_US = 4 };
void setCountries(int mask, int ukNation);                           // default: Sri Lanka only, England & Wales
int countries();
int ukNation();
std::string countryList(int mask, int ukNation);                     // "Sri Lanka + United Kingdom (Scotland)", or "None"
// Which country a place is in, as one Country bit (0: a country without a list). country is the place's ISO code ("LK", "GB", "US"; empty when
// not known), lat/lon its position (used when there is no country: Sri Lanka is recognised by it), systemCountry the Windows region's country.
int resolveAuto(const std::string& country, const std::string& regionText, double lat, double lon, const std::string& systemCountry);
// The UK nation. pref: 0 from the place (its region text, else its position), 1 England & Wales, 2 Scotland, 3 Northern Ireland.
int resolveUkNation(int pref, const std::string& regionText, double lat, double lon);

std::vector<Holiday> forYear(int year);                              // of every country that is on, sorted by date
std::vector<Holiday> onDate(int y, int m, int d);
std::vector<Day> upcoming(int y, int m, int d, int maxDays, int maxCount);   // from this date (inclusive), grouped by date
std::vector<Holiday> estimateYear(int year);                         // the estimate used for years without an official list (also used by tests)
bool officialYear(int year);                                         // true when the year comes from a gazetted list

// Official dates typed into holidays.txt when a new list is published (the built-in lists end with 2027; later years are estimates).
// Each line replaces the date Meridian printed or estimated for that one holiday in that year; the others keep theirs. Format, one per line:
//     2028-01-12  duruthu         (year-month-day in Sri Lanka, then the name; add  adhi  after it for an extra leap-month Poya)
// setOverrides() reads the whole text. It returns "" when every line was understood, otherwise a short note about the first ones it could not
// read (those lines are skipped, the rest is used). An empty text clears all of it. Names: see exampleFile().
std::string setOverrides(const std::string& fileText);
int overrideCount();
int overrideVersion();
std::string exampleFile();                   // the text a new holidays.txt starts with

// The same holiday in more than one country (same name, same kind of day, same day off) is one holiday for everyone: "International".
struct Merged {
    std::vector<Holiday> all;                // each country's version; the first leads (Sri Lanka, then the UK, then the US)
    int mask = 0;                            // the countries that share it (Country bits)
    bool international() const { return (mask & (mask - 1)) != 0; }
    const Holiday& lead() const { return all[0]; }
};
std::vector<Merged> merge(const std::vector<Holiday>& items);        // keeps the order of the first of each
int countryBit(const Holiday& h);                                    // C_LK, C_UK or C_US
std::string countryCodes(int mask);                                  // "LK \xC2\xB7 UK \xC2\xB7 US"
std::string label(const Merged& m);                                  // "International", or the country's code ("US")
std::string tag(const Merged& m);                                    // "INTERNATIONAL \xC2\xB7 CHRISTIAN \xC2\xB7 BANK", or tag(lead)
std::string dayTitle(const Day& d, bool codes);                      // "Christmas Day (International)  +  Memorial Day (US)"; the labels only if codes
std::string countryCode(const Holiday& h);   // "LK", "UK" or "US": which country's list the holiday belongs to
std::string countryName(const Holiday& h);   // "Sri Lanka", "United Kingdom", "United States"
std::string name(const Holiday& h);          // "Vesak Full Moon Poya Day"
std::string shortName(const Holiday& h);     // "Vesak Poya"
std::string tag(const Holiday& h);           // "LK \xC2\xB7 BUDDHIST \xC2\xB7 POYA", "UK \xC2\xB7 CHRISTIAN \xC2\xB7 BANK": the country code leads
int group(const Holiday& h);
bool mercantile(const Holiday& h);           // a mercantile holiday as well as public + bank
std::string categories(const Holiday& h);    // "Public \xC2\xB7 Bank \xC2\xB7 Mercantile"; US "Federal \xC2\xB7 Bank"; UK "Bank"
std::string kindLine(const Holiday& h);      // "Public and bank holiday", "Federal holiday", "Bank holiday" (plus the substitute note, if any)
std::string note(const Holiday& h);          // "" or "Substitute for Sat 25 Dec", "Observed Fri 3 Jul": where a weekend holiday's day off is
std::string what(const Holiday& h);          // what the day is
std::string who(const Holiday& h);           // who gets the day off
std::string how(const Holiday& h);           // how it is observed
std::string source(const Holiday& h);        // where the date comes from

} // namespace holiday
