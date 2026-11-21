// Meridian astronomy core. See astro.h.
#include "astro.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace astro {

double norm360(double x) { x = std::fmod(x, 360.0); if (x < 0) x += 360.0; return x; }
double norm180(double x) { x = norm360(x + 180.0) - 180.0; return x; }

// ---------------------------------------------------------------- time
double julianDay(double unixSec) { return unixSec / 86400.0 + 2440587.5; }

double deltaTSeconds(double unixSec) {
    double y = 1970.0 + unixSec / (365.2425 * 86400.0);
    double t = y - 2000.0;
    return 62.92 + 0.32217 * t + 0.005589 * t * t;   // valid ~2005-2050
}

static double centuriesTT(double unixSec) {
    double jd = julianDay(unixSec) + deltaTSeconds(unixSec) / 86400.0;
    return (jd - 2451545.0) / 36525.0;
}

double gmstDeg(double unixSec) {
    double jd = julianDay(unixSec);
    double T = (jd - 2451545.0) / 36525.0;
    double th = 280.46061837 + 360.98564736629 * (jd - 2451545.0) + 0.000387933 * T * T - T * T * T / 38710000.0;
    return norm360(th);
}

double daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    long long era = (y >= 0 ? y : y - 399) / 400;
    long long yoe = y - era * 400;
    long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (double)(era * 146097 + doe - 719468);
}

void civilFromDays(long long z, int& y, int& m, int& d) {
    z += 719468;
    long long era = (z >= 0 ? z : z - 146096) / 146097;
    long long doe = z - era * 146097;
    long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long yy = yoe + era * 400;
    long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long long mp = (5 * doy + 2) / 153;
    d = (int)(doy - (153 * mp + 2) / 5 + 1);
    m = (int)(mp < 10 ? mp + 3 : mp - 9);
    y = (int)(yy + (m <= 2));
}

int dayOfWeek(int y, int m, int d) {
    long long days = (long long)daysFromCivil(y, m, d);
    long long w = (days + 4) % 7; if (w < 0) w += 7;
    return (int)w;
}

bool isLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int dayOfYear(int y, int m, int d) {
    return (int)(daysFromCivil(y, m, d) - daysFromCivil(y, 1, 1)) + 1;
}

static int weeksInIsoYear(int y) {
    auto p = [](int yy) { return (yy + yy / 4 - yy / 100 + yy / 400) % 7; };
    return (p(y) == 4 || p(y - 1) == 3) ? 53 : 52;
}

int isoWeek(int y, int m, int d, int* isoYear) {
    int wd = dayOfWeek(y, m, d); if (wd == 0) wd = 7;
    int doy = dayOfYear(y, m, d);
    int w = (doy - wd + 10) / 7;
    int iy = y;
    if (w < 1) { iy = y - 1; w = weeksInIsoYear(iy); }
    else if (w > weeksInIsoYear(y)) { w = 1; iy = y + 1; }
    if (isoYear) *isoYear = iy;
    return w;
}

// ---------------------------------------------------------------- coordinates
double refractionDeg(double h) {
    if (h < -1.0) return 0.0;
    double R = 1.02 / std::tan(rad(h + 10.3 / (h + 5.11)));   // arcminutes
    return R / 60.0;
}

static Horizontal horizontalFromHA(double Hdeg, double decDeg, double latDeg) {
    double H = rad(Hdeg), d = rad(decDeg), f = rad(latDeg);
    double sinh = std::sin(f) * std::sin(d) + std::cos(f) * std::cos(d) * std::cos(H);
    double alt = std::asin(std::max(-1.0, std::min(1.0, sinh)));
    double az = std::atan2(std::sin(H), std::cos(H) * std::sin(f) - std::tan(d) * std::cos(f));
    Horizontal h;
    h.alt = deg(alt);
    h.az = norm360(deg(az) + 180.0);
    return h;
}

Horizontal toHorizontal(const Equatorial& eq, double unixSec, const Geo& g) {
    double H = gmstDeg(unixSec) + g.lon - eq.ra;
    return horizontalFromHA(H, eq.dec, g.lat);
}

Equatorial eclipticToEquatorial(double lonDeg, double latDeg, double eclDeg) {
    double l = rad(lonDeg), b = rad(latDeg), e = rad(eclDeg);
    Equatorial r;
    r.ra = norm360(deg(std::atan2(std::sin(l) * std::cos(e) - std::tan(b) * std::sin(e), std::cos(l))));
    r.dec = deg(std::asin(std::sin(b) * std::cos(e) + std::cos(b) * std::sin(e) * std::sin(l)));
    return r;
}

Equatorial galacticToEquatorialJ2000(double l, double b) {
    const double raNGP = rad(192.85948), decNGP = rad(27.12825), lNCP = rad(122.93192);
    double L = rad(l), B = rad(b);
    double sd = std::sin(B) * std::sin(decNGP) + std::cos(B) * std::cos(decNGP) * std::cos(lNCP - L);
    double dec = std::asin(sd);
    double y = std::cos(B) * std::sin(lNCP - L);
    double x = std::sin(B) * std::cos(decNGP) - std::cos(B) * std::sin(decNGP) * std::cos(lNCP - L);
    Equatorial e;
    e.ra = norm360(deg(raNGP + std::atan2(y, x)));
    e.dec = deg(dec);
    return e;
}

