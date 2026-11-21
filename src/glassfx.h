// Meridian: Liquid Glass tactile button & card physics engine.
// Pure arithmetic (no Direct2D / Win32), shared by scene_* and unit/stress tests.
//
// Features:
//   - Silky exponential hover bloom (0..1) + cursor-tracking specular glass sheen
//   - Tactile press-and-hold squish + volume-preserving liquid-jelly elastic rebound on release/click
//   - Radiating translucent glass caustic ripples anchored to exact relative click coordinates
//   - Smooth horizontal sliding liquid-glass active pill for segmented button rows
//   - Guaranteed finite settle (< 0.65 s) + automatic pruning so idle CPU/GPU overhead is strictly zero.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace glassfx {

inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
inline float mixf(float a, float b, float t) { return a + (b - a) * t; }
inline float smoothstep(float a, float b, float x) {
    const float t = clamp01((x - a) / (b - a));
    return t * t * (3.f - 2.f * t);
}

constexpr float kBounceDur = 0.52f;
constexpr size_t kMaxRipples = 8;

enum SlideSlot {
    SLIDE_TIMER_MODE = 0,
    SLIDE_TIMER_PRESET,
    SLIDE_FOCUS_PRESET,
    SLIDE_DOCK_FACE,
    SLIDE_COUNT
};

struct Pose {
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f, r = 0.f;  // deformed liquid-glass rect & corner radius
    float hoverT = 0.f;                                  // smooth 0..1 hover
    float holdT = 0.f;                                   // smooth 0..1 press-down compression
    float sheenA = 0.f;                                  // specular glass sheen intensity 0..1
    float rimBoost = 0.f;                                // Fresnel rim brightness boost 0..1
    float bounceEnergy = 0.f;                            // 0..1 instantaneous jelly motion intensity
    bool  hasRipple = false;                             // true if >= 1 click ripple is active on this control
};

struct RippleDraw {
    float cx = 0.f, cy = 0.f;        // pixel center inside control
    float radius = 0.f;              // primary expanding caustic radius
    float alpha = 0.f;               // 0..1 opacity envelope
    float ringWidth = 1.5f;          // crisp refractive rim stroke width
    float secondaryRadius = 0.f;     // trailing harmonic wave (for tactile cards)
    float secondaryAlpha = 0.f;      // trailing harmonic opacity
    float strength = 1.f;
};

struct SlidePose {
    float x = 0.f, w = 0.f, alpha = 0.f;
    bool  visible = false;
};

struct TogglePose {
    float pos = 0.f;                 // 0 (off) .. 1 (on), smoothly eased
    float stretch = 0.f;             // 0..1 liquid droplet elongation while gliding
};

class Engine {
public:
    void beginFrame(float dt, bool saverBlocked) {
        dt_ = std::isfinite(dt) ? std::max(0.f, std::min(0.25f, dt)) : 0.f;
        blocked_ = saverBlocked;
        if (blocked_) {
            clearAll();
            return;
        }
        // Advance active click ripples & remove completed ones
        size_t dst = 0;
        for (size_t i = 0; i < ripples_.size(); ++i) {
            Ripple& rp = ripples_[i];
            rp.age += dt_;
            if (rp.age < rp.dur && std::isfinite(rp.age)) {
                if (dst != i) ripples_[dst] = rp;
                ++dst;
            }
        }
        ripples_.resize(dst);

        // Advance bounce timers & clear per-frame seen markers
        for (auto& kv : items_) {
            Item& it = kv.second;
            it.seen = false;
            if (it.bounceAge < kBounceDur) {
                it.bounceAge += dt_;
                if (it.bounceAge >= kBounceDur) it.bounceAge = kBounceDur + 1.f;
            }
        }
        for (auto& kv : toggles_) kv.second.seen = false;
    }

