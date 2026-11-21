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

bool snowCode(int c) { return (c >= 71 && c <= 77) || c == 85 || c == 86; }
bool rainCode(int c) { return (c >= 51 && c <= 67) || (c >= 80 && c <= 82); }        // thunder codes are not rain on their own: a thunderstorm can be dry
bool stormCode(int c) { return c == 95 || c == 96 || c == 99; }

// what the weather asks for: strengths 0..1 of clouds, wind, rain, tornado, thunder (lightning), fog, snow and blizzard.
// This is the weather itself, so the light can follow it on every face; whether it is drawn behind the clock is decided later.
void targets(const App& a, float t[8]) {
    for (int i = 0; i < 8; ++i) t[i] = 0.f;
    switch (a.st.quietPreview) {
        case 1: t[0] = 0.9f; return;
        case 2: t[1] = 0.85f; return;
        case 3: t[0] = 0.8f; t[1] = 0.8f; return;
        case 4: t[0] = 0.95f; t[2] = 0.85f; return;
        case 5: t[3] = 1.f; t[4] = 1.f; t[0] = 0.95f; t[1] = 0.7f; t[2] = 0.55f; return;     // severe: tornado and lightning
        case 6: t[4] = 1.f; t[0] = 0.95f; t[1] = 0.2f; return;                                // thunder and cloud, no rain
        case 7: t[4] = 1.f; t[0] = 0.95f; t[1] = 0.5f; t[2] = 0.8f; return;                   // thunderstorm with rain
        case 8: t[5] = 1.f; t[0] = 0.35f; return;                                             // fog
        case 9: return;                                                                        // the sun flare alone: no weather
        case 10: t[0] = 0.65f; t[6] = 0.88f; return;                                          // calm, soothing snowfall
        case 11: t[0] = 0.96f; t[1] = 0.92f; t[5] = 0.30f; t[6] = 0.90f; t[7] = 1.f; return;  // harsh snowstorm (blizzard)
        default: break;
    }
    if (!a.wx.valid) return;
    const weather::Current& c = a.wx.cur;
    const bool thunder = stormCode(c.code);
    const bool thunderSnow = thunder && c.temp <= 1.0 && (snowCode(c.code) || c.precip >= 0.2);
    const bool snow = snowCode(c.code) || thunderSnow;
    if (c.cloud >= 30.0) t[0] = 0.3f + 0.7f * clamp01((float)(c.cloud - 30.0) / 55.f);
    const double w = std::max(c.wind, 0.6 * c.gust);
    if (w >= 12.0) t[1] = 0.28f + 0.72f * clamp01((float)(w - 12.0) / 30.f);
    if (snow) {
        const bool heavySnow = (c.code == 75 || c.code == 86 || c.precip >= 2.8);
        const float baseSnow = heavySnow ? 0.92f : (c.code == 71 || c.code == 77) ? 0.52f : 0.74f;
        t[6] = std::max(baseSnow, 0.45f + 0.55f * clamp01((float)(c.precip / 2.2)));
        t[0] = std::max(t[0], 0.45f + 0.25f * t[6]);
        // Blizzard / harsh snowstorm: snow whipped by strong winds/gusts, heavy snow with brisk winds, or freezing thundersnow
        const bool blizzWind = w >= 28.0 || c.gust >= 42.0 || (heavySnow && (w >= 18.0 || c.gust >= 30.0)) || thunderSnow;
        if (blizzWind) {
            const float windAmt = clamp01((float)(std::max(w - 18.0, (c.gust - 30.0) * 0.75) / 28.0));
            const float heavyBoost = heavySnow ? 0.22f : 0.f;
            t[7] = clamp01(0.38f + 0.62f * windAmt + heavyBoost);
            if (thunderSnow) t[7] = std::max(t[7], 0.85f);
            t[0] = std::max(t[0], 0.80f + 0.16f * t[7]);
            t[1] = std::max(t[1], 0.65f + 0.30f * t[7]);
            t[5] = std::max(t[5], 0.24f * t[7]);
        }
    }
    const bool rain = !snow && (rainCode(c.code) || c.precip >= 0.1);
    if (rain) { t[2] = 0.45f + 0.55f * clamp01((float)(c.precip / 3.0)); t[0] = std::max(t[0], 0.50f + 0.25f * t[2]); }     // rain falls out of cloud: always a few of them, whatever the cover reported
    if (thunder) { t[4] = 1.f; t[0] = std::max(t[0], 0.95f); t[1] = std::max(t[1], 0.3f); }   // dark cloud and lightning, with rain only if rain is reported
    if (!snow && (c.gust >= 70.0 || (thunder && c.gust >= 55.0))) {                           // a severe warm-sector storm: the tornado (never during freezing snow)
        t[3] = 1.f; t[4] = std::max(t[4], 0.7f); t[0] = std::max(t[0], 0.95f); t[1] = std::max(t[1], 0.7f);
    }
    // fog: reported as fog, or the air is saturated and still. Damp mountain mornings read as a soft mist.
    if (c.code == 45 || c.code == 48) t[5] = 0.95f;
    else if (!rain && !snow && !thunder && c.humidity >= 96.0 && w < 10.0) t[5] = 0.25f + 0.45f * clamp01((float)(c.humidity - 96.0) / 4.f);
    if (t[5] > 0.01f && t[7] < 0.05f) t[1] *= 1.f - 0.6f * t[5];                              // wind does not show through thick calm fog (a blizzard keeps its wind)
}