Equatorial precessJ2000(const Equatorial& eq, double unixSec) {
    double T = centuriesTT(unixSec);
    double as = PI / (180.0 * 3600.0);
    double zeta = (2306.2181 * T + 0.30188 * T * T + 0.017998 * T * T * T) * as;
    double z = (2306.2181 * T + 1.09468 * T * T + 0.018203 * T * T * T) * as;
    double th = (2004.3109 * T - 0.42665 * T * T - 0.041833 * T * T * T) * as;
    double a0 = rad(eq.ra), d0 = rad(eq.dec);
    double A = std::cos(d0) * std::sin(a0 + zeta);
    double B = std::cos(th) * std::cos(d0) * std::cos(a0 + zeta) - std::sin(th) * std::sin(d0);
    double C = std::sin(th) * std::cos(d0) * std::cos(a0 + zeta) + std::cos(th) * std::sin(d0);
    Equatorial r;
    r.ra = norm360(deg(std::atan2(A, B) + z));
    r.dec = deg(std::asin(std::max(-1.0, std::min(1.0, C))));
    return r;
}

double angularSeparation(const Equatorial& a, const Equatorial& b) {
    double a1 = rad(a.ra), d1 = rad(a.dec), a2 = rad(b.ra), d2 = rad(b.dec);
    double c = std::sin(d1) * std::sin(d2) + std::cos(d1) * std::cos(d2) * std::cos(a1 - a2);
    return deg(std::acos(std::max(-1.0, std::min(1.0, c))));
}

// ---------------------------------------------------------------- nutation (low precision)
static void nutation(double T, double& dpsiDeg, double& depsDeg) {
    double Om = rad(125.04452 - 1934.136261 * T);
    double Ls = rad(280.4665 + 36000.7698 * T);
    double Lm = rad(218.3165 + 481267.8813 * T);
    dpsiDeg = (-17.20 * std::sin(Om) - 1.32 * std::sin(2 * Ls) - 0.23 * std::sin(2 * Lm) + 0.21 * std::sin(2 * Om)) / 3600.0;
    depsDeg = (9.20 * std::cos(Om) + 0.57 * std::cos(2 * Ls) + 0.10 * std::cos(2 * Lm) - 0.09 * std::cos(2 * Om)) / 3600.0;
}

static double meanObliquity(double T) {
    return 23.0 + 26.0 / 60.0 + 21.448 / 3600.0 - (46.8150 * T + 0.00059 * T * T - 0.001813 * T * T * T) / 3600.0;
}

// ---------------------------------------------------------------- Sun
struct SunRaw { double lonApp, distAU, eps, dpsi; Equatorial eq; double L0; };

static SunRaw sunRaw(double unixSec) {
    double T = centuriesTT(unixSec);
    double L0 = norm360(280.46646 + 36000.76983 * T + 0.0003032 * T * T);
    double M = norm360(357.52911 + 35999.05029 * T - 0.0001537 * T * T);
    double e = 0.016708634 - 0.000042037 * T - 0.0000001267 * T * T;
    double Mr = rad(M);
    double C = (1.914602 - 0.004817 * T - 0.000014 * T * T) * std::sin(Mr)
             + (0.019993 - 0.000101 * T) * std::sin(2 * Mr) + 0.000289 * std::sin(3 * Mr);
    double trueLon = L0 + C;
    double nu = Mr + rad(C);
    double R = 1.000001018 * (1 - e * e) / (1 + e * std::cos(nu));
    double dpsi, deps; nutation(T, dpsi, deps);
    double lonApp = norm360(trueLon - 0.00569 + dpsi);
    double eps = meanObliquity(T) + deps;
    SunRaw s;
    s.lonApp = lonApp; s.distAU = R; s.eps = eps; s.dpsi = dpsi; s.L0 = L0;
    s.eq = eclipticToEquatorial(lonApp, 0.0, eps);
    return s;
}

SunInfo sunInfo(double unixSec, const Geo& g) {
    SunRaw r = sunRaw(unixSec);
    SunInfo s;
    s.eq = r.eq; s.eclLon = r.lonApp; s.distAU = r.distAU;
    s.hor = toHorizontal(r.eq, unixSec, g);
    s.appAlt = s.hor.alt + refractionDeg(s.hor.alt);
    return s;
}

double equationOfTimeMinutes(double unixSec) {
    SunRaw r = sunRaw(unixSec);
    double E = norm180(r.L0 - 0.0057183 - r.eq.ra + r.dpsi * std::cos(rad(r.eps)));
    return E * 4.0;
}

double sunDeclination(double unixSec) { return sunRaw(unixSec).eq.dec; }

