// Meridian: the graceful start of the screen saver. Pure arithmetic, no drawing (the drawing is in scene.cpp, the test in tests/reveal_test.cpp).
//
// When Windows starts Meridian as the screen saver the screen used to jump from your desktop to the finished clock. Now it starts black and
// Meridian comes up out of the dark: the whole picture eases in, a soft circle of light opens from the middle out to the corners, and the clock
// settles from slightly zoomed in to its true size. It is over in a little more than two seconds. Starting Meridian.exe by hand does none of this.
// Leaving is the same picture played backwards, but quicker (0.6 seconds): the moment the keyboard or mouse is used the dark corners start to close in,
// the picture sinks back a little and fades to black, and only then does the window go. (Before, it simply vanished, which felt like a stutter.)
#pragma once
#include <algorithm>
#include <cmath>

namespace reveal {

constexpr double kSeconds = 2.2;          // from the first black frame to the finished picture
constexpr double kLeaveSeconds = 0.6;     // from the first use of the keyboard or mouse to the last black frame

struct Look {
    bool  done;      // the reveal is over: nothing to zoom, nothing to cover
    float scale;     // zoom of the whole picture about the middle of the screen: a little large at first, exactly 1 at the end
    float dim;       // black laid over the whole screen, 1 (all black) down to 0
    float inner;     // the circle of light: fully clear out to this radius (pixels from the middle; may be negative at the start)
    float outer;     // ... and fully black beyond this one, with a soft edge between the two
};

inline float clamp01(float x) { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); }
inline float smoothstep(float a, float b, float x) { const float t = clamp01((x - a) / (b - a)); return t * t * (3.f - 2.f * t); }
inline float smootherstep(float x) { const float t = clamp01(x); return t * t * t * (t * (t * 6.f - 15.f) + 10.f); }
inline float easeOut(float x) { const float t = 1.f - clamp01(x); return 1.f - t * t * t; }

// p: how far along the reveal is, 0 to 1. W, H: the screen in pixels.
inline Look at(double p, float W, float H) {
    Look k{};
    const float q = clamp01((float)p);
    if (!(p < 1.0)) { k.done = true; k.scale = 1.f; k.dim = 0.f; k.inner = k.outer = 0.5f * std::sqrt(W * W + H * H) * 2.f; return k; }
    k.done = false;
    const float reach = 0.5f * std::sqrt(W * W + H * H);            // middle of the screen to a corner
    const float feather = 0.30f * reach;
    const float e = smootherstep(q);
    k.scale = 1.f + 0.045f * (1.f - easeOut(q));                    // 4.5% large, settling gently
    k.dim = 1.f - smoothstep(0.03f, 0.55f, q);                      // the whole picture comes up first ...
    k.outer = e * (reach + feather);                                // ... and the dark corners open last
    k.inner = k.outer - feather;
    return k;
}

// Leaving: q is how far along it is, 0 (the picture as it was) to 1 (all black, time to go). The corners start closing at once (no slow start: the
// screen answers the moment you touch the mouse), then the picture fades out. Never zooms more than the start of the reveal does.
inline Look leaving(double q0, float W, float H) {
    Look k{};
    const float q = clamp01((float)q0);
    const float reach = 0.5f * std::sqrt(W * W + H * H);
    const float feather = 0.30f * reach;
    k.done = false;
    const float e = 1.f - (1.f - q) * (1.f - q);                 // quick at first, then easing out: the corners start moving at once and arrive softly
    k.scale = 1.f + 0.03f * e;
    k.dim = smoothstep(0.30f, 1.00f, q);
    k.outer = (1.f - e) * (reach + feather);
    k.inner = k.outer - feather;
    return k;
}

} // namespace reveal
