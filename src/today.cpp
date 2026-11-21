#include "today.h"
#include "holidays.h"
#include <algorithm>
#include <cmath>

using namespace astro;
using util::fmt;
using util::LocalTime;

namespace today {

namespace {

struct Observance { int m, d; const char* title; bool sl; };
const Observance OBS[] = {
    {1,1,"New Year's Day",false}, {1,4,"World Braille Day",false}, {1,24,"International Day of Education",false},
    {1,27,"International Holocaust Remembrance Day",false}, {2,2,"World Wetlands Day",false}, {2,4,"World Cancer Day",false},
    {2,4,"Independence Day",true}, {2,11,"International Day of Women and Girls in Science",false},
    {2,13,"World Radio Day",false}, {2,14,"Valentine's Day",false}, {2,20,"World Day of Social Justice",false},
    {2,21,"International Mother Language Day",false}, {3,3,"World Wildlife Day",false},
    {3,8,"International Women's Day",false}, {3,14,"Pi Day",false}, {3,20,"International Day of Happiness",false},
    {3,21,"World Poetry Day",false}, {3,22,"World Water Day",false}, {3,23,"World Meteorological Day",false},
    {3,24,"World Tuberculosis Day",false}, {4,1,"April Fools' Day",false}, {4,7,"World Health Day",false},
    {4,12,"International Day of Human Space Flight",false}, {4,13,"Sinhala and Tamil New Year's Eve",true},
    {4,14,"Sinhala and Tamil New Year",true}, {4,22,"Earth Day",false}, {4,23,"World Book Day",false},
    {5,1,"Labour Day",false}, {5,3,"World Press Freedom Day",false}, {5,4,"Star Wars Day",false},
    {5,12,"International Nurses Day",false}, {5,15,"International Day of Families",false},
    {5,17,"World Telecommunication and Information Society Day",false}, {5,22,"International Day for Biological Diversity",false},
    {5,31,"World No Tobacco Day",false}, {6,1,"Global Day of Parents",false}, {6,5,"World Environment Day",false},
    {6,8,"World Oceans Day",false}, {6,14,"World Blood Donor Day",false}, {6,21,"International Day of Yoga",false},
    {6,30,"International Asteroid Day",false}, {7,11,"World Population Day",false},
    {7,18,"Nelson Mandela International Day",false}, {7,30,"International Day of Friendship",false},
    {8,12,"International Youth Day",false}, {8,19,"World Humanitarian Day",false}, {9,5,"International Day of Charity",false},
    {9,8,"International Literacy Day",false}, {9,15,"International Day of Democracy",false},
    {9,21,"International Day of Peace",false}, {9,27,"World Tourism Day",false},
    {10,1,"International Day of Older Persons",false}, {10,2,"International Day of Non-Violence",false},
    {10,4,"World Animal Day",false}, {10,5,"World Teachers' Day",false}, {10,9,"World Post Day",false},
    {10,10,"World Mental Health Day",false}, {10,11,"International Day of the Girl Child",false},
    {10,13,"International Day for Disaster Risk Reduction",false}, {10,15,"Global Handwashing Day",false},
    {10,16,"World Food Day",false}, {10,17,"International Day for the Eradication of Poverty",false},
    {10,18,"World Menopause Day",false}, {10,24,"United Nations Day",false}, {10,31,"Halloween",false},
    {11,14,"World Diabetes Day",false}, {11,16,"International Day for Tolerance",false}, {11,19,"International Men's Day",false},
    {11,20,"Universal Children's Day",false}, {11,25,"International Day for the Elimination of Violence against Women",false},
    {12,1,"World AIDS Day",false}, {12,3,"International Day of Persons with Disabilities",false},
    {12,5,"International Volunteer Day",false}, {12,10,"Human Rights Day",false}, {12,18,"International Migrants Day",false},
    {12,25,"Christmas Day",false}, {12,31,"New Year's Eve",false},
};

// One-off events on one exact date (they do not repeat): the day itself, with a line of detail.
struct OneOff { int y, m, d; const char* title; const char* detail; };
const OneOff ONEOFF[] = {
    {2026, 11, 19, "GTA VI is released", "Grand Theft Auto VI \xC2\xB7 PS5 and Xbox Series X|S"},
};

struct HistoryItem { int m, d, year; const char* text; };
const HistoryItem HIST[] = {
    {1,3,2019,"China's Chang'e 4 makes the first soft landing on the far side of the Moon."},
    {1,4,1643,"Isaac Newton is born (Gregorian calendar)."},
    {1,7,1610,"Galileo first sees three of Jupiter's moons."},
    {1,14,2005,"The Huygens probe lands on Titan."},
    {1,15,2001,"Wikipedia goes online."},
    {1,24,1986,"Voyager 2 makes the first flyby of Uranus."},
    {1,28,1986,"Space Shuttle Challenger is lost 73 seconds after launch."},
    {2,4,1948,"Ceylon becomes independent from British rule."},
    {2,14,1990,"Voyager 1 takes the Pale Blue Dot photograph of Earth."},
    {2,18,1930,"Clyde Tombaugh discovers Pluto on photographic plates."},
    {2,18,2021,"NASA's Perseverance rover lands on Mars."},
    {3,13,1781,"William Herschel discovers Uranus."},
    {3,14,1879,"Albert Einstein is born in Ulm."},
    {3,14,2018,"Stephen Hawking dies at 76."},
    {4,12,1961,"Yuri Gagarin becomes the first human in space."},
    {4,12,1981,"Space Shuttle Columbia makes the first shuttle launch."},
    {4,24,1990,"Space Shuttle Discovery launches the Hubble Space Telescope."},
    {4,25,1953,"The double-helix structure of DNA is published in Nature."},
    {5,5,1961,"Alan Shepard becomes the first American in space."},
    {5,22,1972,"Ceylon becomes the Republic of Sri Lanka."},
    {5,25,1961,"Kennedy sets the goal of landing a person on the Moon."},
    {5,29,1919,"A solar eclipse confirms Einstein's prediction that gravity bends starlight."},
    {6,16,1963,"Valentina Tereshkova becomes the first woman in space."},
    {6,23,1912,"Alan Turing is born in London."},
    {7,12,2022,"The James Webb Space Telescope releases its first full-colour images."},
    {7,14,2015,"New Horizons flies past Pluto."},
    {7,16,1969,"Apollo 11 launches toward the Moon."},
    {7,20,1969,"Neil Armstrong and Buzz Aldrin land on the Moon."},
    {7,25,1978,"Louise Brown, the first baby born through IVF, is born."},
    {8,6,2012,"The Curiosity rover lands in Gale Crater on Mars."},
    {8,20,1977,"Voyager 2 launches."},
    {8,23,2023,"India's Chandrayaan-3 lands near the Moon's south pole."},
    {8,25,1989,"Voyager 2 flies past Neptune."},
    {9,5,1977,"Voyager 1 launches."},
    {9,8,2016,"NASA's OSIRIS-REx launches to the asteroid Bennu."},
    {9,12,1962,"Kennedy gives his \"We choose to go to the Moon\" speech."},
    {9,28,1928,"Alexander Fleming notices the mould that leads to penicillin."},
    {10,4,1957,"Sputnik 1, the first artificial satellite, is launched."},
    {10,6,1995,"The first exoplanet around a Sun-like star, 51 Pegasi b, is announced."},
    {10,14,1947,"Chuck Yeager breaks the sound barrier."},
    {10,15,1997,"The Cassini-Huygens probe launches toward Saturn."},
    {10,18,1967,"Venera 4 becomes the first probe to send data from another planet's atmosphere."},
    {10,24,1946,"A V-2 rocket takes the first photographs of Earth from space."},
    {10,29,1969,"The first ARPANET message is sent."},
    {11,3,1957,"Laika becomes the first animal to orbit Earth."},
    {11,12,2014,"Rosetta's Philae lander touches down on a comet."},
    {11,16,1974,"The Arecibo message is beamed toward the star cluster M13."},
    {11,20,1998,"Zarya, the first International Space Station module, is launched."},
    {12,3,1992,"The first SMS text message is sent."},
    {12,14,1972,"The last humans leave the Moon as Apollo 17 lifts off."},
    {12,16,1917,"Arthur C. Clarke, who later made Sri Lanka his home, is born in England."},
    {12,17,1903,"The Wright brothers make the first powered flights."},
    {12,21,1968,"Apollo 8 launches, the first crewed mission to leave Earth orbit."},
    {12,25,2021,"The James Webb Space Telescope launches."},
};

struct Shower { int m, d; const char* name; double radiantDec; };
const Shower SHOWERS[] = {
    {1,3,"Quadrantid",49}, {4,22,"Lyrid",34}, {5,6,"Eta Aquariid",-1}, {8,12,"Perseid",58}, {10,8,"Draconid",54},
    {10,21,"Orionid",16}, {11,17,"Leonid",22}, {12,14,"Geminid",33}, {12,22,"Ursid",76},
};

} // namespace

bool inSriLanka(const Geo& g) { return g.lat >= 5.7 && g.lat <= 10.0 && g.lon >= 79.4 && g.lon <= 82.2; }

std::string poyaName(int month) {
    static const char* n[12] = {"Duruthu", "Navam", "Medin", "Bak", "Vesak", "Poson", "Esala", "Nikini", "Binara", "Vap", "Il", "Unduvap"};
    return n[(month - 1 + 12) % 12];
}

YearInfo yearInfo(const util::LocalTime& L) {
    YearInfo y{};
    y.doy = dayOfYear(L.y, L.m, L.d);
    y.daysInYear = isLeap(L.y) ? 366 : 365;
    y.daysLeft = y.daysInYear - y.doy;
    y.week = isoWeek(L.y, L.m, L.d, &y.isoYear);
    y.progress = (y.doy - 1 + (L.hh * 3600 + L.mm * 60 + L.ss) / 86400.0) / y.daysInYear;
    return y;
}

static std::string dayLengthTextFrom(const DayEvents& a, const DayEvents& b) {
    if (a.dayLengthSec <= 0) return "Sun stays below the horizon today";
    if (a.dayLengthSec >= 86400.0) return "Sun stays above the horizon all day";
    double diff = a.dayLengthSec - b.dayLengthSec;
    std::string s = util::fmtDuration(a.dayLengthSec) + " of daylight";
    if (std::fabs(diff) >= 1.0) s += " \xC2\xB7 " + util::fmtDeltaMinSec(diff) + (diff > 0 ? " longer" : " shorter") + " than yesterday";
    return s;
}

std::string dayLengthText(const Input& in) {
    double d0 = util::localMidnight(in.now, in.tzOff);
    return dayLengthTextFrom(dayEvents(d0, in.geo), dayEvents(d0 - 86400.0, in.geo));
}

std::vector<TodayItem> buildToday(const Input& in) {
    std::vector<TodayItem> out;
    for (size_t i = 0; i < in.storms.size() && i < 2; ++i) out.push_back({"STORM", in.storms[i].title, in.storms[i].detail, 99});
    util::LocalTime L = util::toLocal(in.now, in.tzOff);
    double d0 = util::localMidnight(in.now, in.tzOff);
    double d1 = d0 + 86400.0;
    auto clock = [&](double t) { return util::fmtClock(t, in.tzAt ? in.tzAt(t) : in.tzOff, in.h24); };
    // Holidays are those of the countries switched on in Settings (see holidays.h), whichever place is on show. Poya naming only where Sri Lanka's list is on.
    const int hc = holiday::countries();
    const bool hol = in.poya && hc != 0;
    const bool lk = hol && (hc & holiday::C_LK) != 0;
    const bool multi = hol && (hc & (hc - 1)) != 0;         // more than one country: say which country each holiday is from

    // custom events
    for (const auto& e : in.custom) {
        if (e.on(L.y, L.m, L.d)) out.push_back({"YOURS", e.title, "", 100});
    }

    // Public holidays of the countries that are on (official list, or an estimate for years without one). A holiday is a whole
    // calendar day: it starts at 12:00 AM local time, whatever time the Moon happens to be full. The same holiday in two countries is one row.
    int poyaItem = -1;
    std::string poyaNote;                                    // the full-moon time, folded into the Poya line
    if (hol) {
        for (const auto& m : holiday::merge(holiday::onDate(L.y, L.m, L.d))) {          // the same holiday in two countries is one row
            const holiday::Holiday& h = m.lead();
            std::string kind = std::string("From ") + clock(d0) + " \xC2\xB7 " + holiday::kindLine(h);
            if (h.estimated) kind += " \xC2\xB7 date estimated";
            if (h.poya() && poyaItem < 0) poyaItem = (int)out.size();
            std::string title = (h.poya() || h.id >= holiday::H_LK_COUNT) ? holiday::shortName(h) : holiday::name(h);
            if (multi) title += " (" + holiday::label(m) + ")";
            out.push_back({h.poya() ? "POYA" : "HOLIDAY", title, kind, 96});
        }
    }
    std::string poyaYesterday;                               // "Vap Poya was yesterday", for the full-moon line the day after
    if (lk) {
        long long z = (long long)std::llround(astro::daysFromCivil(L.y, L.m, L.d)) - 1;
        int yy, mm, dd; astro::civilFromDays(z, yy, mm, dd);
        for (const auto& h : holiday::onDate(yy, mm, dd)) if (h.poya()) { poyaYesterday = holiday::shortName(h) + " was yesterday"; break; }
    }

    // lunar phases that happen today, or soon
    const char* phaseTitles[4] = {"New Moon", "First Quarter", "Full Moon", "Last Quarter"};
    for (int w = 0; w < 4; ++w) {
        double t = nextPhaseTime(d0 - 1.0, w);
        if (t >= d0 && t < d1) {
            MoonInfo m = moonInfo(t, in.geo);
            LocalTime pl = util::toLocal(t, in.tzOff);
            if (w == 2) {
                std::string title = "Full Moon";
                std::string detail = "at " + clock(t) + " \xC2\xB7 fully lit";
                if (m.distKm < 360000.0) detail += fmt(" \xC2\xB7 supermoon, %d km away", (int)(std::round(m.distKm / 1000.0) * 1000));
                double pt = prevPhaseTime(t - 3600.0, 2);
                LocalTime pp = util::toLocal(pt, in.tzOff);
                if (pp.m == pl.m && pp.y == pl.y) { title += " \xC2\xB7 Blue Moon"; }
                if (poyaItem >= 0) poyaNote = "Full Moon at " + clock(t);                 // today is the Poya: one line, not two
                else {
                    if (!poyaYesterday.empty()) detail += " \xC2\xB7 " + poyaYesterday;
                    out.push_back({"MOON", title, detail, lk ? 70 : 92});
                }
            } else {
                static const char* phaseNote[4] = {"the dark Moon, a good night for faint stars", "half lit and waxing", "", "half lit and waning"};
                out.push_back({"MOON", w == 1 || w == 3 ? std::string(phaseTitles[w]) + " Moon" : std::string(phaseTitles[w]), fmt("at %s \xC2\xB7 %s", clock(t).c_str(), phaseNote[w]), 85});
            }
        } else if (t >= d1 && t < d1 + 86400.0) {
            if (w == 2 && poyaItem >= 0) poyaNote = "Full Moon tomorrow at " + clock(t);
            else out.push_back({"SOON", std::string(phaseTitles[w]) + " tomorrow", "at " + clock(t), 40});
        }
    }
    if (poyaItem >= 0 && !poyaNote.empty()) out[poyaItem].detail += " \xC2\xB7 " + poyaNote;
    // the next holiday within a week, or failing that the next full moon
    bool soonHoliday = false;
    if (hol) {
        std::vector<holiday::Day> nx = holiday::upcoming(L.y, L.m, L.d, 8, 4);
        for (const auto& dy : nx) {
            long long dd = (long long)std::llround(astro::daysFromCivil(dy.y, dy.m, dy.d) - astro::daysFromCivil(L.y, L.m, L.d));
            if (dd < 1) continue;
            std::string title = holiday::dayTitle(dy, multi) + (dd == 1 ? " tomorrow" : fmt(" in %lld days", dd));
            out.push_back({"SOON", title, fmt("%s %d %s \xC2\xB7 from %s \xC2\xB7 %s", util::WEEKDAYS3[astro::dayOfWeek(dy.y, dy.m, dy.d)], dy.d, util::MONTHS3[dy.m - 1], clock(d0).c_str(), holiday::categories(dy.items[0]).c_str()), dd == 1 ? 50 : 32});
            soonHoliday = true;
            break;
        }
    }
    if (!soonHoliday) {
        double t = nextPhaseTime(d1, 2);
        double days = (t - d0) / 86400.0;
        if (days > 2.0 && days <= 8.0) {
            LocalTime pl = util::toLocal(t, in.tzOff);
            std::string title = fmt("Full Moon in %d days", (int)std::floor((util::localMidnight(t, in.tzOff) - d0) / 86400.0 + 0.5));
            out.push_back({"SOON", title, fmt("%s %d \xC2\xB7 %s", util::MONTHS3[pl.m - 1], pl.d, clock(t).c_str()), 30});
        }
    }

    // equinox / solstice
    for (int w = 0; w < 4; ++w) {
        double t = seasonInstant(L.y, w);
        if (t >= d0 && t < d1) {
            static const char* names[4] = {"March equinox", "June solstice", "September equinox", "December solstice"};
            static const char* info[4] = {
                "The Sun crosses the celestial equator heading north. Day and night are nearly equal everywhere.",
                "The Sun reaches its northernmost point, 23.4\xC2\xB0 N.",
                "The Sun crosses the celestial equator heading south. Day and night are nearly equal everywhere.",
                "The Sun reaches its southernmost point, 23.4\xC2\xB0 S."};
            out.push_back({"SEASON", names[w], fmt("%s \xC2\xB7 %s", clock(t).c_str(), info[w]), 88});
        }
    }

    // observances (up to two)
    int shown = 0;
    std::vector<std::string> holNames;                       // a world observance with the name of a holiday already listed (Christmas Day) is left out
    if (hol) for (const auto& h : holiday::onDate(L.y, L.m, L.d)) { holNames.push_back(holiday::name(h)); holNames.push_back(holiday::shortName(h)); }
    for (const auto& o : OBS) {
        if (o.m == L.m && o.d == L.d && shown < 2 && !o.sl && std::find(holNames.begin(), holNames.end(), std::string(o.title)) == holNames.end()) {
            out.push_back({"WORLD", o.title, "", 60 - shown * 2});
            ++shown;
        }
    }
    for (const auto& o : ONEOFF)
        if (o.y == L.y && o.m == L.m && o.d == L.d) out.push_back({"WORLD", o.title, o.detail, 62});          // above the day's observances (60)
    if (L.m == 10 && L.d >= 4 && L.d <= 10) out.push_back({"WORLD", "World Space Week", "October 4\xE2\x80\x93" "10", 58});

    // meteor showers
    for (const auto& s : SHOWERS) {
        int dd = (int)std::llround(daysFromCivil(L.y, L.m, L.d) - daysFromCivil(L.y, s.m, s.d));
        if (std::abs(dd) <= 1) {
            double tNight = d1 + 2 * 3600.0;   // 2 AM tonight/tomorrow morning
            MoonInfo m = moonInfo(tNight, in.geo);
            double bestAlt = 90.0 - std::fabs(in.geo.lat - s.radiantDec);
            std::string sky;
            if (m.hor.alt < -2.0) sky = "the Moon is down at 2 AM";
            else if (m.illum < 0.3) sky = fmt("Moon only %d%% lit", (int)std::round(m.illum * 100));
            else sky = fmt("Moon %d%% lit will wash out fainter meteors", (int)std::round(m.illum * 100));
            std::string low = bestAlt < 30.0 ? " \xC2\xB7 radiant stays low from here" : "";
            std::string when = dd == 0 ? "peaks tonight" : (dd < 0 ? "peaks tomorrow" : "peaked yesterday");
            out.push_back({"SKY", std::string(s.name) + " meteor shower " + when, sky + low, 72});
        }
    }

    // Daylight Saving Time notice (ONLY in timezones whose UTC offset actually shifts within 48 hours; never shown in Sri Lanka / non-DST zones)
    if (in.tzAt) {
        const int off0 = in.tzAt(in.now);
        double foundHi = -1.0; int offHi = off0;
        for (int h = 1; h <= 48; ++h) {
            const double th = in.now + (double)h * 3600.0;
            const int oh = in.tzAt(th);
            if (oh != off0) { foundHi = th; offHi = oh; break; }
        }
        if (foundHi > 0.0) {
            double lo = foundHi - 3600.0, hi = foundHi;
            for (int k = 0; k < 12; ++k) {
                const double mid = 0.5 * (lo + hi);
                if (in.tzAt(mid) == off0) lo = mid; else hi = mid;
            }
            const int deltaMin = (offHi - off0) / 60;
            const bool fwd = deltaMin > 0;
            const LocalTime ltBefore = util::toLocal(hi - 1.0, off0);
            const std::string beforeStr = util::fmtClock(hi, off0, in.h24);
            const std::string afterStr = util::fmtClock(hi, offHi, in.h24);
            const int hrs = std::abs(deltaMin) / 60;
            const std::string amt = (std::abs(deltaMin) % 60 == 0) ? fmt("%d hour%s", hrs, hrs == 1 ? "" : "s") : fmt("%d min", std::abs(deltaMin));
            out.push_back({"CLOCKS",
                fwd ? fmt("Clocks spring forward %s", amt.c_str()) : fmt("Clocks fall back %s", amt.c_str()),
                fmt("%s %d %s \xC2\xB7 %s to %s", util::WEEKDAYS3[ltBefore.wday], ltBefore.d, util::MONTHS3[ltBefore.m - 1], beforeStr.c_str(), afterStr.c_str()),
                86});
        }
    }

    // Sun near the zenith (compute dayEvents once and reuse for Golden Hour and daylight fill below)
    const DayEvents todayEv = dayEvents(d0, in.geo);
    if (todayEv.noonAlt >= 89.0) out.push_back({"SKY", "The Sun is almost directly overhead at noon", fmt("%.1f\xC2\xB0 high at %s \xC2\xB7 shadows nearly vanish", todayEv.noonAlt, clock(todayEv.noon).c_str()), 80});
    else if (todayEv.noonAlt >= 86.0) out.push_back({"SKY", "The Sun passes close to overhead this week", fmt("%.1f\xC2\xB0 high at %s", todayEv.noonAlt, clock(todayEv.noon).c_str()), 55});

    // Golden hour cue (active right now, or beginning within 45 minutes)
    {
        auto checkGolden = [&](double startT, double endT, const char* which) -> bool {
            if (startT <= 0.0 || endT <= startT) return false;
            if (in.now >= startT && in.now < endT) {
                out.push_back({"LIGHT", fmt("%s golden hour right now", which), fmt("Warm low sun until %s", clock(endT).c_str()), 36});
                return true;
            }
            if (startT > in.now && startT - in.now <= 45.0 * 60.0) {
                const int mins = std::max(1, (int)std::lround((startT - in.now) / 60.0));
                out.push_back({"LIGHT", fmt("%s golden hour in %d min", which, mins), fmt("%s \xE2\x80\x93 %s", clock(startT).c_str(), clock(endT).c_str()), 34});
                return true;
            }
            return false;
        };
        bool added = false;
        if (todayEv.sun.hasRise && todayEv.golden.hasRise) added = checkGolden(todayEv.sun.rise, todayEv.golden.rise, "Morning");
        if (!added && todayEv.golden.hasSet && todayEv.sun.hasSet) checkGolden(todayEv.golden.set, todayEv.sun.set, "Evening");
    }

    // Moon close to a planet this evening
    {
        double best = 99; int bestP = -1; double bestT = 0;
        for (double t = d0 + 18 * 3600.0; t <= d0 + 30 * 3600.0; t += 1800.0) {
            MoonInfo m = moonInfo(t, in.geo);
            if (m.hor.alt < 5.0) continue;
            for (int p = 0; p < PLANET_COUNT; ++p) {
                PlanetInfo pi = planetInfo(p, t, in.geo);
                if (pi.hor.alt < 5.0 || pi.elongation < 15.0) continue;
                double sep = angularSeparation(m.eq, pi.eq);
                if (sep < best) { best = sep; bestP = p; bestT = t; }
            }
        }
        if (bestP >= 0 && best < 3.0)
            out.push_back({"SKY", fmt("The Moon passes %s", planetName(bestP)), fmt("%.1f\xC2\xB0 apart around %s", best, clock(bestT).c_str()), 66});
    }

    // history (on dates with multiple entries, alternate by year so every entry is reachable)
    {
        std::vector<const HistoryItem*> matches;
        for (const auto& h : HIST) if (h.m == L.m && h.d == L.d) matches.push_back(&h);
        if (!matches.empty()) {
            const HistoryItem* h = matches[(size_t)(std::abs(L.y) % (int)matches.size())];
            out.push_back({"ON THIS DAY", fmt("%d", h->year), h->text, 45});
        }
    }

    // ---- Every day has something to say. On a day with few special occasions the list would be empty ("A quiet day."), so
    // when there are fewer than three items, add what is true every day: the Moon, the next special day, the evening sky,
    // the length of the day and how far through the year it is. They sit below every real occasion (priority 10-30).
    // To add another kind, push one more TodayItem into 'fill' below. Only as many as are needed to reach three are shown.
    if (out.size() < 3) {
        std::vector<TodayItem> fill;
        const auto days = [&](double t) { return (int)std::floor((util::localMidnight(t, in.tzOff) - d0) / 86400.0 + 0.5); };
        const auto inDays = [](int n) { return n <= 0 ? std::string("today") : n == 1 ? std::string("tomorrow") : fmt("in %d days", n); };
        {   // the Moon, unless a lunar phase is already listed
            bool haveMoon = false;
            for (const auto& it : out) if (it.tag == "MOON" || it.tag == "POYA" || (it.tag == "SOON" && it.title.find("Moon") != std::string::npos)) haveMoon = true;
            if (!haveMoon && !in.omitMoonFill) {
                const MoonInfo m = moonInfo(in.now, in.geo);
                const double nf = nextPhaseTime(in.now, 2), nn = nextPhaseTime(in.now, 0);
                const bool full = nf < nn; const double t = full ? nf : nn;
                fill.push_back({"MOON", fmt("%s \xC2\xB7 %d%% lit", phaseName(m.elongation), (int)std::lround(m.illum * 100.0)),
                                fmt("Day %.0f of the cycle \xC2\xB7 %s %s at %s", std::floor(m.ageDays) + 1, full ? "Full Moon" : "New Moon", inDays(days(t)).c_str(), clock(t).c_str()), 28});
            }
        }
        if (hol) {   // the next public holiday, when it is further away than the week the list above already covers
            bool haveSoon = false;
            for (const auto& it : out) if (it.tag == "SOON") haveSoon = true;
            if (!haveSoon) {
                for (const auto& dy : holiday::upcoming(L.y, L.m, L.d, 70, 3)) {
                    const long long dd = (long long)std::llround(astro::daysFromCivil(dy.y, dy.m, dy.d) - astro::daysFromCivil(L.y, L.m, L.d));
                    if (dd < 1) continue;
                    fill.push_back({"NEXT", holiday::dayTitle(dy, multi) + " " + inDays((int)dd),
                                    fmt("%s %d %s \xC2\xB7 %s", util::WEEKDAYS3[astro::dayOfWeek(dy.y, dy.m, dy.d)], dy.d, util::MONTHS3[dy.m - 1], holiday::categories(dy.items[0]).c_str()), 26});
                    break;
                }
            }
        }
        {   // the best planet this evening (8 PM local)
            const double t8 = d0 + 20 * 3600.0; int best = -1; double bestAlt = 12.0;
            for (int p = 0; p < PLANET_COUNT; ++p) {
                const PlanetInfo pi = planetInfo(p, t8, in.geo);
                if (pi.elongation < 20.0) continue;
                if (pi.hor.alt > bestAlt) { bestAlt = pi.hor.alt; best = p; }
            }
            if (best >= 0) {
                const PlanetInfo pi = planetInfo(best, t8, in.geo);
                fill.push_back({"TONIGHT", fmt("%s is up this evening", planetName(best)), fmt("%.0f\xC2\xB0 high in the %s at %s \xC2\xB7 magnitude %+.1f", pi.hor.alt, util::compass16(pi.hor.az).c_str(), clock(t8).c_str(), pi.mag), 22});
            }
        }
        {   // the day itself
            const std::string dl = dayLengthTextFrom(todayEv, dayEvents(d0 - 86400.0, in.geo));
            const size_t p = dl.find(" of daylight");
            if (p != std::string::npos) {
                const std::string sep = " \xC2\xB7 ";
                const size_t q = dl.find(sep, p);
                fill.push_back({"DAYLIGHT", dl.substr(0, p) + " of daylight", q != std::string::npos ? dl.substr(q + sep.size()) : std::string(), 18});
            }
        }
        {
            const YearInfo y = yearInfo(L);
            fill.push_back({"YEAR", fmt("Day %d of %d", y.doy, y.daysInYear), fmt("Week %d \xC2\xB7 %d days left in %d", y.week, y.daysLeft, L.y), 12});
        }
        std::stable_sort(fill.begin(), fill.end(), [](const TodayItem& a, const TodayItem& b) { return a.priority > b.priority; });
        for (const auto& f : fill) { if (out.size() >= 3) break; out.push_back(f); }
    }

    std::stable_sort(out.begin(), out.end(), [](const TodayItem& a, const TodayItem& b) { return a.priority > b.priority; });
    return out;
}

} // namespace today