// ---------------------------------------------------------------- Moon (Meeus ch. 47)
namespace {
struct LRTerm { signed char D, M, Mp, F; int l; int r; };
struct BTerm  { signed char D, M, Mp, F; int b; };

const LRTerm LR[] = {
 {0,0,1,0, 6288774, -20905355}, {2,0,-1,0, 1274027, -3699111}, {2,0,0,0, 658314, -2955968}, {0,0,2,0, 213618, -569925},
 {0,1,0,0, -185116, 48888}, {0,0,0,2, -114332, -3149}, {2,0,-2,0, 58793, 246158}, {2,-1,-1,0, 57066, -152138},
 {2,0,1,0, 53322, -170733}, {2,-1,0,0, 45758, -204586}, {0,1,-1,0, -40923, -129620}, {1,0,0,0, -34720, 108743},
 {0,1,1,0, -30383, 104755}, {2,0,0,-2, 15327, 10321}, {0,0,1,2, -12528, 0}, {0,0,1,-2, 10980, 79661},
 {4,0,-1,0, 10675, -34782}, {0,0,3,0, 10034, -23210}, {4,0,-2,0, 8548, -21636}, {2,1,-1,0, -7888, 24208},
 {2,1,0,0, -6766, 30824}, {1,0,-1,0, -5163, -8379}, {1,1,0,0, 4987, -16675}, {2,-1,1,0, 4036, -12831},
 {2,0,2,0, 3994, -10445}, {4,0,0,0, 3861, -11650}, {2,0,-3,0, 3665, 14403}, {0,1,-2,0, -2689, -7003},
 {2,0,-1,2, -2602, 0}, {2,-1,-2,0, 2390, 10056}, {1,0,1,0, -2348, 6322}, {2,-2,0,0, 2236, -9884},
 {0,1,2,0, -2120, 5751}, {0,2,0,0, -2069, 0}, {2,-2,-1,0, 2048, -4950}, {2,0,1,-2, -1773, 4130},
 {2,0,0,2, -1595, 0}, {4,-1,-1,0, 1215, -3958}, {0,0,2,2, -1110, 0}, {3,0,-1,0, -892, 3258},
 {2,1,1,0, -810, 2616}, {4,-1,-2,0, 759, -1897}, {0,2,-1,0, -713, -2117}, {2,2,-1,0, -700, 2354},
 {2,1,-2,0, 691, 0}, {2,-1,0,-2, 596, 0}, {4,0,1,0, 549, -1423}, {0,0,4,0, 537, -1117},
 {4,-1,0,0, 520, -1571}, {1,0,-2,0, -487, -1739}, {2,1,0,-2, -399, 0}, {0,0,2,-2, -381, -4421},
 {1,1,1,0, 351, 0}, {3,0,-2,0, -340, 0}, {4,0,-3,0, 330, 0}, {2,-1,2,0, 327, 0},
 {0,2,1,0, -323, 1165}, {1,1,-1,0, 299, 0}, {2,0,3,0, 294, 0}, {2,0,-1,-2, 0, 8752},
};

const BTerm B[] = {
 {0,0,0,1, 5128122}, {0,0,1,1, 280602}, {0,0,1,-1, 277693}, {2,0,0,-1, 173237}, {2,0,-1,1, 55413},
 {2,0,-1,-1, 46271}, {2,0,0,1, 32573}, {0,0,2,1, 17198}, {2,0,1,-1, 9266}, {0,0,2,-1, 8822},
 {2,-1,0,-1, 8216}, {2,0,-2,-1, 4324}, {2,0,1,1, 4200}, {2,1,0,-1, -3359}, {2,-1,-1,1, 2463},
 {2,-1,0,1, 2211}, {2,-1,-1,-1, 2065}, {0,1,-1,-1, -1870}, {4,0,-1,-1, 1828}, {0,1,0,1, -1794},
 {0,0,0,3, -1749}, {0,1,-1,1, -1565}, {1,0,0,1, -1491}, {0,1,1,1, -1475}, {0,1,1,-1, -1410},
 {0,1,0,-1, -1344}, {1,0,0,-1, -1335}, {0,0,3,1, 1107}, {4,0,0,-1, 1021}, {4,0,-1,1, 833},
 {0,0,1,-3, 777}, {4,0,-2,1, 671}, {2,0,0,-3, 607}, {2,0,2,-1, 596}, {2,-1,1,-1, 491},
 {2,0,-2,1, -451}, {0,0,3,-1, 439}, {2,0,2,1, 422}, {2,0,-3,-1, 421}, {2,1,-1,1, -366},
 {2,1,0,1, -351}, {4,0,0,1, 331}, {2,-1,1,1, 315}, {2,-2,0,-1, 302}, {0,0,1,3, -283},
 {2,1,1,-1, -229}, {1,1,0,-1, 223}, {1,1,0,1, 223}, {0,1,-2,-1, -220}, {2,1,-1,-1, -220},
 {1,0,1,1, -185}, {2,-1,-2,-1, 181}, {0,1,2,1, -177}, {4,0,-2,-1, 176}, {4,-1,-1,-1, 166},
 {1,0,1,-1, -164}, {4,0,1,-1, 132}, {1,0,-1,-1, -119}, {4,-1,0,-1, 115}, {2,-2,0,1, 107},
};

struct MoonRaw { double lonApp, lat, distKm, eps; Equatorial eq; };

MoonRaw moonRaw(double unixSec) {
    double T = centuriesTT(unixSec);
    double T2 = T * T, T3 = T2 * T, T4 = T3 * T;
    double Lp = norm360(218.3164477 + 481267.88123421 * T - 0.0015786 * T2 + T3 / 538841.0 - T4 / 65194000.0);
    double D  = norm360(297.8501921 + 445267.1114034 * T - 0.0018819 * T2 + T3 / 545868.0 - T4 / 113065000.0);
    double M  = norm360(357.5291092 + 35999.0502909 * T - 0.0001536 * T2 + T3 / 24490000.0);
    double Mp = norm360(134.9633964 + 477198.8675055 * T + 0.0087414 * T2 + T3 / 69699.0 - T4 / 14712000.0);
    double F  = norm360(93.2720950 + 483202.0175233 * T - 0.0036539 * T2 - T3 / 3526000.0 + T4 / 863310000.0);
    double A1 = norm360(119.75 + 131.849 * T);
    double A2 = norm360(53.09 + 479264.290 * T);
    double A3 = norm360(313.45 + 481266.484 * T);
    double E = 1.0 - 0.002516 * T - 0.0000074 * T2;
    double Dr = rad(D), Mr = rad(M), Mpr = rad(Mp), Fr = rad(F);
    double sl = 0, sr = 0, sb = 0;
    for (const auto& t : LR) {
        double arg = t.D * Dr + t.M * Mr + t.Mp * Mpr + t.F * Fr;
        double f = 1.0; int am = std::abs((int)t.M); if (am == 1) f = E; else if (am == 2) f = E * E;
        sl += f * t.l * std::sin(arg);
        sr += f * t.r * std::cos(arg);
    }
    for (const auto& t : B) {
        double arg = t.D * Dr + t.M * Mr + t.Mp * Mpr + t.F * Fr;
        double f = 1.0; int am = std::abs((int)t.M); if (am == 1) f = E; else if (am == 2) f = E * E;
        sb += f * t.b * std::sin(arg);
    }
    sl += 3958 * std::sin(rad(A1)) + 1962 * std::sin(rad(Lp - F)) + 318 * std::sin(rad(A2));
    sb += -2235 * std::sin(rad(Lp)) + 382 * std::sin(rad(A3)) + 175 * std::sin(rad(A1 - F))
        + 175 * std::sin(rad(A1 + F)) + 127 * std::sin(rad(Lp - Mp)) - 115 * std::sin(rad(Lp + Mp));
    double lon = norm360(Lp + sl / 1000000.0);
    double lat = sb / 1000000.0;
    double dist = 385000.56 + sr / 1000.0;
    double dpsi, deps; nutation(T, dpsi, deps);
    double eps = meanObliquity(T) + deps;
    double lonApp = norm360(lon + dpsi);
    MoonRaw m;
    m.lonApp = lonApp; m.lat = lat; m.distKm = dist; m.eps = eps;
    m.eq = eclipticToEquatorial(lonApp, lat, eps);
    return m;
}
} // namespace

