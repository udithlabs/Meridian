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

void drawGlassEffects(Ctx& c, uint64_t key, const glassfx::Pose& gp, bool active, bool cardStyle) {
    Gfx& g = c.g; const Palette& P = c.P; const float s = c.s;
    const float baseA = clamp01(P.ink.a);
    if (baseA <= 0.02f || gp.w <= 2.f || gp.h <= 2.f) return;

    const bool needClip = gp.sheenA > 0.02f || gp.hasRipple;
    if (needClip) {
        g.pushRoundRectClip(gp.x + 0.5f, gp.y + 0.5f, std::max(1.f, gp.w - 1.f), std::max(1.f, gp.h - 1.f), std::max(0.5f, gp.r - 0.5f));
        if (gp.sheenA > 0.02f) {
            const float mx = c.a.mouseX - c.a.shiftX, my = c.a.mouseY - c.a.shiftY;
            const bool hasPtr = c.a.mouseX >= 0.f;
            const float sx = hasPtr ? std::max(gp.x, std::min(gp.x + gp.w, mx)) : (gp.x + gp.w * 0.5f);
            const float sy = hasPtr ? std::max(gp.y, std::min(gp.y + gp.h, my)) : (gp.y + gp.h * 0.35f);
            const Color sheenCol = active ? tint(P, lerp(P.plate, P.accent, 0.32f)) : tint(P, lerp(P.ink, P.accent, 0.24f));
            const bool smallCtl = gp.w <= 54.f * s && gp.h <= 54.f * s;
            const float rx = smallCtl ? std::max(gp.w * 0.62f, 16.f * s) : std::max(gp.w * (cardStyle ? 0.42f : 0.56f), 42.f * s);
            const float ry = smallCtl ? std::max(gp.h * 0.62f, 14.f * s) : std::max(gp.h * (cardStyle ? 0.65f : 0.90f), 26.f * s);
            const float inten = (cardStyle ? (P.dark ? 0.18f : 0.22f) : (smallCtl ? (P.dark ? 0.26f : 0.22f) : (active ? 0.18f : (P.dark ? 0.17f : 0.16f)))) * gp.sheenA * baseA;
            g.glowEllipse(sx, sy, rx, ry, sheenCol, inten);
            // Subtle curved-glass top-edge Fresnel catch
            const float topI = (cardStyle ? 0.16f : (smallCtl ? 0.20f : 0.11f)) * clamp01(gp.sheenA * 0.68f + gp.bounceEnergy * 0.55f) * baseA;
            if (topI > 0.006f) {
                g.glowEllipse(gp.x + gp.w * 0.5f, gp.y + 1.f * s, gp.w * 0.46f, std::max(5.f * s, gp.h * 0.36f), sheenCol, topI);
            }
        }
        if (gp.hasRipple) {
            static thread_local std::vector<glassfx::RippleDraw> rips;
            c.a.glass.collectRipples(key, gp.x, gp.y, gp.w, gp.h, s, rips);
            const Color waveCol = active ? tint(P, lerp(P.plate, P.accent, 0.52f)) : tint(P, lerp(P.ink, P.accent, 0.38f));
            for (const auto& rd : rips) {
                const float ra = rd.alpha * baseA;
                g.glow(rd.cx, rd.cy, rd.radius * 1.18f, waveCol, (cardStyle ? 0.28f : (active ? 0.30f : 0.25f)) * ra);
                g.strokeCircle(rd.cx, rd.cy, rd.radius, withA(waveCol, (cardStyle ? 0.62f : (active ? 0.56f : 0.52f)) * ra), rd.ringWidth);
                if (rd.secondaryAlpha > 0.01f) {
                    g.strokeCircle(rd.cx, rd.cy, rd.secondaryRadius, withA(tint(P, P.accent), 0.42f * rd.secondaryAlpha * baseA), std::max(1.1f, rd.ringWidth * 0.78f));
                }
            }
        }
        g.popRoundRectClip();
    }
    if (gp.rimBoost > 0.02f) {
        const Color rimCol = active
            ? withA(tint(P, lerp(P.ink, P.accent, 0.45f)), 0.46f * gp.rimBoost * baseA)
            : withA(tint(P, lerp(P.ink, P.accent, 0.35f)), (P.dark ? 0.46f : 0.34f) * gp.rimBoost * baseA);
        g.strokeRoundRect(gp.x + 0.5f, gp.y + 0.5f, std::max(1.f, gp.w - 1.f), std::max(1.f, gp.h - 1.f), std::max(0.5f, gp.r - 0.5f), rimCol, std::max(1.f, (1.f + 0.30f * gp.rimBoost) * s));
    }
}

void drawButton(Ctx& c, float x, float y, float w, float h, const std::wstring& label, bool active, int hitId, FontId font, float size, uint64_t glassKey, bool allowHover) {
    Gfx& g = c.g; const Palette& P = c.P; const float s = c.s;
    const float mx = c.a.mouseX - c.a.shiftX, my = c.a.mouseY - c.a.shiftY;
    const bool hover = allowHover && !c.a.showSettings && !c.a.showHelp && !c.a.isSaver && c.a.mouseX >= 0.f && mx >= x && mx <= x + w && my >= y && my <= y + h;
    const uint64_t key = glassKey ? glassKey : (hitId ? c.a.glassKeyForHit(hitId) : 0);
    const glassfx::Pose gp = c.a.glass.pose(key, x, y, w, h, h * 0.5f, s, hover, false);
    if (active) {
        g.fillRoundRect(gp.x, gp.y, gp.w, gp.h, gp.r, mulA(P.ink, 0.92f));
    } else {
        const Color bg = lerp(P.panel, mulA(P.ink, 0.11f), gp.hoverT);
        g.fillRoundRect(gp.x, gp.y, gp.w, gp.h, gp.r, bg);
        g.strokeRoundRect(gp.x + 0.5f, gp.y + 0.5f, std::max(1.f, gp.w - 1.f), std::max(1.f, gp.h - 1.f), std::max(0.5f, gp.r - 0.5f), P.line, std::max(1.f, s));
    }
    drawGlassEffects(c, key, gp, active, false);
    const Color tc = active ? P.plate : lerp(P.ink2, P.ink, gp.hoverT);
    const float fs = size * s;
    g.text(font, fs, gp.x + gp.w * 0.5f, gp.y + gp.h * 0.5f + g.capHeight(font, fs) * 0.5f, label, tc, Align::Center, 0.08f);
    if (hitId) addHit(c.a, x, y, w, h, hitId);
}

void drawSlidingButtons(Ctx& c, int slideSlot, float y, float h, const std::vector<SegItem>& items, FontId font, float size, bool allowHover) {
    if (items.empty()) return;
    Gfx& g = c.g; const Palette& P = c.P; const float s = c.s;
    const float mx = c.a.mouseX - c.a.shiftX, my = c.a.mouseY - c.a.shiftY;

    bool hasActive = false;
    int actIdx = -1;
    float actX = 0.f, actW = 0.f;
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].active) { hasActive = true; actIdx = (int)i; actX = items[i].x; actW = items[i].w; break; }
    }
    const glassfx::SlidePose sp = c.a.glass.stepSlide(slideSlot, hasActive, actX, actW, c.W, c.H);

    std::vector<glassfx::Pose> poses(items.size());
    std::vector<uint64_t> keys(items.size(), 0);
    for (size_t i = 0; i < items.size(); ++i) {
        const SegItem& it = items[i];
        const bool hover = allowHover && !c.a.showSettings && !c.a.showHelp && !c.a.isSaver && c.a.mouseX >= 0.f && mx >= it.x && mx <= it.x + it.w && my >= y && my <= y + h;
        keys[i] = it.glassKey ? it.glassKey : (it.hitId ? c.a.glassKeyForHit(it.hitId) : 0);
        poses[i] = c.a.glass.pose(keys[i], it.x, y, it.w, h, h * 0.5f, s, hover, false);
    }

    // Pass 1: individual button glass frames, hover sheens & ripples (avoiding double-draw once active pill arrives)
    const bool slideArrived = actIdx >= 0 && sp.visible && sp.alpha > 0.85f && std::fabs(sp.x - items[(size_t)actIdx].x) < 4.f * s;
    for (size_t i = 0; i < items.size(); ++i) {
        const glassfx::Pose& gp = poses[i];
        const bool isAct = (int)i == actIdx && slideArrived;
        if (!isAct) {
            const Color bg = lerp(P.panel, mulA(P.ink, 0.11f), gp.hoverT);
            g.fillRoundRect(gp.x, gp.y, gp.w, gp.h, gp.r, bg);
            g.strokeRoundRect(gp.x + 0.5f, gp.y + 0.5f, std::max(1.f, gp.w - 1.f), std::max(1.f, gp.h - 1.f), std::max(0.5f, gp.r - 0.5f), P.line, std::max(1.f, s));
            drawGlassEffects(c, keys[i], gp, false, false);
        }
    }

    // Pass 2: crisp base labels (always high contrast outside the sliding pill)
    const float fs = size * s;
    const float capH = g.capHeight(font, fs);
    for (size_t i = 0; i < items.size(); ++i) {
        const SegItem& it = items[i];
        const glassfx::Pose& gp = poses[i];
        if ((int)i == actIdx && slideArrived) continue;
        const Color baseCol = lerp(P.ink2, P.ink, gp.hoverT);
        g.text(font, fs, gp.x + gp.w * 0.5f, gp.y + gp.h * 0.5f + capH * 0.5f, it.label, baseCol, Align::Center, 0.08f);
    }

    // Pass 3: smoothly gliding luminous liquid-glass active pill + pixel-exact clipped inverted labels inside it!
    if (sp.visible) {
        glassfx::Pose ap{};
        ap.x = sp.x; ap.y = y; ap.w = sp.w; ap.h = h; ap.r = h * 0.5f;
        uint64_t aKey = 0;
        if (actIdx >= 0) {
            const SegItem& ai = items[(size_t)actIdx];
            const glassfx::Pose& agp = poses[(size_t)actIdx];
            const float dx = agp.x - ai.x, dw = agp.w - ai.w;
            ap.x += dx; ap.y = agp.y; ap.w = std::max(8.f, ap.w + dw); ap.h = agp.h; ap.r = ap.h * 0.5f;
            if (slideArrived) {
                ap.hoverT = agp.hoverT; ap.holdT = agp.holdT;
                ap.sheenA = agp.sheenA; ap.rimBoost = agp.rimBoost;
                ap.bounceEnergy = agp.bounceEnergy; ap.hasRipple = agp.hasRipple;
                aKey = keys[(size_t)actIdx];
            } else {
                ap.sheenA = 0.55f * sp.alpha;
                ap.rimBoost = 0.55f * sp.alpha;
            }
        }
        g.fillRoundRect(ap.x, ap.y, ap.w, ap.h, ap.r, mulA(P.ink, 0.92f * sp.alpha));
        if (aKey) drawGlassEffects(c, aKey, ap, true, false);
        else if (ap.rimBoost > 0.02f) {
            g.strokeRoundRect(ap.x + 0.5f, ap.y + 0.5f, std::max(1.f, ap.w - 1.f), std::max(1.f, ap.h - 1.f), std::max(0.5f, ap.r - 0.5f),
                              withA(tint(P, lerp(P.ink, P.accent, 0.45f)), 0.42f * ap.rimBoost * clamp01(P.ink.a)), std::max(1.f, s));
        }

        // Clip inverted dark-on-light (or light-on-dark) labels strictly inside the gliding capsule boundary
        const bool needTextClip = !slideArrived;
        if (needTextClip) g.pushRoundRectClip(ap.x, ap.y, ap.w, ap.h, ap.r);
        for (size_t i = 0; i < items.size(); ++i) {
            const SegItem& it = items[i];
            const glassfx::Pose& gp = poses[i];
            if (gp.x + gp.w < ap.x || gp.x > ap.x + ap.w) continue;
            g.text(font, fs, gp.x + gp.w * 0.5f, gp.y + gp.h * 0.5f + capH * 0.5f, it.label, mulA(P.plate, sp.alpha), Align::Center, 0.08f);
        }
        if (needTextClip) g.popRoundRectClip();
    }

    // Pass 4: register stable hitboxes
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].hitId) addHit(c.a, items[i].x, y, items[i].w, h, items[i].hitId);
    }
}

