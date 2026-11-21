// Regression & stress tests for Meridian 2.9.8 "Glass":
//   - Liquid Glass button & card spring physics (hover bloom, press squish, elastic jelly rebound)
//   - Bounded caustic click ripples & dual-ring card harmonics
//   - Gliding segmented active pills (snap on first/resize/custom-time, smooth glide, resetFaceSlides)
//   - Zero idle overhead proof (item pruning + busy()==false after settle)
//   - 20,000-frame chaotic fuzz & NaN/Inf resilience test
#include "glassfx.h"
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main() {
    // 1. Zero idle allocation & overhead for unhovered controls
    {
        glassfx::Engine g;
        g.beginFrame(1.f / 60.f, false);
        for (uint64_t id = 1; id <= 30; ++id) {
            auto p = g.pose(id, 100.f, 100.f, 120.f, 40.f, 20.f, 1.f, false, false);
            CHECK(p.x == 100.f && p.y == 100.f && p.w == 120.f && p.h == 40.f && p.r == 20.f);
            CHECK(p.hoverT == 0.f && p.sheenA == 0.f && !p.hasRipple);
        }
        g.endFrame();
        CHECK(g.itemCount() == 0 && g.rippleCount() == 0 && !g.busy());
    }

    // 2. Stationary hover settles cleanly to busy()==false (zero 60fps spin while mouse rests on button)
    {
        glassfx::Engine g;
        float prevH = -1.f;
        for (int i = 0; i < 60; ++i) {
            g.beginFrame(1.f / 60.f, false);
            auto p = g.pose(100, 200.f, 300.f, 140.f, 44.f, 22.f, 1.f, true, false);
            CHECK(p.hoverT >= prevH - 1e-6f);
            CHECK(p.w >= 140.f - 1e-4f && p.w <= 140.f + 4.5f);
            prevH = p.hoverT;
            g.endFrame();
        }
        CHECK(prevH == 1.f && !g.busy());

        // Move away -> decays smoothly back to 0 and prunes map entry completely!
        for (int i = 0; i < 90; ++i) {
            g.beginFrame(1.f / 60.f, false);
            g.pose(100, 200.f, 300.f, 140.f, 44.f, 22.f, 1.f, false, false);
            g.endFrame();
        }
        CHECK(!g.busy() && g.itemCount() == 0);
    }

    // 3. Tactile press-and-hold compression -> release outward liquid-jelly rebound -> clean settle
    {
        glassfx::Engine g;
        g.beginFrame(1.f / 60.f, false);
        g.onPress(370, 500.f, 620.f, 400.f, 600.f, 200.f, 56.f, 1.0f, true);
        CHECK(g.heldId() == 370 && g.rippleCount() == 1 && g.busy());
        glassfx::Pose pHeld{};
        for (int i = 0; i < 12; ++i) {
            g.beginFrame(1.f / 60.f, false);
            pHeld = g.pose(370, 400.f, 600.f, 200.f, 56.f, 28.f, 1.f, true, false);
            g.endFrame();
        }
        // Held down -> compressed smaller than hovered rest size
        CHECK(pHeld.holdT > 0.90f && pHeld.h < 56.f);

        // Release -> rebounds outward past original size, then settles cleanly to rest
        g.onRelease();
        CHECK(g.heldId() == 0);
        float maxW = 0.f;
        for (int i = 0; i < 60; ++i) {
            g.beginFrame(1.f / 60.f, false);
            auto p = g.pose(370, 400.f, 600.f, 200.f, 56.f, 28.f, 1.f, false, false);
            maxW = std::max(maxW, p.w);
            g.endFrame();
        }
        CHECK(maxW > 201.0f);
        for (int i = 0; i < 40; ++i) {
            g.beginFrame(1.f / 60.f, false);
            g.pose(370, 400.f, 600.f, 200.f, 56.f, 28.f, 1.f, false, false);
            g.endFrame();
        }
        CHECK(!g.busy() && g.itemCount() == 0 && g.rippleCount() == 0);
    }

    // 4. Tactile (2) Focus Glass Cards: volume-preserving jelly wobble + dual-ring caustic ripple anchored to exact touch point
    {
        glassfx::Engine g;
        g.beginFrame(1.f / 60.f, false);
        g.onPress(390, 80.f, 52.f, 56.f, 38.f, 240.f, 82.f, 1.40f, false);
        std::vector<glassfx::RippleDraw> rips;
        bool sawSecondary = false, sawSquashStretch = false;
        for (int i = 0; i < 45; ++i) {
            g.beginFrame(1.f / 60.f, false);
            auto p = g.pose(390, 56.f, 38.f, 240.f, 82.f, 20.f, 1.f, false, true);
            const float sx = p.w / 240.f, sy = p.h / 82.f;
            if (std::fabs(sx - sy) > 0.004f) sawSquashStretch = true;
            CHECK(p.w <= 240.f + 14.5f && p.h <= 82.f + 14.5f);
            g.collectRipples(390, p.x, p.y, p.w, p.h, 1.f, rips);
            for (const auto& rd : rips) {
                CHECK(rd.cx >= p.x && rd.cx <= p.x + p.w);
                CHECK(rd.cy >= p.y && rd.cy <= p.y + p.h);
                if (rd.secondaryAlpha > 0.05f && rd.secondaryRadius > 2.f) sawSecondary = true;
            }
            g.endFrame();
        }
        CHECK(sawSecondary && sawSquashStretch);
        // Settles completely cleanly
        for (int i = 0; i < 45; ++i) {
            g.beginFrame(1.f / 60.f, false);
            g.pose(390, 56.f, 38.f, 240.f, 82.f, 20.f, 1.f, false, true);
            g.endFrame();
        }
        CHECK(!g.busy() && g.itemCount() == 0 && g.rippleCount() == 0);
    }

    // 5. Machine-gun clicking (100 clicks) stays strictly bounded to kMaxRipples, cancelHold & saver clear cleanly
    {
        glassfx::Engine g;
        for (int i = 0; i < 100; ++i) {
            g.beginFrame(0.01f, false);
            g.onPress(300 + (uint64_t)(i % 5), 100.f + i, 50.f, 100.f, 40.f, 120.f, 40.f, 1.0f, true);
            CHECK(g.rippleCount() <= glassfx::kMaxRipples);
            g.endFrame();
        }
        g.cancelHold();
        CHECK(g.heldId() == 0);
        // Screen saver mode immediately wipes all state
        g.beginFrame(1.f / 60.f, true);
        g.endFrame();
        CHECK(!g.busy() && g.itemCount() == 0 && g.rippleCount() == 0 && g.heldId() == 0);
    }

    // 6. Gliding segmented active pills (first-frame snap, smooth glide, custom-time fade-out/bloom-in-place, resize snap, resetFaceSlides)
    {
        glassfx::Engine g;
        g.beginFrame(1.f / 60.f, false);
        auto s0 = g.stepSlide(glassfx::SLIDE_FOCUS_PRESET, true, 600.f, 60.f, 1920.f, 1080.f);
        CHECK(s0.visible && s0.x == 600.f && s0.w == 60.f && s0.alpha == 1.f);
        g.endFrame();
        CHECK(!g.busy());

        // Click another preset (900, width 80) -> glides smoothly and monotonically
        float prevX = 600.f;
        for (int i = 0; i < 60; ++i) {
            g.beginFrame(1.f / 60.f, false);
            auto sp = g.stepSlide(glassfx::SLIDE_FOCUS_PRESET, true, 900.f, 80.f, 1920.f, 1080.f);
            CHECK(sp.x >= prevX - 1e-4f && sp.x <= 900.f + 1e-4f);
            prevX = sp.x;
            g.endFrame();
        }
        CHECK(std::fabs(prevX - 900.f) < 0.4f && !g.busy());

        // Custom duration typed (hasActive=false, like "04:00") -> fades smoothly to 0
        for (int i = 0; i < 60; ++i) {
            g.beginFrame(1.f / 60.f, false);
            g.stepSlide(glassfx::SLIDE_FOCUS_PRESET, false, 0.f, 0.f, 1920.f, 1080.f);
            g.endFrame();
        }
        CHECK(!g.busy());
        // Click preset at x=420 -> blooms IN PLACE at x=420 (never flies across from 900!)
        g.beginFrame(1.f / 60.f, false);
        auto sBloom = g.stepSlide(glassfx::SLIDE_FOCUS_PRESET, true, 420.f, 56.f, 1920.f, 1080.f);
        CHECK(sBloom.x == 420.f && sBloom.w == 56.f);
        g.endFrame();

        // Window resize snaps immediately without flying across window
        g.beginFrame(1.f / 60.f, false);
        auto sRes = g.stepSlide(glassfx::SLIDE_FOCUS_PRESET, true, 210.f, 40.f, 1280.f, 720.f);
        CHECK(sRes.x == 210.f && sRes.w == 40.f);
        g.endFrame();

        // resetFaceSlides resets Focus slide while leaving Dock slide intact
        g.beginFrame(1.f / 60.f, false);
        g.stepSlide(glassfx::SLIDE_DOCK_FACE, true, 500.f, 90.f, 1280.f, 720.f);
        g.endFrame();
        g.resetFaceSlides();
        g.beginFrame(1.f / 60.f, false);
        auto sDock = g.stepSlide(glassfx::SLIDE_DOCK_FACE, true, 650.f, 90.f, 1280.f, 720.f);
        CHECK(sDock.x > 500.f && sDock.x < 650.f);  // Dock smoothly glides (was NOT reset!)
        auto sFoc = g.stepSlide(glassfx::SLIDE_FOCUS_PRESET, true, 300.f, 50.f, 1280.f, 720.f);
        CHECK(sFoc.x == 300.f);                    // Focus snapped cleanly after face switch!
        g.endFrame();
    }

    // 7. Frame-rate independence across 30/60/120/240 Hz (exact 0.20s elapsed)
    {
        auto simHover = [](int hz) {
            glassfx::Engine g;
            const float dt = 1.f / (float)hz;
            const int n = (int)std::lround(0.20f * hz);
            float ht = 0.f;
            for (int i = 0; i < n; ++i) {
                g.beginFrame(dt, false);
                ht = g.pose(77, 0, 0, 100, 40, 20, 1.f, true, false).hoverT;
                g.endFrame();
            }
            return ht;
        };
        const float h30 = simHover(30), h60 = simHover(60), h120 = simHover(120), h240 = simHover(240);
        CHECK(std::fabs(h30 - h60) < 1e-4f && std::fabs(h60 - h120) < 1e-4f && std::fabs(h120 - h240) < 1e-4f);
    }

    // 8. 20,000-frame chaotic fuzz stress test (150 IDs, random clicks/holds/cancels/resizes, NaN/Inf, neighbor-gap bounds)
    {
        glassfx::Engine g;
        uint32_t rng = 0x243F6A88u;
        auto nextf = [&]() {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            return (float)(rng & 0xFFFFu) / 65535.f;
        };
        const float sizes[5][2] = {{560, 360}, {1080, 1920}, {1280, 720}, {1920, 1080}, {3840, 2160}};
        std::vector<glassfx::RippleDraw> rips;
        for (int iter = 0; iter < 20000; ++iter) {
            const auto& sz = sizes[iter % 5];
            const float s = sz[0] < sz[1] * 0.9f ? sz[0] / 1100.f : std::min(sz[1] / 1080.f, sz[0] / 1440.f);
            float dt = (iter == 1000) ? std::numeric_limits<float>::quiet_NaN()
                     : (iter == 2000) ? std::numeric_limits<float>::infinity()
                     : (0.0005f + nextf() * 0.12f);
            g.beginFrame(dt, iter % 997 == 0);
            const uint64_t id = 1 + (uint64_t)(rng % 150u);
            const bool isCard = (id == 149 || id == 150);
            const float bw = isCard ? 240.f * s : 110.f * s;
            const float bh = isCard ? 82.f * s : 38.f * s;
            const float br = isCard ? 20.f * s : bh * 0.5f;
            if (iter % 11 == 0) {
                float cx = (iter == 3000) ? std::numeric_limits<float>::quiet_NaN() : (100.f + nextf() * bw);
                g.onPress(id, cx, 100.f + nextf() * bh, 100.f, 100.f, bw, bh, isCard ? 1.4f : 1.0f, (iter % 22) == 0);
            }
            if (iter % 17 == 0) g.onRelease();
            if (iter % 43 == 0) g.cancelHold();
            auto p = g.pose(id, 100.f, 100.f, bw, bh, br, s, (iter % 3) == 0, isCard);
            CHECK(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.w) && std::isfinite(p.h) && std::isfinite(p.r));
            CHECK(p.w >= 8.f && p.h >= 8.f && p.r >= 0.f && p.r <= std::min(p.w, p.h) * 0.5f + 1e-3f);
            if (!isCard) CHECK(p.w <= bw + 2.f * 2.2f * s + 1e-2f && p.h <= bh + 2.f * 2.2f * s + 1e-2f);
            g.collectRipples(id, p.x, p.y, p.w, p.h, s, rips);
            for (const auto& rd : rips) {
                CHECK(std::isfinite(rd.cx) && std::isfinite(rd.cy) && std::isfinite(rd.radius) && std::isfinite(rd.alpha));
            }
            g.stepSlide(iter % glassfx::SLIDE_COUNT, (iter % 5) != 0, 100.f + nextf() * 400.f, 60.f + nextf() * 60.f, sz[0], sz[1]);
            g.endFrame();
            CHECK(g.rippleCount() <= glassfx::kMaxRipples);
        }
        // Final calm settle: verify EVERYTHING relaxes back to 0 items, 0 ripples, busy()==false!
        g.cancelHold();
        for (int i = 0; i < 120; ++i) {
            g.beginFrame(1.f / 60.f, false);
            for (int sl = 0; sl < glassfx::SLIDE_COUNT; ++sl) g.stepSlide(sl, false, 0.f, 0.f, 1920.f, 1080.f);
            g.endFrame();
        }
        CHECK(!g.busy() && g.itemCount() == 0 && g.rippleCount() == 0);
    }

    // 9. Settings toggle switches: snap on first frame, smooth liquid-droplet stretch mid-glide, zero idle cost, prune on leave
    {
        glassfx::Engine g;
        g.beginFrame(1.f / 60.f, false);
        auto t0 = g.stepToggle(0x20050010ULL, true);
        CHECK(t0.pos == 1.f && t0.stretch == 0.f);
        g.endFrame();
        CHECK(!g.busy() && g.toggleCount() == 1);

        // Flip OFF -> glides smoothly with liquid stretch mid-flight, then settles to busy()==false
        bool sawStretch = false;
        float prev = 1.f;
        for (int i = 0; i < 40; ++i) {
            g.beginFrame(1.f / 60.f, false);
            auto tp = g.stepToggle(0x20050010ULL, false);
            CHECK(tp.pos <= prev + 1e-6f && tp.pos >= 0.f);
            if (tp.stretch > 0.35f) sawStretch = true;
            prev = tp.pos;
            g.endFrame();
        }
        CHECK(sawStretch && prev == 0.f && !g.busy());

        // Leaving tab (stop calling stepToggle) prunes the entry completely
        g.beginFrame(1.f / 60.f, false);
        g.endFrame();
        CHECK(g.toggleCount() == 0 && !g.busy());
    }

    if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
    std::printf("regression_298 ok\n");
    return 0;
}
