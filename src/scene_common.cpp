// Meridian scene parts shared by several faces: sky dome, day band, weather, today list.
#include "scene.h"
#include <cmath>
#include <algorithm>

namespace app {

using namespace gfx;
using astro::Equatorial; using astro::Horizontal;
using util::fmt;

// ---------------------------------------------------------------- small helpers
ClockParts clockParts(const App& a) {
    util::LocalTime L = util::toLocal(a.now, a.tzOff);
    ClockParts p;
    if (a.st.h24) p.hm = Ws(fmt("%02d:%02d", L.hh, L.mm));
    else { int h = L.hh % 12; if (h == 0) h = 12; p.hm = Ws(fmt("%d:%02d", h, L.mm)); p.ampm = L.hh < 12 ? L"AM" : L"PM"; }
    p.sec = Ws(fmt("%02d", L.ss));
    return p;
}

std::string tempStr(const App& a, double c) { return fmt("%.0f°", weather::toUnit(c, a.st.useF)); }
std::string clockStr(const App& a, double t) { return util::fmtClock(t, a.tzAt ? a.tzAt(t) : a.tzOff, a.st.h24); }
std::string clockStrDay(const App& a, double t) {
    const int tzT = a.tzAt ? a.tzAt(t + 30.0) : a.tzOff;
    util::LocalTime L = util::toLocal(t + 30.0, tzT), N = util::toLocal(a.now, a.tzOff);
    if (L.dayIndex == N.dayIndex) return util::fmtClock(t, tzT, a.st.h24);
    return std::string(util::WEEKDAYS3[L.wday]) + " " + util::fmtClock(t, tzT, a.st.h24);
}

Color tint(const Palette& P, const Color& c) { return P.ember > 0.001f ? emberize(c, P.ember) : c; }

SkyCol skyColors(double alt, float cloud) {
    struct K { double alt; unsigned zen, hor; };
    static const K ks[] = {
        {  16, 0x3F86D6, 0xA9D3F2 }, {   6, 0x4A82CB, 0xC4D6E8 }, {   0, 0x35508E, 0xF2A56B },
        {  -3, 0x242F66, 0xD9708A }, {  -6, 0x171E4C, 0x8D4F87 }, { -10, 0x0D1332, 0x3A3664 },
        { -14, 0x0A1030, 0x24325E }, { -20, 0x060B1D, 0x17264A },
    };
    const int n = (int)(sizeof ks / sizeof ks[0]);
    SkyCol out;
    if (alt >= ks[0].alt) { out.zen = rgb(ks[0].zen); out.hor = rgb(ks[0].hor); }
    else if (alt <= ks[n - 1].alt) { out.zen = rgb(ks[n - 1].zen); out.hor = rgb(ks[n - 1].hor); }
    else {
        for (int i = 0; i + 1 < n; ++i) if (alt <= ks[i].alt && alt >= ks[i + 1].alt) {
            float t = (float)((ks[i].alt - alt) / (ks[i].alt - ks[i + 1].alt));
            t = t * t * (3 - 2 * t);
            out.zen = lerp(rgb(ks[i].zen), rgb(ks[i + 1].zen), t);
            out.hor = lerp(rgb(ks[i].hor), rgb(ks[i + 1].hor), t);
            break;
        }
    }
    if (cloud > 0.01f) {      // overcast: wash toward grey
        auto grey = [&](Color c, float k) { float L = 0.299f * c.r + 0.587f * c.g + 0.114f * c.b; return lerp(c, Color{L * 0.92f, L * 0.95f, L, 1.f}, k); };
        out.zen = grey(out.zen, cloud * 0.55f); out.hor = grey(out.hor, cloud * 0.5f);
    }
    return out;
}

void addHit(App& a, float x, float y, float w, float h, int id) { a.hits.push_back({x + a.shiftX, y + a.shiftY, w, h, id}); }

void drawButton(Ctx& c, float x, float y, float w, float h, const std::wstring& label, bool active, int hitId, FontId font, float size) {
    Gfx& g = c.g; const Palette& P = c.P; float s = c.s;
    const float mx = c.a.mouseX - c.a.shiftX, my = c.a.mouseY - c.a.shiftY;
    bool hover = mx >= x && mx <= x + w && my >= y && my <= y + h;
    if (active) g.fillRoundRect(x, y, w, h, h * 0.5f, mulA(P.ink, 0.92f));
    else {
        g.fillRoundRect(x, y, w, h, h * 0.5f, hover ? mulA(P.ink, 0.10f) : P.panel);
        g.strokeRoundRect(x + 0.5f, y + 0.5f, w - 1.f, h - 1.f, h * 0.5f, P.line, std::max(1.f, s));
    }
    Color tc = active ? P.plate : (hover ? P.ink : P.ink2);
    float fs = size * s;
    g.text(font, fs, x + w * 0.5f, y + h * 0.5f + g.capHeight(font, fs) * 0.5f, label, tc, Align::Center, 0.08f);
    if (hitId) addHit(c.a, x, y, w, h, hitId);
}

static float hashf(int i, int salt) {
    unsigned x = (unsigned)i * 374761393u + (unsigned)salt * 668265263u;
    x = (x ^ (x >> 13)) * 1274126177u; x ^= x >> 16;
    return (x & 0xFFFF) / 65535.f;
}

static Pt domePt(float cx, float cy, float R, double alt, double az) {
    float r = R * (float)(90.0 - alt) / 90.f;
    return {cx - r * (float)std::sin(astro::rad(az - gSkyHeading)), cy - r * (float)std::cos(astro::rad(az - gSkyHeading))};
}

// ---------------------------------------------------------------- the sky dome
void drawDome(Ctx& c, float cx, float cy, float R, bool labels) {
    Gfx& g = c.g; App& a = c.a; const Palette& P = c.P; const float s = c.s;
    const double now = a.now; const astro::Geo& geo = a.st.geo;
    const double sunAlt = a.sun.hor.alt;
    const float night = smoothstep(-4.f, -14.f, (float)sunAlt);
    const float cloud = a.wx.valid ? std::min(1.f, (float)a.wx.cur.cloud / 100.f) : 0.f;
    const float lightLoss = (1.f - 0.65f * a.gloom) * (1.f - 0.85f * a.haze);     // cloud and fog hide stars, and dim the Sun and Moon
    // The theme paints the sky map (Palette::skyOwn: 1 in every fixed theme, 0 in Auto, which follows the real Sun). A dark theme keeps a hint of stars even
    // while the Sun is up, a Sunset or a Dusk sky a little less, a light one none (Palette::starVis says how much); at night they are out in full.
    const float own = clamp01(P.skyOwn);
    const float starA = std::max(night, own * clamp01(P.starVis) * 0.5f) * (1.f - 0.45f * P.ember) * lightLoss;
    const float o = R + 36 * s;
    auto T = [&](Color col) { return tint(P, col); };
    auto dim = [&](Color col) { float k = 1.f - 0.5f * P.ember; col.r *= k; col.g *= k; col.b *= k; return col; };

    g.pushCircleClip(cx, cy, R);

    // sky gradient. At twilight the far horizon stays cool and only the sun's side glows.
    const float sNight = smoothstep(-2.f, -12.f, (float)sunAlt);
    const float sTwi = smoothstep(14.f, 2.f, (float)sunAlt) * (1.f - sNight);
    const float pNight = clamp01(P.nightness), pTwi = std::min(clamp01(P.twilight), 1.f - pNight);
    // fixed daytime themes switch to their dark skyNZen/skyNHor at real midnight, so wNight must follow night too (preventing milky fog wash)
    const float wNight = std::max(sNight + (pNight - sNight) * own, night * own);
    const float wTwi = std::min(sTwi + (pTwi - sTwi) * own, 1.f - wNight);
    const float wDay = std::max(0.f, 1.f - wNight - wTwi);
    const float tw = smoothstep(-15.f, -2.f, (float)sunAlt) * (1.f - smoothstep(5.f, 16.f, (float)sunAlt)) * (1.f - own * pNight);
    SkyCol sc = skyColors(sunAlt, 0.f);
    if (own > 0.001f) {   // the theme's own sky, turning to its night version as the real Sun goes down (a Sunset sky at midnight is deep plum with a violet rim)
        sc.zen = lerp(sc.zen, lerp(P.skyZen, P.skyNZen, night), own); sc.hor = lerp(sc.hor, lerp(P.skyHor, P.skyNHor, night), own);
    }
    {   // the cloud cover from the forecast washes a day sky grey, not a night or a sunset one
        const float wash = cloud * (1.f - 0.85f * (wNight + wTwi));
        if (wash > 0.01f) {
            auto grey = [&](Color c, float k) { float L = 0.299f * c.r + 0.587f * c.g + 0.114f * c.b; return lerp(c, Color{L * 0.92f, L * 0.95f, L, 1.f}, k); };
            sc.zen = grey(sc.zen, wash * 0.55f); sc.hor = grey(sc.hor, wash * 0.5f);
        }
    }
    {   // the weather's light. By day: heavy cloud closes the sky in (darker, greyer, bluer), fog makes it flat and milky.
        // By night: deeper and darker, the same blue-black. At dusk and dawn: richer, the purple and the orange come out.
        const float gl = a.gloom, hz = a.haze, wx = std::max(gl, hz);
        auto moodSky = [&](Color c) {
            if (wx < 0.001f) return c;
            const float L = 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
            Color day = c;
            if (gl > 0.001f) {
                day = lerp(day, Color{L * 0.90f, L * 0.97f, std::min(1.f, L * 1.07f), 1.f}, 0.85f * gl);
                const float f = 1.f - 0.45f * gl; day.r *= f; day.g *= f; day.b *= f;
            }
            if (hz > 0.001f) { const float Lm = L * 0.80f + 0.14f; day = lerp(day, Color{Lm, Lm * 1.01f, Lm * 1.03f, 1.f}, 0.72f * hz); }
            Color nt = satur(c, 1.f + 0.35f * wx); { const float f = 1.f - 0.62f * wx; nt.r *= f; nt.g *= f; nt.b *= f; }
            Color twc = satur(c, 1.f + 0.55f * wx); { const float f = 1.f - 0.16f * gl; twc.r *= f; twc.g *= f; twc.b *= f; }
            return Color{day.r * wDay + nt.r * wNight + twc.r * wTwi, day.g * wDay + nt.g * wNight + twc.g * wTwi, day.b * wDay + nt.b * wNight + twc.b * wTwi, 1.f};
        };
        sc.zen = moodSky(sc.zen); sc.hor = moodSky(sc.hor);
        if (hz > 0.001f) sc.zen = lerp(sc.zen, sc.hor, (0.5f * wDay + 0.30f * wTwi + 0.15f * wNight) * hz);
    }
    Color zen = dim(T(sc.zen)), hor = dim(T(sc.hor));
    Color horBase = lerp(hor, lerp(zen, hor, 0.12f), tw * 0.7f);
    g.radialFill(cx, cy, R, {{0.f, zen}, {0.5f, lerp(zen, horBase, 0.18f)}, {0.82f, lerp(zen, horBase, 0.58f)}, {1.f, horBase}});

    // twilight glow toward the sun
    if (tw > 0.01f) {
        float rr = std::max(0.62f * R, std::min(1.04f * R, R * (float)(90.0 - a.sun.hor.alt) / 90.f));
        float az = (float)astro::rad(a.sun.hor.az - gSkyHeading);
        float gx = cx - rr * std::sin(az), gy = cy - rr * std::cos(az);
        Color warm = lerp(rgb(0xFF9A4D), rgb(0xB4508E), smoothstep(-2.f, -9.f, (float)sunAlt));
        g.glow(gx, gy, 1.55f * R, dim(T(hor)), tw * 0.55f * (1.f - 0.5f * cloud));
        g.glow(gx, gy, 1.05f * R, dim(T(warm)), tw * 0.95f * (1.f - 0.5f * cloud));
    }

    // soft clouds from the current weather
    if (a.st.elClouds && cloud > 0.12f) {
        int n = (int)std::lround(2 + cloud * 9);
        double day = std::floor(now / 86400.0);
        const float lim = R * 1.05f, span = lim * 2.f;
        const float drift = (float)std::fmod(now * 0.004 * (double)s, (double)span);
        float wd = (float)astro::rad((a.wx.valid ? a.wx.cur.windDir + 180.0 : 68.75) - gSkyHeading);
        bool dayish = sunAlt > -4;
        auto wrapSym = [&](float v) { float r = std::fmod(v + lim, span); if (r < 0.f) r += span; return r - lim; };
        for (int i = 0; i < n; ++i) {
            float hx = hashf(i, (int)day), hy = hashf(i + 50, (int)day), hr = hashf(i + 99, (int)day);
            float dx = wrapSym((hx * 2.f - 1.f) * R * 0.95f + drift * std::sin(wd));
            float dy = wrapSym((hy * 2.f - 1.f) * R * 0.95f - drift * std::cos(wd));
            float rr = R * (0.16f + 0.2f * hr);
            Color cc = dayish ? Color{1.f, 1.f, 1.f, 1.f} : Color{0.42f, 0.47f, 0.60f, 1.f};
            g.glow(cx + dx, cy + dy, rr, dim(T(cc)), (dayish ? 0.22f : 0.16f) * (0.5f + 0.5f * cloud));
        }
    }

    // star positions (only computed when stars are actually visible)
    const auto& S = astro::stars();
    std::vector<Horizontal> sh;
    if (starA > 0.02f) {
        sh.resize(S.size());
        for (size_t i = 0; i < S.size(); ++i) {
            Equatorial e{S[i].raH * 15.0, S[i].decDeg};
            sh[i] = astro::toHorizontal(astro::precessJ2000(e, now), now, geo);
        }
    }

    if (starA > 0.02f && a.st.elMilky) {
        // Milky Way band
        float mwA = starA * (1.f - 0.85f * cloud);
        std::vector<Pt> run;
        auto flush = [&]() {
            if (run.size() >= 2) {
                Color mw = T(Color{0.70f, 0.78f, 1.0f, 1.f});
                for (int k = 0; k < 8; ++k) {
                    float t = k / 7.f;
                    g.polyline(run, false, withA(mw, 0.016f * mwA), (0.27f - 0.22f * t) * R, true);
                }
            }
            run.clear();
        };
        for (int l = 0; l <= 360; l += 3) {
            Horizontal h = astro::toHorizontal(astro::precessJ2000(astro::galacticToEquatorialJ2000(l, 0.0), now), now, geo);
            if (h.alt > -3.0) run.push_back(domePt(cx, cy, R, std::max(h.alt, -3.0), h.az)); else flush();
        }
        flush();
        Horizontal gc = astro::toHorizontal(astro::precessJ2000(astro::galacticToEquatorialJ2000(0.0, 0.0), now), now, geo);
        if (gc.alt > 0) { Pt p = domePt(cx, cy, R, gc.alt, gc.az); g.glow(p.x, p.y, 0.2f * R, T(Color{0.85f, 0.85f, 1.f, 1.f}), 0.07f * mwA); }
    }

    // altitude rings, ecliptic
    Color ringC = mulA(withA(P.dark ? Color{1, 1, 1, 1} : Color{1, 1, 1, 1}, 1.f), 0.0f);
    (void)ringC;
    g.strokeCircle(cx, cy, R * 2.f / 3.f, Color{1, 1, 1, 0.07f}, std::max(1.f, s));
    g.strokeCircle(cx, cy, R / 3.f, Color{1, 1, 1, 0.07f}, std::max(1.f, s));
    if (a.st.elEcliptic) {
        float eclA = 0.16f + 0.26f * night;
        double eps = 23.4393 - 0.013 * (astro::julianDay(now) - 2451545.0) / 36525.0;
        for (int l = 0; l < 360; l += 3) {
            Horizontal h = astro::toHorizontal(astro::eclipticToEquatorial(l, 0.0, eps), now, geo);
            if (h.alt < 1.5) continue;
            Pt p = domePt(cx, cy, R, h.alt, h.az);
            g.fillCircle(p.x, p.y, 1.0f * s, T(withA(P.accent, eclA)));
        }
    }

    // constellation lines and stars
    if (starA > 0.02f) {
        if (a.st.elConst) for (const auto& con : astro::constellations())
            for (const auto& ln : con.lines) {
                const Horizontal &h1 = sh[ln.a], &h2 = sh[ln.b];
                if (h1.alt < 1 || h2.alt < 1) continue;
                Pt p1 = domePt(cx, cy, R, h1.alt, h1.az), p2 = domePt(cx, cy, R, h2.alt, h2.az);
                g.line(p1.x, p1.y, p2.x, p2.y, T(Color{0.75f, 0.82f, 1.f, 0.26f * starA * (1.f - 0.8f * cloud)}), std::max(1.f, 1.0f * s), false);
            }
        for (size_t i = 0; i < S.size(); ++i) {
            const Horizontal& h = sh[i];
            if (h.alt < 1.0) continue;
            float vis = smoothstep(1.f, 10.f, (float)h.alt) * starA * (1.f - 0.82f * cloud);
            float mag = (float)S[i].mag;
            float r = std::max(1.0f, 2.5f - 0.36f * mag) * s;
            float al = clamp01(1.25f - 0.17f * mag) * vis;
            Pt p = domePt(cx, cy, R, h.alt, h.az);
            if (mag < 1.0f) g.glow(p.x, p.y, r * 5.f, T(Color{0.85f, 0.9f, 1.f, 1.f}), 0.20f * vis);
            g.fillCircle(p.x, p.y, r, T(Color{0.94f, 0.96f, 1.f, al}));
            if (labels && a.st.elLabels && mag < 0.6f && h.alt > 8 && vis > 0.4f)
                g.text(F_MONO, 12.f * s, (p.x > cx + 0.45f * R) ? p.x - r - 5 * s : p.x + r + 5 * s, p.y + 4 * s, Ws(S[i].name), T(Color{0.85f, 0.9f, 1.f, 0.38f * vis}), (p.x > cx + 0.45f * R) ? Align::Right : Align::Left, 0.06f);
        }
    }

    // sun and moon tracks (today's path as dots)
    if (a.st.elTracks) for (const auto& tp : a.sunTrack) {
        if (tp.alt < 0.5f) continue;
        Pt p = domePt(cx, cy, R, tp.alt, tp.az);
        bool past = tp.t < now;
        g.fillCircle(p.x, p.y, 1.25f * s, T(withA(rgb(0xFFC27A), past ? 0.30f : 0.62f)));
    }
    if (a.st.elTracks) for (const auto& tp : a.moonTrack) {
        if (tp.alt < 0.5f) continue;
        Pt p = domePt(cx, cy, R, tp.alt, tp.az);
        bool past = tp.t < now;
        g.fillCircle(p.x, p.y, 1.15f * s, T(withA(rgb(0xBFD0FF), past ? 0.20f : 0.55f)));
    }

    // planets
    {
        static const unsigned pc[astro::PLANET_COUNT] = {0xC9C2B8, 0xFFF4D2, 0xFF8D63, 0xFFEBC8, 0xF2D9A0};
        for (const auto& pr : a.planets) {
            const astro::PlanetInfo& pi = pr.info;
            if (pi.appAlt < 1.0) continue;
            float dark = (pi.id == astro::VENUS) ? smoothstep(5.f, -5.f, (float)sunAlt) : smoothstep(-1.f, -9.f, (float)sunAlt);
            float vis = dark * smoothstep(1.f, 8.f, (float)pi.appAlt) * (1.f - 0.7f * cloud);
            if (vis < 0.03f) continue;
            Pt p = domePt(cx, cy, R, pi.appAlt, pi.hor.az);
            float r = (2.3f + std::max(0.f, -(float)pi.mag) * 0.55f) * s;
            Color col = T(rgb(pc[pi.id]));
            g.glow(p.x, p.y, r * 6.f, col, 0.24f * vis);
            g.fillCircle(p.x, p.y, r, withA(col, vis));
            if (labels && a.st.elLabels && !eventNamesPlanet(a, pi.id)) { bool left = p.x > cx + 0.35f * R; g.text(F_MONO, 12.f * s, left ? p.x - r - 6 * s : p.x + r + 6 * s, p.y + 4 * s, Ws(util::upper(pi.name)), withA(col, 0.62f * vis), left ? Align::Right : Align::Left, 0.1f); }
        }
    }

    drawEventsBelow(c, cx, cy, R, labels);

    // the Sun
    Pt sunP{0, 0}; bool sunUp = a.sun.appAlt > -0.6;
    if (sunUp) {
        sunP = domePt(cx, cy, R, std::max(0.0, a.sun.appAlt), a.sun.hor.az);
        float k = (float)std::max(0.35, std::min(1.0, 0.45 + a.sun.appAlt / 20.0));
        const float behind = (1.f - 0.60f * a.gloom) * (1.f - 0.70f * a.haze);     // behind cloud or fog the Sun is a pale smudge
        g.glow(sunP.x, sunP.y, 0.42f * R, T(rgb(0xFFB45A)), 0.55f * k * (1.f - 0.5f * cloud) * behind);
        g.glow(sunP.x, sunP.y, 0.14f * R, T(rgb(0xFFF1C8)), 0.9f * (0.45f + 0.55f * behind));
        g.fillCircle(sunP.x, sunP.y, 8.5f * s, T(rgb(0xFFF7DC)));
    }

    // the Moon
    Pt moonP{0, 0}; bool moonUp = a.moon.appAlt > -0.7;
    const float moonSize = std::max(40.f * s, 0.19f * R);
    if (moonUp) {
        moonP = domePt(cx, cy, R, std::max(0.0, a.moon.appAlt), a.moon.hor.az);
        float glowK = (float)a.moon.illum * (0.25f + 0.75f * night) * (1.f - 0.55f * cloud) * (1.f - 0.60f * a.gloom) * (1.f - 0.70f * a.haze);
        g.glow(moonP.x, moonP.y, 0.30f * R, T(rgb(0xBFD0FF)), 0.45f * glowK);
        g.drawBitmap(a.moonSmall, moonP.x, moonP.y, moonSize, moonDomeRotation(a.moon, geo), 1.f - 0.45f * cloud);
    }

    drawEventsAbove(c, cx, cy, R, labels, moonSize, sunP.x, sunP.y, sunUp);

    // end circular dome clip (preserves any backdrop/scenery/rain outside the dome without punching an opaque box or double-rendering scenery)
    g.popCircleClip();
    (void)o;

    // rim, ticks, cardinal letters
    {   // the rim: derived from the already-mooded horizon colour so overcast gloom never leaves a bright neon ring, and clear light themes stay visible
        const Color h = hor;
        const Color target = P.dark ? Color{1.f, 1.f, 1.f, 1.f} : P.ink;
        const Color own2 = tint(P, withA(lerp(h, target, P.dark ? 0.35f : 0.28f), P.dark ? 0.65f : 0.48f));
        g.strokeCircle(cx, cy, R, lerp(mulA(P.ink, 0.30f), own2, own), std::max(1.f, 1.3f * s));
    }
    for (int i = 0; i < 36; ++i) {
        float ang = (float)astro::rad(i * 10.0 - gSkyHeading);
        float len = (i % 9 == 0) ? 11.f * s : (i % 3 == 0 ? 7.f * s : 4.f * s);
        float r0 = R + 5 * s, r1 = r0 + len;
        g.line(cx - r0 * std::sin(ang), cy - r0 * std::cos(ang), cx - r1 * std::sin(ang), cy - r1 * std::cos(ang), mulA(P.ink, (i % 9 == 0) ? 0.5f : 0.22f), std::max(1.f, 1.1f * s), false);
    }
    {
        float lr = R + 30 * s; float fs = 15.f * s;
        struct L { const wchar_t* t; float ang; bool n; } ls[4] = {{L"N", 0, true}, {L"E", 90, false}, {L"S", 180, false}, {L"W", 270, false}};
        for (auto& l : ls) {
            float ang = (float)astro::rad(l.ang - gSkyHeading);
            float px = cx - lr * std::sin(ang), py = cy - lr * std::cos(ang);
            g.text(F_MONO_M, fs, px, py + g.capHeight(F_MONO_M, fs) * 0.5f, l.t, l.n ? P.accent : P.ink2, Align::Center, 0.0f);
        }
    }
    g.fillCircle(cx, cy, 1.6f * s, mulA(P.ink, 0.35f));
    // zenith label
    if (labels) g.text(F_MONO, 11.f * s, cx + 8 * s, cy - 6 * s, L"ZENITH", mulA(P.ink, 0.22f), Align::Left, 0.14f);

    // below-horizon markers on the rim, pointing at where they will rise
    if (!sunUp && sunAlt > -18.0) {
        Pt p = domePt(cx, cy, R, 0.0, a.sun.hor.az);
        float al = smoothstep(-18.f, -2.f, (float)sunAlt);
        g.strokeCircle(p.x, p.y, 6.5f * s, withA(P.accent, 0.85f * al), std::max(1.2f, 1.5f * s));
        g.fillCircle(p.x, p.y, 2.2f * s, withA(P.accent, 0.85f * al));
    }
    if (!moonUp && a.moon.hor.alt > -18.0) {
        Pt p = domePt(cx, cy, R, 0.0, a.moon.hor.az);
        float al = smoothstep(-18.f, -2.f, (float)a.moon.hor.alt);
        g.strokeCircle(p.x, p.y, 6.5f * s, withA(P.accent2, 0.9f * al), std::max(1.2f, 1.5f * s));
        g.fillCircle(p.x, p.y, 2.2f * s, withA(P.accent2, 0.9f * al));
    }
}

// ---------------------------------------------------------------- 24 hour band
void drawDayBand(Ctx& c, float x, float y, float w, float h) {
    Gfx& g = c.g; App& a = c.a; const Palette& P = c.P; const float s = c.s;
    if (a.bandSunAlt.size() != 288) return;
    const float cw = w / 72.f;
    for (int i = 0; i < 72; ++i) {
        SkyCol sc = skyColors(a.bandSunAlt[(size_t)i * 4 + 2], 0.f);
        Color col = tint(P, sc.hor);
        col.r *= 1.f - 0.5f * P.ember; col.g *= 1.f - 0.5f * P.ember; col.b *= 1.f - 0.5f * P.ember;
        g.fillRect(x + i * cw, y, cw + 0.7f, h, withA(col, c.alpha));
    }
    // moon-up line (contiguous runs)
    const float cw288 = w / 288.f;
    float my = y + h + 6 * s;
    for (int i = 0; i < 288;) {
        if (!a.bandMoonUp[i]) { ++i; continue; }
        int j = i; while (j < 288 && a.bandMoonUp[j]) ++j;
        g.fillRoundRect(x + i * cw288, my, (j - i) * cw288, 3.f * s, 1.5f * s, withA(P.accent2, 0.85f * c.alpha));
        i = j;
    }
    // sunrise / sunset ticks
    double d0 = util::localMidnight(a.now, a.tzOff);
    auto tick = [&](double t) { float tx = x + w * (float)((t - d0) / 86400.0); if (tx >= x && tx <= x + w) g.line(tx, y - 5 * s, tx, y, withA(P.accent, 0.9f * c.alpha), std::max(1.f, 1.4f * s), false); };
    if (a.dayEv.sun.hasRise) tick(a.dayEv.sun.rise);
    if (a.dayEv.sun.hasSet) tick(a.dayEv.sun.set);
    // hour labels
    static const wchar_t* l12[5] = {L"12A", L"6A", L"12P", L"6P", L"12A"};
    static const wchar_t* l24[5] = {L"00", L"06", L"12", L"18", L"24"};
    for (int i = 0; i < 5; ++i)
        g.text(F_MONO, 12.5f * s, x + w * i / 4.f, my + 24 * s, a.st.h24 ? l24[i] : l12[i], P.ink3, i == 0 ? Align::Left : (i == 4 ? Align::Right : Align::Center), 0.08f);
    // now marker
    double since = std::fmod(a.now + a.tzOff, 86400.0); if (since < 0) since += 86400.0;
    float nx = x + w * (float)(since / 86400.0);
    g.line(nx, y - 7 * s, nx, y + h + 3 * s, P.ink, std::max(1.5f, 2.f * s), true);
    g.fillCircle(nx, y - 9 * s, 3.2f * s, P.ink);
}

// ---------------------------------------------------------------- hourly curve
static std::vector<Pt> smoothCurve(const std::vector<Pt>& p) {
    std::vector<Pt> out;
    if (p.size() < 3) return p;
    for (size_t i = 0; i + 1 < p.size(); ++i) {
        Pt p0 = p[i == 0 ? 0 : i - 1], p1 = p[i], p2 = p[i + 1], p3 = p[i + 2 < p.size() ? i + 2 : i + 1];
        for (int k = 0; k < 10; ++k) {
            float t = k / 10.f, t2 = t * t, t3 = t2 * t;
            auto cr = [&](float a, float b, float cc, float d) { return 0.5f * ((2 * b) + (-a + cc) * t + (2 * a - 5 * b + 4 * cc - d) * t2 + (-a + 3 * b - 3 * cc + d) * t3); };
            out.push_back({cr(p0.x, p1.x, p2.x, p3.x), cr(p0.y, p1.y, p2.y, p3.y)});
        }
    }
    out.push_back(p.back());
    return out;
}

void drawHourly(Ctx& c, float x, float y, float w, float h) {
    Gfx& g = c.g; App& a = c.a; const Palette& P = c.P; const float s = c.s;
    const weather::Data& d = a.wx;
    if (!d.valid || d.hourly.empty()) return;
    int i0 = 0;
    for (size_t i = 0; i < d.hourly.size(); ++i) if (d.hourly[i].t <= a.now) i0 = (int)i; else break;
    int n = std::min<int>(13, (int)d.hourly.size() - i0);
    if (n < 3) return;
    double tmin = 1e9, tmax = -1e9;
    for (int i = 0; i < n; ++i) { double t = weather::toUnit(d.hourly[i0 + i].temp, a.st.useF); tmin = std::min(tmin, t); tmax = std::max(tmax, t); }
    double span = std::max(tmax - tmin, a.st.useF ? 6.0 : 3.5);
    double mid = (tmax + tmin) * 0.5; tmin = mid - span * 0.5; tmax = mid + span * 0.5;
    double maxP = 0; for (int i = 0; i < n; ++i) maxP = std::max(maxP, d.hourly[i0 + i].precipProb);
    float labH = std::min(17.f * s, h * 0.28f);
    float topPad = std::min(20.f * s, h * 0.25f);
    float barsH = maxP >= 20 ? std::min(26.f * s, std::max(4.f * s, (h - labH - topPad) * 0.35f)) : 4.f * s;
    float yb = y + h - labH;                         // bars baseline
    float yc0 = y + topPad, yc1 = std::max(yc0 + 8.f * s, yb - barsH - 6 * s); // guarantee yc1 > yc0 even in compact pinned tiles
    float xl = x + 10 * s, xr = x + w - 10 * s;
    std::vector<Pt> pts;
    for (int i = 0; i < n; ++i) {
        float px = xl + (xr - xl) * i / (float)(n - 1);
        double t = weather::toUnit(d.hourly[i0 + i].temp, a.st.useF);
        float py = yc1 - (float)((t - tmin) / (tmax - tmin)) * (yc1 - yc0);
        pts.push_back({px, py});
    }
    // rain bars
    for (int i = 0; i < n; ++i) {
        double pp = d.hourly[i0 + i].precipProb;
        if (pp < 20) continue;
        float bh = barsH * (float)pp / 100.f;
        g.fillRoundRect(pts[i].x - 3 * s, yb - bh, 6 * s, bh, 3 * s, withA(P.accent2, (0.30f + 0.35f * (float)pp / 100.f) * c.alpha));
    }
    g.line(xl - 4 * s, yb + 0.5f, xr + 4 * s, yb + 0.5f, P.line, std::max(1.f, s), false);
    std::vector<Pt> sm = smoothCurve(pts);
    std::vector<Pt> area = sm; area.push_back({sm.back().x, yc1 + 6 * s}); area.push_back({sm.front().x, yc1 + 6 * s});
    g.fillPolygon(area, withA(P.accent, 0.07f * c.alpha));
    g.polyline(sm, false, P.accent, std::max(1.6f, 2.f * s), true);
    for (int i = 0; i < n; ++i) {
        bool first = i == 0;
        g.fillCircle(pts[i].x, pts[i].y, (first ? 4.2f : 2.4f) * s, first ? P.accent : P.plate);
        if (!first) g.strokeCircle(pts[i].x, pts[i].y, 2.4f * s, P.accent, std::max(1.f, 1.4f * s));
        if (i % 3 == 0) {
            double t = weather::toUnit(d.hourly[i0 + i].temp, a.st.useF);
            g.text(F_MONO_M, 14.f * s, pts[i].x, pts[i].y - 11 * s, Ws(fmt("%.0f°", t)), first ? P.ink : P.ink2, Align::Center, 0.0f);
            std::wstring lab;
            if (first) lab = L"NOW";
            else {
                const double ht = d.hourly[i0 + i].t + 600.0;
                const int hoff = a.tzAt ? a.tzAt(ht) : a.tzOff;
                util::LocalTime L = util::toLocal(ht, hoff);
                int hh = L.hh; lab = a.st.h24 ? Ws(fmt("%02d", hh)) : Ws(fmt("%d%s", hh % 12 == 0 ? 12 : hh % 12, hh < 12 ? "A" : "P"));
            }
            g.text(F_MONO, 12.5f * s, pts[i].x, y + h - 2 * s, lab, P.ink3, Align::Center, 0.08f);
        }
    }
}

// ---------------------------------------------------------------- weather block
void drawWeatherBlock(Ctx& c, float x, float y, float w, float h) {
    Gfx& g = c.g; App& a = c.a; const Palette& P = c.P; const float s = c.s;
    const weather::Data& d = a.wx;
    if (!d.valid) {
        g.weatherIcon(weather::Icon::Cloudy, x + 36 * s, y + 44 * s, 64 * s, P.ink3, P.ink3, P.plate);
        g.text(F_LIGHT, 26 * s, x + 90 * s, y + 40 * s, L"Weather", P.ink2);
        g.text(F_MONO, 15 * s, x + 90 * s, y + 66 * s, g.ellipsize(F_MONO, 15 * s, Ws(util::upper(a.wxStatus.empty() ? "Connecting…" : a.wxStatus)), w - 90 * s, 0.1f), P.ink3, Align::Left, 0.1f);
        if (a.wxOffline && !a.wxDetail.empty())
            g.text(F_MONO, 12.5f * s, x + 90 * s, y + 90 * s, g.ellipsize(F_MONO, 12.5f * s, Ws(util::upper(a.wxDetail)), w - 90 * s, 0.08f), P.ink3, Align::Left, 0.08f);
        return;
    }
    const weather::Current& cu = d.cur;
    weather::Kind kd = weather::describe(cu.code, cu.isDay);
    float iconS = 84 * s;
    g.weatherIcon(kd.icon, x + iconS * 0.5f, y + 50 * s, iconS, P.ink, P.accent, P.plate);
    float tsz = 92 * s, tx = x + iconS + 22 * s, tb = y + 12 * s + g.capHeight(F_LIGHT, tsz);
    std::wstring tw = Ws(tempStr(a, cu.temp));
    g.text(F_LIGHT, tsz, tx, tb, tw, P.ink);
    float cx0 = tx + g.textWidth(F_LIGHT, tsz, tw) + 30 * s;
    float cwid = x + w - cx0;
    // condition + Air Quality emblem badge (air-waves icon + color-coded AQI value & rating; subtle placeholder while air quality loads)
    float condMaxW = cwid;
    if (cwid > 160 * s) {
        Color aqCol = P.ink3;
        std::wstring aqTxt = L"AQI \x2014";
        if (d.hasAir) {
            const double aq = d.aqi;
            aqCol = aq <= 50.0 ? rgb(0x62E2A0) : (aq <= 100.0 ? rgb(0xFFD26A) : (aq <= 150.0 ? rgb(0xFF9852) : rgb(0xFF6B6B)));
            aqCol = tint(P, aqCol);
            if (!P.dark) aqCol = lerp(aqCol, P.ink, 0.30f);
            const char* aqWord = aq <= 50.0 ? "GOOD" : (aq <= 100.0 ? "MODERATE" : (aq <= 150.0 ? "SENSITIVE" : (aq <= 200.0 ? "UNHEALTHY" : (aq <= 300.0 ? "VERY POOR" : "HAZARDOUS"))));
            aqTxt = Ws(fmt("AQI %.0f \xC2\xB7 %s", aq, aqWord));
        }
        const float afs = 12.5f * s, iconW = 19.f * s, padX = 11.f * s, pillH = 26.f * s;
        const float twAq = g.textWidth(F_MONO_M, afs, aqTxt, 0.08f);
        const float pillW = std::min(cwid * 0.56f, padX + iconW + 7.f * s + twAq + padX);
        const float px = x + w - pillW, py = y + 9.f * s, pcy = py + pillH * 0.5f;
        Color pbg = P.panel; pbg.a = std::max(pbg.a, P.dark ? 0.07f : 0.06f);
        g.fillRoundRect(px, py, pillW, pillH, pillH * 0.5f, pbg);
        g.strokeRoundRect(px + 0.5f, py + 0.5f, pillW - 1.f, pillH - 1.f, pillH * 0.5f, d.hasAir ? withA(aqCol, P.dark ? 0.42f : 0.50f) : P.line, std::max(1.f, s));
        // crisp vector air-quality waves emblem (three well-separated breeze lines with curl tips)
        const float ix = px + padX, lw = std::max(1.25f, 1.50f * s);
        g.line(ix, pcy - 4.4f * s, ix + 9.2f * s, pcy - 4.4f * s, aqCol, lw, true);
        g.arc(ix + 9.2f * s, pcy - 6.3f * s, 1.9f * s, 180.f, -245.f, aqCol, lw, true);
        g.line(ix + 1.2f * s, pcy, ix + 14.8f * s, pcy, aqCol, lw, true);
        g.arc(ix + 14.8f * s, pcy - 2.0f * s, 2.0f * s, 180.f, -245.f, aqCol, lw, true);
        g.line(ix, pcy + 4.4f * s, ix + 11.0f * s, pcy + 4.4f * s, withA(aqCol, 0.85f), lw, true);
        g.arc(ix + 11.0f * s, pcy + 6.3f * s, 1.9f * s, 0.f, 245.f, withA(aqCol, 0.85f), lw, true);
        const float txtRoom = std::max(40.f * s, pillW - (padX + iconW + 7.f * s + padX));
        g.text(F_MONO_M, afs, ix + iconW + 7.f * s, pcy + g.capHeight(F_MONO_M, afs) * 0.5f, g.ellipsize(F_MONO_M, afs, aqTxt, txtRoom, 0.08f), aqCol, Align::Left, 0.08f);
        condMaxW = std::max(90.f * s, cwid - pillW - 14.f * s);
    }
    g.text(F_LIGHT, 28 * s, cx0, y + 30 * s, g.ellipsize(F_LIGHT, 28 * s, Ws(kd.text), condMaxW), P.ink);
    std::string l2 = fmt("FEELS %s", tempStr(a, cu.feels).c_str());
    if (cu.hasHumidity) l2 += fmt(" \xC2\xB7 HUM %.0f%%", cu.humidity);
    if (cu.hasWind) l2 += fmt(" \xC2\xB7 WIND %.0f KM/H %s", cu.wind, util::compass16(cu.windDir).c_str());
    g.text(F_MONO, 15.5f * s, cx0, y + 58 * s, g.ellipsize(F_MONO, 15.5f * s, Ws(l2), cwid, 0.06f), P.ink2, Align::Left, 0.06f);
    std::string l3;
    if (!d.daily.empty()) {
        size_t di = 0; double d0 = util::localMidnight(a.now, d.utcOffset), best = 1e18;
        for (size_t i = 0; i < d.daily.size(); ++i) {
            double diff = std::fabs(d.daily[i].t0 - d0);
            if (diff < best && diff < 43200.0) { best = diff; di = i; }
        }
        const weather::Day& dd = d.daily[di];
        l3 = fmt("H %s · L %s", tempStr(a, dd.tmax).c_str(), tempStr(a, dd.tmin).c_str());
        if (d.hasUv) l3 += fmt(" · UV %.0f %s", dd.uvMax, util::upper(weather::uvLabel(dd.uvMax)).c_str());
    }
    if (d.hasAir && cwid <= 160 * s) l3 += fmt("%sAQI %.0f %s", l3.empty() ? "" : " · ", d.aqi, util::upper(weather::aqiLabel(d.aqi)).c_str());
    g.text(F_MONO, 15.5f * s, cx0, y + 82 * s, g.ellipsize(F_MONO, 15.5f * s, Ws(l3), cwid, 0.06f), P.ink2, Align::Left, 0.06f);
    // stale marker
    {
        std::string note;
        if (!d.source.empty() && d.source != weather::providerName(weather::PV_OPENMETEO)) note = "VIA " + util::upper(d.source);
        if (a.now - d.fetched > 3600) note += std::string(note.empty() ? "" : " · ") + "UPDATED " + clockStr(a, d.fetched);
        if (!note.empty()) g.text(F_MONO, 12.5f * s, x + w, y + 104 * s, Ws(note), P.ink3, Align::Right, 0.08f);
    }
    // summary sentence (cached per minute/fetch so it isn't rebuilt 60 times a second)
    static double sumFetched = -1; static long long sumMin = -1; static int sumTz = 0; static bool sum24 = false, sumF = false; static std::string sumCache;
    const long long curMin = (long long)std::floor(a.now / 60.0);
    if (d.fetched != sumFetched || curMin != sumMin || a.tzOff != sumTz || a.st.h24 != sum24 || a.st.useF != sumF) {
        sumFetched = d.fetched; sumMin = curMin; sumTz = a.tzOff; sum24 = a.st.h24; sumF = a.st.useF;
        sumCache = weather::summarize(d, a.now, a.tzOff, a.st.h24, a.st.useF, a.tzAt);
    }
    g.text(F_SERIF_I, 26 * s, x, y + 132 * s, g.ellipsize(F_SERIF_I, 26 * s, Ws(sumCache), w), P.ink2);
    // hourly
    float hy = y + 148 * s;
    drawHourly(c, x, hy, w, std::max(40.f * s, y + h - hy));
}

// ---------------------------------------------------------------- today list
void drawTodayList(Ctx& c, float x, float y, float w, float h) {
    Gfx& g = c.g; App& a = c.a; const Palette& P = c.P; const float s = c.s;
    const float rowH = 112 * s;
    int n = std::max(1, std::min(3, (int)std::floor((h + 20 * s) / rowH)));
    g.text(F_MONO, 14.f * s, x, y - 14 * s, L"TODAY", P.ink3, Align::Left, 0.22f);
    if (a.todayItems.empty()) {
        g.text(F_SERIF_I, 34 * s, x, y + 40 * s, L"A quiet day.", P.ink2);
        return;
    }
    int shown = 0;
    for (size_t i = 0; i < a.todayItems.size() && shown < n; ++i) {
        today::TodayItem it = a.todayItems[i];
        std::string tag = it.tag, title = it.title, detail = it.detail;
        if (tag == "ON THIS DAY") { tag += " · " + title; title = detail; detail.clear(); }
        float yy = y + shown * rowH;
        if (shown > 0) g.line(x, yy - 18 * s, x + w, yy - 18 * s, P.line, std::max(1.f, s), false);
        g.fillCircle(x + 4 * s, yy + 17 * s, 3.2f * s, P.accent);
        g.text(F_MONO_M, 14.5f * s, x + 18 * s, yy + 22 * s, Ws(tag), P.accent, Align::Left, 0.16f);
        float tsz = 33 * s;
        std::vector<std::wstring> tl = g.wrap(F_LIGHT, tsz, Ws(title), w, 0, detail.empty() ? 2 : 1);
        if (tl.size() > 1) {
            tsz = 26 * s;
            tl = g.wrap(F_LIGHT, tsz, Ws(title), w, 0, 2);
            float by = yy + 52 * s;
            for (size_t k = 0; k < tl.size(); ++k) g.text(F_LIGHT, tsz, x, by + k * 30 * s, tl[k], P.ink);
        } else {
            float by = yy + 60 * s;
            if (!tl.empty()) g.text(F_LIGHT, tsz, x, by, tl[0], P.ink);
        }
        if (!detail.empty()) g.text(F_LIGHT, 21 * s, x, yy + 88 * s, g.ellipsize(F_LIGHT, 21 * s, Ws(detail), w), P.ink2);
        ++shown;
    }
}

// ---------------------------------------------------------------- sun and moon caption lines
void drawSunMoonLines(Ctx& c, float x, float y, float w) {
    Gfx& g = c.g; App& a = c.a; const Palette& P = c.P; const float s = c.s;
    const float tagW = 74 * s;
    auto row = [&](float yy, const wchar_t* tag, const Color& tagC, const std::string& l1, const std::string& l2) {
        g.text(F_MONO_M, 14.5f * s, x, yy, tag, tagC, Align::Left, 0.18f);
        g.text(F_MEDIUM, 21.5f * s, x + tagW, yy + 1 * s, g.ellipsize(F_MEDIUM, 21.5f * s, Ws(l1), w - tagW), P.ink);
        g.text(F_LIGHT, 18.5f * s, x + tagW, yy + 29 * s, g.ellipsize(F_LIGHT, 18.5f * s, Ws(l2), w - tagW), P.ink2);
    };
    // sun
    {
        double alt = a.sun.appAlt;
        std::string l1;
        if (alt > 0) l1 = fmt("%.0f° up · %s", alt, util::compass16(a.sun.hor.az).c_str());
        else l1 = fmt("%.0f° below the horizon", -alt);
        std::string l2;
        if (a.dayEv.sun.hasRise) l2 += "Rise " + clockStr(a, a.dayEv.sun.rise);
        if (a.dayEv.sun.hasSet) l2 += (l2.empty() ? "" : "  ·  ") + std::string("Set ") + clockStr(a, a.dayEv.sun.set);
        if (!a.dayLenText.empty()) { size_t p = a.dayLenText.find(" of daylight"); l2 += "  ·  " + (p == std::string::npos ? a.dayLenText : a.dayLenText.substr(0, p)); }
        row(y, L"SUN", P.accent, l1, l2);
    }
    // moon
    {
        const astro::MoonInfo& m = a.moon;
        std::string l1 = fmt("%s · %.0f%% lit", astro::phaseName(m.elongation), m.illum * 100.0);
        std::string l2;
        const astro::RiseSet& rs = a.moonDetail.rs;
        if (m.appAlt > -0.7) {
            l2 = fmt("%.0f° %s", m.appAlt, util::compass16(m.hor.az).c_str());
            if (rs.hasSet && rs.set > a.now) l2 += "  ·  sets " + clockStrDay(a, rs.set);
        } else {
            if (rs.hasRise && rs.rise > a.now) l2 = "Rises " + clockStrDay(a, rs.rise) + " · " + util::compass16(a.moonDetail.riseAz);
            else l2 = "Below the horizon";
            if (rs.hasRise && rs.hasSet && rs.set > rs.rise) l2 += "  ·  sets " + clockStrDay(a, rs.set);
        }
        row(y + 86 * s, L"MOON", P.accent2, l1, l2);
    }
}

} // namespace app