MoonInfo moonInfo(double unixSec, const Geo& g) {
    MoonRaw m = moonRaw(unixSec);
    SunRaw s = sunRaw(unixSec);
    MoonInfo r;
    r.eq = m.eq; r.eclLon = m.lonApp; r.eclLat = m.lat; r.distKm = m.distKm;
    r.angDiamDeg = 2.0 * deg(std::asin(1737.4 / m.distKm));

    // topocentric position (Meeus ch. 40)
    double phi = rad(g.lat);
    double u = std::atan(0.99664719 * std::tan(phi));
    double rhoSin = 0.99664719 * std::sin(u), rhoCos = std::cos(u);
    double sinPi = 6378.14 / m.distKm;
    double Hdeg = gmstDeg(unixSec) + g.lon - m.eq.ra;
    double H = rad(Hdeg), dec = rad(m.eq.dec);
    double dRa = std::atan2(-rhoCos * sinPi * std::sin(H), std::cos(dec) - rhoCos * sinPi * std::cos(H));
    double decP = std::atan2((std::sin(dec) - rhoSin * sinPi) * std::cos(dRa), std::cos(dec) - rhoCos * sinPi * std::cos(H));
    double HP = Hdeg - deg(dRa);
    r.hor = horizontalFromHA(HP, deg(decP), g.lat);
    r.appAlt = r.hor.alt + refractionDeg(r.hor.alt);

    // phase
    r.elongation = norm360(m.lonApp - s.lonApp);
    double cosPsi = std::cos(rad(m.lat)) * std::cos(rad(m.lonApp - s.lonApp));
    double psi = std::acos(std::max(-1.0, std::min(1.0, cosPsi)));
    double Rkm = s.distAU * 149597870.7;
    double i = std::atan2(Rkm * std::sin(psi), m.distKm - Rkm * std::cos(psi));
    r.phaseAngle = deg(i);
    r.illum = (1.0 + std::cos(i)) / 2.0;
    r.waxing = r.elongation < 180.0;
    r.ageDays = r.elongation / 360.0 * 29.530588;

    // bright limb position angle (Meeus ch. 48)
    double a0 = rad(s.eq.ra), d0 = rad(s.eq.dec), a = rad(m.eq.ra), d = rad(m.eq.dec);
    double y = std::cos(d0) * std::sin(a0 - a);
    double x = std::sin(d0) * std::cos(d) - std::cos(d0) * std::sin(d) * std::cos(a0 - a);
    r.brightLimbPA = norm360(deg(std::atan2(y, x)));
    return r;
}

const char* phaseName(double e) {
    e = norm360(e);
    if (e < 6.0 || e >= 354.0) return "New Moon";
    if (e < 84.0) return "Waxing Crescent";
    if (e < 96.0) return "First Quarter";
    if (e < 174.0) return "Waxing Gibbous";
    if (e < 186.0) return "Full Moon";
    if (e < 264.0) return "Waning Gibbous";
    if (e < 276.0) return "Last Quarter";
    return "Waning Crescent";
}

static double elongationAt(double t) {
    // cheap: apparent longitudes only
    return norm360(moonRaw(t).lonApp - sunRaw(t).lonApp);
}

double nextPhaseTime(double unixSec, int which) {
    double target = 90.0 * (which & 3);
    double cur = elongationAt(unixSec);
    double delta = norm360(target - cur);
    double tt = unixSec + delta / 12.1908 * 86400.0;
    for (int k = 0; k < 8; ++k) {
        double e = elongationAt(tt);
        double d = norm180(target - e);
        tt += d / 12.1908 * 86400.0;
    }
    if (tt <= unixSec + 1.0) {
        tt += 29.530588 * 86400.0;
        for (int k = 0; k < 8; ++k) { double e = elongationAt(tt); tt += norm180(target - e) / 12.1908 * 86400.0; }
    }
    return tt;
}

