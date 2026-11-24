// Live geomagnetic space weather (NOAA SWPC Planetary Kp-Index) for Aurora Borealis / Australis.
// Parsing and geomagnetic visibility math are portable C++17; WinHTTP fetch is Windows-only.
#pragma once
#include <string>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <chrono>

namespace spacewx {

// Maximum observation age (seconds) for driving real-time aurora & Sky storm badge (6 hours = 2 NOAA 3-hour Kp bars).
constexpr double kLiveMaxAgeSec = 6.0 * 3600.0;
// Maximum observation age (seconds) accepted when parsing a NOAA SWPC Kp feed (24 hours).
constexpr double kFeedMaxAgeSec = 24.0 * 3600.0;

struct Data {
    bool valid = false;
    double fetched = 0;        // unix time fetched
    double updated = 0;        // unix time of latest Kp observation
    double kp = 0.0;           // latest 3-hour planetary Kp index (0..9)
    double kpMax24h = 0.0;     // peak Kp strictly from observations within [now - 24h, now + 15m]
    bool hasMax24h = false;
    std::string source;        // "NOAA SWPC"
};

// Parse NOAA SWPC planetary K-index JSON (array-of-arrays or array-of-objects).
bool parseNoaaKp(const std::string& json, double now, Data& out, std::string& err);

// Freshness checks (separate observation timestamp from fetch time).
bool isLive(const Data& d, double now);
bool isStale(const Data& d, double now);
std::string ageLabel(double obsTime, double now);

// NOAA G-scale storm level (0 = G0 Quiet, 1 = G1 Minor [Kp 5-..5+], ..., 4 = G4 Severe [Kp 8-..9-], 5 = G5 Extreme [Kp 9o]).
int stormScale(double kp);

// Human-readable geomagnetic status ("Quiet", "Unsettled", "Active", "G1 Minor Storm", ...).
const char* scaleLabel(double kp);

// Approximate lowest geographic latitude (degrees, abs) where aurora reaches the horizon at a given Kp.
double auroraMinAbsLat(double kp);

// Smooth aurora visibility strength (0..1) for an observer's latitude and live Kp index.
// When globalStormGlow is true, strong storms (Kp >= 4.67) produce a subtle high-altitude glow at any latitude.
float auroraVisibility(double kp, double latDeg, bool globalStormGlow = false);

// Background NOAA Kp poller with separate recurring toggle (`configure`) and non-recurring manual refresh (`requestOneShot`).
class Poller {
public:
    using FetchFn = std::function<bool(double now, Data& out, std::string& err)>;
    using NowFn   = std::function<double()>;
    using LogFn   = std::function<void(const std::string&)>;

    std::mutex mu;
    std::condition_variable cv;
    bool stop = false, refresh = false, oneShot = false, ready = false, busy = false, enabled = true;
    Data result;
    double lastTry = 0, lastOk = 0;
    std::string lastErr;
    double pollIntervalSec = 45.0 * 60.0;
    double retryBaseSec = 180.0;
    std::thread th;
    std::atomic<bool> exited{true};

    void start(FetchFn fetchFn, NowFn nowFn, LogFn logFn = {}) {
        fetchFn_ = std::move(fetchFn);
        nowFn_ = std::move(nowFn);
        logFn_ = std::move(logFn);
        { std::lock_guard<std::mutex> l(mu); stop = false; }
        exited = false;
        th = std::thread([this] { run(); });
    }
    void stopAsync() { { std::lock_guard<std::mutex> l(mu); stop = true; } cv.notify_all(); }
    bool isEnabled() { std::lock_guard<std::mutex> l(mu); return enabled; }
    bool finish(unsigned waitMs) {
        if (!th.joinable()) return true;
        const auto t0 = std::chrono::steady_clock::now();
        while (!exited && std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count() < (long long)waitMs) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (exited) { th.join(); return true; }
        th.detach();
        return false;
    }
    // Enable or disable recurring background polling (wakes immediately on OFF -> ON).
    void configure(bool on) {
        {
            std::lock_guard<std::mutex> l(mu);
            const bool turningOn = on && !enabled;
            enabled = on;
            if (turningOn) refresh = true;
            if (!on) refresh = false;
        }
        cv.notify_all();
    }
    // Request a single immediate fetch WITHOUT turning on recurring polling if `enabled` is false.
    void requestOneShot() {
        {
            std::lock_guard<std::mutex> l(mu);
            if (enabled) refresh = true;
            else oneShot = true;
        }
        cv.notify_all();
    }

private:
    FetchFn fetchFn_;
    NowFn nowFn_;
    LogFn logFn_;

    void run() {
        struct ExitFlag { std::atomic<bool>& f; ~ExitFlag() { f = true; } } exitFlag{exited};
        int failures = 0;
        for (;;) {
            bool wasOneShot = false;
            {
                std::unique_lock<std::mutex> l(mu);
                cv.wait(l, [this] { return stop || enabled || oneShot; });
                if (stop) return;
                wasOneShot = !enabled && oneShot;
                oneShot = false;
                refresh = false;
                busy = true;
                lastTry = nowFn_ ? nowFn_() : 0.0;
            }
            Data d; std::string err;
            const double tNow = nowFn_ ? nowFn_() : 0.0;
            const bool ok = fetchFn_ ? fetchFn_(tNow, d, err) : false;
            if (logFn_) {
                if (ok) {
                    char buf[160];
                    std::snprintf(buf, sizeof(buf), "SpaceWx: NOAA Kp %.2f (%s, 24h max %.2f)", d.kp, scaleLabel(d.kp), d.kpMax24h);
                    logFn_(buf);
                } else {
                    logFn_("SpaceWx: " + err);
                }
            }
            std::unique_lock<std::mutex> l(mu);
            busy = false;
            if (ok) { result = d; ready = true; failures = 0; lastOk = nowFn_ ? nowFn_() : tNow; lastErr.clear(); }
            else { ++failures; lastErr = err; }
            if (stop) return;
            // If this was a manual one-shot while disabled (or user switched off during fetch), return directly to idle wait!
            if (wasOneShot || !enabled) continue;
            const double waitSec = ok ? pollIntervalSec : std::min(pollIntervalSec, retryBaseSec * (1 << std::min(failures, 4)));
            cv.wait_for(l, std::chrono::milliseconds((long long)std::max(1.0, waitSec * 1000.0)), [this] {
                return stop || refresh || oneShot || !enabled;
            });
            if (stop) return;
        }
    }
};

#ifdef _WIN32
bool fetch(double now, Data& out, std::string& err);
#endif

} // namespace spacewx
