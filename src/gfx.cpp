#include "gfx.h"
#include "resource.h"
#include "util.h"
#include <algorithm>
#include <cstdio>
#include <mutex>

namespace gfx {

using D2D1::ColorF;
using D2D1::Point2F;
using D2D1::RectF;

// ---------------------------------------------------------------- palettes
const Palette PAL_DAY = [] {
    Palette p;
    p.bgTop = rgb(0xEAF1F8); p.bgBottom = rgb(0xF7F2E9);
    p.ink = rgb(0x0F1B2A); p.ink2 = rgb(0x4E6075); p.ink3 = rgb(0x8695A6);
    p.line = {15 / 255.f, 27 / 255.f, 42 / 255.f, 0.14f};
    p.accent = rgb(0xE8862A); p.accent2 = rgb(0x3A5FCF);
    p.panel = {15 / 255.f, 27 / 255.f, 42 / 255.f, 0.05f};
    p.plate = rgb(0xF1F2F1); p.starVis = 0; p.dark = false;
    p.skyZen = rgb(0x3F86D6); p.skyHor = rgb(0xA9D3F2); p.skyNZen = rgb(0x060B1D); p.skyNHor = rgb(0x17264A); p.skyOwn = 1;                 // the sky map's own colours
    return p;
}();
// Sunlit: a bright, sunny daytime. A clear sky blue at the top warming to a cream-gold horizon, with deep navy text and a sun-orange accent.
// (Day is the plain light page; this one is a sky.) It is the theme the sun flare is made for: on the Quiet and Sky faces the sun glows in it all day.
const Palette PAL_SUNLIT = [] {
    Palette p;
    p.bgTop = rgb(0x9ACBF2); p.bgBottom = rgb(0xFFF0D2);
    p.ink = rgb(0x0E2038); p.ink2 = rgb(0x37506C); p.ink3 = rgb(0x5B7490);
    p.line = {14 / 255.f, 32 / 255.f, 56 / 255.f, 0.17f};
    p.accent = rgb(0xE5791A); p.accent2 = rgb(0x2D69C4);
    p.panel = {14 / 255.f, 32 / 255.f, 56 / 255.f, 0.06f};
    p.plate = rgb(0xDCEBF3); p.starVis = 0; p.dark = false;
    p.skyZen = rgb(0x3F8EE0); p.skyHor = rgb(0xFFE6B8); p.skyNZen = rgb(0x0C1C46); p.skyNHor = rgb(0x3A5C98); p.skyOwn = 1;                 // the sky map's own colours
    return p;
}();
// Sunlit II: a richer daylight than Sunlit. A deeper azure overhead burning down to a golden horizon, deep navy text, a tangerine accent and frosted-glass
// panels. Its scenery (rays of sunlight, wisps of high cloud, and the sun flare all day) is drawn in scene_space.cpp.
const Palette PAL_SUNLIT2 = [] {
    Palette p;
    p.bgTop = rgb(0x4FA6EE); p.bgBottom = rgb(0xFFE2A4);
    p.ink = rgb(0x0A1A30); p.ink2 = rgb(0x2B4666); p.ink3 = rgb(0x4F6788);
    p.line = {10 / 255.f, 26 / 255.f, 48 / 255.f, 0.18f};
    p.accent = rgb(0xF0661A); p.accent2 = rgb(0x1B5CC0);
    p.panel = {1.f, 1.f, 1.f, 0.20f};
    p.plate = rgb(0xCBE6F7); p.starVis = 0; p.dark = false;
    p.skyZen = rgb(0x1F6FD2); p.skyHor = rgb(0xFFCB7A); p.skyNZen = rgb(0x0A1A48); p.skyNHor = rgb(0x3D62A8); p.skyOwn = 1;                 // the sky map's own colours
    return p;
}();
// Dawn: the cool blue of the last stars melting into a rose and gold horizon, lighter and clearer than Dusk's plum.
const Palette PAL_DAWN = [] {
    Palette p;
    p.bgTop = rgb(0x182847); p.bgBottom = rgb(0x84556A);
    p.ink = rgb(0xFFF6EC); p.ink2 = rgb(0xF4D6C8); p.ink3 = rgb(0xCDA6AC);
    p.line = {255 / 255.f, 240 / 255.f, 228 / 255.f, 0.20f};
    p.accent = rgb(0xFFC477); p.accent2 = rgb(0xB4DAFF);
    p.panel = {1, 1, 1, 0.075f};
    p.plate = rgb(0x3B3052); p.starVis = 0.38f; p.dark = true;
    p.twilight = 1; p.twiTop = rgb(0x262A72); p.twiBottom = rgb(0xC0663F);      // under cloud: indigo above, a glowing orange-rose below
    p.skyZen = rgb(0x2A3F85); p.skyHor = rgb(0xF0A58E); p.skyNZen = rgb(0x141E48); p.skyNHor = rgb(0x73507E); p.skyOwn = 1;                 // the sky map's own colours
    return p;
}();
// Sunset: the sun just gone, the whole sky burning orange and red, deepening to wine at the top.
const Palette PAL_SUNSET = [] {
    Palette p;
    p.bgTop = rgb(0x45142E); p.bgBottom = rgb(0xCF5A1C);
    p.ink = rgb(0xFFF3E2); p.ink2 = rgb(0xFFD9B0); p.ink3 = rgb(0xEDA06C);
    p.line = {255 / 255.f, 238 / 255.f, 214 / 255.f, 0.24f};
    p.accent = rgb(0xFFD36B); p.accent2 = rgb(0xFFB0A0);
    p.panel = {1, 1, 1, 0.09f};
    p.plate = rgb(0x6A2230); p.starVis = 0.12f; p.dark = true;
    p.twilight = 1; p.twiTop = rgb(0x3A0F45); p.twiBottom = rgb(0xD4561A);      // under cloud: plum-purple above, a burning orange below
    p.skyZen = rgb(0x3E1458); p.skyHor = rgb(0xEE7A28); p.skyNZen = rgb(0x22093A); p.skyNHor = rgb(0x582C74); p.skyOwn = 1;                 // the sky map's own colours
    return p;
}();
const Palette PAL_DUSK = [] {
    Palette p;
    p.bgTop = rgb(0x171033); p.bgBottom = rgb(0x3E2347);
    p.ink = rgb(0xFBF0E8); p.ink2 = rgb(0xC7ABC4); p.ink3 = rgb(0x8D7092);
    p.line = {251 / 255.f, 240 / 255.f, 232 / 255.f, 0.16f};
    p.accent = rgb(0xFF9D68); p.accent2 = rgb(0xF7C8DF);
    p.panel = {1, 1, 1, 0.055f};
    p.plate = rgb(0x2A1A40); p.starVis = 0.55f; p.dark = true;
    p.twilight = 1; p.twiTop = rgb(0x2C1066); p.twiBottom = rgb(0xAD4630);      // under cloud: deep violet above, ember orange below
    p.skyZen = rgb(0x1C1352); p.skyHor = rgb(0x9B5A8C); p.skyNZen = rgb(0x0F0A2E); p.skyNHor = rgb(0x432C68); p.skyOwn = 1;                 // the sky map's own colours
    return p;
}();
const Palette PAL_NIGHT = [] {
    Palette p;
    p.bgTop = rgb(0x05070D); p.bgBottom = rgb(0x0B1222);
    p.ink = rgb(0xECEFF8); p.ink2 = rgb(0x8995B0); p.ink3 = rgb(0x4D5873);
    p.line = {160 / 255.f, 175 / 255.f, 210 / 255.f, 0.17f};
    p.accent = rgb(0xFFB46E); p.accent2 = rgb(0xC3D3FF);
    p.panel = {1, 1, 1, 0.04f};
    p.plate = rgb(0x080C17); p.starVis = 1; p.dark = true;
    p.nightness = 1;
    p.skyZen = rgb(0x030612); p.skyHor = rgb(0x0B1630); p.skyNZen = rgb(0x030612); p.skyNHor = rgb(0x0B1630); p.skyOwn = 1;                 // deep velvet nocturnal navy
    return p;
}();
// Midnight: artistic Prussian-velvet & moonlit champagne-gold nocturne. Deep velvety sapphire zenith melting into a luminous
// indigo-cerulean horizon, moon-pearl typography, champagne-gold & cerulean moon-ice accents, and frosted sapphire-crystal panels
// engineered to flatter every face and every weather mode. Subtle lunar airglow and crisp zenith stars are drawn in scene_space.cpp.
const Palette PAL_MIDNIGHT = [] {
    Palette p;
    p.bgTop = rgb(0x050A18); p.bgBottom = rgb(0x111E38);
    p.ink = rgb(0xF0F3FA); p.ink2 = rgb(0x9FB1CE); p.ink3 = rgb(0x63789C);
    p.line = {168 / 255.f, 196 / 255.f, 238 / 255.f, 0.18f};
    p.accent = rgb(0xE8C882); p.accent2 = rgb(0x7CC4E8);
    p.panel = {170 / 255.f, 200 / 255.f, 245 / 255.f, 0.055f};
    p.plate = rgb(0x0B1428); p.starVis = 1; p.dark = true;
    p.nightness = 0.85f;
    p.skyZen = rgb(0x030816); p.skyHor = rgb(0x13264A); p.skyNZen = rgb(0x030816); p.skyNHor = rgb(0x13264A); p.skyOwn = 1;
    return p;
}();
// Galaxy: deep cosmic obsidian sky with a breath of indigo toward the bottom, cool starlight text and soft violet-cyan accents.
// Its deep-space cosmos scenery (Milky Way, emission nebulae, Andromeda, ringed Saturn, meteors, comet, saucer) is drawn in scene_space.cpp.
const Palette PAL_GALAXY = [] {
    Palette p;
    p.bgTop = rgb(0x010208); p.bgBottom = rgb(0x070C22);
    p.ink = rgb(0xE3EAFF); p.ink2 = rgb(0x8190BD); p.ink3 = rgb(0x4E5B84);
    p.line = {150 / 255.f, 165 / 255.f, 220 / 255.f, 0.16f};
    p.accent = rgb(0xB8A4FF); p.accent2 = rgb(0x7FC4FF);
    p.panel = {1, 1, 1, 0.035f};
    p.plate = rgb(0x050915); p.starVis = 1; p.dark = true;
    p.nightness = 1;
    p.skyZen = rgb(0x02030A); p.skyHor = rgb(0x080C22); p.skyNZen = rgb(0x02030A); p.skyNHor = rgb(0x080C22); p.skyOwn = 1;                 // deep obsidian-indigo cosmos (never bright sky blue)
    return p;
}();
const Palette PAL_EMBER = [] {
    Palette p;
    p.bgTop = rgb(0x000000); p.bgBottom = rgb(0x070100);
    p.ink = rgb(0xCF4A2C); p.ink2 = rgb(0x8E321D); p.ink3 = rgb(0x55190F);
    p.line = {208 / 255.f, 72 / 255.f, 43 / 255.f, 0.22f};
    p.accent = rgb(0xE85A32); p.accent2 = rgb(0xC0432A);
    p.panel = {208 / 255.f, 72 / 255.f, 43 / 255.f, 0.07f};
    p.plate = rgb(0x030000); p.starVis = 0.5f; p.ember = 1; p.dark = true;
    p.skyZen = rgb(0x040101); p.skyHor = rgb(0x160504); p.skyNZen = rgb(0x040101); p.skyNHor = rgb(0x160504); p.skyOwn = 1;                 // deep obsidian-crimson bedtime dome
    return p;
}();

Palette lerpPalette(const Palette& a, const Palette& b, float t) {
    Palette p;
    p.bgTop = lerp(a.bgTop, b.bgTop, t); p.bgBottom = lerp(a.bgBottom, b.bgBottom, t);
    p.ink = lerp(a.ink, b.ink, t); p.ink2 = lerp(a.ink2, b.ink2, t); p.ink3 = lerp(a.ink3, b.ink3, t);
    p.line = lerp(a.line, b.line, t); p.accent = lerp(a.accent, b.accent, t); p.accent2 = lerp(a.accent2, b.accent2, t);
    p.panel = lerp(a.panel, b.panel, t); p.plate = lerp(a.plate, b.plate, t);
    p.starVis = a.starVis + (b.starVis - a.starVis) * t;
    p.ember = a.ember + (b.ember - a.ember) * t;
    p.neon = a.neon + (b.neon - a.neon) * t;
    p.nightness = a.nightness + (b.nightness - a.nightness) * t;
    p.twilight = a.twilight + (b.twilight - a.twilight) * t;
    {   // the twilight colours follow whichever side is a twilight (a Day palette has none to mix in)
        const float wa = a.twilight * (1.f - t), wb = b.twilight * t, ws = wa + wb, k = ws > 1e-4f ? wb / ws : t;
        p.twiTop = lerp(a.twiTop, b.twiTop, k); p.twiBottom = lerp(a.twiBottom, b.twiBottom, k);
    }
    p.skyZen = lerp(a.skyZen, b.skyZen, t); p.skyHor = lerp(a.skyHor, b.skyHor, t);
    p.skyNZen = lerp(a.skyNZen, b.skyNZen, t); p.skyNHor = lerp(a.skyNHor, b.skyNHor, t);
    p.skyOwn = a.skyOwn + (b.skyOwn - a.skyOwn) * t;
    p.dark = t < 0.5f ? a.dark : b.dark;
    return p;
}

// ---------------------------------------------------------------- weather light
namespace {
Color grey(const Color& c, float amount) { const float L = luma(c); return lerp(c, {L, L, L, c.a}, amount); }
Color dim(const Color& c, float f) { return {c.r * f, c.g * f, c.b * f, c.a}; }
Color coolTint(const Color& c, float amount) {            // a touch of slate blue, keeping the brightness
    const float L = luma(c);
    return lerp(c, {L * 0.90f, L * 0.99f, std::min(1.f, L * 1.13f + 0.01f), c.a}, amount);
}
float relLum(const Color& c) { return 0.2126f * c.r * c.r + 0.7152f * c.g * c.g + 0.0722f * c.b * c.b; }   // close enough to linear light

// Contrast between a text colour and the two ends of the background (the worse of the two), like the web's contrast ratio: 1 = invisible, 21 = black on white.
float contrastOn(const Color& fg, const Color& top, const Color& bottom) {
    const float lf = relLum(fg);
    float worst = 1e9f;
    for (const Color* bg : {&top, &bottom}) {
        const float lb = relLum(*bg), hi = std::max(lf, lb), lo = std::min(lf, lb);
        worst = std::min(worst, (hi + 0.05f) / (lo + 0.05f));
    }
    return worst;
}
// While the weather moves the background, a text colour can end up nearly the same brightness as it (dark text on a mid-grey sky).
// Pushes 'fg' toward white or black, whichever reads better, just far enough to reach 'want'. Does nothing when it already does.
Color keepContrast(const Color& fg, const Color& top, const Color& bottom, float want) {
    if (contrastOn(fg, top, bottom) >= want) return fg;
    const Color toWhite = {1.f, 1.f, 1.f, fg.a}, toBlack = {0.f, 0.f, 0.f, fg.a};
    const Color& goal = contrastOn(toWhite, top, bottom) >= contrastOn(toBlack, top, bottom) ? toWhite : toBlack;
    float lo = 0.f, hi = 1.f;
    for (int i = 0; i < 12; ++i) { const float mid = 0.5f * (lo + hi); if (contrastOn(lerp(fg, goal, mid), top, bottom) >= want) hi = mid; else lo = mid; }
    return lerp(fg, goal, hi);
}
}

namespace {
// ---- the general rule: heavy cloud darkens and cools the palette, fog flattens it. (Used for the bright themes; night and the twilights have their own below.)
Palette moodGeneric(const Palette& in, float gloom, float haze) {
    Palette p = in;
    // ---- heavy cloud: the sky closes in
    if (gloom > 0.002f) {
        const float g = gloom;
        if (!in.dark) {
            // a bright page turns into a slate overcast: dark at the top, lighter toward the horizon
            const Color topT = rgb(0x2F3944), botT = rgb(0x56616D);
            p.bgTop = lerp(p.bgTop, topT, g * 0.96f); p.bgBottom = lerp(p.bgBottom, botT, g * 0.96f);
            const Color plateT = rgb(0x3E4853);
            p.plate = lerp(p.plate, plateT, g * 0.96f);
            const float Lb = relLum(lerp(p.bgTop, p.bgBottom, 0.5f));
            const float flip = smoothstep(0.215f, 0.150f, Lb);               // light text once the page is dark enough
            p.ink = lerp(p.ink, rgb(0xF0F3F7), flip); p.ink2 = lerp(p.ink2, rgb(0xBDC8D3), flip); p.ink3 = lerp(p.ink3, rgb(0x8A97A4), flip);
            p.line = lerp(p.line, Color{1.f, 1.f, 1.f, 0.17f}, flip); p.panel = lerp(p.panel, Color{1.f, 1.f, 1.f, 0.06f}, flip);
            p.accent = lerp(p.accent, grey(p.accent, 0.55f), g * 0.8f); p.accent2 = lerp(p.accent2, rgb(0xB5C6E8), g * 0.8f * flip);
            if (flip > 0.5f) p.dark = true;
        } else {
            const float darken = 1.f - 0.52f * g, desat = 0.78f * g, cool = 0.30f * g;
            for (Color* c : {&p.bgTop, &p.bgBottom, &p.plate}) *c = coolTint(dim(grey(*c, desat), darken), cool);
            p.ink = lerp(p.ink, dim(grey(p.ink, 0.30f), 0.92f), g); p.ink2 = lerp(p.ink2, dim(grey(p.ink2, 0.35f), 0.90f), g); p.ink3 = lerp(p.ink3, dim(grey(p.ink3, 0.35f), 0.88f), g);
            p.accent = lerp(p.accent, dim(grey(p.accent, 0.55f), 0.80f), g); p.accent2 = lerp(p.accent2, dim(grey(p.accent2, 0.50f), 0.78f), g);
            p.panel.a *= 1.f - 0.2f * g;
        }
        p.starVis *= 1.f - 0.78f * g;
    }
    // ---- fog: flat, milky and quiet. The sky loses its gradient and its colour; nothing is bright and nothing is dark
    if (haze > 0.002f) {
        const float h = haze;
        const bool light = !p.dark;
        const Color fogHi = light ? rgb(0xC8CED4) : rgb(0x3A424C), fogLo = light ? rgb(0xD9DDE1) : rgb(0x2B323B);
        for (Color* c : {&p.bgTop, &p.bgBottom, &p.plate}) *c = grey(*c, 0.72f * h);
        p.bgTop = lerp(p.bgTop, fogHi, 0.60f * h); p.bgBottom = lerp(p.bgBottom, fogLo, 0.60f * h);
        p.bgTop = lerp(p.bgTop, p.bgBottom, 0.40f * h);                     // flatter
        p.plate = lerp(p.plate, lerp(p.bgTop, p.bgBottom, 0.5f), 0.7f * h);
        const Color mid = lerp(p.bgTop, p.bgBottom, 0.5f);
        if (light) { p.ink = lerp(p.ink, lerp(p.ink, mid, 0.16f), h); p.ink2 = lerp(p.ink2, lerp(p.ink2, mid, 0.14f), h); p.ink3 = lerp(p.ink3, lerp(p.ink3, mid, 0.12f), h); }
        else { p.ink2 = lerp(p.ink2, lerp(p.ink2, p.ink, 0.30f), h); p.ink3 = lerp(p.ink3, lerp(p.ink3, p.ink2, 0.55f), h); }   // the lighter fog grey needs the small text a little brighter
        p.accent = lerp(p.accent, grey(p.accent, 0.6f), h); p.accent2 = lerp(p.accent2, grey(p.accent2, 0.6f), h);
        p.starVis *= 1.f - 0.92f * h;
    }
    return p;
}

// ---- night: cloud and fog take the last of the light, so it gets DEEPER. Blue-black, the same hue as a clear night, no grey, no milk.
Palette moodNight(const Palette& in, float gloom, float haze) {
    Palette p = in;
    const float w = std::max(gloom, haze);
    for (Color* c : {&p.bgTop, &p.bgBottom}) *c = dim(satur(*c, 1.0f + 0.35f * w), 1.f - 0.70f * w);
    p.plate = dim(satur(p.plate, 1.0f + 0.30f * w), 1.f - 0.62f * w);
    p.accent = dim(p.accent, 1.f - 0.06f * w);                                    // the warm accent stays warm
    p.panel.a *= 1.f - 0.25f * w;
    p.starVis *= (1.f - 0.78f * gloom) * (1.f - 0.92f * haze);
    return p;
}

// ---- dusk and dawn: the sun is below a lid of cloud, or behind the fog, and lights it from underneath. The purple and the orange come OUT.
Palette moodTwilight(const Palette& in, float gloom, float haze) {
    Palette p = in;
    const float w = std::max(gloom, haze);
    const float k = 0.92f * w;
    p.bgTop = lerp(in.bgTop, in.twiTop, k); p.bgBottom = lerp(in.bgBottom, in.twiBottom, k);
    p.bgTop = satur(p.bgTop, 1.f + 0.20f * w); p.bgBottom = satur(p.bgBottom, 1.f + 0.20f * w);
    p.bgTop = lerp(p.bgTop, p.bgBottom, 0.26f * haze);                           // fog: softer, flatter, still coloured
    p.plate = lerp(in.plate, dim(lerp(p.bgTop, p.bgBottom, 0.42f), 0.62f), 0.90f * w);
    p.ink2 = satur(in.ink2, 1.f + 0.15f * w); p.ink3 = satur(in.ink3, 1.f + 0.20f * w);
    p.accent = satur(in.accent, 1.f + 0.12f * w); p.accent2 = satur(in.accent2, 1.f + 0.10f * w);
    p.starVis *= (1.f - 0.70f * gloom) * (1.f - 0.90f * haze);
    return p;
}
} // namespace

Palette moodPalette(const Palette& in, float gloom, float haze) {
    const float keep = 1.f - in.ember;                      // the red bedtime palette stays as dark and as red as it is
    gloom = clamp01(gloom) * keep; haze = clamp01(haze) * keep;
    if (gloom < 0.002f && haze < 0.002f) return in;
    Palette p = moodGeneric(in, gloom, haze);
    // night and the twilights are not "a darker, greyer version of the day": they get their own treatment, blended by how much of each this palette is
    // (in Auto mode the palette glides Day > Dusk or Dawn > Night, and these weights glide with it)
    const float nW = clamp01(in.nightness), tW = std::min(clamp01(in.twilight), 1.f - nW), s = nW + tW;
    if (s > 0.002f) {
        const Palette special = lerpPalette(moodNight(in, gloom, haze), moodTwilight(in, gloom, haze), tW / s);
        const bool wasDark = p.dark;
        p = lerpPalette(p, special, s);
        p.dark = wasDark || s >= 0.5f;
    }
    // ---- readability: never let the weather leave text with less contrast than the theme gave it (up to a comfortable level).
    //      Without this, a light theme half way to its slate overcast has dark text on a mid-grey sky and the small italic date nearly vanishes.
    p.ink  = keepContrast(p.ink,  p.bgTop, p.bgBottom, std::min(7.0f, contrastOn(in.ink,  in.bgTop, in.bgBottom)));
    p.ink2 = keepContrast(p.ink2, p.bgTop, p.bgBottom, std::min(4.5f, contrastOn(in.ink2, in.bgTop, in.bgBottom)));
    p.ink3 = keepContrast(p.ink3, p.bgTop, p.bgBottom, std::min(3.0f, contrastOn(in.ink3, in.bgTop, in.bgBottom)));
    p.neon *= 1.f - 0.55f * std::max(gloom, haze);
    p.nightness = in.nightness; p.twilight = in.twilight; p.twiTop = in.twiTop; p.twiBottom = in.twiBottom;
    return p;
}

Color emberize(const Color& c, float amount) {
    float L = 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
    Color e = {std::min(1.f, L * 1.18f), L * 0.17f, L * 0.05f, c.a};
    return lerp(c, e, amount);
}

// ---------------------------------------------------------------- in-memory font loader
namespace {
struct MemStream : IDWriteFontFileStream {
    LONG rc = 1; const BYTE* d; UINT64 n;
    MemStream(const BYTE* d_, UINT64 n_) : d(d_), n(n_) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r, void** o) override {
        if (r == __uuidof(IUnknown) || r == __uuidof(IDWriteFontFileStream)) { *o = this; AddRef(); return S_OK; }
        *o = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&rc); }
    ULONG STDMETHODCALLTYPE Release() override { LONG r = InterlockedDecrement(&rc); if (!r) delete this; return r; }
    HRESULT STDMETHODCALLTYPE ReadFileFragment(const void** f, UINT64 off, UINT64 sz, void** ctx) override {
        if (!d || off > n || sz > n - off) return E_FAIL;
        *f = d + off; *ctx = nullptr; return S_OK;
    }
    void STDMETHODCALLTYPE ReleaseFileFragment(void*) override {}
    HRESULT STDMETHODCALLTYPE GetFileSize(UINT64* s) override { *s = n; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetLastWriteTime(UINT64* t) override { *t = 0; return S_OK; }
};
struct MemLoader : IDWriteFontFileLoader {
    std::mutex mu;
    std::vector<std::pair<const BYTE*, UINT64>> blobs;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r, void** o) override {
        if (r == __uuidof(IUnknown) || r == __uuidof(IDWriteFontFileLoader)) { *o = this; return S_OK; }
        *o = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE CreateStreamFromKey(const void* key, UINT32 ks, IDWriteFontFileStream** s) override {
        if (!key || ks != sizeof(size_t) || !s) return E_INVALIDARG;
        const size_t i = *(const size_t*)key;
        const BYTE* ptr = nullptr; UINT64 sz = 0;
        {
            std::lock_guard<std::mutex> l(mu);
            if (i >= blobs.size() || !blobs[i].first || blobs[i].second == 0) return E_INVALIDARG;
            ptr = blobs[i].first; sz = blobs[i].second;
        }
        *s = new MemStream(ptr, sz); return S_OK;
    }
};
MemLoader g_loader;
bool g_loaderRegistered = false;
template <class T> void rel(T*& p) { if (p) { p->Release(); p = nullptr; } }
} // namespace

