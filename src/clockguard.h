// Tells a real step of the system clock (a time-sync correction, someone changing the time) from ordinary passing time.
// Portable: no Windows calls in here, so tests/clockguard_test.cpp builds it on its own.
//
// Idea: look at the wall clock and at a monotonic clock (one that never jumps) every frame. If the wall clock moved by a
// different amount than the monotonic one over the same short stretch, the wall clock was stepped by the difference.
#pragma once
#include <cmath>

namespace util {

struct ClockGuard {
    double maxGap  = 5.0;     // seconds. Further apart than this (the PC slept, a frame stalled) there is no verdict: start over
    double minStep = 0.25;    // seconds. Smaller differences are ordinary timer jitter, not a step

    // wall: the system clock, in seconds. mono: a clock that only moves forward, in seconds.
    // Returns how far the wall clock was stepped since the last call (positive = forward), or 0.
    double sample(double wall, double mono) {
        double step = 0.0;
        if (have_) {
            const double dm = mono - lastMono_, dw = wall - lastWall_;
            if (dm >= 0.0 && dm <= maxGap) {
                const double d = dw - dm;
                if (std::fabs(d) >= minStep) step = d;
            }
        }
        lastWall_ = wall; lastMono_ = mono; have_ = true;
        return step;
    }
    // call after a resume from sleep: the two clocks may disagree about the time spent asleep
    void reset() { have_ = false; }

private:
    double lastWall_ = 0.0, lastMono_ = 0.0;
    bool have_ = false;
};

} // namespace util
