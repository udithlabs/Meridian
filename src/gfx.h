// Direct2D / DirectWrite drawing layer for Meridian (Windows).
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d2d1.h>
#include <dwrite_1.h>
#include <wincodec.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <cmath>
#include <cstdint>
#include "weather.h"

namespace gfx {

struct Color {
    float r = 0, g = 0, b = 0, a = 1;
};
inline Color rgb(unsigned hex, float a = 1.f) {
    return {((hex >> 16) & 255) / 255.f, ((hex >> 8) & 255) / 255.f, (hex & 255) / 255.f, a};
}
inline Color lerp(const Color& x, const Color& y, float t) {
    return {x.r + (y.r - x.r) * t, x.g + (y.g - x.g) * t, x.b + (y.b - x.b) * t, x.a + (y.a - x.a) * t};
}
inline Color withA(const Color& c, float a) { return {c.r, c.g, c.b, a}; }
inline Color mulA(const Color& c, float a) { return {c.r, c.g, c.b, c.a * a}; }
inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
inline float smoothstep(float a, float b, float x) { float t = clamp01((x - a) / (b - a)); return t * t * (3 - 2 * t); }

struct Palette {
    Color bgTop, bgBottom;
    Color ink, ink2, ink3;
    Color line;       // hairlines
    Color accent;     // sun / warmth
    Color accent2;    // moon / cool
    Color panel;      // faint surfaces
    Color plate;      // colour used to occlude shapes behind icons
    float starVis = 1;
    float ember = 0;  // 0..1: how far toward the red night palette
    float neon = 0;   // 0..1: neon bloom around the big time digits (the GTA VI theme)
    // How the light of the weather treats this palette (moodPalette). Night and the twilights do not go grey under cloud and fog:
    float nightness = 0;   // 0..1: how much of a night sky this is. Under cloud and fog it turns deeper and darker, blue-black, never grey
    float twilight = 0;    // 0..1: how much of a dusk or dawn sky this is. Under cloud and fog it turns richer, more purple and orange, never grey
    Color twiTop, twiBottom;   // what the top and the bottom of a twilight sky turn into under a full lid of cloud (or fog): the purple and the orange
    // The sky map (the round dome on the Sky face) in this theme. With skyOwn = 1 the theme paints it: skyZen overhead, skyHor at the rim, whatever the
    // Sun is doing (the real night still deepens it and brings the stars out). With skyOwn = 0 only the Sun decides (day blue, the twilights, night), which
    // is what Auto wants: it follows the real sky.
    // skyNZen / skyNHor are the same sky once the real Sun is well down (the stars out): a Sunset sky turns to deep plum with a violet rim, a Sunlit one to
    // a deep blue, so the map never goes muddy half way.
    Color skyZen{0.f, 0.f, 0.f, 1.f}, skyHor{0.f, 0.f, 0.f, 1.f}, skyNZen{0.f, 0.f, 0.f, 1.f}, skyNHor{0.f, 0.f, 0.f, 1.f};
    float skyOwn = 0;
    bool dark = true;
};
extern const Palette PAL_DAY, PAL_SUNLIT, PAL_SUNLIT2, PAL_DAWN, PAL_SUNSET, PAL_DUSK, PAL_NIGHT, PAL_MIDNIGHT, PAL_GALAXY, PAL_EMBER;
Palette lerpPalette(const Palette& a, const Palette& b, float t);
// The light of the day under weather: gloom (0..1) is heavy cloud or storm, haze (0..1) is fog. Darkens, cools and flattens a palette.
Palette moodPalette(const Palette& in, float gloom, float haze);
inline float luma(const Color& c) { return 0.299f * c.r + 0.587f * c.g + 0.114f * c.b; }
inline Color satur(const Color& c, float k) {       // k > 1: more colourful, k < 1: greyer, same brightness
    const float L = luma(c);
    return {clamp01(L + (c.r - L) * k), clamp01(L + (c.g - L) * k), clamp01(L + (c.b - L) * k), c.a};
}
Color emberize(const Color& c, float amount);     // pull a colour toward dim red

enum FontId { F_DISPLAY = 0, F_LIGHT, F_MEDIUM, F_SERIF, F_SERIF_I, F_MONO, F_MONO_M, F_COUNT };
enum class Align { Left, Center, Right };

struct Pt { float x, y; };
struct Stop { float pos; Color c; };

struct Bitmap { ID2D1Bitmap* bmp = nullptr; int w = 0, h = 0; };

class Gfx {
public:
    Gfx() = default;
    ~Gfx();
    bool init();                                   // factories + fonts
    bool createWindowTarget(HWND hwnd, int w, int h);
    bool createOffscreenTarget(int w, int h);
    void resize(int w, int h);
    bool savePng(const wchar_t* path);
    bool occluded() const;                         // the window is covered or the screen is off: drawing a frame would be wasted
    void setDpi(float dpi) { if (rt_) rt_->SetDpi(dpi, dpi); }     // draw in 96-dpi units on a screen of any scale (the setup uses it)
    bool hasTarget() const { return rt_ != nullptr; }   // false after a failed rebuild of a lost render target: nothing may be drawn until it works again
    int width() const { return w_; }
    int height() const { return h_; }