static float hashf(int i, int salt) {
    unsigned x = (unsigned)i * 374761393u + (unsigned)salt * 668265263u;
    x = (x ^ (x >> 13)) * 1274126177u; x ^= x >> 16;
    return (x & 0xFFFF) / 65535.f;
}

static float fisheyeNorm(double alt) {
    // True 3D hemispherical fisheye lens mapping (strong orthographic-equisolid blend):
    // compresses naturally toward the 360-degree horizon limb so the entire sky reads as a tactile 3D convex orb.
    const double a = std::max(0.0, std::min(90.0, alt));
    const double ortho = std::cos(astro::rad(a));
    const double eq    = (90.0 - a) / 90.0;
    return (float)(0.84 * ortho + 0.16 * eq);
}

gfx::Pt domeProject(float cx, float cy, float R, double alt, double az) {
    const float r = R * fisheyeNorm(alt);
    const double th = astro::rad(az - gSkyHeading);
    return {cx + r * (float)std::sin(th), cy - r * (float)std::cos(th)};
}

static Pt domePt(float cx, float cy, float R, double alt, double az) {
    return domeProject(cx, cy, R, alt, az);
}

// Cached 3D convex optical crystal lens overlay for drawDome (built once per size/theme; 0 steady-state sprite builds)
static Bitmap gDomeGlass;
static uint64_t gDomeGlassKey = 0;

// Refined astronomical spectral color from real B-V color index (-0.25 sapphire blue -> 0.0 diamond white -> +1.85 warm amber-ruby)
static Color starSpectral(float bv) {
    if (bv < 0.0f) {
        const float t = clamp01((bv + 0.25f) / 0.25f);
        return lerp(rgb(0xA8CEFF), rgb(0xEFF5FF), t);
    }
    if (bv < 0.65f) {
        const float t = clamp01(bv / 0.65f);
        return lerp(rgb(0xEFF5FF), rgb(0xFFF4DC), t);
    }
    if (bv < 1.25f) {
        const float t = clamp01((bv - 0.65f) / 0.60f);
        return lerp(rgb(0xFFF4DC), rgb(0xFFD699), t);
    }
    const float t = clamp01((bv - 1.25f) / 0.60f);
    return lerp(rgb(0xFFD699), rgb(0xFFA86B), t);
}