// ---------------------------------------------------------------- lifetime
void Gfx::releaseTarget() {
    clearGlowCache();
    rel(ringLayer_); rel(ringGeom_);
    rel(circleLayer_); rel(circleGeom_); circleR_ = -1.f;
    rel(spriteRt_); savedRt_ = nullptr;
    rel(roundStroke_); rel(brush_);
    if (rt_ && rt_ != hrt_) rt_->Release();
    rt_ = nullptr;
    rel(hrt_);
    rel(wicBmp_);
}

void Gfx::shutdown() {
    for (int i = 0; i < F_COUNT; ++i) clearFontOverride((FontId)i);
    for (auto& f : faces_) { rel(f.face1); rel(f.face); f.cache.clear(); }
    releaseTarget();
    if (dw_ && g_loaderRegistered) { dw_->UnregisterFontFileLoader(&g_loader); g_loaderRegistered = false; }
    rel(wic_); rel(dw_); rel(d2d_);
}

Gfx::~Gfx() { shutdown(); }

bool Gfx::loadFontData(Face& f, const uint8_t* data, size_t size) {
    if (!dw_ || !data || size == 0) return false;
    size_t key = 0;
    {
        std::lock_guard<std::mutex> l(g_loader.mu);
        key = g_loader.blobs.size();
        g_loader.blobs.push_back({data, size});
    }
    auto clearSlot = [&]() {
        std::lock_guard<std::mutex> l(g_loader.mu);
        if (key < g_loader.blobs.size()) g_loader.blobs[key] = {nullptr, 0};
    };
    IDWriteFontFile* ff = nullptr;
    if (FAILED(dw_->CreateCustomFontFileReference(&key, sizeof(key), &g_loader, &ff))) { clearSlot(); return false; }
    BOOL supported = FALSE; DWRITE_FONT_FILE_TYPE ft = DWRITE_FONT_FILE_TYPE_UNKNOWN; DWRITE_FONT_FACE_TYPE fct = DWRITE_FONT_FACE_TYPE_TRUETYPE; UINT32 nf = 0;
    if (FAILED(ff->Analyze(&supported, &ft, &fct, &nf)) || !supported || nf < 1) { ff->Release(); clearSlot(); return false; }
    IDWriteFontFace* face = nullptr;
    HRESULT hr = dw_->CreateFontFace(fct, 1, &ff, 0, DWRITE_FONT_SIMULATIONS_NONE, &face);
    ff->Release();
    if (FAILED(hr) || !face) { clearSlot(); return false; }
    f.face = face; f.blobKey = key; f.cache.clear();
    DWRITE_FONT_METRICS m{}; f.face->GetMetrics(&m);
    f.upem = m.designUnitsPerEm > 0 ? (float)m.designUnitsPerEm : 1000.f;
    f.cap = m.capHeight / f.upem; f.xh = m.xHeight / f.upem;
    if (f.cap <= 0.05f) f.cap = 0.7f;                         // some fonts do not say: a usual value
    if (f.xh <= 0.05f) f.xh = 0.5f;
    f.face->QueryInterface(__uuidof(IDWriteFontFace1), (void**)&f.face1);
    return true;
}