double prevPhaseTime(double unixSec, int which) {
    double target = 90.0 * (which & 3);
    double cur = elongationAt(unixSec);
    double delta = norm360(cur - target);
    double tt = unixSec - delta / 12.1908 * 86400.0;
    for (int k = 0; k < 8; ++k) {
        double e = elongationAt(tt);
        double d = norm180(target - e);
        tt += d / 12.1908 * 86400.0;
    }
    if (tt > unixSec) {
        tt -= 29.530588 * 86400.0;
        for (int k = 0; k < 8; ++k) { double e = elongationAt(tt); tt += norm180(target - e) / 12.1908 * 86400.0; }
    }
    return tt;
}

// ---------------------------------------------------------------- Planets (Keplerian elements, Schlyter)
namespace {
struct Elem { double N0, N1, i0, i1, w0, w1, a, e0, e1, M0, M1; };
const Elem ELEMS[PLANET_COUNT] = {
  {48.3313, 3.24587e-5, 7.0047, 5.00e-8, 29.1241, 1.01444e-5, 0.387098, 0.205635, 5.59e-10, 168.6562, 4.0923344368},
  {76.6799, 2.46590e-5, 3.3946, 2.75e-8, 54.8910, 1.38374e-5, 0.723330, 0.006773, -1.302e-9, 48.0052, 1.6021302244},
  {49.5574, 2.11081e-5, 1.8497, -1.78e-8, 286.5016, 2.92961e-5, 1.523688, 0.093405, 2.516e-9, 18.6021, 0.5240207766},
  {100.4542, 2.76854e-5, 1.3030, -1.557e-7, 273.8777, 1.64505e-5, 5.20256, 0.048498, 4.469e-9, 19.8950, 0.0830853001},
  {113.6634, 2.38980e-5, 2.4886, -1.081e-7, 339.3939, 2.97661e-5, 9.55475, 0.055546, -9.499e-9, 316.9670, 0.0334442282},
};
const char* NAMES[PLANET_COUNT] = {"Mercury", "Venus", "Mars", "Jupiter", "Saturn"};

double solveKepler(double Mdeg, double e) {
    double M = rad(norm360(Mdeg));
    double E = M + e * std::sin(M) * (1.0 + e * std::cos(M));
    for (int k = 0; k < 20; ++k) {
        double dE = (E - e * std::sin(E) - M) / (1.0 - e * std::cos(E));
        E -= dE;
        if (std::fabs(dE) < 1e-12) break;
    }
    return E;
}
} // namespace

const char* planetName(int id) { return (id >= 0 && id < PLANET_COUNT) ? NAMES[id] : ""; }

PlanetInfo planetInfo(int id, double unixSec, const Geo& g) {
    if (id < 0 || id >= PLANET_COUNT) return PlanetInfo{};
    double d = julianDay(unixSec) + deltaTSeconds(unixSec) / 86400.0 - 2451543.5;
    // Sun (geocentric) in the same framework
    double ws = 282.9404 + 4.70935e-5 * d, es = 0.016709 - 1.151e-9 * d, Ms = norm360(356.0470 + 0.9856002585 * d);
    double Es = solveKepler(Ms, es);
    double xvs = std::cos(Es) - es, yvs = std::sqrt(1 - es * es) * std::sin(Es);
    double vs = deg(std::atan2(yvs, xvs)), rs = std::sqrt(xvs * xvs + yvs * yvs);
    double lonsun = rad(vs + ws);
    double xs = rs * std::cos(lonsun), ys = rs * std::sin(lonsun);

    const Elem& el = ELEMS[id];
    double N = rad(el.N0 + el.N1 * d), inc = rad(el.i0 + el.i1 * d), w = rad(el.w0 + el.w1 * d);
    double a = el.a, e = el.e0 + el.e1 * d, M = el.M0 + el.M1 * d;
    double E = solveKepler(M, e);
    double xv = a * (std::cos(E) - e), yv = a * std::sqrt(1 - e * e) * std::sin(E);
    double v = std::atan2(yv, xv), r = std::sqrt(xv * xv + yv * yv);
    double vw = v + w;
    double xh = r * (std::cos(N) * std::cos(vw) - std::sin(N) * std::sin(vw) * std::cos(inc));
    double yh = r * (std::sin(N) * std::cos(vw) + std::cos(N) * std::sin(vw) * std::cos(inc));
    double zh = r * std::sin(vw) * std::sin(inc);
    double lon = deg(std::atan2(yh, xh));
    double lat = deg(std::atan2(zh, std::sqrt(xh * xh + yh * yh)));
    if (id == JUPITER || id == SATURN) {
        double Mj = ELEMS[JUPITER].M0 + ELEMS[JUPITER].M1 * d;
        double Msat = ELEMS[SATURN].M0 + ELEMS[SATURN].M1 * d;
        if (id == JUPITER) {
            lon += -0.332 * std::sin(rad(2 * Mj - 5 * Msat - 67.6)) - 0.056 * std::sin(rad(2 * Mj - 2 * Msat + 21))
                 + 0.042 * std::sin(rad(3 * Mj - 5 * Msat + 21)) - 0.036 * std::sin(rad(Mj - 2 * Msat))
                 + 0.022 * std::cos(rad(Mj - Msat)) + 0.023 * std::sin(rad(2 * Mj - 3 * Msat + 52))
                 - 0.016 * std::sin(rad(Mj - 5 * Msat - 69));
        } else {
            lon += 0.812 * std::sin(rad(2 * Mj - 5 * Msat - 67.6)) - 0.229 * std::cos(rad(2 * Mj - 4 * Msat - 2))
                 + 0.119 * std::sin(rad(Mj - 2 * Msat - 3)) + 0.046 * std::sin(rad(2 * Mj - 6 * Msat - 69))
                 + 0.014 * std::sin(rad(Mj - 3 * Msat + 32));
            lat += -0.020 * std::cos(rad(2 * Mj - 4 * Msat - 2)) + 0.018 * std::sin(rad(2 * Mj - 6 * Msat - 49));
        }
        xh = r * std::cos(rad(lon)) * std::cos(rad(lat));
        yh = r * std::sin(rad(lon)) * std::cos(rad(lat));
        zh = r * std::sin(rad(lat));
    }
    double xg = xh + xs, yg = yh + ys, zg = zh;
    double ecl = rad(23.4393 - 3.563e-7 * d);
    double xe = xg, ye = yg * std::cos(ecl) - zg * std::sin(ecl), ze = yg * std::sin(ecl) + zg * std::cos(ecl);
    PlanetInfo p;
    p.id = id; p.name = NAMES[id];
    p.eq.ra = norm360(deg(std::atan2(ye, xe)));
    p.eq.dec = deg(std::atan2(ze, std::sqrt(xe * xe + ye * ye)));
    double s = std::sqrt(xg * xg + yg * yg + zg * zg);
    p.distAU = s; p.sunDistAU = r;
    double R = rs;
    double cosFV = (r * r + s * s - R * R) / (2 * r * s);
    double FV = deg(std::acos(std::max(-1.0, std::min(1.0, cosFV))));
    switch (id) {
        case MERCURY: p.mag = -0.36 + 5 * std::log10(r * s) + 0.027 * FV + 2.2e-13 * std::pow(FV, 6); break;
        case VENUS:   p.mag = -4.34 + 5 * std::log10(r * s) + 0.013 * FV + 4.2e-7 * FV * FV * FV; break;
        case MARS:    p.mag = -1.51 + 5 * std::log10(r * s) + 0.016 * FV; break;
        case JUPITER: p.mag = -9.25 + 5 * std::log10(r * s) + 0.014 * FV; break;
        default:      p.mag = -9.0 + 5 * std::log10(r * s) + 0.044 * FV; break;
    }
    Equatorial sunEq = sunRaw(unixSec).eq;
    p.elongation = angularSeparation(p.eq, sunEq);
    p.hor = toHorizontal(p.eq, unixSec, g);
    p.appAlt = p.hor.alt + refractionDeg(p.hor.alt);
    return p;
}

