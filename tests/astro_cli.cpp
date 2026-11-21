// Native test harness: prints astronomy values as CSV-like lines for comparison with astropy / ephem.
#include "../src/astro.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
using namespace astro;
int main(int argc, char** argv) {
    Geo g; g.lat = 6.9271; g.lon = 79.8612;
    if (argc > 2) { g.lat = atof(argv[1]); g.lon = atof(argv[2]); }
    double t;
    while (scanf("%lf", &t) == 1) {
        SunInfo s = sunInfo(t, g);
        MoonInfo m = moonInfo(t, g);
        printf("T %.3f\n", t);
        printf("sun %.5f %.5f %.5f %.5f\n", s.hor.alt, s.hor.az, s.eq.ra, s.eq.dec);
        printf("moon %.5f %.5f %.5f %.5f %.2f %.5f %.5f %.4f\n", m.hor.alt, m.hor.az, m.eq.ra, m.eq.dec, m.distKm, m.illum, m.phaseAngle, m.brightLimbPA);
        for (int p = 0; p < PLANET_COUNT; ++p) {
            PlanetInfo pi = planetInfo(p, t, g);
            printf("planet %s %.5f %.5f %.5f %.5f %.2f %.4f\n", pi.name, pi.hor.alt, pi.hor.az, pi.eq.ra, pi.eq.dec, pi.mag, pi.elongation);
        }
        const auto& st = stars();
        const char* want[] = {"Sirius", "Vega", "Antares", "Canopus", "Polaris", "Acrux"};
        for (const char* w : want) for (auto& x : st) if (std::string(x.name) == w) {
            Equatorial e = precessJ2000({x.raH * 15.0, x.decDeg}, t);
            Horizontal h = toHorizontal(e, t, g);
            printf("star %s %.5f %.5f\n", x.name, h.alt, h.az);
        }
        printf("nextnew %.1f nextfull %.1f prevnew %.1f nextfq %.1f\n", nextPhaseTime(t, 0), nextPhaseTime(t, 2), prevPhaseTime(t, 0), nextPhaseTime(t, 1));
        double day0 = std::floor((t + 19800.0) / 86400.0) * 86400.0 - 19800.0;
        DayEvents de = dayEvents(day0, g);
        printf("day %.1f %.1f %.1f %.4f %.1f %.1f %.1f\n", de.sun.rise, de.sun.set, de.noon, de.noonAlt, de.civil.rise, de.civil.set, de.dayLengthSec);
        RiseSet mr = moonRiseSet(day0, day0 + 86400.0, g);
        printf("moonrs %d %.1f %d %.1f\n", mr.hasRise, mr.rise, mr.hasSet, mr.set);
    }
    for (int y = 2026; y <= 2027; ++y) for (int w = 0; w < 4; ++w) printf("season %d %d %.1f\n", y, w, seasonInstant(y, w));
    return 0;
}