bool Gfx::loadFont(FontId id, int resId) {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(resId), MAKEINTRESOURCEW(10));
    if (!r) return false;
    HGLOBAL g = LoadResource(nullptr, r);
    if (!g) return false;
    return loadFontData(faces_[id], (const BYTE*)LockResource(g), SizeofResource(nullptr, r));
}

bool Gfx::setFontOverride(FontId id, const uint8_t* data, size_t size) {
    clearFontOverride(id);
    if (!dw_ || !loadFontData(over_[id], data, size)) return false;
    useOver_[id] = true;
    return true;
}

void Gfx::clearFontOverride(FontId id) {
    Face& f = over_[id];
    rel(f.face1); rel(f.face);
    {
        std::lock_guard<std::mutex> l(g_loader.mu);
        if (f.blobKey < g_loader.blobs.size()) g_loader.blobs[f.blobKey] = {nullptr, 0};
    }
    f.blobKey = (size_t)-1; f.cache.clear();
    useOver_[id] = false;
}

Bitmap Gfx::loadImage(const std::wstring& path, size_t maxPixelBytes, bool* tooLarge) {
    Bitmap out;
    if (tooLarge) *tooLarge = false;
    IWICBitmapDecoder* dec = nullptr; IWICBitmapFrameDecode* fr = nullptr; IWICFormatConverter* cv = nullptr;
    if (wic_ && SUCCEEDED(wic_->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) && SUCCEEDED(dec->GetFrame(0, &fr)) &&
        SUCCEEDED(wic_->CreateFormatConverter(&cv)) &&
        SUCCEEDED(cv->Initialize(fr, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
        UINT w = 0, h = 0; cv->GetSize(&w, &h);
        if (w > 8192 || h > 8192 || (w > 0 && h > 0 && maxPixelBytes && (size_t)w * h * 4 > maxPixelBytes)) { if (tooLarge) *tooLarge = true; }
        else if (w > 0 && h > 0) {
            std::vector<uint8_t> px((size_t)w * h * 4);
            if (SUCCEEDED(cv->CopyPixels(nullptr, w * 4, (UINT)px.size(), px.data()))) out = makeBitmap(px.data(), (int)w, (int)h);
        }
    }
    rel(cv); rel(fr); rel(dec);
    return out;
}

bool Gfx::init() {
    wine_ = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != nullptr;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d_))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&dw_))) return false;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic_)))) return false;
    if (!g_loaderRegistered) { dw_->RegisterFontFileLoader(&g_loader); g_loaderRegistered = true; }
    bool ok = loadFont(F_DISPLAY, IDR_FONT_SORA200) && loadFont(F_LIGHT, IDR_FONT_SORA300) && loadFont(F_MEDIUM, IDR_FONT_SORA500) &&
              loadFont(F_SERIF, IDR_FONT_SERIF) && loadFont(F_SERIF_I, IDR_FONT_SERIF_I) && loadFont(F_MONO, IDR_FONT_MONO) && loadFont(F_MONO_M, IDR_FONT_MONO_M);
    return ok;
}

