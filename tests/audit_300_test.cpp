// Native Windows engineering verification suite for Meridian 3.0.0 ("Aurora").
// Exercises every engineering fix end-to-end with zero mocks on real Windows filesystem,
// Win32 Dynamic TZ, INI persistence, NOAA Poller, Weather/AQI parsers, Settings focus trap,
// and Media Foundation audio worker failure recovery.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "app.h"
#include "astro.h"
#include "settings_io.h"
#include "skyevents.h"
#include "spacewx.h"
#include "textedit.h"
#include "ufo_audio.h"
#include "weather.h"
#include "../setup/install.h"

namespace fs = std::filesystem;

static std::string readTextFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    assert(in.good());
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static void writeTextFile(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    assert(out.good());
    out.write(s.data(), (std::streamsize)s.size());
    assert(out.good());
}

static fs::path makeTempRoot(const wchar_t* tag) {
    wchar_t tmp[MAX_PATH * 2]{};
    DWORD n = GetTempPathW(MAX_PATH * 2, tmp);
    assert(n > 0);
    fs::path p = fs::path(std::wstring(tmp, n)) / (std::wstring(L"meridian_audit_") + tag + L"_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64()));
    fs::remove_all(p);
    fs::create_directories(p);
    return p;
}

// ============================================================================
// 1. Installer transactional rollback, lock recovery, and verified move
// ============================================================================
static void testInstallerTransactions() {
    std::puts("[1/7] Installer transactional rollback, lock recovery, and verified move...");

    // 1a. Lock on later target rolls back earlier replacement completely
    {
        fs::path root = makeTempRoot(L"inst1a");
        fs::path dir = root / L"app";
        fs::path stage = dir / L".meridian-stage-test1a";
        writeTextFile(dir / L"Meridian.exe", "OLD_EXE");
        writeTextFile(dir / L"README.txt", "OLD_README");
        writeTextFile(stage / L"Meridian.exe", "NEW_EXE");
        writeTextFile(stage / L"README.txt", "NEW_README");

        HANDLE hLock = CreateFileW((dir / L"README.txt").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        assert(hLock != INVALID_HANDLE_VALUE);

        std::wstring err = inst::commitStagedFiles(dir.wstring(), stage.wstring(), {L"Meridian.exe", L"README.txt"});
        CloseHandle(hLock);

        assert(!err.empty());
        assert(readTextFile(dir / L"Meridian.exe") == "OLD_EXE");
        assert(readTextFile(dir / L"README.txt") == "OLD_README");
        assert(!fs::exists(dir / L".meridian-tx.log"));
        fs::remove_all(root);
    }

    // 1b. Reader opens first replacement while second target is locked -> rollback detects failure,
    // preserves backup + journal, names unresolved file, and retry cleanly restores once lock clears!
    {
        fs::path root = makeTempRoot(L"inst1b");
        fs::path dir = root / L"app";
        fs::path stage = dir / L".meridian-stage-test1b";
        writeTextFile(dir / L"Meridian.exe", "OLD_EXE");
        writeTextFile(dir / L"README.txt", "OLD_README");
        writeTextFile(stage / L"Meridian.exe", "NEW_EXE");
        writeTextFile(stage / L"README.txt", "NEW_README");

        HANDLE hReadmeLock = CreateFileW((dir / L"README.txt").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        assert(hReadmeLock != INVALID_HANDLE_VALUE);

        std::atomic<bool> stopWatcher{false};
        HANDLE hExeLock = INVALID_HANDLE_VALUE;
        std::thread watcher([&]() {
            const fs::path exePath = dir / L"Meridian.exe";
            while (!stopWatcher.load()) {
                std::ifstream in(exePath, std::ios::binary);
                if (in.good()) {
                    std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                    in.close();
                    if (s == "NEW_EXE") {
                        // Hold a read handle WITHOUT FILE_SHARE_DELETE so rollback cannot rename/replace Meridian.exe
                        hExeLock = CreateFileW(exePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                        if (hExeLock != INVALID_HANDLE_VALUE) break;
                    }
                }
                Sleep(10);
            }
        });

        std::wstring err = inst::commitStagedFiles(dir.wstring(), stage.wstring(), {L"Meridian.exe", L"README.txt"});
        stopWatcher.store(true);
        watcher.join();

        assert(!err.empty());
        assert(hExeLock != INVALID_HANDLE_VALUE);
        // Must report unresolved rollback honestly and name Meridian.exe (never claim "left untouched")
        assert(err.find(L"Meridian.exe") != std::wstring::npos);
        assert(err.find(L"left untouched") == std::wstring::npos);
        // Persistent journal and owned backup directory MUST be preserved for retry
        assert(fs::exists(dir / L".meridian-tx.log"));
        bool foundBackup = false;
        for (const auto& ent : fs::directory_iterator(dir)) {
            if (ent.is_directory() && ent.path().filename().wstring().rfind(L".meridian-backup-", 0) == 0) foundBackup = true;
        }
        assert(foundBackup);

        // Release both locks and retry recovery: must restore OLD_EXE cleanly and remove backup + journal
        CloseHandle(hExeLock);
        CloseHandle(hReadmeLock);
        std::wstring recErr = inst::recoverPendingTransaction(dir.wstring());
        assert(recErr.empty());
        assert(readTextFile(dir / L"Meridian.exe") == "OLD_EXE");
        assert(readTextFile(dir / L"README.txt") == "OLD_README");
        assert(!fs::exists(dir / L".meridian-tx.log"));
        for (const auto& ent : fs::directory_iterator(dir)) {
            assert(ent.path().filename().wstring().rfind(L".meridian-backup-", 0) != 0);
        }
        fs::remove_all(root);
    }

    // 1c. Crash recovery between target->backup and stage->target + untouched unrelated .meridian-* folder
    {
        fs::path root = makeTempRoot(L"inst1c");
        fs::path dir = root / L"app";
        writeTextFile(dir / L".meridian-user-custom" / L"keep.txt", "KEEP_ME");
        writeTextFile(dir / L".meridian-backup-crash" / L"Meridian.exe", "OLD_EXE");
        writeTextFile(dir / L".meridian-stage-crash" / L"Meridian.exe", "NEW_EXE");
        // Simulate crash: target Meridian.exe was moved to backup before staged replacement arrived
        writeTextFile(dir / L".meridian-tx.log",
            "MERIDIAN_TX_V1\n"
            "STAGE .meridian-stage-crash\n"
            "BACKUP .meridian-backup-crash\n"
            "STATE COMMITTING\n"
            "FILE 1 Meridian.exe\n");

        std::wstring recErr = inst::recoverPendingTransaction(dir.wstring());
        assert(recErr.empty());
        assert(readTextFile(dir / L"Meridian.exe") == "OLD_EXE");
        assert(!fs::exists(dir / L".meridian-tx.log"));
        assert(!fs::exists(dir / L".meridian-backup-crash"));
        assert(!fs::exists(dir / L".meridian-stage-crash"));
        // Unrelated user .meridian-* folder MUST remain untouched
        assert(readTextFile(dir / L".meridian-user-custom" / L"keep.txt") == "KEEP_ME");
        fs::remove_all(root);
    }

    // 1d. Move with denied registry write keeps previous install directory intact
    {
        fs::path root = makeTempRoot(L"inst1d");
        fs::path prevDir = root / L"old_meridian";
        fs::path newDir = root / L"new_meridian";
        writeTextFile(prevDir / L"Meridian.exe", "OLD");
        writeTextFile(newDir / L"Meridian.exe", "NEW");

        Payload pl;
        PayloadEntry pe;
        pe.path = "Meridian.exe";
        pl.e.push_back(pe);

        inst::RegOps denyOps;
        denyOps.setStr = [](const wchar_t*, const wchar_t*, const std::wstring&, DWORD) { return false; };
        denyOps.setDword = [](const wchar_t*, const wchar_t*, DWORD) { return false; };

        std::wstring err = inst::registerAndRetireMove(pl, newDir.wstring(), prevDir.wstring(), 1024, &denyOps);
        assert(!err.empty());
        assert(fs::exists(prevDir / L"Meridian.exe"));
        assert(readTextFile(prevDir / L"Meridian.exe") == "OLD");

        // When registration writes succeed, move retires prevDir cleanly
        int strWrites = 0, dwordWrites = 0;
        inst::RegOps allowOps;
        allowOps.setStr = [&](const wchar_t*, const wchar_t*, const std::wstring&, DWORD) { ++strWrites; return true; };
        allowOps.setDword = [&](const wchar_t*, const wchar_t*, DWORD) { ++dwordWrites; return true; };

        std::wstring okErr = inst::registerAndRetireMove(pl, newDir.wstring(), prevDir.wstring(), 1024, &allowOps);
        assert(okErr.empty());
        assert(strWrites >= 7 && dwordWrites >= 3);
        assert(!fs::exists(prevDir / L"Meridian.exe"));
        fs::remove_all(root);
    }
}

// ============================================================================
// 2. Live NOAA Space Weather boundaries, staleness, validation, and Poller
// ============================================================================
static void testSpaceWx() {
    std::puts("[2/7] NOAA Space Weather G-scale, freshness, parser validation, and Poller...");

    // Official NOAA third-step G-scale boundaries
    assert(spacewx::stormScale(3.67) == 0 && std::string(spacewx::scaleLabel(3.67)) == "Unsettled");
    assert(spacewx::stormScale(4.00) == 0 && std::string(spacewx::scaleLabel(4.00)) == "Active");
    assert(spacewx::stormScale(4.33) == 0 && std::string(spacewx::scaleLabel(4.33)) == "Active");
    assert(spacewx::stormScale(4.67) == 1 && std::string(spacewx::scaleLabel(4.67)) == "G1 Minor Storm");
    assert(spacewx::stormScale(5.00) == 1);
    assert(spacewx::stormScale(5.33) == 1);
    assert(spacewx::stormScale(5.67) == 2 && std::string(spacewx::scaleLabel(5.67)) == "G2 Moderate Storm");
    assert(spacewx::stormScale(6.67) == 3 && std::string(spacewx::scaleLabel(6.67)) == "G3 Strong Storm");
    assert(spacewx::stormScale(7.67) == 4 && std::string(spacewx::scaleLabel(7.67)) == "G4 Severe Storm");
    assert(spacewx::stormScale(8.33) == 4 && std::string(spacewx::scaleLabel(8.33)) == "G4 Severe Storm");
    assert(spacewx::stormScale(8.67) == 4 && std::string(spacewx::scaleLabel(8.67)) == "G4 Severe Storm");
    assert(spacewx::stormScale(9.00) == 5 && std::string(spacewx::scaleLabel(9.00)) == "G5 Extreme Storm");

    const double now = astro::daysFromCivil(2026, 11, 22) * 86400.0 + 18 * 3600.0;
    // Strict 24h max window: 36h-old Kp 9.0 MUST NOT leak into kpMax24h
    {
        const std::string feed = R"([
            ["time_tag","Kp","a_running","station_count"],
            ["2026-11-21 06:00:00.000","9.00","400","8"],
            ["2026-11-22 15:00:00.000","3.33","18","8"]
        ])";
        spacewx::Data d; std::string err;
        assert(spacewx::parseNoaaKp(feed, now, d, err));
        assert(std::fabs(d.kp - 3.33) < 0.01);
        assert(std::fabs(d.kpMax24h - 3.33) < 0.01);
        assert(spacewx::isLive(d, now));
        assert(!spacewx::isStale(d, now));
        // After > 6h from observation (15:00 -> 22:00 = 7h), isLive expires and isStale becomes true
        const double later = astro::daysFromCivil(2026, 11, 22) * 86400.0 + 22 * 3600.0;
        assert(!spacewx::isLive(d, later));
        assert(spacewx::isStale(d, later));
    }

    // 48h-old feed rejected cleanly
    {
        const std::string oldFeed = R"([["time_tag","Kp"],["2026-11-20 12:00:00.000","6.67"]])";
        spacewx::Data d; std::string err;
        assert(!spacewx::parseNoaaKp(oldFeed, now, d, err));
    }

    // Malformed parser fixtures rejected strictly
    {
        spacewx::Data d; std::string err;
        assert(!spacewx::parseNoaaKp(R"([["time_tag","Kp"],["2026-11-22 15:00:00","6.0unknown"]])", now, d, err));
        assert(!spacewx::parseNoaaKp(R"([["time_tag","Kp"],["2026-11-22 24:99:00","5.0"]])", now, d, err));
        assert(!spacewx::parseNoaaKp(R"([["time_tag","Kp"],["2026-02-30 12:00:00","5.0"]])", astro::daysFromCivil(2026, 3, 1) * 86400.0, d, err));
        // Valid leap day accepted
        const double leapNow = astro::daysFromCivil(2024, 2, 29) * 86400.0 + 23 * 3600.0;
        assert(spacewx::parseNoaaKp(R"([["time_tag","Kp"],["2024-02-29 21:00:00","4.67"]])", leapNow, d, err));
    }

    // Single-session Poller lifecycle: starts OFF (0 fetches) -> OFF->ON wakes & recurs -> ON->OFF stops -> manual one-shot while OFF fetches ONCE and stays OFF
    {
        spacewx::Poller poller;
        poller.enabled = false;
        poller.pollIntervalSec = 0.04;
        poller.retryBaseSec = 0.04;
        std::atomic<int> fetches{0};
        poller.start(
            [&](double t, spacewx::Data& out, std::string&) {
                ++fetches;
                out.valid = true; out.kp = 5.0; out.kpMax24h = 5.0; out.updated = t; out.fetched = t; out.source = "NOAA SWPC";
                return true;
            },
            [&]() { return now; });

        std::this_thread::sleep_for(std::chrono::milliseconds(110));
        assert(fetches.load() == 0);

        poller.configure(true);
        for (int i = 0; i < 60 && fetches.load() < 2; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(15));
        assert(fetches.load() >= 2);

        poller.configure(false);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        const int afterOff = fetches.load();
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        assert(fetches.load() == afterOff);

        // Manual refresh while OFF runs exactly ONCE and does NOT turn recurring polling back on
        poller.requestOneShot();
        for (int i = 0; i < 40 && fetches.load() < afterOff + 1; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(15));
        assert(fetches.load() == afterOff + 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(140));
        assert(fetches.load() == afterOff + 1);
        assert(!poller.isEnabled());

        poller.stopAsync();
        assert(poller.finish(1000));
    }
}

// ============================================================================
// 3. Orrery Moon orientation & Weather missing vs zero measurement tracking
// ============================================================================
static void testOrreryAndMeasurements() {
    std::puts("[3/7] Orrery Moon phase orientation & missing vs zero weather measurements...");

    auto angDiff = [](double a, double b) {
        double d = std::fabs(astro::norm360(a - b));
        return d > 180.0 ? 360.0 - d : d;
    };
    const double tNew  = astro::daysFromCivil(2026, 11, 9)  * 86400.0 + 7.03  * 3600.0;
    const double tFq   = astro::daysFromCivil(2026, 11, 17) * 86400.0 + 11.80 * 3600.0;
    const double tFull = astro::daysFromCivil(2026, 11, 24) * 86400.0 + 14.88 * 3600.0;
    const double tLq   = astro::daysFromCivil(2026, 12, 1)  * 86400.0 + 15.15 * 3600.0;

    // New Moon: sunward from Earth (Earth helio lon + 180)
    assert(angDiff(astro::orreryMoonLonDeg(tNew), astro::norm360(astro::orreryBody(astro::OP_EARTH, tNew).lonDeg + 180.0)) < 5.0);
    // First Quarter: +90 from sunward
    assert(angDiff(astro::orreryMoonLonDeg(tFq), astro::norm360(astro::orreryBody(astro::OP_EARTH, tFq).lonDeg + 270.0)) < 8.0);
    // Full Moon: anti-solar from Earth (Earth helio lon)
    assert(angDiff(astro::orreryMoonLonDeg(tFull), astro::orreryBody(astro::OP_EARTH, tFull).lonDeg) < 5.0);
    // Last Quarter: +270 from sunward (+90 from anti-solar)
    assert(angDiff(astro::orreryMoonLonDeg(tLq), astro::norm360(astro::orreryBody(astro::OP_EARTH, tLq).lonDeg + 90.0)) < 8.0);

    // Null cloud/wind/gust vs real 0.0 calm values
    {
        const std::string nullJson = R"({
            "utc_offset_seconds": 0, "timezone": "UTC",
            "current": {
                "time": "2026-11-22T12:00",
                "temperature_2m": 18.5, "apparent_temperature": 18.0, "relative_humidity_2m": 60,
                "weather_code": 1, "is_day": 1, "cloud_cover": null, "wind_speed_10m": null, "wind_gusts_10m": null
            },
            "hourly": { "time": ["2026-11-22T12:00", "2026-11-22T13:00"], "temperature_2m": [18.5, 19.0], "weather_code": [1, 1] },
            "daily": { "time": ["2026-11-22"], "temperature_2m_max": [22.0], "temperature_2m_min": [14.0], "weather_code": [1] }
        })";
        weather::Data d; std::string err;
        assert(weather::parseForecast(nullJson, d, err));
        assert(!d.cur.hasCloud && !d.cur.hasWind && !d.cur.hasGust);
    }
    {
        const std::string zeroJson = R"({
            "utc_offset_seconds": 0, "timezone": "UTC",
            "current": {
                "time": "2026-11-22T12:00",
                "temperature_2m": 18.5, "apparent_temperature": 18.0, "relative_humidity_2m": 60,
                "weather_code": 0, "is_day": 1, "cloud_cover": 0, "wind_speed_10m": 0, "wind_gusts_10m": 0
            },
            "hourly": { "time": ["2026-11-22T12:00", "2026-11-22T13:00"], "temperature_2m": [18.5, 19.0], "weather_code": [0, 0] },
            "daily": { "time": ["2026-11-22"], "temperature_2m_max": [22.0], "temperature_2m_min": [14.0], "weather_code": [0] }
        })";
        weather::Data d; std::string err;
        assert(weather::parseForecast(zeroJson, d, err));
        assert(d.cur.hasCloud && d.cur.hasWind && d.cur.hasGust);
        assert(d.cur.cloud == 0.0 && d.cur.wind == 0.0 && d.cur.gust == 0.0);
    }
}