    // Called on WM_LBUTTONDOWN (holding=true) or synthetic/keyboard click (holding=false).
    // Fires a liquid caustic ripple anchored to (clickX, clickY) inside (rx, ry, rw, rh).
    void onPress(uint64_t id, float clickX, float clickY, float rx, float ry, float rw, float rh, float strength = 1.f, bool holding = true) {
        if (blocked_ || id == 0) return;
        const float str = std::isfinite(strength) ? std::max(0.5f, std::min(2.0f, strength)) : 1.f;
        Item& it = items_[id];
        it.bounceAmp = str;
        if (holding) {
            heldId_ = id;
            it.holdTarget = 1.f;
            // Subtle immediate contact pulse so even a 16ms tap visibly reacts on frame 1
            it.bounceAge = 0.f;
            it.tapDip = true;
            it.bounceScale = 0.55f;
        } else {
            heldId_ = 0;
            it.holdTarget = 0.f;
            it.bounceAge = 0.f;
            it.tapDip = true;
            it.bounceScale = 1.0f;
        }

        // Compute normalized click origin inside the control so ripples stay locked even on resize/shift
        float ox = 0.5f, oy = 0.5f;
        if (std::isfinite(clickX) && std::isfinite(clickY) && rw > 1.f && rh > 1.f) {
            ox = std::max(0.05f, std::min(0.95f, (clickX - rx) / rw));
            oy = std::max(0.05f, std::min(0.95f, (clickY - ry) / rh));
        }
        Ripple rp{};
        rp.id = id;
        rp.ox = ox;
        rp.oy = oy;
        rp.age = 0.f;
        rp.dur = str > 1.15f ? 0.62f : 0.46f;
        rp.strength = str;
        if (ripples_.size() >= kMaxRipples) ripples_.erase(ripples_.begin());
        ripples_.push_back(rp);
        busy_ = true;
    }

    // Called on WM_LBUTTONUP: releases held button and launches outward liquid-jelly rebound spring.
    void onRelease() {
        if (heldId_ == 0) return;
        auto it = items_.find(heldId_);
        if (it != items_.end()) {
            it->second.holdTarget = 0.f;
            it->second.bounceAge = 0.f;
            it->second.tapDip = false;
            it->second.bounceScale = 1.0f;
        }
        heldId_ = 0;
        busy_ = true;
    }

    // Called on WM_MOUSELEAVE, WM_CAPTURECHANGED, Alt+Tab, or modal switches so nothing stays stuck pressed.
    void cancelHold() {
        if (heldId_ != 0) {
            auto it = items_.find(heldId_);
            if (it != items_.end()) it->second.holdTarget = 0.f;
            heldId_ = 0;
        }
    }

    // Resets non-dock sliding pill animations when switching faces so pills never fly from another face.
    void resetFaceSlides() {
        for (int i = 0; i < SLIDE_COUNT; ++i) {
            if (i != SLIDE_DOCK_FACE) slides_[i] = SlideGroup{};
        }
    }

    void clearAll() {
        items_.clear();
        toggles_.clear();
        ripples_.clear();
        heldId_ = 0;
        for (int i = 0; i < SLIDE_COUNT; ++i) slides_[i] = SlideGroup{};
        busy_ = false;
    }

