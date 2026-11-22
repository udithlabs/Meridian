// Meridian: the scenery behind the Quiet and Sky faces for two themes.
//  - Midnight: a deep night sky. Dim stars, a faint band of the Milky Way, soft nebula, a ringed planet and a far galaxy sit behind everything. Now and
//    then a meteor crosses, now and then a comet drifts by, and once a day (in the evening, at a moment picked from the date) a flying saucer darts across.
//  - Sunlit II: soft rays of sunlight fanning out from the Sun and a few wisps of high cloud, on top of the sun flare (scene_quiet.cpp).
// Like the weather, all of it is a pure function of the time: nothing is remembered between frames, so a frame can be drawn at any moment. The still
// parts are drawn once into one image and just copied to the screen; only the meteors, the comet and the saucer move, and the program only draws fast
// frames while one of them is on screen (spaceBusyNow / spaceNextStart tell the frame loop in main.cpp when that is).
#include "scene.h"
#include "daily.h"
#include "ufo_audio.h"
#include <cmath>
#include <algorithm>

namespace app {

using namespace gfx;

namespace {

constexpr float PI_F = 3.14159265f;

float hashf(int i, int salt) {
    unsigned x = (unsigned)i * 374761393u + (unsigned)salt * 668265263u;
    x = (x ^ (x >> 13)) * 1274126177u; x ^= x >> 16;
    return (x & 0xFFFF) / 65535.f;
}
float ph(double t, double speed, double off = 0.0) { return (float)std::fmod(t * speed + off, 6.283185307179586); }
float loopPos(double t, double speed, double start, double span) { double v = std::fmod(start + speed * t, span); if (v < 0) v += span; return (float)v; }
uint64_t mixKey(uint64_t a, uint64_t b) { return (a ^ (b + 0x9E3779B97F4A7C15ull + (a << 6) + (a >> 2))) | 1ull; }
float ease(float x) { x = clamp01(x); return x * x * (3.f - 2.f * x); }
float smoother(float x) { x = clamp01(x); return x * x * x * (x * (x * 6.f - 15.f) + 10.f); }

// ---------------------------------------------------------------- cached images
struct Slot { Bitmap bmp; uint64_t key = 0, failed = 0; };
Slot gBg, gNebulaPulse, gMidnightBg, gTail[4], gShip, gRays, gCirrus;  // tails: white, warm, green, ion (cyan)
int gBudget = 0; double gBudgetAt = -1e18;

// (Re)draws an image when its key changed, a couple per frame at most. True when there is an image to use.
template <class Build> bool ensure(Ctx& c, Slot& s, uint64_t key, Build build) {
    if (c.a.now != gBudgetAt) { gBudgetAt = c.a.now; gBudget = std::max(1, c.a.spriteBudget); }
    if (s.bmp.bmp && s.key == key) return true;
    if (s.failed == key || gBudget <= 0) return s.bmp.bmp != nullptr;
    --gBudget;
    Bitmap nb = build();
    if (!nb.bmp) { s.failed = key; return s.bmp.bmp != nullptr; }
    c.g.freeBitmap(s.bmp); s.bmp = nb; s.key = key;
    return true;
}
int scaleStep(float s) { return (int)std::lround(std::log2(std::max(0.05f, s)) * 5.f); }

std::vector<Pt> ellipsePts(float cx, float cy, float rx, float ry, float a0, float a1, int n) {      // angles in radians, 0 = right, positive = down
    std::vector<Pt> p; p.reserve(n + 1);
    for (int i = 0; i <= n; ++i) { const float a = a0 + (a1 - a0) * i / n; p.push_back({cx + rx * std::cos(a), cy + ry * std::sin(a)}); }
    return p;
}

// ---------------------------------------------------------------- when things happen
// Each kind of event has time slots: a slot is a stretch of time that may hold one event, with a chance, at a place inside the slot worked out from the
// slot number. Nothing is stored: given a time we can say what is going on, and when the next one starts.
struct Kind { double slot, dur, lo, hi; float chance; int salt; };
const Kind kMeteorA = {5.5, 0.88, 0.15, 4.3, 0.62f, 100};
const Kind kMeteorB = {8.3, 0.72, 0.25, 7.1, 0.52f, 200};
const Kind kComet   = {240.0, 15.0, 5.0, 215.0, 0.50f, 300};
// The saucer is not one of the slotted events: it flies once a day, for 9.0 seconds (~7 s on screen), at a moment in the local evening (19:00 to 23:00)
// picked from the date (daily.h), so it is the same after a restart and never twice in a day. Pressing U calls it by hand, outside that.
constexpr double kShipDur = 9.0; constexpr double kShipLo = 19.0, kShipHi = 23.0; constexpr int kShipSalt = 400;
constexpr double kNutsSlot = 30.0; constexpr double kNutsDelay = 1.5;   // aliensarenuts: twice every 1 minute (every 30 s)

bool slotEvent(const Kind& K, long long k, double& start) {
    if (hashf((int)(k & 0x7FFFFFFF), K.salt + 1) > K.chance) return false;
    start = (double)k * K.slot + K.lo + (K.hi - K.lo) * hashf((int)(k & 0x7FFFFFFF), K.salt + 2);
    return true;
}
// is an event of this kind on at time t? gives its slot number and the progress 0..1
bool activeAt(const Kind& K, double t, long long& k, float& u) {
    k = (long long)std::floor(t / K.slot);
    double st;
    if (!slotEvent(K, k, st)) return false;
    if (t < st || t > st + K.dur) return false;
    u = (float)((t - st) / K.dur);
    return true;
}
double nextStart(const Kind& K, double t) {
    long long k0 = (long long)std::floor(t / K.slot);
    for (long long k = k0; k < k0 + 6; ++k) { double st; if (slotEvent(K, k, st) && st > t) return st; }
    return 1e18;
}
bool spaceOn(const App& a) { return a.st.theme == THEME_GALAXY && a.st.spaceFx && (a.st.face == FACE_QUIET || a.st.face == FACE_SKY) && !a.packTheme(); }
bool midnightOn(const App& a) { return a.st.theme == THEME_MIDNIGHT && (a.st.face == FACE_QUIET || a.st.face == FACE_SKY || a.st.face == FACE_FOCUS) && !a.packTheme(); }
bool shipOn(const App& a, double t, float& u, long long& k, uint64_t& flightId) {       // the saucer: today's flight, U key, or aliensarenuts (2x/min)
    if (!a.st.aliensEnabled) return false;
    if (a.alienAt > 0 && t >= a.alienAt && t <= a.alienAt + kShipDur) {
        u = (float)((t - a.alienAt) / kShipDur);
        k = (long long)(a.alienSeq ? (a.alienSeq * 2654435761ULL) : (uint64_t)(a.alienAt * 10.0)) & 0xFFFFFF;
        flightId = (1ULL << 62) | std::max<uint64_t>(1ULL, a.alienSeq);
        return true;
    }
    if (a.aliensNuts && t >= a.aliensNutsSince) {
        const double rel = t - a.aliensNutsSince;
        const long long slot = (long long)std::floor(rel / kNutsSlot);
        const double inSlot = rel - (double)slot * kNutsSlot;
        if (inSlot >= kNutsDelay && inSlot <= kNutsDelay + kShipDur) {
            u = (float)((inSlot - kNutsDelay) / kShipDur);
            k = (long long)((uint64_t)(slot + 7) * 2654435761ULL) & 0xFFFFFF;
            flightId = (1ULL << 61) | (uint64_t)(slot + 1);
            return true;
        }
    }
    double st;
    if (!daily::activeAt(t, a.tzOff, kShipDur, kShipLo, kShipHi, kShipSalt, st)) return false;
    const long long day = daily::dayOf(t, a.tzOff);
    u = (float)((t - st) / kShipDur);
    k = day & 0xFFFFFF;
    flightId = (1ULL << 60) | (uint64_t)(day + 1000000LL);
    return true;
}

// how much the weather hides the sky: cloud, fog, drizzle, rain, sleet, hail, snow, blizzard and storms put the stars out
float skyCover(const App& a) {
    float c = std::max(0.9f * a.qfx[0], a.qfx[5]);
    c = std::max(c, std::max(0.8f * a.qfx[2], std::max(0.72f * a.qfx[6], 0.95f * a.qfx[7])));
    c = std::max(c, std::max(0.65f * a.qfx[8], std::max(0.85f * a.qfx[9], 0.95f * a.qfx[10])));
    c = std::max(c, std::max(a.qfx[4], 0.35f * a.qfx[11]));
    return clamp01(c);
}

// ---------------------------------------------------------------- Midnight & Stars: deep-space cosmos
// Authentic O-B-A-F-G-K-M stellar spectral palette
Color starSpectral(float tc) {
    if (tc < 0.17f) return rgb(0x94BAFF);      // hot O/B sapphire
    if (tc < 0.42f) return rgb(0xD2E4FF);      // A blue-white
    if (tc < 0.68f) return rgb(0xF6F8FF);      // F diamond white
    if (tc < 0.84f) return rgb(0xFFF0D0);      // G warm sunlight cream
    if (tc < 0.94f) return rgb(0xFFCF8C);      // K golden amber
    return rgb(0xFFA673);                      // M warm coral-orange giant
}

// Delicate 4-point optical diffraction starburst for bright jewel stars
void drawStarGlint(Gfx& g, float x, float y, float len, float s, const Color& col, float alpha) {
    if (alpha < 0.01f || len < 2.f) return;
    const float wOuter = std::max(0.9f, 1.35f * s), wCore = std::max(0.6f, 0.72f * s);
    const Color cWhite = lerp(col, rgb(0xFFFFFF), 0.55f);
    // Horizontal & vertical main diffraction spikes
    g.line(x - len, y, x + len, y, withA(col, alpha * 0.34f), wOuter, true);
    g.line(x, y - len * 0.92f, x, y + len * 0.92f, withA(col, alpha * 0.34f), wOuter, true);
    g.line(x - len * 0.68f, y, x + len * 0.68f, y, withA(cWhite, alpha * 0.82f), wCore, true);
    g.line(x, y - len * 0.62f, x, y + len * 0.62f, withA(cWhite, alpha * 0.82f), wCore, true);
    // Subtle short diagonal secondary glints
    const float d = len * 0.34f;
    g.line(x - d, y - d, x + d, y + d, withA(col, alpha * 0.24f), wCore, true);
    g.line(x - d, y + d, x + d, y - d, withA(col, alpha * 0.24f), wCore, true);
}

// Rotated ellipse points helper (tiltRad > 0 rotates clockwise on screen)
std::vector<Pt> rotEllipsePts(float cx, float cy, float rx, float ry, float tiltRad, float a0, float a1, int n) {
    const float ct = std::cos(tiltRad), st = std::sin(tiltRad);
    std::vector<Pt> p; p.reserve((size_t)n + 1);
    for (int i = 0; i <= n; ++i) {
        const float a = a0 + (a1 - a0) * i / n;
        const float ex = rx * std::cos(a), ey = ry * std::sin(a);
        p.push_back({cx + ex * ct - ey * st, cy + ex * st + ey * ct});
    }
    return p;
}

// Helper for Milky Way diagonal spine curve (u in 0..1 from lower-left to upper-right)
void milkySpine(float u, float W, float H, float& sx, float& sy) {
    sx = (-0.08f + 1.16f * u) * W;
    sy = (0.91f - 0.80f * u) * H + std::sin(u * 4.8f - 0.6f) * 0.038f * H;
}

Bitmap buildSky(Ctx& c) {
    Gfx& g = c.g; const float W = c.W, H = c.H, s = c.s;
    const int sw = (int)W, sh = (int)H;
    if (sw < 64 || sh < 64 || (double)sw * sh > 1.6e7 || !g.beginSprite(sw, sh)) return Bitmap{};
    const float R = std::max(W, H);
    const float Rmin = std::min(W, H);

    // 1. Sculpted organic deep-space emission & reflection nebulae (curved multi-puff cosmic clouds, never flat round spotlights)
    auto nebulaRidge = [&](float x0, float y0, float x1, float y1, float bend, float scaleR, int puffs, int salt,
                           const Color& cDeep, const Color& cMid, const Color& cCore, float strength) {
        const float dx = x1 - x0, dy = y1 - y0;
        const float len = std::max(1.f, std::hypot(dx, dy));
        const float nx = -dy / len, ny = dx / len;
        for (int i = 0; i < puffs; ++i) {
            const float u = (i + 0.35f + 0.30f * hashf(i, salt + 1)) / (float)puffs;
            const float env = std::sin(u * PI_F);
            const float curve = std::sin(u * PI_F) * bend + (hashf(i, salt + 2) - 0.5f) * scaleR * 0.55f;
            const float px = x0 + dx * u + nx * curve;
            const float py = y0 + dy * u + ny * curve;
            const float rBase = scaleR * (0.55f + 0.72f * env) * (0.78f + 0.44f * hashf(i, salt + 3));
            const float op = strength * (0.45f + 0.55f * env);
            g.glowEllipse(px, py, rBase * 1.38f, rBase * 0.88f, cDeep, op * 0.62f);
            g.glowEllipse(px + nx * rBase * 0.16f, py + ny * rBase * 0.16f, rBase * 0.88f, rBase * 0.62f, cMid, op * 0.48f);
            if (env > 0.45f && (i & 1) == 0)
                g.glow(px - nx * rBase * 0.12f, py - ny * rBase * 0.12f, rBase * 0.48f, cCore, op * 0.34f);
        }
    };
    // Upper-left Orion / Trifid violet-magenta-cyan cosmic cloud ridge
    nebulaRidge(0.02f * W, 0.44f * H, 0.34f * W, 0.12f * H, -0.05f * Rmin, 0.135f * R, 11, 110,
                rgb(0x5232A8), rgb(0xA64492), rgb(0x5CE0F8), 0.16f);
    // Lower-right Carina / Lagoon oceanic teal-sapphire-amber cosmic veil
    nebulaRidge(0.64f * W, 0.94f * H, 0.99f * W, 0.52f * H, 0.06f * Rmin, 0.145f * R, 12, 130,
                rgb(0x1D5EA6), rgb(0x289EC0), rgb(0xE8A864), 0.16f);
    // Upper-mid subtle Rosette / Perseus lilac-blue cosmic whisper
    nebulaRidge(0.42f * W, 0.05f * H, 0.74f * W, 0.20f * H, -0.03f * Rmin, 0.105f * R, 8, 150,
                rgb(0x6C3A98), rgb(0x4672CC), rgb(0xB89EFF), 0.11f);

    // 2. Sculpted Milky Way galactic band with two luminous arms separated by a meandering Great Dark Rift
    for (int i = 0; i < 24; ++i) {
        const float u = (i + 0.5f) / 24.f;
        float sx, sy; milkySpine(u, W, H, sx, sy);
        const float coreW = 0.65f + 0.55f * std::sin(u * PI_F);               // fullest in the galactic core arc
        const Color outerCol = (i & 1) ? rgb(0x4C5CC4) : rgb(0x3A6AA8);
        g.glowEllipse(sx, sy, 0.18f * R * coreW, 0.085f * R * coreW, outerCol, 0.095f * coreW);
    }
    // Luminous upper & lower galactic star-cloud arms flanking the dark interstellar dust lane
    for (int i = 0; i < 46; ++i) {
        const float u = (i + 0.3f + 0.4f * hashf(i, 21)) / 46.f;
        float sx, sy; milkySpine(u, W, H, sx, sy);
        const float armSign = (i & 1) ? 1.f : -1.f;
        // Normal to Milky Way diagonal (~-35 deg) -> offset cleanly off the dark rift centerline
        const float riftOff = armSign * (0.028f + 0.036f * hashf(i, 22)) * H;
        const float cx = sx - riftOff * 0.52f;
        const float cy = sy + riftOff * 0.85f;
        const float coreWarm = std::exp(-std::pow((u - 0.34f) / 0.26f, 2.f)); // warm Sagittarius star clouds toward lower-left/mid
        const Color cloudCol = lerp(
            (hashf(i, 23) < 0.45f ? rgb(0x98B8FF) : rgb(0xCFC2FF)),
            rgb(0xFFE0B0),
            0.55f * coreWarm * hashf(i, 24));
        const float rCloud = (0.045f + 0.055f * hashf(i, 25)) * Rmin;
        const float op = (0.068f + 0.052f * hashf(i, 26)) * (0.65f + 0.50f * std::sin(u * PI_F));
        g.glowEllipse(cx, cy, rCloud * 1.45f, rCloud * 0.78f, cloudCol, op);
    }
    // Delicate translucent H-II pink star-forming nebula knots embedded in the Milky Way
    for (int i = 0; i < 6; ++i) {
        const float u = 0.14f + 0.74f * hashf(i, 27);
        float sx, sy; milkySpine(u, W, H, sx, sy);
        const float off = (hashf(i, 28) - 0.5f) * 0.09f * H;
        g.glow(sx - off * 0.5f, sy + off * 0.85f, (26.f + 22.f * hashf(i, 29)) * s, rgb(0xE068A2), 0.11f);
    }

    // 3. Multi-magnitude spectral starfield (~1,400 crisp circular stars: galactic powder + deep field + bright jewels)
    const float areaScale = clamp01((W * H) / (1920.f * 1080.f));
    // Tier 1: dense glittering Milky Way stardust powder (clumped along the two arms, thinning across the Dark Rift)
    const int nPowder = (int)std::lround(480.f + 560.f * areaScale);
    for (int i = 0; i < nPowder; ++i) {
        const float u = hashf(i, 31);
        float sx, sy; milkySpine(u, W, H, sx, sy);
        // Gaussian-like spread across galactic width, nudged slightly outside the Dark Rift lane
        const float gRnd = (hashf(i, 32) + hashf(i, 33) - 1.f);
        const float armNudge = (gRnd >= 0.f ? 0.012f : -0.012f) * H;
        const float perp = gRnd * 0.155f * H + armNudge;
        const float x = sx - perp * 0.54f;
        const float y = sy + perp * 0.84f;
        if (x < 0.f || x > W || y < 0.f || y > H) continue;
        // Dark Rift dust attenuation right near perp == 0
        const float riftDip = smoothstep(0.005f * H, 0.032f * H, std::fabs(perp));
        const float h = hashf(i, 34);
        const float al = (0.18f + 0.52f * h * h) * (0.35f + 0.65f * riftDip);
        const Color col = starSpectral(hashf(i, 35));
        const float rad = std::max(0.55f, (0.58f + 0.62f * h * h) * s);
        g.fillCircle(x, y, rad, withA(col, al));
    }
    // Tier 2: crisp deep-field stars across the entire cosmos
    const int nField = (int)std::lround(260.f + 220.f * areaScale);
    for (int i = 0; i < nField; ++i) {
        const float x = hashf(i, 41) * W, y = hashf(i, 42) * H;
        const float h = hashf(i, 43);
        const float al = 0.24f + 0.66f * std::pow(h, 1.55f);
        const Color col = starSpectral(hashf(i, 44));
        const float rad = std::max(0.65f, (0.68f + 1.05f * h * h) * s);
        if (h > 0.72f) g.glow(x, y, rad * 3.8f, col, 0.22f * al);
        g.fillCircle(x, y, rad, withA(col, al));
    }
    // Tier 3: brilliant hero jewel stars with soft colored halos and 4-point optical diffraction glints
    const int nHero = 42;
    for (int i = 0; i < nHero; ++i) {
        const float x = (0.03f + 0.94f * hashf(i, 45)) * W;
        const float y = (0.04f + 0.92f * hashf(i, 46)) * H;
        const float h = hashf(i, 47);
        const Color col = starSpectral(hashf(i, 48));
        const float rad = std::max(1.2f, (1.25f + 1.05f * h) * s);
        g.glow(x, y, (6.5f + 8.5f * h) * s, col, 0.34f + 0.22f * h);
        g.glow(x, y, (2.6f + 2.2f * h) * s, rgb(0xFFFFFF), 0.70f);
        g.fillCircle(x, y, rad, withA(lerp(col, rgb(0xFFFFFF), 0.55f), 0.96f));
        if (i < 18) drawStarGlint(g, x, y, (6.5f + 8.5f * h) * s, s, col, 0.55f + 0.35f * h);
    }

    // 4. Pleiades ("Seven Sisters") sapphire reflection nebula & open star cluster (upper-left open sky)
    {
        const float px0 = 0.215f * W, py0 = 0.230f * H, sc = std::max(0.55f, s);
        g.glowEllipse(px0, py0, 62.f * sc, 42.f * sc, rgb(0x528EFF), 0.22f);
        g.glowEllipse(px0 + 8.f * sc, py0 - 4.f * sc, 38.f * sc, 26.f * sc, rgb(0x98C6FF), 0.24f);
        struct Sis { float dx, dy, r; bool glint; };
        static const Sis sis[9] = {
            {-22.f,  -4.f, 1.9f, true},  {-10.f,  -9.f, 2.2f, true},  {  2.f,  -2.f, 2.5f, true},
            { 14.f,   5.f, 2.0f, true},  { 26.f,   2.f, 1.7f, false}, { 16.f, -13.f, 1.6f, false},
            { -6.f,  10.f, 1.5f, false}, {-18.f,  12.f, 1.2f, false}, {  6.f,  15.f, 1.3f, false},
        };
        for (const Sis& m : sis) {
            const float sx = px0 + m.dx * sc, sy = py0 + m.dy * sc;
            g.glow(sx, sy, m.r * 5.2f * sc, rgb(0x88BAFF), 0.44f);
            g.fillCircle(sx, sy, std::max(1.0f, m.r * sc), rgb(0xF0F7FF));
            if (m.glint) drawStarGlint(g, sx, sy, m.r * 4.2f * sc, sc, rgb(0x98C4FF), 0.62f);
        }
    }

    // 5. Masterpiece Sculpted Spiral Galaxy (Andromeda-style, upper-right open sky — clear of Focus Weather card & Orb)
    {
        const float gx = 0.805f * W, gy = 0.215f * H;
        const float gr = std::max(40.f * s, 0.072f * Rmin);
        const float tilt = 0.34f;                                             // +19.5 deg graceful slant
        const float ct = std::cos(tilt), st = std::sin(tilt), squash = 0.36f;
        auto galPt = [&](float lx, float ly, float& ox, float& oy) {
            const float sy2 = ly * squash;
            ox = gx + lx * ct - sy2 * st;
            oy = gy + lx * st + sy2 * ct;
        };
        // Continuous luminous inner & outer galactic disc mist (prevents any hollow ring look)
        for (int k = -6; k <= 6; ++k) {
            const float u = k / 6.f;
            float ox, oy; galPt(u * gr * 1.35f, 0.f, ox, oy);
            const float env = std::exp(-u * u * 1.9f);
            g.glow(ox, oy, gr * (0.42f + 0.32f * env), lerp(rgb(0x6A86D6), rgb(0xC6D2FF), env), 0.16f * env);
        }
        // Two feathered open-ended logarithmic spiral arms (smooth overlapping wisps, never closing into a hard ring)
        for (int arm = 0; arm < 2; ++arm) {
            const float aBase = arm * PI_F;
            const int steps = 42;
            for (int i = 0; i < steps; ++i) {
                const float f = (float)i / (float)(steps - 1);
                const float th = aBase + 0.25f + f * 3.85f;                   // ~220 deg graceful open spiral sweep
                const float rad = gr * (0.10f + 1.48f * std::pow(f, 1.08f));
                float ox, oy; galPt(rad * std::cos(th), rad * std::sin(th), ox, oy);
                const float env = smoothstep(0.f, 0.10f, f) * std::pow(1.f - f, 0.85f);
                const Color armCol = lerp(rgb(0xFFE5BF), rgb(0x88B4FF), clamp01(f * 1.35f));
                g.glow(ox, oy, gr * (0.24f - 0.08f * f), armCol, 0.17f * env);
                if (i % 8 == 3 && f > 0.18f && f < 0.78f)
                    g.glow(ox, oy, gr * 0.10f, rgb(0xF084B8), 0.20f * env);   // H-II star-forming knot
                // Scattered organic starlight dust along & around the arm
                for (int d = 0; d < 2; ++d) {
                    const int id = arm * 200 + i * 2 + d;
                    const float jitR = (hashf(id, 53) - 0.5f) * gr * (0.22f + 0.16f * f);
                    const float jitA = (hashf(id, 55) - 0.5f) * 0.28f;
                    float sx, sy; galPt((rad + jitR) * std::cos(th + jitA), (rad + jitR) * std::sin(th + jitA), sx, sy);
                    g.fillCircle(sx, sy, std::max(0.55f, (0.55f + 0.55f * hashf(id, 54)) * s),
                                 withA(lerp(armCol, rgb(0xFFFFFF), 0.50f), 0.58f * env * (0.6f + 0.4f * hashf(id, 56))));
                }
            }
        }
        // Radiant warm golden galactic bulge & incandescent core
        g.glow(gx, gy, gr * 0.78f, rgb(0xB8C8FF), 0.26f);
        g.glow(gx, gy, gr * 0.46f, rgb(0xFFE6BE), 0.52f);
        g.glow(gx, gy, gr * 0.18f, rgb(0xFFFDF8), 0.88f);
        g.fillCircle(gx, gy, std::max(1.8f, gr * 0.050f), withA(rgb(0xFFFFFF), 0.96f));
        // Companion dwarf elliptical galaxy (M32-style) hovering softly below-left
        float mx, my; galPt(-gr * 0.68f, gr * 0.92f, mx, my);
        g.glow(mx, my, gr * 0.26f, rgb(0xC8D4FF), 0.26f);
        g.glow(mx, my, gr * 0.10f, rgb(0xFFF0D4), 0.55f);
    }

    // 6. Masterpiece Sculpted Tilted Saturn (bottom-left: axial tilt, cloud bands, Cassini Division gap, ring shadow, moons)
    {
        const float pr = std::max(22.f * s, 0.042f * H);
        const float rx = pr * 2.22f, ry = pr * 0.52f, tilt = -0.27f;          // -15.5 deg graceful tilt
        const float px = std::max(0.090f * W, rx * 1.16f + 12.f * s);         // keeps full ring & moons clear of left edge even in narrow portrait
        const float py = std::min(0.868f * H, H - pr * 1.48f - 10.f * s);
        const float ct = std::cos(tilt), st = std::sin(tilt);
        const float A = 0.82f;
        const Color night = rgb(0x050A1E);

        // Soft ambient starlight halo around the planet
        g.glow(px, py, pr * 1.65f, rgb(0x9CB2DF), 0.16f);

        // Multi-band icy rings helper (C-ring inner smoky, B-ring bright dense, Cassini gap, A-ring crisp outer)
        auto drawRings = [&](float a0, float a1, float frontBoost) {
            struct RB { float f; Color col; float alpha, w; };
            static const RB bands[6] = {
                {1.00f, rgb(0xC9B490), 0.40f, 1.5f},   // outer A-ring edge
                {0.93f, rgb(0xDEC9A4), 0.52f, 2.2f},   // A-ring body
                // Cassini Division dark gap around f = 0.86!
                {0.79f, rgb(0xF2DEC0), 0.70f, 2.7f},   // bright dense outer B-ring
                {0.71f, rgb(0xDFC498), 0.62f, 2.5f},   // inner B-ring
                {0.63f, rgb(0xA99374), 0.34f, 1.9f},   // inner smoky C-ring
                {0.57f, rgb(0x88755C), 0.18f, 1.4f},   // innermost gossamer ring
            };
            for (const RB& b : bands) {
                const auto pts = rotEllipsePts(px, py, rx * b.f, ry * b.f, tilt, a0, a1, 72);
                g.polyline(pts, false, withA(b.col, clamp01(b.alpha * A * frontBoost)), std::max(1.2f, b.w * s), false);
            }
        };

        // Far half of the tilted rings (behind sphere)
        drawRings(PI_F, 2.f * PI_F, 0.78f);

        // Opaque 3D lit sphere (sunlit warm golden upper-left -> ochre -> deep cosmic shadow lower-right)
        g.radialFillOffset(px, py, pr, -0.38f * pr, -0.34f * pr, {
            {0.00f, lerp(night, rgb(0xF8E3BD), A)},
            {0.48f, lerp(night, rgb(0xC89D6C), A)},
            {0.82f, lerp(night, rgb(0x5B412C), A)},
            {1.00f, lerp(night, rgb(0x130E16), A)}
        });

        // Analytical curved equatorial cloud bands (strictly inside circle r = 0.985*pr, rotated by tilt — zero layer clip!)
        struct Band { float y0, y1; Color col; float a; };
        static const Band bnds[5] = {
            {-0.68f, -0.44f, rgb(0xA67D52), 0.24f},
            {-0.26f, -0.06f, rgb(0xFFF0D0), 0.26f},   // bright creamy equatorial zone
            { 0.08f,  0.26f, rgb(0x8F643E), 0.28f},   // warm ochre tropical belt
            { 0.34f,  0.50f, rgb(0xC49F72), 0.18f},
            { 0.56f,  0.76f, rgb(0x523E30), 0.24f},   // muted polar hood
        };
        const float rBand = pr * 0.98f;
        for (const Band& bd : bnds) {
            std::vector<Pt> poly; poly.reserve(24);
            const int seg = 10;
            const float ya = bd.y0 * rBand, yb = bd.y1 * rBand;
            const float xa = std::sqrt(std::max(0.f, rBand * rBand - ya * ya));
            const float xb = std::sqrt(std::max(0.f, rBand * rBand - yb * yb));
            for (int k = 0; k <= seg; ++k) {
                const float f = -1.f + 2.f * k / seg;
                const float lx = xa * f, ly = ya + 0.06f * pr * (1.f - f * f);
                poly.push_back({px + lx * ct - ly * st, py + lx * st + ly * ct});
            }
            for (int k = seg; k >= 0; --k) {
                const float f = -1.f + 2.f * k / seg;
                const float lx = xb * f, ly = yb + 0.06f * pr * (1.f - f * f);
                poly.push_back({px + lx * ct - ly * st, py + lx * st + ly * ct});
            }
            g.fillPolygon(poly, withA(bd.col, bd.a * A));
        }

        // Subtle curved ring shadow across the planet's lower hemisphere + crisp sunlit crescent limb
        {
            const auto shPts = rotEllipsePts(px, py + 0.16f * pr, pr * 0.92f, pr * 0.20f, tilt, 0.22f, PI_F - 0.22f, 28);
            g.polyline(shPts, false, withA(rgb(0x050814), 0.42f), std::max(1.8f, 3.0f * s), true);
            const auto rimPts = ellipsePts(px, py, pr * 0.96f, pr * 0.96f, PI_F * 0.96f, PI_F * 1.58f, 32);
            g.polyline(rimPts, false, withA(rgb(0xFFF3DA), 0.34f * A), std::max(1.0f, 1.4f * s), true);
        }

        // Near half of the tilted rings (crisp in front of sphere)
        drawRings(0.f, PI_F, 1.05f);

        // Two tiny orbiting moons aligned with the tilted ring plane (golden Titan & icy Enceladus)
        {
            const float tx = px + (rx * 1.32f) * ct - (-ry * 0.35f) * st;
            const float ty = py + (rx * 1.32f) * st + (-ry * 0.35f) * ct;
            g.glow(tx, ty, 6.5f * s, rgb(0xFFD694), 0.48f);
            g.fillCircle(tx, ty, std::max(1.4f, 1.85f * s), rgb(0xFFE7BA));
            const float ex = px + (-rx * 1.16f) * ct - (ry * 0.25f) * st;
            const float ey = py + (-rx * 1.16f) * st + (ry * 0.25f) * ct;
            g.glow(ex, ey, 4.0f * s, rgb(0xC2E4FF), 0.40f);
            g.fillCircle(ex, ey, std::max(1.0f, 1.2f * s), rgb(0xEDF7FF));
        }
    }

    // 7. Distant Rim-Lit Crescent Ice-Giant (far bottom-right, balancing Saturn with subtle Neptune cyan-sapphire glow)
    {
        const float r2 = std::max(12.f * s, 0.021f * H);
        const float x2 = 0.908f * W, y2 = 0.878f * H;
        const Color night = rgb(0x050A1E);
        g.glow(x2 - r2 * 0.28f, y2 - r2 * 0.22f, r2 * 2.1f, rgb(0x46B8E8), 0.22f);
        g.radialFillOffset(x2, y2, r2, -0.45f * r2, -0.42f * r2, {
            {0.00f, lerp(night, rgb(0x88E2FF), 0.78f)},
            {0.35f, lerp(night, rgb(0x2D76B8), 0.72f)},
            {0.72f, lerp(night, rgb(0x102248), 0.78f)},
            {1.00f, night}
        });
        const auto cres = ellipsePts(x2, y2, r2 * 0.95f, r2 * 0.95f, PI_F * 0.92f, PI_F * 1.62f, 28);
        g.polyline(cres, false, withA(rgb(0xA6F2FF), 0.62f), std::max(1.1f, 1.5f * s), true);
        // Tiny icy moon beside it
        g.glow(x2 - r2 * 1.85f, y2 - r2 * 0.75f, 3.8f * s, rgb(0xBCE8FF), 0.36f);
        g.fillCircle(x2 - r2 * 1.85f, y2 - r2 * 0.75f, std::max(0.9f, 1.05f * s), rgb(0xE8F6FF));
    }

    return g.endSprite();
}

// Half-res cached soft cosmic nebula shimmer veil (breathes gently in opacity with 1 cheap drawBitmapRect!)
Bitmap buildNebulaPulse(Ctx& c) {
    constexpr float SP = 0.35f;
    const int sw = std::max(32, (int)(c.W * SP)), sh = std::max(32, (int)(c.H * SP));
    if (!c.g.beginSprite(sw, sh)) return Bitmap{};
    const float R = std::max((float)sw, (float)sh);
    c.g.glowEllipse(0.17f * sw, 0.27f * sh, 0.32f * R, 0.22f * R, rgb(0x8A56E8), 0.42f);
    c.g.glowEllipse(0.83f * sw, 0.74f * sh, 0.34f * R, 0.22f * R, rgb(0x36C2DC), 0.38f);
    c.g.glowEllipse(0.42f * sw, 0.56f * sh, 0.28f * R, 0.14f * R, rgb(0xA8C0FF), 0.28f);
    c.g.glowEllipse(0.66f * sw, 0.34f * sh, 0.26f * R, 0.13f * R, rgb(0xC29EFF), 0.26f);
    return c.g.endSprite();
}

// Living scintillating jewel stars + whisper-slow drifting galactic stardust motes
void drawLivingStars(Ctx& c, float vis, double t) {
    Gfx& g = c.g; const float W = c.W, H = c.H, s = c.s;
    if (vis <= 0.01f) return;

    // 44 living twinkling stars (top 14 flash delicate breathing 4-point starlight diffraction glints)
    for (int i = 0; i < 44; ++i) {
        const float x = (0.02f + 0.96f * hashf(i, 71)) * W;
        const float y = (0.03f + 0.94f * hashf(i, 72)) * H;
        const float f1 = 0.45f + 1.15f * hashf(i, 73), f2 = 1.25f + 1.65f * hashf(i, 76);
        const float p1 = hashf(i, 74) * 6.28318f, p2 = hashf(i, 77) * 6.28318f;
        const float twRaw = 0.5f + 0.34f * std::sin(ph(t, f1, p1)) + 0.16f * std::sin(ph(t, f2, p2));
        const float tw = std::pow(clamp01(twRaw), 1.35f);
        const Color col = starSpectral(hashf(i, 75));
        const float al = (0.28f + 0.70f * tw) * vis;
        const bool hero = (i < 14);
        const float rad = std::max(0.95f, (hero ? (1.35f + 0.65f * tw) : (0.90f + 0.45f * tw)) * s);
        g.glow(x, y, (hero ? (6.5f + 6.5f * tw) : (4.0f + 3.0f * tw)) * s, col, (hero ? 0.42f : 0.28f) * al);
        g.fillCircle(x, y, rad, withA(lerp(col, rgb(0xFFFFFF), 0.55f), clamp01(al + 0.14f)));
        if (hero && tw > 0.30f) {
            drawStarGlint(g, x, y, (5.5f + 9.0f * tw) * s, s, col, (tw - 0.22f) * 0.92f * vis);
        }
    }

    // 26 delicate cosmic stardust motes drifting imperceptibly slowly along the Milky Way river
    for (int i = 0; i < 26; ++i) {
        const float sp = 0.0022f + 0.0026f * hashf(i, 81);                    // full crossing takes ~4-7 minutes: deeply hypnotic
        const float u = loopPos(t, sp, hashf(i, 82), 1.0);
        float sx, sy; milkySpine(u, W, H, sx, sy);
        const float perp = (hashf(i, 83) - 0.5f) * 0.32f * H + std::sin(ph(t, 0.22 + 0.18 * hashf(i, 84), hashf(i, 85) * 6.28)) * 10.f * s;
        const float px = sx - perp * 0.54f, py = sy + perp * 0.84f;
        if (px < -10.f || px > W + 10.f || py < -10.f || py > H + 10.f) continue;
        const float edge = smoothstep(0.f, 0.08f, u) * (1.f - smoothstep(0.92f, 1.f, u));
        const float pulse = 0.55f + 0.45f * std::sin(ph(t, 0.85 + 0.9 * hashf(i, 86), hashf(i, 87) * 6.28));
        const float ma = (0.28f + 0.36f * hashf(i, 88)) * edge * pulse * vis;
        if (ma < 0.01f) continue;
        const Color col = starSpectral(hashf(i, 89));
        g.fillCircle(px, py, std::max(0.8f, (0.85f + 0.65f * hashf(i, 90)) * s), withA(col, clamp01(ma)));
    }
}

// a streak with its head at the right end: thin and faint at the tail, brighter and thicker toward the head, with a crisp core line
Bitmap buildTail(Gfx& g, float s, const Color& body, const Color& core, float thick, float len) {
    constexpr int SS = 2;
    const int sw = (int)(len * SS), sh = (int)std::ceil(thick * 3.2f + 4.f) * SS;
    if (sw < 8 || sw > 6000 || !g.beginSprite(sw, sh)) return Bitmap{};
    const float cy = sh * 0.5f, cl = std::max(1.2f, 1.1f * s) * SS * 0.8f;
    const int N = sw / SS;
    for (int m = 0; m < N; ++m) {
        const float u = (m + 0.5f) / N;
        const float th = thick * SS * (0.10f + 0.62f * std::pow(u, 1.2f));
        g.fillRect((float)(m * SS), cy - th * 0.5f, (float)SS, th, withA(body, 0.70f * std::pow(u, 1.6f)));
        g.fillRect((float)(m * SS), cy - cl * 0.5f, (float)SS, cl, withA(core, 0.95f * std::pow(u, 1.5f)));
    }
    return g.endSprite();
}

bool tailsReady(Ctx& c) {
    const uint64_t k0 = mixKey(900, (uint64_t)(scaleStep(c.s) + 64));
    const float s = c.s, len = std::floor(320.f * s);
    bool ok = true;
    ok = ensure(c, gTail[0], k0 + 1, [&] { return buildTail(c.g, s, rgb(0xC4D6FF), rgb(0xFFFFFF), 4.f * s, len); }) && ok;
    ok = ensure(c, gTail[1], k0 + 2, [&] { return buildTail(c.g, s, rgb(0xFFC98A), rgb(0xFFF1D6), 4.f * s, len); }) && ok;
    ok = ensure(c, gTail[2], k0 + 3, [&] { return buildTail(c.g, s, rgb(0x9CFFC8), rgb(0xEFFFF6), 4.f * s, len); }) && ok;
    ok = ensure(c, gTail[3], k0 + 4, [&] { return buildTail(c.g, s, rgb(0x6FE8FF), rgb(0xE8FFFF), 5.f * s, len); }) && ok;
    return ok;
}

// a streak of light along a path: head at (hx, hy), moving to the right (side > 0) or left, the path leaning by slope
void streak(Ctx& c, int tailNo, float hx, float hy, float side, float slope, float len, float op) {
    if (!gTail[tailNo].bmp.bmp || op <= 0.004f) return;
    const Bitmap& b = gTail[tailNo].bmp;
    const float th = b.h / 2.f;
    c.g.drawBitmapRot(b, side > 0 ? hx - len : hx, hy - th * 0.5f, len, th, hx, hy, std::atan(slope) * 57.2958f, clamp01(op), side < 0);
}

void drawMeteor(Ctx& c, const Kind& K, long long k, float u, bool ignoreWeather = false) {
    Gfx& g = c.g; const float W = c.W, H = c.H, s = c.s;
    const int ki = (int)(k & 0x7FFFFFFF), sl = K.salt;
    const float cover = ignoreWeather ? 1.f : (1.f - 0.9f * skyCover(c.a));
    if (cover <= 0.01f) return;

    // Weekly alternating stream: every meteor for a full week follows the week's angled direction,
    // alternating R->L (weeks 1, 3, 5) and L->R (weeks 2, 4, 6), resetting back to Week 1 on Week 7.
    float side = -1.f, baseDeg = 22.f;
    daily::meteorWeekPattern(daily::dayOf(c.a.now, c.a.tzOff), side, baseDeg);
    const float ang = (baseDeg + (hashf(ki, sl + 4) - 0.5f) * 9.f) * PI_F / 180.f;
    const float dx = side * std::cos(ang), dy = std::sin(ang);

    // Entry point biased toward the incoming half of the upper sky so the streak crosses gracefully
    const float x0 = W * (side > 0 ? 0.04f + 0.54f * hashf(ki, sl + 5) : 0.42f + 0.54f * hashf(ki, sl + 5));
    const float y0 = H * (0.03f + 0.40f * hashf(ki, sl + 6));
    const float D = W * (0.28f + 0.26f * hashf(ki, sl + 7));

    // Realistic shooting-star kinematics: fast atmospheric entry -> sharp ablation flare -> head extinction -> fading ionization wake
    const float burnEnd = 0.78f + 0.08f * hashf(ki, sl + 10);              // head burns out before the faint wake fully dissipates
    const float uHead = clamp01(u / burnEnd);
    const float pHead = 1.f - std::pow(1.f - uHead, 1.75f);
    const float tailSpan = (0.22f + 0.24f * hashf(ki, sl + 8)) * (0.35f + 0.65f * smoother(uHead / 0.35f));
    const float postFade = u > burnEnd ? 1.f - smoother((u - burnEnd) / std::max(0.05f, 1.f - burnEnd)) : 1.f;
    const float tailAdvance = u > burnEnd ? (1.f - postFade) * tailSpan * 0.42f : 0.f;
    const float pTail = std::max(0.f, std::min(pHead, pHead - tailSpan + tailAdvance));

    // Dynamic brightness: dim ignition -> surging incandescence -> vivid mid/late flare burst -> tumbling shimmer -> burn-out
    const float entry = smoother(u / 0.16f);
    const float headLive = u < burnEnd ? (1.f - smoother((u - (burnEnd - 0.12f)) / 0.12f)) : 0.f;
    const float flareAt = 0.46f + 0.20f * hashf(ki, sl + 11);
    const float flareW = 0.085f + 0.035f * hashf(ki, sl + 12);
    const float df = (u - flareAt) / flareW;
    const float flare = std::exp(-df * df) * (0.55f + 0.55f * hashf(ki, sl + 13));
    const float shimmer = 0.84f + 0.16f * std::sin(u * 48.f + hashf(ki, sl + 14) * 6.283f) * std::cos(u * 19.f + hashf(ki, sl + 15) * 6.283f);
    const float mag = 0.68f + 0.36f * hashf(ki, sl + 16);                  // varied meteor magnitudes (faint swift zip vs bright bolide)
    const float trailVis = clamp01(entry * postFade * shimmer * mag * (0.72f + 0.48f * flare) * cover);
    const float headVis = clamp01(entry * headLive * shimmer * mag * (0.85f + 0.75f * flare) * cover);
    if (trailVis <= 0.004f && headVis <= 0.004f) return;

    // Natural mineral emission color temperature: cool tail -> ionized mid-body -> incandescent white-hot head
    const float kind = hashf(ki, sl + 9);
    const Color coolCol = kind > 0.88f ? rgb(0x46A698) : (kind > 0.68f ? rgb(0xB47848) : rgb(0x6E8EE0));
    const Color midCol  = kind > 0.88f ? rgb(0x98FFD0) : (kind > 0.68f ? rgb(0xFFCF8A) : rgb(0xC6DCFF));
    const Color hotCol  = kind > 0.88f ? rgb(0xECFFF7) : (kind > 0.68f ? rgb(0xFFF6E2) : rgb(0xFFFFFF));

    // Multi-segment tapered plasma wake (outer soft glow + incandescent core) — never a flat uniform white stick
    constexpr int NSEG = 16;
    const float dp = pHead - pTail;
    if (dp > 0.002f && trailVis > 0.004f) {
        const float px0 = x0 + dx * D * pTail, py0 = y0 + dy * D * pTail;
        const float px1 = x0 + dx * D * pHead, py1 = y0 + dy * D * pHead;
        for (int i = 0; i < NSEG; ++i) {
            const float f0 = (float)i / (float)NSEG;
            const float f1 = (float)(i + 1) / (float)NSEG;
            const float fm = 0.5f * (f0 + f1);
            // Slight overlap eliminates subpixel gaps between tapered segments
            const float sx0 = px0 + (px1 - px0) * std::max(0.f, f0 - 0.015f);
            const float sy0 = py0 + (py1 - py0) * std::max(0.f, f0 - 0.015f);
            const float sx1 = px0 + (px1 - px0) * f1;
            const float sy1 = py0 + (py1 - py0) * f1;
            // Wake cools along the tail and dims gently as the head vanishes
            const float wakeFlicker = 0.86f + 0.14f * std::sin((pTail + dp * fm) * 34.f + ki * 1.7f);
            const float grad = std::pow(fm, 1.85f) * wakeFlicker;
            const Color segCol = fm < 0.55f ? lerp(coolCol, midCol, fm / 0.55f) : lerp(midCol, hotCol, (fm - 0.55f) / 0.45f);
            const float outerW = (1.1f + (3.8f + 2.2f * flare) * std::pow(fm, 1.35f)) * s;
            const float coreW  = (0.45f + (1.45f + 0.85f * flare) * std::pow(fm, 1.45f)) * s;
            const float wakeDim = u > burnEnd ? (0.55f * postFade) : 1.f;
            g.line(sx0, sy0, sx1, sy1, withA(segCol, clamp01(0.30f * grad * trailVis * wakeDim)), outerW, true);
            if (fm > 0.18f) {
                g.line(sx0, sy0, sx1, sy1, withA(lerp(segCol, hotCol, 0.55f), clamp01(0.92f * grad * trailVis * (0.28f + 0.72f * headLive))), coreW, true);
            }
        }
    }

    // Blazing shooting-star head: soft outer halo, hot white core, peak-flare starlight glint, and tiny shedding ablation sparks
    if (headVis > 0.01f) {
        const float hx = x0 + dx * D * pHead, hy = y0 + dy * D * pHead;
        const float haloR = (7.5f + 10.5f * flare) * s * mag;
        g.glow(hx, hy, haloR, midCol, clamp01(0.36f * headVis));
        g.glow(hx, hy, (3.2f + 4.0f * flare) * s * mag, hotCol, clamp01(0.75f * headVis));
        g.fillCircle(hx, hy, std::max(1.1f, (1.25f + 0.85f * flare) * s * mag), withA(hotCol, clamp01(headVis)));
        if (flare > 0.25f) {
            const float sp = (4.0f + 7.5f * flare) * s * mag;
            const float ga = clamp01(0.45f * (flare - 0.25f) * headVis);
            g.line(hx - sp, hy, hx + sp, hy, withA(hotCol, ga), std::max(0.8f, 0.9f * s), true);
            g.line(hx, hy - sp * 0.75f, hx, hy + sp * 0.75f, withA(hotCol, ga), std::max(0.8f, 0.9f * s), true);
        }
        // 3 tiny incandescent sparks flaking off behind the meteor during peak burn
        const float nx = -dy, ny = dx;
        for (int sp = 0; sp < 3; ++sp) {
            const float born = 0.26f + 0.14f * sp + 0.08f * hashf(ki + sp, sl + 20);
            const float age = (u - born) / 0.22f;
            if (age <= 0.f || age >= 1.f) continue;
            const float bp = 1.f - std::pow(1.f - clamp01(born / burnEnd), 1.75f);
            const float drift = (hashf(ki + sp, sl + 21) - 0.5f) * 9.f * s * age;
            const float lag = (4.f + 12.f * hashf(ki + sp, sl + 22)) * s * age;
            const float ex = x0 + dx * (D * bp - lag) + nx * drift;
            const float ey = y0 + dy * (D * bp - lag) + ny * drift;
            const float ea = (1.f - age) * (1.f - age) * clamp01(headVis * 0.9f);
            g.fillCircle(ex, ey, std::max(0.75f, (1.0f - 0.45f * age) * s), withA(lerp(midCol, hotCol, 1.f - age), ea));
        }
    }
}

void drawComet(Ctx& c, long long k, float u, bool ignoreWeather = false) {
    Gfx& g = c.g; const float W = c.W, H = c.H, s = c.s;
    const int ki = (int)(k & 0x7FFFFFFF);
    const float side = hashf(ki, 303) < 0.5f ? 1.f : -1.f;
    const float ang = (8.f + 16.f * hashf(ki, 304)) * PI_F / 180.f;
    const float x0 = side > 0 ? -0.10f * W : 1.10f * W, y0 = H * (0.08f + 0.40f * hashf(ki, 305));
    const float hx = x0 + side * std::cos(ang) * 1.30f * W * u, hy = y0 + std::sin(ang) * 1.30f * W * u;
    const float env = ease(u / 0.12f) * (1.f - ease((u - 0.88f) / 0.12f));
    const float cover = ignoreWeather ? 1.f : (1.f - 0.9f * skyCover(c.a));
    const float len = 300.f * s;
    streak(c, 3, hx, hy, side, std::tan(ang) * side, len, 0.80f * env * cover);                      // the ion tail, straight away from the Sun
    if (gTail[1].bmp.bmp) {                                                                              // and a softer, warmer dust tail leaning a little
        const Bitmap& b = gTail[1].bmp; const float th = b.h / 2.f, lw = len * 0.72f;
        g.drawBitmapRot(b, side > 0 ? hx - lw : hx, hy - th * 0.5f, lw, th * 1.6f, hx, hy, std::atan(std::tan(ang) * side) * 57.2958f + (side > 0 ? -7.f : 7.f), clamp01(0.40f * env * cover), side < 0);
    }
    g.glow(hx, hy, 16.f * s, rgb(0xBFE8FF), 0.45f * env * cover);
    g.glow(hx, hy, 6.f * s, rgb(0xFFFFFF), 0.65f * env * cover);
    g.fillCircle(hx, hy, std::max(1.6f, 2.0f * s), withA(rgb(0xFFFFFF), clamp01(env * cover)));
}

// ---------------------------------------------------------------- the flying saucer
// A sculpted metallic silver saucer with a crystal glass dome, a classic green pilot peering out,
// plasma underside thruster ring, chasing rim beacons and a silky ion trail. Rendered at 3x supersampling for razor-sharp edges.
constexpr int SSH = 3;
Bitmap buildShip(Ctx& c, float L) {
    Gfx& g = c.g;
    const float L2 = L * SSH, w = L2 * 1.24f, h = L2 * 0.68f, cx = w * 0.5f, cy = h * 0.63f;
    if (!g.beginSprite((int)w, (int)h)) return Bitmap{};
    auto fill = [&](float x, float y, float rx, float ry, float a0, float a1, Color top, Color bot) {
        auto p = ellipsePts(x, y, rx, ry, a0, a1, 84);
        g.fillPolyVGradient(p, y - ry, y + ry, top, bot);
    };
    // 1. Lower hull shell: deep gunmetal underside with soft curved metallic belly
    fill(cx, cy + 0.022f * L2, 0.505f * L2, 0.140f * L2, 0.f, 2.f * PI_F, rgb(0x8FA4C2), rgb(0x141B29));
    // Glowing plasma propulsion ring recessed under the belly
    fill(cx, cy + 0.104f * L2, 0.215f * L2, 0.044f * L2, 0.f, 2.f * PI_F, withA(rgb(0x1C3B44), 0.95f), withA(rgb(0x0A151D), 0.95f));
    fill(cx, cy + 0.108f * L2, 0.165f * L2, 0.030f * L2, 0.f, 2.f * PI_F, withA(rgb(0xB8FFF0), 0.98f), withA(rgb(0x22C2B0), 0.98f));
    g.glowEllipse(cx, cy + 0.108f * L2, 0.18f * L2, 0.045f * L2, rgb(0x6CFFE6), 0.65f);
    // 2. Sculpted brushed-titanium main rim disc + beveled upper shoulder deck
    fill(cx, cy - 0.010f * L2, 0.515f * L2, 0.082f * L2, 0.f, 2.f * PI_F, rgb(0xF7FAFF), rgb(0x7C94B8));
    fill(cx, cy - 0.024f * L2, 0.455f * L2, 0.062f * L2, 0.f, 2.f * PI_F, rgb(0xE0ECFC), rgb(0x637A9E));
    fill(cx, cy - 0.068f * L2, 0.345f * L2, 0.098f * L2, 0.f, 2.f * PI_F, rgb(0xD8E6F8), rgb(0x52698E));
    // Metallic collar ring around the glass canopy base + upper rim specular sheen (drawn behind the glass canopy)
    fill(cx, cy - 0.102f * L2, 0.215f * L2, 0.044f * L2, 0.f, 2.f * PI_F, rgb(0xA6BCD8), rgb(0x3B4E6B));
    g.polyline(ellipsePts(cx, cy - 0.010f * L2, 0.512f * L2, 0.080f * L2, PI_F * 1.03f, PI_F * 1.97f, 84), false, withA(rgb(0xFFFFFF), 0.85f), 1.6f * SSH, true);
    // 3. Crystal glass canopy dome with sculpted green alien pilot inside
    {
        const float dx = cx, dy = cy - 0.102f * L2, rx = 0.188f * L2, ry = 0.210f * L2;
        auto domePoly = ellipsePts(dx, dy, rx, ry, PI_F, 2.f * PI_F, 84);
        // Back interior cabin shadow
        g.fillPolyVGradient(domePoly, dy - ry, dy, withA(rgb(0x38949E), 0.52f), withA(rgb(0x123642), 0.85f));
        // Pilot flight suit shoulders + neck
        fill(dx, dy - 0.008f * L2, 0.072f * L2, 0.036f * L2, PI_F, 2.f * PI_F, rgb(0x2B4258), rgb(0x162436));
        g.fillPolygon({{dx - 0.020f * L2, dy - 0.050f * L2}, {dx + 0.020f * L2, dy - 0.050f * L2},
                       {dx + 0.015f * L2, dy - 0.018f * L2}, {dx - 0.015f * L2, dy - 0.018f * L2}}, rgb(0x47AD6C));
        // Classic sculpted alien head (wider cranium tapering smoothly to chin)
        fill(dx, dy - 0.095f * L2, 0.062f * L2, 0.058f * L2, 0.f, 2.f * PI_F, rgb(0x88F09E), rgb(0x39965A));
        fill(dx, dy - 0.066f * L2, 0.042f * L2, 0.036f * L2, 0.f, 2.f * PI_F, rgb(0x6CD984), rgb(0x2F8550));
        // Big glossy tilted almond eyes + crisp white catchlights
        auto drawEye = [&](float sideSign) {
            std::vector<Pt> eye;
            const float ex = dx + sideSign * 0.026f * L2, ey = dy - 0.088f * L2;
            const float erx = 0.021f * L2, ery = 0.013f * L2, ang = sideSign * -0.46f;
            const float ca = std::cos(ang), sa = std::sin(ang);
            for (int i = 0; i < 20; ++i) {
                const float th = 2.f * PI_F * i / 20.f;
                const float px = erx * std::cos(th), py = ery * std::sin(th);
                eye.push_back({ex + px * ca - py * sa, ey + px * sa + py * ca});
            }
            g.fillPolygon(eye, rgb(0x070B12));
            g.fillCircle(ex - sideSign * 0.004f * L2, ey - 0.004f * L2, 0.0045f * L2, withA(rgb(0xFFFFFF), 0.92f));
        };
        drawEye(-1.f);
        drawEye(+1.f);
        // Translucent cyan-glass canopy reflection overlay & curved specular highlight
        g.fillPolyVGradient(domePoly, dy - ry, dy, withA(rgb(0xC6FFF4), 0.42f), withA(rgb(0x42C8C2), 0.22f));
        g.glowEllipse(dx - 0.068f * L2, dy - 0.132f * L2, 0.052f * L2, 0.022f * L2, rgb(0xFFFFFF), 0.78f);
        g.polyline(ellipsePts(dx, dy, rx * 0.88f, ry * 0.88f, PI_F * 1.12f, PI_F * 1.44f, 24), false, withA(rgb(0xFFFFFF), 0.58f), 1.8f * SSH, true);
        g.polyline(domePoly, false, withA(rgb(0xE8FFFA), 0.72f), 1.4f * SSH, true);
    }
    // 4. Lower hull shadow seam + subtle hull panel lines
    g.polyline(ellipsePts(cx, cy + 0.022f * L2, 0.480f * L2, 0.125f * L2, 0.18f, PI_F - 0.18f, 56), false, withA(rgb(0x0A0F18), 0.45f), 1.2f * SSH, true);
    for (int i = -3; i <= 3; ++i) {
        const float a = PI_F * 0.5f + i * 0.24f;
        g.line(cx + 0.22f * L2 * std::cos(a), cy + 0.012f * L2 + 0.036f * L2 * std::sin(a),
               cx + 0.48f * L2 * std::cos(a), cy + 0.014f * L2 + 0.066f * L2 * std::sin(a), withA(rgb(0x0A0F18), 0.26f), 1.0f * SSH, true);
    }
    return g.endSprite();
}

// Silky C2-smooth cosmic pathway: sweeping glide, gentle hover bob, and one fluid wide bank (no janky wiggles or sharp jerks)
struct ShipAt { float x, y, tilt, side; };
ShipAt shipAt(const Ctx& c, long long k, float u) {
    const int ki = (int)(k & 0x7FFFFFFF);
    ShipAt r; r.side = hashf(ki, 411) < 0.5f ? 1.f : -1.f;
    const float W = c.W, H = c.H;
    const float y0 = H * (0.16f + 0.30f * hashf(ki, 412));
    const float arcAmp = H * (0.035f + 0.035f * hashf(ki, 413)) * (hashf(ki, 417) < 0.5f ? 1.f : -1.f);
    const float bobAmp = H * 0.011f;
    const float bankAmp = H * (0.055f + 0.055f * hashf(ki, 414)) * (hashf(ki, 415) < 0.5f ? 1.f : -1.f);
    const float phase = hashf(ki, 416) * 6.2831853f;
    auto X = [&](float v) {
        // gentle smooth pace with a playful subtle mid-flight acceleration
        const float p = v + 0.022f * std::sin(v * 6.2831853f);
        return (r.side > 0 ? -0.15f + 1.30f * p : 1.15f - 1.30f * p) * W;
    };
    auto Y = [&](float v) {
        return y0
            + arcAmp * std::sin(v * PI_F)
            + bobAmp * std::sin(v * 9.424778f + phase)
            + bankAmp * smoother((v - 0.34f) / 0.28f);
    };
    r.x = X(u); r.y = Y(u);
    const float e = 0.016f; const float dx = X(u + e) - X(u - e), dy = Y(u + e) - Y(u - e);
    r.tilt = std::max(-15.f, std::min(15.f, std::atan2(dy, std::fabs(dx)) * 57.2958f * 0.72f)) * (r.side > 0 ? 1.f : -1.f);
    return r;
}

void drawShip(Ctx& c, long long k, float u, uint64_t flightId) {
    Gfx& g = c.g; const float s = c.s, t = (float)c.a.now;
    const float L = std::floor(std::max(126.f * s, 0.108f * std::min(c.W, c.H * 1.8f)));
    const uint64_t key = mixKey(mixKey(1700, (uint64_t)L), 5);
    const bool ok = ensure(c, gShip, key, [&] { return buildShip(c, L); });
    tailsReady(c);
    const ShipAt P = shipAt(c, k, u);
    ufoaudio::update(flightId, c.W > 1.f ? P.x / c.W : 0.5f, u);                          // stereo pan tracks the saucer's live horizontal position
    const float env = smoother(u / 0.07f) * (1.f - smoother((u - 0.93f) / 0.07f));
    if (env <= 0.01f) return;
    const float cs = std::cos(P.tilt * PI_F / 180.f), sn = std::sin(P.tilt * PI_F / 180.f);
    // silky layered ion trail behind the saucer
    const float trailLen = std::min(c.W * 0.52f, 520.f * s) * (0.90f + 0.10f * std::sin(t * 28.f));
    const float slope = std::tan(P.tilt * PI_F / 180.f);
    {
        const float lx = -P.side * 0.44f * L, ly = 0.012f * L;
        streak(c, 3, P.x + lx * cs - ly * sn, P.y + lx * sn + ly * cs, P.side, slope, trailLen, 0.82f * env);
    }
    // soft pulsing plasma glow + subtle downward tractor shimmer beneath the hull
    const float pulse = 0.86f + 0.14f * std::sin(t * 9.5f);
    g.glowEllipse(P.x, P.y + 0.16f * L, 0.50f * L, 0.16f * L, rgb(0x6FFFE4), 0.36f * pulse * env);
    g.glowEllipse(P.x, P.y + 0.28f * L, 0.24f * L, 0.20f * L, rgb(0x7AE8FF), 0.14f * pulse * env);
    if (ok) {
        const Bitmap& b = gShip.bmp; const float bw = b.w / (float)SSH, bh = b.h / (float)SSH;
        g.drawBitmapRot(b, P.x - bw * 0.5f, P.y - 0.63f * bh, bw, bh, P.x, P.y, P.tilt, clamp01(env), false);
    }
    // 11 smooth chasing rim beacons: cyan, violet-magenta, and warm gold
    static const unsigned cols[3] = {0x6FF3FF, 0xE678FF, 0xFFD670};
    for (int i = 0; i < 11; ++i) {
        const float a = PI_F * (0.08f + 0.84f * i / 10.f);
        const float lx = 0.475f * L * std::cos(a), ly = -0.010f * L + 0.072f * L * std::sin(a);
        const float rx = P.x + lx * cs - ly * sn, ry = P.y + lx * sn + ly * cs;
        const float on = 0.34f + 0.66f * (0.5f + 0.5f * std::sin(t * 14.f - i * 0.82f));
        const Color lc = rgb(cols[i % 3]);
        g.glow(rx, ry, 6.0f * s, lc, 0.58f * on * env);
        g.fillCircle(rx, ry, std::max(1.4f, 1.8f * s), withA(lc, clamp01(on * env)));
    }
    // delicate plasma stardust motes drifting behind the trail
    for (int i = 0; i < 8; ++i) {
        const float back = (0.42f + 0.88f * hashf(i + (int)(k & 255), 421)) * L, drift = (hashf(i, 422) - 0.5f) * 0.18f * L;
        const float f = std::fmod((float)(u * 8.f + hashf(i, 423)), 1.f);
        const float sx = P.x - P.side * (0.38f * L + back * f * 1.05f), sy = P.y + 0.015f * L + drift * f;
        g.fillCircle(sx, sy, std::max(1.0f, 1.35f * s * (1.f - 0.5f * f)), withA(rgb(0xC8FCFF), 0.72f * (1.f - f) * env));
    }
}

// ---------------------------------------------------------------- Sunlit II: rays of sunlight and wisps of cloud
// where the Sun is on the screen: the same walk as the sun flare in scene_quiet.cpp (east on the left edge, west on the right, highest at noon)
void sunScreenPos(const Ctx& c, float& x, float& y, float& alt) {
    const float a = (float)c.a.sun.hor.alt, az = (float)c.a.sun.hor.az;
    alt = a;
    const float f = -(float)std::sin(astro::rad(az));
    x = c.W * (0.5f + 0.56f * f);
    y = c.H * (0.60f - 0.56f * smoothstep(-1.f, 55.f, a));
}

Bitmap buildRays(Ctx& c, float H) {
    constexpr float SPRR = 0.5f;
    const int sw = (int)(2.7f * H * SPRR), sh = (int)(1.15f * H * SPRR);
    if (sw < 16 || !c.g.beginSprite(sw, sh)) return Bitmap{};
    const float ax = sw * 0.5f, L = sh * 1.0f;
    const int N = 11;
    for (int i = 0; i < N; ++i) {
        const float u = (i + 0.5f) / N;
        const float a = (-58.f + 116.f * u + (hashf(i, 601) - 0.5f) * 9.f) * PI_F / 180.f;       // 0 = straight down
        const float wd = (2.4f + 4.6f * hashf(i, 602)) * PI_F / 180.f;
        const float al = 0.10f + 0.16f * hashf(i, 603);
        const Pt p0{ax, 0.f}, p1{ax + L * std::tan(a - wd * 0.5f), L}, p2{ax + L * std::tan(a + wd * 0.5f), L};
        c.g.fillPolyVGradient({p0, p1, p2}, 0.f, L, withA(rgb(0xFFF6DC), al), withA(rgb(0xFFF6DC), 0.f));
    }
    return c.g.endSprite();
}

Bitmap buildCirrus(Ctx& c) {
    constexpr float SPRR = 0.5f;
    const float W = c.W, H = c.H;
    const int sw = (int)(2.f * W * SPRR), sh = (int)(0.36f * H * SPRR);
    if (sw < 16 || sh < 8 || !c.g.beginSprite(sw, sh)) return Bitmap{};
    for (int i = 0; i < 7; ++i) {
        const float cx = sw * (i + 0.3f + 0.4f * hashf(i, 611)) / 7.f, cy = sh * (0.18f + 0.64f * hashf(i, 612));
        const float rx = (0.13f + 0.12f * hashf(i, 613)) * W * SPRR * 2.f, ry = (0.010f + 0.012f * hashf(i, 614)) * H * SPRR * 2.f;
        // draw wrapped copies at -sw, 0, +sw so tiled cirrus never shows a vertical seam
        for (int wrap = -1; wrap <= 1; ++wrap) {
            const float wx = cx + wrap * (float)sw;
            if (wx + rx * 1.4f < 0.f || wx - rx * 1.4f > (float)sw) continue;
            c.g.glowEllipse(wx, cy, rx, ry, rgb(0xFFFFFF), 0.55f);
            c.g.glowEllipse(wx + rx * 0.3f, cy + ry * 0.6f, rx * 0.6f, ry * 0.7f, rgb(0xFFFFFF), 0.40f);
        }
    }
    return c.g.endSprite();
}

} // namespace

// ---------------------------------------------------------------- Sunlit II
void drawSunRays(Ctx& c) {
    App& a = c.a; Gfx& g = c.g;
    if (a.st.theme != THEME_SUNLIT2 || a.packTheme()) return;
    float sx, sy, alt; sunScreenPos(c, sx, sy, alt);
    const float cover = 1.f - 0.92f * skyCover(a);
    const float vis = smoothstep(-2.f, 7.f, alt) * cover;
    if (vis < 0.01f) return;
    const double t = a.now;
    // wisps of high cloud drifting seamlessly across the upper sky (fades smoothly with vis)
    if (ensure(c, gCirrus, mixKey(2100, ((uint64_t)c.W << 20) ^ (uint64_t)c.H), [&] { return buildCirrus(c); })) {
        const float bw = gCirrus.bmp.w * 2.f, bh = gCirrus.bmp.h * 2.f;
        const float x = -(float)loopPos(t, 3.0, 0.0, (double)bw);
        for (int rep = 0; rep < 2; ++rep) g.drawBitmapRect(gCirrus.bmp, x + rep * bw, 0.04f * c.H, bw, bh, clamp01(0.55f * vis));
    }
    // the rays: a fan from the Sun that breathes very slowly
    if (ensure(c, gRays, mixKey(2200, (uint64_t)c.H), [&] { return buildRays(c, c.H); })) {
        const float bw = gRays.bmp.w * 2.f, bh = gRays.bmp.h * 2.f;
        const float sway = std::sin(ph(t, 0.045)) * 2.2f;
        const float breath = 0.80f + 0.20f * std::sin(ph(t, 0.11));
        g.drawBitmapRot(gRays.bmp, sx - bw * 0.5f, sy, bw, bh, sx, sy, sway, clamp01(0.62f * vis * breath));
    }
    g.glow(sx, sy, 0.30f * c.H, rgb(0xFFF3CE), 0.30f * vis);                               // the glare around the Sun itself
}

// ---------------------------------------------------------------- Midnight: artistic Prussian-velvet & moonlit champagne nocturne
// Quiet, aristocratic nocturnal airglow engineered to flatter every weather mode and every face without competing with typography:
//   - Soft Prussian-indigo & cerulean horizon airglow washing gently across the lower sky
//   - Subtle upper-right moonlit silver-champagne atmospheric crown glow
//   - Whisper-thin nocturnal mist-veil bands for atmospheric depth
//   - ~160 crisp sparse zenith stars (diamond pearl, pale champagne, cerulean ice) fading cleanly toward the horizon & behind weather
Bitmap buildMidnightNocturne(Ctx& c) {
    Gfx& g = c.g; const float W = c.W, H = c.H, s = c.s;
    const int wi = (int)W, hi = (int)H;
    if (wi < 4 || hi < 4 || !g.beginSprite(wi, hi)) return Bitmap{};

    // 1. Subtle luminous Prussian-cerulean horizon airglow along the bottom sky
    g.radialEllipse(W * 0.50f, H * 1.04f, W * 0.78f, H * 0.42f, 0.f, -H * 0.06f, {
        {0.00f, withA(rgb(0x1E3D6E), 0.36f)},
        {0.42f, withA(rgb(0x152B52), 0.22f)},
        {0.78f, withA(rgb(0x0D1B38), 0.08f)},
        {1.00f, withA(rgb(0x081024), 0.00f)},
    }, 1.f);
    g.radialEllipse(W * 0.26f, H * 0.96f, W * 0.48f, H * 0.26f, 0.f, 0.f, {
        {0.00f, withA(rgb(0x234A78), 0.20f)},
        {0.55f, withA(rgb(0x152F58), 0.08f)},
        {1.00f, withA(rgb(0x0A152C), 0.00f)},
    }, 1.f);

    // 2. Quiet upper-right moonlit atmospheric crown (silver-cerulean + whisper of champagne moonlight)
    g.radialEllipse(W * 0.80f, H * 0.14f, W * 0.46f, H * 0.36f, 0.f, 0.f, {
        {0.00f, withA(rgb(0x88C8EC), 0.13f)},
        {0.38f, withA(rgb(0x4672A8), 0.08f)},
        {0.72f, withA(rgb(0x1C3460), 0.03f)},
        {1.00f, withA(rgb(0x0A1428), 0.00f)},
    }, 1.f);
    g.radialEllipse(W * 0.83f, H * 0.11f, W * 0.24f, H * 0.20f, 0.f, 0.f, {
        {0.00f, withA(rgb(0xF0D9A0), 0.085f)},
        {0.52f, withA(rgb(0xB8C8E4), 0.035f)},
        {1.00f, withA(rgb(0x1E3258), 0.00f)},
    }, 1.f);

    // 3. Whisper-soft high nocturnal airglow veil ribbons (adds quiet horizon depth without clutter)
    for (int b = 0; b < 3; ++b) {
        const float bx = W * (0.24f + 0.28f * b);
        const float by = H * (0.76f + 0.07f * b);
        const float rx = W * (0.36f + 0.10f * hashf(b, 871));
        const float ry = H * (0.045f + 0.020f * hashf(b, 872));
        g.radialEllipse(bx, by, rx, ry, 0.f, 0.f, {
            {0.00f, withA(rgb(0x6C98C8), 0.07f)},
            {0.50f, withA(rgb(0x3B5E8E), 0.035f)},
            {1.00f, withA(rgb(0x182A4A), 0.00f)},
        }, 1.f);
    }

    // 4. Crisp, serene zenith diamond stars (fading naturally toward the luminous horizon)
    const int nStars = 165;
    for (int i = 0; i < nStars; ++i) {
        const float hx = hashf(i, 881), hy = hashf(i, 882), hm = hashf(i, 883), hc = hashf(i, 884);
        const float px = W * (0.02f + 0.96f * hx);
        const float py = H * (0.02f + 0.68f * std::pow(hy, 1.25f));
        const float horFade = 1.f - smoothstep(H * 0.42f, H * 0.72f, py);
        if (horFade < 0.04f) continue;
        const Color col = hc < 0.28f ? rgb(0xA6D6F8) : (hc < 0.68f ? rgb(0xEFF4FF) : rgb(0xF6E5BE));
        const float br = std::pow(hm, 2.4f);
        const float r = std::max(0.55f, (0.55f + 1.45f * br) * s);
        const float a0 = (0.18f + 0.64f * br) * horFade;
        if (br > 0.55f) g.glow(px, py, r * 4.5f, col, a0 * 0.34f);
        g.fillCircle(px, py, r, withA(col, a0));
        if (br > 0.84f) {
            const float sp = r * 5.2f;
            g.line(px - sp, py, px + sp, py, withA(col, a0 * 0.36f), std::max(0.65f, 0.75f * s), true);
            g.line(px, py - sp, px, py + sp, withA(col, a0 * 0.36f), std::max(0.65f, 0.75f * s), true);
        }
    }
    return g.endSprite();
}

void drawMidnightBackdropOnly(Ctx& c, float weatherDim, bool explicitMode) {
    App& a = c.a; Gfx& g = c.g;
    const float sc = clamp01(weatherDim);
    const float cover = explicitMode ? 1.f : (1.f - 0.72f * sc);
    const float em = c.P.ember > 0.001f ? 1.f - 0.65f * c.P.ember : 1.f;
    const float themeMul = (explicitMode && !c.P.dark) ? 0.62f : 1.f;
    const float vis = clamp01(cover * em * themeMul);
    if (vis < 0.02f) return;
    if (explicitMode && a.st.theme != THEME_MIDNIGHT && c.P.dark) {
        g.vGradientRect(0.f, 0.f, c.W, c.H, withA(rgb(0x050A18), 0.56f * em), withA(rgb(0x111E38), 0.56f * em));
    }
    const uint64_t key = mixKey(mixKey(3400, (uint64_t)c.W), mixKey((uint64_t)c.H, (uint64_t)(scaleStep(c.s) + 64)));
    if (ensure(c, gMidnightBg, key, [&] { return buildMidnightNocturne(c); })) {
        g.drawBitmapRect(gMidnightBg.bmp, 0.f, 0.f, (float)gMidnightBg.bmp.w, (float)gMidnightBg.bmp.h, vis);
    }
    // Whisper-slow twinkling zenith glints (vanish cleanly behind overcast/fog)
    const float starVis = clamp01((explicitMode ? 1.f : (1.f - 0.92f * sc)) * em * themeMul);
    if (starVis > 0.05f) {
        const double t = a.now;
        for (int i = 0; i < 14; ++i) {
            const float px = c.W * (0.06f + 0.88f * hashf(i, 891));
            const float py = c.H * (0.04f + 0.42f * hashf(i, 892));
            const float tw = 0.5f + 0.5f * std::sin(ph(t, 0.45 + 0.55 * hashf(i, 893), hashf(i, 894) * 6.2831));
            const float pulse = tw * tw;
            const Color col = (i % 3 == 0) ? rgb(0xF4DFB0) : ((i % 3 == 1) ? rgb(0xA6D8FA) : rgb(0xF2F6FF));
            const float r = std::max(0.8f, (1.0f + 0.8f * hashf(i, 895)) * c.s);
            const float aa = (0.18f + 0.48f * pulse) * starVis;
            g.glow(px, py, r * 4.0f, col, aa * 0.42f);
            g.fillCircle(px, py, r, withA(col, aa));
        }
    }
}

void drawMidnightNocturne(Ctx& c) {
    if (!midnightOn(c.a)) return;
    drawMidnightBackdropOnly(c, skyCover(c.a), false);
}

// ---------------------------------------------------------------- Galaxy (deep-space cosmos)
void drawSpace(Ctx& c) {
    App& a = c.a; Gfx& g = c.g;
    if (!spaceOn(a) && !(a.st.theme == THEME_GALAXY && a.st.spaceFx && a.st.face == FACE_FOCUS && !a.packTheme())) return;
    const float cover = 1.f - 0.88f * skyCover(a), em = c.P.ember > 0.001f ? 1.f - 0.65f * c.P.ember : 1.f;
    const float vis = clamp01(cover * em);
    const double t = a.now;
    const uint64_t bgKey = mixKey(mixKey(3200, (uint64_t)c.W), mixKey((uint64_t)c.H, (uint64_t)(scaleStep(c.s) + 64)));
    if (ensure(c, gBg, bgKey, [&] { return buildSky(c); }))
        g.drawBitmapRect(gBg.bmp, 0.f, 0.f, (float)gBg.bmp.w, (float)gBg.bmp.h, vis);
    if (vis > 0.02f && ensure(c, gNebulaPulse, mixKey(3250, ((uint64_t)c.W << 20) ^ (uint64_t)c.H), [&] { return buildNebulaPulse(c); })) {
        const float breath = 0.14f + 0.14f * (0.5f + 0.5f * std::sin(ph(t, 0.30)));
        g.drawBitmapRect(gNebulaPulse.bmp, 0.f, 0.f, c.W, c.H, clamp01(breath * vis));
    }
    drawLivingStars(c, vis, t);
    // meteors and the comet
    long long k; float u;
    if (a.meteorDemoU > 0.f) {
        drawMeteor(c, kMeteorA, 42, a.meteorDemoU);
        drawMeteor(c, kMeteorB, 77, std::min(0.95f, a.meteorDemoU * 0.72f + 0.18f));
    }
    if (activeAt(kMeteorA, t, k, u)) drawMeteor(c, kMeteorA, k, u);
    if (activeAt(kMeteorB, t, k, u)) drawMeteor(c, kMeteorB, k, u);
    if (activeAt(kComet, t, k, u) && tailsReady(c)) drawComet(c, k, u);
}

void drawSpaceBackdropOnly(Ctx& c) {
    App& a = c.a; Gfx& g = c.g;
    const float em = c.P.ember > 0.001f ? 1.f - 0.65f * c.P.ember : 1.f;
    const float vis = (c.P.dark ? 1.0f : 0.52f) * em;
    const double t = a.now;
    const uint64_t bgKey = mixKey(mixKey(3200, (uint64_t)c.W), mixKey((uint64_t)c.H, (uint64_t)(scaleStep(c.s) + 64)));
    if (ensure(c, gBg, bgKey, [&] { return buildSky(c); }))
        g.drawBitmapRect(gBg.bmp, 0.f, 0.f, (float)gBg.bmp.w, (float)gBg.bmp.h, clamp01(vis));
    if (vis > 0.02f && ensure(c, gNebulaPulse, mixKey(3250, ((uint64_t)c.W << 20) ^ (uint64_t)c.H), [&] { return buildNebulaPulse(c); })) {
        const float breath = 0.16f + 0.15f * (0.5f + 0.5f * std::sin(ph(t, 0.30)));
        g.drawBitmapRect(gNebulaPulse.bmp, 0.f, 0.f, c.W, c.H, clamp01(breath * vis));
    }
    drawLivingStars(c, vis, t);
    // Focus Studio STARS is always crystal-clear deep space regardless of outdoor fog or storm
    long long k; float u;
    if (a.meteorDemoU > 0.f) {
        drawMeteor(c, kMeteorA, 42, a.meteorDemoU, true);
        drawMeteor(c, kMeteorB, 77, std::min(0.95f, a.meteorDemoU * 0.72f + 0.18f), true);
    }
    if (activeAt(kMeteorA, t, k, u)) drawMeteor(c, kMeteorA, k, u, true);
    if (activeAt(kMeteorB, t, k, u)) drawMeteor(c, kMeteorB, k, u, true);
    if (activeAt(kComet, t, k, u) && tailsReady(c)) drawComet(c, k, u, true);
}

// in front of everything, for a moment: the saucer
void drawSpaceFront(Ctx& c) {
    App& a = c.a;
    if (!spaceOn(a)) return;
    long long k; float u; uint64_t fid = 0;
    if (shipOn(a, a.now, u, k, fid)) { a.spaceShipDrew = true; drawShip(c, k, u, fid); }
}

bool spaceBusyNow(const App& a, double now) {
    if (!spaceOn(a)) return false;
    long long k; float u; uint64_t fid = 0;
    return activeAt(kMeteorA, now, k, u) || activeAt(kMeteorB, now, k, u) || activeAt(kComet, now, k, u) || shipOn(a, now, u, k, fid);
}

double spaceNextStart(const App& a, double now) {
    if (!spaceOn(a)) return 1e18;
    double ns = std::min(std::min(nextStart(kMeteorA, now), nextStart(kMeteorB, now)), nextStart(kComet, now));
    if (a.st.aliensEnabled) {
        ns = std::min(ns, daily::nextStart(now, a.tzOff, kShipLo, kShipHi, kShipSalt));
        if (a.aliensNuts) {
            const double rel = std::max(0.0, now - a.aliensNutsSince);
            const long long curSlot = (long long)std::floor(rel / kNutsSlot);
            double nextNuts = a.aliensNutsSince + (double)curSlot * kNutsSlot + kNutsDelay;
            if (nextNuts <= now) nextNuts += kNutsSlot;
            ns = std::min(ns, nextNuts);
        }
    }
    return ns;
}

// everything behind the faces that the theme brings: Galaxy cosmos, Midnight nocturne, Sunlit II rays, then the sun flare
void drawScenery(Ctx& c) {
    drawSpace(c);
    drawMidnightNocturne(c);
    drawSunRays(c);
    drawSunFlare(c);
}

void spaceRelease(Gfx& g) {
    g.freeBitmap(gBg.bmp); gBg.key = gBg.failed = 0;
    g.freeBitmap(gNebulaPulse.bmp); gNebulaPulse.key = gNebulaPulse.failed = 0;
    g.freeBitmap(gMidnightBg.bmp); gMidnightBg.key = gMidnightBg.failed = 0;
    for (auto& s : gTail) { g.freeBitmap(s.bmp); s.key = s.failed = 0; }
    g.freeBitmap(gShip.bmp); gShip.key = gShip.failed = 0;
    g.freeBitmap(gRays.bmp); gRays.key = gRays.failed = 0;
    g.freeBitmap(gCirrus.bmp); gCirrus.key = gCirrus.failed = 0;
}

} // namespace app
