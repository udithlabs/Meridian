// Meridian: weather drawn behind the clock on the Quiet face.
// Clouds drift, wind sweeps calm brush strokes across, rain falls, and a thunderstorm twists up a tornado. Clear and calm draws nothing.
// Everything is a pure function of the time (no particle state), so a frame can be drawn at any moment.
#include "scene.h"
#include <cmath>
#include <algorithm>

namespace app {

using namespace gfx;
using util::fmt;

namespace {

constexpr float PI_F = 3.14159265f;

float hashf(int i, int salt) {
    unsigned x = (unsigned)i * 374761393u + (unsigned)salt * 668265263u;
    x = (x ^ (x >> 13)) * 1274126177u; x ^= x >> 16;
    return (x & 0xFFFF) / 65535.f;
}

// a phase in radians for a unix time and a speed, reduced in double (a float cannot hold 1.8e9 seconds to better than two minutes)
float ph(double t, double speed, double off = 0.0) { return (float)std::fmod(t * speed + off, 6.283185307179586); }
// a position that moves at speed along a loop of the given span, from a start offset
float loopPos(double t, double speed, double start, double span) { double v = std::fmod(start + speed * t, span); if (v < 0) v += span; return (float)v; }

bool drizzleCode(int c) { return c == 51 || c == 53 || c == 55; }
bool sleetCode(int c) { return c == 56 || c == 57 || c == 66 || c == 67 || c == 77; }
bool hailCode(int c) { return c == 96 || c == 99 || c == 87 || c == 88 || c == 89 || c == 90; }
bool snowCode(int c) { return (c >= 71 && c <= 76) || c == 85 || c == 86; }
bool rainCode(int c) { return (c >= 61 && c <= 65) || (c >= 80 && c <= 82); }        // thunder codes are not rain on their own: a thunderstorm can be dry
bool stormCode(int c) { return c == 95 || c == 96 || c == 99; }

// what the weather asks for: strengths 0..1 of clouds, wind, rain, tornado, thunder, fog, snow, blizzard, drizzle, sleet, hail and gale.
// This is the weather itself, so the light can follow it on every face; whether it is drawn behind the clock is decided later.
void targets(const App& a, float t[App::QFX_COUNT]) {
    for (int i = 0; i < App::QFX_COUNT; ++i) t[i] = 0.f;
    switch (a.st.quietPreview) {
        case 1: t[0] = 0.90f; return;                                                                  // sculpted clouds
        case 2: t[1] = 0.85f; return;                                                                  // calm painterly wind
        case 3: t[0] = 0.82f; t[1] = 0.80f; return;                                                    // clouds and wind
        case 4: t[0] = 0.95f; t[2] = 0.85f; return;                                                    // rain
        case 5: t[3] = 1.f; t[4] = 1.f; t[0] = 0.95f; t[1] = 0.70f; t[2] = 0.55f; return;              // severe: tornado and lightning
        case 6: t[4] = 1.f; t[0] = 0.95f; t[1] = 0.20f; return;                                        // thunder and cloud, no rain
        case 7: t[4] = 1.f; t[0] = 0.95f; t[1] = 0.50f; t[2] = 0.80f; return;                          // thunderstorm with rain
        case 8: t[5] = 1.f; t[0] = 0.32f; return;                                                      // cinematic fog
        case 9: return;                                                                                // the sun flare alone: no weather
        case 10: t[0] = 0.65f; t[6] = 0.88f; return;                                                   // calm, soothing snowfall
        case 11: t[0] = 0.96f; t[1] = 0.92f; t[5] = 0.30f; t[6] = 0.90f; t[7] = 1.f; return;           // harsh snowstorm (blizzard)
        case 12: t[0] = 0.72f; t[5] = 0.22f; t[8] = 1.f; return;                                       // silky misty drizzle
        case 13: t[0] = 0.88f; t[1] = 0.30f; t[5] = 0.14f; t[9] = 1.f; return;                         // icy freezing sleet (ice streaks + wet snow)
        case 14: t[0] = 0.96f; t[1] = 0.35f; t[4] = 0.45f; t[10] = 1.f; return;                        // dramatic bouncing hailstorm
        case 15: t[0] = 0.80f; t[1] = 0.18f; t[11] = 1.f; return;                                      // roaring coastal gale
        case 16: t[12] = 1.f; return;                                                                  // aurora borealis / australis
        default: break;
    }
    // Live geomagnetic space weather (NOAA SWPC Planetary Kp-index): aurora emerges organically at night during live storms
    if (a.st.auroraAuto && spacewx::isLive(a.spaceWx, a.now) && a.sun.hor.alt < -3.0) {
        const float nightF = smoothstep(-3.f, -10.f, (float)a.sun.hor.alt);
        const float au = spacewx::auroraVisibility(a.spaceWx.kp, a.st.geo.lat, false);
        if (au > 0.02f) t[12] = clamp01(au * nightF);
    }
    if (!a.wx.valid) return;
    const weather::Current& c = a.wx.cur;
    const bool thunder = stormCode(c.code);
    const bool hail = hailCode(c.code);
    const bool thunderSnow = !hail && thunder && c.temp <= 1.0 && (snowCode(c.code) || c.precip >= 0.2);
    const bool sleet = !hail && (sleetCode(c.code) || ((rainCode(c.code) || drizzleCode(c.code)) && c.temp >= -2.0 && c.temp <= 1.5));
    const bool snow = !hail && !sleet && (snowCode(c.code) || thunderSnow);
    const bool drizzle = !hail && !sleet && !snow && (drizzleCode(c.code) || (!rainCode(c.code) && c.precip >= 0.04 && c.precip < 0.45));
    if (c.hasCloud && c.cloud >= 30.0) t[0] = 0.3f + 0.7f * clamp01((float)(c.cloud - 30.0) / 55.f);
    const double windEff = c.hasWind ? c.wind : 0.0;
    const double gustEff = c.hasGust ? c.gust : 0.0;
    const double w = std::max(windEff, 0.6 * gustEff);
    if (w >= 12.0) t[1] = 0.28f + 0.72f * clamp01((float)(w - 12.0) / 30.f);
    if (hail) {
        t[10] = (c.code == 99) ? 1.0f : 0.82f;
        t[0] = std::max(t[0], 0.94f);
        t[1] = std::max(t[1], 0.42f);
    }
    if (sleet) {
        const bool heavySleet = (c.code == 57 || c.code == 67 || c.precip >= 2.2);
        t[9] = std::max(heavySleet ? 0.92f : 0.72f, 0.50f + 0.50f * clamp01((float)(c.precip / 2.2)));
        t[0] = std::max(t[0], 0.78f);
        t[5] = std::max(t[5], 0.14f * t[9]);
    }
    if (snow) {
        const bool heavySnow = (c.code == 75 || c.code == 86 || c.precip >= 2.8);
        const float baseSnow = heavySnow ? 0.92f : (c.code == 71 ? 0.52f : 0.74f);
        t[6] = std::max(baseSnow, 0.45f + 0.55f * clamp01((float)(c.precip / 2.2)));
        t[0] = std::max(t[0], 0.45f + 0.25f * t[6]);
        // Blizzard / harsh snowstorm: snow whipped by strong winds/gusts, heavy snow with brisk winds, or freezing thundersnow
        const bool blizzWind = w >= 28.0 || gustEff >= 42.0 || (heavySnow && (w >= 18.0 || gustEff >= 30.0)) || thunderSnow;
        if (blizzWind) {
            const float windAmt = clamp01((float)(std::max(w - 18.0, (gustEff - 30.0) * 0.75) / 28.0));
            const float heavyBoost = heavySnow ? 0.22f : 0.f;
            t[7] = clamp01(0.38f + 0.62f * windAmt + heavyBoost);
            if (thunderSnow) t[7] = std::max(t[7], 0.85f);
            t[0] = std::max(t[0], 0.80f + 0.16f * t[7]);
            t[1] = std::max(t[1], 0.65f + 0.30f * t[7]);
            t[5] = std::max(t[5], 0.24f * t[7]);
        }
    }
    if (drizzle) {
        const float baseD = (c.code == 55) ? 0.95f : (c.code == 53 ? 0.78f : 0.60f);
        t[8] = std::max(baseD, 0.48f + 0.52f * clamp01((float)(c.precip / 0.6)));
        t[0] = std::max(t[0], 0.58f + 0.20f * t[8]);
        t[5] = std::max(t[5], 0.20f * t[8]);
    }
    const bool rain = !hail && !sleet && !snow && !drizzle && (rainCode(c.code) || c.precip >= 0.45);
    if (rain) { t[2] = 0.45f + 0.55f * clamp01((float)(c.precip / 3.0)); t[0] = std::max(t[0], 0.50f + 0.25f * t[2]); }     // rain falls out of cloud: always a few of them, whatever the cover reported
    if (thunder) { t[4] = 1.f; t[0] = std::max(t[0], 0.95f); t[1] = std::max(t[1], 0.3f); }   // dark cloud and lightning, with rain only if rain is reported
    if (!snow && !sleet && (gustEff >= 70.0 || (thunder && gustEff >= 55.0))) {               // a severe warm-sector storm: the tornado (never during freezing snow/sleet)
        t[3] = 1.f; t[4] = std::max(t[4], 0.7f); t[0] = std::max(t[0], 0.95f); t[1] = std::max(t[1], 0.7f);
    } else if (t[7] < 0.25f && (windEff >= 42.0 || gustEff >= 58.0)) {                        // Beaufort gale-force winds (when not already in a tornado or full blizzard)
        const float gAmt = clamp01((float)std::max((windEff - 42.0) / 24.0, (gustEff - 58.0) / 24.0));
        t[11] = clamp01(0.45f + 0.55f * gAmt);
        t[0] = std::max(t[0], 0.55f + 0.28f * t[11]);
        t[1] *= 1.f - 0.72f * t[11];
    }
    // fog: reported as fog, or the air is saturated and still. Damp mountain mornings read as a soft mist.
    if (c.code == 45 || c.code == 48) t[5] = 0.95f;
    else if (!rain && !snow && !sleet && !hail && !thunder && c.hasHumidity && c.humidity >= 96.0 && w < 10.0) t[5] = std::max(t[5], 0.25f + 0.45f * clamp01((float)(c.humidity - 96.0) / 4.f));
    if (t[5] > 0.01f && t[7] < 0.05f && t[11] < 0.05f) t[1] *= 1.f - 0.6f * t[5];             // wind does not show through thick calm fog (a blizzard or gale keeps its wind)
    if (t[12] > 0.01f) t[12] *= clamp01(1.f - 0.65f * t[0]) * clamp01(1.f - 0.80f * t[5]);     // overcast & fog veil the real live aurora gracefully
}

// the way the wind blows across the screen: +1 to the right, -1 to the left
// how dark and heavy the cloud is: storms, hail and blizzards most, sleet/gale/rain moderate, drizzle light
float heavyOf(const App& a) {
    float h = std::max(a.qfx[3], 0.85f * a.qfx[4]);
    h = std::max(h, std::max(0.84f * a.qfx[10], 0.78f * a.qfx[7]));
    h = std::max(h, std::max(0.52f * a.qfx[9], 0.46f * a.qfx[11]));
    h = std::max(h, std::max(0.30f * a.qfx[2], 0.16f * a.qfx[8]));
    return clamp01(h);
}

float windSide(const App& a) {
    if (a.st.quietPreview != 0 || !a.wx.valid || !a.wx.cur.hasWind) return 1.f;
    const double toward = (a.wx.cur.windDir + 180.0) * 3.14159265 / 180.0;
    return std::sin(toward) >= 0 ? 1.f : -1.f;
}

float windKmh(const App& a) {
    if (a.st.quietPreview == 2 || a.st.quietPreview == 3) return 32.f;
    if (a.st.quietPreview == 5) return 48.f;
    if (a.st.quietPreview == 11) return 58.f;
    if (a.st.quietPreview == 12) return 10.f;
    if (a.st.quietPreview == 13) return 34.f;
    if (a.st.quietPreview == 14) return 44.f;
    if (a.st.quietPreview == 15) return 66.f;
    if (!a.wx.valid) return 0.f;
    return (float)std::max(a.wx.cur.hasWind ? a.wx.cur.wind : 0.0, a.wx.cur.hasGust ? 0.6 * a.wx.cur.gust : 0.0);
}

// overall strength from Settings: every alpha goes through al()
float gFx = 1.f;
float al(float x) { return clamp01(x * gFx); }

// ---------------------------------------------------------------- sculpted volumetric clouds & intentional fog palettes
// Five directional cloud tones: crisp sunlit/moonlit crown rim, lit upper face, mid volume, cool shadow belly, deep flat base.
struct Tones { Color rim, hi, body, shade, deep; };
// Four atmospheric mist tones: upper diffused light bloom, luminous mist crests, cool valley mid-strata, deep grounding mist bed.
struct FogTones { Color glow, light, mid, deep; };

Tones cloudTones(const Palette& P, float heavy) {
    const Color white = {1.f, 1.f, 1.f, 1.f};
    Color rim, hi, body, shade, deep;
    if (P.nightness > 0.5f) {
        // Galaxy / Midnight / Night: moonlit silver-indigo sculpted silhouettes (with a whisper of warm champagne moonlight on warm-gold accents like Midnight)
        const float champ = smoothstep(0.76f, 0.90f, P.accent.r) * smoothstep(0.66f, 0.80f, P.accent.g) * (1.f - smoothstep(0.68f, 0.86f, P.accent.b));
        deep  = lerp(P.bgTop, P.bgBottom, 0.42f);
        shade = lerp(P.bgBottom, lerp(P.ink2, P.accent2, 0.28f), 0.24f - 0.05f * heavy);
        body  = lerp(P.bgBottom, lerp(P.ink, P.accent2, 0.30f), 0.42f - 0.09f * heavy);
        hi    = lerp(body, lerp(white, P.accent2, 0.28f), 0.48f - 0.14f * heavy);
        rim   = lerp(lerp(hi, white, 0.56f - 0.16f * heavy), lerp(white, P.accent, 0.45f), 0.26f * champ * (1.f - 0.45f * heavy));
    } else if (P.twilight > 0.4f) {
        // Dawn / Dusk / Sunset: cool violet-plum underbellies crowned by warm rose-gold / apricot sunlit rims
        const Color warm = lerp(P.accent, P.twiBottom, 0.34f);
        deep  = lerp(lerp(P.bgTop, P.twiTop, 0.72f), rgb(0x110B22), 0.25f + 0.25f * heavy);
        shade = lerp(lerp(P.bgTop, P.bgBottom, 0.32f), P.ink2, 0.20f - 0.06f * heavy);
        body  = lerp(lerp(P.bgBottom, P.ink2, 0.40f), P.twiTop, 0.26f * heavy);
        hi    = lerp(body, lerp(warm, P.ink, 0.40f), 0.62f - 0.22f * heavy);
        rim   = lerp(warm, white, 0.46f - 0.16f * heavy);
    } else if (!P.dark) {
        // Day / Sunlit / Sunlit II: luminous alabaster-white sunlit crowns over sculpted cool periwinkle-slate bases
        const float warmSun = smoothstep(0.18f, 0.65f, P.bgBottom.r - P.bgTop.r + 0.18f);
        deep  = lerp(P.bgTop, rgb(0x46586C), 0.52f + 0.26f * heavy);
        shade = lerp(P.bgTop, rgb(0x76899F), 0.52f + 0.22f * heavy);
        body  = lerp(rgb(0xDBE4EE), white, 0.52f - 0.24f * heavy);
        hi    = lerp(rgb(0xF3F7FB), white, 0.76f - 0.16f * heavy);
        rim   = lerp(white, rgb(0xFFF7E8), 0.28f * warmSun * (1.f - 0.7f * heavy));
    } else {
        // Overcast dark-flipped Day / neutral dark / theme packs: crisp silver-white ridges over dramatic brooding slate undersides
        deep  = lerp(P.bgTop, rgb(0x161F2B), 0.42f + 0.24f * heavy);
        shade = lerp(P.bgBottom, rgb(0x344354), 0.42f + 0.20f * heavy);
        body  = lerp(lerp(P.bgBottom, P.ink, 0.45f), rgb(0x95A6B8), 0.42f - 0.18f * heavy);
        hi    = lerp(body, lerp(P.ink, white, 0.55f), 0.60f - 0.20f * heavy);
        rim   = lerp(hi, white, 0.62f - 0.22f * heavy);
    }
    return {tint(P, rim), tint(P, hi), tint(P, body), tint(P, shade), tint(P, deep)};
}

FogTones fogTones(const Palette& T) {
    const Color white = {1.f, 1.f, 1.f, 1.f};
    Color glow, light, mid, deep;
    if (T.nightness > 0.5f) {
        // Midnight / Night: luminous cool moonlit silver-indigo valley mist over deep obsidian horizon
        deep  = lerp(T.bgTop, T.bgBottom, 0.72f);
        mid   = lerp(T.bgBottom, lerp(T.ink2, T.accent2, 0.36f), 0.30f);
        light = lerp(T.bgBottom, lerp(T.ink, T.accent2, 0.38f), 0.45f);
        glow  = lerp(T.bgBottom, lerp(white, T.accent2, 0.42f), 0.52f);
    } else if (T.twilight > 0.4f) {
        // Dawn / Dusk / Sunset: haunting rose-gold / amethyst twilight valley mist
        const Color warm = lerp(T.accent, T.twiBottom, 0.30f);
        deep  = lerp(T.bgTop, T.twiTop, 0.62f);
        mid   = lerp(T.bgBottom, lerp(T.ink2, T.twiTop, 0.35f), 0.34f);
        light = lerp(T.bgBottom, lerp(warm, T.ink, 0.48f), 0.54f);
        glow  = lerp(warm, white, 0.48f);
    } else if (!T.dark) {
        // Day / Sunlit: sculpted Nordic morning mist — luminous pearl-white crests rolling over cool slate-blue mist troughs
        deep  = lerp(T.bgTop, rgb(0x5B7086), 0.56f);
        mid   = lerp(T.bgTop, rgb(0x889BB0), 0.48f);
        light = lerp(rgb(0xF6F9FC), white, 0.80f);
        glow  = white;
    } else {
        // Overcast dark / generic dark / Ember
        deep  = lerp(T.bgTop, T.bgBottom, 0.55f);
        mid   = lerp(T.bgBottom, T.ink2, 0.34f);
        light = lerp(T.bgBottom, lerp(T.ink, T.accent2, 0.24f), 0.48f);
        glow  = lerp(T.ink, white, 0.38f);
    }
    return {tint(T, glow), tint(T, light), tint(T, mid), tint(T, deep)};
}

// Smooth volumetric plateau puff (no pinpoint star-glow polka dots!):
// lush translucent body fading gracefully from an offset directional light core to feathered cloud edges.
void volPuff(Gfx& g, float cx, float cy, float rx, float ry, float ox, float oy, const Color& core, const Color& edge, float op) {
    if (op < 0.004f || rx < 1.f || ry < 1.f) return;
    g.radialEllipse(cx, cy, rx, ry, ox, oy, {
        {0.00f, withA(core, 1.00f)},
        {0.34f, withA(lerp(core, edge, 0.26f), 0.88f)},
        {0.64f, withA(edge, 0.48f)},
        {0.85f, withA(edge, 0.14f)},
        {1.00f, withA(edge, 0.00f)},
    }, op);
}

// Soft atmospheric cloud-mist puff with gentler center slope for cirrus feathers, mist banks and shadow folds.
void softPlume(Gfx& g, float cx, float cy, float rx, float ry, float ox, float oy, const Color& core, const Color& edge, float op) {
    if (op < 0.004f || rx < 1.f || ry < 1.f) return;
    g.radialEllipse(cx, cy, rx, ry, ox, oy, {
        {0.00f, withA(core, 1.00f)},
        {0.40f, withA(lerp(core, edge, 0.38f), 0.72f)},
        {0.72f, withA(edge, 0.28f)},
        {0.90f, withA(edge, 0.07f)},
        {1.00f, withA(edge, 0.00f)},
    }, op);
}

// ---------------------------------------------------------------- Photorealistic Volumetric fBm Cloud Synthesizer (3.0.0)
// Generates real multi-octave domain-warped fractal noise clouds with Beer-Lambert directional self-shadowing
// and silver-lining edge scattering directly into cached premultiplied BGRA sprites (zero cartoon oval blobs!).
inline uint32_t cHash2(int x, int y, uint32_t seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 1442695041u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
inline float cVNoise(float x, float y, uint32_t seed) {
    const int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float fx = x - (float)xi, fy = y - (float)yi;
    // Quintic smoothstep eliminates grid crease artifacts completely
    const float ux = fx * fx * fx * (fx * (fx * 6.f - 15.f) + 10.f);
    const float uy = fy * fy * fy * (fy * (fy * 6.f - 15.f) + 10.f);
    constexpr float inv = 1.f / 65535.f;
    const float v00 = (cHash2(xi,     yi,     seed) & 0xFFFFu) * inv;
    const float v10 = (cHash2(xi + 1, yi,     seed) & 0xFFFFu) * inv;
    const float v01 = (cHash2(xi,     yi + 1, seed) & 0xFFFFu) * inv;
    const float v11 = (cHash2(xi + 1, yi + 1, seed) & 0xFFFFu) * inv;
    return (v00 + (v10 - v00) * ux) * (1.f - uy) + (v01 + (v11 - v01) * ux) * uy;
}
float cFbm(float x, float y, uint32_t seed, int octaves) {
    float sum = 0.f, amp = 0.50f, norm = 0.f;
    for (int o = 0; o < octaves; ++o) {
        sum += amp * cVNoise(x, y, seed + (uint32_t)o * 97u);
        norm += amp;
        // Orthonormal rotation + lacunarity breaks up axis alignment for organic atmospheric turbulence
        const float nx = (0.80f * x - 0.60f * y) * 2.04f + 13.7f;
        const float ny = (0.60f * x + 0.80f * y) * 2.04f - 7.3f;
        x = nx; y = ny;
        amp *= 0.50f;
    }
    return sum / std::max(0.001f, norm);
}

// kind: 0 = wispy silky high-altitude cirrus, 1 = natural convective cumulus/stratocumulus bank,
//       2 = sweeping horizontal night/coastal stratocumulus sheet, 3 = full-width overcast/storm canopy
Bitmap buildVolumetricCloud(Gfx& g, int sw, int sh, int kind, uint32_t seed, const Tones& T, float heavy, float opScale = 1.f) {
    if (sw < 16 || sh < 12) return Bitmap{};
    std::vector<uint8_t> px((size_t)sw * (size_t)sh * 4u, 0);
    const float invW = 1.f / (float)sw, invH = 1.f / (float)sh;
    const float flip = (seed & 1u) ? -1.f : 1.f;
    const float offX = (float)(seed % 19u) * 3.17f, offY = (float)((seed * 7u) % 17u) * 2.83f;

    auto sampleDensity = [&](float u, float v, int oct) -> float {
        // u in [-1, 1], v in [-1, 1] (for kind==3 canopy: v in [0, 1] hanging from top)
        if (kind == 3) {
            const float xEdge = smoothstep(-1.02f, -0.82f, u) * (1.f - smoothstep(0.82f, 1.02f, u));
            const float yDrop = 1.f - smoothstep(0.18f, 0.96f, v);
            const float baseEnv = xEdge * yDrop;
            if (baseEnv < 0.005f) return 0.f;
            const float wx = (cFbm(u * 2.6f + offX, v * 2.4f + offY, seed + 11u, 2) - 0.5f) * 0.55f;
            const float wy = (cFbm(u * 2.6f + offX + 5.1f, v * 2.4f + offY - 3.7f, seed + 23u, 2) - 0.5f) * 0.35f;
            const float n = cFbm((u + wx) * 3.1f + offX, (v + wy) * 3.8f + offY, seed + 41u, oct);
            return clamp01((baseEnv * 1.18f + (n - 0.44f) * 0.92f) * (0.85f + 0.25f * heavy));
        }

        // Smooth organic elliptical-radial feather so cloud silhouettes dissipate naturally with zero straight clip lines
        const float boxFade = smoothstep(-0.98f, -0.68f, u) * (1.f - smoothstep(0.68f, 0.98f, u))
                            * smoothstep(-0.98f, -0.68f, v) * (1.f - smoothstep(0.68f, 0.98f, v))
                            * (1.f - smoothstep(0.56f, 0.97f, std::hypot(u * 0.96f, v * 0.98f)));
        if (boxFade < 0.004f) return 0.f;

        const float uf = u * flip;
        if (kind == 0) {
            // Silky stretched high-altitude cirrus filaments (wind-sheared mare's tails)
            const float arch = std::sin(uf * 2.2f + 0.5f) * 0.16f - std::pow(std::max(0.f, (uf - 0.25f) / 0.75f), 1.8f) * 0.22f;
            const float dy = v - arch;
            const float env = std::exp(-(uf * uf * 1.65f + dy * dy * 7.8f)) * boxFade;
            if (env < 0.01f) return 0.f;
            const float wx = (cFbm(uf * 1.6f + offX, dy * 4.5f + offY, seed + 3u, 2) - 0.5f) * 0.65f;
            const float wy = (cFbm(uf * 1.8f + offX + 4.3f, dy * 4.8f + offY, seed + 7u, 2) - 0.5f) * 0.38f;
            const float fib = cFbm((uf + wx) * 2.1f + offX, (dy + wy) * 8.2f + offY, seed + 17u, oct);
            const float ridge = 1.f - std::fabs(2.f * cFbm((uf + wx * 0.6f) * 3.2f - offY, (dy + wy * 0.6f) * 10.5f + offX, seed + 29u, std::max(2, oct - 1)) - 1.f);
            const float raw = env * (0.45f + 1.05f * fib * 0.72f + 0.48f * ridge * ridge) - (0.34f - 0.08f * heavy);
            return clamp01(raw * 2.1f);
        }

        if (kind == 1) {
            // Photorealistic turbulent convective cloud bank: naturally ragged asymmetric crown, flat atmospheric base
            const float skew = -0.10f * uf + 0.08f * std::sin(uf * 3.0f);
            const float flatBase = 1.f - smoothstep(0.18f, 0.58f, v);
            const float domeW = 1.f + 0.28f * smoothstep(-0.35f, 0.35f, v);  // wider at base, narrower ragged crests above
            const float r2 = (uf / domeW) * (uf / domeW) * 1.48f + (v + 0.06f - skew) * (v + 0.06f - skew) * 2.45f;
            const float env = std::exp(-r2 * 1.35f) * (0.35f + 0.65f * flatBase) * boxFade;
            if (env < 0.012f) return 0.f;
            const float wx = (cFbm(uf * 2.1f + offX, v * 2.4f + offY, seed + 13u, 2) - 0.5f) * 0.62f;
            const float wy = (cFbm(uf * 2.1f + offX + 6.7f, v * 2.4f + offY - 2.9f, seed + 19u, 2) - 0.5f) * 0.52f;
            const float nMain = cFbm((uf + wx) * 3.4f + offX, (v + wy) * 4.0f + offY, seed + 31u, oct);
            const float billow = 1.f - std::fabs(2.f * cFbm((uf + wx * 0.5f) * 5.2f + offY, (v + wy * 0.5f) * 5.8f - offX, seed + 53u, std::max(2, oct - 1)) - 1.f);
            const float detail = 0.74f * nMain + 0.26f * billow * billow;
            const float raw = env * 1.32f + (detail - 0.50f) * (0.85f * smoothstep(0.02f, 0.42f, env)) - 0.26f;
            return clamp01(raw * 1.95f);
        }

        // kind == 2: Sweeping coastal / nocturnal stratocumulus vapor deck (wide shredded horizontal bands with realistic rifts)
        const float wave = 0.07f * std::sin(uf * 2.7f + 0.8f);
        const float dy = v - wave;
        const float baseCut = 1.f - smoothstep(0.22f, 0.62f, dy);
        const float env = std::exp(-(uf * uf * 1.25f + dy * dy * 4.6f)) * (0.40f + 0.60f * baseCut) * boxFade;
        if (env < 0.01f) return 0.f;
        const float wx = (cFbm(uf * 1.9f + offX, dy * 3.4f + offY, seed + 37u, 2) - 0.5f) * 0.72f;
        const float wy = (cFbm(uf * 2.0f + offX - 5.4f, dy * 3.6f + offY + 4.1f, seed + 43u, 2) - 0.5f) * 0.40f;
        const float n = cFbm((uf + wx) * 2.8f + offX, (dy + wy) * 5.6f + offY, seed + 61u, oct);
        const float wisps = cFbm((uf + wx * 0.8f) * 5.4f - offY, (dy + wy * 0.6f) * 8.2f + offX, seed + 79u, std::max(2, oct - 1));
        const float raw = env * 1.28f + (0.68f * n + 0.32f * wisps - 0.49f) * (0.90f * smoothstep(0.02f, 0.38f, env)) - 0.24f;
        return clamp01(raw * 2.05f);
    };

    // Light march vector toward upper-left sunlight / moonlight
    const float lx = -0.055f, ly = (kind == 3) ? -0.095f : -0.088f;
    const auto toB = [](float val) -> uint8_t { return (uint8_t)std::max(0, std::min(255, (int)(val * 255.f + 0.5f))); };

    for (int y = 0; y < sh; ++y) {
        const float v = (kind == 3) ? ((y + 0.5f) * invH) : ((y + 0.5f) * invH * 2.f - 1.f);
        for (int x = 0; x < sw; ++x) {
            const float u = (x + 0.5f) * invW * 2.f - 1.f;
            const float d = sampleDensity(u, v, 5);
            if (d < 0.006f) continue;

            // 2-step Beer-Lambert directional light march for genuine 3D volumetric internal shadows & silver-lining rims!
            const float d1 = sampleDensity(u + lx * 0.75f, v + ly * 0.75f, 3);
            const float d2 = sampleDensity(u + lx * 2.05f, v + ly * 2.05f, 3);
            const float occ = clamp01(0.36f * d + 0.62f * d1 + 0.74f * d2);
            const float trans = std::exp(-occ * (kind == 0 ? 1.45f : (2.15f + 0.55f * heavy)));
            const float skyGrad = (kind == 3) ? clamp01(0.42f - 0.28f * v) : clamp01(0.58f - 0.54f * v);
            // Silver-lining forward scatter where cloud wisps thin out facing the sky light
            const float silver = clamp01((1.f - smoothstep(0.10f, 0.68f, d2)) * smoothstep(0.04f, 0.36f, d) * (0.35f + 0.65f * skyGrad));
            // Internal sculptural light relief so cloud interiors show rich rolling volumetric depth (never flat grey!)
            const float relief = (d - d1) * 0.85f + (cFbm(u * 4.4f + offX, v * 5.2f + offY, seed + 131u, 3) - 0.5f) * 0.24f;
            const float lit = clamp01(trans * (0.32f + 0.68f * skyGrad) + relief + (kind == 0 ? 0.34f : 0.24f) * silver * (1.f - 0.35f * heavy));

            Color col;
            if (lit < 0.30f)      col = lerp(T.deep,  T.shade, lit / 0.30f);
            else if (lit < 0.58f) col = lerp(T.shade, T.body,  (lit - 0.30f) / 0.28f);
            else if (lit < 0.82f) col = lerp(T.body,  T.hi,    (lit - 0.58f) / 0.24f);
            else                  col = lerp(T.hi,    T.rim,   (lit - 0.82f) / 0.18f);

            const float alpha = clamp01(smoothstep(0.01f, kind == 0 ? 0.56f : 0.72f, d) * opScale);
            const size_t idx = ((size_t)y * (size_t)sw + (size_t)x) * 4u;
            px[idx + 0] = toB(col.b * alpha);
            px[idx + 1] = toB(col.g * alpha);
            px[idx + 2] = toB(col.r * alpha);
            px[idx + 3] = toB(alpha);
        }
    }
    return g.makeBitmap(px.data(), sw, sh);
}

// Keep a tiny ultra-soft fallback only if a sprite hasn't finished building yet on frame 0
void fallbackCloudPuff(Gfx& g, float cx, float cy, float cw, const Tones& T, float op) {
    softPlume(g, cx, cy, cw * 0.45f, cw * 0.16f, -cw * 0.08f, -cw * 0.05f, T.body, T.shade, op * 0.38f);
}

// ---------------------------------------------------------------- cached soft shapes
// A cloud never changes shape, it only slides. Each one is drawn once into a small image and then just moved, which costs a
// fraction of redrawing hundreds of big gradients every frame. Images are made from the colours the theme is HEADING for (not
// the ones it is fading through), a couple per frame at most, so changing the theme or the weather never causes a long frame.
constexpr float SPR = 0.5f;           // images are kept at half size: the shapes are soft, and it quarters the memory

struct Slot { Bitmap bmp; uint64_t key = 0, failed = 0; float nominal = 0; };       // nominal: the size (px) the image was drawn for, so a stale one can be scaled
Slot gCloud[6], gCeiling, gMist, gFront, gFog[3], gFogGlow, gSnowFlake, gBlizzStreak, gBlizzDrift, gDrizzleStreak, gSleetStreak, gHailStone, gGaleGust, gAuroraRay[3], gAuroraGlow, gClockRefl;
float gCeilH = 0, gMistH = 0, gFrontCw = 0, gFogH = 0, gSnowCell = 0, gBlizzS = 0, gBlizzSlant = 0, gBlizzDriftH = 0;
float gDrizS = 0, gDrizSlant = 0, gSleetS = 0, gSleetSlant = 0, gHailS = 0, gHailSlant = 0, gGaleGustH = 0;
bool gResizing = false;      // the window size changed a moment ago: keep drawing the images we have, rebuild once it settles
int gBudget = 0; double gBudgetAt = -1e18;
} // namespace
int gSpriteBuilds = 0;
namespace {

// what this frame draws with
struct Frame {
    Tones T;
    FogTones fog;
    Color mist, wind, windHead, leaf, rain, drizzle, sleet, hailCore, hailIce, snow, snowRim, galeHaze;
    int hq = 0;
};

uint64_t q3(const Color& c) { return ((uint64_t)(c.r * 7.f + 0.5f) << 6) | ((uint64_t)(c.g * 7.f + 0.5f) << 3) | (uint64_t)(c.b * 7.f + 0.5f); }
uint64_t mixKey(uint64_t a, uint64_t b) { return (a ^ (b + 0x9E3779B97F4A7C15ull + (a << 6) + (a >> 2))) | 1ull; }

void budgetTick(const App& a) { if (a.now != gBudgetAt) { gBudgetAt = a.now; gBudget = std::max(1, a.spriteBudget); } }

// (Re)draws a slot's image when its key changed, within the frame's budget. True when the slot has an image to use (possibly in last
// frame's colours: they differ by a few percent, on shapes that are already faint).
template <class Build> bool ensure(Gfx& g, Slot& s, uint64_t key, float nominal, Build build) {
    if (s.bmp.bmp && s.key == key) return true;
    if (s.bmp.bmp && gResizing) return true;                           // while you drag the window edge, reuse (and stretch) what we have
    if (s.failed == key || gBudget <= 0) return s.bmp.bmp != nullptr;
    --gBudget;
    Bitmap nb = build();
    if (!nb.bmp) { s.failed = key; return s.bmp.bmp != nullptr; }
    g.freeBitmap(s.bmp); s.bmp = nb; s.key = key; s.nominal = nominal; ++gSpriteBuilds;
    return true;
}

// remembers when the window size last changed (called once a frame)
void trackSize(const Ctx& c) {
    static float lw = 0, lh = 0; static double at = -1e18;
    if (std::fabs(c.W - lw) > 0.5f || std::fabs(c.H - lh) > 0.5f) { lw = c.W; lh = c.H; at = c.a.now; }
    gResizing = (c.a.now - at) < 0.35 && c.a.now >= at;
}
// sizes in the image keys are rounded, so the image is rebuilt every ~10% of resize, not at every pixel
uint64_t qsz(float v) { return (uint64_t)std::lround(std::log2(std::max(8.f, v)) * 6.f); }

void freeSlot(Gfx& g, Slot& s) { g.freeBitmap(s.bmp); s.key = s.failed = 0; }

float frontWidth(float W, float H) { return W * (W < H * 1.15f ? 1.05f : 0.68f); }
float frontHeightScale(float clockH, float cw) { return std::max(0.45f, std::min(1.05f, clockH / (0.52f * cw) * 0.48f)); }

Frame makeFrame(Ctx& c, float heavy) {
    Frame F;
    const Palette& T = c.a.palTarget;                       // the colours the face is settling into
    const float h = std::max(0.f, std::min(1.f, std::max(heavy, 0.55f * c.a.gloom)));
    F.hq = (int)std::lround(h * 4.f);                       // storm darkness in four steps, so the images are not rebuilt all the time
    F.T = cloudTones(T, F.hq / 4.f);
    F.fog = fogTones(T);
    F.mist = tint(T, lerp(lerp(T.ink, T.accent2, 0.35f), T.bgBottom, 0.55f * T.nightness));
    const Color white = {1.f, 1.f, 1.f, 1.f};
    const Palette& P = c.P;                                 // live fading palette for uncached wind strokes and leaves
    F.wind = tint(P, lerp(P.ink, P.ink2, 0.18f));           // soft brush sheath
    F.windHead = tint(P, P.dark ? lerp(P.ink, white, 0.55f) : P.ink);      // crisp bright core & dust motes
    F.leaf = tint(P, lerp(P.ink2, P.accent, 0.44f));        // tumbling leaves catching the light
    // rain: pale blue-white on a dark page, slate blue on a light one, so the drops read on every theme
    F.rain = tint(T, T.dark ? lerp(lerp(T.ink, T.accent2, 0.35f), white, 0.30f) : lerp(T.ink2, T.accent2, 0.45f));
    // drizzle: softer, mistier silver-blue filaments (deep enough to read cleanly on bright overcast Day too)
    F.drizzle = tint(T, T.dark ? lerp(lerp(T.ink2, T.accent2, 0.34f), white, 0.38f) : lerp(T.ink, T.accent2, 0.38f));
    // sleet: crisp icy glacier silver-cyan
    F.sleet = tint(T, T.dark ? lerp(rgb(0xC9E8FF), white, 0.46f) : lerp(T.ink2, rgb(0x3F6F9E), 0.58f));
    // hail: luminous faceted crystal ice core + sapphire-glacier refraction rim
    F.hailCore = tint(T, T.dark ? rgb(0xF4FAFF) : white);
    F.hailIce = tint(T, T.dark ? lerp(rgb(0x98D2FF), T.accent2, 0.30f) : lerp(T.ink2, rgb(0x436E9E), 0.55f));
    // snow: luminous ice-white on dark themes, crisp pearl-white with soft slate-blue rim on bright daylight themes
    F.snow = tint(T, T.dark ? lerp(lerp(T.ink, white, 0.72f), rgb(0xE8F4FF), 0.30f) : lerp(white, rgb(0xEEF5FC), 0.45f));
    F.snowRim = tint(T, T.dark ? lerp(T.accent2, white, 0.40f) : lerp(T.ink2, T.accent2, 0.42f));
    // gale: sweeping aerodynamic spray / mist-shear veil
    F.galeHaze = tint(T, T.dark ? lerp(T.bgBottom, lerp(T.ink, T.accent2, 0.35f), 0.32f) : lerp(T.bgBottom, rgb(0x70849B), 0.42f));
    return F;
}

// One cached photorealistic volumetric fBm image slot per cloud archetype:
//   0, 1: High sweeping silky cirrus / upper-sky filaments
//   2, 3: Photorealistic turbulent convective cumulus banks
//   4, 5: Wide atmospheric night/coastal stratocumulus vapor decks
bool cloudReady(Ctx& c, const Frame& F, int j) {
    const int sq = (int)std::lround(std::log2(std::max(0.05f, c.s)) * 5.f);          // the scale in ~15% steps
    const float cw = (490.f + 76.f * j) * std::pow(2.f, sq / 5.f);
    uint64_t key = mixKey(mixKey(mixKey(q3(F.T.body), q3(F.T.hi)), mixKey(q3(F.T.shade), q3(F.T.rim))),
                          mixKey(mixKey(q3(F.T.deep), (uint64_t)(sq + 300)), (uint64_t)(8100 + j * 16 + F.hq)));
    return ensure(c.g, gCloud[j], key, cw, [&] {
        const float aspW = (j < 2) ? 1.88f : (j < 4 ? 1.72f : 1.92f);
        const float aspH = (j < 2) ? 0.68f : (j < 4 ? 0.96f : 0.78f);
        const int sw = std::max(64, std::min(440, (int)(cw * aspW * SPR)));
        const int sh = std::max(40, std::min(240, (int)(cw * aspH * SPR)));
        const int kind = (j < 2) ? 0 : (j < 4 ? 1 : 2);
        return buildVolumetricCloud(c.g, sw, sh, kind, (uint32_t)(107 + j * 53), F.T, F.hq / 4.f, 1.0f);
    });
}

// Photorealistic turbulent storm / heavy overcast upper cloud canopy
bool ceilingReady(Ctx& c, const Frame& F) {
    const float W = c.W, H = c.H;
    uint64_t key = mixKey(mixKey(mixKey(q3(F.T.deep), q3(F.T.shade)), mixKey(q3(F.T.body), q3(F.T.hi))), mixKey(qsz(W), mixKey(qsz(H), (uint64_t)(8200 + F.hq))));
    gCeilH = 0.44f * H;
    return ensure(c.g, gCeiling, key, 0.f, [&] {
        const int sw = std::max(96, std::min(480, (int)(W * SPR * 0.75f)));
        const int sh = std::max(48, std::min(220, (int)(gCeilH * SPR * 0.85f)));
        return buildVolumetricCloud(c.g, sw, sh, 3, 913u, F.T, F.hq / 4.f, 0.88f);
    });
}

bool mistReady(Ctx& c, const Frame& F) {
    const float W = c.W, H = c.H;
    uint64_t key = mixKey(mixKey(q3(F.mist), qsz(W)), qsz(H));
    gMistH = 0.26f * H;
    return ensure(c.g, gMist, key, 0.f, [&] {
        const int sw = std::max(16, (int)(W * SPR)), sh = std::max(16, (int)(gMistH * SPR));
        if (!c.g.beginSprite(sw, sh)) return Bitmap{};
        const float R = sh * 0.98f; const int n = (int)std::ceil(sw / (R * 0.8f)) + 1;
        for (int j = 0; j < n; ++j) c.g.glow(sw * (j + 0.5f) / n, (float)sh, R, F.mist, 0.44f);
        return c.g.endSprite();
    });
}

// The ethereal, feathered volumetric cloud wisp that glides softly in front of the Quiet clock
bool frontReady(Ctx& c, const Frame& F, float clockH) {
    const float W = c.W, H = c.H;
    const float fcw = frontWidth(W, H), fht = frontHeightScale(clockH, fcw);
    uint64_t key = mixKey(mixKey(mixKey(q3(F.T.body), q3(F.T.hi)), mixKey(q3(F.T.rim), qsz(W))), mixKey(qsz(H), (uint64_t)(8300 + (int)(fht * 8.f))));
    gFrontCw = fcw;
    return ensure(c.g, gFront, key, 0.f, [&] {
        const int sw = std::max(80, std::min(420, (int)(fcw * 1.65f * SPR * 0.8f)));
        const int sh = std::max(40, std::min(200, (int)(fcw * 0.78f * fht * SPR * 0.8f)));
        return buildVolumetricCloud(c.g, sw, sh, 2, 777u, F.T, F.hq / 4.f, 0.82f);
    });
}

// Three distinct sculpted multi-tone rolling mist banks (no flat oval rows, zero hard sprite edges!)
//   j = 0: High gossamer valley mist veil (slender sweeping translucent ribbons)
//   j = 1: Undulating rolling mist swells with soft rising plumes & luminous crests
//   j = 2: Lush low ground mist shelf with rich cool-trough to pearl-crest depth separation
bool fogReady(Ctx& c, const Frame& F, int j) {
    const float W = c.W, H = c.H;
    uint64_t key = mixKey(mixKey(mixKey(q3(F.fog.light), q3(F.fog.mid)), mixKey(q3(F.fog.deep), q3(F.fog.glow))),
                          mixKey(mixKey(qsz(W), qsz(H)), (uint64_t)(7300 + j)));
    gFogH = 0.36f * H;
    return ensure(c.g, gFog[j], key, 0.f, [&] {
        const int sw = std::max(48, (int)(W * 1.24f * SPR));
        const int sh = std::max(32, (int)(gFogH * SPR));
        if (!c.g.beginSprite(sw, sh)) return Bitmap{};
        auto clampX = [&](float x, float rx) { return std::max(rx * 1.04f, std::min(sw - rx * 1.04f, x)); };
        auto clampY = [&](float y, float ry) { return std::max(ry * 1.04f, std::min(sh - ry * 1.04f, y)); };

        if (j == 0) {
            // High gossamer mist veil: slender horizontal ribbons undulating gently
            const int n = 11;
            for (int m = 0; m < n; ++m) {
                const float u = (m + 0.5f) / n;
                const float env = std::pow(std::sin(u * PI_F), 0.72f);
                const float rx = sw * (0.11f + 0.08f * env * (0.82f + 0.36f * hashf(m, 611)));
                const float ry = sh * (0.12f + 0.11f * env * (0.80f + 0.40f * hashf(m, 612)));
                const float cx = clampX(sw * (0.08f + 0.84f * u) + (hashf(m, 613) - 0.5f) * sw * 0.04f, rx);
                const float wave = std::sin(u * PI_F * 2.4f + 0.6f) * sh * 0.11f;
                const float cy = clampY(sh * 0.54f + wave + (hashf(m, 614) - 0.5f) * sh * 0.08f, ry);
                // Cool misty shadow underside
                softPlume(c.g, cx, clampY(cy + ry * 0.24f, ry), rx * 1.06f, ry * 0.96f, 0.f, ry * 0.18f, F.fog.mid, F.fog.deep, 0.34f * env);
                // Luminous silky ribbon crest
                softPlume(c.g, cx, clampY(cy - ry * 0.12f, ry * 0.75f), rx * 0.94f, ry * 0.72f, -rx * 0.12f, -ry * 0.26f, F.fog.light, F.fog.mid, 0.48f * env);
                // Ultra-thin horizontal gossamer highlight filament
                if (env > 0.45f) {
                    const float fry = std::max(3.f, ry * 0.32f);
                    softPlume(c.g, cx, clampY(cy - ry * 0.28f, fry), rx * 0.78f, fry, 0.f, 0.f, F.fog.glow, F.fog.light, 0.26f * env);
                }
            }
        } else if (j == 1) {
            // Rolling valley mist swells with rising wisps & sculpted light-over-shadow contours
            const int n = 12;
            for (int m = 0; m < n; ++m) {
                const float u = (m + 0.5f) / n;
                const float env = std::pow(std::sin(u * PI_F), 0.68f);
                const float rx = sw * (0.10f + 0.085f * env * (0.85f + 0.35f * hashf(m, 621)));
                const float ry = sh * (0.18f + 0.15f * env * (0.82f + 0.36f * hashf(m, 622)));
                const float cx = clampX(sw * (0.07f + 0.86f * u) + (hashf(m, 623) - 0.5f) * sw * 0.045f, rx);
                const float crest = std::sin(u * PI_F * 3.1f) * sh * 0.12f;
                const float cy = clampY(sh * 0.56f + crest, ry);
                // Cool valley hollow
                softPlume(c.g, cx, clampY(cy + ry * 0.22f, ry), rx * 1.08f, ry * 0.96f, 0.f, ry * 0.20f, F.fog.deep, F.fog.mid, 0.44f * env);
                // Billowing translucent mist swell
                volPuff(c.g, cx, cy, rx * 0.94f, ry * 0.86f, -rx * 0.14f, -ry * 0.32f, F.fog.light, F.fog.mid, 0.46f * env);
                // Delicate rising upper wisp plume
                if ((m % 2 == 0) && env > 0.42f) {
                    const float wrx = rx * 0.62f, wry = ry * 0.58f;
                    const float wx = clampX(cx + (hashf(m, 624) - 0.35f) * rx * 0.5f, wrx);
                    const float wy = clampY(cy - ry * 0.42f, wry);
                    softPlume(c.g, wx, wy, wrx, wry, -wrx * 0.15f, -wry * 0.25f, F.fog.glow, F.fog.light, 0.32f * env);
                }
            }
        } else {
            // Lush low ground mist shelf: dense rolling strata anchoring the lower third
            const int n = 13;
            for (int m = 0; m < n; ++m) {
                const float u = (m + 0.5f) / n;
                const float env = std::pow(std::sin(u * PI_F), 0.62f);
                const float rx = sw * (0.11f + 0.09f * env * (0.85f + 0.30f * hashf(m, 631)));
                const float ry = sh * (0.20f + 0.16f * env * (0.84f + 0.32f * hashf(m, 632)));
                const float cx = clampX(sw * (0.06f + 0.88f * u), rx);
                const float cy = clampY(sh * (0.58f + 0.10f * std::sin(u * PI_F * 2.7f + 1.2f)), ry);
                softPlume(c.g, cx, clampY(cy + ry * 0.18f, ry), rx * 1.10f, ry * 0.98f, 0.f, ry * 0.16f, F.fog.deep, F.fog.mid, 0.48f * env);
                volPuff(c.g, cx, cy - ry * 0.06f, rx * 0.96f, ry * 0.82f, 0.f, -ry * 0.34f, F.fog.light, F.fog.mid, 0.54f * env);
                softPlume(c.g, cx, clampY(cy - ry * 0.32f, ry * 0.42f), rx * 0.72f, ry * 0.38f, 0.f, -ry * 0.12f, F.fog.glow, F.fog.light, 0.30f * env);
            }
        }
        return c.g.endSprite();
    });
}

// where cloud number i is right now: centre (x, y), width, depth layer, and how visible it is at the screen edges. drawClouds and the lightning both use it,
// so a bolt can leave from a cloud you can see.
struct CloudAt { float x, y, cw, vis; int layer; };
CloudAt cloudAt(const Ctx& c, int i, float heavy) {
    const App& a = c.a; const float s = c.s, W = c.W, H = c.H;
    CloudAt r; r.layer = i % 3;                                          // 0 far/high cirrus, 1 mid cumulus, 2 near stratocumulus
    const float depth = 0.50f + 0.25f * r.layer;
    // Positions are worked out in design units (a 1440-wide window) from the cloud clock, then stretched to the real window:
    // resizing only stretches the sky, it never makes a cloud jump. The wind speeds the clock up, never this number.
    const float baseW = (r.layer == 0) ? (380.f + 340.f * hashf(i, 3))
                      : (r.layer == 1) ? (320.f + 360.f * hashf(i, 3))
                                       : (420.f + 400.f * hashf(i, 3));
    const float cwD = baseW * (0.84f + 0.26f * r.layer);
    r.cw = cwD * s;
    const double spanD = 1440.0 + 2.0 * cwD;
    const double posD = loopPos(a.fxCloudT, 8.6 * depth * (0.72 + 0.56 * hashf(i, 4)), hashf(i, 1) * spanD, spanD);
    r.x = (float)(posD / spanD) * (W + 2.f * r.cw) - r.cw;
    float yFrac;
    if (r.layer == 0) {
        // High-altitude cirrus / upper veil sits gracefully in the upper third
        yFrac = 0.06f + 0.28f * hashf(i, 2);
    } else if (r.layer == 1) {
        // Mid-altitude sculpted cumulus decks across the upper/mid sky
        yFrac = heavy > 0.5f ? (0.10f + 0.48f * hashf(i, 2)) : (0.13f + 0.62f * hashf(i, 2));
    } else {
        // Near coastal / storm stratocumulus framing upper and lower sky bands with breathing room
        const float h2 = hashf(i, 2);
        yFrac = heavy > 0.5f ? (0.08f + 0.58f * h2) : (h2 < 0.55f ? (0.10f + 0.28f * (h2 / 0.55f)) : (0.64f + 0.22f * ((h2 - 0.55f) / 0.45f)));
    }
    r.y = H * yFrac + std::sin(ph(a.now, 0.095) + i * 1.3f) * (6.f + 3.f * r.layer) * s;
    const float edge = smoothstep(0.f, 0.14f * W, r.x + r.cw * 0.5f) * (1.f - smoothstep(W - 0.14f * W, W, r.x + r.cw * 0.5f));
    r.vis = 0.28f + 0.72f * edge;
    return r;
}

// ---------------------------------------------------------------- clouds (behind the clock)
void drawClouds(Ctx& c, const Frame& F, float k, float heavy) {
    Gfx& g = c.g; App& a = c.a; const float s = c.s, W = c.W, H = c.H;
    const double t = a.now;
    const float gs = std::min(gFx, 1.6f);
    const int n = (int)std::lround(5 + 7 * k + 2 * heavy);
    // Subtle cohesive atmospheric sky-light veil near the upper sky so clouds feel seated in the air
    {
        const float va = clamp01(al((c.P.dark ? 0.08f : 0.10f) * k * (0.7f + 0.3f * heavy)));
        g.vGradientRect(0.f, 0.f, W, H * 0.36f, withA(F.T.shade, va), withA(F.T.shade, 0.f));
    }
    if (heavy > 0.01f && ceilingReady(c, F)) {
        const float sway = std::sin(ph(t, 0.045)) * 28.f * s;
        g.drawBitmapRect(gCeiling.bmp, sway - 46.f * s, -0.04f * H, W + 92.f * s, gCeilH, clamp01(0.42f * heavy * k * gs));
    }
    // Strictly back-to-front depth sorting: high cirrus (0) -> mid cumulus (1) -> near stratocumulus shelves (2)
    for (int L = 0; L < 3; ++L) {
        for (int i = L; i < n; i += 3) {
            const CloudAt ca = cloudAt(c, i, heavy);
            const float cw = ca.cw, x = ca.x, y = ca.y;
            const float f = k * ca.vis * (1.f + 0.24f * heavy);
            const int j = L * 2 + ((i / 3) & 1);
            if (cloudReady(c, F, j)) {
                const Bitmap& bm = gCloud[j].bmp;
                const float sc = cw / std::max(1.f, gCloud[j].nominal);
                const float bw = gCloud[j].nominal * (L == 0 ? 1.88f : (L == 1 ? 1.72f : 1.92f)) * sc;
                const float bh = gCloud[j].nominal * (L == 0 ? 0.68f : (L == 1 ? 0.96f : 0.78f)) * sc;
                const float anchorY = (L == 0) ? 0.50f : (L == 1 ? 0.54f : 0.52f);
                const float layerBase = (L == 0) ? 0.44f : (L == 1 ? 0.58f : 0.62f);
                const float nightBoost = 1.f + 0.24f * c.a.palTarget.nightness;
                const float lightTrim = c.a.palTarget.dark ? 1.f : 0.96f;
                g.drawBitmapRect(bm, x + cw * 0.5f - bw * 0.5f, y - bh * anchorY, bw, bh,
                                 clamp01(layerBase * f * gs * nightBoost * lightTrim));
            } else if (i < 6 && !gCloud[j].bmp.bmp) {                        // fallback before first sprite finishes building
                fallbackCloudPuff(g, x + cw * 0.5f, y, cw, F.T, clamp01(0.42f * f));
            }
        }
    }
}

// Shared painterly ribbon path helpers (used by Fog tendrils, Wind, Blizzard spindrift and Gale)
constexpr int WPTS = 34, WCURL = 15;                                       // points along a stroke's whole path, and how many of them make its curl
float easeOut3(float x) { x = clamp01(x); const float r = 1.f - x; return 1.f - r * r * r; }
float easeInOut(float x) { x = clamp01(x); return x * x * (3.f - 2.f * x); }

struct WindPath { Pt p[WPTS]; float cl[WPTS]; int n = 0; };
void windPath(WindPath& P, float x0, float y0, float len, float amp, float wl, float phase, float bow, float curlR, float turns, float curlDir) {
    const int nw = curlR > 0.f ? WPTS - WCURL : 22;
    const float inv = 1.f / (float)(nw - 1);
    const float kw = 2.f * PI_F / std::max(16.f, wl);
    for (int j = 0; j < nw; ++j) {
        const float u = j * inv;
        const float x = u * len;
        P.p[j] = {x0 + x, y0 + amp * (0.35f + 0.65f * u) * std::sin(x * kw + phase) + bow * u * u};
    }
    P.n = nw;
    if (curlR > 0.f) {                                                      // the curl carries on from the wave's last direction and tightens as it turns
        float ang = std::atan2(P.p[nw - 1].y - P.p[nw - 2].y, P.p[nw - 1].x - P.p[nw - 2].x);
        const float dphi = turns * 2.f * PI_F / WCURL;
        Pt q = P.p[nw - 1];
        for (int j = 0; j < WCURL; ++j) {
            const float r = curlR * (1.f - 0.58f * (j + 0.5f) / WCURL);
            const float am = ang + curlDir * dphi * 0.5f;
            q.x += r * dphi * std::cos(am); q.y += r * dphi * std::sin(am);
            ang += curlDir * dphi;
            P.p[P.n++] = q;
        }
    }
    P.cl[0] = 0.f;
    for (int j = 1; j < P.n; ++j) P.cl[j] = P.cl[j - 1] + std::hypot(P.p[j].x - P.p[j - 1].x, P.p[j].y - P.p[j - 1].y);
}

int windSlice(const WindPath& P, float a, float b, Pt* out, float* v) {
    if (P.n < 2) return 0;
    const float L = P.cl[P.n - 1];
    const float da = a * L, db = b * L;
    if (db - da < 1.f) return 0;
    auto at = [&](float d) {
        int k = 0; while (k < P.n - 2 && P.cl[k + 1] < d) ++k;
        const float seg = P.cl[k + 1] - P.cl[k], f = seg > 1e-4f ? clamp01((d - P.cl[k]) / seg) : 0.f;
        return Pt{P.p[k].x + (P.p[k + 1].x - P.p[k].x) * f, P.p[k].y + (P.p[k + 1].y - P.p[k].y) * f};
    };
    int m = 0;
    out[m] = at(da); v[m++] = 0.f;
    for (int j = 0; j < P.n; ++j) if (P.cl[j] > da + 0.5f && P.cl[j] < db - 0.5f) { out[m] = P.p[j]; v[m++] = (P.cl[j] - da) / (db - da); }
    out[m] = at(db); v[m++] = 1.f;
    return m;
}

void windRibbon(Gfx& g, const Pt* p, const float* v, int n, float w, const Color& col, std::vector<Pt>& poly) {
    if (n < 2 || col.a < 0.004f) return;
    if (n == 2) { g.line(p[0].x, p[0].y, p[1].x, p[1].y, withA(col, col.a * 0.55f), std::max(0.8f, w * 0.35f), true); return; }
    poly.resize((size_t)(2 * n - 2));
    poly[0] = p[0];
    poly[n - 1] = p[n - 1];
    for (int j = 1; j < n - 1; ++j) {
        float tx = p[j + 1].x - p[j - 1].x, ty = p[j + 1].y - p[j - 1].y;
        const float l = std::sqrt(tx * tx + ty * ty);
        if (l > 1e-4f) { tx /= l; ty /= l; } else { tx = 1.f; ty = 0.f; }
        const float hw = 0.5f * w * std::pow(std::sin(PI_F * std::pow(clamp01(v[j]), 1.45f)), 0.8f);
        poly[j] = {p[j].x - ty * hw, p[j].y + tx * hw};
        poly[2 * n - 2 - j] = {p[j].x + ty * hw, p[j].y - tx * hw};
    }
    g.fillPolygon(poly, col);
}

bool fogGlowReady(Ctx& c, const Frame& F) {
    const float W = c.W, H = c.H;
    uint64_t key = mixKey(mixKey(q3(F.fog.glow), qsz(W)), mixKey(qsz(H), 7390u));
    return ensure(c.g, gFogGlow, key, 0.f, [&] {
        // Quarter-resolution soft atmospheric light diffusion sprite (eliminates expensive per-frame giant radial gradient)
        constexpr float QS = 0.25f;
        const int sw = std::max(32, (int)(W * QS)), sh = std::max(24, (int)(H * 0.68f * QS));
        if (!c.g.beginSprite(sw, sh)) return Bitmap{};
        c.g.glowEllipse(sw * 0.46f, sh * 0.32f, sw * 0.62f, sh * 0.62f, F.fog.glow, 1.0f);
        return c.g.endSprite();
    });
}

// ---------------------------------------------------------------- fog
// Intentional cinematic valley mist:
//   1. Cached diffused upper-sky atmospheric light bloom + subtle horizon-to-valley depth veil
//   2. Four parallax strata of sculpted 4-tone rolling mist banks (cool troughs beneath luminous crests) with gentle counter-drift
//   3. Slow velvety gossamer mist tendrils rising quietly across the mid-lower fog floor
//   4. Delicate hovering dew-light micro-motes catching the diffused light
void drawFog(Ctx& c, const Frame& F, float k, float q = 1.f) {
    Gfx& g = c.g; App& a = c.a; const float s = c.s, W = c.W, H = c.H;
    if (W < 4.f || H < 4.f) return;
    const double t = a.now;
    const float gs = std::min(gFx, 1.6f);
    const bool dark = c.P.dark;

    // 1. Diffused atmospheric light bloom scattering softly through the upper fog + valley depth gradient
    {
        if (k >= 0.35f && fogGlowReady(c, F)) {
            const float glowBreathe = 0.88f + 0.12f * std::sin(ph(t, 0.08));
            g.drawBitmapRect(gFogGlow.bmp, 0.f, 0.f, W, H * 0.68f, clamp01(al((dark ? 0.24f : 0.28f) * k * glowBreathe)));
        }
        g.vGradientRect(0.f, 0.f, W, H * 0.32f, withA(F.fog.deep, al((dark ? 0.18f : 0.14f) * k)), withA(F.fog.deep, 0.f));
        g.vGradientRect(0.f, H * 0.28f, W, H * 0.72f, withA(F.fog.mid, 0.f), withA(F.fog.mid, al((dark ? 0.24f : 0.22f) * k)));
        g.vGradientRect(0.f, H * 0.68f, W, H * 0.32f, withA(F.fog.light, 0.f), withA(F.fog.light, al((dark ? 0.22f : 0.26f) * k)));
    }

    // Light ambient mist (e.g. Drizzle/Sleet background haze): lightweight single low mist bank without heavy multi-stratum overdraw
    if (k < 0.35f) {
        if (fogReady(c, F, 2)) {
            const Bitmap& bm = gFog[2].bmp;
            const float bw = bm.w / SPR * 1.05f, bh = bm.h / SPR * 0.92f;
            const double spanD = 1440.0 + bw / std::max(0.1f, s);
            const float yc = H * 0.86f + std::sin(ph(t, 0.05)) * 0.012f * H;
            const float op = clamp01(0.46f * k * gs);
            for (int cp = 0; cp < 2; ++cp) {
                const float x = (float)(loopPos(a.fxFogT, 5.2, 0.15 * spanD + cp * spanD * 0.5, spanD) / spanD) * (W + bw) - bw;
                if (x + bw >= 0.f && x <= W) g.drawBitmapRect(bm, x, yc - bh * 0.5f, bw, bh, op);
            }
        }
        return;
    }

    // 2. Four sculpted parallax mist strata with subtle counter-drift & thermal vertical breathing
    static const int rowSlot[4] = {0, 1, 1, 2};
    static const float rowY[4] = {0.28f, 0.48f, 0.69f, 0.89f};
    static const float rowDir[4] = {1.0f, -0.38f, 0.92f, 1.34f};          // subtle thermal counter-eddy on stratum 1
    static const float rowOp[4] = {0.30f, 0.38f, 0.48f, 0.60f};
    for (int row = 0; row < 4; ++row) {
        const int j = rowSlot[row];
        if (!fogReady(c, F, j)) continue;
        const Bitmap& bm = gFog[j].bmp;
        const float scX = 0.94f + 0.10f * row;
        const float scY = 0.92f + 0.12f * row;
        const float bw = bm.w / SPR * scX, bh = bm.h / SPR * scY;
        const double spanD = 1440.0 + bw / std::max(0.1f, s);
        const double speedD = 4.2 * rowDir[row] * (0.85 + 0.30 * hashf(row, 120));
        const float yc = H * rowY[row] + std::sin(ph(t, 0.042 + 0.010 * row, row * 1.9)) * 0.016f * H;
        const float breathe = 0.86f + 0.14f * std::sin(ph(t, 0.055 + 0.012 * row, row * 2.4));
        const float op = clamp01(rowOp[row] * k * breathe * gs * (dark ? 0.96f : 0.92f));
        for (int cp = 0; cp < 2; ++cp) {
            const float x = (float)(loopPos(a.fxFogT, speedD, hashf(row, 121) * spanD + cp * spanD * 0.5, spanD) / spanD) * (W + bw) - bw;
            if (x + bw < 0.f || x > W) continue;
            g.drawBitmapRect(bm, x, yc - bh * 0.5f, bw, bh, op);
        }
    }

    // 3. Slow curling gossamer mist tendrils drifting gracefully through the mid-lower fog
    {
        const double ft = std::fabs(a.fxFogT) + t * 0.15;
        const bool left = a.fxFogT < 0.0;
        auto mx = [&](float x) { return left ? W - x : x; };
        Pt pts[36]; float vv[36];
        WindPath P;
        static std::vector<Pt> fPoly;
        if (fPoly.capacity() < 72) fPoly.reserve(72);
        const int nt = std::max(2, (int)std::lround((3.f + 3.f * k) * q));
        for (int i = 0; i < nt; ++i) {
            const double per = 12.0 + 7.5 * hashf(i, 641);
            const double tt = ft / per + hashf(i, 642);
            const double cyc = std::floor(tt);
            const float u = (float)(tt - cyc);
            const int sd = i * 5413 + (int)std::fmod(cyc, 65521.0);
            const float len = W * (0.28f + 0.24f * hashf(sd, 643));
            const float x0 = W * (-0.04f + 0.82f * hashf(sd, 644)) - 0.35f * len + u * 0.09f * W;
            const float y0 = H * (0.44f + 0.46f * hashf(sd, 645));
            const float amp = (12.f + 22.f * hashf(sd, 646)) * s;
            const float wl = (340.f + 280.f * hashf(sd, 647)) * s;
            const float phase = hashf(sd, 648) * 6.2832f - 0.9f * u;
            const float bow = -(0.025f + 0.042f * hashf(sd, 649)) * H;                 // drifts gently upward like rising valley mist
            const float curlR = (30.f + 22.f * hashf(sd, 650)) * s;
            const float turns = 0.44f + 0.22f * hashf(sd, 651);
            windPath(P, x0, y0, len, amp, wl, phase, bow, curlR, turns, -1.f);
            const float head = easeOut3(u / 0.66f), tail = easeInOut((u - 0.26f) / 0.74f);
            const int m = windSlice(P, tail, head, pts, vv);
            if (m < 2) continue;
            for (int j = 0; j < m; ++j) pts[j].x = mx(pts[j].x);
            const float life = smoothstep(0.f, 0.16f, u) * (1.f - smoothstep(0.78f, 1.f, u));
            const float wa = (dark ? 0.095f : 0.115f) * k * life * (0.75f + 0.25f * hashf(sd, 652));
            if (wa < 0.004f) continue;
            const float w = (24.f + 20.f * hashf(sd, 653)) * s;
            // Broad, velvety, feathered vapor layers (soft steam-curl feel, zero crisp needle edge)
            windRibbon(g, pts, vv, m, w * 2.10f, withA(F.fog.mid, al(wa * 0.36f)), fPoly);
            windRibbon(g, pts, vv, m, w * 1.25f, withA(F.fog.light, al(wa * 0.56f)), fPoly);
            windRibbon(g, pts, vv, m, w * 0.62f, withA(F.fog.glow, al(wa * 0.44f)), fPoly);
        }
    }

    // 4. Delicate floating dew-light micro-motes suspended lazily in the illuminated mist
    {
        const int nm = std::max(6, (int)std::lround((10.f + 12.f * k) * q));
        for (int i = 0; i < nm; ++i) {
            const float h1 = hashf(i, 661), h2 = hashf(i, 662), h3 = hashf(i, 663), h4 = hashf(i, 664);
            const double spanX = 1440.0 + 160.0;
            const double posX = loopPos(a.fxFogT + t * 0.35, 10.0 + 14.0 * h1, h2 * spanX, spanX);
            const float px = (float)(posX / spanX) * (W + 140.f * s) - 70.f * s;
            const float py = H * (0.16f + 0.74f * h3) + std::sin(ph(t, 0.22 + 0.18 * h4, h1 * 6.28)) * 18.f * s;
            const float vis = smoothstep(0.f, 0.08f * W, px) * (1.f - smoothstep(0.92f * W, W, px));
            if (vis < 0.02f) continue;
            const float tw = 0.50f + 0.50f * std::sin(ph(t, 0.55 + 0.65 * h4, h2 * 6.28));
            const float ma = al((dark ? 0.24f : 0.28f) * k * vis * tw * (0.55f + 0.45f * h1));
            if (ma < 0.006f) continue;
            const float r = std::max(0.9f, (1.1f + 1.2f * h4) * s);
            g.glow(px, py, r * 4.2f, F.fog.glow, ma * 0.42f);
            g.fillCircle(px, py, r, withA(F.fog.glow, ma));
        }
    }
}

// ---------------------------------------------------------------- the art style of the rain and the wind
// Rain is drawn from small cached images (sprites): every drop and splash is one image copied to the screen, so it stays light enough for 60 frames a
// second (or whatever your screen refreshes at).
//  - Three depths: far (small, faint, slow), middle, near (large, bright, fast). Near things are bigger, brighter and quicker.
//  - Rain ends in a splash on a ground line. Images are placed on whole pixels, so thin lines stay sharp however they move.
// The wind is drawn like a painter draws a breeze (see below): a few calm brush strokes, not a swarm of streaks.
// Like the rest of this file it is a pure function of the time: a drop's place and the age of its splash come from the clock alone.
// To change the look: the tables right below each heading (how many, how fast, how bright, how big).

// ---------------------------------------------------------------- wind
// A few long, thin brush strokes. Each one writes itself across the sky along a lazy wave, often ending in a small curl, then wipes itself away from
// its tail and a new one starts somewhere else. A few leaves tumble along with it, and a little dust drifts by.
// Cost: nothing is cached, so a theme or window change never allocates or rebuilds offscreen sprites. Far strokes use a single tapered polygon for
// atmospheric depth; nearer strokes use two (sheath and bright spine), so a breeze is a handful of light polygons and even a gale stays under ~15
// small polygons per frame. Scales down smoothly with fxQuality.
// Everything comes from the wind clock a.fxWindT (it only counts up, faster in a stronger wind), so any frame can be drawn at any moment.
// To change the look: the tables below, and the numbers marked "look" in drawWind.
const float kWDepth[3] = {0.72f, 0.88f, 1.06f};                            // look: size of a stroke at each depth (far, middle, near)
const float kWAlpha[3] = {0.40f, 0.60f, 0.82f};                            // look: brightness
const float kWWidth[3] = {1.45f, 2.05f, 2.75f};                            // look: the thickest part of a stroke (design px)

float gWindFlip = 1.f;          // the wind's side (+1 right, -1 left), eased: a change of direction fades the strokes out and back in, never flips them

void drawWind(Ctx& c, const Frame& F, float k, float q) {
    Gfx& g = c.g; App& a = c.a; const float s = c.s, W = c.W, H = c.H;
    if (W < 4.f || H < 4.f) return;
    const double T = std::max(0.0, a.fxWindT), t = a.now;
    const float fade = smoothstep(0.15f, 0.9f, std::fabs(gWindFlip));      // fading through a change of direction
    if (fade < 0.01f) return;
    const bool left = gWindFlip < 0.f;
    auto mx = [&](float x) { return left ? W - x : x; };                    // everything is worked out blowing right, then mirrored
    const float gust = 0.5f + 0.5f * std::sin(ph(t, 0.42) + 0.8f * std::sin(ph(t, 0.17)));      // slow surges

    // the strokes
    Pt pts[WPTS + 2]; float vv[WPTS + 2];
    WindPath P;
    static std::vector<Pt> poly;
    if (poly.capacity() < (size_t)(WPTS * 2)) poly.reserve((size_t)(WPTS * 2));
    const int n = std::max(1, (int)std::lround((3.f + 7.f * k) * q));       // look: how many strokes at once (3 in a breeze, 10 in a gale)
    for (int i = 0; i < n; ++i) {
        const int L = i % 3; const float d = kWDepth[L];
        const double per = 3.8 + 2.4 * hashf(i, 201);                       // look: how long one stroke lives (wind-clock seconds)
        const double tt = T / per + hashf(i, 202);
        const double cyc = std::floor(tt);
        const float u = (float)(tt - cyc);                                  // 0..1 through this stroke's life
        const int sd = i * 7919 + (int)std::fmod(cyc, 65521.0);             // a new stroke every life, somewhere else
        if (i > 0 && hashf(sd, 203) > 0.44f + 0.56f * k) continue;          // a light breeze leaves gaps: calmer
        const float len = W * (0.20f + 0.24f * hashf(sd, 204)) * d * (0.85f + 0.30f * k);
        const float x0 = W * (-0.06f + 0.92f * hashf(sd, 205)) - 0.45f * len + u * 0.07f * W * d;    // slides downwind as it plays
        const float y0 = H * (0.08f + 0.84f * hashf(sd, 206));
        const float amp = (7.f + 19.f * hashf(sd, 207)) * s * d;
        const float wl = (240.f + 300.f * hashf(sd, 208)) * s;
        const float phase = hashf(sd, 209) * 6.2832f - 1.35f * u;            // a gentle ripple runs along the stroke while it is drawn
        const float bow = (hashf(sd, 210) - 0.5f) * 0.085f * H;
        const bool curl = hashf(sd, 211) < 0.56f;
        const float curlR = curl ? (15.f + 17.f * hashf(sd, 212)) * s * d : 0.f;
        const float turns = 0.78f + 0.28f * hashf(sd, 213);
        const float curlDir = hashf(sd, 214) < 0.75f ? -1.f : 1.f;          // most curls roll upward
        windPath(P, x0, y0, len, amp, wl, phase, bow, curlR, turns, curlDir);
        const float head = easeOut3(u / 0.62f), tail = easeInOut((u - 0.30f) / 0.70f);   // it writes itself in, then wipes away from the tail
        const int m = windSlice(P, tail, head, pts, vv);
        if (m < 2) continue;
        for (int j = 0; j < m; ++j) pts[j].x = mx(pts[j].x);
        const float life = smoothstep(0.f, 0.08f, u) * (1.f - smoothstep(0.86f, 1.f, u));
        const float slotFade = i < 3 ? 1.f : smoothstep((i - 3) / 7.5f, (i - 3) / 7.5f + 0.14f, k);
        const float al0 = kWAlpha[L] * k * life * fade * slotFade * (0.78f + 0.28f * gust) * (0.75f + 0.25f * hashf(sd, 215));
        if (al0 < 0.006f) continue;
        const float w = std::max(1.f, kWWidth[L] * s * (0.85f + 0.30f * k));
        if (L == 0 || (L == 1 && q < 0.55f)) {
            windRibbon(g, pts, vv, m, w, withA(lerp(F.wind, F.windHead, 0.35f), al(0.75f * al0)), poly);
        } else {
            windRibbon(g, pts, vv, m, w, withA(F.wind, al(0.55f * al0)), poly);
            windRibbon(g, pts, vv, m, w * 0.42f, withA(F.windHead, al(0.90f * al0)), poly);
        }
        const float tip = 1.f - smoothstep(0.48f, 0.62f, u);                // the brush tip glints while it is still writing
        if (tip > 0.01f) {
            const float ta = al(0.75f * al0 * tip);
            if (L == 2 && q > 0.65f) g.glow(pts[m - 1].x, pts[m - 1].y, w * 3.2f, F.windHead, ta * 0.24f);
            g.fillCircle(pts[m - 1].x, pts[m - 1].y, w * 0.48f, withA(F.windHead, ta));
        }
    }

    // leaves: a few tumbling across, fluttering (they turn edge-on and back) and looping on small eddies (suppressed during snowfall & blizzards)
    const float leafMask = 1.f - clamp01(std::max(a.qfx[6], a.qfx[7]) * 2.f);
    const int nl = std::max(0, (int)std::lround(5.f * k * q * leafMask));    // look: how many leaves (1 in a light breeze, 4-5 in a gale, 0 in snow/blizzard)
    static std::vector<Pt> leaf(8);
    for (int i = 0; i < nl; ++i) {
        const double per = 8.5 + 5.0 * hashf(i, 231);                       // look: wind-clock seconds to cross the screen
        const double tt = T / per + hashf(i, 232);
        const double cyc = std::floor(tt);
        const float u = (float)(tt - cyc);
        const int sd = i * 104729 + (int)std::fmod(cyc, 65521.0);
        const float d = 0.75f + 0.45f * hashf(sd, 233);
        const float mg = 40.f * s;
        const float e = hashf(sd, 234) * 6.2832f;
        float x = -mg + u * (W + 2.f * mg) + 16.f * s * d * std::sin(2.f * PI_F * 2.f * u + e);
        float y = H * (0.12f + 0.70f * hashf(sd, 235)) + 0.10f * H * u
                + (20.f + 38.f * hashf(sd, 236)) * s * d * std::sin(2.f * PI_F * (1.1f + 0.8f * hashf(sd, 237)) * u + e)
                + 16.f * s * d * std::cos(2.f * PI_F * 2.f * u + e);
        const float vis = smoothstep(-mg, 0.04f * W, x) * (1.f - smoothstep(0.96f * W, W + mg, x));
        if (vis < 0.02f) continue;
        const float leafFade = smoothstep(i * 0.16f, i * 0.16f + 0.18f, k);
        const float la = al((0.36f + 0.26f * d) * k * fade * vis * leafFade);
        if (la < 0.006f) continue;
        x = mx(x);
        const float flip = std::cos(ph(t, 2.0 + 1.6 * hashf(sd, 238), e));
        const float spin = (u * (180.f + 300.f * hashf(sd, 239)) * (hashf(sd, 240) < 0.5f ? 1.f : -1.f)
                         + 32.f * std::sin(ph(t, 2.3, e * 1.7))) * (PI_F / 180.f) * (left ? -1.f : 1.f);
        const float ln = (7.f + 5.f * hashf(sd, 241)) * s * d;
        const float wd = ln * 0.40f * (0.16f + 0.84f * std::fabs(flip));
        const float cs = std::cos(spin), sn = std::sin(spin);
        for (int j = 0; j < 8; ++j) {                                       // an almond: the top edge out, the bottom edge back
            const int jj = j < 5 ? j : 8 - j;
            const float lx = (jj / 4.f - 0.5f) * ln;
            const float ly = (j < 5 ? -1.f : 1.f) * wd * std::pow(std::max(0.f, std::sin(PI_F * jj / 4.f)), 0.9f);
            leaf[j] = {x + lx * cs - ly * sn, y + lx * sn + ly * cs};
        }
        const Color lc = lerp(F.leaf, F.windHead, 0.30f * smoothstep(-0.25f, 0.25f, flip));    // two-toned: catches light as it turns
        g.fillPolygon(leaf, withA(lc, la));
    }

    // dust: tiny soft specks drifting on the flow
    const int nm = (int)std::lround((3.f + 7.f * k) * q);                   // look: how many specks
    const float invS = 1.f / std::max(0.1f, s);
    for (int i = 0; i < nm; ++i) {
        const double spanD = 1440.0 + 120.0;
        const double posD = loopPos(T, 120.0 * (0.6 + 0.8 * hashf(i, 71)), hashf(i, 72) * spanD, spanD);
        const float x = (float)(posD / spanD) * (W + 120.f * s) - 60.f * s;
        const float y = H * (0.06f + 0.88f * hashf(i, 73)) + std::sin(ph(T, 0.45 + 0.6 * hashf(i, 74), hashf(i, 75) * 6.28) + (x * invS) * 0.003f) * (12.f + 26.f * hashf(i, 76)) * s;
        const float vis = smoothstep(-0.01f * W, 0.07f * W, x) * (1.f - smoothstep(0.93f * W, 1.01f * W, x));
        if (vis < 0.02f) continue;
        const float tw = 0.65f + 0.35f * std::sin(ph(t, 1.3 + hashf(i, 77), hashf(i, 79) * 6.28));      // a faint twinkle
        const float ma = al((0.22f + 0.24f * hashf(i, 80)) * k * vis * tw * fade);
        if (ma < 0.005f) continue;
        g.fillCircle(mx(x), y, std::max(0.9f, (0.9f + 0.8f * hashf(i, 78)) * s), withA(F.windHead, ma));
    }
}

// ---------------------------------------------------------------- rain
// Three depths of drops. Each depth has its own ground line, the far drops landing higher up the screen and the near ones lower down, so together they
// make a wet ground that runs away from you. Every drop that reaches its ground line splashes there: a ring that opens out, a second fainter ring, a flash
// of impact with a short spout, and a small crown of droplets thrown up and falling back.
constexpr int SPL_FR = 24, SPL_VAR = 2, SPL_COLS = 8;                       // a splash is 24 pictures; two variants of it per depth, so they are not all alike
constexpr float SPLASH = 0.52f;                                             // seconds a splash lasts
const float kGround[3] = {0.80f, 0.885f, 0.972f};                           // far, middle, near: where each depth meets the ground (fraction of the height)
const float kDepth[3] = {0.62f, 0.84f, 1.08f};                              // look: size
const float kAlpha[3] = {0.34f, 0.52f, 0.74f};                              // look: brightness
const float kWidth[3] = {1.0f, 1.5f, 2.1f};                                 // look: thickness of a drop
const float kStreak[3] = {24.f, 39.f, 55.f};                                // look: the three lengths of a drop (design px, times the depth)

struct StrkGeo { float wd, len[3], padX, padY, cw, ch, rowY; };
// the cells of the drop image: for each depth a row of three lengths, the head of the drop at the bottom of its cell
void strkGeo(float s, float slant, StrkGeo out[3], float& atlasW, float& atlasH) {
    float y = 0; atlasW = 0;
    for (int L = 0; L < 3; ++L) {
        StrkGeo& G = out[L]; const float d = kDepth[L];
        G.wd = std::max(1.f, 0.85f * kWidth[L] * s);
        for (int v = 0; v < 3; ++v) G.len[v] = std::floor(kStreak[v] * s * d);
        G.padX = std::ceil(G.wd) + 3.f; G.padY = std::ceil(G.wd) + 3.f;
        G.cw = std::ceil(2.f * G.padX + std::fabs(slant) * G.len[2]);
        G.ch = std::ceil(G.len[2] + 2.f * G.padY);
        G.rowY = y; y += G.ch; atlasW = std::max(atlasW, G.cw * 3.f);
    }
    atlasH = y;
}
struct SplGeo { float cw, ch, ax, ay; };
SplGeo splGeo(int L, float s) {                                              // one cell of a splash sheet; (ax, ay) is where the drop lands inside it
    const float d = kDepth[L]; SplGeo G;
    G.cw = std::ceil(2.f * (62.f * s * d + 8.f));
    const float up = std::ceil(82.f * s * d) + 4.f, down = std::ceil(19.f * s * d) + 4.f;
    G.ch = up + down; G.ax = G.cw * 0.5f; G.ay = up;
    return G;
}

Slot gRainStreak, gRainSplash[3];
float gStrkS = 0, gStrkSlant = 0, gSplS[3] = {0, 0, 0};                    // what the rain images were last drawn for (the sheets are rebuilt a couple a frame)

Bitmap buildStreaks(Gfx& g, const Frame& F, float s, float slant) {
    StrkGeo SG[3]; float aw, ah; strkGeo(s, slant, SG, aw, ah);
    if (aw < 4 || ah < 4 || aw > 4096 || ah > 4096 || !g.beginSprite((int)aw, (int)ah)) return Bitmap{};
    const Color C = F.rain;
    for (int L = 0; L < 3; ++L) {
        const StrkGeo& G = SG[L];
        for (int v = 0; v < 3; ++v) {
            g.setOffset(v * G.cw, G.rowY);
            const float len = G.len[v], dx = slant * len, w2 = G.wd * 0.5f;
            const float hy = G.ch - G.padY, hx = dx >= 0 ? G.padX + dx : G.padX, ty = hy - len, tx = hx - dx;
            // the body fades from nothing at the tail to nearly full at the head: two overlapping parts, the lower one adds to the brightness near the head
            g.fillPolyVGradient({{tx - w2, ty}, {tx + w2, ty}, {hx + w2, hy}, {hx - w2, hy}}, ty, hy, withA(C, 0.f), withA(C, 0.75f));
            const float y1 = ty + len * 0.52f, x1 = tx + dx * 0.52f;
            g.fillPolyVGradient({{x1 - w2, y1}, {x1 + w2, y1}, {hx + w2, hy}, {hx - w2, hy}}, y1, hy, withA(C, 0.f), withA(C, 0.55f));
            g.fillCircle(hx, hy - G.wd * 0.2f, G.wd * 0.85f, withA(C, 1.f));            // the bright head
        }
    }
    g.setOffset(0, 0);
    return g.endSprite();
}

// one frame of a splash: the age u runs 0..1 over the life of the splash
void splashFrame(Gfx& g, const Color& C, int L, int V, float u, float s, float side, const SplGeo& G) {
    const float d = kDepth[L], fade = 1.f - u, et = u * SPLASH, xi = G.ax, yg = G.ay;
    const float jit = V == 0 ? 0.88f : 1.14f;
    const float rr = (3.f + 40.f * std::pow(u, 0.62f)) * s * d * jit;
    g.strokeEllipse(xi, yg, rr, rr * 0.30f, withA(C, (0.30f + 0.55f * kAlpha[L]) * std::pow(fade, 1.3f)), std::max(1.2f, 1.6f * s * d));
    if (u > 0.10f && u < 0.75f) {                                              // a second, fainter ring following the first
        const float u2 = (u - 0.10f) / 0.65f, r2 = (2.f + 22.f * std::pow(u2, 0.7f)) * s * d;
        g.strokeEllipse(xi, yg, r2, r2 * 0.30f, withA(C, 0.50f * kAlpha[L] * (1.f - u2)), std::max(1.f, 1.2f * s * d));
    }
    if (u < 0.22f) {                                                           // the flash where the drop hits, and a short spout straight up
        const float fl = 1.f - u / 0.22f;
        g.fillCircle(xi, yg, (2.0f + 2.2f * fl) * s * d, withA(C, 0.95f * fl));
        g.line(xi, yg, xi, yg - (6.f + 20.f * (1.f - fl)) * s * d * (0.5f + 0.5f * fl), withA(C, 0.80f * fl), std::max(1.4f, 1.7f * s * d), true);
    }
    const int nc = L == 0 ? 3 : (L == 1 ? 5 : 7);                              // the crown of droplets thrown up
    for (int j = 0; j < nc; ++j) {
        const int hk = V * 131 + j * 9 + L * 17;
        const float dir = (j & 1) ? 1.f : -1.f;
        const float vx = dir * (16.f + 84.f * hashf(hk, 32)) * s * d + side * 6.f * s;
        const float vy = (190.f + 170.f * hashf(hk, 33)) * s * d, gr = 880.f * s * d;
        auto at = [&](float tau, float& px, float& py) { px = xi + vx * tau; py = yg - vy * tau + 0.5f * gr * tau * tau; };
        float px, py; at(et, px, py);
        if (py > yg) continue;                                                 // back on the ground
        float qx, qy; at(std::max(0.f, et - 0.035f), qx, qy);
        const float dw = std::max(1.5f, (1.5f + 1.2f * hashf(hk, 34)) * s * d);
        g.line(qx, std::min(qy, yg), px, py, withA(C, 0.55f * fade), dw, true);
        g.fillCircle(px, py, dw * 0.75f, withA(C, 0.95f * fade));
    }
}

Bitmap buildSplashSheet(Gfx& g, const Frame& F, int L, float s, float side) {
    const SplGeo G = splGeo(L, s);
    const int rows = (SPL_VAR * SPL_FR + SPL_COLS - 1) / SPL_COLS;
    const float sw = G.cw * SPL_COLS, sh = G.ch * rows;
    if (sw < 8 || sh < 8 || sw > 8192 || sh > 8192 || !g.beginSprite((int)sw, (int)sh)) return Bitmap{};
    for (int V = 0; V < SPL_VAR; ++V)
        for (int f = 0; f < SPL_FR; ++f) {
            const int idx = V * SPL_FR + f;
            g.setOffset((idx % SPL_COLS) * G.cw, (idx / SPL_COLS) * G.ch);
            g.pushClip(0, 0, G.cw, G.ch);
            splashFrame(g, F.rain, L, V, (f + 0.5f) / SPL_FR, s, side, G);
            g.popClip();
        }
    g.setOffset(0, 0);
    return g.endSprite();
}

void drawRain(Ctx& c, const Frame& F, float k, float side, float q, bool sky = false) {
    const bool mistOk = !sky && mistReady(c, F);          // on the Sky face: just the drops and their splashes, no sheen and no mist, so every label stays easy to read
    Gfx& g = c.g; App& a = c.a; const float s = c.s, W = c.W, H = c.H;
    const double t = a.now;
    const Color dropC = F.rain;
    // how much the drops lean: more in a strong wind (in steps of a hundredth, so the images are not rebuilt for every breath of wind)
    const float slant = std::round(0.22f * side * (0.45f + std::min(1.f, windKmh(a) / 40.f)) * 100.f) / 100.f;
    const int sq = (int)std::lround(std::log2(std::max(0.05f, s)) * 5.f);
    const uint64_t key = mixKey(mixKey(q3(F.rain), (uint64_t)(sq + 64)), 5100);
    const bool strOk = ensure(g, gRainStreak, mixKey(key, (uint64_t)(std::lround(slant * 100.f) + 64)), s, [&] {
        Bitmap b = buildStreaks(g, F, s, slant);
        if (b.bmp) { gStrkS = s; gStrkSlant = slant; }
        return b;
    });
    bool splOk[3];
    for (int L = 0; L < 3; ++L) splOk[L] = ensure(g, gRainSplash[L], mixKey(key, 9100 + (uint64_t)L * 17 + (side > 0 ? 0 : 1)), s, [&] {
        Bitmap b = buildSplashSheet(g, F, L, s, side);
        if (b.bmp) gSplS[L] = s;
        return b;
    });
    const float eg = std::min(gFx, 1.6f);

    // a wet sheen on the ground
    if (!sky) g.vGradientRect(0, H * 0.76f, W, H * 0.24f, withA(dropC, 0.f), withA(dropC, al(0.14f * k)));

    StrkGeo SG[3]; float aw = 0, ah = 0;
    if (strOk) strkGeo(gStrkS, gStrkSlant, SG, aw, ah);                          // the cells as they were drawn
    const float bs = gStrkSlant;
    const int n = (int)std::lround((96 + 280 * k) * q);
    for (int L = 0; L < 3; ++L) {
        const float d = kDepth[L], yg = H * kGround[L];
        const SplGeo SP = splGeo(L, gSplS[L] > 0 ? gSplS[L] : s);
        // the drops of this depth, then their splashes (so splashes sit on top)
        for (int pass = 0; pass < 2; ++pass) {
            if (pass == 0 ? !strOk : !splOk[L]) continue;
            for (int i = L; i < n; i += 3) {
                const float sp = (620.f + 460.f * hashf(i, 21)) * s * d;
                const float len = (22.f + 34.f * hashf(i, 22)) * s * d;
                const float span = yg + std::max(len, sp * SPLASH) + sp * 0.12f;      // fall, splash, a short rest, and round again
                const float pos = loopPos(t, sp, hashf(i, 23) * span, span);          // the head of the drop
                const float x0 = hashf(i, 24) * (W + H * 0.70f) - H * 0.35f;           // wide enough for strong wind slant in portrait
                const float a0 = kAlpha[L] * (0.75f + 0.5f * hashf(i, 25)) * k;
                if (pass == 0) {
                    const StrkGeo& G = SG[L];
                    const int v = std::min(2, (int)(hashf(i, 22) * 3.f));
                    const float xh = x0 + bs * pos;
                    if (xh < -40.f * s || xh > W + 40.f * s) continue;
                    float rows = G.ch;                                                // rows to copy: all, or only down to the ground once the head is past it
                    const float hy = G.ch - G.padY, cut = pos - yg;
                    if (cut > 0.f) rows = std::min(G.ch, std::floor(hy - cut) + 1.f);
                    if (rows < 3.f) continue;
                    const float hx = bs * G.len[v] >= 0.f ? G.padX + bs * G.len[v] : G.padX;
                    g.drawBitmapSub(gRainStreak.bmp, std::round(xh - hx), std::round(pos - hy), v * G.cw, G.rowY, G.cw, rows, clamp01(al(a0)));
                } else if (pos >= yg) {
                    const float et = (pos - yg) / sp;                                 // seconds since it landed
                    const float u = et / SPLASH;
                    if (u >= 1.f) continue;
                    const float xi = x0 + bs * yg;
                    if (xi < -70.f * s || xi > W + 70.f * s) continue;
                    const int f = std::min(SPL_FR - 1, (int)(u * SPL_FR));
                    const int V = hashf(i, 30) > 0.5f ? 1 : 0, idx = V * SPL_FR + f;
                    g.drawBitmapSub(gRainSplash[L].bmp, std::round(xi - SP.ax), std::round(yg - SP.ay), (idx % SPL_COLS) * SP.cw, (idx / SPL_COLS) * SP.ch, SP.cw, SP.ch,
                                    clamp01(k * eg * (0.82f + 0.36f * hashf(i, 25))));
                }
            }
        }
    }
    if (mistOk) g.drawBitmapRect(gMist.bmp, 0, H - gMistH, W, gMistH, clamp01(0.45f * k * std::min(gFx, 1.6f)));   // a low mist
}

// A few drops fall in front of the time on the Quiet face (and only there): the near ones, a little faster, all the way down the screen.
// Few on purpose: they cross the digits now and then and never make them hard to read.
void drawFrontDrops(Ctx& c, float k, float side, float q) {
    Gfx& g = c.g; App& a = c.a; const float s = c.s, W = c.W, H = c.H;
    if (!gRainStreak.bmp.bmp || gStrkS <= 0.f) return;                       // the drop images belong to the rain behind: wait until they exist
    StrkGeo SG[3]; float aw, ah; strkGeo(gStrkS, gStrkSlant, SG, aw, ah);
    const StrkGeo& G = SG[2]; const float d = kDepth[2], bs = gStrkSlant;
    const int m = (int)std::lround((5 + 9 * k) * q);                          // look: how many are in front of the text
    for (int i = 0; i < m; ++i) {
        const float sp = (780.f + 320.f * hashf(i, 401)) * s * d * 1.1f;
        const float span = H + G.len[2] + 80.f * s;
        const float pos = loopPos(a.now, sp, hashf(i, 402) * span, span);
        const float x0 = hashf(i, 403) * (W + H * 0.70f) - H * 0.35f;
        const float xh = x0 + bs * pos;
        if (xh < -40.f * s || xh > W + 40.f * s) continue;
        const int v = hashf(i, 404) > 0.5f ? 2 : 1;
        const float hx = bs * G.len[v] >= 0.f ? G.padX + bs * G.len[v] : G.padX, hy = G.ch - G.padY;
        const float a0 = kAlpha[2] * (0.60f + 0.35f * hashf(i, 405)) * k;       // look: how bright
        g.drawBitmapSub(gRainStreak.bmp, std::round(xh - hx), std::round(pos - hy), v * G.cw, G.rowY, G.cw, G.ch, clamp01(al(a0)));
    }
}

// ---------------------------------------------------------------- calm, soothing snow
// Three depth layers of soft pre-baked snowflakes (distant powder, mid crystal glow, near out-of-focus bokeh)
// drifting slowly with lazy dual-sine horizontal flutter into a gentle frosted glow along the bottom edge.
Bitmap buildSnowSheet(Gfx& g, float cell, const Color& core, const Color& rim, bool dark) {
    const int c = std::max(16, (int)std::ceil(cell));
    if (!g.beginSprite(c * 3, c)) return Bitmap{};
    const float half = c * 0.5f;
    // Variant 0: distant delicate snow powder dot
    {
        const float cx = half, cy = half, r = c * 0.14f;
        if (!dark) g.glow(cx, cy, r * 2.2f, rim, 0.36f);
        g.glow(cx, cy, r * 1.9f, core, dark ? 0.55f : 0.65f);
        g.fillCircle(cx, cy, r * 0.65f, withA(core, 0.92f));
    }
    // Variant 1: mid soft snowflake with subtle 6-spoke crystal glint
    {
        const float cx = c + half, cy = half, r = c * 0.24f;
        if (!dark) g.glow(cx, cy, r * 2.0f, rim, 0.42f);
        g.glow(cx, cy, r * 1.85f, core, dark ? 0.48f : 0.68f);
        const float spoke = r * 0.85f, sw = std::max(1.0f, r * 0.16f);
        for (int k = 0; k < 3; ++k) {
            const float ang = k * (PI_F / 3.f);
            const float dx = spoke * std::cos(ang), dy = spoke * std::sin(ang);
            g.line(cx - dx, cy - dy, cx + dx, cy + dy, withA(dark ? core : rim, dark ? 0.55f : 0.38f), sw, true);
        }
        g.fillCircle(cx, cy, r * 0.46f, withA(core, 0.94f));
    }
    // Variant 2: near soft out-of-focus bokeh flake
    {
        const float cx = c * 2 + half, cy = half, r = c * 0.40f;
        if (!dark) g.glow(cx, cy, r * 1.15f, rim, 0.34f);
        g.glow(cx, cy, r * 1.10f, core, dark ? 0.45f : 0.55f);
        g.glow(cx, cy, r * 0.55f, core, dark ? 0.72f : 0.82f);
    }
    return g.endSprite();
}

bool snowReady(Ctx& c, const Frame& F) {
    const float cell = std::max(24.f, std::round(34.f * c.s));
    uint64_t key = mixKey(mixKey(8200, qsz(cell)), mixKey(q3(F.snow), q3(F.snowRim) * 2 + (c.a.palTarget.dark ? 1u : 0u)));
    if (ensure(c.g, gSnowFlake, key, cell, [&] { return buildSnowSheet(c.g, cell, F.snow, F.snowRim, c.a.palTarget.dark); })) {
        gSnowCell = (float)gSnowFlake.bmp.h;
        return true;
    }
    return false;
}

void drawSnow(Ctx& c, const Frame& F, float k, float side, float q, bool skyBg = false) {
    Gfx& g = c.g; App& a = c.a; const float s = c.s, W = c.W, H = c.H;
    if (!snowReady(c, F) || gSnowCell <= 0.f) return;
    const float cell = gSnowCell;
    const double t = a.fxSnowT;
    const float windLean = side * (0.04f + 0.16f * clamp01(a.fxWk / 45.f));
    const float density = (skyBg ? 0.58f : 1.0f) * q;
    struct Layer { int baseN, extraN, variant, salt; float spMin, spMax, swayAmp, swayF1, swayF2, sizeScale, alpha; };
    static const Layer layers[3] = {
        {42, 46, 0, 710, 24.f, 40.f, 14.f, 0.45f, 0.93f, 0.58f, 0.48f},   // far delicate powder
        {28, 34, 1, 740, 42.f, 66.f, 26.f, 0.58f, 1.18f, 0.78f, 0.72f},   // mid calm snowflakes
        {10, 14, 2, 770, 58.f, 88.f, 38.f, 0.48f, 1.05f, 1.00f, 0.52f},   // near soft bokeh flakes
    };
    const float pad = cell * 1.2f;
    const float spanY = H + pad * 2.f;
    for (int L = 0; L < 3; ++L) {
        const Layer& ly = layers[L];
        const int n = std::max(4, (int)std::lround((ly.baseN + ly.extraN * k) * density));
        for (int i = 0; i < n; ++i) {
            const float h1 = hashf(i, ly.salt + 1), h2 = hashf(i, ly.salt + 2), h3 = hashf(i, ly.salt + 3), h4 = hashf(i, ly.salt + 4);
            const float sp = (ly.spMin + (ly.spMax - ly.spMin) * h1) * s;
            const float py = loopPos(t, sp, h2 * spanY, spanY) - pad;
            const float sway = (std::sin(ph(t, ly.swayF1 * (0.8 + 0.4 * h3), h4 * 6.2831)) +
                                0.42f * std::sin(ph(t, ly.swayF2 * (0.8 + 0.4 * h1), h2 * 6.2831))) * ly.swayAmp * s;
            const float driftX = (float)std::fmod(h3 * (W + 160.f * s) + t * sp * windLean, (double)(W + 160.f * s));
            float px = (driftX < 0.f ? driftX + W + 160.f * s : driftX) - 80.f * s + sway;
            if (px < -cell || px > W + cell) continue;
            // gentle entrance from top & whisper-soft dissolve as flakes settle near the bottom
            const float topFade = smoothstep(-pad * 0.4f, H * 0.08f, py);
            const float botFade = 1.f - smoothstep(H * 0.87f, H * 0.99f, py);
            const float shimmer = 0.78f + 0.22f * std::sin(ph(t, 1.4 + 1.2 * h4, h3 * 6.2831));
            const float op = clamp01(al(ly.alpha * k * topFade * botFade * shimmer * (0.68f + 0.32f * h4)));
            if (op < 0.01f) continue;
            const float sc = ly.sizeScale * (0.76f + 0.42f * h1);
            const float sz = cell * sc;
            if (std::fabs(sc - 1.f) < 0.04f)
                g.drawBitmapSub(gSnowFlake.bmp, std::round(px - cell * 0.5f), std::round(py - cell * 0.5f), ly.variant * cell, 0.f, cell, cell, op);
            else {
                // scaled soft draw via direct circle+glow for ultra-clean varied flake sizes when scaled
                const float r = sz * (ly.variant == 0 ? 0.13f : (ly.variant == 1 ? 0.21f : 0.35f));
                if (!c.P.dark) g.glow(px, py, r * 2.0f, F.snowRim, op * 0.35f);
                g.glow(px, py, r * 1.9f, F.snow, op * (ly.variant == 2 ? 0.55f : 0.65f));
                if (ly.variant < 2) g.fillCircle(px, py, std::max(1.0f, r * 0.58f), withA(F.snow, op * 0.92f));
            }
        }
    }
    // Whisper-soft frosted ground glow along the bottom edge (replaces rain splashes)
    if (!skyBg) {
        const float gh = H * 0.13f;
        const float ga = clamp01(al((c.P.dark ? 0.14f : 0.18f) * k));
        g.vGradientRect(0.f, H - gh, W, gh, withA(F.snow, 0.f), withA(F.snow, ga));
    }
}

void drawFrontSnow(Ctx& c, const Frame& F, float k, float side, float q) {
    Gfx& g = c.g; App& a = c.a; const float s = c.s, W = c.W, H = c.H;
    const double t = a.fxSnowT;
    const float windLean = side * (0.05f + 0.14f * clamp01(a.fxWk / 45.f));
    const int n = std::max(3, (int)std::lround((5 + 6 * k) * q));
    const float spanY = H + 90.f * s;
    for (int i = 0; i < n; ++i) {
        const float h1 = hashf(i, 791), h2 = hashf(i, 792), h3 = hashf(i, 793), h4 = hashf(i, 794);
        const float sp = (72.f + 36.f * h1) * s;
        const float py = loopPos(t, sp, h2 * spanY, spanY) - 45.f * s;
        const float sway = (std::sin(ph(t, 0.50 + 0.25 * h3, h4 * 6.2831)) + 0.4f * std::sin(ph(t, 1.05, h2 * 6.2831))) * 42.f * s;
        const float driftX = (float)std::fmod(h3 * (W + 180.f * s) + t * sp * windLean, (double)(W + 180.f * s));
        const float px = (driftX < 0.f ? driftX + W + 180.f * s : driftX) - 90.f * s + sway;
        const float fade = smoothstep(-20.f * s, H * 0.08f, py) * (1.f - smoothstep(H * 0.88f, H, py));
        const float op = clamp01(al(0.28f * k * fade * (0.7f + 0.3f * h4)));
        if (op < 0.01f) continue;
        const float r = (7.5f + 5.5f * h1) * s;
        if (!c.P.dark) g.glow(px, py, r * 1.45f, F.snowRim, op * 0.35f);
        g.glow(px, py, r * 1.4f, F.snow, op * 0.65f);
        g.glow(px, py, r * 0.55f, F.snow, op * 0.85f);
    }
}

// ---------------------------------------------------------------- blizzard / harsh snowstorm
// Intense, driving yet unmistakably artistic winter storm:
//  - Three parallax depths x 4 organic snow variants (feathered snow smear, tumbling twin snow cluster,
//    driving squall streak, and wind-stretched glowing snow puff) pre-baked into a tiny atlas, advected
//    with toroidal 2D motion along the gale + turbulent crosswind eddies so density stays uniform at any aspect.
//  - Cached half-res billowing whiteout drift sprite (gBlizzDrift) for fast sweeping squall curtains and
//    racing ground snowbank plumes at a tiny fraction of per-frame radial-gradient cost.
//  - Curling frosted spindrift wind ribbons tying the blizzard cleanly into Meridian's painterly wind style.
const float kBDepth[3] = {0.58f, 0.88f, 1.24f};
const float kBWidth[3] = {1.85f, 3.00f, 4.45f};
const float kBDrop[4]  = {6.f, 9.f, 13.f, 4.5f};

struct BlizzGeo { float wd, dy[4], padX, padY, cw, ch, rowY; };
void blizzGeo(float s, float slant, BlizzGeo out[3], float& atlasW, float& atlasH) {
    float y = 0.f; atlasW = 0.f;
    for (int L = 0; L < 3; ++L) {
        BlizzGeo& G = out[L]; const float d = kBDepth[L];
        G.wd = std::max(1.3f, kBWidth[L] * s);
        for (int v = 0; v < 4; ++v) G.dy[v] = std::max(3.f, std::floor(kBDrop[v] * s * d));
        G.padX = std::ceil(G.wd * 2.6f) + 5.f;
        G.padY = std::ceil(G.wd * 2.6f) + 5.f;
        G.cw = std::ceil(2.f * G.padX + std::fabs(slant) * G.dy[2]);
        G.ch = std::ceil(G.dy[2] + 2.f * G.padY);
        G.rowY = y; y += G.ch;
        atlasW = std::max(atlasW, G.cw * 4.f);
    }
    atlasH = y;
}

Bitmap buildBlizzStreaks(Gfx& g, const Frame& F, float s, float slant, bool dark) {
    BlizzGeo BG[3]; float aw = 0.f, ah = 0.f;
    blizzGeo(s, slant, BG, aw, ah);
    if (aw < 4 || ah < 4 || aw > 4096 || ah > 4096 || !g.beginSprite((int)aw, (int)ah)) return Bitmap{};
    const Color C = F.snow, Rim = F.snowRim;
    for (int L = 0; L < 3; ++L) {
        const BlizzGeo& G = BG[L];
        for (int v = 0; v < 4; ++v) {
            g.setOffset(v * G.cw, G.rowY);
            const float dy = G.dy[v], dx = slant * dy;
            const float len = std::hypot(dx, dy);
            const float nx = -dy / std::max(1.f, len), ny = dx / std::max(1.f, len);
            const float midX = G.cw * 0.5f, midY = G.ch * 0.5f;
            const float tx = midX - dx * 0.5f, ty = midY - dy * 0.5f;
            auto smear = [&](float t0, float t1, float w0, float w1, const Color& col, float a0, float a1) {
                const float ax = tx + dx * t0, ay = ty + dy * t0;
                const float bx = tx + dx * t1, by = ty + dy * t1;
                g.fillPolyVGradient(
                    {{ax - nx * w0, ay - ny * w0}, {ax + nx * w0, ay + ny * w0},
                     {bx + nx * w1, by + ny * w1}, {bx - nx * w1, by - ny * w1}},
                    ay, by, withA(col, a0), withA(col, a1));
            };
            if (v == 3) {
                // Variant 3: wind-stretched luminous snowflake puff (reads instantaneously as blowing snow)
                const int steps = 4;
                for (int k = 0; k < steps; ++k) {
                    const float f = (k + 0.5f) / steps;
                    const float px = tx + dx * f, py = ty + dy * f;
                    const float env = std::sin(f * PI_F);
                    if (!dark) g.glow(px, py, G.wd * (1.1f + 0.55f * env), Rim, 0.24f * env);
                    g.glow(px, py, G.wd * (0.95f + 0.60f * env), C, (dark ? 0.50f : 0.58f) * env);
                }
                g.glow(midX + dx * 0.14f, midY + dy * 0.14f, G.wd * 1.45f, C, 0.62f);
                g.fillCircle(midX + dx * 0.14f, midY + dy * 0.14f, G.wd * 0.52f, withA(C, 0.90f));
            } else if (v == 1) {
                // Variant 1: tumbling twin snow cluster caught in an eddy
                if (!dark) smear(0.0f, 1.0f, G.wd * 0.35f, G.wd * 0.85f, Rim, 0.0f, 0.22f);
                smear(0.0f, 0.82f, G.wd * 0.28f, G.wd * 0.78f, C, 0.0f, dark ? 0.36f : 0.44f);
                const float p1x = tx + dx * 0.78f + nx * G.wd * 0.22f, p1y = ty + dy * 0.78f + ny * G.wd * 0.22f;
                const float p2x = tx + dx * 0.42f - nx * G.wd * 0.38f, p2y = ty + dy * 0.42f - ny * G.wd * 0.38f;
                if (!dark) { g.glow(p1x, p1y, G.wd * 1.6f, Rim, 0.26f); g.glow(p2x, p2y, G.wd * 1.3f, Rim, 0.20f); }
                g.glow(p1x, p1y, G.wd * 1.45f, C, 0.56f);
                g.fillCircle(p1x, p1y, G.wd * 0.54f, withA(C, 0.92f));
                g.glow(p2x, p2y, G.wd * 1.15f, C, 0.46f);
                g.fillCircle(p2x, p2y, G.wd * 0.40f, withA(C, 0.78f));
            } else {
                // Variants 0 & 2: soft powdery wind-driven snow smears (feathered tail + fluffy snow core, no harsh needle dot)
                if (!dark) {
                    smear(0.0f, 0.85f, G.wd * 0.38f, G.wd * 1.00f, Rim, 0.0f, 0.26f);
                    smear(0.85f, 1.0f, G.wd * 1.00f, G.wd * 0.45f, Rim, 0.26f, 0.0f);
                }
                smear(0.0f, 0.82f, G.wd * 0.25f, G.wd * 0.86f, C, 0.0f, dark ? 0.48f : 0.56f);
                smear(0.82f, 1.0f, G.wd * 0.86f, G.wd * 0.32f, C, dark ? 0.48f : 0.56f, 0.0f);
                smear(0.28f, 0.86f, G.wd * 0.16f, G.wd * 0.48f, C, 0.0f, dark ? 0.68f : 0.76f);
                const float cx = tx + dx * 0.80f, cy = ty + dy * 0.80f;
                g.glow(cx, cy, G.wd * 1.55f, C, dark ? 0.50f : 0.58f);
                g.fillCircle(cx, cy, G.wd * 0.46f, withA(C, 0.88f));
                if (v == 2 && L >= 1) {
                    const float qx = tx + dx * 0.50f - nx * G.wd * 0.26f, qy = ty + dy * 0.50f - ny * G.wd * 0.26f;
                    g.fillCircle(qx, qy, G.wd * 0.30f, withA(C, 0.50f));
                }
            }
        }
    }
    g.setOffset(0, 0);
    return g.endSprite();
}

// Cached half-resolution billowing whiteout snow-squall sprite (replaces expensive per-frame radial gradients)
bool blizzDriftReady(Ctx& c, const Frame& F, float side) {
    const float W = c.W, H = c.H;
    const bool dark = c.P.dark;
    const uint64_t key = mixKey(mixKey(q3(F.snow), mixKey(q3(F.snowRim), dark ? 1u : 0u)),
                                mixKey(mixKey(qsz(W), qsz(H)), side > 0.f ? 8501u : 8502u));
    gBlizzDriftH = 0.24f * H;
    return ensure(c.g, gBlizzDrift, key, 0.f, [&] {
        const int sw = std::max(32, (int)(W * 0.72f * SPR)), sh = std::max(24, (int)(gBlizzDriftH * SPR));
        if (!c.g.beginSprite(sw, sh)) return Bitmap{};
        const int puffs = 11;
        for (int m = 0; m < puffs; ++m) {
            const float u = (m + 0.5f) / puffs;
            const float env = std::sin(u * PI_F);
            const float rx = sw * (0.11f + 0.08f * env * (0.8f + 0.4f * hashf(m, 861)));
            const float ry = sh * (0.20f + 0.16f * env * (0.8f + 0.4f * hashf(m, 862)));
            const float cx = rx * 0.95f + (sw - 2.f * rx * 0.95f) * u;
            // Subtle wind-sheared diagonal tilt inside the plume
            const float slope = (u - 0.5f) * side * sh * 0.16f;
            const float cy = std::max(ry * 0.95f, std::min(sh - ry * 0.95f, sh * 0.5f + slope + (hashf(m, 863) - 0.5f) * sh * 0.18f));
            if (!dark) c.g.glowEllipse(cx, cy + ry * 0.15f, rx * 1.15f, ry * 1.05f, F.snowRim, 0.22f * env);
            c.g.glowEllipse(cx, cy, rx * 1.25f, ry * 1.10f, F.snow, 0.30f * env);
            c.g.glowEllipse(cx, cy, rx * 0.65f, ry * 0.62f, F.snow, 0.22f * env);
        }
        return c.g.endSprite();
    });
}

void drawBlizzard(Ctx& c, const Frame& F, float k, float side, float q, bool skyBg = false) {
    Gfx& g = c.g; App& a = c.a; const float s = c.s, W = c.W, H = c.H;
    if (W < 4.f || H < 4.f) return;
    const double t = a.now, T = std::max(0.0, a.fxBlizzT);
    const bool dark = c.P.dark;
    const float gust = 0.5f + 0.5f * std::sin(ph(t, 0.55) + 0.85f * std::sin(ph(t, 0.21)));
    const float surge = 0.74f + 0.52f * gust * gust;                             // howling squall surge
    const float gs = std::min(gFx, 1.6f);
    const bool driftOk = !skyBg && blizzDriftReady(c, F, side);

    // A. Sweeping diagonal whiteout squall curtains & wintry depth haze (cached sprite -> ultra fast!)
    if (!skyBg) {
        const float topA = al((dark ? 0.08f : 0.10f) * k * (0.85f + 0.22f * gust));
        g.vGradientRect(0.f, 0.f, W, H * 0.36f, withA(F.snowRim, topA), withA(F.snowRim, 0.f));
        const float botH = H * 0.28f;
        const float botA = al((dark ? 0.24f : 0.27f) * k * (0.84f + 0.26f * gust));
        g.vGradientRect(0.f, H - botH, W, botH, withA(F.snow, 0.f), withA(F.snow, botA));

        if (driftOk) {
            const Bitmap& bm = gBlizzDrift.bmp;
            const int nv = std::max(3, (int)std::lround(4.f * q));
            for (int i = 0; i < nv; ++i) {
                const float h1 = hashf(i, 811), h2 = hashf(i, 812), h3 = hashf(i, 813);
                const float scX = 0.95f + 0.50f * h1, scY = 0.85f + 0.55f * h2;
                const float bw = (bm.w / SPR) * scX, bh = (bm.h / SPR) * scY;
                const float spanX = W + bw;
                const float spX = (220.f + 170.f * h3) * s;
                const float px = loopPos(T, side * spX, h2 * spanX, spanX) - bw;
                const float py = H * (0.12f + 0.68f * h1) + std::sin(ph(T, 0.38 + 0.22 * h2, h3 * 6.28)) * 0.05f * H;
                const float pulse = 0.68f + 0.32f * std::sin(ph(t, 0.45 + 0.30 * h1, h2 * 6.28));
                const float op = clamp01((dark ? 0.24f : 0.28f) * k * pulse * (0.76f + 0.36f * gust) * gs);
                g.drawBitmapRect(bm, px, py - bh * 0.5f, bw, bh, op);
            }
        }
    }

    // B. Three parallax depths of dense driving snow streaks & tumbling clusters (toroidal 2D advection)
    const float slant = std::round(side * 1.85f * 10.f) / 10.f;
    const int sq = (int)std::lround(std::log2(std::max(0.05f, s)) * 5.f);
    const uint64_t key = mixKey(mixKey(q3(F.snow), mixKey(q3(F.snowRim), (uint64_t)(sq + 96) * 2 + (dark ? 1u : 0u))),
                                mixKey(8410, (uint64_t)(std::lround(slant * 10.f) + 64)));
    const bool strOk = ensure(g, gBlizzStreak, key, s, [&] {
        Bitmap b = buildBlizzStreaks(g, F, s, slant, dark);
        if (b.bmp) { gBlizzS = s; gBlizzSlant = slant; }
        return b;
    });
    if (strOk && gBlizzS > 0.f) {
        BlizzGeo BG[3]; float aw = 0.f, ah = 0.f;
        blizzGeo(gBlizzS, gBlizzSlant, BG, aw, ah);
        const float bs = gBlizzSlant;
        const float density = (skyBg ? 0.58f : 1.0f) * q;
        const float invS = 1.f / std::max(0.1f, s);
        struct BLayer { int baseN, extraN, salt; float vyMin, vyMax, eddyAmp, alpha; };
        static const BLayer layers[3] = {
            {115, 135, 830, 145.f, 225.f, 12.f, 0.50f},   // far dense blowing winter powder
            { 82, 118, 860, 235.f, 360.f, 19.f, 0.78f},   // mid fierce wind-whipped snow streaks & clusters
            { 28,  42, 890, 360.f, 515.f, 26.f, 0.64f},   // near dramatic soft-blurred blowing snow
        };
        for (int L = 0; L < 3; ++L) {
            const BLayer& ly = layers[L];
            const BlizzGeo& G = BG[L];
            const int n = std::max(8, (int)std::lround((ly.baseN + ly.extraN * k) * density));
            const float padX = G.cw + 16.f * s, padY = G.ch + 24.f * s;
            const float wrapW = W + 2.f * padX, wrapH = H + 2.f * padY;
            for (int i = 0; i < n; ++i) {
                const float h1 = hashf(i, ly.salt + 1), h2 = hashf(i, ly.salt + 2), h3 = hashf(i, ly.salt + 3), h4 = hashf(i, ly.salt + 4);
                const float vy = (ly.vyMin + (ly.vyMax - ly.vyMin) * h1) * s;
                const float vx = bs * vy;
                const float px = loopPos(T, vx, h2 * wrapW, wrapW) - padX;
                const float rawY = loopPos(T, vy, h3 * wrapH, wrapH) - padY;
                const float eddy = std::sin(ph(T, 1.7 + 1.3 * h4, h3 * 6.2831) + (px * invS) * 0.0024f) * ly.eddyAmp * s;
                const float py = rawY + eddy;
                if (px < -G.cw || px > W + G.cw || py < -G.ch || py > H + G.ch) continue;
                const float edgeY = smoothstep(-G.ch * 0.6f, H * 0.04f, py) * (1.f - smoothstep(H * 0.94f, H + G.ch * 0.6f, py));
                const float shimmer = 0.74f + 0.26f * std::sin(ph(t, 2.2 + 1.8 * h4, h2 * 6.2831));
                const float op = clamp01(al(ly.alpha * k * edgeY * shimmer * (0.68f + 0.32f * h4) * (0.86f + 0.20f * surge)));
                if (op < 0.01f) continue;
                const int v = std::min(3, (int)(h4 * 4.f));
                g.drawBitmapSub(gBlizzStreak.bmp, std::round(px - G.cw * 0.5f), std::round(py - G.ch * 0.5f),
                                v * G.cw, G.rowY, G.cw, G.ch, op);
            }
        }
    }

    // C. Howling artistic spindrift wind ribbons (merges harsh snowstorm with Meridian's painterly wind language)
    if (!skyBg) {
        const bool left = side < 0.f;
        auto mx = [&](float x) { return left ? W - x : x; };
        Pt pts[WPTS + 2]; float vv[WPTS + 2];
        WindPath P;
        static std::vector<Pt> bPoly;
        if (bPoly.capacity() < (size_t)(WPTS * 2)) bPoly.reserve((size_t)(WPTS * 2));
        const int nr = std::max(2, (int)std::lround((3.f + 4.f * k) * q));
        for (int i = 0; i < nr; ++i) {
            const int L = i % 3;
            const double per = 1.65 + 1.05 * hashf(i, 921);
            const double tt = T / per + hashf(i, 922);
            const double cyc = std::floor(tt);
            const float u = (float)(tt - cyc);
            const int sd = i * 9733 + (int)std::fmod(cyc, 65521.0);
            const float len = W * (0.22f + 0.18f * hashf(sd, 924)) * (0.85f + 0.22f * k);
            const float x0 = W * (-0.10f + 0.86f * hashf(sd, 925)) - 0.35f * len + u * 0.16f * W;
            const float y0 = H * (0.10f + 0.80f * hashf(sd, 926));
            const float amp = (8.f + 15.f * hashf(sd, 927)) * s;
            const float wl = (380.f + 340.f * hashf(sd, 928)) * s;
            const float phase = hashf(sd, 929) * 6.2832f - 2.6f * u;
            const float bow = (0.09f + 0.08f * hashf(sd, 930)) * H;              // sweeps decisively downward along the blizzard gale
            const bool curl = hashf(sd, 931) < 0.48f;
            const float curlR = curl ? (13.f + 14.f * hashf(sd, 932)) * s : 0.f;
            const float turns = 0.72f + 0.26f * hashf(sd, 933);
            const float curlDir = hashf(sd, 934) < 0.70f ? -1.f : 1.f;
            windPath(P, x0, y0, len, amp, wl, phase, bow, curlR, turns, curlDir);
            const float head = easeOut3(u / 0.56f), tail = easeInOut((u - 0.26f) / 0.74f);
            const int m = windSlice(P, tail, head, pts, vv);
            if (m < 2) continue;
            for (int j = 0; j < m; ++j) pts[j].x = mx(pts[j].x);
            const float life = smoothstep(0.f, 0.10f, u) * (1.f - smoothstep(0.84f, 1.f, u));
            const float ra = (0.28f + 0.14f * L) * k * life * (0.76f + 0.34f * gust);
            if (ra < 0.008f) continue;
            const float w = std::max(1.5f, (2.4f + 1.1f * L) * s);
            if (!dark) windRibbon(g, pts, vv, m, w * 2.5f, withA(F.snowRim, al(0.24f * ra)), bPoly);
            windRibbon(g, pts, vv, m, w * 2.9f, withA(F.snow, al(0.24f * ra)), bPoly);
            windRibbon(g, pts, vv, m, w * 0.90f, withA(F.snow, al(0.58f * ra)), bPoly);
        }

        // D. Fast-racing ground spindrift snowbank plumes skimming the bottom 18% of the screen (cached sprite!)
        if (driftOk) {
            const Bitmap& bm = gBlizzDrift.bmp;
            const int ng = std::max(3, (int)std::lround(5.f * q));
            for (int i = 0; i < ng; ++i) {
                const float h1 = hashf(i, 951), h2 = hashf(i, 952), h3 = hashf(i, 953);
                const float bw = (bm.w / SPR) * (0.52f + 0.34f * h1);
                const float bh = (bm.h / SPR) * (0.36f + 0.22f * h2);
                const float spanX = W + bw;
                const float spX = (310.f + 240.f * h3) * s;
                const float px = loopPos(T, side * spX, h1 * spanX, spanX) - bw;
                const float py = H * (0.87f + 0.12f * h2) - std::sin(ph(T, 1.1 + 0.7 * h3, h1 * 6.28)) * 10.f * s;
                const float op = clamp01((dark ? 0.36f : 0.40f) * k * (0.70f + 0.36f * gust) * (0.70f + 0.30f * h3) * gs);
                g.drawBitmapRect(bm, px, py - bh * 0.5f, bw, bh, op);
            }
        }
    }
}

void drawFrontBlizzard(Ctx& c, const Frame& F, float k, float side, float q) {
    Gfx& g = c.g; App& a = c.a; const float s = c.s, W = c.W, H = c.H;
    if (!gBlizzStreak.bmp.bmp || gBlizzS <= 0.f) return;
    BlizzGeo BG[3]; float aw = 0.f, ah = 0.f;
    blizzGeo(gBlizzS, gBlizzSlant, BG, aw, ah);
    const BlizzGeo& G = BG[2];
    const float bs = gBlizzSlant;
    const double T = std::max(0.0, a.fxBlizzT);
    const float padX = G.cw + 20.f * s, padY = G.ch + 20.f * s;
    const float wrapW = W + 2.f * padX, wrapH = H + 2.f * padY;
    // Fast foreground wind-streaked blizzard flakes whipping across the Quiet digits (sparse so digits stay crisp)
    const int n = std::max(6, (int)std::lround((10 + 10 * k) * q));
    for (int i = 0; i < n; ++i) {
        const float h1 = hashf(i, 971), h2 = hashf(i, 972), h3 = hashf(i, 973), h4 = hashf(i, 974);
        const float vy = (430.f + 190.f * h1) * s;
        const float px = loopPos(T, bs * vy, h2 * wrapW, wrapW) - padX;
        const float py = loopPos(T, vy, h3 * wrapH, wrapH) - padY + std::sin(ph(T, 2.2 + h4, h2 * 6.28)) * 16.f * s;
        const float fade = smoothstep(-G.ch * 0.4f, H * 0.07f, py) * (1.f - smoothstep(H * 0.90f, H + G.ch * 0.4f, py));
        const float op = clamp01(al(0.38f * k * fade * (0.65f + 0.35f * h4)));
        if (op < 0.01f) continue;
        const int v = std::min(3, (int)(h4 * 4.f));
        g.drawBitmapSub(gBlizzStreak.bmp, std::round(px - G.cw * 0.5f), std::round(py - G.ch * 0.5f),
                        v * G.cw, G.rowY, G.cw, G.ch, op);
    }
    // A few swift out-of-focus foreground snow puffs whipping past the glass
    const int nb = std::max(2, (int)std::lround((3 + 3 * k) * q));
    for (int i = 0; i < nb; ++i) {
        const float h1 = hashf(i, 981), h2 = hashf(i, 982), h3 = hashf(i, 983);
        const float vy = (360.f + 140.f * h1) * s;
        const float px = loopPos(T, side * 1.85f * vy, h2 * wrapW, wrapW) - padX;
        const float py = loopPos(T, vy, h3 * wrapH, wrapH) - padY;
        const float op = clamp01(al(0.20f * k * (0.7f + 0.3f * h1)));
        if (op < 0.01f) continue;
        const float r = (9.f + 5.f * h2) * s;
        g.glowEllipse(px, py, r * 2.0f, r * 0.78f, F.snow, op);
    }
}

// ---------------------------------------------------------------- drizzle (silky ultra-fine misty rain)
// Poetic, feather-light micro-rain: much thinner, shorter and slower than rain, drifting softly on the air
// with delicate whisper-thin concentric ground ripples (no heavy vertical rain spouts) and a damp low mist.
const float kDrizDepth[3] = {0.66f, 0.88f, 1.12f};
const float kDrizAlpha[3] = {0.36f, 0.54f, 0.72f};
const float kDrizWidth[3] = {0.85f, 1.15f, 1.48f};
const float kDrizLen[3]   = {14.f, 21.f, 30.f};

void drizGeo(float s, float slant, StrkGeo out[3], float& atlasW, float& atlasH) {
    float y = 0.f; atlasW = 0.f;
    for (int L = 0; L < 3; ++L) {
        StrkGeo& G = out[L]; const float d = kDrizDepth[L];
        G.wd = std::max(0.85f, kDrizWidth[L] * s);
        for (int v = 0; v < 3; ++v) G.len[v] = std::max(5.f, std::floor(kDrizLen[v] * s * d));
        G.padX = std::ceil(G.wd) + 3.f; G.padY = std::ceil(G.wd) + 3.f;
        G.cw = std::ceil(2.f * G.padX + std::fabs(slant) * G.len[2]);
        G.ch = std::ceil(G.len[2] + 2.f * G.padY);
        G.rowY = y; y += G.ch; atlasW = std::max(atlasW, G.cw * 3.f);
    }
    atlasH = y;
}

Bitmap buildDrizzleStreaks(Gfx& g, const Frame& F, float s, float slant) {
    StrkGeo DG[3]; float aw = 0.f, ah = 0.f;
    drizGeo(s, slant, DG, aw, ah);
    if (aw < 4 || ah < 4 || aw > 4096 || ah > 4096 || !g.beginSprite((int)aw, (int)ah)) return Bitmap{};
    const Color C = F.drizzle;
    for (int L = 0; L < 3; ++L) {
        const StrkGeo& G = DG[L];
        for (int v = 0; v < 3; ++v) {
            g.setOffset(v * G.cw, G.rowY);
            const float len = G.len[v], dx = slant * len, w2 = G.wd * 0.5f;
            const float hy = G.ch - G.padY, hx = dx >= 0 ? G.padX + dx : G.padX, ty = hy - len, tx = hx - dx;
            g.fillPolyVGradient({{tx - w2, ty}, {tx + w2, ty}, {hx + w2, hy}, {hx - w2, hy}}, ty, hy, withA(C, 0.f), withA(C, 0.62f));
            g.fillCircle(hx, hy - G.wd * 0.15f, G.wd * 0.68f, withA(C, 0.84f));
        }
    }
    g.setOffset(0, 0);
    return g.endSprite();
}

void drawDrizzle(Ctx& c, const Frame& F, float k, float side, float q, bool skyBg = false) {
    Gfx& g = c.g; App& a = c.a; const float s = c.s, W = c.W, H = c.H;
    if (W < 4.f || H < 4.f) return;
    const double t = a.now;
    const float slant = std::round(0.11f * side * (0.45f + std::min(1.f, windKmh(a) / 45.f)) * 100.f) / 100.f;
    const int sq = (int)std::lround(std::log2(std::max(0.05f, s)) * 5.f);
    const uint64_t key = mixKey(mixKey(q3(F.drizzle), (uint64_t)(sq + 128)), mixKey(6100, (uint64_t)(std::lround(slant * 100.f) + 64)));
    const bool ok = ensure(g, gDrizzleStreak, key, s, [&] {
        Bitmap b = buildDrizzleStreaks(g, F, s, slant);
        if (b.bmp) { gDrizS = s; gDrizSlant = slant; }
        return b;
    });
    if (!ok || gDrizS <= 0.f) return;
    const bool mistOk = !skyBg && mistReady(c, F);
    if (!skyBg) {
        g.vGradientRect(0.f, H * 0.72f, W, H * 0.28f, withA(F.drizzle, 0.f), withA(F.drizzle, al(0.11f * k)));
    }
    StrkGeo DG[3]; float aw = 0.f, ah = 0.f;
    drizGeo(gDrizS, gDrizSlant, DG, aw, ah);
    const float bs = gDrizSlant;
    const float density = (skyBg ? 0.62f : 1.0f) * q;
    const int n = (int)std::lround((115 + 215 * k) * density);
    constexpr float RIPPLE_DUR = 0.44f;
    for (int L = 0; L < 3; ++L) {
        const StrkGeo& G = DG[L];
        const float d = kDrizDepth[L], yg = H * kGround[L];
        for (int i = L; i < n; i += 3) {
            const float h1 = hashf(i, 671), h2 = hashf(i, 672), h3 = hashf(i, 673), h4 = hashf(i, 674);
            const float sp = (265.f + 175.f * h1) * s * d;
            const float span = yg + sp * (RIPPLE_DUR + 0.14f);
            const float pos = loopPos(t, sp, h2 * span, span);
            const float sway = std::sin(ph(t, 0.85 + 0.55 * h4, h3 * 6.2831)) * 7.f * s * d;
            const float x0 = h3 * (W + H * 0.40f) - H * 0.20f + sway;
            const float a0 = kDrizAlpha[L] * (0.70f + 0.45f * h4) * k;
            if (pos < yg + G.len[2]) {
                const int v = std::min(2, (int)(h4 * 3.f));
                const float xh = x0 + bs * pos;
                if (xh >= -30.f * s && xh <= W + 30.f * s) {
                    float rows = G.ch;
                    const float hy = G.ch - G.padY, cut = pos - yg;
                    if (cut > 0.f) rows = std::min(G.ch, std::floor(hy - cut) + 1.f);
                    if (rows >= 2.f) {
                        const float hx = bs * G.len[v] >= 0.f ? G.padX + bs * G.len[v] : G.padX;
                        g.drawBitmapSub(gDrizzleStreak.bmp, std::round(xh - hx), std::round(pos - hy), v * G.cw, G.rowY, G.cw, rows, clamp01(al(a0)));
                    }
                }
            }
            // Delicate whisper-thin surface micro-ripple (only a subset so the ground reads calm & quiet)
            if (pos >= yg && (i % 2 == 0)) {
                const float u = (pos - yg) / (sp * RIPPLE_DUR);
                if (u < 1.f) {
                    const float xi = x0 + bs * yg;
                    if (xi >= -30.f * s && xi <= W + 30.f * s) {
                        const float fade = (1.f - u) * (1.f - u);
                        const float rr = (2.f + 16.f * std::pow(u, 0.65f)) * s * d;
                        g.strokeEllipse(xi, yg, rr, rr * 0.28f, withA(F.drizzle, al(0.42f * kDrizAlpha[L] * fade * k)), std::max(0.85f, 1.0f * s * d));
                        if (u < 0.24f) {
                            g.fillCircle(xi, yg, (1.0f + 0.9f * (1.f - u / 0.24f)) * s * d, withA(F.drizzle, al(0.50f * (1.f - u / 0.24f) * k)));
                        }
                    }
                }
            }
        }
    }
    if (mistOk) g.drawBitmapRect(gMist.bmp, 0.f, H - gMistH, W, gMistH, clamp01(0.42f * k * std::min(gFx, 1.6f)));
}

void drawFrontDrizzle(Ctx& c, float k, float q) {
    Gfx& g = c.g; App& a = c.a; const float s = c.s, W = c.W, H = c.H;
    if (!gDrizzleStreak.bmp.bmp || gDrizS <= 0.f) return;
    StrkGeo DG[3]; float aw = 0.f, ah = 0.f;
    drizGeo(gDrizS, gDrizSlant, DG, aw, ah);
    const StrkGeo& G = DG[2]; const float bs = gDrizSlant;
    const int m = (int)std::lround((6 + 8 * k) * q);
    for (int i = 0; i < m; ++i) {
        const float sp = (380.f + 160.f * hashf(i, 681)) * s;
        const float span = H + G.len[2] + 60.f * s;
        const float pos = loopPos(a.now, sp, hashf(i, 682) * span, span);
        const float x0 = hashf(i, 683) * (W + H * 0.40f) - H * 0.20f;
        const float xh = x0 + bs * pos;
        if (xh < -30.f * s || xh > W + 30.f * s) continue;
        const int v = hashf(i, 684) > 0.5f ? 2 : 1;
        const float hx = bs * G.len[v] >= 0.f ? G.padX + bs * G.len[v] : G.padX, hy = G.ch - G.padY;
        g.drawBitmapSub(gDrizzleStreak.bmp, std::round(xh - hx), std::round(pos - hy), v * G.cw, G.rowY, G.cw, G.ch, clamp01(al(0.38f * k)));
    }
}

// ---------------------------------------------------------------- sleet (icy freezing streaks interwoven with wet snow)
// Unmistakable dual-phase winter precipitation: brisk slanted translucent ice-beaded needles plunging alongside
// heavy tumbling wet snowflakes, leaving crisp icy slush flecks along the frosted bottom plane.
const float kSleetDepth[3] = {0.64f, 0.86f, 1.12f};
const float kSleetAlpha[3] = {0.42f, 0.64f, 0.86f};
const float kSleetWidth[3] = {1.15f, 1.65f, 2.25f};
const float kSleetLen[3]   = {14.f, 22.f, 31.f};

void sleetGeo(float s, float slant, StrkGeo out[3], float& atlasW, float& atlasH) {
    float y = 0.f; atlasW = 0.f;
    for (int L = 0; L < 3; ++L) {
        StrkGeo& G = out[L]; const float d = kSleetDepth[L];
        G.wd = std::max(1.05f, kSleetWidth[L] * s);
        for (int v = 0; v < 3; ++v) G.len[v] = std::max(6.f, std::floor(kSleetLen[v] * s * d));
        G.padX = std::ceil(G.wd * 2.2f) + 4.f; G.padY = std::ceil(G.wd * 2.2f) + 4.f;
        G.cw = std::ceil(2.f * G.padX + std::fabs(slant) * G.len[2]);
        G.ch = std::ceil(G.len[2] + 2.f * G.padY);
        G.rowY = y; y += G.ch; atlasW = std::max(atlasW, G.cw * 3.f);
    }
    atlasH = y;
}

Bitmap buildSleetStreaks(Gfx& g, const Frame& F, float s, float slant, bool dark) {
    StrkGeo SG[3]; float aw = 0.f, ah = 0.f;
    sleetGeo(s, slant, SG, aw, ah);
    if (aw < 4 || ah < 4 || aw > 4096 || ah > 4096 || !g.beginSprite((int)aw, (int)ah)) return Bitmap{};
    const Color C = F.sleet, Ice = F.snow;
    for (int L = 0; L < 3; ++L) {
        const StrkGeo& G = SG[L];
        for (int v = 0; v < 3; ++v) {
            g.setOffset(v * G.cw, G.rowY);
            const float len = G.len[v], dx = slant * len, w2 = G.wd * 0.52f;
            const float hy = G.ch - G.padY, hx = dx >= 0 ? G.padX + dx : G.padX, ty = hy - len, tx = hx - dx;
            g.fillPolyVGradient({{tx - w2, ty}, {tx + w2, ty}, {hx + w2, hy}, {hx - w2, hy}}, ty, hy, withA(C, 0.f), withA(C, dark ? 0.72f : 0.78f));
            // Frozen crystalline bead head (distinguishes freezing sleet cleanly from soft liquid rain)
            if (!dark) g.glow(hx, hy - G.wd * 0.1f, G.wd * 1.9f, C, 0.42f);
            g.glow(hx, hy - G.wd * 0.1f, G.wd * 1.65f, Ice, 0.56f);
            g.fillCircle(hx, hy - G.wd * 0.1f, G.wd * 0.88f, withA(Ice, 0.96f));
        }
    }
    g.setOffset(0, 0);
    return g.endSprite();
}

void drawSleet(Ctx& c, const Frame& F, float k, float side, float q, bool skyBg = false) {
    Gfx& g = c.g; App& a = c.a; const float s = c.s, W = c.W, H = c.H;
    if (W < 4.f || H < 4.f) return;
    const double t = a.now;
    const bool dark = c.P.dark;
    const float slant = std::round(side * (0.38f + 0.16f * clamp01(windKmh(a) / 50.f)) * 20.f) / 20.f;
    const int sq = (int)std::lround(std::log2(std::max(0.05f, s)) * 5.f);
    const uint64_t key = mixKey(mixKey(q3(F.sleet), mixKey(q3(F.snow), (uint64_t)(sq + 160) * 2 + (dark ? 1u : 0u))),
                                mixKey(6300, (uint64_t)(std::lround(slant * 20.f) + 64)));
    const bool ok = ensure(g, gSleetStreak, key, s, [&] {
        Bitmap b = buildSleetStreaks(g, F, s, slant, dark);
        if (b.bmp) { gSleetS = s; gSleetSlant = slant; }
        return b;
    });
    const bool flakeOk = snowReady(c, F) && gSnowCell > 0.f;
    if (!skyBg) {
        g.vGradientRect(0.f, H * 0.78f, W, H * 0.22f, withA(F.sleet, 0.f), withA(F.sleet, al((dark ? 0.16f : 0.18f) * k)));
    }
    const float density = (skyBg ? 0.58f : 1.0f) * q;

    // Phase A: Brisk slanted icy sleet streaks + tiny frosty slush flecks on ground impact
    if (ok && gSleetS > 0.f) {
        StrkGeo SG[3]; float aw = 0.f, ah = 0.f;
        sleetGeo(gSleetS, gSleetSlant, SG, aw, ah);
        const float bs = gSleetSlant;
        const int n = (int)std::lround((72 + 160 * k) * density);
        constexpr float FLECK_DUR = 0.30f;
        for (int L = 0; L < 3; ++L) {
            const StrkGeo& G = SG[L];
            const float d = kSleetDepth[L], yg = H * kGround[L];
            for (int i = L; i < n; i += 3) {
                const float h1 = hashf(i, 701), h2 = hashf(i, 702), h3 = hashf(i, 703), h4 = hashf(i, 704);
                const float sp = (460.f + 300.f * h1) * s * d;
                const float span = yg + sp * (FLECK_DUR + 0.12f);
                const float pos = loopPos(t, sp, h2 * span, span);
                const float x0 = h3 * (W + H * 0.75f) - H * 0.35f;
                const float a0 = kSleetAlpha[L] * (0.72f + 0.38f * h4) * k;
                if (pos < yg + G.len[2]) {
                    const int v = std::min(2, (int)(h4 * 3.f));
                    const float xh = x0 + bs * pos;
                    if (xh >= -40.f * s && xh <= W + 40.f * s) {
                        float rows = G.ch;
                        const float hy = G.ch - G.padY, cut = pos - yg;
                        if (cut > 0.f) rows = std::min(G.ch, std::floor(hy - cut) + 1.f);
                        if (rows >= 2.f) {
                            const float hx = bs * G.len[v] >= 0.f ? G.padX + bs * G.len[v] : G.padX;
                            g.drawBitmapSub(gSleetStreak.bmp, std::round(xh - hx), std::round(pos - hy), v * G.cw, G.rowY, G.cw, rows, clamp01(al(a0)));
                        }
                    }
                }
                if (pos >= yg && (i % 2 == 0)) {
                    const float et = (pos - yg) / sp, u = et / FLECK_DUR;
                    if (u < 1.f) {
                        const float xi = x0 + bs * yg;
                        if (xi >= -40.f * s && xi <= W + 40.f * s) {
                            const float fade = 1.f - u;
                            const float rr = (2.f + 14.f * std::pow(u, 0.55f)) * s * d;
                            g.strokeEllipse(xi, yg, rr, rr * 0.28f, withA(F.sleet, al(0.42f * fade * k)), std::max(0.9f, 1.1f * s * d));
                            // Two tiny icy rebound flecks
                            for (int b = 0; b < 2; ++b) {
                                const float dir = b == 0 ? -1.f : 1.f;
                                const float vx = dir * (28.f + 36.f * hashf(i * 3 + b, 706)) * s * d + side * 14.f * s;
                                const float vy = (115.f + 75.f * hashf(i * 3 + b, 707)) * s * d, gr = 980.f * s * d;
                                const float py = yg - vy * et + 0.5f * gr * et * et;
                                if (py <= yg) g.fillCircle(xi + vx * et, py, std::max(1.0f, 1.35f * s * d), withA(F.snow, al(0.75f * fade * k)));
                            }
                        }
                    }
                }
            }
        }
    }

    // Phase B: Interwoven heavy wet snowflakes tumbling briskly on the wintry breeze
    if (flakeOk) {
        const float cell = gSnowCell;
        const int nf = std::max(14, (int)std::lround((42 + 54 * k) * density));
        const float pad = cell + 20.f * s;
        const float wrapW = W + 2.f * pad, wrapH = H + 2.f * pad;
        for (int i = 0; i < nf; ++i) {
            const float h1 = hashf(i, 711), h2 = hashf(i, 712), h3 = hashf(i, 713), h4 = hashf(i, 714);
            const float vy = (145.f + 115.f * h1) * s;
            const float vx = side * (0.48f + 0.25f * h4) * vy;
            const float px = loopPos(t, vx, h2 * wrapW, wrapW) - pad + std::sin(ph(t, 1.9 + 1.2 * h3, h1 * 6.28)) * 12.f * s;
            const float py = loopPos(t, vy, h3 * wrapH, wrapH) - pad;
            if (px < -cell || px > W + cell || py < -cell || py > H + cell) continue;
            const float edge = smoothstep(-pad * 0.4f, H * 0.06f, py) * (1.f - smoothstep(H * 0.88f, H, py));
            const float op = clamp01(al((0.46f + 0.26f * h4) * k * edge));
            if (op < 0.01f) continue;
            const int var = h4 > 0.42f ? 1 : 0;
            g.drawBitmapSub(gSnowFlake.bmp, std::round(px - cell * 0.5f), std::round(py - cell * 0.5f), var * cell, 0.f, cell, cell, op);
        }
    }
}

void drawFrontSleet(Ctx& c, const Frame& F, float k, float side, float q) {
    Gfx& g = c.g; App& a = c.a; const float s = c.s, W = c.W, H = c.H;
    const double t = a.now;
    if (gSleetStreak.bmp.bmp && gSleetS > 0.f) {
        StrkGeo SG[3]; float aw = 0.f, ah = 0.f;
        sleetGeo(gSleetS, gSleetSlant, SG, aw, ah);
        const StrkGeo& G = SG[2]; const float bs = gSleetSlant;
        const int m = (int)std::lround((5 + 7 * k) * q);
        for (int i = 0; i < m; ++i) {
            const float sp = (640.f + 220.f * hashf(i, 721)) * s;
            const float span = H + G.len[2] + 60.f * s;
            const float pos = loopPos(t, sp, hashf(i, 722) * span, span);
            const float x0 = hashf(i, 723) * (W + H * 0.70f) - H * 0.35f;
            const float xh = x0 + bs * pos;
            if (xh < -40.f * s || xh > W + 40.f * s) continue;
            const int v = hashf(i, 724) > 0.5f ? 2 : 1;
            const float hx = bs * G.len[v] >= 0.f ? G.padX + bs * G.len[v] : G.padX, hy = G.ch - G.padY;
            g.drawBitmapSub(gSleetStreak.bmp, std::round(xh - hx), std::round(pos - hy), v * G.cw, G.rowY, G.cw, G.ch, clamp01(al(0.46f * k)));
        }
    }
    // A few wet tumbling flakes crossing in front
    const int nf = std::max(2, (int)std::lround((3 + 3 * k) * q));
    const float spanY = H + 70.f * s, wrapW = W + 160.f * s;
    for (int i = 0; i < nf; ++i) {
        const float h1 = hashf(i, 726), h2 = hashf(i, 727), h3 = hashf(i, 728);
        const float vy = (185.f + 75.f * h1) * s;
        const float px = loopPos(t, side * 0.55f * vy, h2 * wrapW, wrapW) - 80.f * s;
        const float py = loopPos(t, vy, h3 * spanY, spanY) - 35.f * s;
        const float r = (4.5f + 3.0f * h1) * s;
        g.glow(px, py, r * 1.4f, F.snow, al(0.26f * k));
        g.fillCircle(px, py, r * 0.52f, withA(F.snow, al(0.32f * k)));
    }
}

// ---------------------------------------------------------------- hail (faceted plunging ice stones & kinetic two-hop bounces)
// Dramatic storm of luminous faceted hailstones plunging steeply from dark storm clouds, smashing into
// each depth's ground plane with crystalline shard sparks and kinetic two-hop parabolic rebounds!
const float kHailDepth[3] = {0.62f, 0.88f, 1.22f};
const float kHailRad[3]   = {2.15f, 3.25f, 4.65f};
const float kHailTrail[3] = {18.f, 28.f, 40.f};
const float kHailAlpha[3] = {0.58f, 0.80f, 0.96f};

struct HailGeo { float r, trail[3], padX, padY, cw, ch, rowY; };
void hailGeo(float s, float slant, HailGeo out[3], float& atlasW, float& atlasH) {
    float y = 0.f; atlasW = 0.f;
    for (int L = 0; L < 3; ++L) {
        HailGeo& G = out[L]; const float d = kHailDepth[L];
        G.r = std::max(1.8f, kHailRad[L] * s);
        for (int v = 0; v < 3; ++v) G.trail[v] = std::max(8.f, std::floor(kHailTrail[v] * s * d));
        G.padX = std::ceil(G.r * 2.2f) + 4.f; G.padY = std::ceil(G.r * 2.2f) + 4.f;
        G.cw = std::ceil(2.f * G.padX + std::fabs(slant) * G.trail[2]);
        G.ch = std::ceil(G.trail[2] + 2.f * G.padY);
        G.rowY = y; y += G.ch; atlasW = std::max(atlasW, G.cw * 3.f);
    }
    atlasH = y;
}

void drawFacetedStone(Gfx& g, float cx, float cy, float r, float spin, const Color& core, const Color& ice, float op, bool dark) {
    if (op < 0.01f || r < 0.8f) return;
    // Slight cool refraction halo
    g.glow(cx, cy, r * 2.1f, ice, op * (dark ? 0.42f : 0.48f));
    // Faceted irregular 6-sided ice crystal silhouette
    static thread_local std::vector<Pt> poly(6);
    for (int j = 0; j < 6; ++j) {
        const float ang = spin + j * (PI_F / 3.f);
        const float rr = r * (0.86f + 0.20f * ((j & 1) ? 1.f : 0.35f));
        poly[j] = {cx + rr * std::cos(ang), cy + rr * std::sin(ang)};
    }
    g.fillPolygon(poly, withA(ice, op * (dark ? 0.85f : 0.92f)));
    // Bright icy core shifted subtly toward top-left light source
    g.fillCircle(cx - r * 0.14f, cy - r * 0.16f, r * 0.68f, withA(core, op * 0.96f));
    // Specular diamond catchlight
    if (r >= 2.2f) g.fillCircle(cx - r * 0.28f, cy - r * 0.28f, r * 0.28f, withA(Color{1.f, 1.f, 1.f, 1.f}, op));
}

Bitmap buildHailAtlas(Gfx& g, const Frame& F, float s, float slant, bool dark) {
    HailGeo HG[3]; float aw = 0.f, ah = 0.f;
    hailGeo(s, slant, HG, aw, ah);
    if (aw < 4 || ah < 4 || aw > 4096 || ah > 4096 || !g.beginSprite((int)aw, (int)ah)) return Bitmap{};
    for (int L = 0; L < 3; ++L) {
        const HailGeo& G = HG[L];
        for (int v = 0; v < 3; ++v) {
            g.setOffset(v * G.cw, G.rowY);
            const float len = G.trail[v], dx = slant * len;
            const float hy = G.ch - G.padY, hx = dx >= 0 ? G.padX + dx : G.padX, ty = hy - len, tx = hx - dx;
            const float wTail = G.r * 0.24f, wHead = G.r * 0.72f;
            // Fast icy motion trail behind the plunging hailstone
            g.fillPolyVGradient({{tx - wTail, ty}, {tx + wTail, ty}, {hx + wHead, hy}, {hx - wHead, hy}}, ty, hy,
                                withA(F.hailIce, 0.f), withA(F.hailIce, dark ? 0.42f : 0.48f));
            const float stoneR = G.r * (0.82f + 0.18f * v);
            drawFacetedStone(g, hx, hy, stoneR, v * 0.9f + L * 0.4f, F.hailCore, F.hailIce, 1.0f, dark);
        }
    }
    g.setOffset(0, 0);
    return g.endSprite();
}

void drawHail(Ctx& c, const Frame& F, float k, float side, float q, bool skyBg = false) {
    Gfx& g = c.g; App& a = c.a; const float s = c.s, W = c.W, H = c.H;
    if (W < 4.f || H < 4.f) return;
    const double t = a.now;
    const bool dark = c.P.dark;
    const float slant = std::round(side * (0.28f + 0.14f * clamp01(windKmh(a) / 50.f)) * 20.f) / 20.f;
    const int sq = (int)std::lround(std::log2(std::max(0.05f, s)) * 5.f);
    const uint64_t key = mixKey(mixKey(q3(F.hailCore), mixKey(q3(F.hailIce), (uint64_t)(sq + 192) * 2 + (dark ? 1u : 0u))),
                                mixKey(6500, (uint64_t)(std::lround(slant * 20.f) + 64)));
    const bool ok = ensure(g, gHailStone, key, s, [&] {
        Bitmap b = buildHailAtlas(g, F, s, slant, dark);
        if (b.bmp) { gHailS = s; gHailSlant = slant; }
        return b;
    });
    if (!ok || gHailS <= 0.f) return;
    if (!skyBg) {
        g.vGradientRect(0.f, H * 0.78f, W, H * 0.22f, withA(F.hailIce, 0.f), withA(F.hailIce, al((dark ? 0.15f : 0.18f) * k)));
    }
    HailGeo HG[3]; float aw = 0.f, ah = 0.f;
    hailGeo(gHailS, gHailSlant, HG, aw, ah);
    const float bs = gHailSlant;
    const float density = (skyBg ? 0.56f : 1.0f) * q;
    const int n = (int)std::lround((74 + 136 * k) * density);
    constexpr float BOUNCE1 = 0.36f, BOUNCE2 = 0.20f, BOUNCE_TOT = BOUNCE1 + BOUNCE2;

    for (int L = 0; L < 3; ++L) {
        const HailGeo& G = HG[L];
        const float d = kHailDepth[L], yg = H * kGround[L];
        for (int i = L; i < n; i += 3) {
            const float h1 = hashf(i, 741), h2 = hashf(i, 742), h3 = hashf(i, 743), h4 = hashf(i, 744);
            const float sp = (700.f + 420.f * h1) * s * d;
            const float span = yg + sp * (BOUNCE_TOT + 0.18f);
            const float pos = loopPos(t, sp, h2 * span, span);
            const float x0 = h3 * (W + H * 0.65f) - H * 0.30f;
            const int v = std::min(2, (int)(h4 * 3.f));
            const float a0 = kHailAlpha[L] * (0.74f + 0.32f * h4) * k;

            if (pos < yg) {
                const float xh = x0 + bs * pos;
                if (xh < -50.f * s || xh > W + 50.f * s) continue;
                const float hx = bs * G.trail[v] >= 0.f ? G.padX + bs * G.trail[v] : G.padX;
                const float hy = G.ch - G.padY;
                g.drawBitmapSub(gHailStone.bmp, std::round(xh - hx), std::round(pos - hy), v * G.cw, G.rowY, G.cw, G.ch, clamp01(al(a0)));
            } else {
                const float et = (pos - yg) / sp;
                if (et >= BOUNCE_TOT) continue;
                const float xi = x0 + bs * yg;
                if (xi < -80.f * s || xi > W + 80.f * s) continue;
                const float stoneR = G.r * (0.78f + 0.18f * v);

                // Impact shock-ring & icy crystalline shard burst right at touchdown (0 .. 0.15 s)
                if (et < 0.15f) {
                    const float u = et / 0.15f, fl = 1.f - u;
                    g.glow(xi, yg, stoneR * (2.5f + 2.5f * u), F.hailCore, al(0.65f * fl * k));
                    const float rr = (3.f + 22.f * std::pow(u, 0.55f)) * s * d;
                    g.strokeEllipse(xi, yg, rr, rr * 0.28f, withA(F.hailIce, al(0.55f * fl * k)), std::max(1.0f, 1.35f * s * d));
                    const int ns = L == 0 ? 2 : 4;
                    for (int m = 0; m < ns; ++m) {
                        const float dir = (m & 1) ? 1.f : -1.f;
                        const float svx = dir * (60.f + 115.f * hashf(i * 11 + m, 745)) * s * d + side * 25.f * s;
                        const float svy = (160.f + 160.f * hashf(i * 11 + m, 746)) * s * d, sgr = 1450.f * s * d;
                        const float spx = xi + svx * et, spy = yg - svy * et + 0.5f * sgr * et * et;
                        if (spy <= yg) g.fillCircle(spx, spy, std::max(1.0f, (1.1f + 0.9f * hashf(i * 11 + m, 747)) * s * d), withA(F.hailCore, al(0.85f * fl * k)));
                    }
                }

                // Energetic two-hop parabolic hailstone bounce!
                const float bDir = (hashf(i, 748) < 0.72f) ? side : -side;
                const float vxB = bDir * (75.f + 110.f * hashf(i, 749)) * s * d;
                const float vy1 = (280.f + 165.f * hashf(i, 750)) * s * d;
                const float gr = (2.f * vy1) / BOUNCE1;
                float bx = xi, by = yg, bop = a0;
                if (et < BOUNCE1) {
                    bx = xi + vxB * et;
                    by = yg - vy1 * et + 0.5f * gr * et * et;
                    bop = a0 * (1.f - 0.18f * (et / BOUNCE1));
                } else {
                    const float e2 = et - BOUNCE1;
                    const float vy2 = vy1 * 0.46f;
                    const float gr2 = (2.f * vy2) / BOUNCE2;
                    bx = xi + vxB * BOUNCE1 + vxB * 0.65f * e2;
                    by = yg - vy2 * e2 + 0.5f * gr2 * e2 * e2;
                    bop = a0 * 0.82f * (1.f - e2 / BOUNCE2);
                }
                const float spin = et * (14.f + 10.f * h1) * bDir;
                drawFacetedStone(g, bx, std::min(yg, by), stoneR * 0.92f, spin, F.hailCore, F.hailIce, al(bop), dark);
            }
        }
    }
}

void drawFrontHail(Ctx& c, float k, float q) {
    Gfx& g = c.g; App& a = c.a; const float s = c.s, W = c.W, H = c.H;
    if (!gHailStone.bmp.bmp || gHailS <= 0.f) return;
    HailGeo HG[3]; float aw = 0.f, ah = 0.f;
    hailGeo(gHailS, gHailSlant, HG, aw, ah);
    const HailGeo& G = HG[2]; const float bs = gHailSlant;
    const int m = (int)std::lround((4 + 6 * k) * q);
    for (int i = 0; i < m; ++i) {
        const float sp = (920.f + 340.f * hashf(i, 755)) * s;
        const float span = H + G.trail[2] + 80.f * s;
        const float pos = loopPos(a.now, sp, hashf(i, 756) * span, span);
        const float x0 = hashf(i, 757) * (W + H * 0.65f) - H * 0.30f;
        const float xh = x0 + bs * pos;
        if (xh < -50.f * s || xh > W + 50.f * s) continue;
        const int v = hashf(i, 758) > 0.5f ? 2 : 1;
        const float hx = bs * G.trail[v] >= 0.f ? G.padX + bs * G.trail[v] : G.padX, hy = G.ch - G.padY;
        g.drawBitmapSub(gHailStone.bmp, std::round(xh - hx), std::round(pos - hy), v * G.cw, G.rowY, G.cw, G.ch, clamp01(al(0.55f * k)));
    }
}

// ---------------------------------------------------------------- gale (roaring artistic coastal windstorm)
// Elevates Meridian's signature painterly wind language into an exhilarating, dramatic storm gale:
//   - Cached aerodynamic gust-shear mist plumes racing across the sky during peak wind surges
//   - 12..17 sweeping double-layered howling gale ribbons with tight swirling vortex curls
//   - Swirling clusters of tumbling leaves caught in roaring eddies
//   - Swift motion-streaked spray & wind-dust filaments giving unmistakable kinetic velocity
bool galeGustReady(Ctx& c, const Frame& F) {
    const float W = c.W, H = c.H;
    uint64_t key = mixKey(mixKey(q3(F.galeHaze), q3(F.windHead)), mixKey(qsz(W), qsz(H)));
    gGaleGustH = 0.16f * H;
    return ensure(c.g, gGaleGust, key, 0.f, [&] {
        const int sw = std::max(48, (int)(W * 0.80f * SPR));
        const int sh = std::max(18, (int)(gGaleGustH * SPR));
        if (!c.g.beginSprite(sw, sh)) return Bitmap{};
        for (int m = 0; m < 7; ++m) {
            const float u = (m + 0.5f) / 7.f;
            const float env = std::sin(u * PI_F);
            const float rx = sw * (0.18f + 0.14f * env);
            const float ry = sh * (0.14f + 0.12f * env);
            const float cx = std::max(rx * 1.04f, std::min(sw - rx * 1.04f, sw * (0.10f + 0.80f * u)));
            const float cy = std::max(ry * 1.04f, std::min(sh - ry * 1.04f, sh * 0.5f + (hashf(m, 771) - 0.5f) * sh * 0.28f));
            softPlume(c.g, cx, cy, rx, ry, 0.f, 0.f, F.galeHaze, F.galeHaze, 0.26f * env);
            softPlume(c.g, cx, cy - ry * 0.15f, rx * 0.72f, ry * 0.36f, 0.f, 0.f, F.windHead, F.galeHaze, 0.16f * env);
        }
        return c.g.endSprite();
    });
}

void drawGale(Ctx& c, const Frame& F, float k, float side, float q) {
    Gfx& g = c.g; App& a = c.a; const float s = c.s, W = c.W, H = c.H;
    if (W < 4.f || H < 4.f) return;
    const double T = std::max(0.0, a.fxGaleT), t = a.now;
    const bool left = side < 0.f;
    auto mx = [&](float x) { return left ? W - x : x; };
    const float gust = 0.5f + 0.5f * std::sin(ph(t, 0.68) + 0.9f * std::sin(ph(t, 0.27)));
    const float surge = 0.68f + 0.52f * gust * gust;
    const float gs = std::min(gFx, 1.6f);

    // A. Racing aerodynamic gust-shear veils surging across the sky (cached half-res sprite -> ultra fast!)
    if (galeGustReady(c, F)) {
        const Bitmap& bm = gGaleGust.bmp;
        const int ng = std::max(3, (int)std::lround(5.f * q));
        for (int i = 0; i < ng; ++i) {
            const float h1 = hashf(i, 781), h2 = hashf(i, 782), h3 = hashf(i, 783);
            const float bw = (bm.w / SPR) * (0.95f + 0.55f * h1);
            const float bh = (bm.h / SPR) * (0.72f + 0.45f * h2);
            const float spanX = W + bw;
            const float spX = (360.f + 260.f * h3) * s;
            const float px = loopPos(T, side * spX, h2 * spanX, spanX) - bw;
            const float py = H * (0.10f + 0.80f * h1) + std::sin(ph(T, 0.75 + 0.4 * h2, h3 * 6.28)) * 18.f * s;
            const float op = clamp01((c.P.dark ? 0.26f : 0.24f) * k * (0.65f + 0.45f * surge) * (0.7f + 0.3f * h3) * gs);
            g.drawBitmapRect(bm, px, py - bh * 0.5f, bw, bh, op);
        }
    }

    // B. Sweeping multi-depth howling gale ribbons (taut, dramatic arcs + tight spiraling gale eddies)
    Pt pts[WPTS + 2]; float vv[WPTS + 2];
    WindPath P;
    static std::vector<Pt> gPoly;
    if (gPoly.capacity() < (size_t)(WPTS * 2)) gPoly.reserve((size_t)(WPTS * 2));
    const int n = std::max(5, (int)std::lround((10.f + 8.f * k) * q));
    for (int i = 0; i < n; ++i) {
        const int L = i % 3; const float d = kWDepth[L] * 1.08f;
        const double per = 1.45 + 0.95 * hashf(i, 791);
        const double tt = T / per + hashf(i, 792);
        const double cyc = std::floor(tt);
        const float u = (float)(tt - cyc);
        const int sd = i * 8831 + (int)std::fmod(cyc, 65521.0);
        const float len = W * (0.30f + 0.28f * hashf(sd, 793)) * d;
        const float x0 = W * (-0.12f + 0.82f * hashf(sd, 794)) - 0.35f * len + u * 0.22f * W * d;
        const float y0 = H * (0.06f + 0.88f * hashf(sd, 795));
        const float amp = (9.f + 22.f * hashf(sd, 796)) * s * d;
        const float wl = (340.f + 360.f * hashf(sd, 797)) * s;
        const float phase = hashf(sd, 798) * 6.2832f - 3.2f * u;
        const float bow = (hashf(sd, 799) - 0.42f) * 0.14f * H;
        const bool curl = hashf(sd, 800) < 0.64f;
        const float curlR = curl ? (16.f + 22.f * hashf(sd, 801)) * s * d : 0.f;
        const float turns = 0.88f + 0.42f * hashf(sd, 802);
        const float curlDir = hashf(sd, 803) < 0.72f ? -1.f : 1.f;
        windPath(P, x0, y0, len, amp, wl, phase, bow, curlR, turns, curlDir);
        const float head = easeOut3(u / 0.54f), tail = easeInOut((u - 0.24f) / 0.76f);
        const int m = windSlice(P, tail, head, pts, vv);
        if (m < 2) continue;
        for (int j = 0; j < m; ++j) pts[j].x = mx(pts[j].x);
        const float life = smoothstep(0.f, 0.08f, u) * (1.f - smoothstep(0.84f, 1.f, u));
        const float al0 = kWAlpha[L] * k * life * (0.72f + 0.38f * surge) * (0.76f + 0.24f * hashf(sd, 804));
        if (al0 < 0.008f) continue;
        const float w = std::max(1.3f, (kWWidth[L] * 1.25f) * s * (0.90f + 0.28f * surge));
        // Wide translucent gale wake sheath + taut luminous core
        windRibbon(g, pts, vv, m, w * 2.35f, withA(F.wind, al(0.26f * al0)), gPoly);
        windRibbon(g, pts, vv, m, w * 0.92f, withA(F.wind, al(0.62f * al0)), gPoly);
        if (L >= 1) windRibbon(g, pts, vv, m, w * 0.40f, withA(F.windHead, al(0.95f * al0)), gPoly);
        const float tip = 1.f - smoothstep(0.42f, 0.56f, u);
        if (tip > 0.01f) {
            const float ta = al(0.82f * al0 * tip);
            if (L == 2) g.glow(pts[m - 1].x, pts[m - 1].y, w * 3.4f, F.windHead, ta * 0.26f);
            g.fillCircle(pts[m - 1].x, pts[m - 1].y, w * 0.48f, withA(F.windHead, ta));
        }
    }

    // C. Swirling leaf whirlwinds whipped across the gale (suppressed during freezing snow/sleet/blizzard)
    const float leafMask = 1.f - clamp01(std::max(std::max(a.qfx[6], a.qfx[7]), a.qfx[9]) * 2.f);
    const int nl = std::max(0, (int)std::lround((8.f + 7.f * k) * q * leafMask));
    static std::vector<Pt> leaf(8);
    for (int i = 0; i < nl; ++i) {
        const double per = 3.2 + 2.4 * hashf(i, 821);
        const double tt = T / per + hashf(i, 822);
        const double cyc = std::floor(tt);
        const float u = (float)(tt - cyc);
        const int sd = i * 9187 + (int)std::fmod(cyc, 65521.0);
        const float d = 0.72f + 0.52f * hashf(sd, 823);
        const float mg = 55.f * s;
        const float e = hashf(sd, 824) * 6.2832f;
        // Sweeping gust path with energetic spiral vortex loops
        float x = -mg + u * (W + 2.f * mg) + 28.f * s * d * std::sin(2.f * PI_F * 2.6f * u + e);
        float y = H * (0.10f + 0.78f * hashf(sd, 825)) + (hashf(sd, 826) - 0.5f) * 0.16f * H * u
                + (34.f + 56.f * hashf(sd, 827)) * s * d * std::sin(2.f * PI_F * (1.5f + 1.0f * hashf(sd, 828)) * u + e)
                + 24.f * s * d * std::cos(2.f * PI_F * 2.6f * u + e);
        const float vis = smoothstep(-mg, 0.04f * W, x) * (1.f - smoothstep(0.96f * W, W + mg, x));
        if (vis < 0.02f) continue;
        const float la = al((0.40f + 0.30f * d) * k * vis);
        if (la < 0.008f) continue;
        x = mx(x);
        const float flip = std::cos(ph(t, 4.5 + 3.2 * hashf(sd, 829), e));
        const float spin = (u * (420.f + 520.f * hashf(sd, 830)) * (hashf(sd, 831) < 0.5f ? 1.f : -1.f)) * (PI_F / 180.f) * (left ? -1.f : 1.f);
        const float ln = (7.5f + 6.0f * hashf(sd, 832)) * s * d;
        const float wd = ln * 0.40f * (0.16f + 0.84f * std::fabs(flip));
        const float cs = std::cos(spin), sn = std::sin(spin);
        for (int j = 0; j < 8; ++j) {
            const int jj = j < 5 ? j : 8 - j;
            const float lx = (jj / 4.f - 0.5f) * ln;
            const float ly = (j < 5 ? -1.f : 1.f) * wd * std::pow(std::max(0.f, std::sin(PI_F * jj / 4.f)), 0.9f);
            leaf[j] = {x + lx * cs - ly * sn, y + lx * sn + ly * cs};
        }
        const Color lc = lerp(F.leaf, F.windHead, 0.34f * smoothstep(-0.25f, 0.25f, flip));
        g.fillPolygon(leaf, withA(lc, la));
    }

    // D. Fast motion-streaked spray & wind-dust filaments zipping across the gust
    const int ns = std::max(10, (int)std::lround((18.f + 16.f * k) * q));
    for (int i = 0; i < ns; ++i) {
        const float h1 = hashf(i, 841), h2 = hashf(i, 842), h3 = hashf(i, 843), h4 = hashf(i, 844);
        const double spanD = 1440.0 + 180.0;
        const double posD = loopPos(T, 340.0 * (0.65 + 0.75 * h1), h2 * spanD, spanD);
        const float x = mx((float)(posD / spanD) * (W + 160.f * s) - 80.f * s);
        const float y = H * (0.05f + 0.90f * h3) + std::sin(ph(T, 1.4 + 0.9 * h4, h2 * 6.28)) * 20.f * s;
        const float vis = smoothstep(-0.02f * W, 0.06f * W, x) * (1.f - smoothstep(0.94f * W, 1.02f * W, x));
        if (vis < 0.02f) continue;
        const float ma = al((0.20f + 0.28f * h4) * k * vis * (0.7f + 0.35f * surge));
        if (ma < 0.006f) continue;
        const float slen = (12.f + 18.f * h1) * s * (0.8f + 0.35f * surge);
        const float dy = (h4 - 0.45f) * 3.5f * s;
        g.line(x - side * slen, y - dy, x, y, withA(F.windHead, ma), std::max(0.9f, (0.9f + 0.7f * h4) * s), true);
    }
}

void drawFrontGale(Ctx& c, const Frame& F, float k, float side, float q) {
    Gfx& g = c.g; App& a = c.a; const float s = c.s, W = c.W, H = c.H;
    const double T = std::max(0.0, a.fxGaleT);
    const bool left = side < 0.f;
    auto mx = [&](float x) { return left ? W - x : x; };
    Pt pts[WPTS + 2]; float vv[WPTS + 2];
    WindPath P;
    static std::vector<Pt> fgPoly;
    if (fgPoly.capacity() < (size_t)(WPTS * 2)) fgPoly.reserve((size_t)(WPTS * 2));
    const int n = std::max(1, (int)std::lround((2.f + 1.f * k) * q));
    for (int i = 0; i < n; ++i) {
        const double per = 2.0 + 1.1 * hashf(i, 851);
        const double tt = T / per + hashf(i, 852);
        const double cyc = std::floor(tt);
        const float u = (float)(tt - cyc);
        const int sd = i * 6131 + (int)std::fmod(cyc, 65521.0);
        const float len = W * (0.34f + 0.22f * hashf(sd, 853));
        const float x0 = W * (-0.08f + 0.72f * hashf(sd, 854)) - 0.3f * len + u * 0.22f * W;
        const float y0 = H * (0.24f + 0.56f * hashf(sd, 855));
        windPath(P, x0, y0, len, 14.f * s, 420.f * s, hashf(sd, 856) * 6.28f - 3.f * u, (hashf(sd, 857) - 0.5f) * 0.10f * H, 18.f * s, 0.85f, -1.f);
        const int m = windSlice(P, easeInOut((u - 0.25f) / 0.75f), easeOut3(u / 0.55f), pts, vv);
        if (m < 2) continue;
        for (int j = 0; j < m; ++j) pts[j].x = mx(pts[j].x);
        const float life = smoothstep(0.f, 0.10f, u) * (1.f - smoothstep(0.84f, 1.f, u));
        const float wa = 0.26f * k * life;
        if (wa < 0.006f) continue;
        windRibbon(g, pts, vv, m, 3.2f * s, withA(F.wind, al(wa * 0.60f)), fgPoly);
        windRibbon(g, pts, vv, m, 1.3f * s, withA(F.windHead, al(wa * 0.90f)), fgPoly);
    }
}

// ---------------------------------------------------------------- lightning
// A forked bolt every few seconds and a soft flash inside the cloud in between. Gentle: no full-screen flash.
// The bolts leave from the clouds: a cloud flashes from inside (sheet lightning), now and then a forked bolt comes down out of one, and the clouds
// beside it light up with it.
void drawLightning(Ctx& c, float k, bool tornadoOn, float k0, float heavy) {
    Gfx& g = c.g; App& a = c.a; const Palette& P = c.P; const float s = c.s, W = c.W, H = c.H;
    const double t = a.now;
    const Color bolt = P.dark ? lerp(P.ink, Color{1.f, 1.f, 1.f, 1.f}, 0.45f) : P.accent2;
    const int nc = std::max(1, (int)std::lround(5 + 7 * k0 + 2 * heavy));
    // the clouds a flash can come from: ones that are well on the screen and near enough to matter; cyc and salt pick one for each flash
    auto pick = [&](uint32_t cyc, int salt) {
        CloudAt best = cloudAt(c, 0, heavy); float bs = -1.f;
        for (int tr = 0; tr < 7; ++tr) {
            const CloudAt ca = cloudAt(c, (int)(hashf((int)(cyc * 13u + (uint32_t)tr), salt) * nc) % nc, heavy);
            const float cx = ca.x + ca.cw * 0.5f;
            const float sc = ca.vis * (0.55f + 0.22f * ca.layer) * (cx > 0.08f * W && cx < 0.92f * W ? 1.f : 0.3f);
            if (sc > bs) { bs = sc; best = ca; }
        }
        return best;
    };
    auto lit = [&](const CloudAt& ca, float on, float amt) {              // a cloud lit from inside, and the ones around it a little
        const float cx = ca.x + ca.cw * 0.5f;
        g.glow(cx, ca.y, ca.cw * 0.58f, bolt, al(0.34f * on * k * amt));
        g.glow(cx, ca.y, ca.cw * 0.95f, bolt, al(0.12f * on * k * amt));
    };
    {   // sheet lightning: a cloud flashes inside, a slow rolling flash
        const double per = 4.3; const uint32_t cyc = (uint32_t)(long long)std::floor(t / per); const double f = t / per - std::floor(t / per);
        if (hashf((int)cyc, 91) > 0.45f + 0.3f * (1.f - k)) {
            const float on = f < 0.07 ? 1.f : (f < 0.11 ? 0.3f : (f < 0.2 ? 0.65f : (f < 0.3 ? 0.2f : 0.f)));
            if (on > 0.f) {
                const CloudAt ca = pick(cyc, 92);
                lit(ca, on, 1.f);
                const CloudAt cb = pick(cyc, 95);
                if (cb.x != ca.x) lit(cb, on, 0.55f);
            }
        }
    }
    const double per = 7.0; const uint32_t cyc = (uint32_t)(long long)std::floor(t / per); const double f = t / per - std::floor(t / per);
    if (hashf((int)cyc, 82) < 0.40f + 0.35f * (1.f - k)) return;
    const float on = f < 0.04 ? 1.f : (f < 0.06 ? 0.f : (f < 0.10 ? 0.7f : (f < 0.14 ? 0.25f : 0.f)));
    if (on <= 0.f) return;
    const CloudAt src = pick(cyc, 83);
    lit(src, on, 1.3f);
    const float srcCx = src.x + src.cw * 0.5f;
    const float x0 = std::max(0.04f * W, std::min(0.96f * W, srcCx + (hashf((int)cyc, 88) - 0.5f) * src.cw * 0.35f));
    const float yA = std::min(H * 0.55f, src.y + src.cw * 0.05f);          // from the underside of the cloud
    const float yB = std::min(H * 0.92f, std::max(yA + H * 0.30f, H * (0.55f + 0.30f * hashf((int)cyc, 84))));
    const int N = 11;
    std::vector<Pt> pts, br;
    float x = x0;
    for (int i = 0; i <= N; ++i) { pts.push_back({x, yA + (yB - yA) * i / N}); x += (hashf((int)(cyc * 17u + (uint32_t)i), 85) - 0.5f) * 70.f * s; }
    const int bi = 5; float bx = pts[bi].x, by = pts[bi].y; const float bd = hashf((int)cyc, 86) < 0.5f ? -1.f : 1.f;
    for (int i = 0; i <= 5; ++i) { br.push_back({bx, by}); bx += bd * (10.f + 24.f * hashf((int)(cyc * 7u + (uint32_t)i), 87)) * s; by += (yB - yA) / N * 0.9f; }
    g.glow(x0, yA, W * 0.18f, bolt, al(0.30f * on * k));
    for (const auto* path : {&pts, &br}) {
        const float wf = path == &pts ? 1.f : 0.6f;
        g.polyline(*path, false, withA(bolt, al(0.08f * on * k)), 11.f * s * wf, true);
        g.polyline(*path, false, withA(bolt, al(0.22f * on * k)), 4.5f * s * wf, true);
        g.polyline(*path, false, withA(bolt, al(0.70f * on * k)), std::max(1.4f, 1.8f * s * wf), true);
    }
}

// ---------------------------------------------------------------- tornado
void drawTornado(Ctx& c, float k, float side, float q) {
    Gfx& g = c.g; App& a = c.a; const Palette& P = c.P; const float s = c.s, W = c.W, H = c.H;
    const double t = a.now;
    const bool portrait = W < H * 1.15f;
    Color ink = tint(P, lerp(P.ink, P.ink2, 0.25f));
    Color dust = tint(P, P.ink2);

    const float scale = portrait ? W / 1100.f : H / 1080.f;
    const float xc0 = W * (portrait ? 0.74f : 0.80f);
    const float yTop = H * 0.17f, yBot = H * 0.97f, hf = yBot - yTop;
    const float wTop = 205.f * scale * (portrait ? 0.8f : 1.f), wBot = 13.f * scale;
    const float sway = std::sin(ph(t, 0.23)) * 0.035f * W + std::sin(ph(t, 0.51) + 1.f) * 0.012f * W;
    auto centreX = [&](float u) {
        return xc0 + sway * (0.25f + 0.75f * u) + std::sin(ph(t, 0.9) + u * 5.2f) * 22.f * scale * (0.35f + u) - side * 0.05f * W * u * u;
    };
    auto halfW = [&](float u) { return wBot + (wTop - wBot) * std::pow(1.f - u, 1.35f); };

    // the dark cloud it hangs from
    for (int j = 0; j < 5; ++j) {
        const float u = (j + 0.5f) / 5.f;
        const float px = xc0 + sway * 0.2f + (u - 0.5f) * wTop * 4.2f + std::sin(ph(t, 0.07) + j) * 12.f * scale;
        const float r = wTop * (0.85f + 0.55f * std::sin(u * PI_F)) * (0.9f + 0.2f * hashf(j, 31));
        g.glow(px, yTop - r * 0.12f * std::sin(u * PI_F) + (hashf(j, 32) - 0.5f) * 24.f * scale, r * 1.5f, ink, al(0.22f * k));
    }
    // body of the funnel: soft overlapping puffs along its length (no hard outline), melting into the cloud at the top
    {
        const int N = 16;
        for (int i = 0; i < N; ++i) {
            const float u = (i + 0.5f) / N, cx = centreX(u), hw = halfW(u), cy = yTop + hf * u;
            const float fade = smoothstep(0.f, 0.22f, u);
            g.glow(cx, cy, std::max(hw * 1.8f, 44.f * scale), ink, al(0.13f * k * fade));
            g.glow(cx, cy, std::max(hw * 1.0f, 22.f * scale), ink, al(0.10f * k * fade));
        }
    }
    // swirling bands, spinning faster toward the ground
    {
        const int rings = std::max(8, (int)std::lround(14 * q));
        for (int i = 0; i < rings; ++i) {
            const float u = (i + 0.5f) / rings;
            const float cx = centreX(u), hw = halfW(u), cy = yTop + hf * u, ry = std::max(2.f * s, hw * 0.20f);
            const float spin = ph(t, 1.5 + 3.0 * u, hashf(i, 41) * 6.28);
            for (int qd = 0; qd < 2; ++qd) {
                const float a0 = spin + qd * PI_F, span = 0.8f + 0.5f * hashf(i * 3 + qd, 42);
                Pt pts[7]; float depth = 0.f;
                for (int m = 0; m < 7; ++m) { const float th = a0 + span * m / 6.f; pts[m] = {cx + hw * std::cos(th), cy + ry * std::sin(th)}; depth += std::sin(th); }
                depth /= 7.f;
                const float al0 = (0.12f + 0.16f * (0.5f + 0.5f * depth)) * k * (0.6f + 0.4f * u) * smoothstep(0.f, 0.16f, u);
                g.polyline(std::vector<Pt>(pts, pts + 7), false, withA(ink, al(al0)), std::max(1.3f, (1.7f + 1.5f * (1.f - u)) * s), true);
            }
        }
    }
    // debris circling the funnel
    {
        const int N = std::max(12, (int)std::lround(28 * q));
        for (int i = 0; i < N; ++i) {
            const float u = 0.08f + 0.9f * hashf(i, 51);
            const float cx = centreX(u), hw = halfW(u) * (1.05f + 0.5f * hashf(i, 52)), cy = yTop + hf * u, ry = std::max(2.f * s, hw * 0.20f);
            const float th = ph(t, (1.1 + 2.6 * u) * (0.7 + 0.6 * hashf(i, 53)), hashf(i, 54) * 6.28);
            const float front = 0.5f + 0.5f * std::sin(th);
            g.fillCircle(cx + hw * std::cos(th), cy + ry * std::sin(th) - 10.f * s * hashf(i, 55) * (1.f - u), (1.6f + 2.2f * hashf(i, 56)) * s, withA(ink, al((0.16f + 0.26f * front) * k)));
        }
    }
    // dust thrown up where it touches the ground
    {
        const float cx = centreX(1.f);
        g.glow(cx, yBot, 200.f * scale, dust, al((0.30f + 0.06f * std::sin(ph(t, 1.3))) * k));
        for (int i = 0; i < 8; ++i) {
            const float pp = (float)std::fmod(t * (0.3 + 0.25 * hashf(i, 61)) + hashf(i, 62), 1.0);
            const float dir = (hashf(i, 63) < 0.5f ? -1.f : 1.f);
            g.glow(cx + dir * (10.f + 130.f * pp) * scale * (0.5f + hashf(i, 64)), yBot - (6.f + 70.f * pp * (0.4f + hashf(i, 65))) * scale, (30.f + 40.f * pp) * scale, dust, al(0.16f * (1.f - pp) * k));
        }
    }
}

// ---------------------------------------------------------------- Aurora Borealis / Australis (3.0.0 "Aurora")
// Museum-grade multi-curtain geomagnetic aurora built from 3 cached vertical magnetic-ray sprites:
//   - Variant 0: Luminous oxygen-emerald lower curtain rim fading upward through seafoam-teal into violet-indigo
//   - Variant 1: High-altitude ethereal violet-amethyst & cyan crown veil
//   - Variant 2: High-energy emerald fold accented by a rose-magenta nitrogen lower fringe
// Drawn as 3 depth-layered 3D perspective serpentine ribbons with authentic optical folding & magnetic zenith convergence.
bool auroraRayReady(Ctx& c, int variant) {
    const Palette& P = c.P;
    const int emQ = (int)std::lround(P.ember * 8.f);
    const uint64_t key = mixKey((uint64_t)(8800 + variant * 13), (uint64_t)(emQ * 3 + (P.dark ? 1 : 0)));
    return ensure(c.g, gAuroraRay[variant], key, 1.f, [&]() -> Bitmap {
        const int bw = 76, bh = 360;
        if (!c.g.beginSprite(bw, bh)) return Bitmap{};
        Gfx& g = c.g;
        auto T = [&](unsigned hex) { return tint(P, rgb(hex)); };
        const float cx = bw * 0.5f;

        if (variant == 0) {
            // Primary Oxygen-Emerald -> Seafoam-Teal -> Violet Crown vertical curtain card
            const Color colBot = T(0x46FFB4);
            const Color colMid = T(0x28DFCF);
            const Color colHi  = T(0x6C66F5);
            const Color colTop = T(0xA858E8);
            // Soft broad vertical body
            g.radialEllipse(cx, bh * 0.78f, bw * 0.44f, bh * 0.16f, 0.f, bh * 0.04f, {
                {0.00f, withA(colBot, 0.72f)},
                {0.42f, withA(colMid, 0.40f)},
                {0.78f, withA(colMid, 0.10f)},
                {1.00f, withA(colBot, 0.00f)},
            }, 1.f);
            g.radialEllipse(cx, bh * 0.52f, bw * 0.42f, bh * 0.36f, 0.f, bh * 0.08f, {
                {0.00f, withA(colMid, 0.46f)},
                {0.48f, withA(lerp(colMid, colHi, 0.55f), 0.24f)},
                {0.82f, withA(colHi, 0.07f)},
                {1.00f, withA(colTop, 0.00f)},
            }, 1.f);
            g.radialEllipse(cx, bh * 0.26f, bw * 0.38f, bh * 0.24f, 0.f, bh * 0.04f, {
                {0.00f, withA(colTop, 0.24f)},
                {0.55f, withA(colHi, 0.10f)},
                {1.00f, withA(colTop, 0.00f)},
            }, 1.f);
            // Crisp magnetic-field vertical ray striations (authentic dancing pillars!)
            for (int r = 0; r < 11; ++r) {
                const float rx = cx + (hashf(r, 911) - 0.5f) * bw * 0.68f;
                const float rw = 2.0f + 3.4f * hashf(r, 912);
                const float topY = bh * (0.08f + 0.26f * hashf(r, 913));
                const float botY = bh * (0.76f + 0.14f * hashf(r, 914));
                const float midY = 0.5f * (topY + botY);
                const float rh = 0.5f * (botY - topY);
                const Color rCol = lerp(colBot, colMid, 0.35f + 0.45f * hashf(r, 915));
                g.radialEllipse(rx, midY, rw, rh, 0.f, rh * 0.32f, {
                    {0.00f, withA(colBot, 0.52f)},
                    {0.36f, withA(rCol, 0.28f)},
                    {0.72f, withA(colHi, 0.09f)},
                    {1.00f, withA(colTop, 0.00f)},
                }, 0.85f);
            }
        } else if (variant == 1) {
            // Distant High-Altitude Amethyst-Violet & Cerulean Veil
            const Color colBot = T(0x36DCDA);
            const Color colMid = T(0x785CF2);
            const Color colTop = T(0xCF5AC8);
            g.radialEllipse(cx, bh * 0.64f, bw * 0.46f, bh * 0.28f, 0.f, bh * 0.06f, {
                {0.00f, withA(colBot, 0.42f)},
                {0.50f, withA(colMid, 0.24f)},
                {1.00f, withA(colMid, 0.00f)},
            }, 1.f);
            g.radialEllipse(cx, bh * 0.36f, bw * 0.44f, bh * 0.34f, 0.f, bh * 0.05f, {
                {0.00f, withA(colMid, 0.36f)},
                {0.46f, withA(colTop, 0.22f)},
                {0.84f, withA(colTop, 0.06f)},
                {1.00f, withA(colTop, 0.00f)},
            }, 1.f);
            for (int r = 0; r < 9; ++r) {
                const float rx = cx + (hashf(r, 921) - 0.5f) * bw * 0.64f;
                const float rw = 2.4f + 4.0f * hashf(r, 922);
                const float rh = bh * (0.28f + 0.16f * hashf(r, 923));
                const float ry = bh * (0.42f + 0.14f * hashf(r, 924));
                g.radialEllipse(rx, ry, rw, rh, 0.f, rh * 0.22f, {
                    {0.00f, withA(colBot, 0.30f)},
                    {0.50f, withA(colMid, 0.18f)},
                    {0.85f, withA(colTop, 0.06f)},
                    {1.00f, withA(colTop, 0.00f)},
                }, 0.85f);
            }
        } else {
            // Near High-Energy Folding Curtain with Electric Emerald Rim + Rose-Magenta Nitrogen Fringe
            const Color colFringe = T(0xF562B4);
            const Color colRim    = T(0x64FFBA);
            const Color colMid    = T(0x22E8C0);
            const Color colTop    = T(0x7C60F6);
            // Crisp lower nitrogen rose-pink flash fringe
            g.radialEllipse(cx, bh * 0.88f, bw * 0.40f, bh * 0.075f, 0.f, 0.f, {
                {0.00f, withA(colFringe, 0.62f)},
                {0.52f, withA(colFringe, 0.24f)},
                {1.00f, withA(colFringe, 0.00f)},
            }, 1.f);
            // Intense luminous emerald folding core right above the fringe
            g.radialEllipse(cx, bh * 0.74f, bw * 0.42f, bh * 0.18f, 0.f, bh * 0.04f, {
                {0.00f, withA(colRim, 0.82f)},
                {0.44f, withA(colMid, 0.42f)},
                {0.82f, withA(colMid, 0.10f)},
                {1.00f, withA(colMid, 0.00f)},
            }, 1.f);
            g.radialEllipse(cx, bh * 0.42f, bw * 0.36f, bh * 0.32f, 0.f, bh * 0.08f, {
                {0.00f, withA(colMid, 0.38f)},
                {0.55f, withA(colTop, 0.18f)},
                {1.00f, withA(colTop, 0.00f)},
            }, 1.f);
            for (int r = 0; r < 10; ++r) {
                const float rx = cx + (hashf(r, 931) - 0.5f) * bw * 0.62f;
                const float rw = 1.8f + 3.0f * hashf(r, 932);
                const float rh = bh * (0.28f + 0.14f * hashf(r, 933));
                const float ry = bh * (0.56f + 0.12f * hashf(r, 934));
                g.radialEllipse(rx, ry, rw, rh, 0.f, rh * 0.36f, {
                    {0.00f, withA(colRim, 0.60f)},
                    {0.35f, withA(colMid, 0.30f)},
                    {0.78f, withA(colTop, 0.08f)},
                    {1.00f, withA(colTop, 0.00f)},
                }, 0.90f);
            }
        }
        return c.g.endSprite();
    });
}

bool auroraGlowReady(Ctx& c) {
    const Palette& P = c.P;
    const int gw = std::max(64, std::min(512, (int)std::lround(c.W * 0.25f)));
    const int gh = std::max(64, std::min(320, (int)std::lround(c.H * 0.25f)));
    const int emQ = (int)std::lround(P.ember * 8.f);
    const uint64_t key = mixKey(mixKey(8860, (uint64_t)((gw << 16) | gh)), (uint64_t)(emQ * 3 + (P.dark ? 1 : 0)));
    return ensure(c.g, gAuroraGlow, key, 1.f, [&]() -> Bitmap {
        if (!c.g.beginSprite(gw, gh)) return Bitmap{};
        Gfx& g = c.g;
        auto T = [&](unsigned hex) { return tint(P, rgb(hex)); };
        // Upper celestial emerald-teal atmospheric scatter
        g.radialEllipse(gw * 0.48f, gh * 0.24f, gw * 0.62f, gh * 0.36f, 0.f, 0.f, {
            {0.00f, withA(T(0x28D49E), 0.24f)},
            {0.48f, withA(T(0x22789A), 0.12f)},
            {1.00f, withA(T(0x162448), 0.00f)},
        }, 1.f);
        // Right upper violet-amethyst crown haze
        g.radialEllipse(gw * 0.76f, gh * 0.16f, gw * 0.42f, gh * 0.28f, 0.f, 0.f, {
            {0.00f, withA(T(0x8852E0), 0.18f)},
            {0.55f, withA(T(0x46308E), 0.07f)},
            {1.00f, withA(T(0x181A38), 0.00f)},
        }, 1.f);
        // Subtle emerald-cerulean luminance bounce along the distant horizon
        g.radialEllipse(gw * 0.50f, gh * 1.02f, gw * 0.68f, gh * 0.24f, 0.f, -gh * 0.04f, {
            {0.00f, withA(T(0x2CE0A8), 0.16f)},
            {0.55f, withA(T(0x1A6870), 0.06f)},
            {1.00f, withA(T(0x0E1E2C), 0.00f)},
        }, 1.f);
        return c.g.endSprite();
    });
}

void drawAurora(Ctx& c, float k, float q, bool skyOnly = false) {
    if (k < 0.01f) return;
    Gfx& g = c.g; App& a = c.a; const Palette& P = c.P; const float W = c.W, H = c.H;
    const float str = std::min(1.45f, gFx);
    const float themeA = (P.dark ? 1.0f : 0.52f) * (1.f - 0.35f * P.ember);
    const float master = clamp01(k * str * themeA * (skyOnly ? 0.62f : 1.0f));
    if (master < 0.015f) return;

    // 1. Cached upper-sky atmospheric glow & subtle horizon bounce
    if (auroraGlowReady(c)) {
        const float pulse = 0.88f + 0.12f * std::sin(ph(a.fxAuroraT, 0.24));
        g.drawBitmapRect(gAuroraGlow.bmp, 0.f, 0.f, W, H, clamp01(master * pulse));
    }

    if (!auroraRayReady(c, 0) || !auroraRayReady(c, 1) || !auroraRayReady(c, 2)) return;
    const double t = a.fxAuroraT;

    // Helper: draw one serpentine 3D-folded perspective aurora curtain ribbon
    auto drawRibbon = [&](int variant, int slices, float baseY, float spanH, float widthMul,
                          float ampX, float ampY, float freq, float speed, float phaseOff, float op, float foldStrength) {
        const Bitmap& bm = gAuroraRay[variant].bmp;
        const int N = std::max(14, (int)std::lround(slices * (0.55f + 0.45f * q)));
        for (int i = 0; i < N; ++i) {
            const float u = (float)i / (float)std::max(1, N - 1);
            const float env = std::pow(std::sin(u * PI_F), 0.62f);
            if (env < 0.03f) continue;

            // 3D serpentine ribbon curve with horizontal fold compression (creates authentic luminous double-pleats!)
            const float w1 = std::sin(u * freq + ph(t, speed, phaseOff));
            const float w2 = std::sin(u * (freq * 1.85f) - ph(t, speed * 0.72, phaseOff * 1.7 + 1.1));
            const float w3 = std::cos(u * (freq * 0.65f) + ph(t, speed * 0.42, phaseOff * 0.6 + 2.4));

            // Horizontal perspective fold: shifts x non-linearly so adjacent ray cards bunch at the fold crests
            const float foldX = foldStrength * W * (0.075f * w1 + 0.038f * w2);
            const float px = W * (-0.06f + 1.12f * u) + foldX + ampX * W * w3;
            const float py = H * baseY + ampY * H * (0.58f * w1 + 0.28f * w2 - 0.22f * std::sin(u * PI_F));

            // Perspective curtain height & gentle magnetic-zenith tilt fanning toward upper center
            const float rayH = H * spanH * (0.78f + 0.34f * (0.5f + 0.5f * w2)) * (0.72f + 0.28f * env);
            const float rayW = (W / (float)N) * widthMul * (0.86f + 0.28f * (0.5f + 0.5f * w1));
            const float zenithTilt = ((px - W * 0.50f) / std::max(1.f, W)) * 18.f + w2 * 3.5f;

            // Travelling geomagnetic shimmer wave gliding smoothly along the magnetic field lines
            const float wave = 0.56f + 0.44f * std::sin(u * 13.5f - ph(t, 1.05 + 0.35 * variant, phaseOff));
            const float foldBoost = 1.f + 0.42f * std::max(0.f, std::cos(u * freq + ph(t, speed, phaseOff)));
            const float sliceA = clamp01(op * master * env * (0.68f + 0.32f * wave) * foldBoost);
            if (sliceA < 0.012f) continue;

            // Anchor rotation near the bright lower curtain edge so upper rays fan toward the magnetic zenith
            const float dx = px - rayW * 0.5f;
            const float dy = py - rayH * 0.82f;
            g.drawBitmapRot(bm, dx, dy, rayW, rayH, px, py, zenithTilt, sliceA, false);
        }
    };

    // Layer 0: Broad distant violet-cyan upper canopy veil
    drawRibbon(1, 24, 0.30f, 0.46f, 3.1f, 0.05f, 0.075f, 3.6f, 0.14f, 0.4f, 0.42f, 0.70f);
    // Layer 1: Sovereign sweeping serpentine emerald-teal primary curtain (main centerpiece!)
    drawRibbon(0, 36, 0.42f, 0.52f, 2.75f, 0.04f, 0.095f, 4.8f, 0.22f, 1.9f, 0.62f, 1.15f);
    // Layer 2: Near electric folding emerald + rose-magenta nitrogen-fringe ribbon
    if (!skyOnly) {
        drawRibbon(2, 26, 0.50f, 0.42f, 2.50f, 0.06f, 0.085f, 5.8f, 0.28f, 3.7f, 0.46f, 1.25f);
    }
}

} // namespace

// ---------------------------------------------------------------- foreground weather veil in front of the Quiet clock
void drawQuietFront(Ctx& c, float midY, float clockH) {
    Gfx& g = c.g; App& a = c.a; const float s = c.s, W = c.W, H = c.H;
    const float k = a.qfx[0];
    if (!a.st.quietFx) return;
    gFx = std::max(0.4f, std::min(2.f, a.st.quietStrength));
    budgetTick(a); trackSize(c);
    const float q = std::max(0.3f, std::min(1.f, a.fxQuality));
    const float side = windSide(a);
    const float heavy = heavyOf(a);
    const float calmFrontSnow = a.qfx[6] * (1.f - 0.90f * a.qfx[7]);
    const float fogFront = a.qfx[5] * (1.f - 0.85f * a.qfx[7]);
    const bool needFrame = (calmFrontSnow > 0.02f || a.qfx[7] > 0.02f || a.qfx[9] > 0.02f || a.qfx[11] > 0.02f || fogFront > 0.06f || k >= 0.02f);
    Frame F;
    if (needFrame) F = makeFrame(c, heavy);

    if (a.qfx[2] > 0.02f) drawFrontDrops(c, a.qfx[2], side, q);         // a few raindrops in front of the time
    if (a.qfx[8] > 0.02f) drawFrontDrizzle(c, a.qfx[8], q);             // whisper-fine drizzle filaments
    if (a.qfx[9] > 0.02f) drawFrontSleet(c, F, a.qfx[9], side, q);      // icy sleet needles & wet flakes
    if (a.qfx[10] > 0.02f) drawFrontHail(c, a.qfx[10], q);              // bold plunging hailstones
    if (calmFrontSnow > 0.02f) drawFrontSnow(c, F, calmFrontSnow, side, q);
    if (a.qfx[7] > 0.02f) drawFrontBlizzard(c, F, a.qfx[7], side, q);
    if (a.qfx[11] > 0.02f) drawFrontGale(c, F, a.qfx[11], side, q);

    // Whisper-soft low foreground mist veil gliding gently along the bottom of Quiet during fog (digits stay 100% crisp)
    if (fogFront > 0.30f && fogReady(c, F, 2)) {
        const Bitmap& bm = gFog[2].bmp;
        const float bw = bm.w / SPR * 1.18f, bh = bm.h / SPR * 0.72f;
        const double spanD = 1440.0 + bw / std::max(0.1f, s);
        const float bx = (float)(loopPos(a.fxFogT, 6.2, 0.25 * spanD, spanD) / spanD) * (W + bw) - bw;
        const float by = H * 0.90f + std::sin(ph(a.now, 0.055)) * 0.014f * H;
        g.drawBitmapRect(bm, bx, by - bh * 0.5f, bw, bh, clamp01(0.24f * fogFront * std::min(1.4f, gFx)));
    }

    if (k < 0.02f) return;
    if (!frontReady(c, F, clockH)) return;
    const double t = a.now;
    const float fht = frontHeightScale(clockH, gFrontCw);
    const float bw = gFrontCw * 1.65f, bh = gFrontCw * 0.78f * fht;
    const double spanD = 1440.0 + bw / std::max(0.1f, s);
    // starts completely off-screen left (-bw) and exits completely off-screen right (+W) so wrap never teleports mid-screen
    const float bx = (float)(loopPos(a.fxFrontT, 28.0, 0.35 * spanD, spanD) / spanD) * (W + bw) - bw;
    const float y = midY + std::sin(ph(t, 0.075)) * 0.04f * H + 0.015f * H;
    // an ethereal feathered veil, never a wall: the time stays crisp through it
    g.drawBitmapRect(gFront.bmp, bx, y - bh * 0.52f, bw, bh, clamp01(0.22f * k * std::min(1.4f, gFx)));
}

// ---------------------------------------------------------------- state
void quietFxStep(App& a) {
    float t[App::QFX_COUNT]; targets(a, t);
    for (int i = 0; i < App::QFX_COUNT; ++i) a.qfxT[i] = t[i];
    const float kk = 1.f - std::exp(-(float)std::min(a.dt, 1.0) * 1.4f);
    for (int i = 0; i < App::QFX_COUNT; ++i) {
        a.qfx[i] += (t[i] - a.qfx[i]) * kk;
        if (std::fabs(a.qfx[i] - t[i]) < 0.004f) a.qfx[i] = t[i];
    }
    // the clocks the weather moves by (see app.h): they only ever add up, so nothing jumps when the forecast or the window changes
    {
        const double dtc = std::min(std::max(a.dt, 0.0), 0.25);
        const float side = windSide(a);
        a.fxWk += (windKmh(a) - a.fxWk) * (1.f - std::exp(-(float)dtc * 0.7f));
        a.fxCloudT += dtc * side * (1.0 + 0.089 * a.fxWk);
        a.fxFrontT += dtc * side * (1.0 + 0.03 * a.fxWk);
        a.fxFogT += dtc * side * (1.0 + 0.10 * a.fxWk);
        a.fxSnowT += dtc;
        if (a.qfx[1] < 0.01f && a.qfx[11] < 0.01f) gWindFlip = side;
        else if (gWindFlip != side) {
            const float step = (float)(dtc / 0.65);
            gWindFlip = gWindFlip < side ? std::min(side, gWindFlip + step) : std::max(side, gWindFlip - step);
        }
        const double gustT = a.now;
        const float gust = 0.5f + 0.5f * std::sin(ph(gustT, 0.42) + 0.8f * std::sin(ph(gustT, 0.17)));
        const float galeBoost = 0.40f * clamp01((a.fxWk - 42.f) / 40.f);
        a.fxWindT += dtc * (0.55 + 1.45 * a.qfx[1] + galeBoost) * (0.85f + 0.30f * gust);
        const float bGust = 0.5f + 0.5f * std::sin(ph(gustT, 0.55) + 0.85f * std::sin(ph(gustT, 0.21)));
        const float bBoost = 0.45f * clamp01((a.fxWk - 32.f) / 36.f);
        a.fxBlizzT += dtc * (0.95 + 0.65 * bGust * bGust + bBoost);
        const float gGust = 0.5f + 0.5f * std::sin(ph(gustT, 0.68) + 0.90f * std::sin(ph(gustT, 0.27)));
        a.fxGaleT += dtc * (1.15 + 0.85 * gGust * gGust + 0.50f * clamp01((a.fxWk - 36.f) / 36.f));
        a.fxAuroraT += dtc;
    }
    // the light: heavy cloud, storms, hail and blizzards darken and cool it; fog and drizzle add soft atmospheric haze; sleet brings wintry chill.
    {
        const float cl = clamp01((a.qfx[0] - 0.3f) / 0.7f);                // 0 at a third of the sky, 1 under a solid lid
        float gl = 0.90f * std::pow(cl, 1.4f) + 0.15f * a.qfx[2] + 0.12f * a.qfx[6] + 0.14f * a.qfx[8] + 0.22f * a.qfx[9];
        gl = std::max(gl, std::max(std::max(0.97f * a.qfx[4], a.qfx[3]), std::max(0.82f * a.qfx[7], 0.88f * a.qfx[10])));
        gl = std::max(gl, std::max(0.55f * a.qfx[9], 0.44f * a.qfx[11]));
        float hz = std::max(std::max(a.qfx[5], 0.16f * a.qfx[6]), 0.34f * a.qfx[7]);
        hz = std::max(hz, std::max(0.26f * a.qfx[8], 0.18f * a.qfx[9]));
        gl *= 1.f - 0.55f * a.qfx[5] * (1.f - 0.65f * std::max(a.qfx[7], a.qfx[10]));
        const float m = a.st.weatherMood && !a.packHoldsColours() ? a.st.moodStrength : 0.f;     // a theme pack (GTA VI, Halloween ...) always keeps its own colours
        a.gloom = clamp01(gl * m); a.haze = clamp01(hz * m);
    }
    // if frames keep arriving later than planned this PC is struggling: draw less until they keep up, and creep back up when they do
    if (a.quietFxBusy() && !a.palMoving && a.dt > 0.0 && a.dt < 0.5) {
        const double target = a.frameTarget > 0 ? a.frameTarget : 1.0 / a.quietFps();
        a.fxLate += (float)((a.dt / target - a.fxLate) * 0.08);
        if (a.fxLate > 1.3f) a.fxQuality = std::max(0.3f, a.fxQuality - 0.02f);
        else if (a.fxLate < 1.1f) a.fxQuality = std::min(1.f, a.fxQuality + 0.01f);
    }
}

void quietFxRelease(gfx::Gfx& g) {
    freeSlot(g, gRainStreak);
    for (auto& s : gRainSplash) freeSlot(g, s);
    for (auto& s : gCloud) freeSlot(g, s);
    for (auto& s : gFog) freeSlot(g, s);
    freeSlot(g, gFogGlow);
    freeSlot(g, gCeiling); freeSlot(g, gMist); freeSlot(g, gFront);
    freeSlot(g, gSnowFlake); freeSlot(g, gBlizzStreak); freeSlot(g, gBlizzDrift);
    freeSlot(g, gDrizzleStreak); freeSlot(g, gSleetStreak); freeSlot(g, gHailStone); freeSlot(g, gGaleGust);
    for (auto& s : gAuroraRay) freeSlot(g, s);
    freeSlot(g, gAuroraGlow);
    freeSlot(g, gClockRefl);
}

std::string quietFxDescribe(const App& a) {
    if (!a.st.quietFx) return "Switched off. No weather is drawn behind the clock.";
    float t[App::QFX_COUNT]; targets(a, t);
    if (a.st.quietPreview != 0) {
        static const char* n[17] = {"", "sculpted clouds", "wind", "clouds and wind", "rain", "a severe storm with a tornado", "thunder and cloud with no rain", "a thunderstorm with rain", "cinematic fog", "the sun flare", "snowfall", "a harsh snowstorm (blizzard)", "silky drizzle", "freezing sleet", "a bouncing hailstorm", "a howling coastal gale", "aurora borealis"};
        return std::string("Previewing ") + n[std::max(1, std::min(16, a.st.quietPreview))] + ". Set it back to Live to follow the real weather.";
    }
    if (!a.wx.valid && t[12] < 0.02f) return "No weather yet, so nothing is drawn.";
    std::string parts; int cnt = 0;
    auto add = [&](const char* p) { parts += std::string(cnt++ ? ", " : "") + p; };
    if (t[3] > 0) return "A severe storm: a tornado twists up behind the clock, with lightning.";
    if (t[10] > 0.20f) return t[4] > 0 ? "A hailstorm with thunder: faceted ice stones and lightning." : "Hail: faceted ice stones bouncing across the clock.";
    if (t[7] > 0.20f) return t[4] > 0 ? "A thundersnow blizzard: driving snow, howling wind and lightning." : "A harsh snowstorm: wind-driven snow and swirling spindrift across the clock.";
    if (t[9] > 0.20f) return "Sleet: icy freezing streaks mixed with wet snow.";
    if (t[4] > 0) return t[2] > 0 ? "A thunderstorm: heavy cloud, lightning and rain." : "Thunder with cloud and no rain: heavy cloud and lightning.";
    if (t[11] > 0.25f) return "A coastal gale: sweeping wind ribbons and swirling leaves across the sky.";
    if (t[5] > 0.6f) return "Fog: layered valley mist drifting softly behind the clock.";
    if (t[8] > 0.20f) return "Drizzle: ultra-fine misty rain and gentle surface ripples.";
    if (t[12] > 0.08f) add("aurora");
    if (t[5] > 0) add("mist");
    if (t[0] > 0) add("clouds");
    if (t[1] > 0) add("wind");
    if (t[2] > 0) add("rain");
    if (t[6] > 0) add("snow");
    if (cnt == 0) {
        const float alt = (float)a.sun.hor.alt;
        if (a.st.quietFlare && alt > -3.f && alt < 14.f) return a.sun.hor.az < 180.0 ? "Clear and calm. The morning Sun sends a soft flare in from the left." : "Clear and calm. The evening Sun sends a soft flare in from the right.";
        return "Clear and calm. Nothing is drawn.";
    }
    return "Showing " + parts + ".";
}

// ---------------------------------------------------------------- sun flare
// In the morning and the evening the Sun sits low and a soft golden flare comes in from the side it is on. The sky map has east on the left and west on
// the right, and so does this: a morning flare from the left, an evening one from the right. It is meant to be a quiet detail, not an effect: a warm glow
// at the edge, a thin streak, and a few faint ghosts along the line through the middle of the screen. It is drawn behind the clock, it fades away as the
// Sun climbs or sinks, and cloud, fog and rain take it away (the Sun is behind them). Settings > Quiet switches it off.
void drawSunFlare(Ctx& c) {
    App& a = c.a; Gfx& g = c.g; const Palette& P = c.P; const float s = c.s, W = c.W, H = c.H;
    if (!a.st.quietFlare || a.packTheme()) return;                                   // switched off, or a theme pack is the theme: its colours never change
    const bool preview = a.st.quietPreview == 9;
    // The Sunlit theme is made for the flare: there the Sun glows ALL DAY, walking across the top of the screen from the left edge (east) to the right
    // edge (west) as it does across the sky map, gentle at noon and warmest when it is low. In every other theme only the low morning and evening Sun shows.
    if (a.st.theme == THEME_GALAXY || a.st.theme == THEME_MIDNIGHT) return;           // deep nocturnal themes have no daytime sun flare
    const bool sunlit = a.st.theme == THEME_SUNLIT || a.st.theme == THEME_SUNLIT2;
    float alt = (float)a.sun.hor.alt, az = (float)a.sun.hor.az;
    bool morning = az < 180.f;
    if (preview && (sunlit ? alt < -3.f : !(alt > -4.f && alt < 18.f))) { alt = 3.f; az = 262.f; morning = false; }       // the preview shows it at any hour, as an evening one
    float k = smoothstep(-3.5f, 0.5f, alt) * (1.f - smoothstep(6.f, 17.f, alt));
    if (sunlit) k = smoothstep(-3.5f, 0.5f, alt) * (0.58f + 0.42f * (1.f - smoothstep(6.f, 30.f, alt)));
    if (preview) k = std::max(k, 0.9f);
    if (k < 0.01f) return;
    float covered = std::max(std::max(a.qfx[0], a.qfx[5]), std::max(std::max(a.qfx[2], std::max(a.qfx[6], a.qfx[7])), a.qfx[4]));
    covered = std::max(covered, std::max(a.qfx[8], std::max(a.qfx[9], a.qfx[10])));
    k *= 1.f - 0.92f * clamp01(covered);
    k *= P.dark ? 1.f : (sunlit ? 0.95f : 0.6f);                                       // on a bright page a warm glow needs less, a sunny one a little less than none
    k *= 0.7f + 0.3f * std::max(0.4f, std::min(2.f, a.st.quietStrength));
    k *= 1.f - 0.7f * P.ember;                                                         // red night mode: even softer
    if (k < 0.01f) return;
    float x0, y0;
    if (sunlit) {
        const float f = -(float)std::sin(astro::rad(az));                                // east (90) left (-1), noon south (180) OR north (0/360) center (0), west (270) right (+1)
        x0 = W * (0.5f + 0.56f * f);
        y0 = H * (0.60f - 0.56f * smoothstep(-1.f, 55.f, alt));                          // on the horizon line at sunrise, nearly at the top at noon
    } else {
        x0 = morning ? -0.012f * W : 1.012f * W;
        y0 = H * (0.60f - 0.40f * smoothstep(-1.f, 14.f, alt));                          // low on the screen at sunrise and sunset, higher as it climbs
    }
    const float dir = x0 < 0.5f * W ? 1.f : -1.f;
    const Color white = {1.f, 1.f, 1.f, 1.f};
    Color warm = lerp(rgb(0xFF8A3D), rgb(0xFFD58A), smoothstep(0.f, 12.f, alt));      // deep orange when low, golden higher up
    if (sunlit) warm = lerp(warm, rgb(0xFFC24A), 0.5f * smoothstep(8.f, 40.f, alt));  // and a clean sun-yellow in the middle of the day
    warm = tint(P, warm);
    const Color hot = tint(P, lerp(warm, white, 0.55f));
    const float R = std::max(W, H * 1.2f);
    g.glow(x0, y0, R * 0.34f, warm, 0.40f * k);                                        // the big soft glow
    g.glow(x0, y0, R * 0.12f, warm, 0.50f * k);
    g.glow(x0, y0, R * 0.045f, hot, 1.0f * k);                                         // the bright core, half off the screen
    g.glowEllipse(x0 + dir * W * 0.17f, y0, W * 0.26f, std::max(5.f * s, H * 0.012f), lerp(warm, hot, 0.5f), 0.40f * k);    // a thin streak
    // ghosts: faint orbs along the line from the Sun through the middle of the screen (fewer and fainter when the Sun is high: they would sit on the digits)
    const float gk = sunlit ? 1.f - 0.75f * smoothstep(10.f, 45.f, alt) : 1.f;
    const float vx = W * 0.5f - x0, vy = H * 0.5f - y0;
    struct Ghost { float at, r, a; unsigned col; bool ring; };
    static const Ghost gh[4] = {{0.34f, 0.030f, 0.12f, 0xFFB070, false}, {0.58f, 0.052f, 0.075f, 0xFF9A9A, true}, {0.86f, 0.022f, 0.13f, 0xFFD890, false}, {1.22f, 0.066f, 0.055f, 0x9AD0FF, true}};
    for (const Ghost& o : gh) {
        const float gx = x0 + vx * o.at, gy = y0 + vy * o.at, r = o.r * R;
        const Color gc = tint(P, lerp(rgb(o.col), warm, 0.3f));
        g.glow(gx, gy, r, gc, o.a * k * gk * 2.2f);
        if (o.ring) g.strokeEllipse(gx, gy, r * 0.62f, r * 0.62f, withA(gc, 0.10f * k * gk), std::max(1.f, 1.2f * s));
    }
}

// ---------------------------------------------------------------- Wet-Horizon Clock Reflections on Quiet (3.0.0 "Aurora")
// Renders a vertically mirrored, horizontally wave-rippled reflection of the live Quiet clock digits glistening across
// a rain-slicked horizon floor beneath the date line. Sprite is cached & rebuilt only when the minute digits or palette change.
void drawQuietWetReflection(Ctx& c, float x0, float baseY, float capH, float em, const std::wstring& hm, float dateBottomY) {
    App& a = c.a; Gfx& g = c.g; const Palette& P = c.P; const float s = c.s, W = c.W, H = c.H;
    if (!a.st.glassDrops || !a.st.quietFx || hm.empty()) return;
    const float wet = clamp01(std::max(a.qfx[2], std::max(a.qfx[8] * 0.72f, std::max(a.qfx[9] * 0.80f, std::max(a.qfx[10] * 0.68f, a.qfx[5] * 0.22f)))));
    if (wet < 0.04f) return;

    const float tw = g.timeWidth(F_DISPLAY, em, hm);
    const int sw = std::max(16, std::min(1600, (int)std::ceil(tw + 24.f * s)));
    const int sh = std::max(16, std::min(520, (int)std::ceil(capH + 24.f * s)));
    uint64_t key = 9100ULL;
    for (wchar_t ch : hm) key = mixKey(key, (uint64_t)ch);
    key = mixKey(key, (uint64_t)((sw << 16) | sh));
    key = mixKey(key, (uint64_t)((int)std::lround(P.ink.r * 24.f) * 900 + (int)std::lround(P.ink.g * 24.f) * 30 + (int)std::lround(P.ink.b * 24.f)));

    if (!ensure(c.g, gClockRefl, key, 1.f, [&]() -> Bitmap {
        if (!g.beginSprite(sw, sh)) return Bitmap{};
        const float padX = (sw - tw) * 0.5f;
        const float padBase = sh - 10.f * s;
        g.timeText(F_DISPLAY, em, padX, padBase, hm, P.ink, lerp(P.ink, P.ink2, 0.6f), Align::Left);
        return g.endSprite();
    })) return;

    // Horizon pool starts subtly below the date line and fills down toward the wet ground
    const float poolY = std::min(H * 0.88f, std::max(baseY + capH * 0.28f, dateBottomY + 26.f * s));
    const float reflH = std::min(H - poolY - 6.f * s, capH * 0.44f);
    if (reflH < 18.f * s) return;

    // 1. Wet slate horizon sheen & faint liquid mirror catchlight
    const Color sheenCol = P.dark ? lerp(P.ink2, P.accent2, 0.35f) : lerp(P.bgBottom, P.ink2, 0.25f);
    g.radialEllipse(W * 0.5f, poolY + reflH * 0.45f, tw * 0.64f, reflH * 0.95f, 0.f, -reflH * 0.15f, {
        {0.00f, withA(sheenCol, (P.dark ? 0.13f : 0.08f) * wet)},
        {0.55f, withA(sheenCol, (P.dark ? 0.05f : 0.03f) * wet)},
        {1.00f, withA(sheenCol, 0.00f)},
    }, 1.f);
    // Crisp wet horizon hairline shimmer
    g.line(x0 - 20.f * s, poolY, x0 + tw + 20.f * s, poolY, withA(sheenCol, (P.dark ? 0.10f : 0.07f) * wet), std::max(1.f, s), true);

    // 2. Vertically mirrored clock digits sliced into 34 ultra-fine water-rippled horizontal bands
    const int strips = 34;
    const float srcBand = (float)gClockRefl.bmp.h / (float)strips;
    const float dstBand = reflH / (float)strips;
    const float dstX0 = x0 - (sw - tw) * 0.5f;
    const double t = a.now;
    const float baseOp = (P.dark ? 0.25f : 0.18f) * wet * std::min(1.25f, std::max(0.5f, a.st.quietStrength));

    for (int i = 0; i < strips; ++i) {
        const float u = (i + 0.5f) / (float)strips;
        const float topSoft = smoothstep(0.f, 0.10f, u);
        const float fade = topSoft * std::pow(1.f - u, 1.65f);
        const float op = baseOp * fade;
        if (op < 0.004f) continue;
        // Organic dual-wave rain-puddle horizontal distortion increasing smoothly with depth into the pool
        const float wave = std::sin(u * 14.5f - ph(t, 2.55) + 0.65f * std::sin(ph(t, 1.15)))
                         + 0.42f * std::sin(u * 27.0f + ph(t, 3.40));
        const float dx = dstX0 + wave * (1.2f + 6.8f * u * u) * s;
        // Source strip runs bottom-to-top of upright sprite so flipY mirrors the digits cleanly from their baseline down
        const float sy = (float)gClockRefl.bmp.h - (i + 1) * srcBand;
        const float dy = poolY + i * dstBand;
        g.drawBitmapSubScaled(gClockRefl.bmp, dx, dy, (float)sw, dstBand + 0.8f, 0.f, sy, (float)gClockRefl.bmp.w, srcBand, op, true);
    }
}

// Precipitation & live Aurora on the Sky face: drawn behind the sky dome and labels so every label stays legible.
void drawSkyRain(Ctx& c) {
    App& a = c.a;
    if (!a.st.quietFx) return;
    const bool hasAurora = a.qfx[12] > 0.01f;
    const bool hasPrecip = a.st.skyRain && (a.qfx[2] > 0.01f || a.qfx[6] > 0.01f || a.qfx[7] > 0.01f || a.qfx[8] > 0.01f || a.qfx[9] > 0.01f || a.qfx[10] > 0.01f);
    if (!hasAurora && !hasPrecip) return;
    gFx = std::max(0.4f, std::min(2.f, a.st.quietStrength));
    budgetTick(a); trackSize(c);
    const float side = windSide(a), q = std::max(0.3f, std::min(1.f, a.fxQuality));
    if (hasAurora) drawAurora(c, a.qfx[12], q, true);
    if (!hasPrecip) return;
    const Frame F = makeFrame(c, heavyOf(a));
    const float calmSnow = a.qfx[6] * (1.f - 0.90f * a.qfx[7]);
    if (a.qfx[2] > 0.01f) drawRain(c, F, a.qfx[2] * 0.7f, side, q * 0.7f, true);
    if (a.qfx[8] > 0.01f) drawDrizzle(c, F, a.qfx[8] * 0.78f, side, q * 0.72f, true);
    if (a.qfx[9] > 0.01f) drawSleet(c, F, a.qfx[9] * 0.76f, side, q * 0.72f, true);
    if (a.qfx[10] > 0.01f) drawHail(c, F, a.qfx[10] * 0.75f, side, q * 0.72f, true);
    if (calmSnow > 0.01f) drawSnow(c, F, calmSnow * 0.85f, side, q * 0.75f, true);
    if (a.qfx[7] > 0.01f) drawBlizzard(c, F, a.qfx[7] * 0.78f, side, q * 0.72f, true);
}

void drawQuietFx(Ctx& c) {
    App& a = c.a;
    drawScenery(c);                                                           // behind the weather: a cloud drifts over it
    if (!a.st.quietFx || !a.qfxAnyActive()) return;
    gFx = std::max(0.4f, std::min(2.f, a.st.quietStrength));
    budgetTick(a); trackSize(c);
    const float side = windSide(a), q = std::max(0.3f, std::min(1.f, a.fxQuality));
    const float heavy = heavyOf(a);
    const Frame F = makeFrame(c, heavy);
    const float calmSnow = a.qfx[6] * (1.f - 0.90f * a.qfx[7]);
    const float windVis = a.qfx[1] * (1.f - 0.78f * std::max(a.qfx[7], a.qfx[11]));
    const float fogVis = a.qfx[5] * (1.f - 0.85f * a.qfx[7]);      // blizzard renders its own billowing whiteout drift curtains
    if (a.qfx[12] > 0.01f) drawAurora(c, a.qfx[12], q);            // aurora floats high above clouds & weather
    if (a.qfx[0] > 0.01f) drawClouds(c, F, a.qfx[0], heavy);
    if (fogVis > 0.01f) drawFog(c, F, fogVis, q);
    if (a.qfx[4] > 0.01f) drawLightning(c, a.qfx[4], a.qfx[3] > 0.01f, a.qfx[0], heavy);
    if (a.qfx[3] > 0.01f) drawTornado(c, a.qfx[3], side, q);
    if (a.qfx[8] > 0.01f) drawDrizzle(c, F, a.qfx[8], side, q);
    if (a.qfx[2] > 0.01f) drawRain(c, F, a.qfx[2], side, q);
    if (a.qfx[9] > 0.01f) drawSleet(c, F, a.qfx[9], side, q);
    if (a.qfx[10] > 0.01f) drawHail(c, F, a.qfx[10], side, q);
    if (calmSnow > 0.01f) drawSnow(c, F, calmSnow, side, q);
    if (a.qfx[7] > 0.01f) drawBlizzard(c, F, a.qfx[7], side, q);
    if (a.qfx[11] > 0.01f) drawGale(c, F, a.qfx[11], side, q);
    if (windVis > 0.01f) drawWind(c, F, windVis, q);
}

// Focus tab ambient backdrop: 15 curated studio atmospheres (Off, Live, Rain, Snow, Clouds, Fog, Storm, Stars, Blizzard, Drizzle, Sleet, Hail, Gale, Midnight, Aurora).
void drawFocusBackdrop(Ctx& c) {
    App& a = c.a;
    const int mode = a.st.focusFx;
    if (mode <= 0) {                                          // OFF: still honour the Midnight & Galaxy themes' own signature sky backdrops
        if (!a.packTheme()) {
            if (a.st.theme == THEME_MIDNIGHT) drawMidnightBackdropOnly(c, 0.f, false);
            else if (a.st.theme == THEME_GALAXY && a.st.spaceFx) drawSpaceBackdropOnly(c);
        }
        return;
    }
    gFx = std::max(0.55f, std::min(1.6f, a.st.quietStrength));
    budgetTick(a); trackSize(c);
    const float q = std::max(0.35f, std::min(1.f, a.fxQuality));
    if (mode == 1) {                                          // LIVE: outdoor weather + theme scenery (Midnight / Galaxy / Sunlit II)
        drawQuietFx(c);
        return;
    }
    if (mode == 7) {                                          // STARS: Galaxy deep starry cosmos + occasional serene meteor
        drawSpaceBackdropOnly(c);
        return;
    }
    if (mode == 13) {                                         // MIDNIGHT: artistic Prussian-velvet & moonlit champagne nocturne
        drawMidnightBackdropOnly(c, 0.f, true);
        return;
    }
    if (mode == 14) {                                         // AURORA: multi-curtain Aurora Borealis over crisp nocturnal stars
        if (!a.packTheme()) drawMidnightBackdropOnly(c, 0.25f, a.st.theme != THEME_MIDNIGHT && c.P.dark);
        drawAurora(c, 1.0f, q, false);
        return;
    }
    const float heavy = (mode == 6 || mode == 11) ? 0.85f
                      : (mode == 8 ? 0.80f : (mode == 10 || mode == 12 ? 0.50f : (mode == 2 ? 0.30f : 0.14f)));
    if (a.st.theme == THEME_MIDNIGHT && !a.packTheme()) {     // Midnight theme's lunar airglow & zenith stars breathe softly behind Focus weather
        const float dim = (mode == 4 || mode == 3 || mode == 9 || mode == 12) ? 0.36f : 0.62f;
        drawMidnightBackdropOnly(c, dim, false);
    }
    const Frame F = makeFrame(c, heavy);
    switch (mode) {
        case 2:                                               // RAIN: cozy lo-fi rain window
            drawClouds(c, F, 0.72f, 0.30f);
            drawRain(c, F, 0.78f, 1.f, q * 0.88f);
            break;
        case 3:                                               // SNOW: serene snowfall
            drawClouds(c, F, 0.55f, 0.12f);
            drawSnow(c, F, 0.90f, 1.f, q);
            break;
        case 4:                                               // CLOUDS: sculpted volumetric sky
            drawClouds(c, F, 0.90f, 0.12f);
            break;
        case 5:                                               // FOG: cinematic mountain valley mist
            drawClouds(c, F, 0.32f, 0.10f);
            drawFog(c, F, 0.95f, q);
            break;
        case 6:                                               // STORM: moody rain + distant sheet lightning
            drawClouds(c, F, 0.92f, 0.85f);
            drawLightning(c, 0.78f, false, 0.92f, 0.85f);
            drawRain(c, F, 0.82f, 1.f, q * 0.92f);
            break;
        case 8:                                               // BLIZZARD: fierce driving snowstorm
            drawClouds(c, F, 0.94f, 0.80f);
            drawBlizzard(c, F, 1.0f, 1.f, q);
            break;
        case 9:                                               // DRIZZLE: silky poetic mist-rain
            drawClouds(c, F, 0.68f, 0.14f);
            drawFog(c, F, 0.24f, q * 0.7f);
            drawDrizzle(c, F, 0.96f, 1.f, q);
            break;
        case 10:                                              // SLEET: wintry freezing rain & wet snow
            drawClouds(c, F, 0.84f, 0.50f);
            drawSleet(c, F, 0.96f, 1.f, q);
            break;
        case 11:                                              // HAIL: dramatic bouncing ice storm
            drawClouds(c, F, 0.94f, 0.85f);
            drawLightning(c, 0.45f, false, 0.94f, 0.85f);
            drawHail(c, F, 0.96f, 1.f, q);
            break;
        case 12:                                              // GALE: sweeping coastal windstorm
            drawClouds(c, F, 0.78f, 0.48f);
            drawGale(c, F, 0.96f, 1.f, q);
            break;
        default: break;
    }
}

} // namespace app