// ---------------------------------------------------------------- Stars
static double HM(double h, double m) { return h + m / 60.0; }
static double DM(double d, double m) { return d < 0 ? -(-d + m / 60.0) : (d + m / 60.0); }

const std::vector<Star>& stars() {
    static const std::vector<Star> S = {
        {"Sirius", HM(6,45.1), DM(-16,43), -1.46}, {"Canopus", HM(6,24.0), DM(-52,42), -0.74},
        {"Arcturus", HM(14,15.7), DM(19,11), -0.05}, {"Rigil Kent", HM(14,39.6), DM(-60,50), -0.01},
        {"Vega", HM(18,36.9), DM(38,47), 0.03}, {"Capella", HM(5,16.7), DM(46,0), 0.08},
        {"Rigel", HM(5,14.5), DM(-8,12), 0.13}, {"Procyon", HM(7,39.3), DM(5,13), 0.34},
        {"Achernar", HM(1,37.7), DM(-57,14), 0.46}, {"Betelgeuse", HM(5,55.2), DM(7,24), 0.50},
        {"Hadar", HM(14,3.8), DM(-60,22), 0.61}, {"Altair", HM(19,50.8), DM(8,52), 0.76},
        {"Acrux", HM(12,26.6), DM(-63,6), 0.77}, {"Aldebaran", HM(4,35.9), DM(16,31), 0.85},
        {"Antares", HM(16,29.4), DM(-26,26), 0.96}, {"Spica", HM(13,25.2), DM(-11,10), 0.97},
        {"Pollux", HM(7,45.3), DM(28,2), 1.14}, {"Fomalhaut", HM(22,57.7), DM(-29,37), 1.16},
        {"Deneb", HM(20,41.4), DM(45,17), 1.25}, {"Mimosa", HM(12,47.7), DM(-59,41), 1.25},
        {"Regulus", HM(10,8.4), DM(11,58), 1.35}, {"Adhara", HM(6,58.6), DM(-28,58), 1.50},
        {"Castor", HM(7,34.6), DM(31,53), 1.58}, {"Shaula", HM(17,33.6), DM(-37,6), 1.62},
        {"Gacrux", HM(12,31.2), DM(-57,7), 1.64}, {"Bellatrix", HM(5,25.1), DM(6,21), 1.64},
        {"Elnath", HM(5,26.3), DM(28,36), 1.65}, {"Miaplacidus", HM(9,13.2), DM(-69,43), 1.67},
        {"Alnilam", HM(5,36.2), DM(-1,12), 1.69}, {"Alnair", HM(22,8.2), DM(-46,58), 1.74},
        {"Alnitak", HM(5,40.8), DM(-1,57), 1.74}, {"Alioth", HM(12,54.0), DM(55,58), 1.76},
        {"Dubhe", HM(11,3.7), DM(61,45), 1.81}, {"Mirfak", HM(3,24.3), DM(49,52), 1.79},
        {"Wezen", HM(7,8.4), DM(-26,24), 1.83}, {"Sargas", HM(17,37.3), DM(-43,0), 1.86},
        {"Kaus Australis", HM(18,24.2), DM(-34,23), 1.85}, {"Avior", HM(8,22.5), DM(-59,31), 1.86},
        {"Alkaid", HM(13,47.5), DM(49,19), 1.86}, {"Menkalinan", HM(5,59.5), DM(44,57), 1.90},
        {"Atria", HM(16,48.7), DM(-69,2), 1.92}, {"Alhena", HM(6,37.7), DM(16,24), 1.93},
        {"Peacock", HM(20,25.6), DM(-56,44), 1.94}, {"Alsephina", HM(8,44.7), DM(-54,43), 1.93},
        {"Mirzam", HM(6,22.7), DM(-17,57), 1.98}, {"Alphard", HM(9,27.6), DM(-8,40), 1.98},
        {"Polaris", HM(2,31.8), DM(89,16), 1.98}, {"Hamal", HM(2,7.2), DM(23,28), 2.00},
        {"Diphda", HM(0,43.6), DM(-17,59), 2.04}, {"Nunki", HM(18,55.3), DM(-26,18), 2.05},
        {"Menkent", HM(14,6.7), DM(-36,22), 2.06}, {"Saiph", HM(5,47.8), DM(-9,40), 2.06},
        {"Alpheratz", HM(0,8.4), DM(29,5), 2.06}, {"Rasalhague", HM(17,34.9), DM(12,34), 2.08},
        {"Kochab", HM(14,50.7), DM(74,9), 2.08}, {"Denebola", HM(11,49.1), DM(14,34), 2.14},
        {"Mintaka", HM(5,32.0), -0.30, 2.23}, {"Eltanin", HM(17,56.6), DM(51,29), 2.23},
        {"Schedar", HM(0,40.5), DM(56,32), 2.24}, {"Naos", HM(8,3.6), DM(-40,0), 2.25},
        {"Enif", HM(21,44.2), DM(9,52), 2.40}, {"Markab", HM(23,5.5), DM(15,13), 2.49},
        {"Algol", HM(3,8.2), DM(40,57), 2.12}, {"Mizar", HM(13,23.9), DM(54,56), 2.23},
        {"Merak", HM(11,1.8), DM(56,23), 2.37}, {"Phecda", HM(11,53.8), DM(53,41), 2.44},
        {"Megrez", HM(12,15.4), DM(57,2), 3.31}, {"Imai", HM(12,15.1), DM(-58,45), 2.80},
        {"Meissa", HM(5,35.1), DM(9,56), 3.39}, {"Dschubba", HM(16,0.3), DM(-22,37), 2.32},
        {"Acrab", HM(16,5.4), DM(-19,48), 2.62}, {"Tau Sco", HM(16,35.9), DM(-28,13), 2.82},
        {"Larawag", HM(16,50.2), DM(-34,18), 2.29}, {"Mu Sco", HM(16,51.9), DM(-38,3), 3.08},
        {"Zeta Sco", HM(16,54.6), DM(-42,22), 3.62}, {"Eta Sco", HM(17,12.9), DM(-43,14), 3.33},
        {"Iota Sco", HM(17,47.6), DM(-40,8), 3.03}, {"Girtab", HM(17,42.5), DM(-39,2), 2.41},
    };
    return S;
}

