// Meridian astronomy core. Portable C++17, no OS dependencies.
// Sun, Moon (Meeus ch.47), planets (Keplerian elements), stars, phases, rise/set, twilight.
#pragma once
#include <string>
#include <vector>
#include <functional>

namespace astro {

constexpr double PI = 3.14159265358979323846;
inline double rad(double d) { return d * PI / 180.0; }
inline double deg(double r) { return r * 180.0 / PI; }
double norm360(double x);
double norm180(double x);

struct Geo {
    double lat = 51.4779;  // degrees north (Greenwich, Prime Meridian)
    double lon = 0.0000;   // degrees east
    std::string name = "Greenwich";
};

// ---- time ----
double julianDay(double unixSec);            // UT
double deltaTSeconds(double unixSec);        // TT - UT
double gmstDeg(double unixSec);
double daysFromCivil(int y, int m, int d);   // days since 1970-01-01 (proleptic Gregorian)
void civilFromDays(long long z, int& y, int& m, int& d);
int dayOfWeek(int y, int m, int d);          // 0 = Sunday
int dayOfYear(int y, int m, int d);
int isoWeek(int y, int m, int d, int* isoYear = nullptr);
bool isLeap(int y);

struct Equatorial { double ra = 0, dec = 0; };     // degrees, of date
struct Horizontal { double alt = 0, az = 0; };     // degrees, az clockwise from north

double refractionDeg(double trueAltDeg);           // add to true altitude
Horizontal toHorizontal(const Equatorial& eq, double unixSec, const Geo& g);
Equatorial eclipticToEquatorial(double lonDeg, double latDeg, double eclDeg);
Equatorial galacticToEquatorialJ2000(double l, double b);
Equatorial precessJ2000(const Equatorial& eq, double unixSec);
double angularSeparation(const Equatorial& a, const Equatorial& b);

// ---- Sun ----
struct SunInfo {
    Equatorial eq;
    double eclLon = 0;      // apparent, degrees
    double distAU = 1;
    Horizontal hor;         // true altitude (no refraction) in hor.alt
    double appAlt = 0;      // with refraction
};
SunInfo sunInfo(double unixSec, const Geo& g);
double equationOfTimeMinutes(double unixSec);

// ---- Moon ----
struct MoonInfo {
    Equatorial eq;          // geocentric
    double eclLon = 0, eclLat = 0;
    double distKm = 384400;
    Horizontal hor;         // topocentric, true altitude
    double appAlt = 0;
    double elongation = 0;  // moon - sun ecliptic longitude, 0..360 (0 new, 180 full)
    double phaseAngle = 0;  // degrees, 0 = full, 180 = new
    double illum = 0;       // 0..1
    double brightLimbPA = 0;// position angle of bright limb from celestial north through east, degrees
    double ageDays = 0;     // since last new moon
    bool waxing = true;
    double angDiamDeg = 0.5;
};
MoonInfo moonInfo(double unixSec, const Geo& g);
const char* phaseName(double elongationDeg);
// Direction (screen-space unit vector, x right, y down) conventions are applied by the UI.

// Exact phase instants. which: 0 new, 1 first quarter, 2 full, 3 last quarter.
double nextPhaseTime(double unixSec, int which);
double prevPhaseTime(double unixSec, int which);

// ---- Planets ----
enum Planet { MERCURY = 0, VENUS, MARS, JUPITER, SATURN, PLANET_COUNT };
struct PlanetInfo {
    int id = 0;
    const char* name = "";
    Equatorial eq;
    double distAU = 0, sunDistAU = 0;
    double mag = 0;
    double elongation = 0;  // from sun, degrees
    Horizontal hor;
    double appAlt = 0;
};
PlanetInfo planetInfo(int id, double unixSec, const Geo& g);
const char* planetName(int id);

// ---- Stars ----
struct Star { const char* name; double raH; double decDeg; double mag; float bv = 0.0f; };
const std::vector<Star>& stars();
struct ConstellationLine { int a, b; };            // indices into stars()
struct Constellation { const char* name; std::vector<ConstellationLine> lines; };
const std::vector<Constellation>& constellations();

// ---- Events: rise / set / twilight ----
struct RiseSet { bool hasRise = false, hasSet = false; double rise = 0, set = 0; };
// Generic altitude crossing search over [t0, t1]. altFn returns altitude minus threshold.
void findCrossings(const std::function<double(double)>& f, double t0, double t1, double stepSec,
                   std::vector<double>& risings, std::vector<double>& settings);
RiseSet sunRiseSet(double t0, double t1, const Geo& g, double thresholdDeg = -0.833);
RiseSet moonRiseSet(double t0, double t1, const Geo& g);
double solarNoon(double dayStartUnix, const Geo& g);   // transit near the middle of the 24 h window
struct DayEvents {
    RiseSet sun;
    double noon = 0;
    double noonAlt = 0;
    RiseSet civil, nautical, astro;     // rise = dawn, set = dusk
    RiseSet golden;                      // sun crossing +6 degrees (golden hour boundary)
    RiseSet blue;                        // sun crossing -4 degrees (blue hour boundary)
    double dayLengthSec = 0;
};
DayEvents dayEvents(double dayStartUnix, const Geo& g);

// Equinoxes and solstices: which 0 = March equinox, 1 = June solstice, 2 = September equinox, 3 = December solstice.
double seasonInstant(int year, int which);

// Sun's declination (degrees) at a given time
double sunDeclination(double unixSec);

} // namespace astro
