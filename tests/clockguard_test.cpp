// Builds on its own (no Windows needed):  g++ -std=c++17 -I../src clockguard_test.cpp -o clockguard_test && ./clockguard_test
#include <cstdio>
#include <cmath>
#include "clockguard.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
static bool near(double a, double b, double e = 1e-9) { return std::fabs(a - b) <= e; }

int main() {
    // ordinary time: wall and monotonic move together, with a little jitter
    { util::ClockGuard g; double w = 1.8e9, m = 1000;
      CHECK(near(g.sample(w, m), 0));
      for (int i = 0; i < 100; ++i) { w += 1.0; m += 1.0 + (i % 3 ? 0.004 : -0.004); CHECK(near(g.sample(w, m), 0)); } }
    // the clock is set back an hour: reported once, and only once
    { util::ClockGuard g; double w = 1.8e9, m = 500; g.sample(w, m);
      w += 1.0; m += 1.0; CHECK(near(g.sample(w, m), 0));
      w += 1.0 - 3600.0; m += 1.0; CHECK(near(g.sample(w, m), -3600.0));
      w += 1.0; m += 1.0; CHECK(near(g.sample(w, m), 0)); }
    // forward step of 7.5 s (a small time-sync correction)
    { util::ClockGuard g; g.sample(100.0, 10.0); CHECK(near(g.sample(100.0 + 1.0 + 7.5, 11.0), 7.5)); }
    // a long gap (a frame stalled; or the PC slept and the monotonic clock counted the sleep too) gives no verdict
    { util::ClockGuard g; g.sample(100.0, 10.0); CHECK(near(g.sample(100.0 + 3600.0, 10.0 + 3600.0), 0)); }
    // if the monotonic clock does NOT count sleep, the resume handler must call reset(), or the sleep would look like a step
    { util::ClockGuard g; g.sample(100.0, 10.0); g.reset(); CHECK(near(g.sample(100.0 + 3600.0, 10.5), 0)); }
    // reset() starts over
    { util::ClockGuard g; g.sample(100.0, 10.0); g.reset(); CHECK(near(g.sample(5000.0, 11.0), 0)); }
    // a monotonic clock that went backwards is ignored
    { util::ClockGuard g; g.sample(100.0, 10.0); CHECK(near(g.sample(101.0, 9.0), 0)); }
    // below the threshold is jitter
    { util::ClockGuard g; g.sample(100.0, 10.0); CHECK(near(g.sample(101.2, 11.0), 0)); }
    if (fails == 0) std::printf("clockguard_test: all passed\n");
    return fails ? 1 : 0;
}