const std::vector<Constellation>& constellations() {
    static std::vector<Constellation> C;
    if (!C.empty()) return C;
    auto idx = [](const char* n) -> int {
        const auto& s = stars();
        for (size_t i = 0; i < s.size(); ++i) if (std::strcmp(s[i].name, n) == 0) return (int)i;
        return 0;
    };
    auto chain = [&](std::initializer_list<const char*> names) {
        std::vector<ConstellationLine> L; const char* prev = nullptr;
        for (const char* n : names) { if (prev) L.push_back({idx(prev), idx(n)}); prev = n; }
        return L;
    };
    auto join = [](std::vector<ConstellationLine> a, const std::vector<ConstellationLine>& b) { a.insert(a.end(), b.begin(), b.end()); return a; };
    C.push_back({"Orion", join(join(join(chain({"Betelgeuse", "Meissa", "Bellatrix"}), chain({"Betelgeuse", "Alnitak", "Alnilam", "Mintaka", "Bellatrix"})),
                                   chain({"Alnitak", "Saiph", "Rigel", "Mintaka"})), {})});
    C.push_back({"Crux", join(chain({"Acrux", "Gacrux"}), chain({"Mimosa", "Imai"}))});
    C.push_back({"Big Dipper", chain({"Dubhe", "Merak", "Phecda", "Megrez", "Alioth", "Mizar", "Alkaid"}) });
    C.back().lines.push_back({idx("Megrez"), idx("Dubhe")});
    C.push_back({"Scorpius", chain({"Acrab", "Dschubba", "Antares", "Tau Sco", "Larawag", "Mu Sco", "Zeta Sco", "Eta Sco", "Sargas", "Iota Sco", "Girtab", "Shaula"})});
    C.push_back({"Summer Triangle", chain({"Vega", "Altair", "Deneb", "Vega"})});
    C.push_back({"Pointers", chain({"Rigil Kent", "Hadar"})});
    return C;
}

// ---------------------------------------------------------------- rise / set
void findCrossings(const std::function<double(double)>& f, double t0, double t1, double step,
                   std::vector<double>& risings, std::vector<double>& settings) {
    double tp = t0, fp = f(tp);
    for (double t = t0 + step; ; t += step) {
        if (t > t1) t = t1;
        double fc = f(t);
        if ((fp < 0) != (fc < 0)) {
            double a = tp, b = t, fa = fp;
            for (int k = 0; k < 40; ++k) {
                double m = 0.5 * (a + b), fm = f(m);
                if ((fa < 0) == (fm < 0)) { a = m; fa = fm; } else { b = m; }
            }
            double tc = 0.5 * (a + b);
            if (fp < 0) risings.push_back(tc); else settings.push_back(tc);
        }
        tp = t; fp = fc;
        if (t >= t1) break;
    }
}