// ---------------------------------------------------------------- the sky dome
void drawDome(Ctx& c, float cx, float cy, float R, bool labels) {
    Gfx& g = c.g; App& a = c.a; const Palette& P = c.P; const float s = c.s;
    const double now = a.now; const astro::Geo& geo = a.st.geo;
    const double sunAlt = a.sun.hor.alt;
    const float night = smoothstep(-4.f, -12.f, (float)sunAlt);
    const float cloud = a.wx.valid ? std::min(1.f, (float)a.wx.cur.cloud / 100.f) : 0.f;
    // The theme paints the sky dome (Palette::skyOwn: 1 in every fixed theme, 0 in Auto, which follows the real Sun).
    // Star patterns follow REAL night (sunAlt < -4 deg) even on dark themes like Midnight during the day —
    // unless the user explicitly enables "Star patterns during the day" (Settings > Sky Map).
    const float own = clamp01(P.skyOwn);
    const bool lightDome = !P.dark && night < 0.5f;
    const float dayStarBase = a.st.dayStars ? (lightDome ? 0.55f : 0.82f) : 0.f;
    const float starVisRaw = std::max(night, dayStarBase);
    // Soften gently under heavy overcast/fog without double-attenuating constellations away on partly cloudy nights
    const float wxStarFade = (1.f - 0.36f * cloud) * (1.f - 0.30f * a.gloom) * (1.f - 0.62f * a.haze);
    const float starA = starVisRaw * (1.f - 0.32f * P.ember) * wxStarFade;
    const float o = R + 36 * s;
    auto T = [&](Color col) { return tint(P, col); };
    auto dim = [&](Color col) { float k = 1.f - 0.5f * P.ember; col.r *= k; col.g *= k; col.b *= k; return col; };

    // Subtle floating 3D drop shadow grounding the convex sky crystal hemisphere in space
    g.glowEllipse(cx, cy + R * 0.90f, R * 0.86f, R * 0.24f, Color{0, 0, 0, 1.f}, P.dark ? 0.42f : 0.14f);

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
    // 3D spherical atmospheric scattering: deep cosmic zenith -> lush Rayleigh horizon airglow -> subtle curved outer limb depth
    const Color limbDeep = lerp(horBase, Color{0.01f, 0.02f, 0.05f, 1.f}, P.dark ? 0.36f : 0.12f);
    g.radialFill(cx, cy, R, {
        {0.00f, zen},
        {0.46f, lerp(zen, horBase, 0.14f)},
        {0.76f, lerp(zen, horBase, 0.46f)},
        {0.93f, lerp(horBase, T(rgb(0x98C6FF)), P.dark ? 0.14f : 0.08f)},
        {1.00f, limbDeep},
    });

    // twilight glow toward the sun (projected through the 3D fisheye lens)
    if (tw > 0.01f) {
        const float rr = std::max(0.68f * R, std::min(1.02f * R, R * fisheyeNorm(std::max(0.0, a.sun.hor.alt))));
        const float az = (float)astro::rad(a.sun.hor.az - gSkyHeading);
        const float gx = cx + rr * std::sin(az), gy = cy - rr * std::cos(az);
        const Color warm = lerp(rgb(0xFF9A4D), rgb(0xB4508E), smoothstep(-2.f, -9.f, (float)sunAlt));
        g.glow(gx, gy, 1.45f * R, dim(T(hor)), tw * 0.52f * (1.f - 0.5f * cloud));
        g.glow(gx, gy, 0.96f * R, dim(T(warm)), tw * 0.88f * (1.f - 0.5f * cloud));
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

    // star positions (accurate J2000 -> precessed -> observer's real lat/lon & sidereal time; computed only when stars are visible)
    const auto& S = astro::stars();
    std::vector<Horizontal> sh;
    std::vector<Pt> sp;
    std::vector<float> sr;
    if (starA > 0.02f) {
        sh.resize(S.size());
        sp.resize(S.size());
        sr.resize(S.size());
        for (size_t i = 0; i < S.size(); ++i) {
            Equatorial e{S[i].raH * 15.0, S[i].decDeg};
            sh[i] = astro::toHorizontal(astro::precessJ2000(e, now), now, geo);
            sp[i] = domePt(cx, cy, R, sh[i].alt, sh[i].az);
            sr[i] = std::max(1.05f, 2.75f - 0.36f * (float)S[i].mag) * s;
        }
    }

    if (starA > 0.02f && a.st.elMilky) {
        // Milky Way band
        float mwA = starA * (1.f - 0.55f * cloud) * (lightDome ? 0.35f : 1.0f);
        std::vector<Pt> run;
        auto flush = [&]() {
            if (run.size() >= 2) {
                Color mw = T(Color{0.70f, 0.78f, 1.0f, 1.f});
                for (int k = 0; k < 8; ++k) {
                    float t = k / 7.f;
                    g.polyline(run, false, withA(mw, 0.017f * mwA), (0.27f - 0.22f * t) * R, true);
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
        if (gc.alt > 0) { Pt p = domePt(cx, cy, R, gc.alt, gc.az); g.glow(p.x, p.y, 0.2f * R, T(Color{0.85f, 0.85f, 1.f, 1.f}), 0.08f * mwA); }
    }

    // ---- Modern Posh 3D Fisheye Hemispherical Lens Grid (Orthogonal Curved Great-Circle Cage + Foreshortened Altitude Parallels)
    {
        const Color gridBase = lightDome ? P.ink : T(lerp(rgb(0xB4D2FF), P.accent2, 0.25f));
        const float gMul = lightDome ? 1.25f : 1.0f;

        // 1. Orthogonal 3D hemispherical great-circle fisheye arches (bowing gracefully N<->S and E<->W across the curved vault)
        static const double kTilts[7] = {-65.0, -45.0, -22.5, 0.0, 22.5, 45.0, 65.0};
        constexpr int kSteps = 44;
        std::vector<Pt> arc;
        arc.reserve(kSteps + 1);
        for (int family = 0; family < 2; ++family) {
            for (double tiltDeg : kTilts) {
                const bool isPrime = (std::fabs(tiltDeg) < 0.1);
                const double tRad = astro::rad(tiltDeg);
                const double st = std::sin(tRad), ct = std::cos(tRad);
                const double u0 = isPrime ? 0.0 : 0.045 * 3.14159265358979;
                const double u1 = 3.14159265358979 - u0;
                arc.clear();
                for (int i = 0; i <= kSteps; ++i) {
                    const double u = u0 + (u1 - u0) * ((double)i / (double)kSteps);
                    const double cu = std::cos(u), su = std::sin(u);
                    const double vx = (family == 0) ? cu : (su * st);
                    const double vy = (family == 0) ? (su * st) : cu;
                    const double vz = std::max(0.0, std::min(1.0, su * ct));
                    const double alt = astro::deg(std::asin(vz));
                    const double az  = astro::deg(std::atan2(vx, vy));
                    arc.push_back(domePt(cx, cy, R, alt, az));
                }
                const float alpha = isPrime ? 0.115f : (std::fabs(tiltDeg) < 35.0 ? 0.092f : (std::fabs(tiltDeg) < 55.0 ? 0.082f : 0.068f));
                const float width = std::max(0.8f, (isPrime ? 1.0f : 0.85f) * s);
                g.polyline(arc, false, withA(gridBase, alpha * gMul), width, true);
            }
        }

        // 2. Foreshortened 3D fisheye altitude parallels (15, 30, 45, 60, 75 deg — visibly compressing toward the 360-deg horizon limb!)
        static const double kAlts[5] = {15.0, 30.0, 45.0, 60.0, 75.0};
        for (int i = 0; i < 5; ++i) {
            const bool major = (i == 1 || i == 3); // 30 deg & 60 deg
            const bool limb  = (i == 0);           // 15 deg tight outer shoulder compression ring
            const float rr = R * fisheyeNorm(kAlts[i]);
            const float aRing = major ? 0.145f : (limb ? 0.092f : 0.065f);
            g.strokeCircle(cx, cy, rr, withA(gridBase, aRing * gMul), std::max(0.85f, (major ? 1.05f : 0.85f) * s));
        }

        // Subtle outer diagonal horizon tick lines (NE, SE, SW, NW) anchoring the 360-deg lens perimeter
        for (int k = 0; k < 4; ++k) {
            const double ang = astro::rad(45.0 + k * 90.0 - gSkyHeading);
            const float r0 = R * fisheyeNorm(30.0), r1 = R * 0.996f;
            g.line(cx + r0 * (float)std::sin(ang), cy - r0 * (float)std::cos(ang),
                   cx + r1 * (float)std::sin(ang), cy - r1 * (float)std::cos(ang),
                   withA(gridBase, 0.055f * gMul), std::max(0.8f, 0.85f * s), false);
        }

        if (labels) {
            const float afs = std::max(8.f, 9.f * s);
            const double nAng = astro::rad(-gSkyHeading);
            const float sn = (float)std::sin(nAng), cn = (float)std::cos(nAng);
            for (double aDeg : {30.0, 60.0}) {
                const float rr = R * fisheyeNorm(aDeg);
                const float lx = cx + rr * sn + 6.f * s * cn;
                const float ly = cy - rr * cn + 6.f * s * sn;
                g.text(F_MONO, afs, lx, ly + afs * 0.35f, aDeg < 45.0 ? L"30\xB0" : L"60\xB0", withA(gridBase, 0.32f * gMul), Align::Left, 0.04f);
            }
        }
    }

    if (a.st.elEcliptic) {
        float eclA = 0.16f + 0.24f * night;
        double eps = 23.4393 - 0.013 * (astro::julianDay(now) - 2451545.0) / 36525.0;
        for (int l = 0; l < 360; l += 3) {
            Horizontal h = astro::toHorizontal(astro::eclipticToEquatorial(l, 0.0, eps), now, geo);
            if (h.alt < 0.8) continue;
            Pt p = domePt(cx, cy, R, h.alt, h.az);
            g.fillCircle(p.x, p.y, 1.0f * s, T(withA(P.accent, eclA)));
        }
    }

    // Artistic constellation filigree, spectral stars, diamond glints, and de-cluttered constellation watermarks
    if (starA > 0.02f) {
        const Color lineTint = lightDome
            ? lerp(P.ink, rgb(0xFFFFFF), 0.35f)
            : T(lerp(rgb(0x95B4FF), P.accent2, 0.38f));

        if (a.st.elConst) {
            const auto& cons = astro::constellations();
            // Pass 1: dual-layer artistic filigree lines (trimmed cleanly shy of star jewels!)
            for (const auto& con : cons) {
                for (const auto& ln : con.lines) {
                    if (ln.a < 0 || ln.b < 0 || (size_t)ln.a >= S.size() || (size_t)ln.b >= S.size()) continue;
                    const Horizontal &h1 = sh[ln.a], &h2 = sh[ln.b];
                    if (h1.alt < 1.5 || h2.alt < 1.5) continue;
                    const Pt &p1 = sp[ln.a], &p2 = sp[ln.b];
                    const float dx = p2.x - p1.x, dy = p2.y - p1.y;
                    const float len = std::sqrt(dx * dx + dy * dy);
                    const float g1 = sr[ln.a] + 2.4f * s, g2 = sr[ln.b] + 2.4f * s;
                    if (len <= g1 + g2 + 2.f * s) continue;
                    const float ux = dx / len, uy = dy / len;
                    const float x1 = p1.x + ux * g1, y1 = p1.y + uy * g1;
                    const float x2 = p2.x - ux * g2, y2 = p2.y - uy * g2;
                    const float altF = smoothstep(1.5f, 14.f, (float)std::min(h1.alt, h2.alt));
                    const float la = starA * altF;
                    if (la < 0.02f) continue;
                    // Soft celestial aura under crisp hairline silk thread
                    if (!lightDome) {
                        g.line(x1, y1, x2, y2, withA(lineTint, 0.11f * la), std::max(2.2f, 2.8f * s), true);
                    }
                    g.line(x1, y1, x2, y2, withA(lineTint, (lightDome ? 0.36f : 0.42f) * la), std::max(0.95f, 1.05f * s), true);
                }
            }

            // Pass 2: subtle wide-tracked uppercase constellation watermarks (de-cluttered so labels never collide)
            if (labels && a.st.elLabels && starA > 0.25f) {
                std::vector<Pt> placed;
                const Pt moonChk = (a.moon.appAlt > -0.7) ? domePt(cx, cy, R, std::max(0.0, a.moon.appAlt), a.moon.hor.az) : Pt{-9999.f, -9999.f};
                const Pt sunChk  = (a.sun.appAlt > -0.6)  ? domePt(cx, cy, R, std::max(0.0, a.sun.appAlt),  a.sun.hor.az)  : Pt{-9999.f, -9999.f};
                const float nfs = std::max(8.5f, 9.2f * s);
                for (const auto& con : cons) {
                    if (placed.size() >= 9) break;
                    std::vector<int> uniq;
                    for (const auto& ln : con.lines) {
                        if (std::find(uniq.begin(), uniq.end(), ln.a) == uniq.end()) uniq.push_back(ln.a);
                        if (std::find(uniq.begin(), uniq.end(), ln.b) == uniq.end()) uniq.push_back(ln.b);
                    }
                    if (uniq.empty()) continue;
                    int visN = 0; float sx = 0.f, sy = 0.f; double avgAlt = 0.0;
                    for (int idx : uniq) {
                        if (idx < 0 || (size_t)idx >= S.size()) continue;
                        if (sh[idx].alt >= 10.0) { sx += sp[idx].x; sy += sp[idx].y; avgAlt += sh[idx].alt; ++visN; }
                    }
                    if (visN < 2 || visN * 2 < (int)uniq.size()) continue;
                    const float lx = sx / (float)visN, ly = sy / (float)visN + 11.f * s;
                    const float distC = std::hypot(lx - cx, ly - cy);
                    if (distC > R * 0.84f || distC < 24.f * s) continue;
                    if (std::hypot(lx - moonChk.x, ly - moonChk.y) < 38.f * s) continue;
                    if (std::hypot(lx - sunChk.x,  ly - sunChk.y)  < 34.f * s) continue;
                    bool clash = false;
                    for (const Pt& q : placed) {
                        if (std::fabs(lx - q.x) < 76.f * s && std::fabs(ly - q.y) < 22.f * s) { clash = true; break; }
                    }
                    if (clash) continue;
                    placed.push_back({lx, ly});
                    const float ca = smoothstep(10.f, 24.f, (float)(avgAlt / visN)) * starA;
                    const Color cCol = lightDome ? mulA(P.ink, 0.34f * ca) : withA(lineTint, 0.34f * ca);
                    g.text(F_MONO, nfs, lx, ly, Ws(util::upper(con.name)), cCol, Align::Center, 0.20f);
                }
            }
        }

        // Pass 3: Spectral star jewels + 1st-magnitude 4-point diffraction diamond glints
        for (size_t i = 0; i < S.size(); ++i) {
            const Horizontal& h = sh[i];
            if (h.alt < 1.0) continue;
            const float vis = smoothstep(1.f, 10.f, (float)h.alt) * starA;
            const float mag = (float)S[i].mag;
            const float r = sr[i];
            const float al = clamp01(1.22f - 0.17f * mag) * vis;
            const Pt& p = sp[i];
            const Color baseStar = lightDome ? Color{1.f, 1.f, 1.f, 1.f} : T(starSpectral(S[i].bv));
            if (mag < 1.6f && !lightDome) {
                const float gInt = (mag < 0.6f ? 0.28f : 0.16f) * vis;
                g.glow(p.x, p.y, r * (mag < 0.6f ? 5.6f : 4.0f), baseStar, gInt);
            }
            if (mag < 0.85f && vis > 0.35f) {
                const float spk = r * (lightDome ? 2.4f : 3.2f);
                const Color sc2 = withA(baseStar, (lightDome ? 0.42f : 0.36f) * vis);
                const float sw = std::max(0.75f, 0.85f * s);
                g.line(p.x - spk, p.y, p.x + spk, p.y, sc2, sw, true);
                g.line(p.x, p.y - spk, p.x, p.y + spk, sc2, sw, true);
            }
            g.fillCircle(p.x, p.y, r, withA(baseStar, al));
            if (mag < 1.8f && !lightDome) {
                g.fillCircle(p.x, p.y, r * 0.52f, Color{1.f, 1.f, 1.f, std::min(1.f, al * 0.92f)});
            }
            if (labels && a.st.elLabels && mag < 0.55f && h.alt > 10 && vis > 0.4f) {
                const bool left = p.x > cx + 0.45f * R;
                g.text(F_MONO, 11.f * s, left ? p.x - r - 5 * s : p.x + r + 5 * s, p.y + 3.5f * s,
                       Ws(S[i].name), withA(baseStar, (lightDome ? 0.50f : 0.44f) * vis), left ? Align::Right : Align::Left, 0.06f);
            }
        }
    }

    // ---- Smooth 3D Horizon-to-Horizon Sun & Moon Orbital Arcs across the Fisheye Dome
    // Always selects the SINGLE active (or nearest upcoming) complete horizon-to-horizon passage so
    // both the Sun and Moon cleanly enter from one edge of the 360-deg horizon and depart on the opposite rim.
    if (a.st.elTracks) {
        auto drawSkyArc = [&](const std::vector<TrackPt>& trk, const Color& col, float pastA, float futA, float width, bool sunStyle) {
            struct Seg {
                size_t i0, i1; // inclusive indices in trk where alt >= 0
                bool hasRise = false, hasSet = false;
                Pt risePt{}, setPt{};
                double t0 = 0.0, t1 = 0.0;
            };
            std::vector<Seg> segs;
            bool inSeg = false;
            Seg cur{};
            for (size_t i = 0; i < trk.size(); ++i) {
                if (trk[i].alt >= 0.0f) {
                    if (!inSeg) {
                        cur = Seg{};
                        cur.i0 = i;
                        cur.t0 = trk[i].t;
                        if (i > 0 && trk[i - 1].alt < 0.0f) {
                            const float f = -trk[i - 1].alt / std::max(0.01f, trk[i].alt - trk[i - 1].alt);
                            double dAz = trk[i].az - trk[i - 1].az;
                            if (dAz > 180.0) dAz -= 360.0; else if (dAz < -180.0) dAz += 360.0;
                            cur.risePt = domePt(cx, cy, R, 0.0, trk[i - 1].az + dAz * f);
                            cur.hasRise = true;
                            cur.t0 = trk[i - 1].t + (trk[i].t - trk[i - 1].t) * f;
                        }
                        inSeg = true;
                    }
                    cur.i1 = i;
                    cur.t1 = trk[i].t;
                } else if (inSeg) {
                    const float f = trk[i - 1].alt / std::max(0.01f, trk[i - 1].alt - trk[i].alt);
                    double dAz = trk[i].az - trk[i - 1].az;
                    if (dAz > 180.0) dAz -= 360.0; else if (dAz < -180.0) dAz += 360.0;
                    cur.setPt = domePt(cx, cy, R, 0.0, trk[i - 1].az + dAz * f);
                    cur.hasSet = true;
                    cur.t1 = trk[i - 1].t + (trk[i].t - trk[i - 1].t) * f;
                    segs.push_back(cur);
                    inSeg = false;
                }
            }
            if (inSeg) segs.push_back(cur);
            if (segs.empty()) return;

            // Choose the single active passage containing `now`, else the nearest complete upcoming passage (else fallback)
            int pick = -1;
            for (int k = 0; k < (int)segs.size(); ++k) {
                if (segs[(size_t)k].t0 <= now && now <= segs[(size_t)k].t1) { pick = k; break; }
            }
            if (pick < 0) {
                double bestDt = 1e18;
                for (int k = 0; k < (int)segs.size(); ++k) {
                    const Seg& s2 = segs[(size_t)k];
                    if (s2.t0 >= now && s2.hasRise && s2.hasSet && (s2.t0 - now) < bestDt) {
                        bestDt = s2.t0 - now;
                        pick = k;
                    }
                }
            }
            if (pick < 0) {
                double bestDt = 1e18;
                for (int k = 0; k < (int)segs.size(); ++k) {
                    if (segs[(size_t)k].t0 >= now && (segs[(size_t)k].t0 - now) < bestDt) {
                        bestDt = segs[(size_t)k].t0 - now;
                        pick = k;
                    }
                }
            }
            if (pick < 0) pick = (int)segs.size() - 1;
            const Seg& sg = segs[(size_t)pick];

            std::vector<Pt> pastRun, futRun;
            if (sg.hasRise) {
                if (sg.t0 < now) pastRun.push_back(sg.risePt);
                else futRun.push_back(sg.risePt);
            }
            for (size_t i = sg.i0; i <= sg.i1; ++i) {
                const Pt p = domePt(cx, cy, R, trk[i].alt, trk[i].az);
                if (trk[i].t < now) pastRun.push_back(p);
                else {
                    if (!pastRun.empty() && futRun.empty()) pastRun.push_back(p); // stitch past->future seamlessly
                    futRun.push_back(p);
                }
            }
            if (sg.hasSet) {
                if (sg.t1 < now) pastRun.push_back(sg.setPt);
                else futRun.push_back(sg.setPt);
            }

            if (pastRun.size() >= 2) {
                g.polyline(pastRun, false, withA(col, pastA * 0.25f), width * 2.4f, true);
                g.polyline(pastRun, false, withA(col, pastA), width, true);
            }
            if (futRun.size() >= 2) {
                g.polyline(futRun, false, withA(col, futA * 0.30f), width * 3.1f, true);
                g.polyline(futRun, false, withA(col, futA), width, true);
            }

            // Subtle luminous hour beads along the active passage + horizon rise/set portal beacons on the 360-deg rim
            for (size_t i = sg.i0; i <= sg.i1; i += 6) {
                if (trk[i].alt < 2.5f) continue;
                const Pt p = domePt(cx, cy, R, trk[i].alt, trk[i].az);
                const bool past = trk[i].t < now;
                g.fillCircle(p.x, p.y, (sunStyle ? 1.6f : 1.4f) * s, withA(col, past ? pastA * 0.95f : std::min(1.f, futA * 1.18f)));
            }
            if (sg.hasRise) {
                g.glow(sg.risePt.x, sg.risePt.y, 9.5f * s, col, 0.50f);
                g.fillCircle(sg.risePt.x, sg.risePt.y, 2.5f * s, withA(col, 0.90f));
            }
            if (sg.hasSet) {
                g.glow(sg.setPt.x, sg.setPt.y, 9.5f * s, col, 0.42f);
                g.fillCircle(sg.setPt.x, sg.setPt.y, 2.5f * s, withA(col, 0.78f));
            }
        };
        drawSkyArc(a.sunTrack,  T(rgb(0xFFCB82)), 0.28f, 0.68f, std::max(1.1f, 1.50f * s), true);
        drawSkyArc(a.moonTrack, T(rgb(0xB8D6FF)), 0.22f, 0.60f, std::max(1.0f, 1.30f * s), false);
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

    // the Sun (3D atmospheric horizon refraction + layered radiant coronal sphere)
    Pt sunP{0, 0}; bool sunUp = a.sun.appAlt > -0.6;
    if (sunUp) {
        sunP = domePt(cx, cy, R, std::max(0.0, a.sun.appAlt), a.sun.hor.az);
        const float k = (float)std::max(0.40, std::min(1.0, 0.50 + a.sun.appAlt / 18.0));
        const float behind = (1.f - 0.55f * a.gloom) * (1.f - 0.65f * a.haze);
        const float horWarm = 1.f - smoothstep(0.f, 18.f, (float)a.sun.appAlt);
        const Color sHalo = lerp(rgb(0xFFBC64), rgb(0xFF7D3E), horWarm * 0.68f);
        const Color sPhoto = lerp(rgb(0xFFE9A8), rgb(0xFFC278), horWarm * 0.55f);
        const Color sCore = rgb(0xFFFDF2);
        // Atmospheric limb bloom when emerging from / dipping into the 360-deg horizon
        if (horWarm > 0.05f) {
            const Pt rimP = domePt(cx, cy, R, 0.0, a.sun.hor.az);
            g.glow(rimP.x, rimP.y, 0.34f * R, T(rgb(0xFF9448)), 0.42f * horWarm * (1.f - 0.45f * cloud));
        }
        g.glow(sunP.x, sunP.y, 0.46f * R, T(sHalo), 0.62f * k * (1.f - 0.45f * cloud) * behind);
        g.glow(sunP.x, sunP.y, 0.17f * R, T(rgb(0xFFEDBA)), 0.94f * (0.50f + 0.50f * behind));
        const float srSun = (8.6f + 1.2f * (1.f - horWarm)) * s;
        g.strokeCircle(sunP.x, sunP.y, srSun * 1.32f, withA(T(sHalo), 0.36f * behind), std::max(0.9f, 1.1f * s));
        g.fillCircle(sunP.x, sunP.y, srSun, T(sPhoto));
        g.fillCircle(sunP.x, sunP.y, srSun * 0.62f, T(sCore));
    } else if (sunAlt > -22.0) {
        // Soft directional twilight refraction glow along the curved 360-deg horizon rim before sunrise / after sunset
        const Pt rimP = domePt(cx, cy, R, 0.0, a.sun.hor.az);
        const float al = smoothstep(-22.f, -1.f, (float)sunAlt) * (1.f - 0.4f * cloud);
        g.glow(rimP.x, rimP.y, 0.36f * R, T(rgb(0xFF964C)), 0.38f * al);
    }

    // the Moon (3D perspective depth scaling + horizon emergence moonlight halo)
    Pt moonP{0, 0}; bool moonUp = a.moon.appAlt > -0.7;
    const float moonPersp = 0.88f + 0.16f * smoothstep(0.f, 65.f, (float)std::max(0.0, a.moon.appAlt));
    const float moonSize = std::max(38.f * s, 0.185f * R) * moonPersp;
    if (moonUp) {
        moonP = domePt(cx, cy, R, std::max(0.0, a.moon.appAlt), a.moon.hor.az);
        const float glowK = (float)a.moon.illum * (0.28f + 0.72f * night) * (1.f - 0.55f * cloud) * (1.f - 0.60f * a.gloom) * (1.f - 0.70f * a.haze);
        const float moonHor = 1.f - smoothstep(0.f, 16.f, (float)a.moon.appAlt);
        if (moonHor > 0.05f) {
            const Pt rimP = domePt(cx, cy, R, 0.0, a.moon.hor.az);
            g.glow(rimP.x, rimP.y, 0.28f * R, T(rgb(0xA8CEFF)), 0.36f * moonHor * (0.35f + 0.65f * glowK));
        }
        g.glow(moonP.x, moonP.y, 0.34f * R, T(rgb(0xBFD8FF)), 0.50f * glowK);
        g.drawBitmap(a.moonSmall, moonP.x, moonP.y, moonSize, moonDomeRotation(a.moon, geo), 1.f - 0.45f * cloud);
    } else if (a.moon.hor.alt > -20.0) {
        const Pt rimP = domePt(cx, cy, R, 0.0, a.moon.hor.az);
        const float al = smoothstep(-20.f, -1.f, (float)a.moon.hor.alt) * (0.35f + 0.65f * (float)a.moon.illum);
        g.glow(rimP.x, rimP.y, 0.26f * R, T(rgb(0xA6C8FF)), 0.34f * al);
    }

    drawEventsAbove(c, cx, cy, R, labels, moonSize, sunP.x, sunP.y, sunUp);

    // Subtle geomagnetic aurora crown ribbon shimmering along the northern/southern horizon inside the dome when Aurora is active
    if (a.qfx[12] > 0.04f) {
        const float au = a.qfx[12] * (P.dark ? 0.85f : 0.48f);
        const float poleAz = (float)astro::rad((geo.lat >= 0.0 ? 0.0 : 180.0) - gSkyHeading);
        const float ax = cx + R * 0.72f * std::sin(poleAz), ay = cy - R * 0.72f * std::cos(poleAz);
        g.radialEllipse(ax, ay, R * 0.76f, R * 0.34f, 0.f, 0.f, {
            {0.00f, withA(T(rgb(0x46FFB4)), 0.30f * au)},
            {0.48f, withA(T(rgb(0x28DFCF)), 0.16f * au)},
            {0.80f, withA(T(rgb(0x8858F0)), 0.08f * au)},
            {1.00f, withA(T(rgb(0x8858F0)), 0.00f)},
        }, 1.f);
    }

    // ---- Modern Posh 3D Convex Optical Sapphire Crystal Dome Overlay (cached pixel shader: Fresnel rim, meniscus glaze & limb depth)
    {
        const int sz = std::max(180, std::min(560, ((int)std::lround(R * 2.02f) + 7) & ~7));
        const uint64_t dk = ((uint64_t)sz << 8) ^ (uint64_t)(lightDome ? 2u : (P.ember > 0.3f ? 1u : 0u));
        if (!gDomeGlass.bmp || gDomeGlassKey != dk) {
            std::vector<uint8_t> px((size_t)sz * (size_t)sz * 4u, 0);
            const float half = sz * 0.5f, invR = 1.f / (half * 0.996f);
            const Color specC = T(lightDome ? rgb(0xFFFFFF) : rgb(0xE2F0FF));
            const Color caustC = T(lightDome ? rgb(0xFFFFFF) : rgb(0x8CC2FF));
            const auto toB = [](float v) -> uint8_t { return (uint8_t)std::max(0, std::min(255, (int)(v * 255.f + 0.5f))); };
            for (int y = 0; y < sz; ++y) {
                const float ny = (y + 0.5f - half) * invR;
                for (int x = 0; x < sz; ++x) {
                    const float nx = (x + 0.5f - half) * invR;
                    const float d2 = nx * nx + ny * ny;
                    if (d2 >= 1.0f) continue;
                    const float d = std::sqrt(d2);
                    const float nz = std::sqrt(std::max(0.0f, 1.0f - d2));

                    // True 3D convex hemisphere Fresnel & studio reflection math
                    const float fresnel = std::pow(1.0f - nz, 3.0f);
                    // Upper-left studio directional key light (-0.48, -0.68, 0.55)
                    const float ndl = std::max(0.0f, -0.48f * nx - 0.68f * ny + 0.55f * nz);
                    const float ndh = std::max(0.0f, -0.273f * nx - 0.386f * ny + 0.881f * nz);
                    const float satinSpec = std::pow(ndh, 18.0f) * (lightDome ? 0.13f : 0.055f);

                    // Razor-posh curved upper-left optical lens meniscus highlight hugging the dome shoulder
                    const float arcDir = std::max(0.0f, -0.50f * nx - 0.86f * ny);
                    const float meniscusBand = smoothstep(0.89f, 0.945f, d) * (1.f - smoothstep(0.955f, 0.992f, d)) * std::pow(arcDir, 2.0f) * (lightDome ? 0.28f : 0.22f);
                    const float rimSheen = smoothstep(0.85f, 0.994f, d) * std::pow(arcDir, 1.6f) * (lightDome ? 0.34f : 0.22f);
                    const float fresRim  = fresnel * (0.28f + 0.72f * ndl) * (lightDome ? 0.24f : 0.18f);

                    // Subtle bottom-right inner caustic refraction arc
                    const float botDir = std::max(0.0f, 0.24f * nx + 0.97f * ny);
                    const float botCaust = smoothstep(0.83f, 0.986f, d) * std::pow(botDir, 2.3f) * (lightDome ? 0.15f : 0.115f);

                    // Subtle 3D outer spherical curvature shadow (bottom-right rim curves away from key light)
                    const float shadeDir = std::max(0.0f, 0.48f * nx + 0.68f * ny);
                    const float limbDark = smoothstep(0.76f, 1.00f, d) * (0.35f + 0.65f * shadeDir) * (lightDome ? 0.13f : 0.30f);

                    const float litA = clamp01(satinSpec + meniscusBand + rimSheen + fresRim + botCaust);
                    const float totA = clamp01(litA + limbDark * (1.0f - litA));
                    if (totA < 0.002f) continue;

                    const float wCaust = botCaust / std::max(0.001f, litA);
                    const float rC = (specC.r * (1.f - wCaust) + caustC.r * wCaust) * litA;
                    const float gC = (specC.g * (1.f - wCaust) + caustC.g * wCaust) * litA;
                    const float bC = (specC.b * (1.f - wCaust) + caustC.b * wCaust) * litA;
                    const size_t idx = ((size_t)y * (size_t)sz + (size_t)x) * 4u;
                    px[idx + 0] = toB(std::min(1.f, bC));
                    px[idx + 1] = toB(std::min(1.f, gC));
                    px[idx + 2] = toB(std::min(1.f, rC));
                    px[idx + 3] = toB(totA);
                }
            }
            g.freeBitmap(gDomeGlass);
            gDomeGlass = g.makeBitmap(px.data(), sz, sz);
            gDomeGlassKey = dk;
        }
        if (gDomeGlass.bmp) g.drawBitmapRect(gDomeGlass, cx - R, cy - R, 2.f * R, 2.f * R, 1.f);
    }

    // end circular dome clip (preserves any backdrop/scenery/rain outside the dome without punching an opaque box or double-rendering scenery)
    g.popCircleClip();
    (void)o;

    // Clean modern double-step optical lens bezel, 72 calibrated degree ticks & crisp cardinal markers (zero gold bezel)
    {
        const Color h = hor;
        const Color target = P.dark ? Color{1.f, 1.f, 1.f, 1.f} : P.ink;
        const Color rimCol = lerp(mulA(P.ink, 0.30f), tint(P, withA(lerp(h, target, P.dark ? 0.38f : 0.28f), P.dark ? 0.48f : 0.45f)), own);
        g.strokeCircle(cx, cy, R, rimCol, std::max(1.f, 1.30f * s));
        g.strokeCircle(cx, cy, R + 3.5f * s, mulA(P.ink, P.dark ? 0.13f : 0.16f), std::max(0.8f, 0.90f * s));
    }
    for (int i = 0; i < 72; ++i) {
        const float ang = (float)astro::rad(i * 5.0 - gSkyHeading);
        const bool card = (i % 18 == 0), maj = (i % 6 == 0);
        const float len = card ? 11.5f * s : (maj ? 6.8f * s : 3.4f * s);
        const float r0 = R + 4.5f * s, r1 = r0 + len;
        g.line(cx + r0 * std::sin(ang), cy - r0 * std::cos(ang), cx + r1 * std::sin(ang), cy - r1 * std::cos(ang),
               mulA(P.ink, card ? 0.56f : (maj ? 0.28f : 0.13f)), std::max(0.85f, (card ? 1.25f : 0.95f) * s), false);
    }
    {
        float lr = R + 29 * s; float fs = 14.5f * s;
        struct L { const wchar_t* t; float ang; bool n; } ls[4] = {{L"N", 0, true}, {L"E", 90, false}, {L"S", 180, false}, {L"W", 270, false}};
        for (auto& l : ls) {
            float ang = (float)astro::rad(l.ang - gSkyHeading);
            float px = cx + lr * std::sin(ang), py = cy - lr * std::cos(ang);
            g.text(F_MONO_M, fs, px, py + g.capHeight(F_MONO_M, fs) * 0.5f, l.t, l.n ? P.accent : P.ink2, Align::Center, 0.0f);
        }
    }
    // Modern precision optical zenith reticle & label
    g.strokeCircle(cx, cy, 4.2f * s, mulA(P.ink, 0.28f), std::max(0.8f, 0.9f * s));
    g.fillCircle(cx, cy, 1.35f * s, mulA(P.ink, 0.44f));
    if (labels) g.text(F_MONO, 10.f * s, cx + 8.5f * s, cy - 6.5f * s, L"ZENITH", mulA(P.ink, 0.24f), Align::Left, 0.14f);

    // below-horizon markers on the 360-deg rim, pointing at where Sun / Moon will rise
    if (!sunUp && sunAlt > -20.0) {
        Pt p = domePt(cx, cy, R, 0.0, a.sun.hor.az);
        float al = smoothstep(-20.f, -2.f, (float)sunAlt);
        g.glow(p.x, p.y, 12.f * s, P.accent, 0.45f * al);
        g.strokeCircle(p.x, p.y, 6.0f * s, withA(P.accent, 0.85f * al), std::max(1.2f, 1.4f * s));
        g.fillCircle(p.x, p.y, 2.2f * s, withA(P.accent, 0.92f * al));
    }
    if (!moonUp && a.moon.hor.alt > -20.0) {
        Pt p = domePt(cx, cy, R, 0.0, a.moon.hor.az);
        float al = smoothstep(-20.f, -2.f, (float)a.moon.hor.alt);
        g.glow(p.x, p.y, 12.f * s, P.accent2, 0.48f * al);
        g.strokeCircle(p.x, p.y, 6.0f * s, withA(P.accent2, 0.90f * al), std::max(1.2f, 1.4f * s));
        g.fillCircle(p.x, p.y, 2.2f * s, withA(P.accent2, 0.95f * al));
    }
}

// ---------------------------------------------------------------- Museum-Grade 3D Horological Planetarium Solar System Orrery (3.0.0)
// Cached high-res celestial vault + Asteroid Belt + Kuiper dust + 3D perspective-tilted ecliptic orbits + depth-sorted
// directionally sun-lit 3D planets (with Jupiter's bands & 4 Galilean moons, Saturn's split back/front Cassini rings,
// Earth's tilted Moon orbit & Rayleigh glow) behind a curved watch-glass sapphire lens.
static Bitmap gOrreryBg;
static uint64_t gOrreryBgKey = 0;

void orreryRelease(gfx::Gfx& g) {
    g.freeBitmap(gOrreryBg);
    gOrreryBgKey = 0;
    g.freeBitmap(gDomeGlass);
    gDomeGlassKey = 0;
}

static uint32_t orrHash(uint32_t x) {
    x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
    return x;
}
static float orrRand(uint32_t i, uint32_t salt) {
    return (orrHash(i * 374761393u ^ salt * 668265263u) & 0xFFFFu) / 65535.f;
}
static float orrVNoise(float x, float y) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3.f - 2.f * fx); fy = fy * fy * (3.f - 2.f * fy);
    auto h = [](int a, int b) { return (orrHash((uint32_t)a * 73856093u ^ (uint32_t)b * 19349663u) & 0xFFFFu) / 65535.f; };
    float v00 = h(xi, yi), v10 = h(xi + 1, yi), v01 = h(xi, yi + 1), v11 = h(xi + 1, yi + 1);
    return (v00 * (1.f - fx) + v10 * fx) * (1.f - fy) + (v01 * (1.f - fx) + v11 * fx) * fy;
}

void drawOrrery(Ctx& c, float cx, float cy, float R, bool labels, bool compact) {
    Gfx& g = c.g; App& a = c.a; const Palette& P = c.P; const float s = c.s;
    const double t = a.now;
    auto T = [&](Color col) { return tint(P, col); };

    // 3D planetarium ecliptic perspective: ~38° viewing inclination with a graceful -8° horological cant
    constexpr float TILT_Y = 0.72f;
    constexpr float ROT_RAD = -0.135f;
    const float cosR = std::cos(ROT_RAD), sinR = std::sin(ROT_RAD);

    // Perceptual logarithmic orbital radius map: spacious inner terrestrial orbits, clean outer gas giants
    auto orrRad = [&](double au) -> float {
        const double u = std::log(1.0 + 2.35 * std::max(0.05, au)) / std::log(1.0 + 2.35 * 31.8);
        return R * (float)(0.125 + 0.795 * u);
    };
    auto project3D = [&](float rr, double lonRad, float& sx, float& sy, float& zDepth) {
        const float lx = rr * (float)std::cos(lonRad);
        const float ly = -rr * (float)std::sin(lonRad) * TILT_Y;
        sx = cx + lx * cosR - ly * sinR;
        sy = cy + lx * sinR + ly * cosR;
        zDepth = -(float)std::sin(lonRad);  // -1 = far back (top), +1 = near front (bottom)
    };

    // 1. Cached high-res celestial vault + Asteroid Belt + Kuiper dust + sapphire watch-glass plate
    const int bmpSz = std::max(180, std::min(560, ((int)std::lround(R * 2.02f) + 7) & ~7));
    const uint64_t bgKey = ((uint64_t)bmpSz << 16) ^ ((uint64_t)(P.ember > 0.3f ? 2u : (P.dark ? 1u : 0u)) << 8) ^ (uint64_t)std::lround(P.accent.r * 31.f);
    if (!gOrreryBg.bmp || gOrreryBgKey != bgKey) {
        std::vector<uint8_t> px((size_t)bmpSz * (size_t)bmpSz * 4u, 0);
        const float half = bmpSz * 0.5f;
        const float invR = 1.f / (half * 0.992f);
        const Color cCore = T(rgb(0x0D1930));
        const Color cDeep = T(rgb(0x030712));
        const Color cNeb  = T(rgb(0x2A4B88));
        const Color cNeb2 = T(rgb(0x52347A));
        const Color cZod  = T(rgb(0xFFE0A4));
        const Color cGlass = T(rgb(0xD8EBFF));

        for (int y = 0; y < bmpSz; ++y) {
            const float ny = (y + 0.5f - half) * invR;
            for (int x = 0; x < bmpSz; ++x) {
                const float nx = (x + 0.5f - half) * invR;
                const float d2 = nx * nx + ny * ny;
                if (d2 > 1.01f) continue;
                const float d = std::sqrt(d2);
                const float edgeA = 1.f - smoothstep(0.985f, 1.005f, d);

                // Deep sapphire-obsidian space vignette
                const float radT = smoothstep(0.f, 1.f, d);
                float r = cCore.r * (1.f - radT) + cDeep.r * radT;
                float gr = cCore.g * (1.f - radT) + cDeep.g * radT;
                float b = cCore.b * (1.f - radT) + cDeep.b * radT;

                // Subtle diagonal Milky Way galactic dust swath across the celestial vault
                const float mwU = nx * 0.78f + ny * 0.62f;
                const float mwV = -nx * 0.62f + ny * 0.78f;
                const float band = std::exp(-mwV * mwV * 5.8f);
                const float n1 = orrVNoise(mwU * 3.8f + 4.1f, mwV * 5.2f + 2.7f);
                const float n2 = orrVNoise(mwU * 8.2f - 1.3f, mwV * 9.5f + 6.4f);
                const float neb = band * (0.48f * n1 + 0.52f * n2) * (1.f - 0.35f * d);
                r  += (cNeb.r * 0.65f + cNeb2.r * 0.35f) * neb * 0.24f;
                gr += (cNeb.g * 0.65f + cNeb2.g * 0.35f) * neb * 0.22f;
                b  += (cNeb.b * 0.65f + cNeb2.b * 0.35f) * neb * 0.30f;

                // Un-rotate into tilted ecliptic plane for warm zodiacal sunlight dust & faint asteroid mist
                const float ex = nx * cosR + ny * sinR;
                const float ey = (-nx * sinR + ny * cosR) / TILT_Y;
                const float er = std::sqrt(ex * ex + ey * ey);
                const float zod = std::exp(-er * er * 8.5f) * 0.24f + std::exp(-er * er * 2.2f) * 0.055f;
                r += cZod.r * zod; gr += cZod.g * zod * 0.90f; b += cZod.b * zod * 0.68f;

                // Faint dusty sheen along the Main Asteroid Belt (~2.7 AU -> er ~ 0.495)
                const float ab = std::exp(-((er - 0.50f) * (er - 0.50f)) / 0.0018f) * 0.065f;
                r += 0.68f * ab; gr += 0.76f * ab; b += 0.92f * ab;

                // Faint curved sapphire watch-glass dome reflection on upper-left quadrant
                const float gx = nx + 0.38f, gy = ny + 0.48f;
                const float gGlow = std::exp(-(gx * gx * 2.1f + gy * gy * 3.4f)) * 0.085f;
                const float rimGlass = smoothstep(0.86f, 0.995f, d) * std::max(0.f, -nx * 0.55f - ny * 0.83f) * 0.14f;
                r  += cGlass.r * (gGlow + rimGlass);
                gr += cGlass.g * (gGlow + rimGlass);
                b  += cGlass.b * (gGlow + rimGlass);

                const auto toB = [](float v) -> uint8_t { return (uint8_t)std::max(0, std::min(255, (int)(v * 255.f + 0.5f))); };
                const size_t idx = ((size_t)y * (size_t)bmpSz + (size_t)x) * 4u;
                px[idx + 0] = toB(std::min(1.f, b) * edgeA);
                px[idx + 1] = toB(std::min(1.f, gr) * edgeA);
                px[idx + 2] = toB(std::min(1.f, r) * edgeA);
                px[idx + 3] = toB(edgeA);
            }
        }

        // Stamp crisp pixel dots (background stars, Main Asteroid Belt rocks, Kuiper Belt ice specks)
        auto addDot = [&](float pxX, float pxY, float rad, float dr, float dg, float db, float da) {
            const int x0 = std::max(0, (int)std::floor(pxX - rad - 1.f));
            const int x1 = std::min(bmpSz - 1, (int)std::ceil(pxX + rad + 1.f));
            const int y0 = std::max(0, (int)std::floor(pxY - rad - 1.f));
            const int y1 = std::min(bmpSz - 1, (int)std::ceil(pxY + rad + 1.f));
            for (int yy = y0; yy <= y1; ++yy) {
                for (int xx = x0; xx <= x1; ++xx) {
                    const float nx = (xx + 0.5f - half) * invR, ny = (yy + 0.5f - half) * invR;
                    if (nx * nx + ny * ny > 0.985f) continue;
                    const float dd = std::hypot(xx + 0.5f - pxX, yy + 0.5f - pxY);
                    if (dd >= rad + 0.8f) continue;
                    const float wgt = (1.f - smoothstep(rad * 0.25f, rad + 0.7f, dd)) * da;
                    const size_t idx = ((size_t)yy * (size_t)bmpSz + (size_t)xx) * 4u;
                    px[idx + 0] = (uint8_t)std::min(255, (int)px[idx + 0] + (int)(db * wgt * 255.f));
                    px[idx + 1] = (uint8_t)std::min(255, (int)px[idx + 1] + (int)(dg * wgt * 255.f));
                    px[idx + 2] = (uint8_t)std::min(255, (int)px[idx + 2] + (int)(dr * wgt * 255.f));
                }
            }
        };

        // 200 deep-space background spectral stars
        for (uint32_t i = 0; i < 200; ++i) {
            const float ang = orrRand(i, 11) * 6.2831853f;
            const float rr = std::sqrt(orrRand(i, 12)) * 0.96f;
            const float sx = half + std::cos(ang) * rr * half * 0.992f;
            const float sy = half + std::sin(ang) * rr * half * 0.992f;
            const Color sc = T(starSpectral(orrRand(i, 13) * 1.9f - 0.25f));
            const float mag = orrRand(i, 14);
            addDot(sx, sy, 0.45f + 0.65f * mag * mag, sc.r, sc.g, sc.b, 0.18f + 0.55f * mag * mag);
        }

        // 520 Main Asteroid Belt micro-asteroids projected onto the tilted 3D ecliptic plane (between Mars 1.52 AU & Jupiter 5.20 AU)
        const double uMars = std::log(1.0 + 2.35 * 1.524) / std::log(1.0 + 2.35 * 31.8);
        const double uJup  = std::log(1.0 + 2.35 * 5.203) / std::log(1.0 + 2.35 * 31.8);
        const float rBelMin = (float)(0.125 + 0.795 * (uMars + (uJup - uMars) * 0.22));
        const float rBelMax = (float)(0.125 + 0.795 * (uMars + (uJup - uMars) * 0.76));
        for (uint32_t i = 0; i < 520; ++i) {
            const float ang = orrRand(i, 31) * 6.2831853f;
            const float gU = 0.5f * (orrRand(i, 32) + orrRand(i, 33));
            // Subtle Kirkwood resonance gap near 0.46
            float u = rBelMin + (rBelMax - rBelMin) * gU;
            if (std::fabs(gU - 0.48f) < 0.035f) u += (gU < 0.48f ? -0.014f : 0.014f);
            const float lx = u * std::cos(ang), ly = -u * std::sin(ang) * TILT_Y;
            const float sx = half + (lx * cosR - ly * sinR) * half * 0.992f;
            const float sy = half + (lx * sinR + ly * cosR) * half * 0.992f;
            const float zLit = 0.55f + 0.45f * (-std::sin(ang) * 0.5f + 0.5f);
            const float br = (0.14f + 0.34f * orrRand(i, 34)) * zLit;
            const Color ac = T(lerp(rgb(0x9CB0CE), rgb(0xDEC9A8), orrRand(i, 35)));
            addDot(sx, sy, 0.35f + 0.55f * orrRand(i, 36), ac.r, ac.g, ac.b, br);
        }

        // 160 icy Kuiper Belt micro-specks beyond Neptune
        for (uint32_t i = 0; i < 160; ++i) {
            const float ang = orrRand(i, 71) * 6.2831853f;
            const float u = 0.935f + 0.055f * orrRand(i, 72);
            const float lx = u * std::cos(ang), ly = -u * std::sin(ang) * TILT_Y;
            const float sx = half + (lx * cosR - ly * sinR) * half * 0.992f;
            const float sy = half + (lx * sinR + ly * cosR) * half * 0.992f;
            const Color kc = T(rgb(0x9AC4F0));
            addDot(sx, sy, 0.38f + 0.35f * orrRand(i, 73), kc.r, kc.g, kc.b, 0.12f + 0.22f * orrRand(i, 74));
        }

        g.freeBitmap(gOrreryBg);
        gOrreryBg = g.makeBitmap(px.data(), bmpSz, bmpSz);
        gOrreryBgKey = bgKey;
    }

    // gOrreryBg is already anti-aliased transparent outside R, and all orbits/planets sit strictly inside R,
    // so we avoid an outer pushCircleClip (allowing each 3D planet sphere to use pushCircleClip cleanly at depth 1!).
    if (gOrreryBg.bmp) g.drawBitmapRect(gOrreryBg, cx - R, cy - R, 2.f * R, 2.f * R, 1.f);
    else g.fillCircle(cx, cy, R, T(rgb(0x050B18)));

    // 2. Subtle astrolabe ecliptic spokes (12 zodiacal hour-lines projected onto the 3D ecliptic plane)
    for (int k = 0; k < 12; ++k) {
        const double ang = astro::rad(k * 30.0);
        float x0, y0, x1, y1, z0, z1;
        project3D(R * 0.14f, ang, x0, y0, z0);
        project3D(R * 0.97f, ang, x1, y1, z1);
        const bool card = (k % 3 == 0);
        g.line(x0, y0, x1, y1, withA(T(card ? rgb(0xE8C88A) : rgb(0x88AEE0)), card ? 0.11f : 0.048f), std::max(0.85f, s * (card ? 1.0f : 0.8f)), false);
    }

    // Compute live heliocentric ecliptic coordinates for all 8 planets
    astro::HelioBody bodies[astro::ORRERY_COUNT];
    for (int i = 0; i < astro::ORRERY_COUNT; ++i) bodies[i] = astro::orreryBody(i, t);

    static const unsigned pHex[astro::ORRERY_COUNT] = {
        0xCFC4B2, // Mercury
        0xF8E2AE, // Venus
        0x4BB8FF, // Earth
        0xE46C46, // Mars
        0xE8BA84, // Jupiter
        0xF2D492, // Saturn
        0x86E4EC, // Uranus
        0x5288FF, // Neptune
    };
    static const float pRad[astro::ORRERY_COUNT] = {3.2f, 4.9f, 5.6f, 4.1f, 9.4f, 7.8f, 5.7f, 5.5f};
    static const wchar_t* pShort[astro::ORRERY_COUNT] = {L"MER", L"VEN", L"EARTH", L"MARS", L"JUPITER", L"SATURN", L"URANUS", L"NEPTUNE"};

    // 3. 3D perspective-tilted Keplerian orbit tracks sampled from the exact same ephemeris as the planets
    // (cached once per day so eccentric orbits like Mercury & Mars stay 100% aligned with their planet markers at ~0 ms/frame).
    struct OrbitSample { double rAU, lonDeg; };
    constexpr int kOrbitSegs = 64;
    static long long gOrbitCacheDay = -1;
    static OrbitSample gOrbitPts[astro::ORRERY_COUNT][kOrbitSegs + 1];
    const long long curDay = (long long)std::floor(t / 86400.0);
    if (gOrbitCacheDay != curDay) {
        const double t0 = (double)curDay * 86400.0;
        for (int i = 0; i < astro::ORRERY_COUNT; ++i) {
            const double spanSec = bodies[i].periodDays * 86400.0;
            for (int k = 0; k <= kOrbitSegs; ++k) {
                const astro::HelioBody sb = astro::orreryBody(i, t0 + spanSec * ((double)k / (double)kOrbitSegs));
                gOrbitPts[i][k] = {sb.rAU, sb.lonDeg};
            }
        }
        gOrbitCacheDay = curDay;
    }

    for (int i = 0; i < astro::ORRERY_COUNT; ++i) {
        const bool isEarth = (i == astro::OP_EARTH);
        const Color baseC = isEarth ? T(rgb(0x74C8FF)) : T(lerp(rgb(0x9CB8DF), rgb(pHex[i]), 0.22f));
        for (int k = 0; k < kOrbitSegs; ++k) {
            const OrbitSample& s0 = gOrbitPts[i][k];
            const OrbitSample& s1 = gOrbitPts[i][k + 1];
            float x0, y0, x1, y1, z0, z1;
            project3D(orrRad(s0.rAU), astro::rad(s0.lonDeg), x0, y0, z0);
            project3D(orrRad(s1.rAU), astro::rad(s1.lonDeg), x1, y1, z1);
            const float zf = 0.5f + 0.5f * (0.5f * (z0 + z1)); // 0 far .. 1 near
            const float op = (isEarth ? (0.16f + 0.24f * zf) : ((i < 4 ? 0.08f : 0.065f) + 0.13f * zf));
            g.line(x0, y0, x1, y1, withA(baseC, op), std::max(0.9f, (isEarth ? 1.35f : 1.0f) * s), false);
        }
    }

    // Project all 8 planets + glowing swept motion wakes sampled backwards along each planet's true ephemeris path
    Pt pScreen[astro::ORRERY_COUNT];
    float pZ[astro::ORRERY_COUNT];
    const float sc = compact ? std::max(0.64f, R / 235.f) : s;
    for (int i = 0; i < astro::ORRERY_COUNT; ++i) {
        const float rr = orrRad(bodies[i].rAU);
        project3D(rr, astro::rad(bodies[i].lonDeg), pScreen[i].x, pScreen[i].y, pZ[i]);

        const Color pc = T(rgb(pHex[i]));
        const double wakeDays = bodies[i].periodDays * ((i < 4) ? (54.0 / 360.0) : (34.0 / 360.0));
        const int segs = 16;
        float prevX = pScreen[i].x, prevY = pScreen[i].y;
        for (int k = 1; k <= segs; ++k) {
            const float f0 = (float)(k - 1) / segs, f1 = (float)k / segs;
            const astro::HelioBody hb = astro::orreryBody(i, t - wakeDays * f1 * 86400.0);
            float x1, y1, z1;
            project3D(orrRad(hb.rAU), astro::rad(hb.lonDeg), x1, y1, z1);
            const float op = (1.f - f0) * (1.f - f0) * (i == astro::OP_EARTH ? 0.68f : 0.48f);
            g.line(prevX, prevY, x1, y1, withA(pc, op), std::max(1.1f, (compact ? 1.7f : 2.3f) * sc * (1.f - 0.60f * f0)), true);
            prevX = x1; prevY = y1;
        }
    }

    // Subtle Earth-Sun vernal sightline vector
    {
        const Pt& ep = pScreen[astro::OP_EARTH];
        const Color rayC = (a.lunarShadow.active || a.solarCover.active) ? withA(T(rgb(0xFF8C68)), 0.52f) : withA(T(rgb(0x74C8FF)), 0.22f);
        g.line(cx, cy, ep.x, ep.y, rayC, std::max(0.95f, 1.15f * s), false);
    }

    // 4. Depth-sort all 8 planets + the Sun (-1) from farthest (-1) to nearest (+1) for true 3D occlusion!
    struct DrawItem { int id; float z; };
    DrawItem items[astro::ORRERY_COUNT + 1];
    for (int i = 0; i < astro::ORRERY_COUNT; ++i) items[i] = {i, pZ[i]};
    items[astro::ORRERY_COUNT] = {-1, 0.f};
    std::sort(items, items + astro::ORRERY_COUNT + 1, [](const DrawItem& u, const DrawItem& v) { return u.z < v.z; });

    auto drawSaturnRings = [&](float px, float py, float pr, bool frontHalf) {
        const float rxOut = pr * 2.42f, ryOut = pr * 0.90f;
        // Clip cleanly to upper (back) or lower (front) half so rings visibly wrap around Saturn's 3D sphere!
        if (frontHalf) g.pushClip(px - rxOut - 6.f, py, 2.f * (rxOut + 6.f), ryOut + 8.f);
        else           g.pushClip(px - rxOut - 6.f, py - ryOut - 8.f, 2.f * (rxOut + 6.f), ryOut + 8.f);
        const float opMul = frontHalf ? 1.0f : 0.72f;
        // Outer A-ring
        g.strokeEllipse(px, py, rxOut, ryOut, withA(T(rgb(0xE8CF9C)), 0.56f * opMul), std::max(1.2f, 1.8f * sc));
        // Dark Cassini Division gap
        g.strokeEllipse(px, py, pr * 2.02f, pr * 0.75f, withA(rgb(0x0A0E18), 0.78f * opMul), std::max(0.8f, 1.0f * sc));
        // Bright inner B-ring
        g.strokeEllipse(px, py, pr * 1.72f, pr * 0.64f, withA(T(rgb(0xFBE5B4)), 0.82f * opMul), std::max(1.5f, 2.3f * sc));
        // Delicate translucent C-ring
        g.strokeEllipse(px, py, pr * 1.36f, pr * 0.50f, withA(T(rgb(0xC8B088)), 0.34f * opMul), std::max(0.8f, 1.0f * sc));
        g.popClip();
    };

    for (const DrawItem& it : items) {
        if (it.id == -1) {
            // Radiant Volumetric Sun with golden corona + soft 6-ray starburst flares
            const float sunR = std::max(6.8f, (compact ? 8.2f : 11.2f) * sc);
            g.glow(cx, cy, R * 0.30f, T(rgb(0xFFBB58)), 0.34f);
            g.glow(cx, cy, R * 0.15f, T(rgb(0xFFE49C)), 0.64f);
            // Soft solar coronal flare rays
            for (int r = 0; r < 3; ++r) {
                const float ang = ROT_RAD + r * 1.0471975f;
                const float len = sunR * (r == 0 ? 3.8f : 2.9f);
                const Color rayC = withA(T(rgb(0xFFE8B0)), r == 0 ? 0.24f : 0.16f);
                g.line(cx - std::cos(ang) * len, cy - std::sin(ang) * len, cx + std::cos(ang) * len, cy + std::sin(ang) * len, rayC, std::max(1.0f, 1.3f * sc), true);
            }
            g.radialFill(cx, cy, sunR * 1.35f, {
                {0.00f, T(rgb(0xFFFFFF))},
                {0.42f, T(rgb(0xFFF6D2))},
                {0.72f, T(rgb(0xFFD272))},
                {1.00f, withA(T(rgb(0xFF9C38)), 0.f)},
            });
            g.fillCircle(cx, cy, sunR * 0.78f, T(rgb(0xFFFDF0)));
            continue;
        }

        const int i = it.id;
        const Pt& p = pScreen[i];
        const Color pc = T(rgb(pHex[i]));
        const float persp = 1.0f + 0.10f * it.z;
        const float pr = std::max(2.6f, pRad[i] * sc * persp);

        // Direction toward the Sun for true 3D directional lighting!
        const float dx = cx - p.x, dy = cy - p.y;
        const float dl = std::max(1.f, std::sqrt(dx * dx + dy * dy));
        const float sx = dx / dl, sy = dy / dl;

        // Soft planetary atmospheric aura
        g.glow(p.x, p.y, pr * (i == astro::OP_EARTH ? 3.6f : 3.0f), pc, i == astro::OP_EARTH ? 0.56f : 0.34f);

        // Saturn back half of rings (behind the globe)
        if (i == astro::OP_SATURN) drawSaturnRings(p.x, p.y, pr, false);

        // Uranus delicate tilted icy ring
        if (i == astro::OP_URANUS && pr >= 3.8f) {
            g.strokeEllipse(p.x, p.y, pr * 0.72f, pr * 1.85f, withA(T(rgb(0xBCEFF4)), 0.36f), std::max(0.8f, 1.0f * sc));
        }

        // Earth's tilted 3D Moon orbit (back half before Earth; uses exact geocentric ecliptic longitude so
        // New Moon sits on Earth's sunward side and Full Moon sits opposite the Sun!)
        float moonX = 0, moonY = 0; bool moonFront = true;
        if (i == astro::OP_EARTH) {
            const float mR = pr * 2.65f;
            const double mAng = astro::rad(a.moon.eclLon);
            const float mlx = mR * (float)std::cos(mAng), mly = -mR * (float)std::sin(mAng) * TILT_Y;
            moonX = p.x + mlx * cosR - mly * sinR;
            moonY = p.y + mlx * sinR + mly * cosR;
            moonFront = (-std::sin(mAng)) >= 0.f;
            g.strokeEllipse(p.x, p.y, mR, mR * TILT_Y, withA(T(rgb(0xB6DAFF)), 0.24f), std::max(0.8f, 0.9f * sc));
            if (!moonFront) {
                const Color mCol = a.lunarShadow.active ? T(rgb(0xFF7250)) : T(rgb(0xE8F0FC));
                g.fillCircle(moonX, moonY, std::max(1.6f, 2.0f * sc), mCol);
            }
        }

        // 3D Sun-Lit Planetary Sphere (lit bright hemisphere facing Sun -> deep shaded nightside facing away!)
        g.pushCircleClip(p.x, p.y, pr);
        const Color litC = lerp(pc, rgb(0xFFFFFF), 0.52f);
        const Color midC = pc;
        const Color darkC = lerp(pc, rgb(0x060A14), 0.80f);
        g.radialFillOffset(p.x, p.y, pr * 1.18f, sx * pr * 0.44f, sy * pr * 0.44f, {
            {0.00f, litC},
            {0.42f, midC},
            {0.82f, lerp(midC, darkC, 0.65f)},
            {1.00f, darkC},
        });

        // Iconic planetary surface & cloud band detail inside the clipped sphere
        if (i == astro::OP_JUPITER && pr >= 5.f) {
            // Equatorial cloud belts + Great Red Spot hint
            g.fillRect(p.x - pr, p.y - pr * 0.46f, pr * 2.f, pr * 0.22f, withA(T(rgb(0xC67E50)), 0.42f));
            g.fillRect(p.x - pr, p.y - pr * 0.10f, pr * 2.f, pr * 0.18f, withA(T(rgb(0xFFF0D4)), 0.32f));
            g.fillRect(p.x - pr, p.y + pr * 0.16f, pr * 2.f, pr * 0.24f, withA(T(rgb(0xB86A42)), 0.46f));
            g.fillCircle(p.x + sx * pr * 0.18f, p.y + pr * 0.26f, pr * 0.16f, withA(T(rgb(0xD85A3A)), 0.62f));
        } else if (i == astro::OP_SATURN && pr >= 4.5f) {
            g.fillRect(p.x - pr, p.y - pr * 0.30f, pr * 2.f, pr * 0.16f, withA(T(rgb(0xD6AE68)), 0.32f));
            g.fillRect(p.x - pr, p.y + pr * 0.08f, pr * 2.f, pr * 0.18f, withA(T(rgb(0xC89C58)), 0.34f));
        } else if (i == astro::OP_EARTH && pr >= 3.8f) {
            // Subtle emerald-teal continents & bright polar cloud cap
            g.fillCircle(p.x + pr * 0.15f, p.y - pr * 0.12f, pr * 0.36f, withA(T(rgb(0x4AD492)), 0.42f));
            g.fillCircle(p.x - pr * 0.24f, p.y + pr * 0.18f, pr * 0.28f, withA(T(rgb(0x52CE8C)), 0.34f));
            g.fillCircle(p.x, p.y - pr * 0.78f, pr * 0.36f, withA(rgb(0xFFFFFF), 0.55f));
        } else if (i == astro::OP_MARS && pr >= 3.0f) {
            g.fillCircle(p.x, p.y - pr * 0.80f, pr * 0.28f, withA(rgb(0xFFF4EC), 0.58f));
        }

        // Crisp sunlit atmospheric rim crescent facing the Sun
        g.strokeCircle(p.x + sx * pr * 0.10f, p.y + sy * pr * 0.10f, pr * 0.96f, withA(litC, i == astro::OP_EARTH ? 0.62f : 0.34f), std::max(0.8f, 1.1f * sc));
        g.popCircleClip();

        // Saturn front half of rings (crossing smoothly OVER the planet!)
        if (i == astro::OP_SATURN) drawSaturnRings(p.x, p.y, pr, true);

        // Jupiter's 4 Galilean moons (Io, Europa, Ganymede, Callisto) orbiting live in real time!
        if (i == astro::OP_JUPITER && pr >= 5.5f) {
            static const double gPer[4] = {1.7691, 3.5512, 7.1546, 16.689};
            static const float  gDist[4] = {1.58f, 1.98f, 2.46f, 3.02f};
            const double days = t / 86400.0;
            for (int m = 0; m < 4; ++m) {
                const double ma = std::fmod(days / gPer[m] * 6.2831853 + m * 1.4, 6.2831853);
                const float mx = p.x + pr * gDist[m] * (float)std::cos(ma) * cosR;
                const float my = p.y + pr * gDist[m] * (float)std::cos(ma) * sinR - pr * gDist[m] * 0.28f * (float)std::sin(ma);
                g.fillCircle(mx, my, std::max(1.0f, 1.25f * sc), withA(T(rgb(0xF8ECD2)), 0.78f));
            }
        }

        // Earth's Moon when on the front half of its orbit
        if (i == astro::OP_EARTH && moonFront) {
            const Color mCol = a.lunarShadow.active ? T(rgb(0xFF7250)) : T(rgb(0xF2F7FF));
            g.glow(moonX, moonY, 5.f * sc, mCol, 0.35f);
            g.fillCircle(moonX, moonY, std::max(1.7f, 2.2f * sc), mCol);
        }
    }

    // 5. De-cluttered uppercase planet labels (Earth prioritized first; tests 4 sides against all planet dots, Sun & placed labels)
    if (labels) {
        struct LBox { float x0, y0, x1, y1; };
        std::vector<LBox> placed;
        const float sunClear = std::max(12.f, 16.f * sc);
        placed.push_back({cx - sunClear, cy - sunClear, cx + sunClear, cy + sunClear});
        static const int order[astro::ORRERY_COUNT] = {astro::OP_EARTH, astro::OP_SATURN, astro::OP_JUPITER, astro::OP_MARS, astro::OP_VENUS, astro::OP_URANUS, astro::OP_NEPTUNE, astro::OP_MERCURY};
        const float lfs = std::max(8.5f, (compact ? 8.8f : 10.0f) * sc);
        const float lh = g.capHeight(F_MONO_M, lfs) + 4.f * sc;
        for (int oi = 0; oi < astro::ORRERY_COUNT; ++oi) {
            const int i = order[oi];
            const Pt& p = pScreen[i];
            const float pr = std::max(2.6f, pRad[i] * sc) + (i == astro::OP_SATURN ? 9.f * sc : (i == astro::OP_JUPITER ? 4.f * sc : 3.f * sc));
            const float lw = g.textWidth(F_MONO_M, lfs, pShort[i], 0.10f);
            struct Cand { float x, y; Align al; LBox box; };
            const float pad = pr + 5.f * sc;
            Cand cands[4] = {
                {p.x + pad, p.y, Align::Left,  {p.x + pad, p.y - lh * 0.5f, p.x + pad + lw, p.y + lh * 0.5f}},
                {p.x - pad, p.y, Align::Right, {p.x - pad - lw, p.y - lh * 0.5f, p.x - pad, p.y + lh * 0.5f}},
                {p.x, p.y - pad - lh * 0.4f, Align::Center, {p.x - lw * 0.5f, p.y - pad - lh, p.x + lw * 0.5f, p.y - pad}},
                {p.x, p.y + pad + lh * 0.4f, Align::Center, {p.x - lw * 0.5f, p.y + pad, p.x + lw * 0.5f, p.y + pad + lh}},
            };
            for (const Cand& cd : cands) {
                const float c0 = std::hypot(cd.box.x0 - cx, cd.box.y0 - cy);
                const float c1 = std::hypot(cd.box.x1 - cx, cd.box.y0 - cy);
                const float c2 = std::hypot(cd.box.x0 - cx, cd.box.y1 - cy);
                const float c3 = std::hypot(cd.box.x1 - cx, cd.box.y1 - cy);
                if (std::max(std::max(c0, c1), std::max(c2, c3)) > R * 0.965f) continue;
                bool clash = false;
                for (const LBox& b : placed) {
                    if (cd.box.x0 < b.x1 + 5.f * sc && cd.box.x1 > b.x0 - 5.f * sc &&
                        cd.box.y0 < b.y1 + 3.f * sc && cd.box.y1 > b.y0 - 3.f * sc) { clash = true; break; }
                }
                if (!clash) {
                    for (int j = 0; j < astro::ORRERY_COUNT; ++j) {
                        if (j == i) continue;
                        const float jr = std::max(2.6f, pRad[j] * sc) + 5.f * sc;
                        if (pScreen[j].x + jr > cd.box.x0 && pScreen[j].x - jr < cd.box.x1 &&
                            pScreen[j].y + jr > cd.box.y0 && pScreen[j].y - jr < cd.box.y1) { clash = true; break; }
                    }
                }
                if (!clash) {
                    const Color lCol = i == astro::OP_EARTH ? T(rgb(0x98DCFF)) : lerp(rgb(0xDCE8FA), T(rgb(pHex[i])), 0.25f);
                    g.text(F_MONO_M, lfs, cd.x, cd.y + g.capHeight(F_MONO_M, lfs) * 0.4f, pShort[i],
                           withA(lCol, i == astro::OP_EARTH ? 0.96f : 0.76f), cd.al, 0.10f);
                    placed.push_back(cd.box);
                    break;
                }
            }
        }
    }

    // 6. Clean neutral observatory rim & calibrated degree ticks (matching Sky Dome — aligned with ecliptic -ROT_RAD)
    g.strokeCircle(cx, cy, R, mulA(P.ink, P.dark ? 0.24f : 0.30f), std::max(1.f, 1.25f * s));
    for (int i = 0; i < 72; ++i) {
        const float ang = (float)astro::rad(i * 5.0) - ROT_RAD;
        const bool card = (i % 18 == 0);
        const bool maj  = (i % 6 == 0);
        const float len = card ? (compact ? 8.f : 11.f) * s : (maj ? (compact ? 5.f : 7.f) * s : (compact ? 2.2f : 3.5f) * s);
        const float r0 = R + 4.5f * s, r1 = r0 + len;
        g.line(cx + r0 * std::cos(ang), cy - r0 * std::sin(ang), cx + r1 * std::cos(ang), cy - r1 * std::sin(ang),
               mulA(P.ink, card ? 0.44f : (maj ? 0.24f : 0.11f)), std::max(0.9f, (card ? 1.25f : 1.0f) * s), false);
    }
    if (!compact) {
        const float lr = R + 28.f * s, fs = 12.5f * s;
        struct EclL { const wchar_t* t; float ang; } els[4] = {{L"0\xB0", 0}, {L"90\xB0", 90}, {L"180\xB0", 180}, {L"270\xB0", 270}};
        for (const auto& e : els) {
            const float ang = (float)astro::rad(e.ang) - ROT_RAD;
            const float px = cx + lr * std::cos(ang), py = cy - lr * std::sin(ang);
            g.text(F_MONO_M, fs, px, py + g.capHeight(F_MONO_M, fs) * 0.5f, e.t, P.ink3, Align::Center, 0.06f);
        }
    }
}

// ---------------------------------------------------------------- 24 hour band
void drawDayBand(Ctx& c, float x, float y, float w, float h) {
    Gfx& g = c.g; App& a = c.a; const Palette& P = c.P; const float s = c.s;
    if (a.bandSunAlt.size() != 288 || a.bandMoonUp.size() != 288) return;
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
        const bool aqFresh = weather::airFresh(d, a.now);
        if (aqFresh) {
            const double aq = d.aqi;
            const bool isEu = (d.aqiScale == weather::AqiScale::Eu);
            aqCol = isEu
                ? (aq <= 20.0 ? rgb(0x62E2A0) : (aq <= 40.0 ? rgb(0x7EE6C6) : (aq <= 60.0 ? rgb(0xFFD26A) : (aq <= 80.0 ? rgb(0xFF9852) : rgb(0xFF6B6B)))))
                : (aq <= 50.0 ? rgb(0x62E2A0) : (aq <= 100.0 ? rgb(0xFFD26A) : (aq <= 150.0 ? rgb(0xFF9852) : rgb(0xFF6B6B))));
            aqCol = tint(P, aqCol);
            if (!P.dark) aqCol = lerp(aqCol, P.ink, 0.30f);
            const std::string aqWord = util::upper(weather::aqiLabel(aq, d.aqiScale));
            aqTxt = Ws(fmt("%s %.0f \xC2\xB7 %s", weather::aqiScaleShort(d.aqiScale), aq, aqWord.c_str()));
        }
        const float afs = 12.5f * s, iconW = 19.f * s, padX = 11.f * s, pillH = 26.f * s;
        const float twAq = g.textWidth(F_MONO_M, afs, aqTxt, 0.08f);
        const float pillW = std::min(cwid * 0.56f, padX + iconW + 7.f * s + twAq + padX);
        const float px = x + w - pillW, py = y + 9.f * s;
        const float mxAq = a.mouseX - a.shiftX, myAq = a.mouseY - a.shiftY;
        const bool allowAq = !a.showSettings && !a.showHelp && !a.isSaver && a.mouseX >= 0.f;
        const bool hovAq = allowAq && mxAq >= px - 3.f * s && mxAq <= px + pillW + 3.f * s && myAq >= py - 3.f * s && myAq <= py + pillH + 3.f * s;
        const glassfx::Pose gpAq = a.glass.pose(HIT_SKY_AQI, px, py, pillW, pillH, pillH * 0.5f, s, hovAq, true);
        if (gpAq.sheenA > 0.02f || gpAq.bounceEnergy > 0.02f) {
            g.fillRoundRect(gpAq.x, gpAq.y + 2.5f * s, gpAq.w, gpAq.h, gpAq.r, Color{0, 0, 0, (P.dark ? 0.22f : 0.09f) * clamp01(gpAq.sheenA + gpAq.bounceEnergy)});
        }
        Color pbg = P.panel; pbg.a = std::max(pbg.a, P.dark ? 0.075f : 0.065f);
        g.fillRoundRect(gpAq.x, gpAq.y, gpAq.w, gpAq.h, gpAq.r, pbg);
        if (gpAq.hoverT > 0.01f) g.fillRoundRect(gpAq.x, gpAq.y, gpAq.w, gpAq.h, gpAq.r, withA(aqCol, (P.dark ? 0.14f : 0.11f) * gpAq.hoverT));
        const Color aqRim = aqFresh
            ? withA(aqCol, (P.dark ? 0.42f : 0.50f) + 0.34f * clamp01(gpAq.hoverT + gpAq.bounceEnergy))
            : lerp(P.line, withA(P.ink, 0.55f), gpAq.hoverT);
        g.strokeRoundRect(gpAq.x + 0.5f, gpAq.y + 0.5f, std::max(1.f, gpAq.w - 1.f), std::max(1.f, gpAq.h - 1.f), std::max(0.5f, gpAq.r - 0.5f), aqRim, std::max(1.f, s));
        drawGlassEffects(c, HIT_SKY_AQI, gpAq, false, true);
        // crisp vector air-quality waves emblem (flexes softly with the liquid-glass touch pill)
        const float offX = (gpAq.x - px) * 0.35f, offY = (gpAq.y - py) * 0.35f;
        const float pcy = py + pillH * 0.5f + offY;
        const float ix = px + offX + padX, lw = std::max(1.25f, 1.50f * s);
        g.line(ix, pcy - 4.4f * s, ix + 9.2f * s, pcy - 4.4f * s, aqCol, lw, true);
        g.arc(ix + 9.2f * s, pcy - 6.3f * s, 1.9f * s, 180.f, -245.f, aqCol, lw, true);
        g.line(ix + 1.2f * s, pcy, ix + 14.8f * s, pcy, aqCol, lw, true);
        g.arc(ix + 14.8f * s, pcy - 2.0f * s, 2.0f * s, 180.f, -245.f, aqCol, lw, true);
        g.line(ix, pcy + 4.4f * s, ix + 11.0f * s, pcy + 4.4f * s, withA(aqCol, 0.85f), lw, true);
        g.arc(ix + 11.0f * s, pcy + 6.3f * s, 1.9f * s, 0.f, 245.f, withA(aqCol, 0.85f), lw, true);
        const float txtRoom = std::max(40.f * s, pillW - (padX + iconW + 7.f * s + padX));
        g.text(F_MONO_M, afs, ix + iconW + 7.f * s, pcy + g.capHeight(F_MONO_M, afs) * 0.5f, g.ellipsize(F_MONO_M, afs, aqTxt, txtRoom, 0.08f), aqCol, Align::Left, 0.08f);
        addHit(a, px - 3.f * s, py - 3.f * s, pillW + 6.f * s, pillH + 6.f * s, HIT_SKY_AQI);
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
    if (weather::airFresh(d, a.now) && cwid <= 160 * s) l3 += fmt("%s%s %.0f %s", l3.empty() ? "" : " · ", weather::aqiScaleShort(d.aqiScale), d.aqi, util::upper(weather::aqiLabel(d.aqi, d.aqiScale)).c_str());
    g.text(F_MONO, 15.5f * s, cx0, y + 82 * s, g.ellipsize(F_MONO, 15.5f * s, Ws(l3), cwid, 0.06f), P.ink2, Align::Left, 0.06f);
    // stale marker
    {
        std::string note;
        if (!d.source.empty() && d.source != weather::providerName(weather::PV_OPENMETEO)) note = "VIA " + util::upper(d.source);
        if (a.now - d.fetched > 3600) note += std::string(note.empty() ? "" : " · ") + "UPDATED " + clockStr(a, d.fetched);
        if (!note.empty()) g.text(F_MONO, 12.5f * s, x + w, y + 104 * s, Ws(note), P.ink3, Align::Right, 0.08f);
    }
    // summary sentence (cached per minute/fetch/place/source so switching places in the same timezone never reuses stale prose)
    static double sumFetched = -1, sumLat = 1e9, sumLon = 1e9, sumTemp = 1e9;
    static long long sumMin = -1; static int sumTz = 0; static bool sum24 = false, sumF = false;
    static std::string sumSrc, sumCache;
    const long long curMin = (long long)std::floor(a.now / 60.0);
    if (d.fetched != sumFetched || curMin != sumMin || a.tzOff != sumTz || a.st.h24 != sum24 || a.st.useF != sumF ||
        a.st.geo.lat != sumLat || a.st.geo.lon != sumLon || d.source != sumSrc || d.cur.temp != sumTemp) {
        sumFetched = d.fetched; sumMin = curMin; sumTz = a.tzOff; sum24 = a.st.h24; sumF = a.st.useF;
        sumLat = a.st.geo.lat; sumLon = a.st.geo.lon; sumSrc = d.source; sumTemp = d.cur.temp;
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
