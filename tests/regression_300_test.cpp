// Regression tests for Meridian 3.0.0 "Aurora":
//   - Live NOAA SWPC Kp-index space weather parser & geomagnetic aurora visibility model
//   - Heliocentric 8-planet Solar System Orrery ephemeris (Mercury through Neptune)
#include "spacewx.h"
#include "astro.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

static void testSpaceWx() {
    // 1. Classic NOAA SWPC array-of-arrays payload
    const double now = astro::daysFromCivil(2026, 11, 22) * 86400.0 + 15 * 3600.0;
    const std::string noaaArr = R"([
        ["time_tag","Kp","a_running","station_count"],
        ["2026-11-22 03:00:00.000","3.33","18","8"],
        ["2026-11-22 06:00:00.000","6.67","94","8"],
        ["2026-11-22 12:00:00.000","5.33","56","8"]
    ])";
    spacewx::Data d; std::string err;
    assert(spacewx::parseNoaaKp(noaaArr, now, d, err));
    assert(d.valid);
    assert(std::fabs(d.kp - 5.33) < 0.01);
    assert(std::fabs(d.kpMax24h - 6.67) < 0.01);
    assert(spacewx::stormScale(d.kp) == 1);        // G1 Minor Storm
    assert(spacewx::stormScale(d.kpMax24h) == 3);  // G3 Strong Storm

    // 2. Official NOAA third-step G-scale boundaries (including Kp 9- = 8.67 -> G4 Severe, Kp 9o = 9.00 -> G5 Extreme)
    assert(spacewx::stormScale(4.33) == 0);
    assert(spacewx::stormScale(4.67) == 1);
    assert(spacewx::stormScale(5.33) == 1);
    assert(spacewx::stormScale(5.67) == 2);
    assert(spacewx::stormScale(6.67) == 3);
    assert(spacewx::stormScale(7.67) == 4);
    assert(spacewx::stormScale(8.33) == 4);
    assert(spacewx::stormScale(8.67) == 4);        // Kp 9- is G4 Severe Storm per NOAA
    assert(spacewx::stormScale(9.00) == 5);        // Kp 9o is G5 Extreme Storm

    // 3. Newer object-array schema fallback
    const std::string noaaObj = R"([
        {"time_tag":"2026-11-22T09:00:00Z","kp_index":7.33},
        {"time_tag":"2026-11-22T12:00:00Z","kp_index":8.67}
    ])";
    spacewx::Data d2; err.clear();
    assert(spacewx::parseNoaaKp(noaaObj, now, d2, err));
    assert(std::fabs(d2.kp - 8.67) < 0.01);
    assert(spacewx::stormScale(d2.kp) == 4);       // G4 Severe Storm

    // 4. Geomagnetic visibility threshold: Oslo (59.9N) sees Kp 5.3; equator (7N) does not unless globalStormGlow
    assert(spacewx::auroraVisibility(5.33, 59.9, false) > 0.35f);
    assert(spacewx::auroraVisibility(5.33, 7.0, false) == 0.0f);
    assert(spacewx::auroraVisibility(7.0, 7.0, true) > 0.35f);

    // 5. Reject stale or malformed feeds cleanly
    const std::string stale = R"([["time_tag","Kp"],["2020-01-01 00:00:00","6.0"]])";
    spacewx::Data d3; err.clear();
    assert(!spacewx::parseNoaaKp(stale, now, d3, err));
    assert(!spacewx::parseNoaaKp("{not-an-array}", now, d3, err));
}

static void testOrrery() {
    const double t = astro::daysFromCivil(2026, 11, 22) * 86400.0 + 12 * 3600.0;
    double prevA = 0.0;
    for (int i = 0; i < astro::ORRERY_COUNT; ++i) {
        astro::HelioBody b = astro::orreryBody(i, t);
        assert(std::isfinite(b.xAU) && std::isfinite(b.yAU) && std::isfinite(b.rAU));
        assert(b.semiMajorAU > prevA);
        prevA = b.semiMajorAU;
        assert(b.rAU > 0.25 && b.rAU < 35.0);
        assert(b.lonDeg >= 0.0 && b.lonDeg < 360.0);
        // Verify Cartesian radius matches rAU
        const double mag = std::sqrt(b.xAU * b.xAU + b.yAU * b.yAU + b.zAU * b.zAU);
        assert(std::fabs(mag - b.rAU) < 0.02);
    }
    // Earth moves ~90 degrees in 91 days
    astro::HelioBody e0 = astro::orreryBody(astro::OP_EARTH, t);
    astro::HelioBody e1 = astro::orreryBody(astro::OP_EARTH, t + 91.3 * 86400.0);
    const double dLon = astro::norm360(e1.lonDeg - e0.lonDeg);
    assert(dLon > 85.0 && dLon < 97.0);
    // Moon orientation: New Moon (2026-11-09 07:02 UTC) sits sunward of Earth (Earth helio lon + 180); Full Moon (2026-11-24 14:53 UTC) sits anti-solar (Earth helio lon)
    const double tNew = astro::daysFromCivil(2026, 11, 9) * 86400.0 + 7.03 * 3600.0;
    const double tFull = astro::daysFromCivil(2026, 11, 24) * 86400.0 + 14.88 * 3600.0;
    auto angDiff = [](double a, double b) { double d = std::fabs(astro::norm360(a - b)); return d > 180.0 ? 360.0 - d : d; };
    const double sunwardNew = astro::norm360(astro::orreryBody(astro::OP_EARTH, tNew).lonDeg + 180.0);
    const double antisolarFull = astro::orreryBody(astro::OP_EARTH, tFull).lonDeg;
    assert(angDiff(astro::orreryMoonLonDeg(tNew), sunwardNew) < 5.0);
    assert(angDiff(astro::orreryMoonLonDeg(tFull), antisolarFull) < 5.0);
}

int main() {
    testSpaceWx();
    testOrrery();
    std::puts("regression_300 ok");
    return 0;
}
