// Sky events within a few days: meteor showers, planet meetings, oppositions, planet parades, solar and lunar eclipses.
// Everything is computed from the astronomy core (no data files, no network). Portable C++.
#pragma once
#include "astro.h"
#include <string>
#include <vector>

namespace skyev {

enum Kind { METEOR = 0, CONJUNCTION, MOON_NEAR, OPPOSITION, ELONGATION, PARADE, SOLAR_ECLIPSE, LUNAR_ECLIPSE };
const int MOON_ID = 100;                       // body id for the Moon in Event::bodies (planets use astro::Planet ids)

struct Detail { std::string label, value; };   // value may contain time tokens {t:UNIX}, formatted by the UI

struct Event {
    Kind kind = METEOR;
    std::string title, subtitle;
    double tPeak = 0;                          // peak / closest approach / greatest eclipse (unix)
    double tStart = 0, tEnd = 0;               // the span in which it is "on" (eclipse: first and last contact)
    std::vector<Detail> details;
    // meteor showers
    int shower = -1; double raDeg = 0, decDeg = 0, zhr = 0, speed = 0;
    // planets / Moon
    int bodies[8] = {0, 0, 0, 0, 0, 0, 0, 0}; int nBodies = 0;
    double sepDeg = 0;
    // eclipses
    int type = 0;                              // lunar: 0 penumbral, 1 partial, 2 total.  solar: 0 partial, 1 annular, 2 total
    double magnitude = 0, obscuration = 0, gamma = 0;
    bool visible = false;                      // can be seen from the observer's location
    double daysAway(double now) const { return (tPeak - now) / 86400.0; }
    bool active(double now) const { return now >= tStart && now <= tEnd; }
};

struct Options {
    bool meteors = true, planets = true, eclipses = true;
    double windowDays = 5.0;                   // list events that peak within this many days (and those still under way)
};

std::vector<Event> find(double now, const astro::Geo& g, const Options& o);

// ---- moment-by-moment geometry for drawing ----
// Earth's shadow on the Moon. dx/dy: shadow-axis centre relative to the Moon's centre, degrees (x east, y north).
struct LunarShadow { bool active = false; double dx = 0, dy = 0, umbra = 0, penumbra = 0, moonR = 0; };
LunarShadow lunarShadowAt(double t, const astro::Geo& g);
// The Moon in front of the Sun as seen from the observer. dx/dy: Moon centre relative to the Sun's centre (x east, y north).
struct SolarCover { bool active = false; double dx = 0, dy = 0, sunR = 0, moonR = 0, sep = 0; };
SolarCover solarCoverAt(double t, const astro::Geo& g);

const char* showerName(int i);
int showerCount();

} // namespace skyev