    // Evaluates liquid-glass deformation & shading parameters for one control this frame.
    Pose pose(uint64_t id, float x, float y, float w, float h, float r, float s, bool hovered, bool cardStyle = false) {
        Pose p{};
        const float W = (std::isfinite(w) && w > 1.f) ? w : 1.f;
        const float H = (std::isfinite(h) && h > 1.f) ? h : 1.f;
        const float S = (std::isfinite(s) && s > 0.1f) ? s : 0.1f;
        const float R = std::max(0.f, std::min(std::isfinite(r) ? r : H * 0.5f, std::min(W, H) * 0.5f));
        p.x = x; p.y = y; p.w = W; p.h = H; p.r = R;
        if (blocked_ || id == 0) return p;

        // Avoid creating map entries for totally idle unhovered controls
        auto found = items_.find(id);
        bool hasRip = hasRippleFor(id);
        if (found == items_.end() && !hovered && id != heldId_ && !hasRip) {
            return p;
        }

        Item& it = (found != items_.end()) ? found->second : items_[id];
        it.seen = true;
        it.hoverTarget = hovered ? 1.f : 0.f;
        it.holdTarget = (id == heldId_) ? 1.f : 0.f;
        if (cardStyle && it.bounceAmp < 1.25f) it.bounceAmp = 1.35f;

        // Smooth frame-rate-independent hover & press easing
        const float kH = 1.f - std::exp(-dt_ * (it.hoverTarget > it.hoverT ? 18.f : 9.5f));
        it.hoverT = clamp01(it.hoverT + (it.hoverTarget - it.hoverT) * kH);
        if (std::fabs(it.hoverT - it.hoverTarget) < 0.004f) it.hoverT = it.hoverTarget;

        const float kP = 1.f - std::exp(-dt_ * (it.holdTarget > it.holdT ? 28.f : 18.f));
        it.holdT = clamp01(it.holdT + (it.holdTarget - it.holdT) * kP);
        if (std::fabs(it.holdT - it.holdTarget) < 0.004f) it.holdT = it.holdTarget;

        const float he = smoothstep(0.f, 1.f, it.hoverT);
        const float pe = smoothstep(0.f, 1.f, it.holdT);

        // Elastic liquid-jelly rebound & volume-preserving squash/stretch
        float jellyX = 0.f, jellyY = 0.f, energy = 0.f;
        if (it.bounceAge < kBounceDur) {
            const float age = it.bounceAge;
            const float env = std::exp(-age * 8.2f) * (1.f - smoothstep(0.36f, kBounceDur, age));
            const float primary = it.tapDip ? -std::sin(age * 24.5f) : std::sin(age * 19.5f);
            const float harm = std::sin(age * 27.0f);
            const float amp = it.bounceAmp * it.bounceScale;
            if (cardStyle) {
                // Juicy volume-preserving liquid-glass jelly wobble for (2) touch cards
                jellyX = env * (primary * 0.036f + harm * 0.014f) * amp;
                jellyY = env * (primary * 0.040f - harm * 0.015f) * amp;
            } else {
                jellyX = env * (primary * 0.026f + harm * 0.006f) * amp;
                jellyY = env * (primary * 0.030f - harm * 0.006f) * amp;
            }
            energy = clamp01(env * amp * 1.4f);
        }

        // Hover swell + tactile press squish + jelly rebound, safely bounded so neighbor buttons never overlap
        const float maxExpand = (cardStyle ? 7.0f : 2.2f) * S;
        const float maxShrink = (cardStyle ? 8.0f : 3.2f) * S;
        const float hoverPxX = std::min(cardStyle ? 2.8f * S : 1.5f * S, std::max(0.75f * S, H * 0.030f)) * he;
        const float hoverPxY = std::min(cardStyle ? 2.4f * S : 1.3f * S, std::max(0.65f * S, H * 0.028f)) * he;
        const float pressPxX = -std::min(cardStyle ? 5.2f * S : 2.4f * S, W * (cardStyle ? 0.022f : 0.020f)) * pe;
        const float pressPxY = -std::min(cardStyle ? 4.4f * S : 2.0f * S, H * (cardStyle ? 0.048f : 0.042f)) * pe;

        const float dx = std::max(-maxShrink, std::min(maxExpand, hoverPxX + pressPxX + jellyX * W));
        const float dy = std::max(-maxShrink, std::min(maxExpand, hoverPxY + pressPxY + jellyY * H));

        p.w = std::max(8.f, W + 2.f * dx);
        p.h = std::max(8.f, H + 2.f * dy);
        p.x = x + (W - p.w) * 0.5f;
        p.y = y + (H - p.h) * 0.5f;
        p.r = std::max(0.f, std::min(R * (p.h / H), std::min(p.w, p.h) * 0.5f));
        p.hoverT = it.hoverT;
        p.holdT = it.holdT;
        p.sheenA = clamp01(he + 0.35f * pe + 0.45f * energy);
        p.rimBoost = clamp01(he * 0.72f + pe * 0.45f + energy * 0.85f);
        p.bounceEnergy = energy;
        p.hasRipple = hasRip;
        return p;
    }