    bool begin();
    bool end();                                    // returns false if the target must be recreated
    long long lastPreEndTicks() const { return preEnd_; }   // QueryPerformanceCounter value taken just before the frame was handed to the screen (EndDraw may wait for the screen's refresh)
    void clear(const Color& c);
    void setOffset(float x, float y);              // translate everything drawn afterwards (burn-in shift)
    void setReveal(float scale, float cx, float cy);   // zoom everything drawn afterwards about (cx,cy): the screen saver's start (1 = none; begin() resets it)
    void vignette(float cx, float cy, float inner, float outer, float w, float h);   // black over the w x h screen, clear inside `inner`, soft edge out to `outer`

    // shapes
    void fillRect(float x, float y, float w, float h, const Color& c);
    void fillRoundRect(float x, float y, float w, float h, float r, const Color& c);
    void strokeRoundRect(float x, float y, float w, float h, float r, const Color& c, float width);
    void vGradientRect(float x, float y, float w, float h, const Color& top, const Color& bottom);
    void fillCircle(float cx, float cy, float r, const Color& c);
    void strokeCircle(float cx, float cy, float r, const Color& c, float width);
    void strokeEllipse(float cx, float cy, float rx, float ry, const Color& c, float width);
    void line(float x1, float y1, float x2, float y2, const Color& c, float width, bool round = true);
    void polyline(const std::vector<Pt>& pts, bool closed, const Color& c, float width, bool round = true);
    void fillPolygon(const std::vector<Pt>& pts, const Color& c);
    void arc(float cx, float cy, float r, float startDeg, float sweepDeg, const Color& c, float width, bool round = true); // 0 deg = up, clockwise
    void radialFill(float cx, float cy, float r, const std::vector<Stop>& stops, float opacity = 1.f);
    void radialFillOffset(float cx, float cy, float r, float ox, float oy, const std::vector<Stop>& stops);
    void radialEllipse(float cx, float cy, float rx, float ry, float ox, float oy, const std::vector<Stop>& stops, float opacity = 1.f);
    void glow(float cx, float cy, float r, const Color& c, float intensity = 1.f);
    void glowEllipse(float cx, float cy, float rx, float ry, const Color& c, float intensity = 1.f);
    void fillCircleVGradient(float cx, float cy, float r, const Color& top, const Color& bottom);   // top of the circle to its bottom
    void fillPolyVGradient(const std::vector<Pt>& pts, float y0, float y1, const Color& top, const Color& bottom);
    void setNeon(const Color& c, float amount) { neonC_ = c; neonA_ = amount; }       // big time digits drawn after this get a neon bloom
    void ringMask(float cx, float cy, float r, float outer, const Color& top, const Color& bottom, float gradY0, float gradY1);
    void pushClip(float x, float y, float w, float h);
    void popClip();
    void pushRoundRectClip(float x, float y, float w, float h, float r);
    void popRoundRectClip();
    void pushCircleClip(float cx, float cy, float r);
    void popCircleClip();
    // draw only in the ring: inside the square of half-size `outer` around (cx,cy) but outside the circle of radius r (used to put a glow back around the dome)
    void pushRingClip(float cx, float cy, float r, float outer);
    void popRingClip();

    // text (own layout over DirectWrite glyph runs: stable tabular digits, tracking, kerning)
    float textWidth(FontId f, float size, const std::wstring& s, float tracking = 0);
    float capHeight(FontId f, float size);
    float xHeight(FontId f, float size);
    void text(FontId f, float size, float x, float baseline, const std::wstring& s, const Color& c, Align al = Align::Left, float tracking = 0);
    void textGradient(FontId f, float size, float x, float baseline, const std::wstring& s, const Color& top, const Color& bottom, float gradTop, float gradBottom, Align al = Align::Left, float tracking = 0);
    // fixed-pitch digits: every digit in a cell as wide as the widest digit
    float digitCell(FontId f, float size);
    float timeWidth(FontId f, float size, const std::wstring& s, float colonScale = 0.42f, float track = 0.f);   // track: extra space after every character, in em
    void timeText(FontId f, float size, float x, float baseline, const std::wstring& s, const Color& top, const Color& bottom, Align al = Align::Left, float colonScale = 0.42f, float track = 0.f);
    std::wstring ellipsize(FontId f, float size, const std::wstring& s, float maxWidth, float tracking = 0);
    std::vector<std::wstring> wrap(FontId f, float size, const std::wstring& s, float maxWidth, float tracking = 0, int maxLines = 99);