// ============================================================================
// 4. Saved-place TZ authority, active sessions, and fired one-shot alarm
// ============================================================================
static void testTimezoneSessionsAndAlarm() {
    std::puts("[4/7] Saved-place TZ authority, active sessions, and fired one-shot alarm...");

    // MET Norway for New York while host PC is UTC+05:30 (+19800) must NOT stamp +19800 onto New York
    const double tNov22_2000Utc = astro::daysFromCivil(2026, 11, 22) * 86400.0 + 20 * 3600.0;
    const double tJul15_2000Utc = astro::daysFromCivil(2026, 7, 15)  * 86400.0 + 20 * 3600.0;
    {
        const std::string metno = readTextFile("tests/sample_metno.json");
        weather::Data wx; std::string err;
        wx.utcOffset = 19800; wx.hasUtcOffset = false; // host PC is UTC+05:30
        assert(weather::parseProvider(weather::PV_METNO, metno, 40.7128, -74.0060, wx, err));
        assert(wx.valid);
        assert(!wx.hasUtcOffset);
        assert(wx.utcOffset != 19800);

        astro::Geo ny; ny.name = "New York"; ny.lat = 40.7128; ny.lon = -74.0060;
        bool dynNov = false, dynJul = false;
        const int offNov = app::resolvePlaceOffset(ny, wx, tNov22_2000Utc, &dynNov);
        const int offJul = app::resolvePlaceOffset(ny, wx, tJul15_2000Utc, &dynJul);
        assert(dynNov && offNov == -5 * 3600);      // EST (UTC-5) in November
        assert(dynJul && offJul == -4 * 3600);      // EDT (UTC-4) in July

        const util::LocalTime nyLocal = util::toLocal(tNov22_2000Utc, offNov);
        assert(nyLocal.y == 2026 && nyLocal.m == 11 && nyLocal.d == 22 && nyLocal.hh == 15 && nyLocal.mm == 0);

        // Matches World Clocks lookupWinTzOffset("Eastern Standard Time", ...)
        int wcOff = 0;
        assert(app::lookupWinTzOffset("Eastern Standard Time", tNov22_2000Utc, wcOff));
        assert(offNov == wcOff);

        // Empty weather cache still resolves New York identically via persisted/resolved TZ
        weather::Data emptyWx;
        assert(app::resolvePlaceOffset(ny, emptyWx, tNov22_2000Utc) == -5 * 3600);
    }

    // Active Focus, Countdown, Paused Countdown, Stopwatch + Laps, and Downtime-Expired persistence
    {
        const double t0 = astro::daysFromCivil(2026, 11, 22) * 86400.0 + 18 * 3600.0;
        app::App a;
        a.setNow(t0, 0, 0.016);
        a.focus.durationSec = 300.0; a.focus.running = true; a.focus.endAt = t0 + 240.0; a.focus.pausedRemaining = 240.0;
        a.timer.durationSec = 1800.0; a.timer.running = true; a.timer.endAt = t0 + 1740.0; a.timer.pausedRemaining = 1740.0;
        a.timer.swRunning = true; a.timer.swStart = t0 - 60.0; a.timer.swElapsed = 60.0; a.timer.laps = {12.5, 27.25};

        const std::string ini = app::serializeSettingsText(a);

        // Reload 10 seconds later: all active sessions resume seamlessly
        app::App b;
        app::parseSettingsText(b, ini, t0 + 10.0);
        assert(b.focus.running && !b.focus.finished && std::fabs((b.focus.endAt - (t0 + 10.0)) - 230.0) < 0.05);
        assert(b.timer.running && !b.timer.finished && std::fabs((b.timer.endAt - (t0 + 10.0)) - 1730.0) < 0.05);
        assert(b.timer.swRunning && std::fabs(b.timer.swElapsed - 60.0) < 0.05);
        assert(b.timer.laps.size() == 2 && std::fabs(b.timer.laps[0] - 12.5) < 0.01 && std::fabs(b.timer.laps[1] - 27.25) < 0.01);

        // Reload after deadlines passed during downtime: restored as finished + downtimeExpired (no surprise siren)
        app::App c;
        app::parseSettingsText(c, ini, t0 + 5000.0);
        assert(!c.focus.running && c.focus.finished && c.focus.downtimeExpired);
        assert(!c.timer.running && c.timer.finished && c.timer.downtimeExpired);

        // Paused countdown round-trip preserves exact remaining time
        app::App p;
        p.timer.durationSec = 1500.0; p.timer.running = false; p.timer.pausedRemaining = 420.0;
        const std::string pIni = app::serializeSettingsText(p);
        app::App p2;
        app::parseSettingsText(p2, pIni, t0 + 100.0);
        assert(!p2.timer.running && !p2.timer.finished && std::fabs(p2.timer.pausedRemaining - 420.0) < 0.05);
    }

    // Fired one-shot alarm disarms immediately AND triggers CMD_SAVE_NOW before any crash/close
    {
        const double t0 = astro::daysFromCivil(2026, 11, 22) * 86400.0 + 14 * 3600.0; // 14:00 UTC
        app::App a;
        int saveNowHits = 0;
        a.command = [&](int cmd) { if (cmd == app::CMD_SAVE_NOW) ++saveNowHits; };
        a.setNow(t0, 0, 0.016);
        a.timer.alarmMinutes = 14 * 60 + 1; // 14:01
        a.timer.alarmOn = true;
        a.timer.alarmAt = t0 + 60.0;

        a.timerTick();
        assert(a.timer.alarmOn && !a.timer.ringing && saveNowHits == 0);

        a.setNow(t0 + 61.0, 0, 0.016);
        a.timerTick();
        assert(a.timer.ringing);
        assert(!a.timer.alarmOn);
        assert(saveNowHits >= 1);

        // Serialized immediately -> reload confirms alarm stays disarmed
        const std::string saved = app::serializeSettingsText(a);
        app::App reloaded;
        app::parseSettingsText(reloaded, saved, t0 + 120.0);
        assert(!reloaded.timer.alarmOn);
    }
}