// the way the wind blows across the screen: +1 to the right, -1 to the left
// how dark and heavy the cloud is: storms and blizzards most, rain a little
float heavyOf(const App& a) { return std::max(std::max(std::max(a.qfx[3], 0.85f * a.qfx[4]), 0.78f * a.qfx[7]), 0.30f * a.qfx[2]); }

float windSide(const App& a) {
    if (a.st.quietPreview != 0 || !a.wx.valid) return 1.f;
    const double toward = (a.wx.cur.windDir + 180.0) * 3.14159265 / 180.0;
    return std::sin(toward) >= 0 ? 1.f : -1.f;
}

float windKmh(const App& a) {
    if (a.st.quietPreview == 2 || a.st.quietPreview == 3) return 32.f;
    if (a.st.quietPreview == 5) return 48.f;
    if (a.st.quietPreview == 11) return 58.f;
    return a.wx.valid ? (float)std::max(a.wx.cur.wind, 0.6 * a.wx.cur.gust) : 0.f;
}

// overall strength from Settings: every alpha goes through al()
float gFx = 1.f;
float al(float x) { return clamp01(x * gFx); }

// cloud colours that read on any theme: a body between the page and the ink, a bright top, a darker belly
struct Tones { Color body, hi, shade; };
Tones cloudTones(const Palette& P, float heavy) {
    const Color white = {1.f, 1.f, 1.f, 1.f};
    // on a light page the clouds are soft grey with a white top and a slate belly; on a dark one they are pale and glowing
    // at night the cloud is a dim shape against a deep dark sky, not a pale glow: nothing is lit
    const float nt = P.nightness;
    Color body = lerp(P.bgBottom, P.ink, P.dark ? 0.50f - 0.14f * nt : 0.22f);
    Color hi = P.dark ? lerp(body, white, 0.55f - 0.10f * nt) : lerp(body, white, 0.7f);
    Color shade = P.dark ? lerp(body, P.bgTop, 0.55f) : lerp(body, P.ink, 0.38f);
    body = lerp(body, P.dark ? P.bgTop : P.ink, 0.30f * heavy);
    shade = lerp(shade, P.dark ? P.bgTop : P.ink, 0.30f * heavy);
    return {tint(P, body), tint(P, hi), tint(P, shade)};
}

// one cumulus: a row of lumpy puffs with a flat underside, lit from above
void cloudShape(Gfx& g, float x, float y, float cw, float ht, int seed, const Tones& T, float aBody, float aHi, float aShade, int puffs) {
    for (int j = 0; j < puffs; ++j) {
        const float u = (j + 0.5f) / puffs;
        const float bump = std::pow(std::sin(u * PI_F), 0.75f);
        const float hr = hashf(seed * 31 + j, 5);
        const float r = cw * (0.10f + 0.15f * bump) * (0.8f + 0.45f * hr) * ht;
        const float px = x + (u - 0.5f) * cw * 0.92f + (hashf(seed * 31 + j, 6) - 0.5f) * cw * 0.03f;
        const float py = y - r * 0.55f * bump + (hashf(seed * 31 + j, 7) - 0.5f) * r * 0.25f;
        g.glow(px, py + r * 0.45f, r * 1.7f, T.shade, aShade);
        g.glow(px, py, r * 1.55f, T.body, aBody);
        g.glow(px - r * 0.18f, py - r * 0.42f, r * 0.85f, T.hi, aHi);
    }
    g.glow(x - cw * 0.12f, y + cw * 0.03f, cw * 0.30f * ht, T.shade, aShade * 0.7f);
    g.glow(x + cw * 0.14f, y + cw * 0.035f, cw * 0.28f * ht, T.shade, aShade * 0.6f);
}