    // Collects active expanding caustic ripples for one control, ready to render inside pushRoundRectClip.
    void collectRipples(uint64_t id, float rx, float ry, float rw, float rh, float s, std::vector<RippleDraw>& out) const {
        out.clear();
        if (blocked_ || id == 0 || ripples_.empty() || rw <= 1.f || rh <= 1.f) return;
        const float S = std::max(0.1f, s);
        for (const Ripple& rp : ripples_) {
            if (rp.id != id) continue;
            const float u = clamp01(rp.age / std::max(0.05f, rp.dur));
            const float inv = 1.f - u;
            const float ease = 1.f - inv * inv * inv;
            const float cx = rx + rp.ox * rw;
            const float cy = ry + rp.oy * rh;
            const float farX = std::max(cx - rx, (rx + rw) - cx);
            const float farY = std::max(cy - ry, (ry + rh) - cy);
            const float maxR = std::hypot(farX, farY) * 1.15f;
            RippleDraw d{};
            d.cx = cx;
            d.cy = cy;
            d.radius = std::max(3.f * S, ease * maxR);
            d.alpha = (1.f - smoothstep(0.12f, 1.f, u)) * rp.strength;
            d.ringWidth = std::max(1.1f, (rp.strength > 1.15f ? 2.4f : 1.85f) * S * (1.f - 0.45f * u));
            if (rp.strength > 1.15f && u > 0.08f) {
                const float u2 = clamp01((u - 0.08f) / 0.92f);
                const float inv2 = 1.f - u2;
                d.secondaryRadius = std::max(2.f * S, (1.f - inv2 * inv2 * inv2) * maxR * 0.72f);
                d.secondaryAlpha = (1.f - smoothstep(0.15f, 1.f, u2)) * 0.55f * rp.strength;
            }
            d.strength = rp.strength;
            if (d.alpha > 0.008f) out.push_back(d);
        }
    }

    // Smoothly glides a luminous active pill across a horizontal segmented row.
    SlidePose stepSlide(int slot, bool hasActive, float targetX, float targetW, float winW, float winH) {
        SlidePose sp{};
        if (slot < 0 || slot >= SLIDE_COUNT || blocked_) return sp;
        SlideGroup& g = slides_[slot];
        const bool resized = !g.init || std::fabs(winW - g.lastW) > 1.f || std::fabs(winH - g.lastH) > 1.f;
        g.lastW = winW; g.lastH = winH;

        if (resized) {
            g.init = true;
            g.x = targetX;
            g.w = targetW;
            g.alpha = hasActive ? 1.f : 0.f;
            g.busy = false;
        } else {
            const float targetA = hasActive ? 1.f : 0.f;
            if (hasActive && g.alpha < 0.04f) {
                // Appearing from no-selection state (e.g. custom typed duration -> preset): bloom cleanly in place
                g.x = targetX;
                g.w = targetW;
            } else if (hasActive) {
                const float kPos = 1.f - std::exp(-dt_ * 16.f);
                g.x += (targetX - g.x) * kPos;
                g.w += (targetW - g.w) * kPos;
                if (std::fabs(g.x - targetX) < 0.35f) g.x = targetX;
                if (std::fabs(g.w - targetW) < 0.35f) g.w = targetW;
            }
            const float kA = 1.f - std::exp(-dt_ * 14.f);
            g.alpha = clamp01(g.alpha + (targetA - g.alpha) * kA);
            if (std::fabs(g.alpha - targetA) < 0.004f) g.alpha = targetA;
            g.busy = (hasActive && (std::fabs(g.x - targetX) > 0.35f || std::fabs(g.w - targetW) > 0.35f)) ||
                     std::fabs(g.alpha - targetA) > 0.004f;
        }
        sp.x = g.x;
        sp.w = g.w;
        sp.alpha = smoothstep(0.f, 1.f, g.alpha);
        sp.visible = sp.alpha > 0.01f && sp.w > 1.f;
        return sp;
    }