void Gfx::clearGlowCache() { for (auto& kv : glowCache_) if (kv.second) kv.second->Release(); glowCache_.clear(); }
void Gfx::invalidateDeviceResources() {
    clearGlowCache();
    rel(ringLayer_); rel(ringGeom_);
    rel(circleLayer_); rel(circleGeom_); circleR_ = -1.f;
    for (int i = 0; i < 4; ++i) { rel(rrPool_[i].layer); rel(rrPool_[i].geom); rrPool_[i].w = -1.f; rrPushed_[i] = false; }
    rrDepth_ = 0;
    rel(spriteRt_); savedRt_ = nullptr; rel(brush_); rel(roundStroke_);
}

static bool makeDeviceResources(ID2D1Factory* f, ID2D1RenderTarget* rt, ID2D1SolidColorBrush** brush, ID2D1StrokeStyle** ss) {
    if (FAILED(rt->CreateSolidColorBrush(ColorF(1, 1, 1, 1), brush))) return false;
    D2D1_STROKE_STYLE_PROPERTIES sp = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND, 10.f, D2D1_DASH_STYLE_SOLID, 0.f);
    return SUCCEEDED(f->CreateStrokeStyle(sp, nullptr, 0, ss));
}

bool Gfx::createWindowTarget(HWND hwnd, int w, int h) {
    releaseTarget();
    w_ = std::max(1, w); h_ = std::max(1, h);
    D2D1_RENDER_TARGET_PROPERTIES rp = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_UNKNOWN), 96.f, 96.f);
    if (FAILED(d2d_->CreateHwndRenderTarget(rp, D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(w_, h_)), &hrt_))) return false;
    rt_ = hrt_;
    rt_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    if (!makeDeviceResources(d2d_, rt_, &brush_, &roundStroke_)) { releaseTarget(); return false; }
    return true;
}

bool Gfx::createOffscreenTarget(int w, int h) {
    releaseTarget();
    w_ = std::max(1, w); h_ = std::max(1, h);
    if (FAILED(wic_->CreateBitmap(w_, h_, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &wicBmp_))) return false;
    D2D1_RENDER_TARGET_PROPERTIES rp = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.f, 96.f);
    ID2D1RenderTarget* t = nullptr;
    if (FAILED(d2d_->CreateWicBitmapRenderTarget(wicBmp_, rp, &t))) { rel(wicBmp_); return false; }
    rt_ = t;
    rt_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    if (!makeDeviceResources(d2d_, rt_, &brush_, &roundStroke_)) { releaseTarget(); return false; }
    return true;
}

void Gfx::resize(int w, int h) {
    w_ = std::max(1, w); h_ = std::max(1, h);
    if (hrt_) {
        HRESULT hr = hrt_->Resize(D2D1::SizeU(w_, h_));
        if (hr == (HRESULT)D2DERR_RECREATE_TARGET) releaseTarget();
    }
}

bool Gfx::savePng(const wchar_t* path) {
    if (!wicBmp_) return false;
    IWICStream* stm = nullptr; IWICBitmapEncoder* enc = nullptr; IWICBitmapFrameEncode* fr = nullptr;
    bool ok = false;
    if (SUCCEEDED(wic_->CreateStream(&stm)) && SUCCEEDED(stm->InitializeFromFilename(path, GENERIC_WRITE)) &&
        SUCCEEDED(wic_->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) && SUCCEEDED(enc->Initialize(stm, WICBitmapEncoderNoCache)) &&
        SUCCEEDED(enc->CreateNewFrame(&fr, nullptr)) && SUCCEEDED(fr->Initialize(nullptr)) && SUCCEEDED(fr->SetSize(w_, h_))) {
        WICPixelFormatGUID pf = GUID_WICPixelFormat32bppBGRA;
        fr->SetPixelFormat(&pf);
        if (SUCCEEDED(fr->WriteSource(wicBmp_, nullptr)) && SUCCEEDED(fr->Commit()) && SUCCEEDED(enc->Commit())) ok = true;
    }
    rel(fr); rel(enc); rel(stm);
    return ok;
}

bool Gfx::occluded() const { return hrt_ && (hrt_->CheckWindowState() & D2D1_WINDOW_STATE_OCCLUDED) != 0; }

// What every drawing starts from: the reveal zoom (the screen saver's start, normally 1 = none) about its centre, then the burn-in shift.
static D2D1::Matrix3x2F baseM(float sc, float cx, float cy, float ox, float oy) {
    return D2D1::Matrix3x2F::Scale(sc, sc, D2D1::Point2F(cx, cy)) * D2D1::Matrix3x2F::Translation(ox, oy);
}
bool Gfx::begin() { if (!rt_) return false; ox_ = oy_ = 0; rsc_ = 1.f; rt_->BeginDraw(); rt_->SetTransform(D2D1::Matrix3x2F::Identity()); return true; }
void Gfx::setOffset(float x, float y) { ox_ = x; oy_ = y; rt_->SetTransform(baseM(rsc_, rcx_, rcy_, x, y)); }
void Gfx::setReveal(float scale, float cx, float cy) { rsc_ = scale; rcx_ = cx; rcy_ = cy; if (rt_) rt_->SetTransform(baseM(rsc_, rcx_, rcy_, ox_, oy_)); }
// A soft round window onto what is underneath: clear inside `inner`, black outside `outer`, a smooth fade between. Covers the whole w x h.
void Gfx::vignette(float cx, float cy, float inner, float outer, float w, float h) {
    if (outer <= 1.f) { fillRect(0, 0, w, h, Color{0, 0, 0, 1.f}); return; }
    const float f = std::min(0.999f, std::max(0.001f, inner / outer));
    ID2D1GradientStopCollection* gs = stops({{0.f, Color{0, 0, 0, 0.f}}, {f, Color{0, 0, 0, 0.f}}, {1.f, Color{0, 0, 0, 1.f}}});
    if (!gs) return;
    ID2D1RadialGradientBrush* b = nullptr;
    rt_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(Point2F(cx, cy), Point2F(0, 0), outer, outer), gs, &b);
    if (b) { rt_->FillRectangle(RectF(0, 0, w, h), b); b->Release(); }
    gs->Release();
}
bool Gfx::end() {
    if (!rt_) return false;
    { LARGE_INTEGER q; QueryPerformanceCounter(&q); preEnd_ = q.QuadPart; }
    HRESULT hr = rt_->EndDraw();
    if (hr == (HRESULT)D2DERR_RECREATE_TARGET) return false;
    return SUCCEEDED(hr);
}
void Gfx::clear(const Color& c) { rt_->Clear(ColorF(c.r, c.g, c.b, c.a)); }

// ---------------------------------------------------------------- brushes & geometry helpers
void Gfx::setBrush(const Color& c) { if (!brush_) return; brush_->SetColor(ColorF(c.r, c.g, c.b, c.a)); brush_->SetOpacity(1.f); }

ID2D1GradientStopCollection* Gfx::stops(const std::vector<Stop>& s) {
    std::vector<D2D1_GRADIENT_STOP> g;
    for (const auto& st : s) {
        Color c = st.c;
        if (wine_) { c.r *= c.a; c.g *= c.a; c.b *= c.a; }      // Wine does not premultiply gradient stops
        g.push_back({st.pos, ColorF(c.r, c.g, c.b, c.a)});
    }
    ID2D1GradientStopCollection* coll = nullptr;
    rt_->CreateGradientStopCollection(g.data(), (UINT32)g.size(), D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &coll);
    return coll;
}

