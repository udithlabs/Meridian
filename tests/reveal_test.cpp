// The screen saver's graceful start (src/reveal.h): the numbers behind the animation. No Windows needed.
#include "reveal.h"
#include <cstdio>
#include <cmath>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main() {
    const float W = 1920.f, H = 1080.f;
    const float reach = 0.5f * std::sqrt(W * W + H * H);

    // the first frame is all black, the last one is the finished picture and asks for nothing at all
    reveal::Look a = reveal::at(0.0, W, H);
    CHECK(!a.done); CHECK(a.dim > 0.999f); CHECK(a.outer < 1.f); CHECK(a.scale > 1.04f && a.scale < 1.05f);
    reveal::Look z = reveal::at(1.0, W, H);
    CHECK(z.done); CHECK(z.scale == 1.f); CHECK(z.dim == 0.f);
    CHECK(reveal::at(7.0, W, H).done);                                   // past the end stays finished
    CHECK(!reveal::at(-3.0, W, H).done && reveal::at(-3.0, W, H).dim > 0.999f);   // before the start stays black

    // it only ever moves one way: the picture settles (scale falls), the black lifts (dim falls), the circle of light grows
    reveal::Look prev = a;
    for (int i = 1; i <= 1000; ++i) {
        const double p = i / 1000.0 * 0.9999;
        reveal::Look k = reveal::at(p, W, H);
        CHECK(k.scale <= prev.scale + 1e-6f); CHECK(k.dim <= prev.dim + 1e-6f); CHECK(k.outer >= prev.outer - 1e-3f); CHECK(k.inner >= prev.inner - 1e-3f);
        CHECK(k.scale >= 1.f); CHECK(k.dim >= 0.f && k.dim <= 1.f); CHECK(k.outer >= k.inner);
        prev = k;
    }

    // just before the end nothing is left to see: no dark corners, no zoom (so the last frame does not jump)
    reveal::Look e = reveal::at(0.9999, W, H);
    CHECK(e.dim == 0.f); CHECK(e.scale < 1.0001f); CHECK(e.inner > 0.99f * reach);
    // the whole picture is up before the corners finish opening
    reveal::Look m = reveal::at(0.6, W, H);
    CHECK(m.dim == 0.f); CHECK(m.inner < reach);
    // it is a screen-sized animation: the same shape on a small window and a 4K one
    for (float s : {640.f, 3840.f}) { reveal::Look k = reveal::at(0.9999, s, s * 0.5625f); CHECK(k.inner > 0.99f * 0.5f * std::sqrt(s * s + s * 0.5625f * s * 0.5625f)); }
    CHECK(reveal::kSeconds > 1.5 && reveal::kSeconds < 3.0);

    // leaving: the first frame is the picture as it was (nothing laid over it), the last is black, and it only ever moves one way
    reveal::Look l0 = reveal::leaving(0.0, W, H);
    CHECK(!l0.done); CHECK(l0.dim == 0.f); CHECK(l0.scale == 1.f); CHECK(l0.inner >= reach - 1e-3f);
    reveal::Look l1 = reveal::leaving(1.0, W, H);
    CHECK(l1.dim > 0.999f); CHECK(l1.outer < 1e-3f);
    // it answers at once: a tenth of the way through the corners have already started to close
    CHECK(reveal::leaving(0.1, W, H).inner < reach - 1.f);
    reveal::Look lp = l0;
    for (int i = 1; i <= 1000; ++i) {
        reveal::Look k = reveal::leaving(i / 1000.0, W, H);
        CHECK(k.scale >= lp.scale - 1e-6f); CHECK(k.dim >= lp.dim - 1e-6f); CHECK(k.outer <= lp.outer + 1e-3f); CHECK(k.inner <= lp.inner + 1e-3f);
        CHECK(k.dim >= 0.f && k.dim <= 1.f); CHECK(k.outer >= k.inner);
        lp = k;
    }
    CHECK(reveal::leaving(5.0, W, H).dim > 0.999f && reveal::leaving(-2.0, W, H).dim == 0.f);     // past the end stays black, before the start stays clear
    CHECK(reveal::kLeaveSeconds > 0.2 && reveal::kLeaveSeconds < 0.9);                           // quick enough to feel instant, long enough to be seen

    if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
    std::printf("reveal ok\n");
    return 0;
}