// ---------------------------------------------------------------- cached soft shapes
// A cloud never changes shape, it only slides. Each one is drawn once into a small image and then just moved, which costs a
// fraction of redrawing hundreds of big gradients every frame. Images are made from the colours the theme is HEADING for (not
// the ones it is fading through), a couple per frame at most, so changing the theme or the weather never causes a long frame.
constexpr float SPR = 0.5f;           // images are kept at half size: the shapes are soft, and it quarters the memory

struct Slot { Bitmap bmp; uint64_t key = 0, failed = 0; float nominal = 0; };       // nominal: the size (px) the image was drawn for, so a stale one can be scaled
Slot gCloud[6], gCeiling, gMist, gFront, gFog[3], gSnowFlake, gBlizzStreak, gBlizzDrift;
float gCeilH = 0, gMistH = 0, gFrontCw = 0, gFogH = 0, gSnowCell = 0, gBlizzS = 0, gBlizzSlant = 0, gBlizzDriftH = 0;
bool gResizing = false;      // the window size changed a moment ago: keep drawing the images we have, rebuild once it settles
int gBudget = 0; double gBudgetAt = -1e18;
} // namespace
int gSpriteBuilds = 0;
namespace {

// what this frame draws with
struct Frame { Tones T; Color mist, fog, wind, windHead, leaf, rain, snow, snowRim; int hq = 0; };

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

float frontWidth(float W, float H) { return W * (W < H * 1.15f ? 1.0f : 0.62f); }
float frontHeightScale(float clockH, float cw) { return std::max(0.55f, std::min(1.35f, clockH / (0.50f * cw) * 0.62f)); }

Frame makeFrame(Ctx& c, float heavy) {
    Frame F;
    const Palette& T = c.a.palTarget;                       // the colours the face is settling into
    const float h = std::max(0.f, std::min(1.f, std::max(heavy, 0.55f * c.a.gloom)));
    F.hq = (int)std::lround(h * 4.f);                       // storm darkness in four steps, so the images are not rebuilt all the time
    F.T = cloudTones(T, F.hq / 4.f);
    F.mist = tint(T, lerp(lerp(T.ink, T.accent2, 0.35f), T.bgBottom, 0.55f * T.nightness));
    const Color white = {1.f, 1.f, 1.f, 1.f};
    const Palette& P = c.P;                                 // live fading palette for uncached wind strokes and leaves
    F.wind = tint(P, lerp(P.ink, P.ink2, 0.18f));           // soft brush sheath
    F.windHead = tint(P, P.dark ? lerp(P.ink, white, 0.55f) : P.ink);      // crisp bright core & dust motes
    F.leaf = tint(P, lerp(P.ink2, P.accent, 0.44f));        // tumbling leaves catching the light
    // rain: pale blue-white on a dark page, slate blue on a light one, so the drops read on every theme
    F.rain = tint(T, T.dark ? lerp(lerp(T.ink, T.accent2, 0.35f), white, 0.30f) : lerp(T.ink2, T.accent2, 0.45f));
    // snow: luminous ice-white on dark themes, crisp pearl-white with soft slate-blue rim on bright daylight themes
    F.snow = tint(T, T.dark ? lerp(lerp(T.ink, white, 0.72f), rgb(0xE8F4FF), 0.30f) : lerp(white, rgb(0xEEF5FC), 0.45f));
    F.snowRim = tint(T, T.dark ? lerp(T.accent2, white, 0.40f) : lerp(T.ink2, T.accent2, 0.42f));
    F.fog = tint(T, lerp(lerp(T.bgTop, T.bgBottom, 0.5f), white, T.dark ? 0.42f - 0.31f * T.nightness : 0.50f));     // a night fog is a faint dark veil, not a milky one
    return F;
}

// one image slot per cloud shape: slide it, never redraw it
bool cloudReady(Ctx& c, const Frame& F, int j) {
    const int sq = (int)std::lround(std::log2(std::max(0.05f, c.s)) * 5.f);          // the scale in ~15% steps
    const float cw = (430.f + 75.f * j) * std::pow(2.f, sq / 5.f);
    uint64_t key = mixKey(mixKey(mixKey(q3(F.T.body), q3(F.T.hi)), mixKey(q3(F.T.shade), (uint64_t)(sq + 300))), (uint64_t)j);
    return ensure(c.g, gCloud[j], key, cw, [&] {
        const int sw = (int)(cw * 1.5f * SPR), sh = (int)(cw * 1.1f * SPR);
        if (!c.g.beginSprite(sw, sh)) return Bitmap{};
        cloudShape(c.g, sw * 0.5f, sh * 0.52f, cw * SPR, 1.f, j + 3, F.T, 0.40f, 0.28f, 0.34f, 9);
        return c.g.endSprite();
    });
}

bool ceilingReady(Ctx& c, const Frame& F) {
    const float W = c.W, H = c.H;
    uint64_t key = mixKey(mixKey(mixKey(q3(F.T.shade), q3(F.T.body)), qsz(W)), qsz(H));
    gCeilH = 0.40f * H;
    return ensure(c.g, gCeiling, key, 0.f, [&] {
        const int sw = (int)(W * SPR), sh = (int)(gCeilH * SPR);
        if (!c.g.beginSprite(sw, sh)) return Bitmap{};
        for (int j = 0; j < 7; ++j) {
            const float px = sw * (j + 0.5f) / 7.f;
            c.g.glow(px, (0.02f + hashf(j, 90) * 0.06f) * H * SPR, W * 0.26f * SPR, F.T.shade, 0.84f);
            c.g.glow(px, 0.06f * H * SPR, W * 0.20f * SPR, F.T.body, 0.40f);
        }
        return c.g.endSprite();
    });
}

bool mistReady(Ctx& c, const Frame& F) {
    const float W = c.W, H = c.H;
    uint64_t key = mixKey(mixKey(q3(F.mist), qsz(W)), qsz(H));
    gMistH = 0.26f * H;
    return ensure(c.g, gMist, key, 0.f, [&] {
        const int sw = (int)(W * SPR), sh = (int)(gMistH * SPR);
        if (!c.g.beginSprite(sw, sh)) return Bitmap{};
        const float R = sh * 0.98f; const int n = (int)std::ceil(sw / (R * 0.8f)) + 1;
        for (int j = 0; j < n; ++j) c.g.glow(sw * (j + 0.5f) / n, (float)sh, R, F.mist, 0.44f);
        return c.g.endSprite();
    });
}

// the one cloud that passes in front of the time
bool frontReady(Ctx& c, const Frame& F, float clockH) {
    const float W = c.W, H = c.H;
    const float fcw = frontWidth(W, H), fht = frontHeightScale(clockH, fcw);
    uint64_t key = mixKey(mixKey(mixKey(q3(F.T.body), q3(F.T.hi)), mixKey(q3(F.T.shade), qsz(W))), mixKey(qsz(H), (uint64_t)(fht * 8.f)));
    gFrontCw = fcw;
    return ensure(c.g, gFront, key, 0.f, [&] {
        const int sw = (int)(fcw * 1.5f * SPR), sh = (int)(fcw * 1.1f * fht * SPR);
        if (!c.g.beginSprite(sw, sh)) return Bitmap{};
        cloudShape(c.g, sw * 0.5f, sh * 0.52f, fcw * SPR, fht, 77, F.T, 0.48f, 0.38f, 0.34f, 15);
        cloudShape(c.g, sw * 0.5f + fcw * 0.06f * SPR, sh * 0.52f - fcw * 0.03f * fht * SPR, fcw * 0.72f * SPR, fht * 0.9f, 78, F.T, 0.28f, 0.24f, 0.f, 11);
        return c.g.endSprite();
    });
}

// a bank of fog: a long, low, soft shape
bool fogReady(Ctx& c, const Frame& F, int j) {
    const float W = c.W, H = c.H;
    uint64_t key = mixKey(mixKey(mixKey(q3(F.fog), qsz(W)), qsz(H)), (uint64_t)j);
    gFogH = 0.34f * H;
    return ensure(c.g, gFog[j], key, 0.f, [&] {
        const int sw = (int)(W * 1.3f * SPR), sh = (int)(gFogH * SPR);
        if (!c.g.beginSprite(sw, sh)) return Bitmap{};
        const int n = 9;
        for (int m = 0; m < n; ++m) {
            const float rx = sw * (0.10f + 0.07f * hashf(j * 17 + m, 111));
            const float ry = sh * (0.22f + 0.14f * hashf(j * 17 + m, 112));
            const float cx = rx * 0.9f + (sw - 2.f * rx * 0.9f) * ((m + 0.15f + 0.7f * hashf(j * 17 + m, 113)) / n);
            const float cyMin = ry * 0.95f, cyMax = sh - ry * 0.95f;
            const float cy = cyMin + (cyMax - cyMin) * hashf(j * 17 + m, 114);
            c.g.glowEllipse(cx, cy, rx * 1.5f, ry * 1.5f, F.fog, 0.40f);
            c.g.glowEllipse(cx, cy, rx * 0.85f, ry * 0.85f, F.fog, 0.30f);
        }
        return c.g.endSprite();
    });
}

// where cloud number i is right now: centre (x, y), width, depth layer, and how visible it is at the screen edges. drawClouds and the lightning both use it,
// so a bolt can leave from a cloud you can see.
struct CloudAt { float x, y, cw, vis; int layer; };
CloudAt cloudAt(const Ctx& c, int i, float heavy) {
    const App& a = c.a; const float s = c.s, W = c.W, H = c.H;
    CloudAt r; r.layer = i % 3;                                          // 0 far, 1 middle, 2 near
    const float depth = 0.55f + 0.22f * r.layer;
    // Positions are worked out in design units (a 1440-wide window) from the cloud clock, then stretched to the real window:
    // resizing only stretches the sky, it never makes a cloud jump. The wind speeds the clock up, never this number.
    const float cwD = (260.f + 380.f * hashf(i, 3)) * (0.8f + 0.35f * r.layer);
    r.cw = cwD * s;
    const double spanD = 1440.0 + 2.0 * cwD;
    const double posD = loopPos(a.fxCloudT, 9.0 * depth * (0.7 + 0.6 * hashf(i, 4)), hashf(i, 1) * spanD, spanD);
    r.x = (float)(posD / spanD) * (W + 2.f * r.cw) - r.cw;
    const float yFrac = heavy > 0.5f ? 0.04f + 0.60f * hashf(i, 2) : 0.05f + 0.82f * hashf(i, 2);
    r.y = H * yFrac + std::sin(ph(a.now, 0.11) + i) * 9.f * s;
    const float edge = smoothstep(0.f, 0.12f * W, r.x + r.cw * 0.5f) * (1.f - smoothstep(W - 0.12f * W, W, r.x + r.cw * 0.5f));
    r.vis = 0.30f + 0.70f * edge;
    return r;
}

// ---------------------------------------------------------------- clouds (behind the clock)
void drawClouds(Ctx& c, const Frame& F, float k, float heavy) {
    Gfx& g = c.g; App& a = c.a; const float s = c.s, W = c.W, H = c.H;
    const double t = a.now;
    const float gs = std::min(gFx, 1.6f);
    const int n = (int)std::lround(5 + 7 * k + 2 * heavy);
    if (heavy > 0.01f && ceilingReady(c, F)) {
        const float sway = std::sin(ph(t, 0.05)) * 24.f * s;
        g.drawBitmapRect(gCeiling.bmp, sway - 40.f * s, -0.04f * H, W + 80.f * s, gCeilH, clamp01(0.31f * heavy * k * gs));
    }
    for (int i = 0; i < n; ++i) {
        const CloudAt ca = cloudAt(c, i, heavy);
        const int layer = ca.layer; const float cw = ca.cw, x = ca.x, y = ca.y;
        const float f = k * ca.vis * (1.f + 0.30f * heavy);
        const int j = i % 6;
        if (cloudReady(c, F, j)) {
            const Bitmap& bm = gCloud[j].bmp;
            const float sc = cw / std::max(1.f, gCloud[j].nominal);
            const float bw = bm.w / SPR * sc, bh = bm.h / SPR * sc;      // back to full size
            g.drawBitmapRect(bm, x + cw * 0.5f - bw * 0.5f, y - bh * 0.52f, bw, bh, clamp01((0.28f + 0.085f * layer) * f * gs * (1.f + 0.45f * c.a.palTarget.nightness)));   // a touch stronger at night, where a dim cloud needs it to be seen
        } else if (i < 6 && !gCloud[j].bmp.bmp) {                        // no image yet (or none on this system): a few plain clouds
            cloudShape(g, x + cw * 0.5f, y, cw, 1.f, i, F.T, al((0.12f + 0.04f * layer) * f), al((0.09f + 0.03f * layer) * f), al((0.11f + 0.03f * layer) * f), 6);
        }
    }
}

// ---------------------------------------------------------------- fog
// Low, slow banks of fog in layers (thicker and nearer toward the ground) and a veil that thickens downward. Nothing hard-edged.
void drawFog(Ctx& c, const Frame& F, float k) {
    Gfx& g = c.g; App& a = c.a; const float s = c.s, W = c.W, H = c.H;
    const double t = a.now;
    const float gs = std::min(gFx, 1.6f);
    g.vGradientRect(0, H * 0.22f, W, H * 0.78f, withA(F.fog, 0.f), withA(F.fog, al(0.30f * k)));
    for (int row = 0; row < 5; ++row) {
        const int j = row % 3;
        if (!fogReady(c, F, j)) continue;
        const Bitmap& bm = gFog[j].bmp;
        const float depth = 0.55f + 0.11f * row;
        const float sc = 0.85f + 0.12f * row;
        const float bw = bm.w / SPR * sc, bh = bm.h / SPR * sc;
        const double spanD = 1440.0 + bw / s;                            // design units, as for the clouds
        const double speedD = 3.5 * depth * (0.85 + 0.3 * hashf(row, 120));
        const float yc = H * (0.30f + 0.15f * row) + std::sin(ph(t, 0.045 + 0.01 * row, row * 1.7)) * 0.012f * H;
        const float breathe = 0.88f + 0.12f * std::sin(ph(t, 0.06 + 0.013 * row, row * 2.3));
        const float op = clamp01((0.16f + 0.07f * row) * k * breathe * gs);
        for (int cp = 0; cp < 2; ++cp) {                                 // two copies half a loop apart: there is always fog in view
            const float x = (float)(loopPos(a.fxFogT, speedD, hashf(row, 121) * spanD + cp * spanD * 0.5, spanD) / spanD) * (W + bw) - bw;
            g.drawBitmapRect(bm, x, yc - bh * 0.5f, bw, bh, op);
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
constexpr int WPTS = 34, WCURL = 15;                                       // points along a stroke's whole path, and how many of them make its curl

float gWindFlip = 1.f;          // the wind's side (+1 right, -1 left), eased: a change of direction fades the strokes out and back in, never flips them

float easeOut3(float x) { x = clamp01(x); const float r = 1.f - x; return 1.f - r * r * r; }
float easeInOut(float x) { x = clamp01(x); return x * x * (3.f - 2.f * x); }

// The whole path of one stroke, blowing to the right: a gentle wave that grows from its tail toward its head, then (perhaps) a curl that winds inward.
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

// the part of a path between fractions a and b of its length: the path's own points (so the curl stays round) plus the two cut ends.
// v is where each point sits along the cut, 0 at the tail and 1 at the head. Returns how many points.
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

// a tapered ribbon along the points: sharp at both ends, w wide at its fullest, a little past the middle (a brush stroke swells toward its head)
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

} // namespace

// ---------------------------------------------------------------- the one cloud in front of the clock
void drawQuietFront(Ctx& c, float midY, float clockH) {
    Gfx& g = c.g; App& a = c.a; const float s = c.s, W = c.W, H = c.H;
    const float k = a.qfx[0];
    if (!a.st.quietFx) return;
    gFx = std::max(0.4f, std::min(2.f, a.st.quietStrength));
    budgetTick(a); trackSize(c);
    const float q = std::max(0.3f, std::min(1.f, a.fxQuality));
    const float side = windSide(a);
    if (a.qfx[2] > 0.02f) drawFrontDrops(c, a.qfx[2], side, q);     // a few drops in front of the time
    const float calmFrontSnow = a.qfx[6] * (1.f - 0.90f * a.qfx[7]);
    if (calmFrontSnow > 0.02f || a.qfx[7] > 0.02f) {
        const Frame F = makeFrame(c, heavyOf(a));
        if (calmFrontSnow > 0.02f) drawFrontSnow(c, F, calmFrontSnow, side, q);
        if (a.qfx[7] > 0.02f) drawFrontBlizzard(c, F, a.qfx[7], side, q);
    }
    if (k < 0.02f) return;
    const float heavy = heavyOf(a);
    const Frame F = makeFrame(c, heavy);
    if (!frontReady(c, F, clockH)) return;
    const double t = a.now;
    const float bw = gFront.bmp.w / SPR, bh = gFront.bmp.h / SPR;
    const double spanD = 1440.0 + bw / std::max(0.1f, s);
    // starts completely off-screen left (-bw) and exits completely off-screen right (+W) so wrap never teleports mid-screen
    const float bx = (float)(loopPos(a.fxFrontT, 30.0, 0.35 * spanD, spanD) / spanD) * (W + bw) - bw;
    const float y = midY + std::sin(ph(t, 0.075)) * 0.04f * H + 0.015f * H;
    // a soft veil, never a wall: the time stays easy to read through it
    g.drawBitmapRect(gFront.bmp, bx, y - bh * 0.52f, bw, bh, clamp01(0.25f * k * std::min(1.4f, gFx)));
}

// ---------------------------------------------------------------- state
void quietFxStep(App& a) {
    float t[8]; targets(a, t);
    for (int i = 0; i < 8; ++i) a.qfxT[i] = t[i];
    const float kk = 1.f - std::exp(-(float)std::min(a.dt, 1.0) * 1.4f);
    for (int i = 0; i < 8; ++i) {
        a.qfx[i] += (t[i] - a.qfx[i]) * kk;
        if (std::fabs(a.qfx[i] - t[i]) < 0.004f) a.qfx[i] = t[i];
    }
    // the clocks the clouds, fog, wind, snow and blizzard move by (see app.h): they only ever add up, so nothing jumps when the forecast or the window changes
    {
        const double dtc = std::min(std::max(a.dt, 0.0), 0.25);
        const float side = windSide(a);
        a.fxWk += (windKmh(a) - a.fxWk) * (1.f - std::exp(-(float)dtc * 0.7f));
        a.fxCloudT += dtc * side * (1.0 + 0.089 * a.fxWk);
        a.fxFrontT += dtc * side * (1.0 + 0.03 * a.fxWk);
        a.fxFogT += dtc * side * (1.0 + 0.10 * a.fxWk);
        a.fxSnowT += dtc;
        if (a.qfx[1] < 0.01f) gWindFlip = side;
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
    }
    // the light: heavy cloud and storms darken and cool it, fog flattens and mutes it, snow brings a gentle cool wintry overcast, blizzard brings deep stormy chill + blowing haze.
    {
        const float cl = clamp01((a.qfx[0] - 0.3f) / 0.7f);                // 0 at a third of the sky, 1 under a solid lid
        float gl = 0.90f * std::pow(cl, 1.4f) + 0.15f * a.qfx[2] + 0.12f * a.qfx[6];
        gl = std::max(gl, std::max(std::max(0.97f * a.qfx[4], a.qfx[3]), 0.82f * a.qfx[7]));
        const float hz = std::max(std::max(a.qfx[5], 0.16f * a.qfx[6]), 0.34f * a.qfx[7]);
        gl *= 1.f - 0.55f * a.qfx[5] * (1.f - 0.65f * a.qfx[7]);          // calm fog is milky; a blizzard stays dramatic and stormy
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

void quietFxRelease(gfx::Gfx& g) { freeSlot(g, gRainStreak); for (auto& s : gRainSplash) freeSlot(g, s); for (auto& s : gCloud) freeSlot(g, s); for (auto& s : gFog) freeSlot(g, s); freeSlot(g, gCeiling); freeSlot(g, gMist); freeSlot(g, gFront); freeSlot(g, gSnowFlake); freeSlot(g, gBlizzStreak); freeSlot(g, gBlizzDrift); }

std::string quietFxDescribe(const App& a) {
    if (!a.st.quietFx) return "Switched off. No weather is drawn behind the clock.";
    float t[8]; targets(a, t);
    if (a.st.quietPreview != 0) {
        static const char* n[12] = {"", "clouds", "wind", "clouds and wind", "rain", "a severe storm with a tornado", "thunder and cloud with no rain", "a thunderstorm with rain", "fog", "the sun flare", "snowfall", "a harsh snowstorm (blizzard)"};
        return std::string("Previewing ") + n[std::max(1, std::min(11, a.st.quietPreview))] + ". Set it back to Live to follow the real weather.";
    }
    if (!a.wx.valid) return "No weather yet, so nothing is drawn.";
    std::string parts; int cnt = 0;
    auto add = [&](const char* p) { parts += std::string(cnt++ ? ", " : "") + p; };
    if (t[3] > 0) return "A severe storm: a tornado twists up behind the clock, with lightning.";
    if (t[7] > 0.20f) return t[4] > 0 ? "A thundersnow blizzard: driving snow, howling wind and lightning." : "A harsh snowstorm: wind-driven snow and swirling spindrift across the clock.";
    if (t[4] > 0) return t[2] > 0 ? "A thunderstorm: heavy cloud, lightning and rain." : "Thunder with cloud and no rain: heavy cloud and lightning.";
    if (t[5] > 0.6f) return "Fog: soft banks drift low behind the clock.";
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
    if (a.st.theme == THEME_MIDNIGHT) return;                                         // a deep night sky has no flare
    const bool sunlit = a.st.theme == THEME_SUNLIT || a.st.theme == THEME_SUNLIT2;
    float alt = (float)a.sun.hor.alt, az = (float)a.sun.hor.az;
    bool morning = az < 180.f;
    if (preview && (sunlit ? alt < -3.f : !(alt > -4.f && alt < 18.f))) { alt = 3.f; az = 262.f; morning = false; }       // the preview shows it at any hour, as an evening one
    float k = smoothstep(-3.5f, 0.5f, alt) * (1.f - smoothstep(6.f, 17.f, alt));
    if (sunlit) k = smoothstep(-3.5f, 0.5f, alt) * (0.58f + 0.42f * (1.f - smoothstep(6.f, 30.f, alt)));
    if (preview) k = std::max(k, 0.9f);
    if (k < 0.01f) return;
    const float covered = std::max(std::max(a.qfx[0], a.qfx[5]), std::max(std::max(a.qfx[2], std::max(a.qfx[6], a.qfx[7])), a.qfx[4]));      // clouds, fog, rain, snow, blizzard, thunder (eased)
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

// Rain, snow & blizzard on the Sky face: drawn behind the sky dome and labels so every label stays legible.
void drawSkyRain(Ctx& c) {
    App& a = c.a;
    if (!a.st.quietFx || !a.st.skyRain) return;
    if (a.qfx[2] < 0.01f && a.qfx[6] < 0.01f && a.qfx[7] < 0.01f) return;
    gFx = std::max(0.4f, std::min(2.f, a.st.quietStrength));
    budgetTick(a); trackSize(c);
    const float side = windSide(a), q = std::max(0.3f, std::min(1.f, a.fxQuality));
    const Frame F = makeFrame(c, heavyOf(a));
    const float calmSnow = a.qfx[6] * (1.f - 0.90f * a.qfx[7]);
    if (a.qfx[2] > 0.01f) drawRain(c, F, a.qfx[2] * 0.7f, side, q * 0.7f, true);
    if (calmSnow > 0.01f) drawSnow(c, F, calmSnow * 0.85f, side, q * 0.75f, true);
    if (a.qfx[7] > 0.01f) drawBlizzard(c, F, a.qfx[7] * 0.78f, side, q * 0.72f, true);
}

void drawQuietFx(Ctx& c) {
    App& a = c.a;
    drawScenery(c);                                                           // behind the weather: a cloud drifts over it
    if (!a.st.quietFx) return;
    if (a.qfx[0] + a.qfx[1] + a.qfx[2] + a.qfx[3] + a.qfx[4] + a.qfx[5] + a.qfx[6] + a.qfx[7] < 0.01f) return;
    gFx = std::max(0.4f, std::min(2.f, a.st.quietStrength));
    budgetTick(a); trackSize(c);
    const float side = windSide(a), q = std::max(0.3f, std::min(1.f, a.fxQuality));
    const float heavy = heavyOf(a);
    const Frame F = makeFrame(c, heavy);
    const float calmSnow = a.qfx[6] * (1.f - 0.90f * a.qfx[7]);
    const float windVis = a.qfx[1] * (1.f - 0.78f * a.qfx[7]);
    const float fogVis = a.qfx[5] * (1.f - 0.85f * a.qfx[7]);      // blizzard renders its own billowing whiteout drift curtains
    if (a.qfx[0] > 0.01f) drawClouds(c, F, a.qfx[0], heavy);
    if (fogVis > 0.01f) drawFog(c, F, fogVis);
    if (a.qfx[4] > 0.01f) drawLightning(c, a.qfx[4], a.qfx[3] > 0.01f, a.qfx[0], heavy);
    if (a.qfx[3] > 0.01f) drawTornado(c, a.qfx[3], side, q);
    if (a.qfx[2] > 0.01f) drawRain(c, F, a.qfx[2], side, q);
    if (calmSnow > 0.01f) drawSnow(c, F, calmSnow, side, q);
    if (a.qfx[7] > 0.01f) drawBlizzard(c, F, a.qfx[7], side, q);
    if (windVis > 0.01f) drawWind(c, F, windVis, q);
}

// Focus tab ambient backdrop: lets users pair their focus session with soothing rain, snow, blizzard, clouds, fog, storm, stars or live weather.
void drawFocusBackdrop(Ctx& c) {
    App& a = c.a;
    const int mode = a.st.focusFx;
    if (mode <= 0) return;
    gFx = std::max(0.55f, std::min(1.6f, a.st.quietStrength));
    budgetTick(a); trackSize(c);
    const float q = std::max(0.35f, std::min(1.f, a.fxQuality));
    if (mode == 1) {                                          // LIVE: outdoor weather + theme scenery
        drawQuietFx(c);
        return;
    }
    if (mode == 7) {                                          // STARS: deep starry cosmos + occasional serene meteor
        drawSpaceBackdropOnly(c);
        return;
    }
    const float heavy = (mode == 6) ? 0.85f : (mode == 8 ? 0.80f : (mode == 2 ? 0.30f : 0.15f));
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
        case 4:                                               // CLOUDS: slow drifting clouds
            drawClouds(c, F, 0.88f, 0.12f);
            break;
        case 5:                                               // FOG: calm mountain mist
            drawClouds(c, F, 0.35f, 0.10f);
            drawFog(c, F, 0.92f);
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
        default: break;
    }
}

} // namespace app