ID2D1PathGeometry* Gfx::polyGeom(const std::vector<Pt>& pts, bool closed, bool filled) {
    if (pts.size() < 2) return nullptr;
    ID2D1PathGeometry* g = nullptr; ID2D1GeometrySink* sink = nullptr;
    if (FAILED(d2d_->CreatePathGeometry(&g))) return nullptr;
    if (FAILED(g->Open(&sink))) { g->Release(); return nullptr; }
    sink->BeginFigure(Point2F(pts[0].x, pts[0].y), filled ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
    for (size_t i = 1; i < pts.size(); ++i) sink->AddLine(Point2F(pts[i].x, pts[i].y));
    sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
    sink->Close(); sink->Release();
    return g;
}

// ---------------------------------------------------------------- shapes
void Gfx::fillRect(float x, float y, float w, float h, const Color& c) { setBrush(c); rt_->FillRectangle(RectF(x, y, x + w, y + h), brush_); }

void Gfx::fillRoundRect(float x, float y, float w, float h, float r, const Color& c) {
    setBrush(c); rt_->FillRoundedRectangle(D2D1::RoundedRect(RectF(x, y, x + w, y + h), r, r), brush_);
}
void Gfx::strokeRoundRect(float x, float y, float w, float h, float r, const Color& c, float width) {
    setBrush(c); rt_->DrawRoundedRectangle(D2D1::RoundedRect(RectF(x, y, x + w, y + h), r, r), brush_, width);
}

void Gfx::vGradientRect(float x, float y, float w, float h, const Color& top, const Color& bottom) {
    ID2D1GradientStopCollection* gs = stops({{0.f, top}, {1.f, bottom}});
    if (!gs) return;
    ID2D1LinearGradientBrush* b = nullptr;
    rt_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(Point2F(0, y), Point2F(0, y + h)), gs, &b);
    if (b) { rt_->FillRectangle(RectF(x, y, x + w, y + h), b); b->Release(); }
    gs->Release();
}

void Gfx::fillCircle(float cx, float cy, float r, const Color& c) { setBrush(c); rt_->FillEllipse(D2D1::Ellipse(Point2F(cx, cy), r, r), brush_); }
void Gfx::strokeCircle(float cx, float cy, float r, const Color& c, float width) { setBrush(c); rt_->DrawEllipse(D2D1::Ellipse(Point2F(cx, cy), r, r), brush_, width); }
void Gfx::strokeEllipse(float cx, float cy, float rx, float ry, const Color& c, float width) { setBrush(c); rt_->DrawEllipse(D2D1::Ellipse(Point2F(cx, cy), rx, ry), brush_, width); }

void Gfx::line(float x1, float y1, float x2, float y2, const Color& c, float width, bool round) {
    setBrush(c); rt_->DrawLine(Point2F(x1, y1), Point2F(x2, y2), brush_, width, round ? roundStroke_ : nullptr);
}

void Gfx::polyline(const std::vector<Pt>& pts, bool closed, const Color& c, float width, bool round) {
    ID2D1PathGeometry* g = polyGeom(pts, closed, false);
    if (!g) return;
    setBrush(c); rt_->DrawGeometry(g, brush_, width, round ? roundStroke_ : nullptr);
    g->Release();
}

void Gfx::fillPolygon(const std::vector<Pt>& pts, const Color& c) {
    ID2D1PathGeometry* g = polyGeom(pts, true, true);
    if (!g) return;
    setBrush(c); rt_->FillGeometry(g, brush_);
    g->Release();
}

void Gfx::arc(float cx, float cy, float r, float startDeg, float sweepDeg, const Color& c, float width, bool round) {
    if (!std::isfinite(sweepDeg)) return;
    sweepDeg = std::max(-360.f, std::min(360.f, sweepDeg));
    int n = std::max(2, (int)std::ceil(std::fabs(sweepDeg) / 2.0f) + 1);
    std::vector<Pt> pts; pts.reserve(n);
    for (int i = 0; i < n; ++i) {
        float a = (startDeg + sweepDeg * i / (n - 1)) * 3.14159265f / 180.f;
        pts.push_back({cx + r * std::sin(a), cy - r * std::cos(a)});
    }
    polyline(pts, false, c, width, round);
}

void Gfx::radialFill(float cx, float cy, float r, const std::vector<Stop>& st, float opacity) {
    ID2D1GradientStopCollection* gs = stops(st);
    if (!gs) return;
    ID2D1RadialGradientBrush* b = nullptr;
    rt_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(Point2F(cx, cy), Point2F(0, 0), r, r), gs, &b);
    if (b) { b->SetOpacity(opacity); rt_->FillEllipse(D2D1::Ellipse(Point2F(cx, cy), r, r), b); b->Release(); }
    gs->Release();
}

void Gfx::radialFillOffset(float cx, float cy, float r, float ox, float oy, const std::vector<Stop>& st) {
    ID2D1GradientStopCollection* gs = stops(st);
    if (!gs) return;
    ID2D1RadialGradientBrush* b = nullptr;
    rt_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(Point2F(cx, cy), Point2F(ox, oy), r, r), gs, &b);
    if (b) { rt_->FillEllipse(D2D1::Ellipse(Point2F(cx, cy), r, r), b); b->Release(); }
    gs->Release();
}

void Gfx::radialEllipse(float cx, float cy, float rx, float ry, float ox, float oy, const std::vector<Stop>& st, float opacity) {
    if (opacity < 0.002f || rx < 0.5f || ry < 0.5f) return;
    ID2D1GradientStopCollection* gs = stops(st);
    if (!gs) return;
    ID2D1RadialGradientBrush* b = nullptr;
    rt_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(Point2F(cx, cy), Point2F(ox, oy), rx, ry), gs, &b);
    if (b) { b->SetOpacity(opacity); rt_->FillEllipse(D2D1::Ellipse(Point2F(cx, cy), rx, ry), b); b->Release(); }
    gs->Release();
}

void Gfx::glow(float cx, float cy, float r, const Color& c, float intensity) { glowEllipse(cx, cy, r, r, c, intensity); }

void Gfx::glowEllipse(float cx, float cy, float rx, float ry, const Color& c, float intensity) {
    const float a = clamp01(intensity) * c.a;
    if (a < 0.002f || rx < 0.5f || ry < 0.5f) return;
    // an eased falloff. The brush for a colour is built once and reused: only its centre, size and opacity change per call
    const uint32_t key = ((uint32_t)(clamp01(c.r) * 255.f + 0.5f) << 16) | ((uint32_t)(clamp01(c.g) * 255.f + 0.5f) << 8) | (uint32_t)(clamp01(c.b) * 255.f + 0.5f);
    ID2D1RadialGradientBrush* b = nullptr;
    auto it = glowCache_.find(key);
    if (it != glowCache_.end()) b = it->second;
    else {
        if (glowCache_.size() > 300) clearGlowCache();                    // colours fading between themes would otherwise pile up
        const Color k = {c.r, c.g, c.b, 1.f};
        ID2D1GradientStopCollection* gs = stops({{0.f, withA(k, 1.f)}, {0.18f, withA(k, 0.55f)}, {0.42f, withA(k, 0.22f)}, {0.7f, withA(k, 0.06f)}, {1.f, withA(k, 0.f)}});
        if (gs) { rt_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(Point2F(0, 0), Point2F(0, 0), 1.f, 1.f), gs, &b); gs->Release(); }
        if (b) glowCache_[key] = b;
    }
    if (!b) return;
    b->SetOpacity(a); b->SetCenter(Point2F(cx, cy)); b->SetRadiusX(rx); b->SetRadiusY(ry);
    rt_->FillEllipse(D2D1::Ellipse(Point2F(cx, cy), rx, ry), b);
}

void Gfx::fillCircleVGradient(float cx, float cy, float r, const Color& top, const Color& bottom) {
    ID2D1GradientStopCollection* gs = stops({{0.f, top}, {1.f, bottom}});
    if (!gs) return;
    ID2D1LinearGradientBrush* b = nullptr;
    rt_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(Point2F(0, cy - r), Point2F(0, cy + r)), gs, &b);
    if (b) { rt_->FillEllipse(D2D1::Ellipse(Point2F(cx, cy), r, r), b); b->Release(); }
    gs->Release();
}

void Gfx::fillPolyVGradient(const std::vector<Pt>& pts, float y0, float y1, const Color& top, const Color& bottom) {
    ID2D1PathGeometry* g = polyGeom(pts, true, true);
    if (!g) return;
    ID2D1GradientStopCollection* gs = stops({{0.f, top}, {1.f, bottom}});
    ID2D1LinearGradientBrush* b = nullptr;
    if (gs) rt_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(Point2F(0, y0), Point2F(0, y1)), gs, &b);
    if (b) { rt_->FillGeometry(g, b); b->Release(); }
    if (gs) gs->Release();
    g->Release();
}

// The ring: a square with a round hole in it (alternate fill).
ID2D1PathGeometry* Gfx::ringGeometry(float cx, float cy, float r, float outer) {
    ID2D1PathGeometry* g = nullptr; ID2D1GeometrySink* sink = nullptr;
    if (FAILED(d2d_->CreatePathGeometry(&g)) || FAILED(g->Open(&sink))) { if (g) g->Release(); return nullptr; }
    sink->SetFillMode(D2D1_FILL_MODE_ALTERNATE);
    sink->BeginFigure(Point2F(cx - outer, cy - outer), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(Point2F(cx + outer, cy - outer)); sink->AddLine(Point2F(cx + outer, cy + outer)); sink->AddLine(Point2F(cx - outer, cy + outer));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    const int N = 240;
    sink->BeginFigure(Point2F(cx + r, cy), D2D1_FIGURE_BEGIN_FILLED);
    for (int i = 1; i < N; ++i) { float a = 6.2831853f * i / N; sink->AddLine(Point2F(cx + r * std::cos(a), cy + r * std::sin(a))); }
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close(); sink->Release();
    return g;
}

// Paints the page background over everything outside a circle (inside a square) so glows cannot spill past the dome.
void Gfx::ringMask(float cx, float cy, float r, float outer, const Color& top, const Color& bottom, float gradY0, float gradY1) {
    ID2D1PathGeometry* g = ringGeometry(cx, cy, r, outer);
    if (!g) return;
    ID2D1GradientStopCollection* gs = stops({{0.f, top}, {1.f, bottom}});
    ID2D1LinearGradientBrush* b = nullptr;
    if (gs) rt_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(Point2F(0, gradY0), Point2F(0, gradY1)), gs, &b);
    if (b) { rt_->FillGeometry(g, b); b->Release(); }
    if (gs) gs->Release();
    g->Release();
}