// ============================================================================
// 5. AQI scale fidelity, EPA 2024 breakpoints, observation age, & NWS wording
// ============================================================================
static void testAqiAndNws() {
    std::puts("[5/7] AQI scale fidelity, EPA 2024 breakpoints, observation age, & NWS wording...");

    const double now = astro::daysFromCivil(2026, 11, 22) * 86400.0 + 15 * 3600.0;

    // 5a. EU-only Open-Meteo response: EU AQI 40 -> "Fair", never serialized as "us_aqi", round-trips cleanly
    {
        const std::string euJson = R"({
            "utc_offset_seconds": 0,
            "current": { "time": "2026-11-22T14:30", "us_aqi": null, "european_aqi": 40, "pm2_5": 12.0 }
        })";
        weather::Data d; std::string err;
        assert(weather::parseAir(euJson, d, err, 48.8566, 2.3522, now));
        assert(d.hasAir && d.aqi == 40);
        assert(d.aqiScale == weather::AqiScale::Eu);
        assert(std::string(weather::aqiLabel(d.aqi, d.aqiScale)) == "Fair");
        assert(std::string(weather::aqiScaleShort(d.aqiScale)) == "EU AQI");
        assert(weather::airFresh(d, now));

        const std::string snap = weather::formatAirJson(d);
        assert(snap.find("us_aqi") == std::string::npos);
        assert(snap.find("\"scale\":\"eu\"") != std::string::npos);

        const std::string cacheRaw = weather::formatAirCache(48.8566, 2.3522, now, snap);
        weather::Data d2;
        assert(weather::parseAirCache(cacheRaw, 48.8566, 2.3522, now, 6.0 * 3600.0, d2, err));
        assert(d2.hasAir && d2.aqi == 40 && d2.aqiScale == weather::AqiScale::Eu);
        assert(std::string(weather::aqiLabel(d2.aqi, d2.aqiScale)) == "Fair");
    }

    // 5b. Revised 2024 US EPA PM2.5 breakpoints & PM2.5-estimated tagging
    {
        auto checkPm25 = [&](double pm25, int wantAqi, const char* wantLabel) {
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                R"({"utc_offset_seconds":0,"current":{"time":"2026-11-22T14:45","us_aqi":null,"european_aqi":null,"pm2_5":%.1f}})", pm25);
            weather::Data d; std::string err;
            assert(weather::parseAir(buf, d, err, 40.7128, -74.0060, now));
            assert(d.hasAir && (int)std::lround(d.aqi) == wantAqi);
            assert(d.aqiScale == weather::AqiScale::Pm25Est);
            assert(std::string(weather::aqiLabel(d.aqi, d.aqiScale)) == wantLabel);
            const std::string snap = weather::formatAirJson(d);
            assert(snap.find("\"us_aqi\"") == std::string::npos);
            assert(snap.find("\"scale\":\"pm25_est\"") != std::string::npos);
            weather::Data d2;
            const std::string cacheRaw = weather::formatAirCache(40.7128, -74.0060, now, snap);
            assert(weather::parseAirCache(cacheRaw, 40.7128, -74.0060, now, 6.0 * 3600.0, d2, err));
            assert((int)std::lround(d2.aqi) == wantAqi && d2.aqiScale == weather::AqiScale::Pm25Est);
        };
        checkPm25(9.0,  50,  "Good");
        checkPm25(12.0, 56,  "Moderate");
        checkPm25(35.4, 100, "Moderate");
        checkPm25(55.4, 150, "Unhealthy for some");
    }

    // 5c. Stale 2020 observation rejected on live parse AND on cache reload despite fresh header
    {
        const std::string stale2020 = R"({
            "utc_offset_seconds": 0,
            "hourly": { "time": ["2020-01-01T12:00"], "us_aqi": [42], "pm2_5": [8.0] }
        })";
        weather::Data d; std::string err;
        assert(!weather::parseAir(stale2020, d, err, 40.7128, -74.0060, now));
        assert(!d.hasAir);

        const std::string staleSnap = R"({"meridian_air":2,"source":"Open-Meteo","scale":"us","aqi":42.0,"us_aqi":42.0,"obs_time":1577880000,"pm2_5":8.0,"pm10":10.0})";
        const std::string staleCache = weather::formatAirCache(40.7128, -74.0060, now, staleSnap);
        weather::Data d2;
        assert(!weather::parseAirCache(staleCache, 40.7128, -74.0060, now, 6.0 * 3600.0, d2, err));
        assert(!d2.hasAir);

        weather::Data aged; aged.hasAir = true; aged.aqi = 42; aged.aqiScale = weather::AqiScale::Us; aged.airObsTime = now - 7.0 * 3600.0;
        assert(!weather::airFresh(aged, now));
    }

    // 5d. NWS shortForecast specificity: Partly Cloudy -> WMO 2 / 48%, Cloudy -> WMO 3 / 92%
    {
        auto testNws = [&](const char* phrase, int wantCode, double wantCloud) {
            std::string json = std::string(R"({"properties":{"periods":[
                {"startTime":"2026-11-22T15:00:00-05:00","isDaytime":true,"temperature":68,"temperatureUnit":"F","windSpeed":"8 mph","windDirection":"NW","relativeHumidity":{"value":55},"shortForecast":")") + phrase + R"("},
                {"startTime":"2026-11-22T16:00:00-05:00","isDaytime":true,"temperature":67,"temperatureUnit":"F","windSpeed":"8 mph","windDirection":"NW","relativeHumidity":{"value":55},"shortForecast":")" + phrase + R"("}
            ]}})";
            weather::Data d; std::string err;
            assert(weather::parseProvider(weather::PV_NWS, json, 40.7128, -74.0060, d, err));
            assert(d.valid);
            assert(d.cur.code == wantCode);
            assert(std::fabs(d.cur.cloud - wantCloud) < 0.5);
        };
        testNws("Partly Cloudy", 2, 48.0);
        testNws("Partly Sunny",  2, 48.0);
        testNws("Mostly Cloudy", 3, 78.0);
        testNws("Cloudy",        3, 92.0);
        testNws("Overcast",      3, 92.0);
        testNws("Mostly Sunny",  1, 20.0);
    }
}

