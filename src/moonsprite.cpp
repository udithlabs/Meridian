// Procedural Moon: a selenographic albedo + height map built once (real maria outlines, ~60 named craters with
// ray systems, thousands of smaller craters), then lit per pixel with relief shading so craters and mountains
// stand out along the terminator. Portable C++, no OS dependencies.
#include "moonsprite.h"
#include <cmath>
#include <algorithm>
#include <mutex>
#include <thread>
#include <atomic>

namespace moonsprite {
namespace {

const double PI = 3.14159265358979323846;
const double D2R = PI / 180.0;
const double KM_PER_DEG = 30.32;                 // 1737 km radius * pi / 180

// ---------------------------------------------------------------- noise
inline unsigned hash2(int x, int y, unsigned seed = 0) {
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u; h ^= h >> 16;
    return h;
}
inline double hashf(int x, int y, unsigned seed = 0) { return (hash2(x, y, seed) & 0xFFFFFF) / (double)0x1000000; }
inline double smooth(double t) { return t * t * (3.0 - 2.0 * t); }
inline double smoothstep(double a, double b, double x) { double t = std::max(0.0, std::min(1.0, (x - a) / (b - a))); return smooth(t); }
double vnoise(double x, double y, unsigned seed = 0) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    double fx = smooth(x - xi), fy = smooth(y - yi);
    double a = hashf(xi, yi, seed), b = hashf(xi + 1, yi, seed), c = hashf(xi, yi + 1, seed), d = hashf(xi + 1, yi + 1, seed);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
double fbm(double x, double y, int oct, unsigned seed = 0) {
    double s = 0, a = 0.5, n = 0;
    for (int i = 0; i < oct; ++i) { s += a * vnoise(x, y, seed + i * 31); n += a; x *= 2.03; y *= 2.03; a *= 0.5; }
    return s / n;
}
struct Rng {
    unsigned s;
    explicit Rng(unsigned seed) : s(seed * 2654435761u + 12345u) {}
    double next() { s = s * 1664525u + 1013904223u; unsigned h = s; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; return (h & 0xFFFFFF) / (double)0x1000000; }
};

// ---------------------------------------------------------------- data
struct Mare { double lon, lat, ax, ay, dark, rough; };       // centre, half-axes in km (east-west, north-south), darkness 0..1
const Mare MARIA[] = {
    {-16.0, 33.0, 560, 500, 0.56, 0.8},   // Imbrium
    {18.5, 28.0, 340, 300, 0.60, 0.8},    // Serenitatis
    {31.0, 8.5, 460, 330, 0.66, 0.9},     // Tranquillitatis
    {59.1, 17.0, 300, 220, 0.70, 0.5},    // Crisium
    {51.3, -7.8, 340, 330, 0.50, 1.0},    // Fecunditatis
    {34.6, -15.2, 170, 160, 0.56, 0.6},   // Nectaris
    {-16.6, -21.3, 380, 300, 0.52, 0.9},  // Nubium
    {-38.6, -24.4, 200, 190, 0.62, 0.6},  // Humorum
    {-23.1, -10.5, 200, 140, 0.52, 0.7},  // Cognitum
    {3.6, 13.3, 130, 120, 0.58, 0.7},     // Vaporum
    {-8.3, 10.9, 160, 110, 0.54, 0.7},    // Sinus Aestuum
    {0.4, 1.6, 120, 100, 0.50, 0.6},      // Sinus Medii
    {-55.0, 18.0, 760, 880, 0.50, 1.3},   // Oceanus Procellarum
    {-33.0, 3.0, 420, 360, 0.46, 1.0},    // Mare Insularum
    {-3.0, 56.0, 760, 90, 0.38, 0.8},     // Frigoris
    {-31.7, 44.1, 140, 130, 0.50, 0.4},   // Sinus Iridum
    {-50.0, 54.0, 260, 110, 0.40, 0.8},   // Sinus Roris
    {28.0, 38.0, 200, 90, 0.40, 0.7},     // Somniorum
    {27.0, 45.0, 110, 70, 0.38, 0.5},     // Mortis
    {0.4, 26.5, 100, 80, 0.40, 0.5},      // Palus Putredinis
    {22.0, 22.6, 90, 40, 0.40, 0.5},      // Anguis
    {68.0, 5.0, 100, 70, 0.34, 0.5},      // Undarum / Spumans
    {87.0, 1.3, 190, 140, 0.50, 0.5},     // Smythii
    {86.0, 13.0, 190, 100, 0.46, 0.5},    // Marginis
    {81.0, 57.0, 120, 110, 0.40, 0.4},    // Humboldtianum
    {-92.8, -19.4, 150, 150, 0.40, 0.4},  // Orientale
    {10.0, -45.0, 0, 0, 0, 0},
};
const int NMARIA = (int)(sizeof MARIA / sizeof MARIA[0]) - 1;

struct Named { double lon, lat, diamKm; int flags; };     // flags: 1 bright rays, 2 dark floor, 4 fresh/bright, 8 big rays (Tycho)
const Named NAMED[] = {
    {-11.4, -43.3, 85, 1 | 8}, {-20.1, 9.6, 93, 1 | 4}, {-38.0, 8.1, 32, 1 | 4}, {-47.5, 23.7, 40, 1 | 4},
    {-9.4, 51.6, 101, 2}, {-14.4, -58.6, 225, 0}, {-1.9, -9.3, 153, 2}, {-2.8, -13.4, 119, 0}, {-1.9, -17.7, 97, 0},
    {26.4, -11.5, 100, 4}, {24.0, -13.2, 98, 0}, {23.4, -18.1, 100, 0}, {60.9, -8.9, 132, 4}, {60.4, -25.1, 177, 0},
    {61.6, -16.4, 147, 0}, {60.3, -35.3, 125, 0}, {-68.5, -5.2, 172, 2}, {-74.3, -3.3, 146, 2}, {-39.9, -17.6, 110, 2},
    {-54.6, -44.0, 227, 0}, {-69.1, -66.5, 303, 0}, {-67.6, 2.2, 106, 0}, {-66.9, 5.1, 57, 0}, {29.9, 31.8, 95, 2},
    {17.3, 50.2, 87, 0}, {16.3, 44.3, 67, 0}, {44.4, 46.7, 87, 0}, {39.1, 46.7, 69, 0}, {56.4, 53.6, 125, 2},
    {-3.9, 29.7, 83, 2}, {1.5, 30.7, 39, 0}, {1.2, 33.9, 55, 0}, {-11.3, 14.5, 58, 0}, {-13.1, 26.7, 34, 0},
    {-21.0, 25.8, 30, 0}, {-22.2, -20.7, 61, 4}, {-6.3, -50.5, 194, 0}, {-21.8, -49.6, 145, 0}, {-13.5, -29.9, 106, 2},
    {5.2, -5.5, 138, 0}, {4.1, -11.2, 136, 0}, {6.0, -41.1, 126, 0}, {-26.1, -29.3, 47, 0}, {-27.7, -27.7, 48, 0},
    {46.5, 5.6, 56, 0}, {46.0, 21.2, 64, 0}, {46.9, 16.1, 28, 1 | 4}, {55.5, 27.7, 126, 2}, {33.2, -21.0, 112, 2},
    {32.2, -29.7, 88, 0}, {16.3, 16.3, 27, 4}, {9.1, 14.5, 39, 4}, {17.9, 21.8, 16, 4}, {-37.0, 21.9, 20, 0},
    {-53.0, 20.0, 18, 0}, {-18.0, 6.0, 20, 4}, {-43.0, -33.0, 40, 0}, {-8.0, 25.0, 15, 0}, {-31.0, -57.0, 90, 0},
    {-26.0, -12.0, 30, 0}, {-4.0, -33.0, 70, 0}, {12.0, -52.0, 85, 0}, {38.0, -42.0, 90, 0}, {-2.0, 40.0, 25, 0},
};
const int NNAMED = (int)(sizeof NAMED / sizeof NAMED[0]);

// ---------------------------------------------------------------- map
const double LON0 = -110.0, LON1 = 110.0, LAT0 = -90.0, LAT1 = 90.0;
struct Level {
    int w = 0, h = 0; double res = 0.2;
    std::vector<float> alb, hgt;
    float at(const std::vector<float>& a, double lon, double lat) const {
        double x = (lon - LON0) / res - 0.5, y = (lat - LAT0) / res - 0.5;
        int xi = (int)std::floor(x), yi = (int)std::floor(y); double fx = x - xi, fy = y - yi;
        auto g = [&](int xx, int yy) { xx = std::max(0, std::min(w - 1, xx)); yy = std::max(0, std::min(h - 1, yy)); return a[(size_t)yy * w + xx]; };
        double v00 = g(xi, yi), v10 = g(xi + 1, yi), v01 = g(xi, yi + 1), v11 = g(xi + 1, yi + 1);
        return (float)(v00 + (v10 - v00) * fx + (v01 - v00) * fy + (v00 - v10 - v01 + v11) * fx * fy);
    }
};
Level LV[3];
std::once_flag built;
std::atomic<bool> g_ready{false};
void buildAll();

double mareMask(double lon0, double lat0, double& dark, double& rough) {
    // domain-warp the position so coastlines are irregular (bays, peninsulas) instead of clean ellipses
    double wl = (fbm(lon0 * 0.055 + 3, lat0 * 0.055, 3, 21) - 0.5) * 2.0 * 7.0 + (fbm(lon0 * 0.30 + 8, lat0 * 0.30 + 2, 3, 22) - 0.5) * 2.0 * 2.2 + (fbm(lon0 * 1.1, lat0 * 1.1, 2, 25) - 0.5) * 2.0 * 0.5;
    double wb = (fbm(lon0 * 0.055, lat0 * 0.055 + 9, 3, 23) - 0.5) * 2.0 * 6.0 + (fbm(lon0 * 0.30 + 1, lat0 * 0.30 + 6, 3, 24) - 0.5) * 2.0 * 2.0 + (fbm(lon0 * 1.1 + 5, lat0 * 1.1, 2, 26) - 0.5) * 2.0 * 0.5;
    double lon = lon0 + wl, lat = lat0 + wb;
    double edgeNoise = (fbm(lon0 * 0.45 + 11, lat0 * 0.45 + 5, 3, 7) - 0.5) * 0.16;
    double best = 0; dark = 0; rough = 1;
    for (int i = 0; i < NMARIA; ++i) {
        const Mare& m = MARIA[i];
        double dy = (lat - m.lat) * KM_PER_DEG / m.ay;
        if (std::fabs(dy) > 1.7) continue;
        double dx = (lon - m.lon) * std::cos(m.lat * D2R) * KM_PER_DEG / m.ax;
        double d = std::sqrt(dx * dx + dy * dy) + edgeNoise;
        double v = 1.0 - smoothstep(0.78, 1.0, d);
        if (v > best) { best = v; dark = m.dark; rough = m.rough; }
    }
    return best;
}

struct Crater { double lon, lat, R; int flags; };

void splat(Level& L, const Crater& c) {
    const double cl = std::max(0.2, std::cos(c.lat * D2R));
    const double span = c.R * ((c.flags & 8) ? 2.8 : (c.flags & 4) ? 3.2 : 2.2);
    int x0 = (int)std::floor((c.lon - span / cl - LON0) / L.res), x1 = (int)std::ceil((c.lon + span / cl - LON0) / L.res);
    int y0 = (int)std::floor((c.lat - span - LAT0) / L.res), y1 = (int)std::ceil((c.lat + span - LAT0) / L.res);
    x0 = std::max(0, x0); x1 = std::min(L.w - 1, x1); y0 = std::max(0, y0); y1 = std::min(L.h - 1, y1);
    const bool complexC = c.R > 0.9;
    const double depth = c.R * (c.R < 0.4 ? 0.36 : c.R < 1.0 ? 0.26 : 0.17);
    const double rimH = depth * 0.30;
    const bool dark = (c.flags & 2) != 0, bright = (c.flags & 4) != 0;
    for (int y = y0; y <= y1; ++y) {
        double lat = LAT0 + (y + 0.5) * L.res;
        for (int x = x0; x <= x1; ++x) {
            double lon = LON0 + (x + 0.5) * L.res;
            double dx = (lon - c.lon) * std::cos(lat * D2R), dy = lat - c.lat;
            double r = std::sqrt(dx * dx + dy * dy) / c.R;
            if (r > span / c.R) continue;
            size_t i = (size_t)y * L.w + x;
            double h = L.hgt[i], a = L.alb[i];
            if (r < 1.2) {
                double inside = 1.0 - smoothstep(0.92, 1.12, r);
                double s = complexC ? smoothstep(0.62, 1.0, r) : smoothstep(0.22, 1.0, r);
                double prof = -depth * (1.0 - s);
                if (complexC) prof += 0.38 * depth * std::exp(-std::pow(r / 0.11, 2.0));            // central peak
                if (dark) prof = -depth * 0.55 * (1.0 - smoothstep(0.7, 1.0, r));                    // lava-flooded floor
                h = h * (1.0 - 0.75 * inside) + (prof + 0.0) * inside + h * 0.75 * inside * 0.35;
                if (dark) a = a * (1.0 - 0.8 * inside) + 0.40 * 0.8 * inside;
                else a += (bright ? 0.05 : 0.02) * inside;
            }
            h += rimH * std::exp(-std::pow((r - 1.0) / 0.13, 2.0));
            if (r > 1.0) h += 0.10 * depth * std::exp(-(r - 1.0) / 0.7);
            if (bright || (c.flags & 1)) {                                                         // ejecta blanket
                double e = (1.0 - smoothstep(1.0, 2.6, r)) * (r > 0.7 ? 1.0 : 0.0);
                a += 0.16 * e;
            }
            L.hgt[i] = (float)h; L.alb[i] = (float)std::min(1.1, a);
        }
    }
}

// bright ray system around a young crater, in map space (great-circle distance and bearing)
void rays(Level& L, double lon0, double lat0, double R, double len, double amp, int nray, double phase, unsigned seed) {
    const double sl = std::sin(lat0 * D2R), cl0 = std::cos(lat0 * D2R);
    const double maxd = len * 3.0;
    int y0 = std::max(0, (int)std::floor((lat0 - maxd - LAT0) / L.res)), y1 = std::min(L.h - 1, (int)std::ceil((lat0 + maxd - LAT0) / L.res));
    for (int y = y0; y <= y1; ++y) {
        double lat = LAT0 + (y + 0.5) * L.res, sp = std::sin(lat * D2R), cp = std::cos(lat * D2R);
        for (int x = 0; x < L.w; ++x) {
            double lon = LON0 + (x + 0.5) * L.res, dl = (lon - lon0) * D2R;
            double cd = sl * sp + cl0 * cp * std::cos(dl);
            double d = std::acos(std::max(-1.0, std::min(1.0, cd))) / D2R;
            if (d < R * 1.05 || d > maxd) continue;
            double brg = std::atan2(std::sin(dl) * cp, cl0 * sp - sl * cp * std::cos(dl));
            double jitter = (fbm(brg * 3.0 + 40, d * 0.05, 2, seed) - 0.5) * 0.9;
            double ray = std::pow(std::max(0.0, std::cos(nray * 0.5 * brg + phase + jitter)), 10.0);
            double ray2 = std::pow(std::max(0.0, std::cos(nray * 0.5 * 1.618 * brg + phase * 2.3 - jitter)), 14.0) * 0.5;
            double fall = std::exp(-d / len) * smoothstep(R * 1.05, R * 2.0, d);
            L.alb[(size_t)y * L.w + x] += (float)(amp * (ray + ray2) * fall);
        }
    }
}

void build() {
    Level& L = LV[0];
    L.res = 0.2; L.w = (int)std::round((LON1 - LON0) / L.res); L.h = (int)std::round((LAT1 - LAT0) / L.res);
    L.alb.assign((size_t)L.w * L.h, 0.f); L.hgt.assign((size_t)L.w * L.h, 0.f);
    // base terrain
    for (int y = 0; y < L.h; ++y) {
        double lat = LAT0 + (y + 0.5) * L.res;
        for (int x = 0; x < L.w; ++x) {
            double lon = LON0 + (x + 0.5) * L.res;
            double u = lon * 0.30, v = lat * 0.30;
            double mott = fbm(u, v, 4, 1);                                              // large-scale highland mottling
            double fine = fbm(lon * 2.4 + 3, lat * 2.4 + 9, 3, 2);
            double aH = 0.74 + 0.20 * (mott - 0.5) + 0.07 * (fine - 0.5);
            double hH = 0.14 * (fbm(lon * 0.55, lat * 0.55, 4, 5) - 0.5) + 0.05 * (fine - 0.5);
            double dark, rough;
            double m = mareMask(lon, lat, dark, rough);
            double aM = (1.0 - dark) * (0.90 + 0.40 * (fbm(lon * 0.35 + 20, lat * 0.35, 4, 4) - 0.5)) + 0.04 * (fine - 0.5);
            double hM = -0.03 + 0.02 * (fbm(lon * 1.2, lat * 1.2, 3, 8) - 0.5) * rough;
            size_t i = (size_t)y * L.w + x;
            L.alb[i] = (float)(aH * (1.0 - m) + aM * m * 0.86);
            L.hgt[i] = (float)(hH * (1.0 - m) + hM * m);
        }
    }
    // random crater population: many small, few large (power law); fewer on the younger maria
    Rng rng(20260);
    std::vector<Crater> cr;
    const int N = 5200;
    for (int i = 0; i < N; ++i) {
        double R = 0.19 * std::pow(rng.next(), -0.52);
        if (R > 2.6) continue;
        double lon = LON0 + 10.0 + (LON1 - LON0 - 20.0) * rng.next();
        double lat = std::asin(2.0 * rng.next() - 1.0) / D2R;
        double dark, rough;
        double m = mareMask(lon, lat, dark, rough);
        if (m > 0.5 && rng.next() < (R > 0.45 ? 0.9 : 0.55)) continue;
        int flags = rng.next() < 0.07 ? 4 : 0;
        cr.push_back({lon, lat, R, flags});
    }
    std::sort(cr.begin(), cr.end(), [](const Crater& a, const Crater& b) { return a.R > b.R; });
    for (const Crater& c : cr) splat(L, c);
    // named craters on top
    for (int i = 0; i < NNAMED; ++i) {
        const Named& n = NAMED[i];
        splat(L, {n.lon, n.lat, n.diamKm * 0.5 / KM_PER_DEG, n.flags});
    }
    // ray systems
    rays(L, -11.4, -43.3, 1.4, 26.0, 0.20, 24, 0.4, 11);
    rays(L, -20.1, 9.6, 1.5, 9.0, 0.16, 18, 1.3, 12);
    rays(L, -38.0, 8.1, 0.5, 4.5, 0.14, 14, 0.7, 13);
    rays(L, -47.5, 23.7, 0.7, 5.0, 0.16, 14, 2.1, 14);
    rays(L, 46.9, 16.1, 0.5, 5.0, 0.12, 12, 0.2, 15);
    // limit
    for (float& a : L.alb) a = std::max(0.06f, std::min(1.0f, a));
    // mip levels (box filter)
    for (int k = 1; k < 3; ++k) {
        const Level& s = LV[k - 1]; Level& d = LV[k];
        d.res = s.res * 2; d.w = s.w / 2; d.h = s.h / 2;
        d.alb.assign((size_t)d.w * d.h, 0.f); d.hgt.assign((size_t)d.w * d.h, 0.f);
        for (int y = 0; y < d.h; ++y) for (int x = 0; x < d.w; ++x) {
            size_t i0 = (size_t)(2 * y) * s.w + 2 * x;
            d.alb[(size_t)y * d.w + x] = 0.25f * (s.alb[i0] + s.alb[i0 + 1] + s.alb[i0 + s.w] + s.alb[i0 + s.w + 1]);
            d.hgt[(size_t)y * d.w + x] = 0.25f * (s.hgt[i0] + s.hgt[i0 + 1] + s.hgt[i0 + s.w] + s.hgt[i0 + s.w + 1]);
        }
    }
}
void buildAll() { build(); g_ready = true; }
struct PrewarmGuard {
    std::thread th;
    ~PrewarmGuard() { if (th.joinable()) th.join(); }
} g_prewarm;
std::once_flag g_prewarmStarted;
} // namespace

void prewarm() {
    std::call_once(g_prewarmStarted, [] {
        g_prewarm.th = std::thread([] { std::call_once(built, buildAll); });
    });
}
void shutdown() { if (g_prewarm.th.joinable()) g_prewarm.th.join(); }
bool ready() { return g_ready.load(); }

void render(const Params& p, std::vector<uint8_t>& out) {
    std::call_once(built, buildAll);
    const int N = p.size;
    out.assign((size_t)N * N * 4, 0);
    const double i = p.phaseAngleDeg * D2R, chi = p.brightLimbPA * D2R;
    const double opSurge = 1.0 + 0.10 * std::exp(-i * i / 0.12);
    // sun direction in sprite frame (x right, y up, z toward viewer)
    const double sx = std::sin(i) * -std::sin(chi), sy = std::sin(i) * std::cos(chi), sz = std::cos(i);
    const double footprint = 180.0 / N;                                          // degrees of lunar surface per sprite pixel
    const int lvl = std::max(0, std::min(2, (int)std::floor(std::log2(std::max(0.2, footprint) / 0.2) + 0.15)));
    const Level& L = LV[lvl];
    const int ss = N >= 320 ? 2 : 1;
    const double relief = p.relief;
    for (int py = 0; py < N; ++py) {
        for (int px = 0; px < N; ++px) {
            double accR = 0, accG = 0, accB = 0, accA = 0;
            for (int sy2 = 0; sy2 < ss; ++sy2) for (int sx2 = 0; sx2 < ss; ++sx2) {
                double x = ((px + (sx2 + 0.5) / ss) / N) * 2.0 - 1.0, y = 1.0 - ((py + (sy2 + 0.5) / ss) / N) * 2.0;
                double rho = std::sqrt(x * x + y * y);
                double cover = std::max(0.0, std::min(1.0, (1.0 - rho) * N * 0.5 + 0.5));
                if (cover <= 0.0) continue;
                double rr = std::min(rho, 0.9999);
                double z = std::sqrt(1.0 - rr * rr);
                double k = rr / std::max(rho, 1e-9);
                double xn = x * k, yn = y * k;
                double mu = z;
                // selenographic coordinates (north up, east = right)
                double latD = std::asin(std::max(-1.0, std::min(1.0, yn))) / D2R + p.libLat;
                double lonD = std::atan2(xn, z) / D2R + p.libLon;
                latD = std::max(-89.5, std::min(89.5, latD)); lonD = std::max(-108.0, std::min(108.0, lonD));
                double alb = L.at(L.alb, lonD, latD);
                // relief: height gradient -> perturbed normal
                double d = L.res;
                double hE = (L.at(L.hgt, lonD + d, latD) - L.at(L.hgt, lonD - d, latD)) / (2 * d);
                double hN = (L.at(L.hgt, lonD, latD + d) - L.at(L.hgt, lonD, latD - d)) / (2 * d);
                double cosLat = std::max(0.15, std::sqrt(std::max(0.0, 1.0 - yn * yn)));
                hE /= cosLat;
                const double limbFade = smoothstep(0.03, 0.34, z);              // flatten shading right at the limb (aliasing)
                hE = std::max(-1.0, std::min(1.0, hE)) * limbFade; hN = std::max(-1.0, std::min(1.0, hN)) * limbFade;
                // tangent frame on the unit sphere (view coordinates)
                double ex = z, ey = 0, ez = -xn, el = std::max(1e-6, std::sqrt(ex * ex + ey * ey + ez * ez)); ex /= el; ez /= el;
                double nx = -xn * yn, ny = 1.0 - yn * yn, nz = -z * yn, nl = std::max(1e-6, std::sqrt(nx * nx + ny * ny + nz * nz)); nx /= nl; ny /= nl; nz /= nl;
                double Nx = xn - relief * (hE * ex + hN * nx), Ny = yn - relief * (hE * ey + hN * ny), Nz = z - relief * (hE * ez + hN * nz);
                double Nl = std::sqrt(Nx * Nx + Ny * Ny + Nz * Nz); Nx /= Nl; Ny /= Nl; Nz /= Nl;
                double mu0 = xn * sx + yn * sy + z * sz;                 // geometric sun elevation
                double mu0p = Nx * sx + Ny * sy + Nz * sz;               // with relief
                double lit = 0.0;
                if (mu0p > 0.0) lit = 2.0 * mu0p / (mu0p + mu);          // Lommel-Seeliger
                lit *= smoothstep(-0.06, 0.16, mu0) * 0.5 + 0.5 * smoothstep(0.0, 0.10, mu0p);
                lit = std::min(1.25, lit);
                // opposition surge at full moon
                lit *= opSurge;
                double v = alb * lit;
                // earthshine on the dark side
                double dk = p.earthshine * (0.55 + 0.9 * alb) * (1.0 - std::min(1.0, lit * 1.5));
                double base = std::min(1.0, v * 1.04 + dk);
                // colour: highlands warm white, maria cooler grey
                double cool = std::max(0.0, std::min(1.0, (0.78 - alb) / 0.4));
                double cr = p.tint[0] * (1.0 - 0.07 * cool), cg = p.tint[1] * (1.0 - 0.02 * cool), cb = p.tint[2] * (1.0 + 0.03 * cool);
                double r = std::min(1.0, base * cr), g = std::min(1.0, base * cg), b = std::min(1.0, base * cb);
                if (lit < 0.05) { r *= 0.88; g *= 0.95; b *= 1.04; }     // earthshine is bluer
                if (p.shadow) {
                    // Earth's shadow: soft penumbra, then the copper umbra lit by sunlight bent through the atmosphere
                    const double sd = std::sqrt((x - p.shX) * (x - p.shX) + (y - p.shY) * (y - p.shY));
                    const double pt = std::max(0.0, std::min(1.0, (p.penumbraR - sd) / std::max(0.05, p.penumbraR - p.umbraR)));
                    const double fp = 1.0 - 0.58 * std::pow(pt, 1.7);
                    double rr2 = r * fp, gg2 = g * fp, bb2 = b * fp;
                    const double u = smoothstep(p.umbraR + 0.035, p.umbraR - 0.06, sd);
                    if (u > 0.0) {
                        const double rn = sd / std::max(0.2, p.umbraR);
                        const double fu = 0.045 + 0.150 * smoothstep(0.30, 1.0, rn);
                        const double rim = smoothstep(0.80, 1.0, rn);
                        double cr2 = r * fu * (1.00 + 0.55 * rim) + 0.010;
                        double cg2 = g * fu * (0.36 + 0.50 * rim) + 0.0035;
                        double cb2 = b * fu * (0.15 + 0.55 * rim) + 0.0015;
                        rr2 += (cr2 - rr2) * u; gg2 += (cg2 - gg2) * u; bb2 += (cb2 - bb2) * u;
                    }
                    r = rr2; g = gg2; b = bb2;
                }
                accR += r * cover; accG += g * cover; accB += b * cover; accA += cover;
            }
            double inv = 1.0 / (ss * ss);
            double alpha = accA * inv;
            if (alpha <= 0.0) continue;
            uint8_t* o = &out[((size_t)py * N + px) * 4];
            // premultiplied BGRA (colours already weighted by coverage)
            o[0] = (uint8_t)std::lround(std::min(1.0, accB * inv) * 255.0);
            o[1] = (uint8_t)std::lround(std::min(1.0, accG * inv) * 255.0);
            o[2] = (uint8_t)std::lround(std::min(1.0, accR * inv) * 255.0);
            o[3] = (uint8_t)std::lround(std::min(1.0, alpha) * 255.0);
        }
    }
}
} // namespace moonsprite
