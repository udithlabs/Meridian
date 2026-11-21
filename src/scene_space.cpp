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
Slot gBg, gTail[4], gShip, gRays, gCirrus;               // tails: white, warm, green, ion (cyan)
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
bool spaceOn(const App& a) { return a.st.theme == THEME_MIDNIGHT && a.st.spaceFx && (a.st.face == FACE_QUIET || a.st.face == FACE_SKY) && !a.packTheme(); }
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

// how much the weather hides the sky: cloud, fog, rain, snow, blizzard and storms put the stars out
float skyCover(const App& a) { return clamp01(std::max(std::max(0.9f * a.qfx[0], a.qfx[5]), std::max(std::max(0.8f * a.qfx[2], std::max(0.72f * a.qfx[6], 0.95f * a.qfx[7])), a.qfx[4]))); }

// ---------------------------------------------------------------- Midnight: the still sky
Bitmap buildSky(Ctx& c) {
    Gfx& g = c.g; const float W = c.W, H = c.H, s = c.s;
    const int sw = (int)W, sh = (int)H;
    if (sw < 64 || sh < 64 || (double)sw * sh > 1.6e7 || !g.beginSprite(sw, sh)) return Bitmap{};
    const float R = std::max(W, H);
    // soft nebula: purple, teal and a trace of rose
    g.glowEllipse(0.16f * W, 0.30f * H, 0.42f * R, 0.30f * R, rgb(0x5B3A9E), 0.13f);
    g.glowEllipse(0.80f * W, 0.64f * H, 0.38f * R, 0.26f * R, rgb(0x1E6F8E), 0.10f);
    g.glowEllipse(0.52f * W, 0.08f * H, 0.30f * R, 0.14f * R, rgb(0x8A3A86), 0.08f);
    // a band of the Milky Way, from the lower left to the upper right
    for (int i = 0; i < 18; ++i) {
        const float u = (i + 0.5f) / 18.f, j = hashf(i, 21) - 0.5f;
        g.glowEllipse((-0.08f + 1.16f * u) * W, (0.86f - 0.72f * u) * H + j * 0.04f * H, 0.16f * R, 0.06f * R, rgb(0x93A9FF), 0.045f);
    }
    // stars, many of them in the band
    const int N = (int)std::max(140.f, std::min(560.f, W * H / 4600.f));
    const float z1 = std::max(1.f, std::round(s));
    for (int i = 0; i < N; ++i) {
        float x, y;
        if (hashf(i, 31) < 0.40f) { const float u = hashf(i, 32); x = (-0.08f + 1.16f * u) * W; y = (0.86f - 0.72f * u) * H + (hashf(i, 33) - 0.5f) * 0.24f * H; }
        else { x = hashf(i, 34) * W; y = hashf(i, 35) * H; }
        const float h = hashf(i, 36);
        const float al = 0.13f + 0.40f * h * h * h;                                   // dim: most are barely there
        const float tc = hashf(i, 37);
        const Color col = tc < 0.30f ? rgb(0xBFD0FF) : (tc < 0.50f ? rgb(0xFFE4BF) : rgb(0xE9EEFF));
        const float z = hashf(i, 38) > 0.94f ? z1 * 2.f : z1;
        g.fillRect(std::round(x), std::round(y), z, z, withA(col, al));
        if (h > 0.93f) g.glow(std::round(x) + z * 0.5f, std::round(y) + z * 0.5f, 4.f * s, col, 0.20f);
    }
    // a ringed planet low on the left, dim and far
    {
        const float pr = std::max(20.f * s, 0.040f * H), px = 0.075f * W, py = 0.895f * H, rx = pr * 2.1f, ry = pr * 0.50f, A = 0.72f;
        const Color ring = rgb(0xD9BE92);
        const auto back = ellipsePts(px, py, rx, ry, PI_F, 2.f * PI_F, 60), front = ellipsePts(px, py, rx, ry, 0.f, PI_F, 60);
        for (int b = 0; b < 3; ++b) {
            const float f = 1.f - 0.16f * b;
            const auto bk = ellipsePts(px, py, rx * f, ry * f, PI_F, 2.f * PI_F, 60);
            g.polyline(bk, false, withA(ring, A * (b == 1 ? 0.22f : 0.38f)), std::max(1.6f, 2.4f * s), false);
        }
        // the disc is opaque (dimmed by mixing with the night colour, not by alpha) so the far half of the rings stays hidden behind it
        const Color night = rgb(0x050A1E);
        g.radialFillOffset(px, py, pr, -0.38f * pr, -0.34f * pr, {{0.f, lerp(night, rgb(0xF1D8AE), A)}, {0.55f, lerp(night, rgb(0xB28A5E), A)}, {1.f, lerp(night, rgb(0x1E1710), A)}});
        for (int b = 0; b < 3; ++b) {
            const float f = 1.f - 0.16f * b;
            const auto fr = ellipsePts(px, py, rx * f, ry * f, 0.f, PI_F, 60);
            g.polyline(fr, false, withA(ring, A * (b == 1 ? 0.30f : 0.55f)), std::max(1.6f, 2.4f * s), false);
        }
        (void)back; (void)front;
    }
    // a faraway galaxy, tilted, at the upper right
    {
        const float gx = 0.885f * W, gy = 0.105f * H, gr = std::max(26.f * s, 0.05f * H);
        g.glowEllipse(gx, gy, gr * 1.9f, gr * 0.70f, rgb(0xA9B8FF), 0.20f);
        g.glowEllipse(gx, gy, gr * 0.75f, gr * 0.32f, rgb(0xFFF0D8), 0.34f);
        g.glow(gx, gy, gr * 0.20f, rgb(0xFFFFFF), 0.55f);
        for (int i = 0; i < 14; ++i) { const float a = hashf(i, 51) * 6.283f, d = std::sqrt(hashf(i, 52)); g.fillRect(std::round(gx + std::cos(a) * d * gr * 1.7f), std::round(gy + std::sin(a) * d * gr * 0.6f), z1, z1, withA(rgb(0xDCE6FF), 0.35f)); }
    }
    return g.endSprite();
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

void drawMeteor(Ctx& c, const Kind& K, long long k, float u) {
    Gfx& g = c.g; const float W = c.W, H = c.H, s = c.s;
    const int ki = (int)(k & 0x7FFFFFFF), sl = K.salt;
    const float cover = 1.f - 0.9f * skyCover(c.a);
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

void drawComet(Ctx& c, long long k, float u) {
    Gfx& g = c.g; const float W = c.W, H = c.H, s = c.s;
    const int ki = (int)(k & 0x7FFFFFFF);
    const float side = hashf(ki, 303) < 0.5f ? 1.f : -1.f;
    const float ang = (8.f + 16.f * hashf(ki, 304)) * PI_F / 180.f;
    const float x0 = side > 0 ? -0.10f * W : 1.10f * W, y0 = H * (0.08f + 0.40f * hashf(ki, 305));
    const float hx = x0 + side * std::cos(ang) * 1.30f * W * u, hy = y0 + std::sin(ang) * 1.30f * W * u;
    const float env = ease(u / 0.12f) * (1.f - ease((u - 0.88f) / 0.12f));
    const float cover = 1.f - 0.9f * skyCover(c.a);
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

// ---------------------------------------------------------------- Midnight
void drawSpace(Ctx& c) {
    App& a = c.a; Gfx& g = c.g;
    if (!spaceOn(a)) return;
    const float cover = 1.f - 0.88f * skyCover(a), em = c.P.ember > 0.001f ? 1.f - 0.65f * c.P.ember : 1.f;
    const double t = a.now;
    if (ensure(c, gBg, mixKey(mixKey(3100, (uint64_t)c.W), mixKey((uint64_t)c.H, (uint64_t)(scaleStep(c.s) + 64))), [&] { return buildSky(c); }))
        g.drawBitmapRect(gBg.bmp, 0.f, 0.f, (float)gBg.bmp.w, (float)gBg.bmp.h, clamp01(cover * em));
    // a few brighter stars that breathe, very slowly
    for (int i = 0; i < 9; ++i) {
        const float x = std::round(hashf(i, 71) * c.W), y = std::round(hashf(i, 72) * c.H);
        const float al = (0.34f + 0.20f * std::sin(ph(t, 0.25 + 0.5 * hashf(i, 73), hashf(i, 74) * 6.28))) * cover * em;
        const Color col = hashf(i, 75) < 0.5f ? rgb(0xCFDCFF) : rgb(0xFFEBCF);
        g.glow(x + 0.5f, y + 0.5f, 5.f * c.s, col, 0.30f * al);
        g.fillRect(x, y, std::max(1.f, std::round(c.s * 1.2f)), std::max(1.f, std::round(c.s * 1.2f)), withA(col, clamp01(al + 0.15f)));
    }
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
    const float vis = (c.P.dark ? 0.92f : 0.42f) * em;
    const double t = a.now;
    if (ensure(c, gBg, mixKey(mixKey(3100, (uint64_t)c.W), mixKey((uint64_t)c.H, (uint64_t)(scaleStep(c.s) + 64))), [&] { return buildSky(c); }))
        g.drawBitmapRect(gBg.bmp, 0.f, 0.f, (float)gBg.bmp.w, (float)gBg.bmp.h, clamp01(vis));
    for (int i = 0; i < 9; ++i) {
        const float x = std::round(hashf(i, 71) * c.W), y = std::round(hashf(i, 72) * c.H);
        const float al = (0.34f + 0.20f * std::sin(ph(t, 0.25 + 0.5 * hashf(i, 73), hashf(i, 74) * 6.28))) * vis;
        const Color col = hashf(i, 75) < 0.5f ? rgb(0xCFDCFF) : rgb(0xFFEBCF);
        g.glow(x + 0.5f, y + 0.5f, 5.f * c.s, col, 0.30f * al);
        g.fillRect(x, y, std::max(1.f, std::round(c.s * 1.2f)), std::max(1.f, std::round(c.s * 1.2f)), withA(col, clamp01(al + 0.15f)));
    }
    long long k; float u;
    if (a.meteorDemoU > 0.f) drawMeteor(c, kMeteorA, 42, a.meteorDemoU);
    if (activeAt(kMeteorA, t, k, u)) drawMeteor(c, kMeteorA, k, u);
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

// everything behind the faces that the theme brings: the Midnight sky or the Sunlit II rays, then the sun flare
void drawScenery(Ctx& c) {
    drawSpace(c);
    drawSunRays(c);
    drawSunFlare(c);
}

void spaceRelease(Gfx& g) {
    g.freeBitmap(gBg.bmp); gBg.key = gBg.failed = 0;
    for (auto& s : gTail) { g.freeBitmap(s.bmp); s.key = s.failed = 0; }
    g.freeBitmap(gShip.bmp); gShip.key = gShip.failed = 0;
    g.freeBitmap(gRays.bmp); gRays.key = gRays.failed = 0;
    g.freeBitmap(gCirrus.bmp); gCirrus.key = gCirrus.failed = 0;
}

} // namespace app