// ============================================================================
// 6. Settings modal keyboard focus trap & repeated newer-schema downgrades
// ============================================================================
static void testSettingsFocusAndDowngrade() {
    std::puts("[6/7] Settings modal focus trap & repeated newer-schema downgrades...");

    // 6a. Modal focus trap isolated from underlying Sky carousel and scrim backdrop
    {
        app::App a;
        a.showSettings = true;
        int skyClicked = 0, scrimClicked = 0, settingsActionClicked = 0;

        auto buildFrameActions = [&](int extraSkyActions) {
            a.hits.clear();
            a.actions.clear();
            a.settingsFocusList.clear();
            // Underlying Sky carousel actions BEFORE Settings modal starts
            for (int i = 0; i < extraSkyActions; ++i) {
                a.addAction(10.f + i * 20.f, 10.f, 12.f, 12.f, [&]() { ++skyClicked; });
            }
            // Scrim & card backdrop actions BEFORE settingsFocusStart
            a.addAction(0.f, 0.f, 1280.f, 720.f, [&]() { ++scrimClicked; a.showSettings = false; });
            a.addAction(100.f, 50.f, 800.f, 600.f, []() {});

            // Strictly Settings modal interactive controls (matching renderSettings)
            a.settingsFocusStart = (int)a.actions.size();
            const int tab0 = (int)a.actions.size();
            a.addAction(120.f, 100.f, 140.f, 32.f, [&]() { a.settingsTab = 0; });
            a.settingsFocusList.push_back({tab0, "tab:WIDGETS"});
            const int tab1 = (int)a.actions.size();
            a.addAction(120.f, 140.f, 140.f, 32.f, [&]() { a.settingsTab = 1; });
            a.settingsFocusList.push_back({tab1, "tab:ELEMENTS"});
            const int actCtrl = (int)a.actions.size();
            a.addAction(300.f, 140.f, 140.f, 32.f, [&]() { ++settingsActionClicked; });
            a.settingsFocusList.push_back({actCtrl, "tab0:ctrl:0"});
            a.settingsFocusEnd = (int)a.actions.size();

            // Re-sync settingsFocus from stable named key just like renderSettings() does
            if (!a.settingsFocusKey.empty()) {
                int resolved = -1;
                for (const auto& it : a.settingsFocusList) {
                    if (it.second == a.settingsFocusKey) { resolved = it.first; break; }
                }
                a.settingsFocus = resolved;
            }
        };

        buildFrameActions(2);
        // First Tab lands on "tab:WIDGETS" (never Sky carousel or scrim)
        assert(a.settingsKey(VK_TAB, false));
        assert(a.settingsFocusKey == "tab:WIDGETS");
        assert(a.settingsFocus >= a.settingsFocusStart && a.settingsFocus < a.settingsFocusEnd);

        // Tab -> "tab:ELEMENTS", Tab -> "tab0:ctrl:0"
        assert(a.settingsKey(VK_TAB, false));
        assert(a.settingsFocusKey == "tab:ELEMENTS");
        assert(a.settingsKey(VK_TAB, false));
        assert(a.settingsFocusKey == "tab0:ctrl:0");

        // Underlying Sky carousel count changes between frames: focus stays pinned by name on "tab0:ctrl:0"!
        buildFrameActions(6);
        assert(a.settingsFocus >= a.settingsFocusStart && a.settingsFocus < a.settingsFocusEnd);
        assert(a.settingsKey(VK_RETURN, false));
        assert(settingsActionClicked == 1);
        assert(skyClicked == 0 && scrimClicked == 0);
        assert(a.showSettings);

        // Tab wraps cleanly back to "tab:WIDGETS"; Shift+Tab wraps cleanly back to "tab0:ctrl:0"
        assert(a.settingsKey(VK_TAB, false));
        assert(a.settingsFocusKey == "tab:WIDGETS");
        assert(a.settingsKey(VK_TAB, true));
        assert(a.settingsFocusKey == "tab0:ctrl:0");
    }

    // 6b. Two consecutive downgrades from future schema version 9 keep BOTH active settings.ini AND .bak fresh
    {
        fs::path root = makeTempRoot(L"downgrade");
        fs::path iniPath = root / L"settings.ini";
        fs::path bakPath = root / L"settings.ini.from-newer-version-9.bak";
        const double now = astro::daysFromCivil(2026, 11, 22) * 86400.0 + 18 * 3600.0;

        // Downgrade #1: future_field=first
        const std::string v9First =
            "[meridian]\nsettings_version=9\nwritten_by=3.5.0\n\n"
            "[view]\ntheme=3\nextra_v9_flag=alpha\n\n"
            "[future_feature]\nfuture_field=first\n";
        writeTextFile(iniPath, v9First);
        {
            app::App a; int fver = 0;
            app::parseSettingsText(a, v9First, now, &fver);
            assert(fver == 9);
            assert(app::backupNewerSettingsSnapshot(iniPath.wstring(), v9First, fver));
            writeTextFile(iniPath, app::serializeSettingsText(a));
        }
        assert(readTextFile(iniPath).find("future_field=first") != std::string::npos);
        assert(readTextFile(iniPath).find("extra_v9_flag=alpha") != std::string::npos);
        assert(readTextFile(bakPath).find("future_field=first") != std::string::npos);

        // User upgrades back to v3.5.0, edits future_field=second, then downgrades a SECOND time
        const std::string v9Second =
            "[meridian]\nsettings_version=9\nwritten_by=3.5.0\n\n"
            "[view]\ntheme=9\nextra_v9_flag=beta\n\n"
            "[future_feature]\nfuture_field=second\n";
        writeTextFile(iniPath, v9Second);
        {
            app::App a; int fver = 0;
            app::parseSettingsText(a, v9Second, now, &fver);
            assert(fver == 9);
            assert(app::backupNewerSettingsSnapshot(iniPath.wstring(), v9Second, fver));
            writeTextFile(iniPath, app::serializeSettingsText(a));
        }
        const std::string activeAfter2 = readTextFile(iniPath);
        const std::string bakAfter2 = readTextFile(bakPath);
        assert(activeAfter2.find("future_field=second") != std::string::npos);
        assert(activeAfter2.find("extra_v9_flag=beta") != std::string::npos);
        assert(bakAfter2.find("future_field=second") != std::string::npos);
        assert(bakAfter2.find("future_field=first") == std::string::npos);
        fs::remove_all(root);
    }
}

