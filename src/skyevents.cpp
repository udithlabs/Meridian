// Sky events: see skyevents.h. Portable C++17.
#include "skyevents.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <functional>

using namespace astro;

namespace skyev {
namespace {

const double DAY = 86400.0, HOUR = 3600.0;
const double AU_KM = 149597870.7, RE_KM = 6378.14, RSUN_KM = 696000.0, KMOON = 0.2725076;

std::string S(const char* f, ...) {
    char b[600]; va_list a; va_start(a, f); vsnprintf(b, sizeof b, f, a); va_end(a); return b;
}
long long ll(double t) { return (long long)std::llround(t); }
std::string TT(double t)  { return S("{t:%lld}", ll(t)); }     // clock time
[[maybe_unused]] std::string DD(double t)  { return S("{d:%lld}", ll(t)); }     // weekday + date
std::string DTT(double t) { return S("{dt:%lld}", ll(t)); }    // date + clock time

const char* compass(double az) {
    static const char* N[16] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
    return N[(int)std::floor(norm360(az) / 22.5 + 0.5) % 16];
}

// ---------------------------------------------------------------- numerics
template <class F> double goldenMin(F f, double a, double b, double tol) {
    const double gr = 0.6180339887498949;
    double c = b - gr * (b - a), d = a + gr * (b - a), fc = f(c), fd = f(d);
    for (int i = 0; i < 80 && b - a > tol; ++i) {
        if (fc < fd) { b = d; d = c; fd = fc; c = b - gr * (b - a); fc = f(c); }
        else { a = c; c = d; fc = fd; d = a + gr * (b - a); fd = f(d); }
    }
    return 0.5 * (a + b);
}
template <class F> double bisect(F f, double a, double b, double tol) {   // f(a) and f(b) of opposite sign
    double fa = f(a);
    for (int i = 0; i < 80 && b - a > tol; ++i) {
        double m = 0.5 * (a + b), fm = f(m);
        if ((fm <= 0) == (fa <= 0)) { a = m; fa = fm; } else b = m;
    }
    return 0.5 * (a + b);
}

double eclLonOf(const Equatorial& eq, double t) {
    double T = (julianDay(t) - 2451545.0) / 36525.0;
    double eps = rad(23.4392911 - 0.0130042 * T);
    double ra = rad(eq.ra), de = rad(eq.dec);
    return norm360(deg(std::atan2(std::sin(ra) * std::cos(eps) + std::tan(de) * std::sin(eps), std::cos(ra))));
}

Equatorial bodyEq(int id, double t, const Geo& g) { return id == MOON_ID ? moonInfo(t, g).eq : planetInfo(id, t, g).eq; }
const char* bodyName(int id) { return id == MOON_ID ? "Moon" : planetName(id); }

Equatorial midpoint(const std::vector<Equatorial>& v) {
    Equatorial m = v[0]; double sra = 0, sdec = 0;
    for (auto& e : v) { sra += norm180(e.ra - m.ra); sdec += e.dec; }
    m.ra = norm360(m.ra + sra / v.size()); m.dec = sdec / v.size();
    return m;
}

// Highest point reached by a sky position within +-12 h of tCentre while the Sun is below sunLimit.
bool bestTimeFor(const std::function<Equatorial(double)>& pos, double tCentre, const Geo& g, double sunLimit,
                 double& tBest, double& altBest, double& azBest) {
    bool found = false; altBest = -90;
    for (double t = tCentre - 12 * HOUR; t <= tCentre + 12 * HOUR; t += 600) {
        if (sunInfo(t, g).hor.alt > sunLimit) continue;
        Horizontal h = toHorizontal(pos(t), t, g);
        if (h.alt > altBest) { altBest = h.alt; azBest = h.az; tBest = t; found = true; }
    }
    return found;
}

std::string magStr(double m) { return S("%+.1f", m); }

// ---------------------------------------------------------------- meteor showers
struct Shower {
    const char* name; const char* con; const char* parent; const char* blurb;
    double lam, zhr, ra, dec, v;     // peak solar longitude (J2000), zenithal hourly rate, radiant (J2000), speed km/s
};
const Shower SH[] = {
    {"Quadrantids",        "Bootes",       "asteroid 2003 EH1",             "Sharp peak lasting only a few hours.",              283.16,  80, 230,  49, 41},
    {"Lyrids",             "Lyra",         "comet Thatcher (C/1861 G1)",    "Swift meteors, some leave glowing trains.",          32.32,  18, 271,  34, 49},
    {"Eta Aquariids",      "Aquarius",     "Halley's Comet",                "Fast, long-trailed streaks; best near the equator.",  45.5,  50, 338,  -1, 66},
    {"Southern Delta Aquariids", "Aquarius", "comet Machholz (96P)",        "Faint but steady, favours southern skies.",         125.0,  25, 340, -16, 41},
    {"Alpha Capricornids", "Capricornus",  "comet NEAT (169P)",             "Slow, bright fireballs, few in number.",            127.0,   5, 307, -10, 23},
    {"Perseids",           "Perseus",      "comet Swift-Tuttle",            "Bright meteors, many with lingering trains.",       140.0, 100,  48,  58, 59},
    {"Draconids",          "Draco",        "comet Giacobini-Zinner",        "Slow meteors; rate can surge unpredictably.",       195.4,  10, 262,  54, 20},
    {"Orionids",           "Orion",        "Halley's Comet",                "Fast and fine-grained, from Halley's dust.",        208.0,  20,  95,  16, 66},
    {"Southern Taurids",   "Taurus",       "comet Encke (2P)",              "Slow, fireball-rich, spread over weeks.",           223.0,   5,  52,  13, 27},
    {"Northern Taurids",   "Taurus",       "comet Encke (2P)",              "Slow, fireball-rich, spread over weeks.",           230.0,   5,  58,  22, 29},
    {"Leonids",            "Leo",          "comet Tempel-Tuttle (55P)",     "Very fast with bright trains; occasional storms.",  235.27, 15, 152,  22, 71},
    {"Geminids",           "Gemini",       "asteroid Phaethon (3200)",      "Slow, bright and often yellowish; the year's best.", 262.2, 150, 112,  33, 35},
    {"Ursids",             "Ursa Minor",   "comet Tuttle (8P)",             "Quiet shower near the northern pole.",              270.7,  10, 217,  76, 33},
    {"Alpha Centaurids",   "Centaurus",    "unknown",                       "Southern shower with bright fireballs.",            319.2,   6, 210, -59, 56},
};
const int NSH = (int)(sizeof(SH) / sizeof(SH[0]));

double sunLonJ2000(double t) {
    static Geo g0;
    double y = 1970.0 + t / (365.2425 * DAY);
    return norm360(sunInfo(t, g0).eclLon - 0.013970 * (y - 2000.0));
}
double peakTime(double lam, double now) {
    double t = now;
    for (int i = 0; i < 6; ++i) t += norm180(lam - sunLonJ2000(t)) / 0.985647 * DAY;
    return t;
}

void addMeteors(std::vector<Event>& out, double now, const Geo& g, const Options& o) {
    for (int i = 0; i < NSH; ++i) {
        const Shower& s = SH[i];
        double tp = peakTime(s.lam, now), days = (tp - now) / DAY;
        // peakTime() finds the nearest peak within +-182.6d; if it already passed, step forward one tropical year so 365-day searches find every shower
        if (days < -1.5) { tp = peakTime(s.lam, tp + 365.2422 * DAY); days = (tp - now) / DAY; }
        if (days < -1.5 || days > o.windowDays) continue;
        Event e; e.kind = METEOR; e.shower = i;
        e.title = S("%s meteor shower", s.name);
        e.tPeak = tp; e.tStart = tp - o.windowDays * DAY; e.tEnd = tp + 1.5 * DAY;
        Equatorial rd = precessJ2000({s.ra, s.dec}, tp);
        e.raDeg = rd.ra; e.decDeg = rd.dec; e.zhr = s.zhr; e.speed = s.v;
        double tb = tp, ab = -90, zb = 0;
        bool dark = bestTimeFor([&](double) { return rd; }, tp, g, -12.0, tb, ab, zb);
        e.visible = dark && ab > 5.0;
        double rate = s.zhr * std::sin(rad(std::max(ab, 0.0))) * 0.7;
        int n = rate >= 20 ? (int)(std::round(rate / 5.0) * 5) : (int)std::round(rate);
        if (e.visible) e.subtitle = S("Peak %s  \xC2\xB7  up to ~%d meteors/hr", DTT(tp).c_str(), std::max(n, 1));
        else e.subtitle = S("Peak %s  \xC2\xB7  radiant stays low from here", DTT(tp).c_str());
        e.details.push_back({"Peak", DTT(tp)});
        if (e.visible) {
            e.details.push_back({"Best viewing", S("%s, radiant %.0f\xC2\xB0 high in the %s", TT(tb).c_str(), ab, compass(zb))});
            MoonInfo m = moonInfo(tb, g);
            std::string mv;
            if (m.hor.alt <= 0) mv = S("%.0f%% lit, below the horizon \xE2\x80\x94 dark skies", m.illum * 100);
            else {
                double w = m.illum;
                const char* q = w < 0.15 ? "little interference" : w < 0.4 ? "some interference" : w < 0.7 ? "washes out faint ones" : "strong glare";
                mv = S("%.0f%% lit, %.0f\xC2\xB0 up \xE2\x80\x94 %s", w * 100, m.hor.alt, q);
            }
            e.details.push_back({"Moon", mv});
        } else {
            e.details.push_back({"Visibility", "Radiant never climbs high enough from your latitude"});
        }
        e.details.push_back({"Rate", S("ZHR %.0f  \xC2\xB7  radiant in %s", s.zhr, s.con)});
        e.details.push_back({"Origin", s.parent});
        e.details.push_back({"Speed", S("%.0f km/s  \xC2\xB7  %s", s.v, s.blurb)});
        out.push_back(e);
    }
}

// ---------------------------------------------------------------- planets
struct Cand { std::vector<int> ids; double t = 0; };

Event makeConjunction(const std::vector<int>& ids, double tp, bool withMoon, const Geo& g) {
    Event e; e.kind = withMoon ? MOON_NEAR : CONJUNCTION; e.tPeak = tp;
    double halfSpan = withMoon ? 12 * HOUR : 36 * HOUR;
    e.tStart = tp - halfSpan; e.tEnd = tp + halfSpan;
    std::vector<Equatorial> pts;
    for (int id : ids) { pts.push_back(bodyEq(id, tp, g)); e.bodies[e.nBodies++] = id; }
    double sep = 0;
    for (size_t i = 0; i < pts.size(); ++i) for (size_t j = i + 1; j < pts.size(); ++j) sep = std::max(sep, angularSeparation(pts[i], pts[j]));
    e.sepDeg = sep;
    // evening or morning sky: east of the Sun means evening
    double lonSun = sunInfo(tp, g).eclLon; bool evening = true;
    for (int id : ids) if (id != MOON_ID) { evening = norm180(eclLonOf(bodyEq(id, tp, g), tp) - lonSun) > 0; break; }
    if (withMoon) evening = moonInfo(tp, g).elongation < 180.0;
    std::string sepS = sep < 1.0 ? S("%.2f\xC2\xB0", sep) : S("%.1f\xC2\xB0", sep);
    if (ids.size() == 2 && withMoon) {
        e.title = S("Moon near %s", bodyName(ids[1] == MOON_ID ? ids[0] : ids[1]));
    } else if (ids.size() == 2) {
        e.title = S("%s and %s", bodyName(ids[0]), bodyName(ids[1]));
    } else {
        e.title = S("%s, %s and %s gather", bodyName(ids[0]), bodyName(ids[1]), bodyName(ids[2]));
    }
    e.subtitle = S("%s apart  \xC2\xB7  %s sky", sepS.c_str(), evening ? "evening" : "morning");
    e.details.push_back({ids.size() > 2 ? "Closest together" : "Closest approach", S("%s  \xC2\xB7  %s apart", DTT(tp).c_str(), sepS.c_str())});
    e.details.push_back({"Apparent gap", S("%.1f Moon-widths (the Moon is about 0.5\xC2\xB0 across)", sep / 0.52)});
    std::string mags;
    for (int id : ids) {
        if (id == MOON_ID) { MoonInfo m = moonInfo(tp, g); mags += S("%sMoon %.0f%% lit", mags.empty() ? "" : "  \xC2\xB7  ", m.illum * 100); }
        else mags += S("%s%s %s", mags.empty() ? "" : "  \xC2\xB7  ", planetName(id), magStr(planetInfo(id, tp, g).mag).c_str());
    }
    e.details.push_back({"Brightness", mags});
    double tb = tp, ab = -90, zb = 0;
    bool seen = bestTimeFor([&](double t) {
        std::vector<Equatorial> p; for (int id : ids) p.push_back(bodyEq(id, t, g)); return midpoint(p);
    }, tp, g, -4.0, tb, ab, zb);
    e.visible = seen && ab > 3.0;
    if (e.visible) e.details.push_back({"Best view", S("%s, %.0f\xC2\xB0 high in the %s", TT(tb).c_str(), ab, compass(zb))});
    else e.details.push_back({"Best view", "Stays too close to the horizon from here"});
    e.details.push_back({"Tip", sep < 0.6 ? "Close enough to share a telescope view"
                                : sep < 2.0 ? "Both fit in a single binocular view" : "Look for the pair with the naked eye"});
    return e;
}

void addPlanets(std::vector<Event>& out, double now, const Geo& g, const Options& o) {
    const double step = 20 * 60.0;
    double t0 = now - 1.0 * DAY, t1 = now + o.windowDays * DAY;
    int n = (int)((t1 - t0) / step) + 3;
    std::vector<double> ts(n);
    std::vector<Equatorial> eq[6];
    for (auto& v : eq) v.resize(n);
    for (int i = 0; i < n; ++i) {
        ts[i] = t0 - step + i * step;
        for (int p = 0; p < PLANET_COUNT; ++p) eq[p][i] = planetInfo(p, ts[i], g).eq;
        eq[5][i] = moonInfo(ts[i], g).eq;
    }
    std::vector<Cand> pp, mp;
    for (int a = 0; a < 6; ++a) for (int b = a + 1; b < 6; ++b) {
        bool moon = (b == 5);
        double thr = moon ? 3.5 : 4.0;
        int ia = a, ib = moon ? MOON_ID : b;
        std::vector<double> s(n);
        for (int i = 0; i < n; ++i) s[i] = angularSeparation(eq[a][i], eq[b][i]);
        for (int i = 1; i < n - 1; ++i) {
            if (!(s[i] <= s[i - 1] && s[i] < s[i + 1]) || s[i] > thr + 1.5) continue;
            double tm = goldenMin([&](double x) { return angularSeparation(bodyEq(ia, x, g), bodyEq(ib, x, g)); }, ts[i - 1], ts[i + 1], 5.0);
            if (tm < t0 || tm > t1) continue;
            double sp = angularSeparation(bodyEq(ia, tm, g), bodyEq(ib, tm, g));
            if (sp > thr) continue;
            bool ok = planetInfo(a, tm, g).elongation > 10.0;
            if (moon) { double me = moonInfo(tm, g).elongation; ok = ok && std::min(me, 360 - me) > 12.0; }
            else ok = ok && planetInfo(b, tm, g).elongation > 10.0;
            if (!ok) continue;
            Cand c; c.ids = {ia, ib}; c.t = tm;
            (moon ? mp : pp).push_back(c);
        }
    }
    // planets meeting in a group of three: merge pairs that share a body and happen within two days, and absorb any third pair already inside that triple
    for (size_t i = 0; i < pp.size(); ++i) {
        bool merged = true;
        while (merged) {
            merged = false;
            for (size_t j = i + 1; j < pp.size();) {
                bool share = false, subset = true;
                for (int y : pp[j].ids) {
                    bool inI = std::find(pp[i].ids.begin(), pp[i].ids.end(), y) != pp[i].ids.end();
                    if (inI) share = true; else subset = false;
                }
                if (std::fabs(pp[i].t - pp[j].t) < 2 * DAY && (subset || (share && pp[i].ids.size() < 3))) {
                    for (int y : pp[j].ids) if (std::find(pp[i].ids.begin(), pp[i].ids.end(), y) == pp[i].ids.end()) pp[i].ids.push_back(y);
                    pp[i].t = 0.5 * (pp[i].t + pp[j].t);
                    pp.erase(pp.begin() + j);
                    merged = true;
                } else ++j;
            }
        }
    }
    for (auto& c : pp) out.push_back(makeConjunction(c.ids, c.t, false, g));
    for (auto& c : mp) out.push_back(makeConjunction(c.ids, c.t, true, g));

    // ---- oppositions
    const int outer[3] = {MARS, JUPITER, SATURN};
    for (int id : outer) {
        auto f = [&](double t) { return norm180(eclLonOf(planetInfo(id, t, g).eq, t) - sunInfo(t, g).eclLon - 180.0); };
        double ta = t0, fa = f(ta);
        for (double tb2 = ta + 3 * HOUR; tb2 <= t1 + 3 * HOUR; tb2 += 3 * HOUR) {
            double fb = f(tb2);
            if (std::fabs(fa) < 90 && std::fabs(fb) < 90 && ((fa <= 0) != (fb <= 0))) {
                double tp = bisect(f, ta, tb2, 30.0);
                if (tp >= t0 && tp <= t1) {
                    PlanetInfo p = planetInfo(id, tp, g);
                    Event e; e.kind = OPPOSITION; e.tPeak = tp; e.tStart = tp - 2 * DAY; e.tEnd = tp + 2 * DAY;
                    e.bodies[e.nBodies++] = id;
                    e.title = S("%s at opposition", planetName(id));
                    e.subtitle = S("Closest and brightest of the year  \xC2\xB7  mag %s", magStr(p.mag).c_str());
                    e.details.push_back({"Opposition", DTT(tp)});
                    e.details.push_back({"Distance", S("%.2f AU (%.0f million km)", p.distAU, p.distAU * AU_KM / 1e6)});
                    e.details.push_back({"Brightness", S("magnitude %s", magStr(p.mag).c_str())});
                    double tb = tp, ab = -90, zb = 0;
                    bool seen = bestTimeFor([&](double t) { return planetInfo(id, t, g).eq; }, tp, g, -12.0, tb, ab, zb);
                    e.visible = seen && ab > 5;
                    if (seen) e.details.push_back({"Best view", S("%s, %.0f\xC2\xB0 high in the %s", TT(tb).c_str(), ab, compass(zb))});
                    e.details.push_back({"What it means", "The planet sits opposite the Sun: rises at sunset, up all night"});
                    out.push_back(e);
                }
            }
            ta = tb2; fa = fb;
        }
    }

    // ---- greatest elongations of Mercury and Venus
    for (int id = MERCURY; id <= VENUS; ++id) {
        auto el = [&](double t) { return norm180(eclLonOf(planetInfo(id, t, g).eq, t) - sunInfo(t, g).eclLon); };
        const double st = 3 * HOUR;
        int m = (int)((t1 - t0) / st) + 3;
        std::vector<double> v(m);
        for (int i = 0; i < m; ++i) v[i] = -std::fabs(el(t0 - st + i * st));
        for (int i = 1; i < m - 1; ++i) {
            if (!(v[i] <= v[i - 1] && v[i] < v[i + 1])) continue;
            double ta = t0 - st + (i - 1) * st, tb2 = t0 - st + (i + 1) * st;
            double tp = goldenMin([&](double x) { return -std::fabs(el(x)); }, ta, tb2, 60.0);
            if (tp < t0 || tp > t1) continue;
            double e0 = el(tp); bool east = e0 > 0;
            PlanetInfo p = planetInfo(id, tp, g);
            Event e; e.kind = ELONGATION; e.tPeak = tp; e.tStart = tp - 2 * DAY; e.tEnd = tp + 2 * DAY;
            e.bodies[e.nBodies++] = id; e.sepDeg = std::fabs(e0);
            e.title = S("%s at greatest %s elongation", planetName(id), east ? "eastern" : "western");
            e.subtitle = S("%.1f\xC2\xB0 from the Sun  \xC2\xB7  %s sky", std::fabs(e0), east ? "evening" : "morning");
            e.details.push_back({"Greatest elongation", S("%s  \xC2\xB7  %.1f\xC2\xB0 %s of the Sun", DTT(tp).c_str(), std::fabs(e0), east ? "east" : "west")});
            e.details.push_back({"Brightness", S("magnitude %s", magStr(p.mag).c_str())});
            double tb = tp, ab = -90, zb = 0;
            bool seen = bestTimeFor([&](double t) { return planetInfo(id, t, g).eq; }, tp, g, -4.0, tb, ab, zb);
            e.visible = seen && ab > 3;
            if (e.visible) e.details.push_back({"Best view", S("%s, %.0f\xC2\xB0 high in the %s", TT(tb).c_str(), ab, compass(zb))});
            else e.details.push_back({"Best view", "Low from here, hugging the horizon"});
            e.details.push_back({"What it means", east ? "Farthest the planet strays from the Sun: the best evening sighting" : "Farthest the planet strays from the Sun: the best morning sighting"});
            out.push_back(e);
        }
    }

    // ---- planet parade: four or more planets above the horizon together in twilight
    // Track discrete twilight windows (gaps > 6h split separate evenings/mornings) and pick the highest-count, highest-altitude viewing instant.
    int bestN = 0; double bestScore = -1e9, bestT = 0, bestStart = 0, bestEnd = 0;
    double curStart = 0, curEnd = 0, curBestScore = -1e9, curBestT = 0; int curBestN = 0;
    auto flushParade = [&]() {
        if (curBestN >= 4 && (curBestN > bestN || (curBestN == bestN && curBestScore > bestScore))) {
            bestN = curBestN; bestScore = curBestScore; bestT = curBestT; bestStart = curStart; bestEnd = curEnd;
        }
        curStart = curEnd = 0; curBestN = 0; curBestScore = -1e9;
    };
    for (double t = now - 2 * HOUR; t <= t1; t += 20 * 60.0) {
        double sa = sunInfo(t, g).hor.alt;
        if (sa > -3.0 || sa < -18.0) continue;
        int cnt = 0; double sumAlt = 0;
        for (int p = 0; p < PLANET_COUNT; ++p) {
            PlanetInfo pi = planetInfo(p, t, g);
            if (pi.appAlt > 3.0 && pi.elongation > 10.0 && pi.mag < 3.0) { ++cnt; sumAlt += pi.appAlt; }
        }
        if (cnt >= 4) {
            if (curStart > 0 && t - curEnd > 6 * HOUR) flushParade();
            if (curStart == 0) curStart = t;
            curEnd = t;
            double score = sumAlt / cnt;
            if (cnt > curBestN || (cnt == curBestN && score > curBestScore)) { curBestN = cnt; curBestScore = score; curBestT = t; }
        }
    }
    flushParade();
    if (bestN >= 4) {
        Event e; e.kind = PARADE; e.tPeak = bestT; e.tStart = bestStart - 10 * 60.0; e.tEnd = bestEnd + 10 * 60.0;
        std::vector<std::pair<double, int>> L;
        for (int p = 0; p < PLANET_COUNT; ++p) {
            PlanetInfo pi = planetInfo(p, bestT, g);
            if (pi.appAlt > 3.0 && pi.elongation > 10.0 && pi.mag < 3.0) {
                e.bodies[e.nBodies++] = p;
                L.push_back({eclLonOf(pi.eq, bestT), p});
            }
        }
        std::sort(L.begin(), L.end());
        double gap = 0;
        for (size_t i = 0; i < L.size(); ++i) gap = std::max(gap, norm360(L[(i + 1) % L.size()].first - L[i].first));
        double span = 360.0 - gap;
        e.sepDeg = span; e.visible = true;
        e.title = bestN >= 5 ? "Five-planet parade" : "Four-planet parade";
        std::string names;
        for (int i = 0; i < e.nBodies; ++i) names += S("%s%s", i ? ", " : "", planetName(e.bodies[i]));
        e.subtitle = S("%s  \xC2\xB7  spread over %.0f\xC2\xB0", names.c_str(), span);
        bool eve = sunInfo(bestT, g).hor.alt < 0 && norm180(eclLonOf(planetInfo(e.bodies[0], bestT, g).eq, bestT) - sunInfo(bestT, g).eclLon) > 0;
        e.details.push_back({"Best view", S("%s  \xC2\xB7  %s twilight", TT(bestT).c_str(), eve ? "evening" : "morning")});
        for (int i = 0; i < e.nBodies; ++i) {
            PlanetInfo pi = planetInfo(e.bodies[i], bestT, g);
            e.details.push_back({planetName(e.bodies[i]), S("mag %s  \xC2\xB7  %.0f\xC2\xB0 high in the %s", magStr(pi.mag).c_str(), pi.appAlt, compass(pi.hor.az))});
        }
        e.details.push_back({"What it means", "The planets trace the ecliptic, the Sun's path, all at once"});
        out.push_back(e);
    }
}

// ---------------------------------------------------------------- eclipses
Equatorial topocentric(const Equatorial& eq, double distKm, double t, const Geo& g) {
    double phi = rad(g.lat);
    double u = std::atan(0.99664719 * std::tan(phi));
    double rhoSin = 0.99664719 * std::sin(u), rhoCos = std::cos(u);
    double sinPi = RE_KM / distKm;
    double H = rad(gmstDeg(t) + g.lon - eq.ra), dec = rad(eq.dec);
    double dRa = std::atan2(-rhoCos * sinPi * std::sin(H), std::cos(dec) - rhoCos * sinPi * std::cos(H));
    double decP = std::atan2((std::sin(dec) - rhoSin * sinPi) * std::cos(dRa), std::cos(dec) - rhoCos * sinPi * std::cos(H));
    Equatorial r; r.ra = norm360(eq.ra + deg(dRa)); r.dec = deg(decP);
    return r;
}

double obscuration(double rs, double rm, double d) {
    if (d >= rs + rm) return 0;
    if (d <= std::fabs(rs - rm)) return rm >= rs ? 1.0 : (rm * rm) / (rs * rs);
    double a = rs * rs * std::acos((d * d + rs * rs - rm * rm) / (2 * d * rs));
    double b = rm * rm * std::acos((d * d + rm * rm - rs * rs) / (2 * d * rm));
    double c = 0.5 * std::sqrt((-d + rs + rm) * (d + rs - rm) * (d - rs + rm) * (d + rs + rm));
    return (a + b - c) / (PI * rs * rs);
}

struct LGeo { double d, ru, rp, sm, dx, dy; };
LGeo lunarGeom(double t, const Geo& g) {
    MoonInfo m = moonInfo(t, g); SunInfo s = sunInfo(t, g);
    double R = s.distAU;
    // the shadow axis follows the geometric Sun: undo the 20.5" of aberration
    double dl = 20.4898 / 3600.0 / R;
    double T = (julianDay(t) - 2451545.0) / 36525.0, eps = 23.4392911 - 0.0130042 * T;
    Equatorial a = eclipticToEquatorial(s.eclLon, 0, eps), b = eclipticToEquatorial(s.eclLon + dl, 0, eps);
    double sra = s.eq.ra + norm180(b.ra - a.ra), sdec = s.eq.dec + (b.dec - a.dec);
    double shra = norm360(sra + 180.0), shdec = -sdec;
    LGeo L;
    L.dx = norm180(shra - m.eq.ra) * std::cos(rad(m.eq.dec));
    L.dy = shdec - m.eq.dec;
    L.d = std::sqrt(L.dx * L.dx + L.dy * L.dy);
    double piM = deg(std::asin(RE_KM / m.distKm)), piS = 8.794 / 3600.0 / R, sS = 959.63 / 3600.0 / R;
    L.ru = 1.0135 * (piM + piS - sS);   // Danjon-style atmospheric enlargement, calibrated to NASA magnitudes
    L.rp = 1.0135 * (piM + piS + sS);
    L.sm = deg(std::asin(KMOON * std::sin(rad(piM))));
    return L;
}

struct SGeo { double sep, sunR, moonR, dx, dy, sunAlt; };
SGeo solarTopo(double t, const Geo& g) {
    MoonInfo m = moonInfo(t, g); SunInfo s = sunInfo(t, g);
    Equatorial me = topocentric(m.eq, m.distKm, t, g), se = topocentric(s.eq, s.distAU * AU_KM, t, g);
    SGeo r;
    r.dx = norm180(me.ra - se.ra) * std::cos(rad(se.dec));
    r.dy = me.dec - se.dec;
    r.sep = std::sqrt(r.dx * r.dx + r.dy * r.dy);
    r.sunR = 959.63 / 3600.0 / s.distAU;
    double sinPi = RE_KM / m.distKm;
    r.moonR = deg(std::asin(0.272281 * sinPi)) * (1.0 + std::sin(rad(m.hor.alt)) * sinPi);
    r.sunAlt = s.hor.alt;
    return r;
}

double geocentricSep(double t) {
    static Geo g0;
    return angularSeparation(moonInfo(t, g0).eq, sunInfo(t, g0).eq);
}

const double ECL_MAX_LAT = 1.65;   // Moon's ecliptic latitude (degrees) beyond which no eclipse is possible

void addLunar(std::vector<Event>& out, double now, const Geo& g, const Options& o) {
    double a = now - 12 * HOUR, b = now + o.windowDays * DAY + 12 * HOUR;
    for (double t = nextPhaseTime(a - 2 * DAY, 2); t <= b; t = nextPhaseTime(t + 5 * DAY, 2)) {
        if (t < a) continue;
        if (std::fabs(moonInfo(t, g).eclLat) > ECL_MAX_LAT) continue;
        double tm = goldenMin([&](double x) { return lunarGeom(x, g).d; }, t - 4 * HOUR, t + 4 * HOUR, 0.5);
        LGeo L = lunarGeom(tm, g);
        double um = (L.ru + L.sm - L.d) / (2 * L.sm), pm = (L.rp + L.sm - L.d) / (2 * L.sm);
        if (pm <= 0.01) continue;
        Event e; e.kind = LUNAR_ECLIPSE; e.tPeak = tm; e.magnitude = um;
        e.type = um >= 1.0 ? 2 : um > 0 ? 1 : 0;
        auto contact = [&](double thr, bool before) {
            auto f = [&](double x) { return lunarGeom(x, g).d - thr; };
            return before ? bisect(f, tm - 4.5 * HOUR, tm, 0.5) : bisect(f, tm, tm + 4.5 * HOUR, 0.5);
        };
        double p1 = contact(L.rp + L.sm, true), p4 = contact(L.rp + L.sm, false);
        double u1 = 0, u4 = 0, t2 = 0, t3 = 0;
        if (e.type >= 1) { u1 = contact(L.ru + L.sm, true); u4 = contact(L.ru + L.sm, false); }
        if (e.type == 2) { t2 = contact(L.ru - L.sm, true); t3 = contact(L.ru - L.sm, false); }
        e.tStart = p1; e.tEnd = p4;
        if (e.tEnd + 3 * HOUR < now || e.tStart > now + o.windowDays * DAY) continue;
        // visible if the Moon is above the horizon during the part you can notice
        double vs = e.type ? u1 : p1, ve = e.type ? u4 : p4;
        for (double x = vs; x <= ve; x += 300) if (moonInfo(x, g).hor.alt > 0) { e.visible = true; break; }
        const char* tn = e.type == 2 ? "Total" : e.type == 1 ? "Partial" : "Penumbral";
        e.title = S("%s lunar eclipse", tn);
        MoonInfo mm = moonInfo(tm, g);
        if (e.type == 2) e.subtitle = S("Totality %s\xE2\x80\x93%s  \xC2\xB7  %.0f min", TT(t2).c_str(), TT(t3).c_str(), (t3 - t2) / 60.0);
        else if (e.type == 1) e.subtitle = S("%.0f%% of the Moon in the umbra at %s", std::min(um, 1.0) * 100, TT(tm).c_str());
        else e.subtitle = S("Faint shading at %s  \xC2\xB7  magnitude %.2f", TT(tm).c_str(), pm);
        e.details.push_back({"Type", S("%s  \xC2\xB7  umbral magnitude %.2f", tn, std::max(um, 0.0))});
        e.details.push_back({"Penumbral begins", TT(p1)});
        if (e.type >= 1) e.details.push_back({"Partial begins", TT(u1)});
        if (e.type == 2) e.details.push_back({"Totality", S("%s\xE2\x80\x93%s", TT(t2).c_str(), TT(t3).c_str())});
        e.details.push_back({"Greatest", S("%s  \xC2\xB7  Moon %.0f\xC2\xB0 %s", TT(tm).c_str(), mm.hor.alt, mm.hor.alt > 0 ? compass(mm.hor.az) : "below the horizon")});
        if (e.type >= 1) e.details.push_back({"Partial ends", TT(u4)});
        e.details.push_back({"Penumbral ends", TT(p4)});
        if (mm.hor.alt > 0) e.details.push_back({"From here", "Visible \xE2\x80\x94 no equipment needed"});
        else if (e.visible) e.details.push_back({"From here", "Partly visible \xE2\x80\x94 the Moon is up for only part of it"});
        else e.details.push_back({"From here", "Not visible \xE2\x80\x94 the Moon is below the horizon"});
        if (e.type == 0) e.details.push_back({"Tip", "A penumbral eclipse is a subtle dimming; look for a darker edge"});
        e.bodies[0] = MOON_ID; e.nBodies = 1;
        out.push_back(e);
    }
}

void addSolar(std::vector<Event>& out, double now, const Geo& g, const Options& o) {
    double a = now - 12 * HOUR, b = now + o.windowDays * DAY + 12 * HOUR;
    for (double t = nextPhaseTime(a - 2 * DAY, 0); t <= b; t = nextPhaseTime(t + 5 * DAY, 0)) {
        if (t < a) continue;
        if (std::fabs(moonInfo(t, g).eclLat) > 1.65) continue;
        // geocentric greatest eclipse and gamma
        double tG = goldenMin([&](double x) { return geocentricSep(x); }, t - 5 * HOUR, t + 5 * HOUR, 0.5);
        MoonInfo mg = moonInfo(tG, g); SunInfo sg = sunInfo(tG, g);
        double theta = rad(geocentricSep(tG));
        double Dm = mg.distKm, Ds = sg.distAU * AU_KM;
        double Dsm = std::sqrt(Ds * Ds + Dm * Dm - 2 * Ds * Dm * std::cos(theta));
        double gam = Dm * Ds * std::sin(theta) / Dsm / RE_KM;
        if (mg.eq.dec < sg.eq.dec) gam = -gam;
        double rm = KMOON * RE_KM;
        double rpen = rm + Dm * (RSUN_KM + rm) / Dsm;                 // penumbra radius in the fundamental plane (km)
        if (std::fabs(gam) > 1.0 + rpen / RE_KM) continue;
        double La = rm * Dsm / (RSUN_KM - rm), zApex = La - Dm;      // apex of the umbral cone relative to the plane
        double h = RE_KM * std::sqrt(std::max(0.0, 1.0 - std::min(1.0, gam * gam)));
        bool central = std::fabs(gam) < 0.9972 + std::fabs((rm - Dm * (RSUN_KM - rm) / Dsm) / RE_KM);
        int type = 0;
        if (central) type = zApex >= 0 ? 2 : (zApex + h <= 0 ? 1 : 3);   // 3 = hybrid
        const char* tn = type == 2 ? "Total" : type == 1 ? "Annular" : type == 3 ? "Hybrid" : "Partial";

        // what you see from the observer's location
        double tm = goldenMin([&](double x) { return solarTopo(x, g).sep; }, tG - 5 * HOUR, tG + 5 * HOUR, 0.5);
        SGeo L = solarTopo(tm, g);
        bool local = L.sep < L.sunR + L.moonR;
        Event e; e.kind = SOLAR_ECLIPSE; e.gamma = gam;
        e.type = type == 2 ? 2 : type == 1 ? 1 : type == 3 ? 2 : 0;
        e.bodies[0] = MOON_ID; e.nBodies = 1;
        e.title = S("%s solar eclipse", tn);
        e.sepDeg = L.sep;
        double c1 = 0, c4 = 0; bool sunUp = false;
        if (local) {
            auto f1 = [&](double x) { SGeo q = solarTopo(x, g); return q.sep - (q.sunR + q.moonR); };
            c1 = bisect(f1, tm - 4.5 * HOUR, tm, 0.5); c4 = bisect(f1, tm, tm + 4.5 * HOUR, 0.5);
            for (double x = c1; x <= c4; x += 60) if (sunInfo(x, g).hor.alt > -0.6) { sunUp = true; break; }
        }
        if (local && sunUp) {
            e.tStart = c1; e.tEnd = c4; e.tPeak = tm;
            e.magnitude = (L.sunR + L.moonR - L.sep) / (2 * L.sunR);
            e.obscuration = obscuration(L.sunR, L.moonR, L.sep);
            e.visible = true;
            bool ctr = L.sep <= std::fabs(L.sunR - L.moonR);
            int ltype = !ctr ? 0 : (L.moonR >= L.sunR ? 2 : 1);
            e.type = ltype;
            if (e.tEnd + 3 * HOUR < now || e.tStart > now + o.windowDays * DAY) continue;
            const char* lt = ltype == 2 ? "total" : ltype == 1 ? "annular" : "partial";
            e.title = S("%s solar eclipse", ltype == 2 ? "Total" : ltype == 1 ? "Annular" : "Partial");
            if (ltype == 0) e.subtitle = S("Partial here  \xC2\xB7  %.0f%% of the Sun covered at %s", e.obscuration * 100, TT(tm).c_str());
            else e.subtitle = S("%s here  \xC2\xB7  %s at %s", ltype == 2 ? "Total" : "Annular", ltype == 2 ? "totality" : "ring", TT(tm).c_str());
            e.details.push_back({"Type", S("%s worldwide  \xC2\xB7  %s from here", tn, lt)});
            e.details.push_back({"Begins", TT(c1)});
            if (ctr) {
                auto f2 = [&](double x) { SGeo q = solarTopo(x, g); return q.sep - std::fabs(q.sunR - q.moonR); };
                double c2 = bisect(f2, tm - 4.5 * HOUR, tm, 0.5), c3 = bisect(f2, tm, tm + 4.5 * HOUR, 0.5);
                e.details.push_back({ltype == 2 ? "Totality" : "Annular phase", S("%s\xE2\x80\x93%s  \xC2\xB7  %.0f s", TT(c2).c_str(), TT(c3).c_str(), c3 - c2)});
            }
            Horizontal hz = toHorizontal(sunInfo(tm, g).eq, tm, g);
            e.details.push_back({"Maximum", S("%s  \xC2\xB7  %.0f%% covered  \xC2\xB7  Sun %.0f\xC2\xB0 high in the %s", TT(tm).c_str(), e.obscuration * 100, L.sunAlt, compass(hz.az))});
            e.details.push_back({"Ends", TT(c4)});
            e.details.push_back({"Safety", "Never look at the Sun directly: use certified eclipse glasses"});
        } else {
            e.tPeak = tG; e.tStart = tG - 3 * HOUR; e.tEnd = tG + 3 * HOUR;
            if (e.tEnd + 3 * HOUR < now || e.tStart > now + o.windowDays * DAY) continue;
            e.visible = false;
            e.subtitle = "Not visible from your location";
            e.details.push_back({"Type", S("%s solar eclipse worldwide", tn)});
            e.details.push_back({"Greatest eclipse", S("%s  \xC2\xB7  gamma %+.3f", TT(tG).c_str(), gam)});
            if (local) { e.subtitle = "Happens while the Sun is below your horizon"; e.details.push_back({"From here", "Not visible \xE2\x80\x94 the Sun is below the horizon during the eclipse"}); }
            else e.details.push_back({"From here", S("Not visible \xE2\x80\x94 the Moon passes %.1f\xC2\xB0 from the Sun's disc", std::max(0.0, L.sep - L.sunR - L.moonR))});
        }
        out.push_back(e);
    }
}

} // namespace

// ---------------------------------------------------------------- public
std::vector<Event> find(double now, const Geo& g, const Options& o) {
    std::vector<Event> ev;
    if (o.meteors) addMeteors(ev, now, g, o);
    if (o.planets) addPlanets(ev, now, g, o);
    if (o.eclipses) { addLunar(ev, now, g, o); addSolar(ev, now, g, o); }
    std::stable_sort(ev.begin(), ev.end(), [&](const Event& a, const Event& b) {
        bool aa = a.active(now) && (a.kind == SOLAR_ECLIPSE || a.kind == LUNAR_ECLIPSE);
        bool bb = b.active(now) && (b.kind == SOLAR_ECLIPSE || b.kind == LUNAR_ECLIPSE);
        if (aa != bb) return aa;
        return std::fabs(a.tPeak - now) < std::fabs(b.tPeak - now);
    });
    return ev;
}

LunarShadow lunarShadowAt(double t, const Geo& g) {
    LGeo L = lunarGeom(t, g);
    LunarShadow s;
    s.dx = L.dx; s.dy = L.dy; s.umbra = L.ru; s.penumbra = L.rp; s.moonR = L.sm;
    s.active = L.d < L.rp + L.sm;
    return s;
}

SolarCover solarCoverAt(double t, const Geo& g) {
    SGeo r = solarTopo(t, g);
    SolarCover c;
    c.dx = r.dx; c.dy = r.dy; c.sunR = r.sunR; c.moonR = r.moonR; c.sep = r.sep;
    c.active = r.sep < r.sunR + r.moonR;
    return c;
}

const char* showerName(int i) { return (i >= 0 && i < NSH) ? SH[i].name : ""; }
int showerCount() { return NSH; }

} // namespace skyev