    // Smoothly glides and liquid-stretches a Settings toggle switch knob (snaps cleanly on first frame, prunes on tab leave).
    TogglePose stepToggle(uint64_t id, bool on) {
        const float target = on ? 1.f : 0.f;
        if (blocked_ || id == 0) return {target, 0.f};
        auto it = toggles_.find(id);
        if (it == toggles_.end()) {
            ToggleState ts{}; ts.pos = target; ts.target = target; ts.seen = true;
            toggles_[id] = ts;
            return {target, 0.f};
        }
        ToggleState& ts = it->second;
        ts.seen = true;
        ts.target = target;
        const float k = 1.f - std::exp(-dt_ * 18.f);
        ts.pos = clamp01(ts.pos + (target - ts.pos) * k);
        if (std::fabs(ts.pos - target) < 0.004f) ts.pos = target;
        const float moving = std::fabs(ts.pos - target) > 0.005f ? 1.f : 0.f;
        const float stretch = moving * clamp01(4.f * ts.pos * (1.f - ts.pos));
        return {ts.pos, stretch};
    }

    // Must be called once at the end of App::render to prune unseen idle controls and compute busy().
    void endFrame() {
        if (blocked_) { busy_ = false; return; }
        bool anyBusy = !ripples_.empty();
        for (auto it = items_.begin(); it != items_.end(); ) {
            Item& m = it->second;
            if (!m.seen) {
                m.hoverTarget = 0.f;
                if (it->first != heldId_) m.holdTarget = 0.f;
                const float k = 1.f - std::exp(-dt_ * 14.f);
                m.hoverT = clamp01(m.hoverT * (1.f - k));
                m.holdT = clamp01(m.holdT * (1.f - k));
                if (m.hoverT < 0.004f) m.hoverT = 0.f;
                if (m.holdT < 0.004f) m.holdT = 0.f;
            }
            const bool hovMove = std::fabs(m.hoverT - m.hoverTarget) > 0.004f;
            const bool holdMove = std::fabs(m.holdT - m.holdTarget) > 0.004f;
            const bool bncMove = m.bounceAge < kBounceDur;
            if (hovMove || holdMove || bncMove) anyBusy = true;

            const bool idleDead = m.hoverT <= 0.f && m.holdT <= 0.f && !bncMove && it->first != heldId_ && !hasRippleFor(it->first);
            if (idleDead) it = items_.erase(it);
            else ++it;
        }
        for (auto it = toggles_.begin(); it != toggles_.end(); ) {
            if (!it->second.seen) it = toggles_.erase(it);
            else {
                if (std::fabs(it->second.pos - it->second.target) > 0.004f) anyBusy = true;
                ++it;
            }
        }
        for (int i = 0; i < SLIDE_COUNT; ++i) {
            if (slides_[i].busy) anyBusy = true;
        }
        busy_ = anyBusy;
    }

    bool busy() const { return busy_; }
    size_t itemCount() const { return items_.size(); }
    size_t toggleCount() const { return toggles_.size(); }
    size_t rippleCount() const { return ripples_.size(); }
    uint64_t heldId() const { return heldId_; }

private:
    struct Item {
        float hoverT = 0.f, hoverTarget = 0.f;
        float holdT = 0.f, holdTarget = 0.f;
        float bounceAge = kBounceDur + 1.f;
        float bounceAmp = 1.f;
        float bounceScale = 1.f;
        bool  tapDip = false;
        bool  seen = false;
    };
    struct ToggleState {
        float pos = 0.f, target = 0.f;
        bool  seen = false;
    };
    struct Ripple {
        uint64_t id = 0;
        float ox = 0.5f, oy = 0.5f;
        float age = 0.f, dur = 0.48f, strength = 1.f;
    };
    struct SlideGroup {
        float x = 0.f, w = 0.f, alpha = 0.f;
        float lastW = 0.f, lastH = 0.f;
        bool  init = false, busy = false;
    };

    bool hasRippleFor(uint64_t id) const {
        for (const auto& rp : ripples_) if (rp.id == id) return true;
        return false;
    }

    std::unordered_map<uint64_t, Item> items_;
    std::unordered_map<uint64_t, ToggleState> toggles_;
    std::vector<Ripple> ripples_;
    SlideGroup slides_[SLIDE_COUNT];
    uint64_t heldId_ = 0;
    float dt_ = 0.016f;
    bool blocked_ = false;
    bool busy_ = false;
};

} // namespace glassfx