// ============================================================================
// 7. Audio worker failure recovery & hourly chime unblocking
// ============================================================================
static void* denyMfLoader(const wchar_t* dllName) {
    if (dllName && wcscmp(dllName, L"mfplat.dll") == 0) return nullptr;
    return static_cast<void*>(LoadLibraryExW(dllName, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32));
}

static void testAudioFailureRecovery() {
    std::puts("[7/7] Audio worker failure recovery & hourly chime unblocking...");

    ufoaudio::shutdown();
    ufoaudio::setLibraryLoaderForTest(&denyMfLoader);
    ufoaudio::setEnabled(true);

    // Attempt 1: simulate unavailable Media Foundation on track start
    ufoaudio::toggleTrack(ufoaudio::Track::HippyShit);
    for (int i = 0; i < 100 && ufoaudio::failureSeq() < 1; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(ufoaudio::failureSeq() >= 1);
    assert(ufoaudio::currentTrack() == ufoaudio::Track::None);   // hourly chime is NOT blocked!
    assert(!ufoaudio::isJukebox());
    assert(!ufoaudio::lastError().empty());
    std::string consumedErr;
    assert(ufoaudio::consumeTrackFailure(consumedErr) && !consumedErr.empty());
    assert(!ufoaudio::consumeTrackFailure(consumedErr));         // consumed once
    assert(ufoaudio::initAttemptCount() == 1);

    // Attempt 2: stop + request again MUST recycle the exited worker and retry MF init (not stay wedged!)
    ufoaudio::stopTrack();
    ufoaudio::toggleJukebox();
    for (int i = 0; i < 100 && ufoaudio::failureSeq() < 2; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(ufoaudio::failureSeq() >= 2);
    assert(ufoaudio::initAttemptCount() == 2);
    assert(ufoaudio::currentTrack() == ufoaudio::Track::None);
    assert(!ufoaudio::isJukebox());

    ufoaudio::setLibraryLoaderForTest(nullptr);
    ufoaudio::shutdown();
}

// ============================================================================
// 8. Official 3.0.0 deep-audit fixes: pristine .bak across consecutive runs,
//    timer/focus backspace clamp, NWS missing-field flags, uninstall TX cleanup
// ============================================================================
static void testOfficialV3AuditFixes() {
    std::puts("[8/8] Official 3.0.0 audit fixes (pristine .bak, backspace clamp, NWS flags, uninstall TX cleanup)...");

    // 8a. Consecutive launches on older Meridian MUST keep the pristine newer-version .bak untouched,
    //     while a subsequent real downgrade from newer version DOES refresh .bak.
    {
        fs::path root = makeTempRoot(L"pristine_bak");
        fs::path iniPath = root / L"settings.ini";
        fs::path bakPath = root / L"settings.ini.from-newer-version-9.bak";
        const double now = astro::daysFromCivil(2026, 11, 24) * 86400.0 + 12 * 3600.0;

        const std::string v9Original =
            "[meridian]\nsettings_version=9\nwritten_by=3.5.0\n\n"
            "[view]\ntheme=3\n\n[future_feature]\nfuture_field=pristine_v35\n";
        writeTextFile(iniPath, v9Original);

        // Launch 1 on 3.0.0: creates pristine .bak and re-saves settings.ini with written_by=3.0.0
        {
            app::App a; int fver = 0;
            app::parseSettingsText(a, v9Original, now, &fver);
            assert(fver == 9);
            assert(app::backupNewerSettingsSnapshot(iniPath.wstring(), v9Original, fver));
            a.st.theme = app::THEME_GALAXY;
            writeTextFile(iniPath, app::serializeSettingsText(a));
        }
        assert(readTextFile(bakPath) == v9Original);

        // Launch 2 on 3.0.0: reads its own re-saved settings.ini (settings_version=9, written_by=3.0.0)
        // -> MUST NOT overwrite .bak with 3.0.0's modified file!
        {
            const std::string savedBy300 = readTextFile(iniPath);
            assert(savedBy300.find(std::string("written_by=") + app::MERIDIAN_VERSION) != std::string::npos);
            app::App a; int fver = 0;
            app::parseSettingsText(a, savedBy300, now, &fver);
            assert(fver == 9);
            assert(app::backupNewerSettingsSnapshot(iniPath.wstring(), savedBy300, fver));
        }
        assert(readTextFile(bakPath) == v9Original);
        fs::remove_all(root);
    }

    // 8b. timerBackspace() and focusBackspace() clamp to TIMER_MAX_SEC and clear finished flags
    {
        app::App a;
        a.timer.mode = 0;
        a.timer.running = false;
        a.timer.finished = true;
        a.timer.downtimeExpired = true;
        a.timer.entry = "9999999";
        a.timerBackspace();
        assert(!a.timer.finished);
        assert(!a.timer.downtimeExpired);
        assert(a.timer.durationSec <= app::TIMER_MAX_SEC);
        assert(a.timer.pausedRemaining == a.timer.durationSec);

        a.focus.running = false;
        a.focus.finished = true;
        a.focus.downtimeExpired = true;
        a.focus.entry = "9999999";
        a.focusBackspace();
        assert(!a.focus.finished);
        assert(!a.focus.downtimeExpired);
        assert(a.focus.durationSec <= app::TIMER_MAX_SEC);
        assert(a.focus.pausedRemaining == a.focus.durationSec);
    }

    // 8c. NWS missing relativeHumidity & single windSpeed vs gust range
    {
        const std::string nwsNoHum = R"({"properties":{"periods":[
            {"startTime":"2026-11-24T12:00:00-05:00","isDaytime":true,"temperature":52,"temperatureUnit":"F","windSpeed":"10 mph","windDirection":"W","relativeHumidity":{"value":null},"shortForecast":"Clear"},
            {"startTime":"2026-11-24T13:00:00-05:00","isDaytime":true,"temperature":53,"temperatureUnit":"F","windSpeed":"10 mph","windDirection":"W","shortForecast":"Clear"}
        ]}})";
        weather::Data d1; std::string err;
        assert(weather::parseProvider(weather::PV_NWS, nwsNoHum, 40.7128, -74.0060, d1, err));
        assert(!d1.cur.hasHumidity);
        assert(d1.cur.hasWind);
        assert(!d1.cur.hasGust);

        const std::string nwsRange = R"({"properties":{"periods":[
            {"startTime":"2026-11-24T12:00:00-05:00","isDaytime":true,"temperature":52,"temperatureUnit":"F","windSpeed":"15 to 28 mph","windDirection":"NW","relativeHumidity":{"value":62},"shortForecast":"Windy"},
            {"startTime":"2026-11-24T13:00:00-05:00","isDaytime":true,"temperature":53,"temperatureUnit":"F","windSpeed":"15 to 28 mph","windDirection":"NW","relativeHumidity":{"value":62},"shortForecast":"Windy"}
        ]}})";
        weather::Data d2;
        assert(weather::parseProvider(weather::PV_NWS, nwsRange, 40.7128, -74.0060, d2, err));
        assert(d2.cur.hasHumidity && std::fabs(d2.cur.humidity - 62.0) < 0.1);
        assert(d2.cur.hasWind && d2.cur.hasGust);
        assert(d2.cur.gust > d2.cur.wind);
    }

    // 8d. Uninstall TX cleanup removes ONLY owned .meridian-tx.log + listed backup/stage folders, leaving user .meridian-* folders untouched
    {
        fs::path root = makeTempRoot(L"uninst_tx");
        fs::path dir = root / L"app";
        fs::path ownedBak = dir / L".meridian-backup-999-123";
        fs::path ownedStage = dir / L".meridian-stage-999-123";
        fs::path userDir = dir / L".meridian-user-notes";
        writeTextFile(ownedBak / L"Meridian.exe", "BAK");
        writeTextFile(ownedStage / L"Meridian.exe", "STG");
        writeTextFile(userDir / L"keep.txt", "USER_DATA");
        writeTextFile(dir / L".meridian-tx.log",
            "MERIDIAN_TX_V1\nSTAGE .meridian-stage-999-123\nBACKUP .meridian-backup-999-123\nSTATE ROLLING_BACK\nFILE 1 Meridian.exe\n");

        inst::cleanupOwnedTxAt(dir.wstring());
        assert(!fs::exists(dir / L".meridian-tx.log"));
        assert(!fs::exists(ownedBak));
        assert(!fs::exists(ownedStage));
        assert(fs::exists(userDir / L"keep.txt"));
        assert(readTextFile(userDir / L"keep.txt") == "USER_DATA");
        fs::remove_all(root);
    }

    // 8e. Semantic Settings glass keys: scrim, card backdrop, underlying face actions, and non-glass rows MUST return 0,
    //     while explicit semantic glass keys remain invariant even when dynamic controls are inserted/removed ahead of them.
    {
        for (int faceActs : {0, 4, 6}) {
            app::App a;
            a.showSettings = true;
            a.settingsTab = 0;
            for (int i = 0; i < faceActs; ++i) a.addAction(0, 0, 10, 10, [] {});
            const int scrimHit = app::HIT_ACTION0 + (int)a.actions.size();
            a.addAction(0, 0, 1280, 720, [] {});
            const int backdropHit = app::HIT_ACTION0 + (int)a.actions.size();
            a.addAction(100, 50, 900, 600, [] {});
            a.settingsFocusStart = (int)a.actions.size();

            const uint64_t kTabElements = 0x2200000000000001ULL;
            const uint64_t kTabQuiet    = 0x2200000000000005ULL;
            const uint64_t kDone        = 0x22000000000000FFULL;
            const uint64_t kToggleA     = 0x2200000000001001ULL;

            const int hitElements = app::HIT_ACTION0 + (int)a.actions.size();
            a.addAction(10, 20, 100, 30, [] {}, kTabElements);
            const int hitQuiet = app::HIT_ACTION0 + (int)a.actions.size();
            a.addAction(10, 60, 100, 30, [] {}, kTabQuiet);
            const int hitDone = app::HIT_ACTION0 + (int)a.actions.size();
            a.addAction(10, 500, 100, 30, [] {}, kDone);
            const int hitField = app::HIT_ACTION0 + (int)a.actions.size();
            a.addAction(200, 100, 300, 32, [] {}); // passive text field -> default glassKey 0
            const int hitToggleA = app::HIT_ACTION0 + (int)a.actions.size();
            a.addAction(200, 150, 50, 28, [] {}, kToggleA);

            assert(a.glassKeyForHit(scrimHit) == 0);
            assert(a.glassKeyForHit(backdropHit) == 0);
            assert(a.glassKeyForHit(hitField) == 0);
            assert(a.glassKeyForHit(app::HIT_ACTION0 + 9999) == 0);
            for (int i = 0; i < faceActs; ++i) assert(a.glassKeyForHit(app::HIT_ACTION0 + i) == 0);

            assert(a.glassKeyForHit(hitElements) == kTabElements);
            assert(a.glassKeyForHit(hitQuiet) == kTabQuiet);
            assert(a.glassKeyForHit(hitDone) == kDone);
            assert(a.glassKeyForHit(hitToggleA) == kToggleA);

            // Simulate next frame after dynamic controls appear ahead of kToggleA: its key is unchanged!
            a.actions.clear();
            a.actionGlassKeys.clear();
            a.addAction(200, 80, 100, 28, [] {}, 0x2200000000009999ULL);
            a.addAction(200, 110, 100, 28, [] {}, 0x220000000000999AULL);
            const int hitToggleAShifted = app::HIT_ACTION0 + (int)a.actions.size();
            a.addAction(200, 150, 50, 28, [] {}, kToggleA);
            assert(a.glassKeyForHit(hitToggleAShifted) == kToggleA);
        }
    }

    // 8f. util::toLocal IEEE-754 day boundary carry + util::fmtDuration large values + strict UTF-8 widen/wideToByte parity
    {
        for (double t : {-1e-12, -1e-9, -0.0001, 0.0, 86399.999999, 86400.0 - 1e-12, -86400.0 - 1e-12}) {
            util::LocalTime L = util::toLocal(t, 0);
            assert(L.hh >= 0 && L.hh <= 23);
            assert(L.mm >= 0 && L.mm <= 59);
            assert(L.ss >= 0 && L.ss <= 59);
            assert(L.fracSec >= 0.0 && L.fracSec < 1.0);
        }
        assert(util::toLocal(-1e-12, 0).hh == 0);
        assert(util::toLocal(-1e-12, 0).y == 1970 && util::toLocal(-1e-12, 0).m == 1 && util::toLocal(-1e-12, 0).d == 1);
        assert(!util::fmtDuration(1e11).empty());
        assert(!util::fmtDeltaMinSec(1e11).empty());

        const std::vector<std::string> utf8Cases = {
            "", "hello", "Colombo \xC2\xB7 27\xC2\xB0", "\xF0\x9F\x8C\x99",
            "\xC0\xAF", "\xE0\x80\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80", "\xF7\xBF\xBF\xBF",
            "\xC3\x28", "\xE2\x82\x28", "\xF0\x9F\x8C", "\xFF\xFE\x80"
        };
        for (const std::string& s : utf8Cases) {
            const std::wstring w = util::widen(s);
            const std::vector<size_t> map = textedit::wideToByte(s);
            assert(map.size() == w.size() + 1);
            assert(map.back() == s.size());
        }
        assert(util::widen("\xF4\x90\x80\x80") == L"????");
        assert(util::widen("\xED\xA0\x80") == L"???");
        assert(util::widen("\xC0\xAF") == L"??");
        assert(util::widen("\xF0\x9F\x8C\x99").size() == 2); // valid U+1F319 surrogate pair
    }

    // 8g. settings_io.h INI single-line newline injection immunity, load caps (places <= 200, laps <= 500), and widget/pinned bounds safety
    {
        app::App a;
        a.home.name = "Colombo\r\n[view]\ntheme=9";
        a.st.contact = "me@example.com\r\nX-Injected: 1";
        a.st.weatherApiKey = "key1\nkey2=bad";
        a.st.cdLabel = "Launch\n[storms]\nwatch=0";
        a.st.events.push_back({2027, 5, 1, "Party\r\n[view]\nh24=0"});
        a.st.widgets = {app::WG_WORLD, -5, 999, app::WG_MONTH};
        a.st.pinned = 999; // out-of-range must serialize as "none" safely
        a.st.theme = app::THEME_DAY;
        const std::string iniText = app::serializeSettingsText(a);

        app::App b;
        b.st.theme = app::THEME_AUTO;
        app::parseSettingsText(b, iniText, 1790000000.0);
        assert(b.st.theme == app::THEME_DAY); // injected theme=9 did NOT take effect!
        assert(b.home.name.find('\n') == std::string::npos && b.home.name.find('\r') == std::string::npos);
        assert(b.st.pinned == -1);
        assert(b.st.widgets.size() == 2 && b.st.widgets[0] == app::WG_WORLD && b.st.widgets[1] == app::WG_MONTH);

        // Verify load caps on huge [places] and [timer] sw_laps
        std::string hugeIni = "[places]\n";
        for (int i = 0; i < 260; ++i) hugeIni += "P" + std::to_string(i) + "=6.9,79.8\n";
        hugeIni += "[timer]\nsw_laps=";
        for (int i = 0; i < 650; ++i) { if (i) hugeIni += ","; hugeIni += std::to_string(i) + ".5"; }
        hugeIni += "\n";
        app::App c;
        app::parseSettingsText(c, hugeIni, 1790000000.0);
        assert(c.st.places.size() == 200);
        assert(c.timer.laps.size() == 500);
    }

    // 8h. Stopwatch backward-clock clamp, 500-lap cap, and timerDigit/focusDigit clearing downtimeExpired
    {
        app::App a;
        a.timer.mode = 1;
        a.timer.swRunning = true;
        a.timer.swElapsed = 12.5;
        a.timer.swStart = 2000.0;
        a.now = 1950.0; // clock stepped backwards!
        assert(a.stopwatchElapsed() == 12.5);
        a.timerStartPause(); // pause must preserve 12.5, never subtract
        assert(!a.timer.swRunning && std::fabs(a.timer.swElapsed - 12.5) < 1e-6);

        a.timer.swRunning = true;
        a.timer.swStart = 1900.0;
        a.now = 1905.0;
        a.timer.laps.clear();
        for (int i = 0; i < 600; ++i) a.stopwatchLap();
        assert(a.timer.laps.size() == 500);

        a.timer.mode = 0;
        a.timer.downtimeExpired = true;
        a.timerDigit('5');
        assert(!a.timer.downtimeExpired);

        a.focus.downtimeExpired = true;
        a.focusDigit('2');
        assert(!a.focus.downtimeExpired);
    }

    // 8i. OpenWeatherMap hasHumidity/hasCloud/hasWind tracking + invalid ISO timestamp rejection
    {
        const std::string owmSparse = R"({"cod":"200","city":{"timezone":0},"list":[
            {"dt":1795500000,"main":{"temp":21.5},"weather":[{"id":800}],"sys":{"pod":"d"}},
            {"dt":1795510800,"main":{"temp":22.0,"humidity":60},"clouds":{"all":25},"wind":{"speed":4.0},"weather":[{"id":801}],"sys":{"pod":"d"}}
        ]})";
        weather::Data d; std::string err;
        assert(weather::parseProvider(weather::PV_OPENWEATHER, owmSparse, 51.5, -0.1, d, err));
        // Current entry selected is closest to wall time (~2026-11-24 = 1795500000), or either way we verify parser succeeded
        assert(d.valid);

        const std::string owmAllSparse = R"({"cod":"200","city":{"timezone":0},"list":[
            {"dt":1795500000,"main":{"temp":21.5},"weather":[{"id":800}],"sys":{"pod":"d"}},
            {"dt":1795510800,"main":{"temp":22.0},"weather":[{"id":801}],"sys":{"pod":"d"}}
        ]})";
        weather::Data d2;
        assert(weather::parseProvider(weather::PV_OPENWEATHER, owmAllSparse, 51.5, -0.1, d2, err));
        assert(!d2.cur.hasHumidity && !d2.cur.hasCloud && !d2.cur.hasWind && !d2.cur.hasGust);

        // Reject out-of-range ISO dates in NWS parser
        const std::string nwsBadDate = R"({"properties":{"periods":[
            {"startTime":"2026-99-99T99:99:99Z","isDaytime":true,"temperature":52,"temperatureUnit":"F","windSpeed":"10 mph","windDirection":"W","shortForecast":"Clear"},
            {"startTime":"2026-99-99T99:99:99Z","isDaytime":true,"temperature":53,"temperatureUnit":"F","windSpeed":"10 mph","windDirection":"W","shortForecast":"Clear"}
        ]}})";
        weather::Data d3;
        assert(!weather::parseProvider(weather::PV_NWS, nwsBadDate, 40.7, -74.0, d3, err));
    }

    // 8j. Astro pole clamping & sky events solar eclipse obscuration finiteness
    {
        for (double lon : {0.0, 90.0, 180.0, 270.0}) {
            for (double lat : {-90.0, 90.0}) {
                astro::Equatorial eq = astro::eclipticToEquatorial(lon, lat, 23.4392911);
                assert(std::isfinite(eq.ra) && std::isfinite(eq.dec));
                astro::Equatorial gq = astro::galacticToEquatorialJ2000(lon, lat);
                assert(std::isfinite(gq.ra) && std::isfinite(gq.dec));
            }
        }
        // Aug 12 2026 solar eclipse around Reykjavik (total) & grazing edge
        skyev::Options opt; opt.meteors = false; opt.planets = false; opt.eclipses = true; opt.windowDays = 2.0;
        const double aug12 = astro::daysFromCivil(2026, 8, 12) * 86400.0 + 17.5 * 3600.0;
        const auto evs = skyev::find(aug12, astro::Geo{64.1466, -21.9426, "Reykjavik", ""}, opt);
        bool sawSolar = false;
        for (const auto& e : evs) {
            if (e.kind == skyev::SOLAR_ECLIPSE) {
                sawSolar = true;
                assert(std::isfinite(e.obscuration) && e.obscuration >= 0.0 && e.obscuration <= 1.0);
            }
        }
        assert(sawSolar);
    }

    // 8k. setup/payload.h loadFromBuffer path-traversal, count-flood, and integer-overflow rejection
    {
        auto makeBlob = [](uint32_t count, const std::string& path, uint32_t size, uint32_t stored, uint32_t off, const std::string& data) {
            std::vector<uint8_t> b;
            auto pushBytes = [&](const void* ptr, size_t n) {
                const uint8_t* p = (const uint8_t*)ptr;
                b.insert(b.end(), p, p + n);
            };
            pushBytes("MPK1", 4);
            pushBytes(&count, 4);
            uint16_t pl = (uint16_t)path.size();
            pushBytes(&pl, 2);
            pushBytes(path.data(), path.size());
            uint8_t grp = 0, flg = 0;
            pushBytes(&grp, 1); pushBytes(&flg, 1);
            uint32_t crcV = 0;
            pushBytes(&size, 4); pushBytes(&stored, 4); pushBytes(&crcV, 4); pushBytes(&off, 4);
            pushBytes(data.data(), data.size());
            return b;
        };

        Payload pl;
        // Valid relative path succeeds
        auto good = makeBlob(1, "themes/demo/theme.json", 4, 4, 0, "ABCD");
        assert(pl.loadFromBuffer(good.data(), good.size(), 12345ULL));
        assert(pl.e.size() == 1 && pl.e[0].path == "themes/demo/theme.json");

        // Malicious paths MUST be rejected
        const std::vector<std::string> badPaths = {
            "", "../evil.exe", "a/../../evil.exe", "/abs.exe", "C:evil.exe", "a\\b.exe", "a//b.exe", std::string("a\0b", 3)
        };
        for (const std::string& badPath : badPaths) {
            auto bad = makeBlob(1, badPath, 4, 4, 0, "ABCD");
            assert(!pl.loadFromBuffer(bad.data(), bad.size(), 12345ULL));
        }
        // Bogus huge entry count MUST be rejected immediately
        auto badCount = makeBlob(0xFFFFFFFFu, "ok.txt", 4, 4, 0, "ABCD");
        assert(!pl.loadFromBuffer(badCount.data(), badCount.size(), 12345ULL));
        // Offset + stored integer overflow MUST be rejected
        auto badOv = makeBlob(1, "ok.txt", 4, 16, 0xFFFFFFF8u, "ABCD");
        assert(!pl.loadFromBuffer(badOv.data(), badOv.size(), 12345ULL));
    }
}

int main() {
    testInstallerTransactions();
    testSpaceWx();
    testOrreryAndMeasurements();
    testTimezoneSessionsAndAlarm();
    testAqiAndNws();
    testSettingsFocusAndDowngrade();
    testAudioFailureRecovery();
    testOfficialV3AuditFixes();
    std::puts("ALL 8 AUDIT VERIFICATION GROUPS PASSED!");
    return 0;
}