void Gfx::pushRingClip(float cx, float cy, float r, float outer) {
    if (ringLayer_) return;                                   // one at a time
    ringGeom_ = ringGeometry(cx, cy, r, outer);
    if (!ringGeom_) return;
    if (FAILED(rt_->CreateLayer(nullptr, &ringLayer_)) || !ringLayer_) { ringGeom_->Release(); ringGeom_ = nullptr; ringLayer_ = nullptr; return; }
    rt_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), ringGeom_), ringLayer_);
}
void Gfx::popRingClip() {
    if (!ringLayer_) return;
    rt_->PopLayer();
    ringLayer_->Release(); ringLayer_ = nullptr;
    if (ringGeom_) { ringGeom_->Release(); ringGeom_ = nullptr; }
}

void Gfx::pushClip(float x, float y, float w, float h) { rt_->PushAxisAlignedClip(RectF(x, y, x + w, y + h), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE); }
void Gfx::popClip() { rt_->PopAxisAlignedClip(); }

void Gfx::pushRoundRectClip(float x, float y, float w, float h, float r) {
    if (rrDepth_ >= 4) { ++rrDepth_; return; }
    const int idx = rrDepth_++;
    rrPushed_[idx] = false;
    if (!rt_ || !d2d_ || w <= 0.f || h <= 0.f) return;
    RRSlot& sl = rrPool_[idx];
    const float rr = std::max(0.f, std::min(r, std::min(w, h) * 0.5f));
    if (!sl.geom || std::fabs(x - sl.x) > 0.01f || std::fabs(y - sl.y) > 0.01f ||
        std::fabs(w - sl.w) > 0.01f || std::fabs(h - sl.h) > 0.01f || std::fabs(rr - sl.r) > 0.01f) {
        rel(sl.geom);
        if (FAILED(d2d_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(RectF(x, y, x + w, y + h), rr, rr), &sl.geom))) {
            sl.geom = nullptr;
            return;
        }
        sl.x = x; sl.y = y; sl.w = w; sl.h = h; sl.r = rr;
    }
    if (!sl.layer && FAILED(rt_->CreateLayer(nullptr, &sl.layer))) { sl.layer = nullptr; return; }
    if (!sl.layer || !sl.geom) return;
    rt_->PushLayer(D2D1::LayerParameters(RectF(x - 1.f, y - 1.f, x + w + 1.f, y + h + 1.f), sl.geom), sl.layer);
    rrPushed_[idx] = true;
}
void Gfx::popRoundRectClip() {
    if (rrDepth_ <= 0) return;
    const int idx = --rrDepth_;
    if (idx < 4 && rrPushed_[idx] && rt_) rt_->PopLayer();
    if (idx < 4) rrPushed_[idx] = false;
}

void Gfx::pushCircleClip(float cx, float cy, float r) {
    if (!rt_ || !d2d_ || r <= 0.f) return;
    if (!circleGeom_ || std::fabs(cx - circleCx_) > 0.01f || std::fabs(cy - circleCy_) > 0.01f || std::fabs(r - circleR_) > 0.01f) {
        rel(circleGeom_);
        if (FAILED(d2d_->CreateEllipseGeometry(D2D1::Ellipse(Point2F(cx, cy), r, r), &circleGeom_))) { circleGeom_ = nullptr; return; }
        circleCx_ = cx; circleCy_ = cy; circleR_ = r;
    }
    if (!circleLayer_ && FAILED(rt_->CreateLayer(nullptr, &circleLayer_))) { circleLayer_ = nullptr; return; }
    if (!circleLayer_ || !circleGeom_) return;
    rt_->PushLayer(D2D1::LayerParameters(RectF(cx - r - 1.f, cy - r - 1.f, cx + r + 1.f, cy + r + 1.f), circleGeom_), circleLayer_);
}
void Gfx::popCircleClip() {
    if (rt_ && circleLayer_ && circleGeom_) rt_->PopLayer();
}

// ---------------------------------------------------------------- text
const Gfx::Glyph& Gfx::glyph(Face& f, uint32_t cp) {
    auto it = f.cache.find(cp);
    if (it != f.cache.end()) return it->second;
    UINT16 idx = 0; UINT32 c = cp;
    f.face->GetGlyphIndices(&c, 1, &idx);
    Glyph g{idx, 0.f};
    if (idx == 0 && cp != 0) {   // fall back to '?' for missing glyphs, space for space-like
        UINT32 q = (cp == ' ' || cp == 0xA0) ? ' ' : '?'; UINT16 qi = 0; f.face->GetGlyphIndices(&q, 1, &qi); g.index = qi;
    }
    DWRITE_GLYPH_METRICS gm{};
    UINT16 gi = g.index;
    f.face->GetDesignGlyphMetrics(&gi, 1, &gm, FALSE);
    g.adv = gm.advanceWidth / f.upem;
    return f.cache.emplace(cp, g).first->second;
}

void Gfx::shape(FontId id, float size, const std::wstring& s, float tracking, std::vector<UINT16>& idx, std::vector<float>& adv, float* total) {
    Face& f = fc(id);
    idx.clear(); adv.clear();
    idx.reserve(s.size()); adv.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        uint32_t cp = (uint32_t)s[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.size()) {
            uint32_t lo = (uint32_t)s[i + 1];
            if (lo >= 0xDC00 && lo <= 0xDFFF) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); ++i; }
            else cp = 0xFFFD;
        } else if (cp >= 0xD800 && cp <= 0xDFFF) {
            cp = 0xFFFD;
        }
        const Glyph& g = glyph(f, cp);
        idx.push_back(g.index); adv.push_back(g.adv * size + tracking * size);
    }
    if (f.face1 && idx.size() > 1 && tracking == 0.f) {
        scratchKern_.assign(idx.size(), 0);
        if (SUCCEEDED(f.face1->GetKerningPairAdjustments((UINT32)idx.size(), idx.data(), scratchKern_.data())))
            for (size_t i = 0; i < idx.size(); ++i) adv[i] += scratchKern_[i] / f.upem * size;
    }
    float t = 0; for (float a : adv) t += a;
    if (total) *total = t;
}

float Gfx::textWidth(FontId f, float size, const std::wstring& s, float tracking) {
    float t = 0;
    shape(f, size, s, tracking, scratchIdx_, scratchAdv_, &t);
    if (!scratchAdv_.empty()) t -= tracking * size;     // no trailing tracking
    return t;
}
float Gfx::capHeight(FontId f, float size) { return fc(f).cap * size; }
float Gfx::xHeight(FontId f, float size) { return fc(f).xh * size; }

void Gfx::drawRun(Face& f, float size, float x, float baseline, std::vector<UINT16>& idx, std::vector<float>& adv, ID2D1Brush* brush) {
    if (idx.empty()) return;
    DWRITE_GLYPH_RUN run{};
    run.fontFace = f.face; run.fontEmSize = size; run.glyphCount = (UINT32)idx.size();
    run.glyphIndices = idx.data(); run.glyphAdvances = adv.data();
    rt_->DrawGlyphRun(Point2F(x, baseline), &run, brush, DWRITE_MEASURING_MODE_NATURAL);
}

void Gfx::text(FontId id, float size, float x, float baseline, const std::wstring& s, const Color& c, Align al, float tracking) {
    if (s.empty() || c.a <= 0.003f) return;
    float total = 0;
    shape(id, size, s, tracking, scratchIdx_, scratchAdv_, &total);
    if (!scratchAdv_.empty()) total -= tracking * size;
    if (al == Align::Center) x -= total * 0.5f; else if (al == Align::Right) x -= total;
    setBrush(c);
    drawRun(fc(id), size, x, baseline, scratchIdx_, scratchAdv_, brush_);
}

void Gfx::textGradient(FontId id, float size, float x, float baseline, const std::wstring& s, const Color& top, const Color& bottom, float gy0, float gy1, Align al, float tracking) {
    if (s.empty()) return;
    float total = 0;
    shape(id, size, s, tracking, scratchIdx_, scratchAdv_, &total);
    if (!scratchAdv_.empty()) total -= tracking * size;
    if (al == Align::Center) x -= total * 0.5f; else if (al == Align::Right) x -= total;
    ID2D1GradientStopCollection* gs = stops({{0.f, top}, {1.f, bottom}});
    ID2D1LinearGradientBrush* b = nullptr;
    if (gs) rt_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(Point2F(0, gy0), Point2F(0, gy1)), gs, &b);
    if (b) { drawRun(fc(id), size, x, baseline, scratchIdx_, scratchAdv_, b); b->Release(); }
    if (gs) gs->Release();
}

float Gfx::digitCell(FontId f, float size) {
    float m = 0;
    for (wchar_t d = L'0'; d <= L'9'; ++d) m = std::max(m, glyph(fc(f), d).adv);
    return m * size;
}

float Gfx::timeWidth(FontId f, float size, const std::wstring& s, float colonScale, float track) {
    if (s.empty()) return 0.f;
    float w = 0, cell = digitCell(f, size);
    for (wchar_t ch : s) w += (ch >= L'0' && ch <= L'9') ? cell : (ch == L':' || ch == L'.') ? cell * colonScale : glyph(fc(f), ch).adv * size;
    return w + track * size * (float)(s.size() - 1);
}

