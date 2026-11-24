// Meridian: sky events on the dome (meteors, conjunctions, parades, eclipses), event chips and the hover card.
#include "scene.h"
#include <cmath>
#include <algorithm>

namespace app {

using namespace gfx;
using util::fmt;
using astro::Horizontal;

namespace {

const double PI_ = 3.14159265358979323846;

Color kindColor(skyev::Kind k) {
    switch (k) {
        case skyev::METEOR: return rgb(0xFFD08A);
        case skyev::CONJUNCTION: return rgb(0x8FD3FF);
        case skyev::MOON_NEAR: return rgb(0xB9C8FF);
        case skyev::OPPOSITION: return rgb(0xFF9F7A);
        case skyev::ELONGATION: return rgb(0xB8E0A0);
        case skyev::PARADE: return rgb(0xE2A8FF);
        case skyev::SOLAR_ECLIPSE: return rgb(0xFFC24D);
        default: return rgb(0xFF7A52);
    }
}
const char* kindLabel(skyev::Kind k) {
    switch (k) {
        case skyev::METEOR: return "METEOR SHOWER";
        case skyev::CONJUNCTION: return "CONJUNCTION";
        case skyev::MOON_NEAR: return "MOON AND PLANET";
        case skyev::OPPOSITION: return "OPPOSITION";
        case skyev::ELONGATION: return "ELONGATION";
        case skyev::PARADE: return "PLANET PARADE";
        case skyev::SOLAR_ECLIPSE: return "SOLAR ECLIPSE";
        default: return "LUNAR ECLIPSE";
    }
}

float hf(long long i, int salt) {
    unsigned x = (unsigned)(i * 374761393LL) + (unsigned)salt * 668265263u;
    x = (x ^ (x >> 13)) * 1274126177u; x ^= x >> 16;
    return (x & 0xFFFF) / 65535.f;
}

Pt dp(float cx, float cy, float R, double alt, double az) {
    return domeProject(cx, cy, R, alt, az);
}

struct V3 { double x, y, z; };
V3 hvec(double alt, double az) { double ca = std::cos(astro::rad(alt)); return {ca * std::cos(astro::rad(az)), ca * std::sin(astro::rad(az)), std::sin(astro::rad(alt))}; }
V3 add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 mul(V3 a, double k) { return {a.x * k, a.y * k, a.z * k}; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double len(V3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }

bool bodyPos(const App& a, int id, double& alt, double& az) {
    if (id == skyev::MOON_ID) { alt = a.moon.appAlt; az = a.moon.hor.az; return true; }
    if (id >= 0 && id < (int)a.planets.size()) { alt = a.planets[id].info.appAlt; az = a.planets[id].info.hor.az; return true; }
    return false;
}

double eclLonOfEq(const astro::Equatorial& eq, double t) {
    double T = (astro::julianDay(t) - 2451545.0) / 36525.0;
    double eps = astro::rad(23.4392911 - 0.0130042 * T), ra = astro::rad(eq.ra), de = astro::rad(eq.dec);
    return astro::norm360(astro::deg(std::atan2(std::sin(ra) * std::cos(eps) + std::tan(de) * std::sin(eps), std::cos(ra))));
}

void dottedLine(Gfx& g, Pt p, Pt q, float spacing, float r, const Color& col) {
    float dx = q.x - p.x, dy = q.y - p.y, L = std::sqrt(dx * dx + dy * dy);
    int n = std::max(1, (int)(L / spacing));
    for (int i = 0; i <= n; ++i) g.fillCircle(p.x + dx * i / n, p.y + dy * i / n, r, col);
}

void dottedCircle(Gfx& g, float cx, float cy, float rad, float r, const Color& col) {
    int n = std::max(24, (int)(2 * PI_ * rad / (r * 4.5f)));
    for (int i = 0; i < n; ++i) { float t = (float)(2 * PI_ * i / n); g.fillCircle(cx + rad * std::cos(t), cy + rad * std::sin(t), r, col); }
}

// a label with a faint shadow so it reads over stars
void label(Gfx& g, float s, float x, float y, const std::string& text, const Color& col, Align al) {
    std::wstring w = util::widen(text);
    g.text(F_MONO, 11.5f * s, x + 1.f, y + 1.f, w, Color{0, 0, 0, 0.55f * col.a}, al, 0.1f);
    g.text(F_MONO, 11.5f * s, x, y, w, col, al, 0.1f);
}

// label placed toward the dome centre from an anchor so it stays on the sky
void labelInward(Gfx& g, float s, float cx, float cy, float ax, float ay, float clearance, const std::string& text, const Color& col) {
    float dx = cx - ax, dy = cy - ay, L = std::sqrt(dx * dx + dy * dy);
    if (L < 100.f * s) {                                  // near the zenith: tuck the label up and to the right
        label(g, s, ax + clearance * 0.55f, ay - clearance * 0.45f, text, col, Align::Left);
        return;
    }
    dx /= L; dy /= L;
    float px = ax + dx * clearance, py = ay + dy * clearance;
    Align al = dx > 0.35f ? Align::Left : dx < -0.35f ? Align::Right : Align::Center;
    label(g, s, px, py + 4 * s, text, col, al);
}

std::string dayWord(const App& a, double t) {
    int off = a.tzAt ? a.tzAt(t + 30.0) : a.tzOff;
    util::LocalTime L = util::toLocal(t + 30.0, off), N = util::toLocal(a.now, a.tzOff);
    long long d = L.dayIndex - N.dayIndex;
    if (d == 0) return "TODAY";
    if (d == 1) return "TOMORROW";
    if (d == -1) return "YESTERDAY";
    return d > 0 ? fmt("IN %lld DAYS", d) : fmt("%lld DAYS AGO", -d);
}

std::string whenShort(const App& a, const skyev::Event& e) {
    double rel = e.tPeak - a.now;
    bool eclipse = e.kind == skyev::SOLAR_ECLIPSE || e.kind == skyev::LUNAR_ECLIPSE;
    if (e.active(a.now) && eclipse && e.visible) return "NOW";
    if (e.kind == skyev::METEOR && std::fabs(rel) < 5400.0) return "PEAKING";
    if (std::fabs(rel) < 90.0) return "NOW";
    if (std::fabs(rel) < 5400.0) return rel >= 0 ? fmt("IN %d MIN", (int)std::lround(rel / 60.0)) : "JUST NOW";
    std::string w = dayWord(a, e.tPeak);
    int off = a.tzAt ? a.tzAt(e.tPeak + 30.0) : a.tzOff;
    util::LocalTime L = util::toLocal(e.tPeak + 30.0, off), N = util::toLocal(a.now, a.tzOff);
    long long d = L.dayIndex - N.dayIndex;
    if (d == 0 || d == 1) w += " " + clockStr(a, e.tPeak);
    return w;
}

std::string whenLong(const App& a, const skyev::Event& e) {
    double rel = e.tPeak - a.now;
    bool eclipse = e.kind == skyev::SOLAR_ECLIPSE || e.kind == skyev::LUNAR_ECLIPSE;
    if (e.active(a.now) && eclipse) return e.visible ? "HAPPENING NOW" : "HAPPENING NOW, NOT VISIBLE";
    if (e.kind == skyev::METEOR && std::fabs(rel) < 5400.0) return "PEAKING NOW";
    double ar = std::fabs(rel);
    std::string v = ar < 5400.0 ? fmt("%d MIN", (int)std::lround(ar / 60.0)) : ar < 36 * 3600.0 ? fmt("%d H", (int)std::lround(ar / 3600.0)) : fmt("%.1f DAYS", ar / 86400.0);
    return rel >= 0 ? "IN " + v : v + " AGO";
}

std::string chipName(const skyev::Event& e) {
    switch (e.kind) {
        case skyev::METEOR: return util::upper(skyev::showerName(e.shower));
        case skyev::OPPOSITION: return util::upper(astro::planetName(e.bodies[0])) + " OPPOSITION";
        case skyev::ELONGATION: return util::upper(astro::planetName(e.bodies[0])) + " ELONGATION";
        default: return util::upper(e.title);
    }
}

float proximity(const App& a, const skyev::Event& e) {
    float d = (float)std::fabs((e.tPeak - a.now) / 86400.0);
    return 0.3f + 0.7f * (1.f - smoothstep(0.3f, 5.f, d));
}

void addEventHit(App& a, float x, float y, float w, float h, int idx, bool round) {
    a.eventHits.push_back({x + a.shiftX, y + a.shiftY, w, h, idx, round});
}
void addRoundHit(App& a, float cx, float cy, float r, int idx) { addEventHit(a, cx - r, cy - r, 2 * r, 2 * r, idx, true); }

float pulse(const App& a, float period = 3.2f) { return a.eventsAnimating ? 0.5f + 0.5f * (float)std::sin(a.now * 2 * PI_ / period) : 0.55f; }

// ------------------------------------------------------------ meteors
void drawMeteors(Ctx& c, const skyev::Event& e, int idx, float cx, float cy, float R, bool labels, float night, float prox, bool above) {
    App& a = c.a; Gfx& g = c.g; const Palette& P = c.P; const float s = c.s;
    Horizontal rh = astro::toHorizontal({e.raDeg, e.decDeg}, a.now, a.st.geo);
    if (rh.alt < -1.5) return;
    Color col = tint(P, kindColor(e.kind));
    Pt rp = dp(cx, cy, R, std::max(0.0, rh.alt), rh.az);
    const bool hov = a.hoverOn && a.hoverEvent == idx;

    if (!above) {
        if (night < 0.25f || !a.st.evAnimate) return;
        a.eventsAnimating = true;
        V3 r = hvec(rh.alt, rh.az);
        V3 cz = cross(V3{0, 0, 1}, r);
        V3 u1 = len(cz) < 1e-6 ? V3{0, 1, 0} : mul(cz, 1.0 / len(cz));
        V3 u2 = cross(r, u1);
        const int K = std::max(1, std::min(5, 1 + (int)(e.zhr / 35.0)));
        for (int k = 0; k < K; ++k) {
            const double period = 5.5 + 1.7 * k;
            const double tt = a.now + k * 3.7 + idx * 1.3;
            const long long cyc = (long long)std::floor(tt / period);
            const double ph = tt - cyc * period;
            const double life = 0.85 + 0.55 * hf(cyc * 7 + k, 3);
            const double st = hf(cyc * 13 + k, 5) * std::max(0.1, period - life - 0.2);
            if (ph < st || ph > st + life) continue;
            const double u = (ph - st) / life;
            const double phi = hf(cyc * 17 + k, 7) * 2 * PI_;
            const double rho0 = astro::rad(10.0 + 55.0 * hf(cyc * 19 + k, 11));
            const double plen = astro::rad(12.0 + 18.0 * hf(cyc * 23 + k, 13));
            const double headR = rho0 + plen * (1.0 - std::pow(1.0 - std::min(1.0, u / 0.82), 1.65));
            const double span = plen * (0.18 + 0.36 * std::min(1.0, u / 0.35));
            const double tailR = std::max(rho0, headR - span);
            const float entry = (float)std::min(1.0, u / 0.16);
            const float headLive = u < 0.82 ? (float)std::max(0.0, std::min(1.0, (0.82 - u) / 0.14)) : 0.f;
            const float afterGlow = u > 0.82 ? (float)std::max(0.0, 1.0 - (u - 0.82) / 0.18) : 1.f;
            const float df = (float)((u - 0.56) / 0.11);
            const float flare = std::exp(-df * df) * (0.55f + 0.45f * hf(cyc * 31 + k, 19));
            const float shimmer = 0.85f + 0.15f * std::sin((float)u * 42.f + (float)cyc);
            const float env = entry * afterGlow * shimmer * (0.55f + 0.45f * hf(cyc * 29 + k, 17)) * (0.78f + 0.55f * flare);
            auto at = [&](double rho, Pt& out) {
                V3 p = add(mul(r, std::cos(rho)), mul(add(mul(u1, std::cos(phi)), mul(u2, std::sin(phi))), std::sin(rho)));
                double alt = astro::deg(std::asin(std::max(-1.0, std::min(1.0, p.z)))), az = astro::deg(std::atan2(p.y, p.x));
                out = dp(cx, cy, R, alt, az);
                return alt > 2.0;
            };
            const int N = 10; Pt prev; bool pv = at(tailR, prev);
            for (int i = 1; i <= N; ++i) {
                Pt cur; bool cv = at(tailR + (headR - tailR) * i / N, cur);
                if (pv && cv) {
                    const float f = (float)i / N;
                    const Color cool = Color{0.68f, 0.78f, 1.0f, 1.f};
                    const Color warm = Color{1.0f, 0.90f, 0.72f, 1.f};
                    const Color hot  = Color{1.0f, 0.99f, 0.95f, 1.f};
                    const Color sc = f < 0.55f ? lerp(cool, warm, f / 0.55f) : lerp(warm, hot, (f - 0.55f) / 0.45f);
                    const float segA = clamp01(env * std::pow(f, 1.75f) * night);
                    g.line(prev.x, prev.y, cur.x, cur.y, tint(P, withA(sc, segA * 0.32f)), (0.9f + (2.6f + 1.4f * flare) * f) * s, true);
                    g.line(prev.x, prev.y, cur.x, cur.y, tint(P, withA(lerp(sc, hot, 0.5f), segA * (0.3f + 0.7f * headLive))), (0.4f + (1.2f + 0.6f * flare) * f) * s, true);
                }
                prev = cur; pv = cv;
            }
            if (pv && headLive > 0.02f) {
                const float ha = clamp01(env * headLive * night);
                g.glow(prev.x, prev.y, (9.f + 8.f * flare) * s, tint(P, Color{1.f, 0.91f, 0.76f, 1.f}), 0.42f * ha);
                g.fillCircle(prev.x, prev.y, (1.2f + 0.7f * flare) * s, tint(P, Color{1.f, 0.99f, 0.96f, ha}));
            }
        }
        return;
    }

    // the radiant: a small burst that always shows while it is above the horizon
    float vis = smoothstep(-1.5f, 5.f, (float)rh.alt) * prox;
    float spin = (float)(a.eventsAnimating ? std::fmod(a.now * 0.18, 2 * PI_) : 0.0);
    g.glow(rp.x, rp.y, (hov ? 46.f : 34.f) * s, col, (0.20f + (hov ? 0.20f : 0.f)) * vis);
    for (int i = 0; i < 8; ++i) {
        float t = spin + (float)(i * PI_ / 4);
        float r0 = 8.5f * s, r1 = (i % 2 ? 13.f : 18.f) * s;
        g.line(rp.x + r0 * std::cos(t), rp.y + r0 * std::sin(t), rp.x + r1 * std::cos(t), rp.y + r1 * std::sin(t), withA(col, 0.75f * vis), std::max(1.f, 1.3f * s), true);
    }
    g.strokeCircle(rp.x, rp.y, 5.2f * s, withA(col, 0.9f * vis), std::max(1.f, 1.3f * s));
    if (labels) labelInward(g, s, cx, cy, rp.x, rp.y, 30 * s, util::upper(skyev::showerName(e.shower)) + "  RADIANT", withA(col, 0.85f * vis));
    addRoundHit(a, rp.x, rp.y, std::max(26.f * s, 0.07f * R), idx);
}

// ------------------------------------------------------------ meetings, oppositions, parades
void drawGroup(Ctx& c, const skyev::Event& e, int idx, float cx, float cy, float R, bool labels, bool above, float skyA) {
    App& a = c.a; Gfx& g = c.g; const Palette& P = c.P; const float s = c.s;
    Pt pts[8]; int n = 0;
    for (int i = 0; i < e.nBodies && n < 8; ++i) {
        double alt, az;
        if (!bodyPos(a, e.bodies[i], alt, az) || alt < -0.5) continue;
        pts[n++] = dp(cx, cy, R, std::max(0.0, alt), az);
    }
    if (n == 0 || (e.kind != skyev::PARADE && n < e.nBodies)) return;
    if (e.kind == skyev::PARADE && n < 3) return;
    const float prox = proximity(a, e);
    const float al = skyA * prox;
    if (al < 0.03f) return;
    Color col = tint(P, kindColor(e.kind));
    const bool hov = a.hoverOn && a.hoverEvent == idx;
    float mx = 0, my = 0; for (int i = 0; i < n; ++i) { mx += pts[i].x; my += pts[i].y; } mx /= n; my /= n;
    float rad = 0; for (int i = 0; i < n; ++i) rad = std::max(rad, std::hypot(pts[i].x - mx, pts[i].y - my));

    if (!above) {
        if (e.kind == skyev::PARADE) {
            // the ecliptic between the outermost planets
            double lo = 1e9, hi = -1e9, ref = 0; bool first = true;
            for (int i = 0; i < e.nBodies; ++i) {
                double l = eclLonOfEq(a.planets[e.bodies[i]].info.eq, a.now);
                if (first) { ref = l; first = false; }
                double d = astro::norm180(l - ref); lo = std::min(lo, d); hi = std::max(hi, d);
            }
            std::vector<Pt> arc;
            double eps = 23.4393 - 0.013 * (astro::julianDay(a.now) - 2451545.0) / 36525.0;
            for (double d = lo - 4.0; d <= hi + 4.0; d += 2.0) {
                Horizontal h = astro::toHorizontal(astro::eclipticToEquatorial(astro::norm360(ref + d), 0.0, eps), a.now, a.st.geo);
                if (h.alt > 0.5) arc.push_back(dp(cx, cy, R, h.alt, h.az));
            }
            if (arc.size() >= 2) {
                for (int k = 0; k < 3; ++k) g.polyline(arc, false, withA(col, (0.05f + 0.05f * (hov ? 1.f : 0.f)) * al), (9.f - 3.f * k) * s, true);
                g.polyline(arc, false, withA(col, 0.55f * al), std::max(1.4f, 1.8f * s), true);
            }
        } else if (e.nBodies >= 2 && n >= 2) {
            for (int i = 0; i < n; ++i)
                for (int j = i + 1; j < n; ++j)
                    dottedLine(g, pts[i], pts[j], 5.5f * s, 0.9f * s, withA(col, 0.65f * al));
        }
        return;
    }

    // rings and labels above the bodies
    const float pu = pulse(a);
    if (e.kind == skyev::PARADE) {
        for (int i = 0; i < n; ++i) g.strokeCircle(pts[i].x, pts[i].y, 11.f * s, withA(col, (0.55f + 0.2f * pu) * al), std::max(1.f, 1.3f * s));
        if (labels) labelInward(g, s, cx, cy, mx, my, rad + 22 * s, util::upper(e.title), withA(col, 0.9f * al));
        for (int i = 0; i < n; ++i) addRoundHit(a, pts[i].x, pts[i].y, 16.f * s, idx);
        return;
    }
    float rr = std::max(e.kind == skyev::MOON_NEAR ? 30.f * s : 20.f * s, rad + 13.f * s);
    if (e.nBodies == 1) rr = 17.f * s;
    g.glow(mx, my, rr * 2.2f, col, (0.10f + (hov ? 0.10f : 0.f)) * al);
    g.strokeCircle(mx, my, rr, withA(col, (0.45f + 0.3f * pu + (hov ? 0.2f : 0.f)) * al), std::max(1.f, 1.4f * s));
    g.strokeCircle(mx, my, rr + 5.f * s * (0.6f + pu), withA(col, 0.20f * al), std::max(1.f, 1.f * s));
    if (labels) {
        std::string t;
        switch (e.kind) {
            case skyev::OPPOSITION: t = "OPPOSITION"; break;
            case skyev::ELONGATION: t = fmt("%.0f\xC2\xB0 FROM SUN", e.sepDeg); break;
            default: {
                for (int i = 0; i < e.nBodies; ++i) t += std::string(i ? " \xC2\xB7 " : "") + util::upper(e.bodies[i] == skyev::MOON_ID ? "Moon" : astro::planetName(e.bodies[i]));
                t += e.sepDeg < 1.0 ? fmt("  %.2f\xC2\xB0", e.sepDeg) : fmt("  %.1f\xC2\xB0", e.sepDeg);
            }
        }
        labelInward(g, s, cx, cy, mx, my, rr + 12 * s, t, withA(col, 0.92f * al));
    }
    addRoundHit(a, mx, my, rr + 8.f * s, idx);
}

// ------------------------------------------------------------ eclipses
void drawLunarEclipse(Ctx& c, const skyev::Event& e, int idx, float cx, float cy, float R, bool labels, float moonSize, bool above) {
    App& a = c.a; Gfx& g = c.g; const Palette& P = c.P; const float s = c.s;
    if (a.moon.appAlt < -0.7) return;
    Color col = tint(P, kindColor(e.kind));
    Pt mp = dp(cx, cy, R, std::max(0.0, a.moon.appAlt), a.moon.hor.az);
    const bool hov = a.hoverOn && a.hoverEvent == idx;
    const float al = proximity(a, e);
    const skyev::LunarShadow& L = a.lunarShadow;
    float mr = moonSize * 0.5f;
    if (L.active && L.moonR > 0) {
        // shadow centre in screen space: sprite frame has north up and east left, rotated like the moon sprite
        float k = mr / (float)L.moonR;
        float sx = -(float)L.dx * k, sy = -(float)L.dy * k;
        float th = (float)astro::rad(moonDomeRotation(a.moon, a.st.geo));
        float px = mp.x + sx * std::cos(th) - sy * std::sin(th), py = mp.y + sx * std::sin(th) + sy * std::cos(th);
        float ru = (float)L.umbra * k, rp = (float)L.penumbra * k;
        if (!above) {
            g.radialFill(px, py, rp, {{0.f, Color{0.f, 0.f, 0.f, 0.30f}}, {std::max(0.05f, ru / rp), Color{0.f, 0.f, 0.f, 0.26f}}, {1.f, Color{0.f, 0.f, 0.f, 0.f}}});
            g.fillCircle(px, py, ru, Color{0.f, 0.f, 0.f, 0.20f});
        } else {
            dottedCircle(g, px, py, ru, std::max(0.8f, 0.9f * s), withA(col, (hov ? 0.85f : 0.55f)));
            if (labels) label(g, s, px, py + ru + 16 * s, "EARTH'S SHADOW", withA(col, 0.8f), Align::Center);
        }
    }
    if (above) {
        g.strokeCircle(mp.x, mp.y, mr + 8 * s, withA(col, (0.5f + (hov ? 0.3f : 0.f)) * al), std::max(1.f, 1.3f * s));
        addRoundHit(a, mp.x, mp.y, mr + 14 * s, idx);
    }
}

// the Sun seen through a lens: shows how much of it the Moon covers
void drawSolarInset(Ctx& c, const skyev::Event& e, int idx, float cx, float cy, float R, bool labels, float sunX, float sunY) {
    App& a = c.a; Gfx& g = c.g; const Palette& P = c.P; const float s = c.s;
    const skyev::SolarCover& S = a.solarCover;
    if (!S.active || S.sunR <= 0) return;
    Color col = tint(P, kindColor(e.kind));
    const bool hov = a.hoverOn && a.hoverEvent == idx;
    float dx = cx - sunX, dy = cy - sunY, L = std::max(1.f, std::hypot(dx, dy));
    if (L < 30.f * s) { dx = 0; dy = 1; L = 1; }
    float rin = std::max(30.f * s, 0.095f * R);
    float ix = sunX + dx / L * (rin + 40 * s), iy = sunY + dy / L * (rin + 40 * s);
    g.line(sunX, sunY, ix, iy, withA(col, 0.35f), std::max(1.f, s), true);
    g.fillCircle(ix, iy, rin, Color{0.02f, 0.03f, 0.06f, 0.88f});
    float rs = rin * 0.62f;
    const float cover = (float)a.solarCover.moonR;
    float obsc = (float)std::min(1.0, std::max(0.0, e.obscuration));
    g.glow(ix, iy, rs * 2.6f, tint(P, rgb(0xFFD58A)), 0.5f * (1.f - 0.6f * obsc));
    g.fillCircle(ix, iy, rs, tint(P, rgb(0xFFF1C8)));
    float k = rs / (float)S.sunR;
    float th = (float)astro::rad(moonDomeRotation(a.moon, a.st.geo));
    float sx = -(float)S.dx * k, sy = -(float)S.dy * k;
    float mx = ix + sx * std::cos(th) - sy * std::sin(th), my = iy + sx * std::sin(th) + sy * std::cos(th);
    float mrp = cover * k;
    if (S.sep < S.sunR - S.moonR + 1e-4 && S.moonR >= S.sunR) {   // totality: corona
        for (int i = 0; i < 12; ++i) { float t = (float)(i * PI_ / 6 + 0.3); g.line(ix + rs * 1.05f * std::cos(t), iy + rs * 1.05f * std::sin(t), ix + rs * (1.5f + 0.3f * hf(i, 2)) * std::cos(t), iy + rs * (1.5f + 0.3f * hf(i, 2)) * std::sin(t), Color{1.f, 0.97f, 0.9f, 0.5f}, std::max(1.f, 1.4f * s), true); }
    }
    g.fillCircle(mx, my, mrp, Color{0.015f, 0.016f, 0.03f, 1.f});
    g.strokeCircle(ix, iy, rin, withA(col, hov ? 0.9f : 0.6f), std::max(1.f, 1.3f * s));
    if (labels) label(g, s, ix, iy + rin + 16 * s, fmt("%.0f%% COVERED", e.obscuration * 100), withA(col, 0.9f), Align::Center);
    addRoundHit(a, ix, iy, rin + 6 * s, idx);
}

} // namespace

std::string eventKindLabel(int kind) { return kindLabel((skyev::Kind)kind); }
Color eventKindColor(int kind) { return kindColor((skyev::Kind)kind); }
std::string eventWhenShort(const App& a, const skyev::Event& e) { return whenShort(a, e); }
void addEventRowHit(App& a, float x, float y, float w, float h, int idx) { addEventHit(a, x, y, w, h, idx, false); }

// ---------------------------------------------------------------- public
std::string eventText(const App& a, const std::string& in) {
    std::string out;
    for (size_t i = 0; i < in.size();) {
        if (in[i] == '{') {
            size_t c = in.find(':', i), e = in.find('}', i);
            if (c != std::string::npos && e != std::string::npos && c < e) {
                std::string k = in.substr(i + 1, c - i - 1);
                double v = std::atof(in.substr(c + 1, e - c - 1).c_str());
                int off = a.tzAt ? a.tzAt(v + 30.0) : a.tzOff;
                util::LocalTime L = util::toLocal(v + 30.0, off);
                std::string d = fmt("%s %d %s", util::WEEKDAYS3[L.wday], L.d, util::MONTHS3[L.m - 1]);
                out += k == "t" ? clockStr(a, v) : k == "d" ? d : d + ", " + clockStr(a, v);
                i = e + 1; continue;
            }
        }
        out += in[i++];
    }
    return out;
}

bool eventNamesPlanet(const App& a, int id) {
    if (a.sun.hor.alt > 5.0) return false;
    for (const auto& e : a.events) {
        if (e.kind != skyev::CONJUNCTION && e.kind != skyev::MOON_NEAR) continue;
        for (int i = 0; i < e.nBodies; ++i) if (e.bodies[i] == id) return true;
    }
    return false;
}

void eventsBeginFrame(App& a) {
    a.eventsAnimating = false;
    int best = -1; float bd = 1e9, ax = 0, ay = 0;
    const bool modal = a.showHelp || a.showSettings || a.isSaver;
    const bool allow = !modal && a.mouseX >= 0 && (a.now - a.lastMouseMove) < 12.0;
    if (allow) {
        for (const auto& h : a.eventHits) {
            float hx = h.x + h.w * 0.5f, hy = h.y + h.h * 0.5f, dd = std::hypot(a.mouseX - hx, a.mouseY - hy);
            bool in = h.round ? dd <= h.w * 0.5f : (a.mouseX >= h.x && a.mouseX <= h.x + h.w && a.mouseY >= h.y && a.mouseY <= h.y + h.h);
            if (in && dd < bd) { bd = dd; best = h.idx; ax = hx; ay = hy; }
        }
    }
    if (best >= (int)a.events.size()) best = -1;
    if (best >= 0) { a.hoverEvent = best; a.hoverX = ax; a.hoverY = ay; }
    a.hoverOn = best >= 0;
    const float tgt = a.hoverOn ? 1.f : 0.f;
    if (modal) a.hoverT = 0.f;
    else {
        a.hoverT += (tgt - a.hoverT) * (1.f - std::exp(-(float)a.dt * 14.f));
        if (std::fabs(a.hoverT - tgt) < 0.01f) a.hoverT = tgt;
    }
    a.eventHits.clear();
}

void drawEventsBelow(Ctx& c, float cx, float cy, float R, bool labels) {
    App& a = c.a;
    const float night = smoothstep(-4.f, -14.f, (float)a.sun.hor.alt);
    const float skyA = 1.f - smoothstep(-3.f, 5.f, (float)a.sun.hor.alt);
    const float moonSize = std::max(40.f * c.s, 0.19f * R);
    for (size_t i = 0; i < a.events.size(); ++i) {
        const skyev::Event& e = a.events[i];
        switch (e.kind) {
            case skyev::METEOR: drawMeteors(c, e, (int)i, cx, cy, R, labels, night, proximity(a, e), false); break;
            case skyev::CONJUNCTION: case skyev::MOON_NEAR: case skyev::PARADE: case skyev::OPPOSITION: case skyev::ELONGATION:
                drawGroup(c, e, (int)i, cx, cy, R, labels, false, skyA); break;
            case skyev::LUNAR_ECLIPSE: drawLunarEclipse(c, e, (int)i, cx, cy, R, labels, moonSize, false); break;
            default: break;
        }
    }
    if (a.lunarShadow.active || a.solarCover.active) a.eventsAnimating = true;
}

void drawEventsAbove(Ctx& c, float cx, float cy, float R, bool labels, float moonSize, float sunX, float sunY, bool sunUp) {
    App& a = c.a;
    const float night = smoothstep(-4.f, -14.f, (float)a.sun.hor.alt);
    const float skyA = 1.f - smoothstep(-3.f, 5.f, (float)a.sun.hor.alt);
    for (size_t i = 0; i < a.events.size(); ++i) {
        const skyev::Event& e = a.events[i];
        switch (e.kind) {
            case skyev::METEOR: drawMeteors(c, e, (int)i, cx, cy, R, labels, night, proximity(a, e), true); break;
            case skyev::CONJUNCTION: case skyev::MOON_NEAR: case skyev::PARADE: case skyev::OPPOSITION: case skyev::ELONGATION:
                drawGroup(c, e, (int)i, cx, cy, R, labels, true, skyA); break;
            case skyev::LUNAR_ECLIPSE: drawLunarEclipse(c, e, (int)i, cx, cy, R, labels, moonSize, true); break;
            case skyev::SOLAR_ECLIPSE: if (sunUp && e.active(a.now) && e.visible) drawSolarInset(c, e, (int)i, cx, cy, R, labels, sunX, sunY); break;
            default: break;
        }
    }
}

void drawEventChips(Ctx& c, float x, float y, float w) {
    App& a = c.a; Gfx& g = c.g; const Palette& P = c.P; const float s = c.s;
    if (!a.st.evChips || a.events.empty()) return;
    const float h = 26.f * s, gap = 8.f * s, fs = 11.5f * s, padL = 24.f * s, padR = 13.f * s;
    struct Chip { int idx; std::wstring text; float w; };
    std::vector<Chip> chips;
    float total = 0;
    const int maxChips = 3;
    int shown = 0;
    for (size_t i = 0; i < a.events.size() && shown < maxChips; ++i) {
        const skyev::Event& e = a.events[i];
        std::string t = chipName(e) + "  \xC2\xB7  " + whenShort(a, e);
        std::wstring wt = util::widen(t);
        float cw = g.textWidth(F_MONO_M, fs, wt, 0.08f) + padL + padR;
        if (!chips.empty() && total + gap + cw > w) break;
        if (cw > w) { wt = g.ellipsize(F_MONO_M, fs, wt, w - padL - padR, 0.08f); cw = w; }
        chips.push_back({(int)i, wt, cw}); total += (chips.size() > 1 ? gap : 0) + cw; ++shown;
    }
    int rest = (int)a.events.size() - shown;
    std::wstring more;
    float mw = 0;
    if (rest > 0 && total + gap + 40 * s < w) { more = util::widen(fmt("+%d", rest)); mw = g.textWidth(F_MONO_M, fs, more, 0.08f) + 22 * s; total += gap + mw; }
    float x0 = x + (w - total) * 0.5f;
    for (auto& ch : chips) {
        const skyev::Event& e = a.events[ch.idx];
        const bool hov = a.hoverOn && a.hoverEvent == ch.idx;
        const float k = e.visible ? 1.f : 0.62f;
        Color col = tint(P, kindColor(e.kind));
        g.fillRoundRect(x0, y, ch.w, h, h * 0.5f, hov ? mulA(P.ink, 0.12f) : P.panel);
        g.strokeRoundRect(x0 + 0.5f, y + 0.5f, ch.w - 1.f, h - 1.f, h * 0.5f, hov ? withA(col, 0.8f) : P.line, std::max(1.f, s));
        g.glow(x0 + 13 * s, y + h * 0.5f, 10 * s, col, 0.5f * k);
        g.fillCircle(x0 + 13 * s, y + h * 0.5f, 3.2f * s, withA(col, k));
        g.text(F_MONO_M, fs, x0 + padL, y + h * 0.5f + g.capHeight(F_MONO_M, fs) * 0.5f, ch.text, hov ? P.ink : mulA(P.ink2, 0.5f + 0.5f * k), Align::Left, 0.08f);
        addEventHit(a, x0, y, ch.w, h, ch.idx, false);
        x0 += ch.w + gap;
    }
    if (!more.empty()) {
        g.fillRoundRect(x0, y, mw, h, h * 0.5f, P.panel);
        g.strokeRoundRect(x0 + 0.5f, y + 0.5f, mw - 1.f, h - 1.f, h * 0.5f, P.line, std::max(1.f, s));
        g.text(F_MONO_M, fs, x0 + mw * 0.5f, y + h * 0.5f + g.capHeight(F_MONO_M, fs) * 0.5f, more, P.ink3, Align::Center, 0.08f);
    }
}

void drawEventTooltip(Ctx& c) {
    App& a = c.a;
    if (a.hoverT < 0.01f || a.hoverEvent < 0 || a.hoverEvent >= (int)a.events.size()) return;
    const skyev::Event& e = a.events[a.hoverEvent];
    Gfx& g = c.g; const Palette& P = c.P; const float s = c.s, W = c.W, H = c.H;
    const float al = smoothstep(0.f, 1.f, a.hoverT);
    Color kc = tint(P, kindColor(e.kind));
    const float cw = std::min(W - 32 * s, 470 * s), pad = 22 * s, lab = 150 * s;
    const float vw = cw - pad * 2 - lab;
    const float tfs = 28 * s, sfs = 15.f * s, rfs = 15.5f * s;

    std::vector<std::wstring> tl = g.wrap(F_SERIF, tfs, util::widen(e.title), cw - pad * 2 - 6 * s, 0, 2);
    std::vector<std::wstring> sl = g.wrap(F_LIGHT, sfs, util::widen(eventText(a, e.subtitle)), cw - pad * 2 - 6 * s, 0, 2);
    struct Row { std::wstring label; std::vector<std::wstring> lines; };
    std::vector<Row> rows;
    for (const auto& d : e.details) rows.push_back({util::widen(util::upper(d.label)), g.wrap(F_LIGHT, rfs, util::widen(eventText(a, d.value)), vw, 0, 3)});

    float hgt = pad + 14 * s + 16 * s + tl.size() * tfs * 1.1f + 8 * s + sl.size() * sfs * 1.4f + 16 * s;
    for (auto& r : rows) hgt += std::max<size_t>(1, r.lines.size()) * rfs * 1.38f + 9 * s;
    hgt += pad - 6 * s;

    float x = a.hoverX > W * 0.5f ? a.hoverX - cw - 30 * s : a.hoverX + 30 * s;
    x = std::max(12 * s, std::min(W - cw - 12 * s, x));
    float y = a.hoverY - hgt * 0.3f;
    y = std::max(12 * s, std::min(H - hgt - 12 * s, y)) + (1.f - al) * 8 * s;

    g.fillRoundRect(x - 2 * s, y + 6 * s, cw + 4 * s, hgt, 22 * s, Color{0, 0, 0, 0.22f * al});
    g.fillRoundRect(x, y, cw, hgt, 20 * s, withA(P.plate, 0.975f * al));
    g.strokeRoundRect(x + 0.5f, y + 0.5f, cw - 1.f, hgt - 1.f, 20 * s, mulA(P.line, al), std::max(1.f, s));
    g.fillRoundRect(x + 10 * s, y + 22 * s, 3.f * s, hgt - 44 * s, 1.5f * s, withA(kc, 0.85f * al));

    float yy = y + pad + 8 * s;
    g.text(F_MONO_M, 11.5f * s, x + pad, yy, util::widen(kindLabel(e.kind)), withA(kc, al), Align::Left, 0.14f);
    g.text(F_MONO, 11.5f * s, x + cw - pad, yy, util::widen(whenLong(a, e)), mulA(P.ink3, al), Align::Right, 0.12f);
    yy += 16 * s;
    for (auto& t : tl) { yy += tfs * 1.0f; g.text(F_SERIF, tfs, x + pad, yy, t, mulA(P.ink, al)); yy += tfs * 0.1f; }
    yy += 6 * s;
    for (auto& t : sl) { yy += sfs * 1.4f; g.text(F_LIGHT, sfs, x + pad, yy, t, mulA(P.ink2, al)); }
    yy += 12 * s;
    g.line(x + pad, yy, x + cw - pad, yy, mulA(P.line, al), std::max(1.f, s), false);
    yy += 6 * s;
    for (auto& r : rows) {
        float by = yy + rfs * 1.1f;
        g.text(F_MONO, 11.f * s, x + pad, by - 1 * s, r.label, mulA(P.ink3, al), Align::Left, 0.1f);
        for (size_t i = 0; i < r.lines.size(); ++i) g.text(F_LIGHT, rfs, x + pad + lab, by + i * rfs * 1.38f, r.lines[i], mulA(P.ink, al));
        yy += std::max<size_t>(1, r.lines.size()) * rfs * 1.38f + 9 * s;
    }
}

} // namespace app