static RiseSet firstOf(const std::vector<double>& r, const std::vector<double>& s) {
    RiseSet o;
    if (!r.empty()) { o.hasRise = true; o.rise = r.front(); }
    if (!s.empty()) { o.hasSet = true; o.set = s.front(); }
    return o;
}

RiseSet sunRiseSet(double t0, double t1, const Geo& g, double thr) {
    std::vector<double> r, s;
    findCrossings([&](double t) { return sunInfo(t, g).hor.alt - thr; }, t0, t1, 300.0, r, s);
    return firstOf(r, s);
}

RiseSet moonRiseSet(double t0, double t1, const Geo& g) {
    std::vector<double> r, s;
    findCrossings([&](double t) {
        MoonInfo m = moonInfo(t, g);
        double sd = deg(std::asin(1737.4 / m.distKm));
        return m.hor.alt + 0.5667 + sd;
    }, t0, t1, 300.0, r, s);
    return firstOf(r, s);
}

double solarNoon(double t0, const Geo& g) {
    double best = t0, bestAlt = -999;
    for (double t = t0; t <= t0 + 86400.0; t += 600.0) {
        double a = sunInfo(t, g).hor.alt;
        if (a > bestAlt) { bestAlt = a; best = t; }
    }
    double lo = std::max(t0, best - 600.0), hi = std::min(t0 + 86400.0, best + 600.0);
    for (int k = 0; k < 60; ++k) {
        double m1 = lo + (hi - lo) / 3.0, m2 = hi - (hi - lo) / 3.0;
        if (sunInfo(m1, g).hor.alt < sunInfo(m2, g).hor.alt) lo = m1; else hi = m2;
    }
    return 0.5 * (lo + hi);
}

DayEvents dayEvents(double t0, const Geo& g) {
    DayEvents e;
    double t1 = t0 + 86400.0;
    e.sun = sunRiseSet(t0, t1, g, -0.833);
    e.civil = sunRiseSet(t0, t1, g, -6.0);
    e.nautical = sunRiseSet(t0, t1, g, -12.0);
    e.astro = sunRiseSet(t0, t1, g, -18.0);
    e.golden = sunRiseSet(t0, t1, g, 6.0);
    e.blue = sunRiseSet(t0, t1, g, -4.0);
    e.noon = solarNoon(t0, g);
    e.noonAlt = sunInfo(e.noon, g).hor.alt;
    if (e.sun.hasRise && e.sun.hasSet) {
        double L = e.sun.set - e.sun.rise;
        if (L < 0) L += 86400.0;
        e.dayLengthSec = L;
    } else if (e.sun.hasRise) {
        e.dayLengthSec = std::max(0.0, std::min(86400.0, t1 - e.sun.rise));
    } else if (e.sun.hasSet) {
        e.dayLengthSec = std::max(0.0, std::min(86400.0, e.sun.set - t0));
    } else {
        e.dayLengthSec = (e.noonAlt > -0.833) ? 86400.0 : 0.0;
    }
    return e;
}

// Meeus, Astronomical Algorithms ch. 27 (equinoxes and solstices), good to about a minute.
double seasonInstant(int year, int which) {
    double Y = (year - 2000) / 1000.0;
    double Y2 = Y * Y, Y3 = Y2 * Y, Y4 = Y3 * Y;
    double jde0;
    switch (which & 3) {
        case 0: jde0 = 2451623.80984 + 365242.37404 * Y + 0.05169 * Y2 - 0.00411 * Y3 - 0.00057 * Y4; break;
        case 1: jde0 = 2451716.56767 + 365241.62603 * Y + 0.00325 * Y2 + 0.00888 * Y3 - 0.00030 * Y4; break;
        case 2: jde0 = 2451810.21715 + 365242.01767 * Y - 0.11575 * Y2 + 0.00337 * Y3 + 0.00078 * Y4; break;
        default: jde0 = 2451900.05952 + 365242.74049 * Y - 0.06223 * Y2 - 0.00823 * Y3 + 0.00032 * Y4; break;
    }
    static const double P[24][3] = {
        {485, 324.96, 1934.136}, {203, 337.23, 32964.467}, {199, 342.08, 20.186}, {182, 27.85, 445267.112},
        {156, 73.14, 45036.886}, {136, 171.52, 22518.443}, {77, 222.54, 65928.934}, {74, 296.72, 3034.906},
        {70, 243.58, 9037.513}, {58, 119.81, 33718.147}, {52, 297.17, 150.678}, {50, 21.02, 2281.226},
        {45, 247.54, 29929.562}, {44, 325.15, 31555.956}, {29, 60.93, 4443.417}, {18, 155.12, 67555.328},
        {17, 288.79, 4562.452}, {16, 198.04, 62894.029}, {14, 199.76, 31436.921}, {12, 95.39, 14577.848},
        {12, 287.11, 31931.756}, {12, 320.81, 34777.259}, {9, 227.73, 1222.114}, {8, 15.45, 16859.074}};
    double T = (jde0 - 2451545.0) / 36525.0;
    double W = rad(35999.373 * T - 2.47);
    double dl = 1.0 + 0.0334 * std::cos(W) + 0.0007 * std::cos(2 * W);
    double S = 0;
    for (const auto& p : P) S += p[0] * std::cos(rad(p[1] + p[2] * T));
    double jde = jde0 + 0.00001 * S / dl;
    double unixTT = (jde - 2440587.5) * 86400.0;
    return unixTT - deltaTSeconds(unixTT);
}

} // namespace astro