void Gfx::timeText(FontId id, float size, float x, float baseline, const std::wstring& s, const Color& top, const Color& bottom, Align al, float colonScale, float track) {
    if (s.empty()) return;
    float total = timeWidth(id, size, s, colonScale, track), cell = digitCell(id, size);
    if (al == Align::Center) x -= total * 0.5f; else if (al == Align::Right) x -= total;
    ID2D1GradientStopCollection* gs = stops({{0.f, top}, {1.f, bottom}});
    ID2D1LinearGradientBrush* b = nullptr;
    float capH = capHeight(id, size);
    if (gs) rt_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(Point2F(0, baseline - capH), Point2F(0, baseline)), gs, &b);
    Face& f = fc(id);
    for (wchar_t ch : s) {
        float cellW = (ch >= L'0' && ch <= L'9') ? cell : (ch == L':' || ch == L'.') ? cell * colonScale : glyph(f, ch).adv * size;
        const Glyph& g = glyph(f, ch);
        UINT16 gi = g.index; float ga = g.adv * size;
        DWRITE_GLYPH_RUN run{}; run.fontFace = f.face; run.fontEmSize = size; run.glyphCount = 1; run.glyphIndices = &gi; run.glyphAdvances = &ga;
        float gx = x + (cellW - ga) * 0.5f;
        if (neonA_ > 0.01f && size >= 70.f && ch != L' ') {          // neon bloom: the glyph again, a little wider, a little fainter
            static const float dx[8] = {1, 0.7071f, 0, -0.7071f, -1, -0.7071f, 0, 0.7071f}, dy[8] = {0, 0.7071f, 1, 0.7071f, 0, -0.7071f, -1, -0.7071f};
            for (int ring = 0; ring < 2; ++ring) {
                const float rr = size * (ring ? 0.030f : 0.012f);
                setBrush(withA(neonC_, (ring ? 0.045f : 0.085f) * neonA_));
                for (int k = 0; k < 8; ++k) rt_->DrawGlyphRun(Point2F(gx + dx[k] * rr, baseline + dy[k] * rr), &run, brush_, DWRITE_MEASURING_MODE_NATURAL);
            }
        }
        if (b) rt_->DrawGlyphRun(Point2F(gx, baseline), &run, b, DWRITE_MEASURING_MODE_NATURAL);
        x += cellW + track * size;
    }
    if (b) b->Release();
    if (gs) gs->Release();
}

std::wstring Gfx::ellipsize(FontId f, float size, const std::wstring& s, float maxW, float tracking) {
    if (s.empty() || textWidth(f, size, s, tracking) <= maxW) return s;
    size_t lo = 0, hi = s.size();
    while (lo < hi) {
        size_t mid = lo + (hi - lo + 1) / 2;
        size_t cut = mid;
        if (cut > 0 && s[cut - 1] >= 0xD800 && s[cut - 1] <= 0xDBFF) --cut;    // never split a UTF-16 surrogate pair
        if (textWidth(f, size, s.substr(0, cut) + L"…", tracking) <= maxW) lo = mid;
        else hi = mid - 1;
    }
    if (lo > 0 && s[lo - 1] >= 0xD800 && s[lo - 1] <= 0xDBFF) --lo;
    return s.substr(0, lo) + L"…";
}

std::vector<std::wstring> Gfx::wrap(FontId f, float size, const std::wstring& s, float maxW, float tracking, int maxLines) {
    std::vector<std::wstring> lines;
    if (maxLines <= 0) return lines;
    std::wstring cur, word;
    auto flush = [&]() {
        if (word.empty()) return;
        std::wstring t = cur.empty() ? word : cur + L" " + word;
        if (!cur.empty() && textWidth(f, size, t, tracking) > maxW) { lines.push_back(cur); cur = word; }
        else cur = t;
        word.clear();
    };
    for (wchar_t ch : s) { if (ch == L' ') flush(); else word.push_back(ch); }
    flush();
    if (!cur.empty()) lines.push_back(cur);
    if ((int)lines.size() > maxLines) {
        lines.resize((size_t)maxLines);
        std::wstring& last = lines.back();
        if (textWidth(f, size, last + L"…", tracking) <= maxW) last += L"…";
        else last = ellipsize(f, size, last, maxW, tracking);
    }
    return lines;
}

// ---------------------------------------------------------------- bitmaps
Bitmap Gfx::makeBitmap(const uint8_t* data, int w, int h) {
    Bitmap b;
    if (!rt_) return b;                                          // no render target right now (it is being rebuilt)
    D2D1_BITMAP_PROPERTIES bp = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.f, 96.f);
    if (SUCCEEDED(rt_->CreateBitmap(D2D1::SizeU(w, h), data, w * 4, bp, &b.bmp))) { b.w = w; b.h = h; }
    return b;
}

