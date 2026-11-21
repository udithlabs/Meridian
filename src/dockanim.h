// Meridian: fluid pill-to-dock morph layout, hover hysteresis, and hit-gating.
// Pure arithmetic (no Direct2D / Win32), shared by scene.cpp and unit/stress tests.
#pragma once
#include <algorithm>
#include <cmath>

namespace dockanim {

inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
inline float mixf(float a, float b, float t) { return a + (b - a) * t; }
inline float smoothstep(float a, float b, float x) {
    const float t = clamp01((x - a) / (b - a));
    return t * t * (3.f - 2.f * t);
}

struct Input {
    float W = 1920.f;          // client width (px)
    float H = 1080.f;          // client height (px)
    float s = 1.f;             // layoutScale
    float expW = 900.f;        // desired full expanded dock capsule width (px)
    float expH = 58.f;         // desired full expanded dock capsule height (px)
    float mouseX = -1.f;       // pointer X in client coords (< 0 when outside window)
    float mouseY = -1.f;       // pointer Y in client coords (< 0 when outside window)
    float idleSec = 0.f;       // seconds since lastMouseMove
    float dt = 0.016f;         // frame delta seconds
    bool blocked = false;      // showSettings || showHelp || isSaver
    float overrideT = -1.f;    // >= 0 pins morph progress for dev screenshots
};

struct State {
    float t = 0.f;             // current morph progress: 0 (resting pill) .. 1 (expanded dock)
    float target = 0.f;        // current target: 0 or 1
};

struct Frame {
    // Resting pill geometry
    float pillX = 0.f, pillY = 0.f, pillW = 0.f, pillH = 0.f;
    // Expanded dock geometry
    float expX = 0.f, expY = 0.f, expW = 0.f, expH = 0.f;
    // Current morphed capsule geometry (always centered horizontally, always true capsule r = curH * 0.5)
    float curX = 0.f, curY = 0.f, curW = 0.f, curH = 0.f, curR = 0.f;
    float e = 0.f;             // smoothstep(0, 1, t): container morph progress
    float hint = 0.f;          // resting pill idle visibility 0..1
    float vis = 0.f;           // combined capsule visibility 0..1
    float contentA = 0.f;      // dock buttons fade-in alpha 0..1
    float shadowA = 0.f;       // capsule drop-shadow alpha factor 0..1
    float rimA = 0.f;          // capsule border alpha factor 0..1
    float pillGlowA = 0.f;     // subtle accent wake-up glow while hovering pill during early morph
    bool inPill = false;       // pointer inside forgiving resting pill hover zone
    bool inExp = false;        // pointer inside expanded dock hysteresis zone
    bool visible = false;      // whether anything should be drawn this frame
    bool canHit = false;       // whether dock buttons may register click hitboxes
    bool swallowClick = false; // whether dock capsule absorbs clicks to block accidental dblclick fullscreen
    bool busy = false;         // whether animation is still moving toward target
};

inline Frame step(const Input& in, State& st) {
    Frame f{};
    const float W = (std::isfinite(in.W) && in.W > 1.f) ? in.W : 1.f;
    const float H = (std::isfinite(in.H) && in.H > 1.f) ? in.H : 1.f;
    const float s = (std::isfinite(in.s) && in.s > 0.1f) ? in.s : 0.1f;
    const float dt = std::isfinite(in.dt) ? std::max(0.f, std::min(1.f, in.dt)) : 0.f;
    const float idleSec = std::isfinite(in.idleSec) ? std::max(0.f, in.idleSec) : 1e6f;
    if (!std::isfinite(st.t)) st.t = 0.f;
    if (!std::isfinite(st.target)) st.target = 0.f;
    st.t = clamp01(st.t);

    const float maxW = std::max(40.f, W - 16.f * s);
    const float maxH = std::max(20.f, H * 0.28f);
    const float wantW = std::isfinite(in.expW) ? in.expW : maxW;
    const float wantH = std::isfinite(in.expH) ? in.expH : 58.f * s;
    f.expW = std::min(std::max(std::min(120.f * s, maxW), wantW), maxW);
    f.expH = std::min(std::max(std::min(24.f * s, maxH), wantH), maxH);
    f.expX = (W - f.expW) * 0.5f;
    f.expY = std::max(4.f * s, H - 28.f * s - f.expH);

    f.pillW = std::min(76.f * s, f.expW * 0.48f);
    f.pillH = std::min(std::max(4.f, 4.5f * s), f.expH * 0.35f);
    f.pillX = (W - f.pillW) * 0.5f;
    f.pillY = std::max(0.f, std::min(H - f.pillH - 2.f, H - 20.f * s));

    const bool ptrInWin = std::isfinite(in.mouseX) && std::isfinite(in.mouseY) &&
                          in.mouseX >= 0.f && in.mouseX <= W &&
                          in.mouseY >= 0.f && in.mouseY <= H;

    // Forgiving Fitts's-Law hover zone over the bottom-center resting pill (reaches to screen bottom edge)
    const float pillPadX = std::max(28.f, 36.f * s);
    const float pillPadTop = std::max(22.f, 26.f * s);
    f.inPill = ptrInWin &&
               in.mouseX >= f.pillX - pillPadX && in.mouseX <= f.pillX + f.pillW + pillPadX &&
               in.mouseY >= f.pillY - pillPadTop && in.mouseY <= H;

    // Expanded hysteresis zone: covers entire expanded dock + generous buffer down to bottom edge,
    // so stationary hover on the pill, fast diagonal dashes to edge buttons, or THEME width shifts never flicker shut.
    const float expPadX = std::max(24.f, 36.f * s);
    const float expPadTop = std::max(20.f, 26.f * s);
    f.inExp = ptrInWin &&
              in.mouseX >= f.expX - expPadX && in.mouseX <= f.expX + f.expW + expPadX &&
              in.mouseY >= f.expY - expPadTop && in.mouseY <= H;

    // Currently visible morphing capsule catch zone: allows catching a mid-collapse dock smoothly
    // without accidentally re-triggering from empty far-left/right space once nearly collapsed.
    const float prevE = smoothstep(0.f, 1.f, st.t);
    const float prevW = mixf(f.pillW, f.expW, prevE);
    const float prevX = mixf(f.pillX, f.expX, prevE);
    const float prevY = mixf(f.pillY, f.expY, prevE);
    const float curPadX = std::max(18.f, 22.f * s);
    const float curPadTop = std::max(16.f, 20.f * s);
    const bool inCur = ptrInWin &&
                       in.mouseX >= prevX - curPadX && in.mouseX <= prevX + prevW + curPadX &&
                       in.mouseY >= prevY - curPadTop && in.mouseY <= H;

    if (in.blocked) {
        st.target = 0.f;
        st.t = 0.f;
    } else if (in.overrideT >= 0.f) {
        st.t = clamp01(in.overrideT);
        st.target = st.t;
    } else {
        const bool wasOpening = st.target > 0.5f;
        const bool stayOpen = (wasOpening && f.inExp) || (!wasOpening && st.t > 0.14f && inCur);
        st.target = (f.inPill || stayOpen) ? 1.f : 0.f;
        // Exact same liquid exponential curve as Focus Studio Orb (15/s open, 7.5/s close)
        const float rate = (st.target > st.t) ? 15.f : 7.5f;
        const float k = 1.f - std::exp(-dt * rate);
        st.t = clamp01(st.t + (st.target - st.t) * k);
        if (std::fabs(st.t - st.target) < 0.004f) st.t = st.target;
    }

    f.e = smoothstep(0.f, 1.f, st.t);
    f.curX = mixf(f.pillX, f.expX, f.e);
    f.curY = mixf(f.pillY, f.expY, f.e);
    f.curW = mixf(f.pillW, f.expW, f.e);
    f.curH = mixf(f.pillH, f.expH, f.e);
    f.curR = f.curH * 0.5f;

    f.hint = in.blocked ? 0.f : (1.f - clamp01((idleSec - 2.4f) / 0.7f));
    if (in.overrideT >= 0.f) f.hint = 1.f;
    f.vis = in.blocked ? 0.f : mixf(f.hint, 1.f, clamp01(f.e * 3.5f));
    f.contentA = smoothstep(0.36f, 0.92f, f.e) * f.vis;
    f.shadowA = smoothstep(0.08f, 0.55f, f.e) * f.vis;
    f.rimA = smoothstep(0.06f, 0.48f, f.e) * f.vis;
    f.pillGlowA = (f.inPill && f.e < 0.45f) ? (1.f - f.e / 0.45f) * f.vis : 0.f;
    f.visible = !in.blocked && (f.vis > 0.01f || f.e > 0.004f);
    f.canHit = !in.blocked && f.e > 0.62f;
    f.swallowClick = !in.blocked && (f.e > 0.30f || f.inPill);
    f.busy = !in.blocked && in.overrideT < 0.f && std::fabs(st.t - st.target) > 0.004f;
    return f;
}

// Button hitboxes activate only once the expanding capsule fully encompasses the button.
inline bool buttonClickable(const Frame& f, float bx, float bw, float s) {
    if (!f.canHit) return false;
    const float inset = std::max(2.f, 4.f * s);
    return bx >= f.curX + inset && (bx + bw) <= (f.curX + f.curW - inset);
}

} // namespace dockanim