    // bitmaps
    Bitmap makeBitmap(const uint8_t* bgraPremul, int w, int h);
    void drawBitmap(const Bitmap& b, float cx, float cy, float size, float rotationDeg, float opacity = 1.f);
    void drawBitmapRect(const Bitmap& b, float x, float y, float w, float h, float opacity = 1.f);
    // draws a rectangle (sx,sy,sw,sh, in the bitmap's own pixels) of a bitmap at (dx,dy) at its own size. On whole pixels this copies pixel for pixel, so thin lines stay crisp.
    void drawBitmapSub(const Bitmap& b, float dx, float dy, float sx, float sy, float sw, float sh, float opacity = 1.f);
    void drawBitmapRot(const Bitmap& b, float x, float y, float w, float h, float pivotX, float pivotY, float rotDeg, float opacity = 1.f, bool flipX = false);
    // PNG, JPEG, BMP or GIF from disk; an empty Bitmap when it cannot be read. When maxPixelBytes is not 0, a picture whose decoded size
    // (width x height x 4) is larger is refused before any pixels are allocated, and *tooLarge (if given) says that was the reason.
    Bitmap loadImage(const std::wstring& path, size_t maxPixelBytes = 0, bool* tooLarge = nullptr);
    // theme packs can bring their own fonts: use this font file for a role instead of the built-in one (the data must stay alive)
    bool setFontOverride(FontId id, const uint8_t* data, size_t size);
    void clearFontOverride(FontId id);
    // draw soft shapes once into a bitmap: beginSprite, use the normal calls (origin at the top left of the sprite), endSprite
    bool beginSprite(int w, int h);
    Bitmap endSprite();    void freeBitmap(Bitmap& b);

    // weather icons (line style)
    void weatherIcon(weather::Icon icon, float cx, float cy, float size, const Color& ink, const Color& accent, const Color& plate, float strokeScale = 1.f);

    bool wineCompat() const { return wine_; }
    void invalidateDeviceResources();
    void shutdown();

private:
    struct Glyph { UINT16 index; float adv; };
    struct Face {
        IDWriteFontFace* face = nullptr;
        IDWriteFontFace1* face1 = nullptr;
        float upem = 1000, cap = 0.7f, xh = 0.5f;
        std::unordered_map<uint32_t, Glyph> cache;
        size_t blobKey = (size_t)-1;                // index of the font data in the loader (theme fonts)
    };
    bool loadFont(FontId id, int resId);
    bool loadFontData(Face& f, const uint8_t* data, size_t size);
    Face& fc(FontId id) { return useOver_[id] ? over_[id] : faces_[id]; }       // the font in use for a role: the theme's, else the built-in
    const Glyph& glyph(Face& f, uint32_t cp);
    void shape(FontId id, float size, const std::wstring& s, float tracking, std::vector<UINT16>& idx, std::vector<float>& adv, float* total);
    ID2D1GradientStopCollection* stops(const std::vector<Stop>& s);
    void setBrush(const Color& c);
    void drawRun(Face& f, float size, float x, float baseline, std::vector<UINT16>& idx, std::vector<float>& adv, ID2D1Brush* brush);
    ID2D1PathGeometry* polyGeom(const std::vector<Pt>& pts, bool closed, bool filled);
    ID2D1PathGeometry* ringGeometry(float cx, float cy, float r, float outer);
    ID2D1Layer* ringLayer_ = nullptr; ID2D1PathGeometry* ringGeom_ = nullptr;
    ID2D1Layer* circleLayer_ = nullptr; ID2D1EllipseGeometry* circleGeom_ = nullptr;
    float circleCx_ = -1e9f, circleCy_ = -1e9f, circleR_ = -1.f;
    struct RRSlot {
        ID2D1Layer* layer = nullptr;
        ID2D1RoundedRectangleGeometry* geom = nullptr;
        float x = -1e9f, y = -1e9f, w = -1.f, h = -1.f, r = -1.f;
    };
    RRSlot rrPool_[4];
    bool rrPushed_[4] = {false, false, false, false};
    int rrDepth_ = 0;

    ID2D1Factory* d2d_ = nullptr;
    IDWriteFactory* dw_ = nullptr;
    IWICImagingFactory* wic_ = nullptr;
    ID2D1RenderTarget* rt_ = nullptr;
    ID2D1HwndRenderTarget* hrt_ = nullptr;
    IWICBitmap* wicBmp_ = nullptr;
    ID2D1SolidColorBrush* brush_ = nullptr;
    ID2D1StrokeStyle* roundStroke_ = nullptr;
    Face faces_[F_COUNT];
    Face over_[F_COUNT];                            // fonts brought by the active theme pack
    bool useOver_[F_COUNT] = {false, false, false, false, false, false, false};
    int w_ = 0, h_ = 0;
    bool wine_ = false;
    float ox_ = 0, oy_ = 0;
    float rsc_ = 1.f, rcx_ = 0, rcy_ = 0, savedRsc_ = 1.f;
    long long preEnd_ = 0;
    ID2D1BitmapRenderTarget* spriteRt_ = nullptr; ID2D1RenderTarget* savedRt_ = nullptr; float savedOx_ = 0, savedOy_ = 0; int spriteW_ = 0, spriteH_ = 0;
    Color neonC_ = {1, 0, 0.5f, 1}; float neonA_ = 0;
    std::unordered_map<uint32_t, ID2D1RadialGradientBrush*> glowCache_;     // one brush per colour: glows only move and fade it
    std::vector<UINT16> scratchIdx_;
    std::vector<float> scratchAdv_;
    std::vector<INT32> scratchKern_;
    void releaseTarget();
    void clearGlowCache();
};

} // namespace gfx