void Gfx::drawBitmap(const Bitmap& b, float cx, float cy, float size, float rotationDeg, float opacity) {
    if (!b.bmp) return;
    rt_->SetTransform(D2D1::Matrix3x2F::Rotation(rotationDeg, Point2F(cx, cy)) * baseM(rsc_, rcx_, rcy_, ox_, oy_));
    rt_->DrawBitmap(b.bmp, RectF(cx - size / 2, cy - size / 2, cx + size / 2, cy + size / 2), opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    rt_->SetTransform(baseM(rsc_, rcx_, rcy_, ox_, oy_));
}

void Gfx::drawBitmapRect(const Bitmap& b, float x, float y, float w, float h, float opacity) {
    if (!b.bmp || opacity <= 0.003f) return;
    rt_->DrawBitmap(b.bmp, RectF(x, y, x + w, y + h), clamp01(opacity), D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

void Gfx::drawBitmapSub(const Bitmap& b, float dx, float dy, float sx, float sy, float sw, float sh, float opacity) {
    if (!b.bmp || opacity <= 0.003f || sw <= 0.f || sh <= 0.f) return;
    rt_->DrawBitmap(b.bmp, RectF(dx, dy, dx + sw, dy + sh), clamp01(opacity), D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, RectF(sx, sy, sx + sw, sy + sh));
}

void Gfx::drawBitmapRot(const Bitmap& b, float x, float y, float w, float h, float px, float py, float rotDeg, float opacity, bool flipX) {
    if (!b.bmp || opacity <= 0.003f) return;
    rt_->SetTransform(D2D1::Matrix3x2F::Scale(flipX ? -1.f : 1.f, 1.f, Point2F(x + w * 0.5f, y + h * 0.5f)) * D2D1::Matrix3x2F::Rotation(rotDeg, Point2F(px, py)) * baseM(rsc_, rcx_, rcy_, ox_, oy_));
    rt_->DrawBitmap(b.bmp, RectF(x, y, x + w, y + h), clamp01(opacity), D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    rt_->SetTransform(baseM(rsc_, rcx_, rcy_, ox_, oy_));
}

bool Gfx::beginSprite(int w, int h) {
    if (!rt_ || spriteRt_ || w < 2 || h < 2) return false;
    ID2D1BitmapRenderTarget* t = nullptr;
    if (FAILED(rt_->CreateCompatibleRenderTarget(D2D1::SizeF((float)w, (float)h), D2D1::SizeU((UINT32)w, (UINT32)h),
               D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE, &t)) || !t) return false;
    spriteRt_ = t; savedRt_ = rt_; savedOx_ = ox_; savedOy_ = oy_; savedRsc_ = rsc_; spriteW_ = w; spriteH_ = h;
    rt_ = t; ox_ = oy_ = 0; rsc_ = 1.f;               // (a picture is drawn at its own size, whatever the screen is doing)
    rt_->BeginDraw(); rt_->Clear(ColorF(0, 0, 0, 0)); rt_->SetTransform(D2D1::Matrix3x2F::Identity());
    return true;
}

Bitmap Gfx::endSprite() {
    Bitmap b;
    if (!spriteRt_) return b;
    HRESULT hr = rt_->EndDraw();
    ID2D1Bitmap* bmp = nullptr;
    if (SUCCEEDED(hr) && SUCCEEDED(spriteRt_->GetBitmap(&bmp)) && bmp) { b.bmp = bmp; b.w = spriteW_; b.h = spriteH_; }
    rt_ = savedRt_; ox_ = savedOx_; oy_ = savedOy_; rsc_ = savedRsc_; savedRt_ = nullptr;
    rel(spriteRt_);
    return b;
}

void Gfx::freeBitmap(Bitmap& b) { rel(b.bmp); b.w = b.h = 0; }

// ---------------------------------------------------------------- weather icons
namespace {
struct Outline { std::vector<Pt> pts; };

bool inRoundRect(double x, double y, double x0, double y0, double x1, double y1, double rr) {
    double cx = std::max(x0 + rr, std::min(x, x1 - rr)), cy = std::max(y0 + rr, std::min(y, y1 - rr));
    return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= rr * rr + 1e-12 && x >= x0 && x <= x1 && y >= y0 && y <= y1;
}

const std::vector<Pt>& cloudOutline() {
    static std::vector<Pt> out;
    if (!out.empty()) return out;
    struct Circ { double x, y, r; };
    const Circ cs[] = {{-0.36, 0.14, 0.40}, {0.02, -0.12, 0.46}, {0.42, 0.16, 0.44}};
    const double px = 0.0, py = 0.12;
    const int N = 180;
    std::vector<Pt> raw;
    for (int i = 0; i < N; ++i) {
        double th = 6.283185307 * i / N, ux = std::cos(th), uy = std::sin(th), best = 0;
        for (const Circ& c : cs) {
            double bx = px - c.x, by = py - c.y, b = bx * ux + by * uy, cc = bx * bx + by * by - c.r * c.r;
            double disc = b * b - cc;
            if (disc >= 0) best = std::max(best, -b + std::sqrt(disc));
        }
        double lo = 0, hi = 3;
        for (int k = 0; k < 40; ++k) { double m = 0.5 * (lo + hi); if (inRoundRect(px + m * ux, py + m * uy, -0.74, 0.0, 0.78, 0.50, 0.25)) lo = m; else hi = m; }
        best = std::max(best, lo);
        raw.push_back({(float)(px + best * ux), (float)(py + best * uy)});
    }
    {   // the rounded-rect base is dead straight; give it a slight belly so no run of exactly collinear points reaches the stroker
        float fx0 = 9, fx1 = -9;
        for (auto& p : raw) if (std::fabs(p.y - 0.50f) < 1e-4f) { fx0 = std::min(fx0, p.x); fx1 = std::max(fx1, p.x); }
        if (fx1 > fx0) {
            float mid = 0.5f * (fx0 + fx1), hw = 0.5f * (fx1 - fx0);
            for (auto& p : raw) if (std::fabs(p.y - 0.50f) < 1e-4f) { float u = (p.x - mid) / hw; p.y += 0.035f * (1.f - u * u); }
        }
    }
    float minx = 9, maxx = -9, miny = 9, maxy = -9;
    for (auto& p : raw) { minx = std::min(minx, p.x); maxx = std::max(maxx, p.x); miny = std::min(miny, p.y); maxy = std::max(maxy, p.y); }
    float s = 2.0f / std::max(maxx - minx, maxy - miny), cx = (minx + maxx) / 2, cy = (miny + maxy) / 2;
    for (auto& p : raw) out.push_back({(p.x - cx) * s, (p.y - cy) * s});
    return out;
}

const std::vector<Pt>& crescentOutline() {
    static std::vector<Pt> out;
    if (!out.empty()) return out;
    const double ax = 0, ay = 0, ar = 0.78, bx = 0.40, by = -0.30, br = 0.66;
    double d = std::sqrt((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
    double a = (ar * ar - br * br + d * d) / (2 * d), h = std::sqrt(std::max(0.0, ar * ar - a * a));
    double ex = (bx - ax) / d, ey = (by - ay) / d;
    double mx = ax + a * ex, my = ay + a * ey;
    double p1x = mx + h * ey, p1y = my - h * ex, p2x = mx - h * ey, p2y = my + h * ex;
    auto ang = [](double cx, double cy, double x, double y) { return std::atan2(y - cy, x - cx); };
    double a1 = ang(ax, ay, p1x, p1y), a2 = ang(ax, ay, p2x, p2y);
    auto sampleArc = [&](double cx, double cy, double r, double s, double e, int dir, std::vector<Pt>& v) {
        double sweep = e - s;
        if (dir > 0) { while (sweep < 0) sweep += 6.283185307; while (sweep > 6.283185307) sweep -= 6.283185307; }
        else { while (sweep > 0) sweep -= 6.283185307; while (sweep < -6.283185307) sweep += 6.283185307; }
        for (int i = 0; i <= 60; ++i) { double t = s + sweep * i / 60; v.push_back({(float)(cx + r * std::cos(t)), (float)(cy + r * std::sin(t))}); }
    };
    std::vector<Pt> arcA;
    for (int dir = 1; dir >= -1; dir -= 2) {
        std::vector<Pt> t; sampleArc(ax, ay, ar, a1, a2, dir, t);
        Pt m = t[t.size() / 2];
        if ((m.x - bx) * (m.x - bx) + (m.y - by) * (m.y - by) > br * br) { arcA = t; break; }
    }
    double b1 = ang(bx, by, p2x, p2y), b2 = ang(bx, by, p1x, p1y);
    std::vector<Pt> arcB;
    for (int dir = 1; dir >= -1; dir -= 2) {
        std::vector<Pt> t; sampleArc(bx, by, br, b1, b2, dir, t);
        Pt m = t[t.size() / 2];
        if (m.x * m.x + m.y * m.y < ar * ar) { arcB = t; break; }
    }
    std::vector<Pt> raw = arcA; raw.insert(raw.end(), arcB.begin(), arcB.end());
    float minx = 9, maxx = -9, miny = 9, maxy = -9;
    for (auto& p : raw) { minx = std::min(minx, p.x); maxx = std::max(maxx, p.x); miny = std::min(miny, p.y); maxy = std::max(maxy, p.y); }
    float s = 1.7f / std::max(maxx - minx, maxy - miny), cx = (minx + maxx) / 2, cy = (miny + maxy) / 2;
    for (auto& p : raw) out.push_back({(p.x - cx) * s, (p.y - cy) * s});
    return out;
}
} // namespace

void Gfx::weatherIcon(weather::Icon icon, float cx, float cy, float size, const Color& ink, const Color& accent, const Color& plate, float strokeScale) {
    using weather::Icon;
    const float s = size * 0.5f;                 // unit -> pixels
    const float sw = std::max(1.2f, size * 0.062f * strokeScale);
    auto P = [&](float x, float y) { return Pt{cx + x * s, cy + y * s}; };
    // closed Catmull-Rom spline through the points, as cubic Beziers (smooth and cheap: ~60 points instead of 180 line segments)
    auto smoothGeom = [&](const std::vector<Pt>& q) -> ID2D1PathGeometry* {
        const size_t n = q.size();
        if (n < 3) return nullptr;
        ID2D1PathGeometry* g = nullptr; ID2D1GeometrySink* sink = nullptr;
        if (FAILED(d2d_->CreatePathGeometry(&g))) return nullptr;
        if (FAILED(g->Open(&sink))) { g->Release(); return nullptr; }
        sink->BeginFigure(Point2F(q[0].x, q[0].y), D2D1_FIGURE_BEGIN_FILLED);
        for (size_t i = 0; i < n; ++i) {
            const Pt& p0 = q[(i + n - 1) % n]; const Pt& p1 = q[i]; const Pt& p2 = q[(i + 1) % n]; const Pt& p3 = q[(i + 2) % n];
            sink->AddBezier(D2D1::BezierSegment(Point2F(p1.x + (p2.x - p0.x) / 6.f, p1.y + (p2.y - p0.y) / 6.f),
                                                Point2F(p2.x - (p3.x - p1.x) / 6.f, p2.y - (p3.y - p1.y) / 6.f),
                                                Point2F(p2.x, p2.y)));
        }
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close(); sink->Release();
        return g;
    };
    auto cloud = [&](float ox, float oy, float k, const Color& stroke, bool fillPlate) {
        const std::vector<Pt>& o = cloudOutline();
        std::vector<Pt> pts;
        for (size_t i = 0; i < o.size(); i += 3) pts.push_back(P(ox + o[i].x * k, oy + o[i].y * k));
        ID2D1PathGeometry* g = smoothGeom(pts);
        if (!g) return;
        if (fillPlate) { setBrush(plate); rt_->FillGeometry(g, brush_); }
        setBrush(stroke); rt_->DrawGeometry(g, brush_, sw, roundStroke_);
        g->Release();
    };
    auto sun = [&](float ox, float oy, float k, const Color& col) {
        float r = 0.34f * k;
        strokeCircle(cx + ox * s, cy + oy * s, r * s, col, sw);
        for (int i = 0; i < 8; ++i) {
            float a = i * 0.7853982f;
            Pt a0 = P(ox + std::cos(a) * (r + 0.17f * k), oy + std::sin(a) * (r + 0.17f * k));
            Pt a1 = P(ox + std::cos(a) * (r + 0.40f * k), oy + std::sin(a) * (r + 0.40f * k));
            line(a0.x, a0.y, a1.x, a1.y, col, sw);
        }
    };
    auto moon = [&](float ox, float oy, float k, const Color& col, bool fill) {
        std::vector<Pt> pts;
        for (const Pt& p : crescentOutline()) pts.push_back(P(ox + p.x * k, oy + p.y * k));
        if (fill) fillPolygon(pts, withA(col, 0.18f));
        polyline(pts, true, col, sw);
    };
    auto drops = [&](float oy, int n, float len, float slant) {
        for (int i = 0; i < n; ++i) {
            float x = (n == 1 ? 0.f : (i - (n - 1) * 0.5f) * 0.42f);
            Pt a = P(x + slant * 0.5f, oy), b = P(x - slant * 0.5f, oy + len);
            line(a.x, a.y, b.x, b.y, accent, sw);
        }
    };
    switch (icon) {
        case Icon::ClearDay: sun(0, 0, 1.15f, accent); break;
        case Icon::ClearNight: moon(0, 0, 1.0f, ink, true); break;
        case Icon::PartlyDay: sun(-0.34f, -0.34f, 0.8f, accent); cloud(0.14f, 0.18f, 0.78f, ink, true); break;
        case Icon::PartlyNight: moon(-0.30f, -0.30f, 0.62f, ink, true); cloud(0.14f, 0.18f, 0.78f, ink, true); break;
        case Icon::Cloudy: cloud(-0.18f, -0.12f, 0.62f, lerp(plate, ink, 0.55f), false); cloud(0.08f, 0.14f, 0.82f, ink, true); break;
        case Icon::Fog: {
            const float ys[4] = {-0.55f, -0.18f, 0.19f, 0.56f}, xs[4] = {0.75f, 0.95f, 0.65f, 0.85f};
            for (int i = 0; i < 4; ++i) { Pt a = P(-xs[i] * 0.85f + (i % 2 ? 0.1f : -0.1f), ys[i]), b = P(xs[i] * 0.85f + (i % 2 ? 0.1f : -0.1f), ys[i]); line(a.x, a.y, b.x, b.y, i == 1 ? ink : withA(ink, 0.6f), sw); }
            break;
        }
        case Icon::Drizzle: cloud(0, -0.28f, 0.78f, ink, true); drops(0.50f, 3, 0.16f, 0.1f); break;
        case Icon::Rain: cloud(0, -0.30f, 0.80f, ink, true); drops(0.44f, 3, 0.34f, 0.18f); break;
        case Icon::Showers: sun(-0.46f, -0.50f, 0.52f, accent); cloud(0.08f, -0.14f, 0.74f, ink, true); drops(0.58f, 3, 0.30f, 0.18f); break;
        case Icon::Thunder: {
            cloud(0, -0.30f, 0.80f, ink, true);
            std::vector<Pt> bolt = {P(0.10f, 0.34f), P(-0.16f, 0.66f), P(0.00f, 0.66f), P(-0.10f, 0.98f), P(0.22f, 0.58f), P(0.06f, 0.58f), P(0.18f, 0.34f)};
            fillPolygon(bolt, accent);
            break;
        }
        case Icon::Snow: {
            cloud(0, -0.30f, 0.80f, ink, true);
            for (int i = 0; i < 3; ++i) {
                float x = (i - 1) * 0.42f, y = 0.62f, r = 0.11f;
                for (int k = 0; k < 3; ++k) {
                    float a = k * 1.0471976f + 0.2f;
                    Pt p0 = P(x - std::cos(a) * r, y - std::sin(a) * r), p1 = P(x + std::cos(a) * r, y + std::sin(a) * r);
                    line(p0.x, p0.y, p1.x, p1.y, accent, sw * 0.8f);
                }
            }
            break;
        }
    }
}

} // namespace gfx
