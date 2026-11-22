// Meridian: an always-on clock for Windows. Window, input, settings, weather worker, chime.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <dwmapi.h>
#include <objbase.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <cstdio>
#include "scene.h"
#include "resource.h"
#include "locate.h"
#include "clockguard.h"
#include "robust.h"
#include "saver.h"
#include "textesc.h"
#include "textedit.h"
#include "ufo_audio.h"
#include "ufo_audio_codec.h"
#include <memory>

using namespace app;
using util::fmt;

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#define MY_DWMWA_BORDER_COLOR 34
#define MY_DWMWA_CAPTION_COLOR 35
#define MY_DWMWA_TEXT_COLOR 36

namespace {

// ---------------------------------------------------------------- files
std::wstring envVar(const wchar_t* n) {
    wchar_t buf[MAX_PATH * 2]; DWORD k = GetEnvironmentVariableW(n, buf, MAX_PATH * 2);
    return k > 0 && k < MAX_PATH * 2 ? std::wstring(buf, k) : L"";
}
std::wstring dataDir(bool local) {
    std::wstring base = envVar(local ? L"LOCALAPPDATA" : L"APPDATA");
    if (base.empty()) base = envVar(L"TEMP");
    std::wstring d = base + L"\\Meridian";
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}
bool readFile(const std::wstring& p, std::string& out) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{}; GetFileSizeEx(h, &sz);
    if (sz.QuadPart > 8 * 1024 * 1024) { CloseHandle(h); return false; }
    out.assign((size_t)sz.QuadPart, '\0'); DWORD got = 0;
    bool ok = sz.QuadPart == 0 || ReadFile(h, &out[0], (DWORD)sz.QuadPart, &got, nullptr);
    CloseHandle(h); out.resize(got);
    return ok;
}
bool writeFile(const std::wstring& p, const std::string& data) {
    return robust::atomicWrite(p, data);                 // a power cut can no longer leave a half-written settings.ini or forecast cache
}
double fileMTime(const std::wstring& p) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a)) return 0;
    ULARGE_INTEGER u; u.LowPart = a.ftLastWriteTime.dwLowDateTime; u.HighPart = a.ftLastWriteTime.dwHighDateTime;
    return (double)(u.QuadPart - 116444736000000000ULL) / 1e7;
}

double unixNow() {
    FILETIME ft; GetSystemTimePreciseAsFileTime(&ft);
    ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
    return (double)(u.QuadPart - 116444736000000000ULL) / 1e7;
}
int localOffset(double t) {
    ULARGE_INTEGER u; u.QuadPart = (ULONGLONG)(std::floor(t) * 1e7) + 116444736000000000ULL;
    FILETIME ft; ft.dwLowDateTime = u.LowPart; ft.dwHighDateTime = u.HighPart;
    SYSTEMTIME ut, lt;
    if (!FileTimeToSystemTime(&ft, &ut) || !SystemTimeToTzSpecificLocalTime(nullptr, &ut, &lt)) return 0;
    FILETIME f2; SystemTimeToFileTime(&lt, &f2);
    ULARGE_INTEGER u2; u2.LowPart = f2.dwLowDateTime; u2.HighPart = f2.dwHighDateTime;
    long long diff = ((long long)u2.QuadPart - (long long)u.QuadPart) / 10000000LL;
    return (int)(std::llround(diff / 900.0) * 900);
}

// ---------------------------------------------------------------- globals
struct Globals {
    HWND hwnd = nullptr;
    gfx::Gfx gfx;
    App app;
    std::wstring settingsPath, forecastCache, airCache, logPath, holPath;
    double holMTime = -1, holCheckAt = 0;           // holidays.txt: when it was last changed (0 = no file) and when to look again
    bool running = true;
    WINDOWPLACEMENT prevPl{};
    double lastFrame = 0;
    double nextFrame = 0;
    util::ClockGuard clock;                         // notices when the system time is set forwards or back
    double lastDraw = 0;                            // when a frame was last really drawn
    // frame pacing: rain and wind are drawn on the screen's own beat (see renderFrame)
    HANDLE hrTimer = nullptr;                       // a high-resolution waitable timer: waits accurate to a fraction of a millisecond (the plain wait is only good to ~15 ms)
    double refreshHz = 60, refreshAt = -1e9;        // the refresh rate of the screen the window is on
    double drawCost = 0.004;                        // seconds from the start of a frame to handing it to the screen (smoothed)
    double lastEnd = 0, paceStart = 0;
    bool smooth = false;                            // frames are being paced to the screen right now
    bool noVsync = false; int shortRun = 0;         // the screen did not set the pace (frames came back too quickly): keep time ourselves
    bool sirenOn = false; double sirenStart = 0;
    bool finishHandled = false, focusFinishHandled = false, ringHandled = false;
    long long lastChimeHour = -1;                   // tracks hour index for optional soft hourly chime (never chimes on startup)
    unsigned lastCaption = 0xFFFFFFFFu;
    bool saveDirty = false, saveFailed = false; double saveAt = 0;
    bool saver = false;                             // running as the screen saver (Meridian.scr /s): see saver.h
    bool leaving = false; double leaveAt = 0;       // the screen saver is fading out (the keyboard or mouse was used): when that began
    bool deferWorkers = false;                      // the screen saver starts its weather and storm downloads only after its reveal is over
    bool geoPending = false; double geoAt = 0;      // a typed location is applied a moment after the last keystroke
    double recreateWaitUntil = 0; bool targetLogged = false;   // a lost render target that cannot be rebuilt yet is tried again about once a second (see ensureTarget)
    int wheelAcc = 0;                               // mouse-wheel movement not yet worth a step: a touchpad sends many small ones, a wheel notch is 120
};
Globals G;

// ---------------------------------------------------------------- chime
std::vector<uint8_t>& chimeWav() {
    static std::vector<uint8_t> wav;
    if (!wav.empty()) return wav;
    const int sr = 22050; const double dur = 3.6; const int n = (int)(sr * dur);
    std::vector<float> mix(n, 0.f);
    struct Note { double t, f, a; };
    const Note notes[] = {{0.00, 784.0, 1.0}, {0.42, 987.8, 0.95}, {0.84, 1174.7, 0.9}, {1.40, 1568.0, 0.8}};
    const double ratio[] = {1.0, 2.0, 3.01, 4.17, 5.43}, amp[] = {1.0, 0.40, 0.16, 0.08, 0.04}, dec[] = {2.6, 3.4, 4.6, 6.0, 7.5};
    for (const Note& nt : notes) {
        for (int i = (int)(nt.t * sr); i < n; ++i) {
            double t = i / (double)sr - nt.t; if (t < 0) continue;
            double env = std::min(1.0, t / 0.006);
            double v = 0;
            for (int k = 0; k < 5; ++k) v += amp[k] * std::exp(-t * dec[k]) * std::sin(6.283185307 * nt.f * ratio[k] * t);
            mix[i] += (float)(v * env * nt.a);
        }
    }
    float peak = 0.001f; for (float v : mix) peak = std::max(peak, std::fabs(v));
    wav.resize(44 + n * 2);
    auto w32 = [&](size_t o, uint32_t v) { memcpy(&wav[o], &v, 4); };
    auto w16 = [&](size_t o, uint16_t v) { memcpy(&wav[o], &v, 2); };
    memcpy(&wav[0], "RIFF", 4); w32(4, 36 + n * 2); memcpy(&wav[8], "WAVEfmt ", 8); w32(16, 16); w16(20, 1); w16(22, 1);
    w32(24, sr); w32(28, sr * 2); w16(32, 2); w16(34, 16); memcpy(&wav[36], "data", 4); w32(40, n * 2);
    for (int i = 0; i < n; ++i) { int16_t s = (int16_t)std::lround(mix[i] / peak * 0.55f * 32767.0); memcpy(&wav[44 + i * 2], &s, 2); }
    return wav;
}
void sirenStart() { if (G.saver) return; auto& w = chimeWav(); PlaySoundW((LPCWSTR)w.data(), nullptr, SND_MEMORY | SND_ASYNC | SND_LOOP | SND_NODEFAULT); G.sirenOn = true; G.sirenStart = unixNow(); }
void sirenStop() { if (G.sirenOn) PlaySoundW(nullptr, nullptr, 0); G.sirenOn = false; }

// Pure-math subtle two-tone glass bell for the optional Soft hourly chime (off by default, silent at bedtime & saver)
std::vector<uint8_t>& hourlyChimeWav() {
    static std::vector<uint8_t> wav;
    if (!wav.empty()) return wav;
    const int sr = 22050; const double dur = 1.75; const int n = (int)(sr * dur);
    std::vector<float> mix(n, 0.f);
    struct Tone { double t0, f, a; };
    const Tone tones[] = {{0.00, 659.25, 1.0}, {0.16, 987.77, 0.58}};
    const double ratio[] = {1.0, 2.0, 3.0}, amp[] = {1.0, 0.24, 0.06}, dec[] = {2.8, 4.4, 6.2};
    for (const Tone& tn : tones) {
        for (int i = (int)(tn.t0 * sr); i < n; ++i) {
            const double t = i / (double)sr - tn.t0; if (t < 0) continue;
            const double env = std::min(1.0, t / 0.014);
            double v = 0;
            for (int k = 0; k < 3; ++k) v += amp[k] * std::exp(-t * dec[k]) * std::sin(6.283185307 * tn.f * ratio[k] * t);
            mix[i] += (float)(v * env * tn.a);
        }
    }
    float peak = 0.001f; for (float v : mix) peak = std::max(peak, std::fabs(v));
    wav.resize(44 + n * 2);
    auto w32 = [&](size_t o, uint32_t v) { memcpy(&wav[o], &v, 4); };
    auto w16 = [&](size_t o, uint16_t v) { memcpy(&wav[o], &v, 2); };
    memcpy(&wav[0], "RIFF", 4); w32(4, 36 + n * 2); memcpy(&wav[8], "WAVEfmt ", 8); w32(16, 16); w16(20, 1); w16(22, 1);
    w32(24, sr); w32(28, sr * 2); w16(32, 2); w16(34, 16); memcpy(&wav[36], "data", 4); w32(40, n * 2);
    for (int i = 0; i < n; ++i) { int16_t s = (int16_t)std::lround(mix[i] / peak * 0.26f * 32767.0); memcpy(&wav[44 + i * 2], &s, 2); }
    return wav;
}
void playHourlyChime() {
    if (G.saver || G.sirenOn || ufoaudio::currentTrack() != ufoaudio::Track::None) return;
    auto& w = hourlyChimeWav();
    PlaySoundW((LPCWSTR)w.data(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

const char* const WKEY[WG_COUNT] = {"world", "month", "system", "note", "countdown", "phases", "week", "today", "holidays", "hourly", "daylight", "sky", "conditions", "year"};
std::string lowerTrim(std::string v) { v = util::trim(v); for (auto& ch : v) if (ch >= 'A' && ch <= 'Z') ch += 32; return v; }
std::vector<std::string> splitCsv(const std::string& v) {
    std::vector<std::string> out; size_t i = 0;
    while (i <= v.size()) { size_t e = v.find(',', i); if (e == std::string::npos) e = v.size(); std::string t = util::trim(v.substr(i, e - i)); if (!t.empty()) out.push_back(t); i = e + 1; }
    return out;
}
// ---------------------------------------------------------------- settings
// where theme packs live: a 'themes' folder next to Meridian.exe, and one in %APPDATA%\\Meridian (for installs in a read-only place)
std::vector<std::filesystem::path> themeRoots() {
    wchar_t buf[MAX_PATH * 2]; DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
    std::vector<std::filesystem::path> v;
    if (n > 0 && n < MAX_PATH * 2) v.push_back(std::filesystem::path(std::wstring(buf, n)).parent_path() / L"themes");
    v.push_back(std::filesystem::path(dataDir(false)) / L"themes");
    return v;
}

void applyGeoFromPlace() {
    if (G.app.placeIdx < 0 || G.app.placeIdx >= (int)G.app.st.places.size()) G.app.st.geo = G.app.home;
    else G.app.st.geo = G.app.st.places[G.app.placeIdx];
}

// The layout of settings.ini. Raise it when a change needs an older file converted, and do the conversion in loadSettings() under the file's
// own number (0 = a file from before this field existed; its layout is the same as 1).
constexpr int SETTINGS_VERSION = 1;

void loadSettings() {
    Settings& st = G.app.st; TimerState& tm = G.app.timer;
    std::string txt;
    if (!readFile(G.settingsPath, txt)) return;
    util::Ini ini = util::Ini::parse(txt);
    const int fileVersion = (int)ini.getNum("meridian", "settings_version", 0);
    if (fileVersion > SETTINGS_VERSION) {
        // written by a newer Meridian than this one. Saving would drop whatever this version does not know about, so keep a copy first (once).
        const std::wstring bak = G.settingsPath + L".from-newer-version-" + std::to_wstring(fileVersion) + L".bak";
        if (GetFileAttributesW(bak.c_str()) == INVALID_FILE_ATTRIBUTES) CopyFileW(G.settingsPath.c_str(), bak.c_str(), TRUE);
    }
    st.theme = (int)std::max(0.0, std::min((double)THEME_COUNT - 1, ini.getNum("view", "theme", st.theme)));
    st.face = (int)std::max(0.0, std::min((double)FACE_COUNT - 1, ini.getNum("view", "face", st.face)));
    st.h24 = ini.getBool("view", "h24", st.h24);
    st.seconds = ini.getBool("view", "seconds", st.seconds);
    st.useF = ini.getBool("view", "fahrenheit", st.useF);
    st.poya = ini.getBool("view", "poya", st.poya);
    st.pixelShift = ini.getBool("view", "pixel_shift", st.pixelShift);
    st.keepAwake = ini.getBool("view", "keep_awake", st.keepAwake);
    st.topmost = ini.getBool("view", "topmost", st.topmost);
    st.fullscreen = ini.getBool("view", "fullscreen", st.fullscreen);
    st.brightness = (float)std::max(0.15, std::min(1.0, ini.getNum("view", "brightness", st.brightness)));
    st.emberAtNight = ini.getBool("view", "ember_at_bedtime", st.emberAtNight);
    st.autoDim = ini.getBool("view", "auto_dim", st.autoDim);
    st.dimNight = (float)std::max(0.15, std::min(1.0, ini.getNum("view", "dim_at_night", st.dimNight)));
    st.emberFrom = (int)std::max(0.0, std::min(23.0, ini.getNum("view", "bedtime_from", st.emberFrom)));
    st.emberTo = (int)std::max(0.0, std::min(23.0, ini.getNum("view", "bedtime_to", st.emberTo)));
    st.hourlyChime = ini.getBool("view", "hourly_chime", st.hourlyChime);
    G.app.home.name = ini.get("location", "name", G.app.home.name);
    G.app.home.lat = std::max(-89.0, std::min(89.0, ini.getNum("location", "lat", G.app.home.lat)));
    G.app.home.lon = std::max(-180.0, std::min(180.0, ini.getNum("location", "lon", G.app.home.lon)));
    st.placeLocalTime = ini.getBool("location", "place_local_time", st.placeLocalTime);
    G.app.located = ini.getBool("location", "located", true);                  // no such line (an older settings.ini): the saved place stands and is not looked up again
    G.app.locSource = ini.get("location", "source", "");
    G.app.locAcc = ini.getNum("location", "accuracy_m", -1);
    G.app.locAt = ini.getNum("location", "found_at", 0);
    G.app.homeCountry = ini.get("location", "country", ""); G.app.homeRegion = ini.get("location", "region", "");
    {   // which countries' holidays: [holidays] auto=1, or countries=lk,uk,us
        st.holAuto = ini.getBool("holidays", "auto", st.holAuto);
        const std::string list = lowerTrim(ini.get("holidays", "countries", "lk"));
        int mk = 0;
        if (list.find("lk") != std::string::npos) mk |= holiday::C_LK;
        if (list.find("uk") != std::string::npos) mk |= holiday::C_UK;
        if (list.find("us") != std::string::npos) mk |= holiday::C_US;
        st.holMask = mk;
        const std::string nat = lowerTrim(ini.get("holidays", "uk_nation", "auto"));
        st.ukPref = nat.find("scot") != std::string::npos ? 2 : nat.find("north") != std::string::npos ? 3 : nat.find("eng") != std::string::npos || nat.find("wales") != std::string::npos ? 1 : 0;
    }
    st.winX = (int)ini.getNum("window", "x", st.winX); st.winY = (int)ini.getNum("window", "y", st.winY);
    st.winW = (int)ini.getNum("window", "w", st.winW); st.winH = (int)ini.getNum("window", "h", st.winH);
    st.places.clear();
    for (auto& kv : ini.ordered("places")) {
        double la, lo;
        if (sscanf(kv.second.c_str(), "%lf , %lf", &la, &lo) == 2 && std::fabs(la) <= 89 && std::fabs(lo) <= 180) { astro::Geo g; g.name = kv.first; g.lat = la; g.lon = lo; st.places.push_back(g); }
    }
    {   // the place that was on show when Meridian was closed: match by name+coordinates first (so duplicate names like two Springfields stay distinct), else coordinates, else legacy name
        const std::string showing = ini.get("location", "showing", "");
        const std::string sc = ini.get("location", "showing_coords", "");
        double sla = 0, slo = 0;
        const bool hasCoords = !sc.empty() && sscanf(sc.c_str(), "%lf , %lf", &sla, &slo) == 2;
        G.app.placeIdx = -1;
        if (hasCoords) {
            for (size_t i = 0; i < st.places.size(); ++i)
                if ((showing.empty() || st.places[i].name == showing) &&
                    std::fabs(st.places[i].lat - sla) < 0.001 && std::fabs(st.places[i].lon - slo) < 0.001) { G.app.placeIdx = (int)i; break; }
            if (G.app.placeIdx < 0)
                for (size_t i = 0; i < st.places.size(); ++i)
                    if (std::fabs(st.places[i].lat - sla) < 0.001 && std::fabs(st.places[i].lon - slo) < 0.001) { G.app.placeIdx = (int)i; break; }
        }
        if (G.app.placeIdx < 0 && !showing.empty())
            for (size_t i = 0; i < st.places.size(); ++i)
                if (st.places[i].name == showing) { G.app.placeIdx = (int)i; break; }
    }
    st.events.clear();
    for (auto& kv : ini.ordered("events")) {
        today::CustomEvent e; e.title = kv.second;
        std::string key = kv.first; if (key.compare(0, 2, "--") == 0) key.erase(0, 2);                 // "--12-25" is the same as "12-25"
        const int used = util::parseEventDatePrefix(key.c_str(), e.y, e.m, e.d);
        if (!used || !util::onlySpace(key.c_str() + used) || e.title.empty()) continue;                  // not a real date (31 February ...): left out
        if (st.events.size() >= 200) break;
        st.events.push_back(e);
    }
    for (int i = 0; i < weather::PV_COUNT; ++i) st.provEnabled[i] = ini.getBool("weather", weather::providerId(i), true);
    st.contact = ini.get("weather", "contact", "");
    st.weatherApiKey = ini.get("weather", "weatherapi_key", "");
    st.openWeatherKey = ini.get("weather", "openweather_key", "");
    st.evMeteors = ini.getBool("sky", "meteors", st.evMeteors); st.evPlanets = ini.getBool("sky", "planets", st.evPlanets);
    st.evEclipses = ini.getBool("sky", "eclipses", st.evEclipses); st.evChips = ini.getBool("sky", "chips", st.evChips);
    st.evAnimate = ini.getBool("sky", "animate", st.evAnimate);
    st.evDays = (int)std::max(1.0, std::min(30.0, ini.getNum("sky", "days", st.evDays)));
    st.elToday = ini.getBool("elements", "today", st.elToday); st.elWeather = ini.getBool("elements", "weather", st.elWeather);
    st.elSunMoon = ini.getBool("elements", "sun_moon", st.elSunMoon); st.elBand = ini.getBool("elements", "day_band", st.elBand);
    st.elLabels = ini.getBool("elements", "labels", st.elLabels); st.elConst = ini.getBool("elements", "constellations", st.elConst);
    st.elMilky = ini.getBool("elements", "milky_way", st.elMilky); st.elEcliptic = ini.getBool("elements", "ecliptic", st.elEcliptic);
    st.elClouds = ini.getBool("elements", "clouds", st.elClouds); st.elTracks = ini.getBool("elements", "tracks", st.elTracks);
    st.dayStars = ini.getBool("elements", "day_stars", st.dayStars);
    st.elEvents = ini.getBool("panel", "sky_events", st.elEvents); st.elWorld = ini.getBool("panel", "world_clocks", st.elWorld);
    st.elHolidays = ini.getBool("panel", "holidays", st.elHolidays); st.holCal = ini.getBool("panel", "calendar_holidays", st.holCal);
    st.panelSecs = (int)std::max(4.0, std::min(120.0, ini.getNum("panel", "seconds", st.panelSecs)));
    { std::string k = lowerTrim(ini.get("panel", "transition", "slide")); st.panelStyle = k == "rise" ? 1 : k == "fade" ? 2 : 0; }
    st.panelHold = ini.getBool("panel", "pause_on_hover", st.panelHold); st.panelDots = ini.getBool("panel", "dots", st.panelDots);
    st.skyHeading = (float)astro::norm360(ini.getNum("skymap", "heading", st.skyHeading));
    st.skySpin = ini.getBool("skymap", "free_rotate", st.skySpin);
    st.skySpinSecs = (int)std::max(5.0, std::min(7200.0, ini.getNum("skymap", "seconds_per_turn", st.skySpinSecs)));
    st.skySpinDir = lowerTrim(ini.get("skymap", "direction", st.skySpinDir > 0 ? "clockwise" : "counterclockwise")).compare(0, 7, "counter") == 0 ? -1 : 1;
    st.quietFx = ini.getBool("quiet", "weather", st.quietFx);
    st.stormWatch = ini.getBool("storms", "watch", st.stormWatch);
    st.stormRadius = (int)std::max(100.0, std::min(6000.0, ini.getNum("storms", "radius_km", st.stormRadius)));
    st.quietStrength = (float)std::max(0.5, std::min(2.0, ini.getNum("quiet", "strength", 100.0) / 100.0));
    st.quietFlare = ini.getBool("quiet", "sun_flare", st.quietFlare);
    st.quietSmooth = ini.getBool("quiet", "smooth_motion", st.quietSmooth);
    st.skyRain = ini.getBool("quiet", "sky_rain", st.skyRain);
    st.spaceFx = ini.getBool("quiet", "midnight_sky", st.spaceFx);
    st.aliensEnabled = ini.getBool("quiet", "aliens", st.aliensEnabled);
    st.weatherMood = ini.getBool("quiet", "weather_light", st.weatherMood);
    st.moodStrength = (float)std::max(0.5, std::min(1.5, ini.getNum("quiet", "weather_light_strength", 100.0) / 100.0));
    // theme packs (the themes folder): which one is the theme, which extra tab was open, and which are switched off
    st.themePack = ini.get("view", "theme_pack", "");
    st.facePack = ini.get("view", "face_pack", "");
    if (ini.getBool("view", "face_is_pack", false) && !st.facePack.empty()) st.face = FACE_PACK;
    st.packsOff = splitCsv(ini.get("themes", "disabled", ""));
    if (ini.sections.count("widgets")) {
        if (ini.sections["widgets"].count("list")) {
            st.widgets.clear();
            for (auto& t : splitCsv(ini.get("widgets", "list"))) {
                std::string k = lowerTrim(t);
                for (int i = 0; i < WG_COUNT; ++i) if (k == WKEY[i] && std::find(st.widgets.begin(), st.widgets.end(), i) == st.widgets.end()) st.widgets.push_back(i);
            }
        }
        // lists saved by an older version get the new widgets once, so the Widgets face has something to cycle through
        if (!ini.sections["widgets"].count("layout")) {
            for (int id : {WG_HOURLY, WG_HOLIDAYS, WG_DAYLIGHT, WG_SKY, WG_AIR, WG_YEAR})
                if (std::find(st.widgets.begin(), st.widgets.end(), id) == st.widgets.end()) st.widgets.push_back(id);
        }
        { std::string k = lowerTrim(ini.get("widgets", "per_page", st.wgPerPage <= 0 ? "all" : std::to_string(st.wgPerPage)));
          st.wgPerPage = k == "all" ? 0 : (int)std::max(1, std::min(6, atoi(k.c_str()))); }
        st.wgSecs = (int)std::max(4.0, std::min(300.0, ini.getNum("widgets", "page_seconds", st.wgSecs)));
        st.pinned = -1;
        { std::string k = lowerTrim(ini.get("widgets", "pinned", "none")); for (int i = 0; i < WG_COUNT; ++i) if (k == WKEY[i]) st.pinned = i; }
        if (ini.sections["widgets"].count("clocks")) {
            st.clocks.clear();
            for (auto& t : splitCsv(ini.get("widgets", "clocks"))) {
                std::string k = lowerTrim(t);
                for (int i = 0; i < cityCount(); ++i) if (k == lowerTrim(cityName(i)) && st.clocks.size() < 4 && std::find(st.clocks.begin(), st.clocks.end(), i) == st.clocks.end()) st.clocks.push_back(i);
            }
        }
        st.weekMon = ini.getBool("widgets", "week_starts_monday", st.weekMon);
        st.note = unescapeNl(ini.get("widgets", "note", st.note), ini.getNum("widgets", "note_format", 1) >= 2);
        st.cdLabel = ini.get("widgets", "countdown_label", st.cdLabel); st.cdDate = ini.get("widgets", "countdown_date", st.cdDate);
    }
    tm.durationSec = std::max(TIMER_MIN_SEC, std::min(TIMER_MAX_SEC, ini.getNum("timer", "duration", tm.durationSec))); tm.pausedRemaining = tm.durationSec;
    tm.alarmMinutes = (int)std::max(0.0, std::min(1439.0, ini.getNum("timer", "alarm_minutes", tm.alarmMinutes)));
    tm.alarmOn = ini.getBool("timer", "alarm_on", false);
    {
        FocusState& fc = G.app.focus;
        fc.durationSec = std::max(TIMER_MIN_SEC, std::min(TIMER_MAX_SEC, ini.getNum("focus", "duration", fc.durationSec)));
        fc.pausedRemaining = fc.durationSec;
        static const char* const kFx[14] = {"off", "live", "rain", "snow", "clouds", "fog", "storm", "stars", "blizzard", "drizzle", "sleet", "hail", "gale", "midnight"};
        const std::string bk = lowerTrim(ini.get("focus", "backdrop", "live"));
        st.focusFx = 1;
        for (int i = 0; i < 14; ++i) if (bk == kFx[i]) { st.focusFx = i; break; }
    }
}

bool saveSettings() {                                      // false only when the file could not be written
    if (G.saver) return true;                                   // the screen saver shows your settings, it never changes them
    Settings& st = G.app.st; const TimerState& tm = G.app.timer;
    std::string o;
    o += "; Meridian settings. Most of this is easier from the Settings panel in the app (press the comma key).\n";
    o += "; If you edit this file by hand, do it while the app is closed. Everything is saved automatically.\n\n";
    o += fmt("[meridian]\n; which layout this file has, so a newer Meridian can convert an older file (and an older one can tell it is looking at a newer file). Leave it alone.\nsettings_version=%d\nwritten_by=%s\n\n", SETTINGS_VERSION, MERIDIAN_VERSION);
    o += "[location]\n; used for the sky, sunrise/sunset, the moon and the weather\n";
    o += "name=" + G.app.home.name + "\n" + fmt("lat=%.4f\nlon=%.4f\n", G.app.home.lat, G.app.home.lon);
    o += "; FIND MY LOCATION (Settings > Place) fills this in and it is kept until you press the button again. located=0 only on a new install that has not found\n; its location yet: it then looks once at the next start. source is where the position came from, accuracy_m how far off it may be.\n";
    o += fmt("located=%d\nsource=", G.app.located ? 1 : 0) + G.app.locSource + fmt("\naccuracy_m=%.0f\nfound_at=%.0f\n", G.app.locAcc, G.app.locAt);
    o += "; the country (two-letter code) and region the lookup reported. They pick the holidays when [holidays] auto=1. Empty for a place typed by hand.\ncountry=" + G.app.homeCountry + "\nregion=" + G.app.homeRegion + "\n";
    const bool showSaved = G.app.placeIdx >= 0 && G.app.placeIdx < (int)st.places.size();
    o += "; which place is on show: the name and coordinates of one of the [places] below, or empty for the place above. Kept across restarts.\nshowing=" +
         (showSaved ? st.places[(size_t)G.app.placeIdx].name : std::string()) + "\nshowing_coords=" +
         (showSaved ? fmt("%.4f,%.4f", st.places[(size_t)G.app.placeIdx].lat, st.places[(size_t)G.app.placeIdx].lon) : std::string()) +
         fmt("\n; when viewing a saved place (N), show the clock in that place's local timezone. 1 = on, 0 = off\nplace_local_time=%d\n\n", st.placeLocalTime ? 1 : 0);
    o += "[places]\n; extra places, cycle with N.  Name=lat,lon\n";
    for (auto& p : st.places) {
        std::string safe = p.name;
        for (char& ch : safe) if (ch == '=' || ch == '\r' || ch == '\n') ch = '-';
        while (!safe.empty() && (safe[0] == ';' || safe[0] == '#' || safe[0] == '[')) safe.erase(safe.begin());
        if (safe.empty()) safe = "Place";
        o += safe + fmt("=%.4f,%.4f\n", p.lat, p.lon);
    }
    o += "\n[events]\n; your own dates shown under TODAY.  MM-DD repeats every year, YYYY-MM-DD happens once.\n";
    for (auto& e : st.events) o += (e.y ? fmt("%04d-%02d-%02d", e.y, e.m, e.d) : fmt("%02d-%02d", e.m, e.d)) + "=" + e.title + "\n";
    o += "\n[view]\n";
    o += fmt("theme=%d\n; 0 auto, 1 day, 2 dusk, 3 night, 4 ember, 5 dawn, 6 sunset, 7 sunlit, 8 sunlit II, 9 galaxy, 10 midnight\nface=%d\n; 0 sky, 1 quiet, 2 timer, 3 almanac, 4 widgets, 5 focus\n", st.theme, st.face == FACE_PACK ? (int)FACE_QUIET : st.face);
    o += "; a theme pack (a folder in the themes folder) used as the theme, and the pack tab that was open. Leave empty for the built-in themes.\ntheme_pack=" + st.themePack + "\nface_pack=" + st.facePack + fmt("\nface_is_pack=%d\n", st.face == FACE_PACK ? 1 : 0);
    o += fmt("h24=%d\nseconds=%d\nfahrenheit=%d\npoya=%d\npixel_shift=%d\nkeep_awake=%d\ntopmost=%d\nfullscreen=%d\nbrightness=%.2f\n",
             st.h24, st.seconds, st.useF, st.poya, st.pixelShift, st.keepAwake, st.topmost, st.fullscreen, st.brightness);
    o += fmt("ember_at_bedtime=%d\nbedtime_from=%d\nbedtime_to=%d\nauto_dim=%d\ndim_at_night=%.2f\nhourly_chime=%d\n\n", st.emberAtNight, st.emberFrom, st.emberTo, st.autoDim, st.dimNight, st.hourlyChime ? 1 : 0);
    o += "[weather]\n; sources are tried in this order until one answers. 1 = on, 0 = off\n";
    for (int i = 0; i < weather::PV_COUNT; ++i) o += fmt("%s=%d\n", weather::providerId(i), st.provEnabled[i] ? 1 : 0);
    o += "; optional: an email or website. MET Norway and NWS ask apps to identify themselves in the User-Agent\ncontact=" + st.contact + "\n";
    o += "; optional free API keys for WeatherAPI.com and OpenWeatherMap (stay local, also provide backup Air Quality)\n";
    o += "weatherapi_key=" + st.weatherApiKey + "\nopenweather_key=" + st.openWeatherKey + "\n\n";
    o += "[sky]\n; events appear on the sky map when they are this many days away or less. 1 = on, 0 = off\n";
    o += fmt("days=%d\nmeteors=%d\nplanets=%d\neclipses=%d\nchips=%d\nanimate=%d\n\n", st.evDays, st.evMeteors, st.evPlanets, st.evEclipses, st.evChips, st.evAnimate);
    o += fmt("[elements]\n; parts of the Sky face. 1 = on, 0 = off\ntoday=%d\nweather=%d\nsun_moon=%d\nday_band=%d\nlabels=%d\nconstellations=%d\nmilky_way=%d\necliptic=%d\nclouds=%d\ntracks=%d\nday_stars=%d\n\n",
             st.elToday, st.elWeather, st.elSunMoon, st.elBand, st.elLabels, st.elConst, st.elMilky, st.elEcliptic, st.elClouds, st.elTracks, st.dayStars ? 1 : 0);
    o += fmt("[panel]\n; the left side of the Sky face rotates between Today (see today= above), Sky events, Holidays and World clocks. 1 = on, 0 = off\nsky_events=%d\nholidays=%d\nworld_clocks=%d\n", st.elEvents, st.elHolidays, st.elWorld);
    o += fmt("; seconds each panel stays, and how the next one arrives: slide, rise or fade\nseconds=%d\ntransition=%s\npause_on_hover=%d\ndots=%d\n", st.panelSecs, st.panelStyle == 1 ? "rise" : st.panelStyle == 2 ? "fade" : "slide", st.panelHold, st.panelDots);
    o += fmt("; mark holidays on the month calendar widget (hover a date for details)\ncalendar_holidays=%d\n\n", st.holCal);
    o += fmt("[holidays]\n; whose holidays to show, everywhere: any mix of lk (Sri Lanka), uk (United Kingdom) and us (United States).\n; auto=1 picks the country your place is in (nothing for any other country); auto=0 uses the countries listed here.\nauto=%d\n", st.holAuto);
    { std::string l; if (st.holMask & holiday::C_LK) l += "lk"; if (st.holMask & holiday::C_UK) l += l.empty() ? "uk" : ",uk"; if (st.holMask & holiday::C_US) l += l.empty() ? "us" : ",us"; o += "countries=" + l + "\n"; }
    o += std::string("; which UK list: auto (from your place), england-wales, scotland or northern-ireland\nuk_nation=") + (st.ukPref == 1 ? "england-wales" : st.ukPref == 2 ? "scotland" : st.ukPref == 3 ? "northern-ireland" : "auto") + "\n\n";
    o += fmt("[skymap]\n; which compass direction is at the top of the sky map, in degrees (0 north, 90 east, 180 south, 270 west)\nheading=%.0f\n", st.skyHeading);
    o += fmt("; free_rotate=1 turns the whole sky slowly, one full turn every seconds_per_turn seconds; direction is clockwise or counterclockwise\nfree_rotate=%d\nseconds_per_turn=%d\ndirection=%s\n\n",
             st.skySpin, st.skySpinSecs, st.skySpinDir > 0 ? "clockwise" : "counterclockwise");
    o += fmt("[quiet]\n; draw the weather behind the clock on the Quiet face (clouds, wind, rain, a tornado in a thunderstorm). 1 = on, 0 = off\nweather=%d\n; how strongly it is drawn, in percent (50 to 200). 100 is the default\nstrength=%.0f\n; a soft sun flare from the side the Sun is on, in the morning and the evening. 1 = on, 0 = off\nsun_flare=%d\n; rain and wind drawn at your screen's own refresh rate (up to 90 frames a second) so they stay smooth. 0 = 30 frames a second, lighter on a laptop battery\nsmooth_motion=%d\n", st.quietFx, st.quietStrength * 100.f, st.quietFlare, st.quietSmooth);
    o += fmt("; rain also falls behind the map on the Sky face. 1 = on, 0 = off\nsky_rain=%d\n; the Galaxy theme's stars, meteors and comet behind the Quiet and Sky faces. 1 = on, 0 = off\nmidnight_sky=%d\n", st.skyRain, st.spaceFx);
    if (!st.aliensEnabled) o += "aliens=0\n";
    o += fmt("; the light follows the weather on every face: heavy cloud and storms darken and cool the theme, fog flattens it. 1 = on, 0 = off\nweather_light=%d\n; how strong that is, in percent (50 to 150)\nweather_light_strength=%.0f\n\n", st.weatherMood, st.moodStrength * 100.f);
    o += fmt("[storms]\n; show named storms (cyclones, typhoons, hurricanes) when one is near you: its name, distance and direction. 1 = on, 0 = off\nwatch=%d\n; how far away still counts as near, in km\nradius_km=%d\n\n", st.stormWatch, st.stormRadius);
    {
        std::string off; for (const std::string& id : st.packsOff) off += std::string(off.empty() ? "" : ",") + id;
        o += "[themes]\n; theme packs are folders in the 'themes' folder next to Meridian.exe (or in %APPDATA%\\Meridian\\themes). Drop one in, or delete it, and Meridian notices.\n; packs listed here are switched off (they stay installed). Easier from Settings > Themes.\ndisabled=" + off + "\n\n";
    }
    {
        std::string wl, cl;
        for (int id : st.widgets) wl += std::string(wl.empty() ? "" : ",") + WKEY[id];
        for (int i : st.clocks) cl += std::string(cl.empty() ? "" : ",") + cityName(i);
        o += "[widgets]\n; the Widgets face, in order: world, month, system, note, countdown, phases, week, today, holidays, hourly, daylight, sky, conditions, year\nlist=" + wl + "\n";
        o += std::string("; widgets shown together on one page (1 to 6, or all) and how many seconds each page stays; the transition is the one set under [panel]\nper_page=") + (st.wgPerPage <= 0 ? std::string("all") : std::to_string(st.wgPerPage)) + fmt("\npage_seconds=%d\nlayout=2\n", st.wgSecs);
        o += std::string("; one widget can join the rotating panel on the Sky face (a widget name, or none)\npinned=") + (st.pinned >= 0 ? WKEY[st.pinned] : "none") + "\n";
        o += "; world clock cities, up to four (London, New York, Tokyo, Dubai, Singapore, Sydney, Los Angeles, Paris, Berlin, Delhi, Moscow, Beijing, Seoul, Cairo, Johannesburg, Sao Paulo, Chicago, Auckland, Istanbul, Riyadh, Karachi, Dhaka, Bangkok, Colombo, San Francisco, Denver, Phoenix, Toronto, Zurich, Hong Kong, Melbourne)\nclocks=" + cl + "\n";
        o += fmt("week_starts_monday=%d\n", st.weekMon) + "note=" + escapeNl(st.note) + "\nnote_format=2\ncountdown_label=" + st.cdLabel + "\ncountdown_date=" + st.cdDate + "\n\n";
    }
    o += fmt("[window]\nx=%d\ny=%d\nw=%d\nh=%d\n\n", st.winX, st.winY, st.winW, st.winH);
    o += fmt("[timer]\nduration=%.0f\nalarm_minutes=%d\nalarm_on=%d\n\n", tm.durationSec, tm.alarmMinutes, tm.alarmOn ? 1 : 0);
    static const char* const kFxNames[14] = {"off", "live", "rain", "snow", "clouds", "fog", "storm", "stars", "blizzard", "drizzle", "sleet", "hail", "gale", "midnight"};
    const int fxIdx = std::max(0, std::min(13, st.focusFx));
    o += fmt("[focus]\nduration=%.0f\nbackdrop=%s\n", G.app.focus.durationSec, kFxNames[fxIdx]);
    return writeFile(G.settingsPath, o);
}
void markDirty() { G.saveDirty = true; G.saveAt = unixNow() + 1.5; }
// Saves when the timer says so. A save that fails (disk full, settings.ini locked by another program, a read-only folder) is not forgotten:
// it stays pending and is tried again every 10 seconds, and Meridian tells you once.
void saveIfDue(double now) {
    if (!G.saveDirty || now < G.saveAt) return;
    G.saveDirty = false;
    if (saveSettings()) { G.saveFailed = false; return; }
    G.saveDirty = true; G.saveAt = now + 10.0;
    if (!G.saveFailed) { G.saveFailed = true; G.app.setToast(L"Could not save your settings. Will keep trying"); }
}


// ---------------------------------------------------------------- weather worker
void weatherLog(const std::string& line) {
    static std::mutex logMu; std::lock_guard<std::mutex> lg(logMu);      // the weather and storm workers both write here
    util::LocalTime L = util::toLocal(unixNow(), localOffset(unixNow()));
    robust::appendLog(G.logPath, fmt("%04d-%02d-%02d %02d:%02d:%02d  ", L.y, L.m, L.d, L.hh, L.mm, L.ss) + line + "\n", 48 * 1024, 16 * 1024);
}

// The screen saver ends here, at once and for good: the sleep the saver was asked to start is started, and the process ends without any clean-up.
// The window is NOT hidden or destroyed first: ShowWindow(SW_HIDE) waits for the desktop compositor, and DestroyWindow, the graphics and every
// library's shutdown can take seconds on some PCs (a graphics driver that is still waking up). Windows keeps the screen on a bare grey desktop until
// the saver has ended, so the quickest way back is to end the process and let Windows take the window away.
[[noreturn]] void saverQuit(const char* why) {
    if (G.leaving) weatherLog(fmt("Saver: gone %.2f s after the keyboard or mouse was used (%s)", unixNow() - G.leaveAt, why));
    saver::finish();
    TerminateProcess(GetCurrentProcess(), 0);
    for (;;) Sleep(1000);
}

// Can the fade-out be seen? Only when a picture is really being drawn: a target, not covered, and a frame within the last moments.
bool saverCanFade(double now) {
    return G.gfx.hasTarget() && !G.app.wantRecreate && !G.gfx.occluded() && G.lastDraw > 0 && now - G.lastDraw < 2.5;
}

struct Worker {
    std::mutex mu; std::condition_variable cv;
    bool stop = false, refresh = true;
    double lat = 0, lon = 0;
    int tz = 0;                                                      // local UTC offset, for providers that do not report one
    bool enabled[weather::PV_COUNT] = {true, true, true, true, true, true, true, true};
    std::string contact, weatherApiKey, openWeatherKey;
    weather::Data result; bool ready = false; std::string status, detail; bool failing = false;
    // Every place change gets a new number. A download remembers the number it started with and is thrown away if the place has
    // changed since: a slow reply for the old place must never be shown under the new place's name.
    unsigned long long locGen = 1, resultGen = 0;
    std::atomic<bool> exited{false};
    std::vector<weather::ProviderStatus> prov = std::vector<weather::ProviderStatus>(weather::PV_COUNT);
    std::thread th;
    void start() { th = std::thread([this] { run(); }); }
    void stopAsync() { { std::lock_guard<std::mutex> l(mu); stop = true; } cv.notify_all(); }
    // after stopAsync(): wait up to waitMs for the thread to end. true: it ended (and is joined). false: it is stuck in a download;
    // it is let go, and the program must end with ExitProcess so that nothing it touches is torn down under it.
    bool finish(DWORD waitMs) {
        if (!th.joinable()) return true;
        const ULONGLONG t0 = GetTickCount64();
        while (!exited && GetTickCount64() - t0 < waitMs) Sleep(10);
        if (exited) { th.join(); return true; }
        th.detach(); return false;
    }
    void request(double la, double lo) {
        { std::lock_guard<std::mutex> l(mu);
          if (std::fabs(la - lat) > 1e-6 || std::fabs(lo - lon) > 1e-6) { ++locGen; ready = false; failing = false; status.clear(); detail.clear(); }   // a new place
          lat = la; lon = lo; refresh = true; }
        cv.notify_all();
    }
    void run() {
        struct ExitFlag { std::atomic<bool>& f; ~ExitFlag() { f = true; } } exitFlag{exited};
        int failures = 0;
        for (;;) {
            double la, lo; int tzo; bool en[weather::PV_COUNT]; std::string ct, wKey, owmKey; unsigned long long gen;
            { std::unique_lock<std::mutex> l(mu); if (stop) return; la = lat; lo = lon; gen = locGen; tzo = tz; ct = contact; wKey = weatherApiKey; owmKey = openWeatherKey; for (int i = 0; i < weather::PV_COUNT; ++i) en[i] = enabled[i]; refresh = false; }
            auto stale = [&] { std::lock_guard<std::mutex> l(mu); return locGen != gen || stop; };
            auto hasNonSpace = [](const std::string& s) { for (unsigned char c : s) if (c > ' ') return true; return false; };
            weather::Data d; bool ok = false; int tried = 0; std::string firstErr;
            std::vector<weather::ProviderStatus> ps(weather::PV_COUNT);
            for (int id = 0; id < weather::PV_COUNT; ++id) {
                ps[id].enabled = en[id];
                if (!en[id] || ok) continue;
                if (weather::cancelled() || stale()) break;                // closing, or the place changed: do not start another download
                const std::string keyForId = (id == weather::PV_WEATHERAPI) ? wKey : (id == weather::PV_OPENWEATHER ? owmKey : std::string());
                if (weather::providerNeedsKey(id) && !hasNonSpace(keyForId)) {
                    ps[id].err = "needs free API key";
                    continue;
                }
                std::string body, abody, err;
                ps[id].tried = true; ps[id].lastTry = unixNow(); ++tried;
                if (weather::fetchProvider(id, la, lo, ct, body, abody, err, keyForId)) {
                    weather::Data cand; cand.utcOffset = tzo; cand.fetched = unixNow();
                    if (weather::parseProvider(id, body, la, lo, cand, err)) {
                        ok = true; d = cand; d.fetched = unixNow();
                        std::string aerr;
                        if (!d.hasAir && !weather::cancelled() && !stale()) {
                            // Calm 350 ms stagger before Air Quality prevents simultaneous/burst connection aborts (E_ABORT 2147500036) on Open-Meteo
                            const ULONGLONG tStag = GetTickCount64();
                            while (!weather::cancelled() && !stale() && GetTickCount64() - tStag < 350ULL) Sleep(25);
                            if (!weather::cancelled() && !stale()) {
                                if (weather::fetchAir(la, lo, abody, aerr, wKey, owmKey)) {
                                    weather::parseAir(abody, d, aerr, la, lo);
                                }
                            }
                        }
                        if (!stale()) {
                            writeFile(G.forecastCache, fmt("%d %.4f,%.4f\n", id, la, lo) + body);
                            if (d.hasAir) {
                                writeFile(G.airCache, weather::formatAirCache(la, lo, d.fetched, weather::formatAirJson(d)));
                                weatherLog(fmt("Air quality (%s): US AQI %.0f (%s)",
                                               d.airSource.empty() ? "Open-Meteo" : d.airSource.c_str(), d.aqi, weather::aqiLabel(d.aqi)));
                            } else {
                                // Keep a recent cached AQI ONLY if it matches this exact place; delete if from another city or stale
                                std::string cachedAir, cerr;
                                if (readFile(G.airCache, cachedAir) && weather::parseAirCache(cachedAir, la, lo, d.fetched, 6 * 3600.0, d, cerr)) {
                                    weatherLog(fmt("Air quality: cached US AQI %.0f (%s)", d.aqi, aerr.empty() ? "reused" : aerr.c_str()));
                                } else {
                                    DeleteFileW(G.airCache.c_str());
                                    if (!aerr.empty()) weatherLog("Air quality: " + aerr);
                                }
                            }
                        }
                        ps[id].ok = true; ps[id].lastOk = d.fetched;
                    } else err = "unreadable reply (" + err + ")";
                }
                ps[id].err = err;
                if (firstErr.empty() && !err.empty()) firstErr = err;
                weatherLog(std::string(weather::providerName(id)) + ": " + (ps[id].ok ? std::string("ok") : err));
            }
            if (weather::cancelled()) return;
            {
                std::unique_lock<std::mutex> l(mu);
                if (stop) return;
                if (locGen != gen) continue;                               // the place changed while this ran: drop it; the new place is already waiting (refresh is set)
                for (int i = 0; i < weather::PV_COUNT; ++i) {
                    double keepOk = prov[i].lastOk;
                    prov[i] = ps[i];
                    if (!ps[i].ok) prov[i].lastOk = keepOk;
                }
                if (ok) { result = d; resultGen = gen; ready = true; status.clear(); detail.clear(); failing = false; failures = 0; }
                else {
                    failing = true; ++failures;
                    if (tried == 0) { status = "No weather source enabled"; detail = "turn one on in settings"; }
                    else { status = "Offline · " + firstErr; detail = fmt("tried %d source%s · retrying", tried, tried == 1 ? "" : "s"); }
                }
                double waitSec = ok ? 15 * 60.0 : std::min(15 * 60.0, 30.0 * (1 << std::min(failures, 5)));
                cv.wait_for(l, std::chrono::milliseconds((long long)(waitSec * 1000)), [this] { return stop || refresh; });
                if (stop) return;
            }
        }
    }
};
Worker W;

// named storms: one small download every half hour, for the whole world. Which ones are near is worked out on the main thread.
struct StormWorker {
    std::mutex mu; std::condition_variable cv;
    bool stop = false, refresh = true, enabled = true, ready = false, busy = false;
    double lastTry = 0, lastOk = 0; std::string lastErr;             // shown in Settings > Weather
    storms::Data result; std::thread th;
    std::atomic<bool> exited{false};
    void start() { th = std::thread([this] { run(); }); }
    void stopAsync() { { std::lock_guard<std::mutex> l(mu); stop = true; } cv.notify_all(); }
    bool finish(DWORD waitMs) {                                       // see Worker::finish
        if (!th.joinable()) return true;
        const ULONGLONG t0 = GetTickCount64();
        while (!exited && GetTickCount64() - t0 < waitMs) Sleep(10);
        if (exited) { th.join(); return true; }
        th.detach(); return false;
    }
    void configure(bool en) { { std::lock_guard<std::mutex> l(mu); if (en && !enabled) refresh = true; enabled = en; } cv.notify_all(); }
    void request() { { std::lock_guard<std::mutex> l(mu); refresh = true; } cv.notify_all(); }
    void run() {
        struct ExitFlag { std::atomic<bool>& f; ~ExitFlag() { f = true; } } exitFlag{exited};
        int failures = 0;
        for (;;) {
            { std::unique_lock<std::mutex> l(mu); cv.wait(l, [this] { return stop || enabled; }); if (stop) return; refresh = false; busy = true; lastTry = unixNow(); }
            storms::Data d; std::string err;
            const bool ok = storms::fetch(unixNow(), d, err);
            weatherLog(ok ? fmt("Storms: %s, %d active in the world", d.source.c_str(), (int)d.list.size()) : "Storms: " + err);
            std::unique_lock<std::mutex> l(mu);
            busy = false;
            if (ok) { result = d; ready = true; failures = 0; lastOk = unixNow(); lastErr.clear(); } else { ++failures; lastErr = err; }
            const double waitSec = ok ? 30 * 60.0 : std::min(30 * 60.0, 120.0 * (1 << std::min(failures, 4)));
            cv.wait_for(l, std::chrono::milliseconds((long long)(waitSec * 1000)), [this] { return stop || refresh; });
            if (stop) return;
        }
    }
};
StormWorker SW;

void applyStormConfig() { SW.configure(G.app.st.stormWatch); }

// called every frame: take a finished download, and re-check which storms are near when the place, radius or switch changed
void pollStorms() {
    bool changed = false;
    {
        std::lock_guard<std::mutex> l(SW.mu);
        if (SW.ready) { G.app.stormData = SW.result; SW.ready = false; changed = true; }
        G.app.stormTry = SW.lastTry; G.app.stormOk = SW.lastOk; G.app.stormErr = SW.lastErr; G.app.stormBusy = SW.busy;     // for Settings
    }
    static double lastLat = 1e9, lastLon = 1e9, lastAgeCheck = 0; static int lastRad = -1; static bool lastOn = true;
    Settings& st = G.app.st;
    const double tNow = unixNow();
    const bool ageTick = !G.app.stormNear.empty() && (tNow - lastAgeCheck >= 600.0 || tNow < lastAgeCheck);
    if (changed || ageTick || st.geo.lat != lastLat || st.geo.lon != lastLon || st.stormRadius != lastRad || st.stormWatch != lastOn) {
        lastLat = st.geo.lat; lastLon = st.geo.lon; lastRad = st.stormRadius; lastOn = st.stormWatch; lastAgeCheck = tNow;
        G.app.stormsChanged();
    }
}

void applyWeatherConfig() {
    std::lock_guard<std::mutex> l(W.mu);
    for (int i = 0; i < weather::PV_COUNT; ++i) W.enabled[i] = G.app.st.provEnabled[i];
    W.contact = G.app.st.contact;
    W.weatherApiKey = G.app.st.weatherApiKey;
    W.openWeatherKey = G.app.st.openWeatherKey;
}

void loadWeatherCache() {
    std::string txt;
    if (!readFile(G.forecastCache, txt)) return;
    size_t nl = txt.find('\n'); if (nl == std::string::npos) return;
    int id; double la, lo; if (sscanf(txt.c_str(), "%d %lf,%lf", &id, &la, &lo) != 3) return;
    if (id < 0 || id >= weather::PV_COUNT) return;
    if (std::fabs(la - G.app.st.geo.lat) > 0.01 || std::fabs(lo - G.app.st.geo.lon) > 0.01) return;
    weather::Data d; std::string err; d.utcOffset = G.app.tzOff;
    if (!weather::parseProvider(id, txt.substr(nl + 1), la, lo, d, err)) return;
    if (!d.hasAir) {
        std::string air; if (readFile(G.airCache, air)) weather::parseAirCache(air, la, lo, unixNow(), 6 * 3600.0, d, err);
    }
    d.fetched = fileMTime(G.forecastCache);
    G.app.wx = d;
}

void pollWeather() {
    std::lock_guard<std::mutex> l(W.mu);
    W.tz = localOffset(unixNow());
    G.app.wxProv = W.prov;
    if (W.ready && W.resultGen != W.locGen) W.ready = false;          // a reply for a place that is no longer the one shown
    if (W.ready) { G.app.wx = W.result; G.app.wxFetchedAt = unixNow(); W.ready = false; G.app.wxOffline = false; G.app.wxStatus.clear(); G.app.wxDetail.clear(); }
    else if (W.failing) { G.app.wxOffline = true; G.app.wxStatus = W.status; G.app.wxDetail = W.detail; }
}

// ---------------------------------------------------------------- window helpers
void updateCaption(bool force) {
    const gfx::Palette& p = G.app.pal;
    auto q = [](float v) { return (unsigned)std::lround(std::max(0.f, std::min(1.f, v)) * 255.f); };
    unsigned col = q(p.bgTop.r) | (q(p.bgTop.g) << 8) | (q(p.bgTop.b) << 16);
    if (!force && col == G.lastCaption) return;
    G.lastCaption = col;
    BOOL dark = p.dark ? TRUE : FALSE;
    DwmSetWindowAttribute(G.hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof dark);
    COLORREF cr = col, tx = p.dark ? RGB(150, 160, 180) : RGB(60, 70, 85);
    DwmSetWindowAttribute(G.hwnd, MY_DWMWA_CAPTION_COLOR, &cr, sizeof cr);
    DwmSetWindowAttribute(G.hwnd, MY_DWMWA_BORDER_COLOR, &cr, sizeof cr);
    DwmSetWindowAttribute(G.hwnd, MY_DWMWA_TEXT_COLOR, &tx, sizeof tx);
}

void applyTopmost() {
    const bool wantTop = G.saver || G.app.st.topmost;
    SetWindowPos(G.hwnd, wantTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    if (!G.saver && G.hwnd) {
        SetWindowTextW(G.hwnd, G.app.st.topmost ? L"Meridian \x2014 Always on top (press P to unpin)" : L"Meridian");
    }
}

// Holding the screen awake all day suits a clock that is plugged in. On the battery it would only drain it, so it lets go there
// (and takes hold again when the charger comes back: see PBT_APMPOWERSTATUSCHANGE in WndProc).
void applyKeepAwake() {
    const bool hold = G.app.st.keepAwake && !robust::onBattery();
    SetThreadExecutionState(hold ? (ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED) : ES_CONTINUOUS);
}

void setFullscreen(bool fs) {
    Settings& st = G.app.st;
    if (fs == st.fullscreen && ((GetWindowLongW(G.hwnd, GWL_STYLE) & WS_OVERLAPPEDWINDOW) == 0) == fs) return;
    st.fullscreen = fs;
    const HWND zOrder = (G.saver || st.topmost) ? HWND_TOPMOST : HWND_NOTOPMOST;
    if (fs) {
        G.prevPl.length = sizeof G.prevPl; GetWindowPlacement(G.hwnd, &G.prevPl);
        LONG style = GetWindowLongW(G.hwnd, GWL_STYLE);
        SetWindowLongW(G.hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
        MONITORINFO mi{sizeof mi}; GetMonitorInfoW(MonitorFromWindow(G.hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowPos(G.hwnd, zOrder, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    } else {
        LONG style = GetWindowLongW(G.hwnd, GWL_STYLE);
        SetWindowLongW(G.hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        if (G.prevPl.length) SetWindowPlacement(G.hwnd, &G.prevPl);
        SetWindowPos(G.hwnd, zOrder, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        applyTopmost();
    }
    markDirty();
}

void setFace(int f) {
    App& a = G.app;
    f = (f % FACE_COUNT + FACE_COUNT) % FACE_COUNT;
    if (f == a.st.face) return;
    a.st.face = f; a.faceChangedAt = unixNow(); a.glass.resetFaceSlides(); markDirty();
    if (f == FACE_WIDGETS) { a.wgtRot.cur = a.wgtRot.prev = -1; }          // start again from the first page
}

// an extra tab from a theme pack ("pack/face")
void setPackFace(const std::string& key) {
    App& a = G.app; themes::Pack* p = nullptr; themes::Face* f = nullptr;
    if (!themes::findFace(key, p, f) || !p->live(a.now, a.tzOff)) return;
    if (a.st.face == FACE_PACK && a.st.facePack == key) return;
    a.st.face = FACE_PACK; a.st.facePack = key; a.faceChangedAt = unixNow(); a.glass.resetFaceSlides(); markDirty();
}

// every face you can switch to, in dock order: the five built-in ones, then the tabs of the theme packs
std::vector<std::pair<int, std::string>> faceList() {
    std::vector<std::pair<int, std::string>> v;
    for (int i = 0; i < FACE_COUNT; ++i) v.push_back({i, ""});
    for (themes::Pack* p : G.app.livePacks()) for (themes::Face& f : p->faces) v.push_back({FACE_PACK, themes::faceKey(*p, f)});
    return v;
}
void gotoFaceIndex(int idx) {
    auto v = faceList();
    if (idx < 0 || idx >= (int)v.size()) return;
    if (v[(size_t)idx].first == FACE_PACK) setPackFace(v[(size_t)idx].second); else setFace(v[(size_t)idx].first);
}
void stepFace(int dir) {
    auto v = faceList(); const App& a = G.app;
    int cur = a.st.face;
    if (a.st.face == FACE_PACK) { cur = 0; for (size_t i = 0; i < v.size(); ++i) if (v[i].second == a.st.facePack) cur = (int)i; }
    gotoFaceIndex(((cur + dir) % (int)v.size() + (int)v.size()) % (int)v.size());
}

void toast(const std::wstring& s) { G.app.setToast(s); }
// asks for a redraw now. While rain or wind is paced to the screen the next frame is at most a refresh away, so an extra frame in between would only make the motion uneven.
void invalidate() { if (G.smooth) return; if (G.hwnd) InvalidateRect(G.hwnd, nullptr, FALSE); G.nextFrame = 0; }
// throttled redraw for high-frequency events (e.g. 500-1000 Hz mouse moves) so we never bypass the frame cap
void invalidateSoon() { if (G.smooth) return; double soon = unixNow() + 1.0 / 60.0; if (G.nextFrame > soon) G.nextFrame = soon; }

// T key: the built-in themes first, then every usable theme pack that is allowed to theme the whole clock ("global": true)
void cycleTheme(int dir) {
    App& a = G.app; Settings& st = a.st;
    const std::vector<themes::Pack*> packs = a.globalPacks();
    const int nb = THEME_COUNT, n = nb + (int)packs.size();
    int cur = themeSlot(st.theme);
    if (!st.themePack.empty()) for (size_t i = 0; i < packs.size(); ++i) if (packs[i]->id == st.themePack) cur = nb + (int)i;
    cur = ((cur + dir) % n + n) % n;
    if (cur < nb) { st.themePack.clear(); st.theme = THEME_ORDER[cur]; }
    else st.themePack = packs[(size_t)(cur - nb)]->id;
    toast(Ws(std::string("Theme · ") + a.themeName()));
    markDirty();
}

void applyPlaceChange() {
    Settings& st = G.app.st;
    G.geoPending = false;
    applyGeoFromPlace();
    G.app.wx = weather::Data(); G.app.wxStatus = "Connecting…";
    loadWeatherCache();
    W.request(st.geo.lat, st.geo.lon);
    markDirty();                                  // which place is on show is remembered for the next start
}

void switchPlace() {
    Settings& st = G.app.st;
    if (st.places.empty()) { toast(L"No extra places yet. Add some in Settings, under Place"); return; }
    int n = (int)st.places.size();
    G.app.placeIdx++; if (G.app.placeIdx >= n) G.app.placeIdx = -1;
    applyPlaceChange();
    toast(Ws(st.geo.name));
}

// Opens a file in its usual program. Always says exactly one thing about it.
void openFile(const std::wstring& path, const wchar_t* shown = nullptr, const wchar_t* extra = nullptr) {
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) { toast(L"Nothing there yet"); return; }
    const INT_PTR r = (INT_PTR)ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (r <= 32) { toast(shown ? std::wstring(L"Could not open ") + shown : std::wstring(L"Could not open the file")); return; }
    if (shown) toast(std::wstring(L"Opened ") + shown + (extra ? extra : L""));
}

// settings.ini is saved first so the file shows what is on screen. If that save fails the file still opens, and the one message says so.
void openSettingsIni() {
    const bool saved = saveSettings();
    openFile(G.settingsPath, L"settings.ini", saved ? nullptr : L", but the latest changes could not be saved first");
}

// ---------------------------------------------------------------- holidays.txt
// Dates typed into holidays.txt (next to settings.ini) replace the built-in or estimated ones. The file is read at start-up and read again
// within a few seconds of being saved, so a new Government list can be added without a restart. A bad line is skipped and reported once.
void loadHolidays(bool announce) {
    std::string text;
    G.holMTime = fileMTime(G.holPath);
    if (G.holMTime > 0 && !readFile(G.holPath, text)) text.clear();
    const std::string note = holiday::setOverrides(text);
    App& a = G.app;
    a.holCount = holiday::overrideCount(); a.holStatus = note;
    a.dayIndex = -1; a.minuteCacheAt = -1e9;                     // the Today list and the calendar are rebuilt with the new dates
    if (!announce) return;
    if (!note.empty()) toast(Ws("holidays.txt: " + note));
    else toast(Ws(a.holCount == 0 ? std::string("Holidays: nothing in holidays.txt") : fmt("Holidays updated \xC2\xB7 %d date%s from holidays.txt", a.holCount, a.holCount == 1 ? "" : "s")));
}
// Which countries' holidays are on: the ones switched on by hand, or the one the home place is in. Cheap enough to check every frame.
void syncHolidayCountries() {
    App& a = G.app; const Settings& st = a.st;
    const int mask = st.holAuto ? holiday::resolveAuto(a.homeCountry, a.homeRegion, a.home.lat, a.home.lon, a.sysCountry) : st.holMask;
    const int nation = holiday::resolveUkNation(st.ukPref, a.homeRegion, a.home.lat, a.home.lon);
    if (mask == holiday::countries() && (!(mask & holiday::C_UK) || nation == holiday::ukNation())) return;
    holiday::setCountries(mask, nation);
    a.dayIndex = -1; a.minuteCacheAt = -1e9;                       // the Today list and the calendar are rebuilt
    weatherLog("Holidays: " + holiday::countryList(mask, nation));
}
void pollHolidays(double now) {
    syncHolidayCountries();
    if (now < G.holCheckAt) return;
    G.holCheckAt = now + 3.0;
    if (fileMTime(G.holPath) != G.holMTime) loadHolidays(true);
}
void openHolidaysFile() {
    if (GetFileAttributesW(G.holPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        if (!writeFile(G.holPath, holiday::exampleFile())) { toast(L"Could not create holidays.txt"); return; }
        G.holMTime = fileMTime(G.holPath);                        // the starter file holds no dates: nothing to announce
    }
    openFile(G.holPath, L"holidays.txt");
}

// ---------------------------------------------------------------- find my location
// One lookup at a time, on its own thread (it can take up to about 25 seconds). The answer is applied on the main thread by pollLocate().
// It is kept in settings.ini and never looked up again by itself: only the FIND MY LOCATION button (Settings > Place) asks again,
// and a brand new install asks once on its first start.
struct LocState { std::mutex mu; bool done = false; locate::Found r; };
struct Locator {
    std::shared_ptr<LocState> st;
    std::thread th;
    std::atomic<bool> exited{true};
    bool autoRun = false;
    void start(bool autoMode, const std::string& ua) {
        if (th.joinable()) { if (exited) th.join(); else th.detach(); }
        auto s = std::make_shared<LocState>(); st = s; autoRun = autoMode; exited = false;
        // Test builds only (-DMERIDIAN_DEV): MERIDIAN_TEST_LOCATE="lat,lon,name,seconds" answers like a slow lookup would, without the network.
        std::string fake;
#ifdef MERIDIAN_DEV
        { wchar_t buf[256]; const DWORD n = GetEnvironmentVariableW(L"MERIDIAN_TEST_LOCATE", buf, 256); if (n > 0 && n < 256) fake = util::narrow(buf); }
#endif
        th = std::thread([this, s, ua, fake, autoMode] {
            struct ExitFlag { std::atomic<bool>& f; ~ExitFlag() { f = true; } } ef{exited};
            locate::Found f;
            double la = 0, lo = 0, delay = 0; char nm[128] = {0};
            if (!fake.empty() && sscanf(fake.c_str(), "%lf,%lf,%127[^,],%lf", &la, &lo, nm, &delay) >= 3) {
                Sleep((DWORD)(delay * 1000.0)); f.ok = true; f.lat = la; f.lon = lo; f.name = nm; f.source = "IP address";
            } else f = locate::find(ua, !autoMode);
            std::lock_guard<std::mutex> l(s->mu); s->r = std::move(f); s->done = true;
        });
    }
    bool finish(DWORD waitMs) {
        if (!th.joinable()) return true;
        const ULONGLONG t0 = GetTickCount64();
        while (!exited && GetTickCount64() - t0 < waitMs) Sleep(10);
        if (exited) { th.join(); return true; }
        th.detach(); return false;
    }
};
Locator LC;

void startLocate(bool autoMode) {
    App& a = G.app;
    if (a.locBusy) { toast(L"Already looking for your location"); return; }
    a.locBusy = true; a.locErr.clear();
    const std::string ua = weather::userAgentBase() + " (always-on clock)";          // the contact in Settings is for MET Norway only: the location services never get it
    LC.start(autoMode, ua);
    if (!autoMode) toast(L"Finding your location…");
}

void applyLocated(const locate::Found& f, bool autoMode) {
    App& a = G.app;
    a.locBusy = false;
    // The lookup that runs by itself (first start) never gets in the way of a choice you made: if you typed a place while it was still
    // working, it is done with and its answer is thrown away. Only the FIND MY LOCATION button replaces a place you chose.
    if (autoMode && a.located) { if (f.ok) weatherLog(fmt("Location: found %s via %s, but you had chosen a place meanwhile: kept yours", f.name.c_str(), f.source.c_str())); return; }
    if (!f.ok) {
        a.locErr = f.err;
        weatherLog("Location: " + f.err);
        toast(autoMode ? L"Could not find your location. Press FIND MY LOCATION in Settings > Place" : L"Could not find your location");
        return;
    }
    a.home.name = f.name.empty() ? std::string("My location") : f.name;
    a.home.lat = std::max(-89.0, std::min(89.0, std::round(f.lat * 10000.0) / 10000.0));
    a.home.lon = std::max(-180.0, std::min(180.0, std::round(f.lon * 10000.0) / 10000.0));
    if (!autoMode || a.placeIdx < 0) a.placeIdx = -1;      // the button shows the new position now, even if a saved place was on screen. The automatic lookup only
                                                           // fills in "your location": a saved place that you picked stays on show
    a.located = true; a.locSource = f.source; a.locAcc = f.accuracyM; a.locAt = unixNow(); a.locErr.clear();
    a.homeCountry = f.country; a.homeRegion = f.region;
    a.edInit = false;                                      // the boxes in Settings > Place load the new values
    applyPlaceChange();
    markDirty();
    weatherLog(fmt("Location: %s, %.4f, %.4f via %s", a.home.name.c_str(), a.home.lat, a.home.lon, f.source.c_str()));
    toast(Ws("Location \xC2\xB7 " + a.home.name));
}

void pollLocate() {
    if (!LC.st) return;
    locate::Found f; bool done = false;
    { std::lock_guard<std::mutex> l(LC.st->mu); if (LC.st->done) { f = LC.st->r; done = true; } }
    if (!done) return;
    LC.st.reset();
    applyLocated(f, LC.autoRun);
}

void command(int id) {
    App& a = G.app;
    switch (id) {
        case CMD_TOPMOST: applyTopmost(); toast(a.st.topmost ? L"Kept on top" : L"Normal window"); break;
        case CMD_FULLSCREEN: setFullscreen(!a.st.fullscreen); break;
        case CMD_KEEPAWAKE: applyKeepAwake(); break;
        case CMD_WEATHER_CFG: applyWeatherConfig(); break;
        case CMD_STORM_CFG: applyStormConfig(); break;
        case CMD_STORM_REFRESH: if (!a.st.stormWatch) toast(L"Turn on Show storms near me first"); else { SW.request(); a.stormBusy = true; toast(L"Asking for storm reports"); } break;
        case CMD_WEATHER_REFRESH: applyWeatherConfig(); a.wxStatus = "Refreshing…"; W.request(a.st.geo.lat, a.st.geo.lon); break;
        case CMD_GEO: applyPlaceChange(); break;
        case CMD_GEO_SOON: G.geoPending = true; G.geoAt = unixNow() + 1.1; break;
        case CMD_LOCATE: startLocate(false); break;
        case CMD_OPEN_LOCATION_SETTINGS: ShellExecuteW(nullptr, L"open", L"ms-settings:privacy-location", nullptr, nullptr, SW_SHOWNORMAL); break;
        case CMD_SAVE: markDirty(); break;
        case CMD_OPEN_INI: openSettingsIni(); break;
        case CMD_OPEN_HOLIDAYS: openHolidaysFile(); break;
        case CMD_OPEN_LOG: openFile(G.logPath, L"weather.log"); break;
        case CMD_OPEN_THEMES: {                                    // the themes folder next to Meridian.exe (made if it is not there yet)
            const auto r = themes::roots();
            if (!r.empty()) { std::error_code ec; std::filesystem::create_directories(r[0], ec); ShellExecuteW(nullptr, L"open", r[0].wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
        } break;
        case CMD_RESCAN_THEMES: a.rescanPacks(G.gfx); toast(Ws(fmt("Themes · %d found", (int)themes::all().size()))); break;
        default: break;
    }
}

// ---------------------------------------------------------------- text fields of the settings panel
// The editing itself (caret, selection, undo) is in textedit.h, where it is tested on its own. This part connects it to the keyboard
// and the clipboard.
void textBlur() { App& a = G.app; a.textTarget = nullptr; a.textChanged = nullptr; }
void textTouched(bool edited) {
    App& a = G.app;
    a.textFocusAt = unixNow();                             // the caret shows solid right after anything happens
    if (edited && a.textChanged) a.textChanged();
}
void textType(const std::wstring& w) {
    App& a = G.app;
    if (!a.textTarget) return;
    const std::string in = textedit::sanitize(util::narrow(w), a.textMulti, a.textNumeric);
    if (textedit::insert(*a.textTarget, a.ted, in, a.textMax, in.size() <= 4 ? 1 : 0)) textTouched(true);       // one character = typing; more = a paste
}
void textPaste() {
    if (!G.app.textTarget) return;
    if (!OpenClipboard(G.hwnd)) return;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const wchar_t* p = (const wchar_t*)GlobalLock(h);
        if (p) { size_t n = 0; while (n < 16384 && p[n]) ++n; std::wstring w(p, n); GlobalUnlock(h); textType(w); }     // the first 16K characters are plenty for any field
    }
    CloseClipboard();
}
void textCopy(const std::string& utf8) {
    if (utf8.empty() || !OpenClipboard(G.hwnd)) return;
    std::wstring o;
    for (wchar_t c : util::widen(utf8)) { if (c == L'\n') o.push_back(L'\r'); o.push_back(c); }              // Windows line breaks
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (o.size() + 1) * sizeof(wchar_t));
    if (h) {
        wchar_t* p = (wchar_t*)GlobalLock(h);
        if (p) { memcpy(p, o.c_str(), (o.size() + 1) * sizeof(wchar_t)); GlobalUnlock(h); if (!SetClipboardData(CF_UNICODETEXT, h)) GlobalFree(h); }
        else GlobalFree(h);
    }
    CloseClipboard();
}
// returns true when the key was consumed by the focused text field
bool textKey(WPARAM vk, bool ctrl, bool shift) {
    App& a = G.app;
    if (!a.textTarget) return false;
    std::string& t = *a.textTarget; textedit::State& s = a.ted;
    textedit::clamp(t, s);
    const std::vector<size_t>* lines = a.textMulti ? &a.textLines : nullptr;
    static const std::vector<size_t> none;
    const std::vector<size_t>& vl = lines ? *lines : none;
    bool edited = false;
    auto go = [&](size_t pos) { textedit::moveTo(t, s, pos, shift); };
    switch (vk) {
        case VK_TAB: textBlur(); return false;                // let Tab leave the text field and advance Settings keyboard focus
        case VK_ESCAPE: textBlur(); return true;
        case VK_RETURN: if (a.textMulti) textType(L"\n"); else textBlur(); return true;
        case VK_BACK: edited = textedit::backspace(t, s, ctrl); break;
        case VK_DELETE: edited = textedit::del(t, s, ctrl); break;
        case VK_LEFT:  if (s.hasSel() && !shift && !ctrl) go(s.lo()); else go(ctrl ? textedit::wordLeft(t, s.caret) : textedit::prevChar(t, s.caret)); break;
        case VK_RIGHT: if (s.hasSel() && !shift && !ctrl) go(s.hi()); else go(ctrl ? textedit::wordRight(t, s.caret) : textedit::nextChar(t, s.caret)); break;
        case VK_HOME: go(ctrl || !a.textMulti ? 0 : textedit::visualLineStart(t, vl, s.caret)); break;
        case VK_END:  go(ctrl || !a.textMulti ? t.size() : textedit::visualLineEnd(t, vl, s.caret)); break;
        case VK_UP:   go(a.textMulti ? textedit::verticalTarget(t, vl, s.caret, -1) : 0); break;
        case VK_DOWN: go(a.textMulti ? textedit::verticalTarget(t, vl, s.caret, +1) : t.size()); break;
        case VK_INSERT: if (shift) textPaste(); else if (ctrl) textCopy(textedit::selected(t, s)); break;
        case 'A': if (ctrl) textedit::selectAll(t, s); break;
        case 'C': if (ctrl) textCopy(textedit::selected(t, s)); break;
        case 'X': if (ctrl) { textCopy(textedit::selected(t, s)); edited = textedit::deleteSelection(t, s); } break;
        case 'V': if (ctrl) textPaste(); break;
        case 'Z': if (ctrl) edited = shift ? textedit::redo(t, s) : textedit::undo(t, s); break;
        case 'Y': if (ctrl) edited = textedit::redo(t, s); break;
        default: break;                                   // everything else is typing (arrives as WM_CHAR) or a key a text box has no use for
    }
    textTouched(edited);
    return true;                                          // the text box has the keyboard: no face switching, no shortcuts
}

void timerAdjustKey(int dir, bool shift) {
    TimerState& t = G.app.timer;
    if (t.mode == 1) return;
    double base = 60.0 * dir;
    if (shift) base *= (t.mode == 2 ? 60.0 : 10.0);
    G.app.timerAdjust(base);
}

void perform(int id) {
    App& a = G.app; TimerState& t = a.timer;
    if (id >= HIT_FACE0 && id < HIT_FACE0 + FACE_COUNT) setFace(id - HIT_FACE0);
    else if (id >= HIT_PACKFACE0 && id < HIT_PACKFACE0 + 64) { const size_t k = (size_t)(id - HIT_PACKFACE0); if (k < a.packFaceKeys.size()) setPackFace(a.packFaceKeys[k]); }
    else if (id == HIT_THEME) cycleTheme(1);
    else if (id == HIT_THEME_PREV) cycleTheme(-1);
    else if (id == HIT_FULLSCREEN) setFullscreen(!a.st.fullscreen);
    else if (id == HIT_HELP) { a.showHelp = !a.showHelp; a.dockT = a.dockTarget = 0; a.glass.cancelHold(); }
    else if (id == HIT_SETTINGS) { a.showSettings = !a.showSettings; a.dockT = a.dockTarget = 0; a.glass.cancelHold(); }
    else if (id == HIT_DOCK_BG || id == HIT_FCARD_CLOCK || id == HIT_FCARD_WEATHER || id == HIT_SKY_AQI) { /* passive: absorbs clicks / tactile Liquid Glass touch cards & AQI pill */ }
    else if (id == HIT_PANEL_PREV) a.skyRot.pending = -1;
    else if (id == HIT_PANEL_NEXT) a.skyRot.pending = 1;
    else if (id == HIT_WGT_PREV) a.wgtRot.pending = -1;
    else if (id == HIT_WGT_NEXT) a.wgtRot.pending = 1;
    else if (id == HIT_CAL_PREV) a.calOffset--;
    else if (id == HIT_CAL_NEXT) a.calOffset++;
    else if (id == HIT_CAL_TODAY) a.calOffset = 0;
    else if (id == HIT_PIN) { a.st.topmost = !a.st.topmost; applyTopmost(); toast(a.st.topmost ? L"Kept on top" : L"Normal window"); markDirty(); }
    else if (id >= HIT_TAB0 && id < HIT_TAB0 + 3) { t.mode = id - HIT_TAB0; t.entry.clear(); }
    else if (id >= HIT_PRESET0 && id < HIT_PRESET0 + 7) { a.timerSetDuration(TIMER_PRESETS[id - HIT_PRESET0]); t.entry.clear(); markDirty(); }
    else if (id == HIT_START) { a.timerStartPause(); markDirty(); }
    else if (id == HIT_RESET) a.timerReset();
    else if (id == HIT_LAP) a.stopwatchLap();
    else if (id >= HIT_FPRESET0 && id < HIT_FPRESET0 + 7) { a.focusSetDuration(FOCUS_PRESETS[id - HIT_FPRESET0]); markDirty(); }
    else if (id == HIT_FSTART) { a.focusStartPause(); markDirty(); }
    else if (id == HIT_FRESET) a.focusReset();
    else if (id >= HIT_FOCUSFX0 && id < HIT_FOCUSFX0 + 14) {
        static const wchar_t* const kFxToast[14] = {L"Off", L"Live weather", L"Rain", L"Snow", L"Clouds", L"Fog", L"Storm", L"Stars", L"Blizzard", L"Drizzle", L"Sleet", L"Hail", L"Gale", L"Midnight"};
        const int m = id - HIT_FOCUSFX0;
        a.st.focusFx = m;
        markDirty();
        toast(std::wstring(L"Focus ambience \xB7 ") + kFxToast[m]);
    }
}

void handleSecretKey(wchar_t kch) {
    App& a = G.app;
    const auto act = ufoaudio::feedSecretChar(a.secretBuf, kch);
    if (act == ufoaudio::SecretAction::None) return;
    switch (act) {
        case ufoaudio::SecretAction::DisableAliens:
            a.st.aliensEnabled = false;
            a.aliensNuts = false;
            a.alienAt = -1e9;
            ufoaudio::stop();
            markDirty();
            toast(L"Aliens don't exist. (UFO & music off)");
            break;
        case ufoaudio::SecretAction::EnableAliens:
            a.st.aliensEnabled = true;
            markDirty();
            toast(L"Aliens do exist! (UFO & music on)");
            break;
        case ufoaudio::SecretAction::AliensNuts:
            a.st.aliensEnabled = true;
            a.aliensNuts = true;
            a.aliensNutsSince = unixNow();
            markDirty();
            toast(L"Aliens are nuts! (2x per minute on Galaxy)");
            break;
        case ufoaudio::SecretAction::AliensNormal:
            a.aliensNuts = false;
            a.alienAt = -1e9;
            ufoaudio::stop();
            toast(L"Aliens calmed down (back to once a day)");
            break;
        case ufoaudio::SecretAction::ToggleHippy: {
            const auto nowTrack = ufoaudio::toggleTrack(ufoaudio::Track::HippyShit);
            toast(nowTrack == ufoaudio::Track::HippyShit
                ? L"Now playing \xB7 Full track (type again in About to stop)"
                : L"Music stopped");
            break;
        }
        case ufoaudio::SecretAction::ToggleResearched: {
            const auto nowTrack = ufoaudio::toggleTrack(ufoaudio::Track::Researched);
            toast(nowTrack == ufoaudio::Track::Researched
                ? L"Now playing \xB7 Bob Bradley \x2014 Researched (type again in About to stop)"
                : L"Music stopped");
            break;
        }
        case ufoaudio::SecretAction::ToggleJukebox: {
            const bool on = ufoaudio::toggleJukebox();
            toast(on
                ? L"Jukebox on \xB7 Playing all tracks (type jukebox or killjukebox to stop)"
                : L"Jukebox stopped");
            break;
        }
        case ufoaudio::SecretAction::KillJukebox:
            ufoaudio::stopTrack();
            toast(L"Jukebox stopped");
            break;
        default: break;
    }
    invalidate();
}

void dismissAlerts() {
    TimerState& t = G.app.timer; FocusState& f = G.app.focus;
    const bool wasFinished = t.finished;
    t.finished = false; t.ringing = false;
    if (wasFinished) t.pausedRemaining = t.durationSec;       // do not wipe a paused countdown when dismissing a ringing alarm
    if (f.finished) { f.finished = false; f.pausedRemaining = f.durationSec; }
    sirenStop(); G.finishHandled = G.focusFinishHandled = G.ringHandled = false;
}

void onKey(WPARAM vk, bool shift, bool ctrl, bool alt, bool repeat) {
    App& a = G.app; Settings& st = a.st; TimerState& t = a.timer; FocusState& fc = a.focus;
    bool timerFace = st.face == FACE_TIMER;
    bool focusFace = st.face == FACE_FOCUS;
    if (textKey(vk, ctrl, shift)) return;                           // (typing keeps repeating)
    // Holding a key down sends it again and again. Only the keys that adjust something should repeat (arrows, + and -, Backspace, the brightness
    // brackets). A held S, C, H, P, K, B, M, T, N, W, R or G would flip the setting back and forth, refresh the weather over and over, or open settings.ini
    // in a new window each time; a held digit would type the same digit again.
    if (repeat && vk != VK_LEFT && vk != VK_RIGHT && vk != VK_UP && vk != VK_DOWN && vk != VK_ADD && vk != VK_SUBTRACT &&
        vk != VK_OEM_PLUS && vk != VK_OEM_MINUS && vk != VK_BACK && vk != VK_OEM_4 && vk != VK_OEM_6 && vk != VK_TAB) return;
    if (a.showSettings && !a.showHelp && a.settingsKey((unsigned)vk, shift)) { invalidate(); return; }      // Tab/Space/Enter focus + What's new scrolling
    if (vk == VK_ESCAPE) {
        if (a.showHelp) a.showHelp = false;
        else if (a.showSettings) { a.showSettings = false; markDirty(); }
        else if (t.finished || t.ringing || fc.finished) dismissAlerts();
        else if (st.fullscreen) setFullscreen(false);
        return;
    }
    if (vk == VK_F1 || vk == VK_OEM_2 || (vk == 'I' && !a.inAboutTab())) { a.showHelp = !a.showHelp; a.dockT = a.dockTarget = 0; a.secretBuf.clear(); return; }
    if (a.showHelp) { a.showHelp = false; return; }
    if (vk == VK_OEM_COMMA) { a.showSettings = !a.showSettings; a.dockT = a.dockTarget = 0; if (!a.showSettings) markDirty(); return; }
    if (alt && vk == VK_RETURN) { setFullscreen(!st.fullscreen); return; }
    if (vk == VK_F11 || (vk == 'F' && !ctrl && !a.showSettings)) { setFullscreen(!st.fullscreen); return; }
    if (a.showSettings) {                                           // Settings overlay is modal: do not leak clock hotkeys behind it
        // Raw virtual-key cheat capture on Settings > About: 100% immune to Caps Lock, Shift, and keyboard layout
        if (!ctrl && !alt && !a.textTarget && a.inAboutTab()) {
            wchar_t kch = 0;
            if (vk >= 'A' && vk <= 'Z') kch = (wchar_t)vk;
            else if (vk == VK_BACK) kch = L'\b';
            if (kch) handleSecretKey(kch);
        }
        return;
    }
    if (vk == VK_LEFT) { stepFace(-1); return; }
    if (vk == VK_RIGHT) { stepFace(+1); return; }
    // digits: timer / alarm / focus duration entry on Timer & Focus faces, face switching elsewhere
    int digit = -1;
    if (vk >= '0' && vk <= '9') digit = (int)(vk - '0');
    else if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) digit = (int)(vk - VK_NUMPAD0);
    if (digit >= 0) {
        if (timerFace && t.mode != 1) {
            if (t.finished || t.ringing) dismissAlerts();
            if (t.mode == 0 && t.running) { toast(L"Pause the timer (Space) to type a new time"); return; }       // typing a time used to stop a running countdown and reset it
            a.timerDigit(digit); markDirty();
        } else if (focusFace) {
            if (fc.finished) dismissAlerts();
            if (fc.running) { toast(L"Pause Focus (Space) to type a new time"); return; }
            a.focusDigit(digit); markDirty();
        }
        else if (!timerFace && !focusFace && digit >= 1) gotoFaceIndex(digit - 1);
        return;
    }
    switch (vk) {
        case VK_TAB: if (st.face == FACE_SKY || st.face == FACE_WIDGETS) { a.panelNext(shift ? -1 : 1); markDirty(); }
                     else if (st.face == FACE_PACK && a.packKey(VK_TAB)) markDirty();         // a theme tab's own button (themes/README.txt: "key")
                     return;
        case 'T': cycleTheme(shift ? -1 : 1); return;
        case VK_SPACE: case VK_RETURN:
            if (t.finished || t.ringing || fc.finished) { dismissAlerts(); return; }
            if (timerFace) { a.timerStartPause(); markDirty(); }
            else if (focusFace) { a.focusStartPause(); markDirty(); }
            else if (st.face == FACE_PACK && a.packKey((int)vk)) markDirty();
            return;
        case 'R': if (timerFace) a.timerReset(); else if (focusFace) a.focusReset(); else if (shift) { a.wx = weather::Data(); W.request(st.geo.lat, st.geo.lon); } return;
        case 'L': if (timerFace) a.stopwatchLap(); return;
        case VK_BACK: if (timerFace) a.timerBackspace(); else if (focusFace) a.focusBackspace(); return;
        case VK_ADD: case VK_OEM_PLUS: if (timerFace) { timerAdjustKey(+1, shift); markDirty(); } else if (focusFace) { a.focusAdjust((shift ? 600.0 : 60.0)); markDirty(); } return;
        case VK_SUBTRACT: case VK_OEM_MINUS: if (timerFace) { timerAdjustKey(-1, shift); markDirty(); } else if (focusFace) { a.focusAdjust((shift ? -600.0 : -60.0)); markDirty(); } return;
        case VK_UP: if (timerFace) { timerAdjustKey(+1, shift); markDirty(); } else if (focusFace) { a.focusAdjust((shift ? 600.0 : 60.0)); markDirty(); } else if (st.face == FACE_PACK && a.packKey(VK_UP)) markDirty(); return;
        case VK_DOWN: if (timerFace) { timerAdjustKey(-1, shift); markDirty(); } else if (focusFace) { a.focusAdjust((shift ? -600.0 : -60.0)); markDirty(); } else if (st.face == FACE_PACK && a.packKey(VK_DOWN)) markDirty(); return;
        case 'S': st.seconds = !st.seconds; toast(st.seconds ? L"Seconds on" : L"Seconds off"); markDirty(); return;
        case 'C': st.useF = !st.useF; toast(st.useF ? L"Fahrenheit" : L"Celsius"); markDirty(); return;
        case 'H': st.h24 = !st.h24; a.minuteCacheAt = -1e9; a.dayIndex = -1; toast(st.h24 ? L"24-hour clock" : L"12-hour clock"); markDirty(); return;
        case 'P': st.topmost = !st.topmost; applyTopmost(); toast(st.topmost ? L"Kept on top" : L"Normal window"); markDirty(); return;
        case VK_OEM_4: st.brightness = std::max(0.15f, st.brightness - 0.1f); toast(Ws(fmt("Brightness %.0f%%", st.brightness * 100))); markDirty(); return;
        case VK_OEM_6: st.brightness = std::min(1.0f, st.brightness + 0.1f); toast(Ws(fmt("Brightness %.0f%%", st.brightness * 100))); markDirty(); return;
        case 'U':
            if (!st.aliensEnabled) return;
            if (st.theme == THEME_GALAXY && st.spaceFx && (st.face == FACE_QUIET || st.face == FACE_SKY) && !a.packTheme()) {
                ++a.alienSeq; a.alienAt = unixNow(); toast(L"Hello");
            } else {
                toast(L"Switch to Galaxy theme (Sky or Quiet) to summon the visitor");
            }
            return;           // an easter egg: calls the saucer (Galaxy theme, Quiet and Sky faces)
        case 'B': st.emberAtNight = !st.emberAtNight; toast(st.emberAtNight ? Ws(fmt("Red night mode %02d:00–%02d:00", st.emberFrom, st.emberTo)) : L"Red night mode off"); markDirty(); return;
        case 'W': a.wxStatus = "Refreshing…"; W.request(st.geo.lat, st.geo.lon); toast(L"Refreshing weather"); return;
        case 'N': switchPlace(); return;
        case 'M': st.pixelShift = !st.pixelShift; toast(st.pixelShift ? L"Burn-in shift on" : L"Burn-in shift off"); markDirty(); return;
        case 'K': st.keepAwake = !st.keepAwake; applyKeepAwake(); toast(st.keepAwake ? L"Keeping the screen awake" : L"Screen may sleep"); markDirty(); return;
        case 'G': openSettingsIni(); return;
        default: break;
    }
}

// ---------------------------------------------------------------- right-click menu
// The things you reach for most. (Right-click used to change the theme at once, which is easy to do by accident on a wall-mounted
// touch screen; a long press on a touch screen opens this menu too.)
void showContextMenu(HWND hwnd, POINT pt) {                       // pt in screen coordinates
    App& a = G.app; Settings& st = a.st;
    enum { M_THEME = 1, M_FULL, M_TOP, M_AWAKE, M_SHIFT, M_DIM, M_START, M_SETTINGS, M_HELP, M_EXIT, M_FACE0 = 100 };
    HMENU menu = CreatePopupMenu(), faces = CreatePopupMenu();
    if (!menu || !faces) { if (menu) DestroyMenu(menu); if (faces) DestroyMenu(faces); return; }
    static const wchar_t* const builtin[FACE_COUNT] = {L"Sky", L"Quiet", L"Timer", L"Almanac", L"Widgets", L"Focus"};
    const auto fl = faceList();
    for (size_t i = 0; i < fl.size() && i < 60; ++i) {
        std::wstring label = L"Theme pack";
        bool cur = false;
        if (fl[i].first == FACE_PACK) {
            themes::Pack* pk = nullptr; themes::Face* fc = nullptr;
            if (themes::findFace(fl[i].second, pk, fc) && fc) label = Ws(fc->name);
            cur = st.face == FACE_PACK && st.facePack == fl[i].second;
        } else if (fl[i].first >= 0 && fl[i].first < FACE_COUNT) { label = builtin[fl[i].first]; cur = st.face == fl[i].first; }
        AppendMenuW(faces, MF_STRING | (cur ? MF_CHECKED : 0), (UINT_PTR)(M_FACE0 + i), label.c_str());
    }
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)faces, L"Face");
    AppendMenuW(menu, MF_STRING, M_THEME, L"Next theme\tT");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (st.fullscreen ? MF_CHECKED : 0), M_FULL, L"Fullscreen\tF11");
    AppendMenuW(menu, MF_STRING | (st.topmost ? MF_CHECKED : 0), M_TOP, L"Keep on top\tP");
    AppendMenuW(menu, MF_STRING | (st.keepAwake ? MF_CHECKED : 0), M_AWAKE, L"Keep screen awake\tK");
    AppendMenuW(menu, MF_STRING | (st.pixelShift ? MF_CHECKED : 0), M_SHIFT, L"Burn-in shift\tM");
    AppendMenuW(menu, MF_STRING | (st.autoDim ? MF_CHECKED : 0), M_DIM, L"Dim with the sun");
    AppendMenuW(menu, MF_STRING | (robust::startWithWindows() ? MF_CHECKED : 0), M_START, L"Start with Windows");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, M_SETTINGS, L"Settings…\t,");
    AppendMenuW(menu, MF_STRING, M_HELP, L"Shortcuts\tF1");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, M_EXIT, L"Exit");

    robust::setMenuDark(a.pal.dark);
    SetForegroundWindow(hwnd);                                    // so the menu closes when you click elsewhere
    const int cmd = (int)TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);                                            // takes the Face submenu with it
    if (cmd >= M_FACE0) { gotoFaceIndex(cmd - M_FACE0); return; }
    switch (cmd) {
        case M_THEME: cycleTheme(1); break;
        case M_FULL: setFullscreen(!st.fullscreen); break;
        case M_TOP: st.topmost = !st.topmost; applyTopmost(); toast(st.topmost ? L"Kept on top" : L"Normal window"); markDirty(); break;
        case M_AWAKE: st.keepAwake = !st.keepAwake; applyKeepAwake(); toast(st.keepAwake ? L"Keeping the screen awake" : L"Screen may sleep"); markDirty(); break;
        case M_SHIFT: st.pixelShift = !st.pixelShift; toast(st.pixelShift ? L"Burn-in shift on" : L"Burn-in shift off"); markDirty(); break;
        case M_DIM: st.autoDim = !st.autoDim; toast(st.autoDim ? L"Dimming with the sun" : L"Brightness stays as set"); markDirty(); break;
        case M_START: {
            const bool want = !robust::startWithWindows();
            if (robust::setStartWithWindows(want)) toast(want ? L"Meridian will start with Windows" : L"Meridian will not start with Windows");
            else toast(L"Could not change the startup setting");
            break;
        }
        case M_SETTINGS: a.showSettings = !a.showSettings; a.dockT = a.dockTarget = 0; if (!a.showSettings) markDirty(); break;
        case M_HELP: a.showHelp = !a.showHelp; a.dockT = a.dockTarget = 0; break;
        case M_EXIT: PostMessageW(hwnd, WM_CLOSE, 0, 0); break;
        default: break;
    }
}

// ---------------------------------------------------------------- frame
bool paletteSettled() { return !G.app.palMoving; }

// a minimized clock cannot show that its timer ended or its alarm rang, so its taskbar button flashes until it is opened
void flashIfMinimized() {
    if (!G.hwnd || !IsIconic(G.hwnd)) return;
    FLASHWINFO f{}; f.cbSize = sizeof f; f.hwnd = G.hwnd; f.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG; f.uCount = 0; f.dwTimeout = 0;
    FlashWindowEx(&f);
}

void tickTimers(double now) {
    App& a = G.app; TimerState& t = a.timer; FocusState& fc = a.focus;
    a.timerTick();
    if (t.finished && !G.finishHandled) { G.finishHandled = true; a.st.face = FACE_TIMER; t.mode = 0; a.faceChangedAt = now; sirenStart(); flashIfMinimized(); }
    if (fc.finished && !G.focusFinishHandled) { G.focusFinishHandled = true; a.st.face = FACE_FOCUS; a.faceChangedAt = now; sirenStart(); flashIfMinimized(); }
    if (t.ringing && !G.ringHandled) { G.ringHandled = true; a.st.face = FACE_TIMER; t.mode = 2; a.faceChangedAt = now; sirenStart(); flashIfMinimized(); }
    if (!t.finished) G.finishHandled = false;
    if (!fc.finished) G.focusFinishHandled = false;
    if (!t.ringing) G.ringHandled = false;
    if (G.sirenOn && now - G.sirenStart > 120.0) sirenStop();
    if (G.sirenOn && !t.finished && !fc.finished && !t.ringing) sirenStop();
    // Optional soft hourly chime (off by default, silent during bedtime/Ember, saver, alarm/timer siren, or music playback)
    const long long hourIdx = (long long)std::floor((now + a.tzOff) / 3600.0);
    if (G.lastChimeHour < 0) {
        G.lastChimeHour = hourIdx;
    } else if (hourIdx != G.lastChimeHour) {
        if (hourIdx == G.lastChimeHour + 1 && a.st.hourlyChime && !G.saver && !G.sirenOn &&
            !t.finished && !fc.finished && !t.ringing && ufoaudio::currentTrack() == ufoaudio::Track::None) {
            const util::LocalTime L = util::toLocal(now, a.tzOff);
            bool bed = false;
            if (a.st.emberAtNight) bed = a.st.emberFrom > a.st.emberTo ? (L.hh >= a.st.emberFrom || L.hh < a.st.emberTo) : (L.hh >= a.st.emberFrom && L.hh < a.st.emberTo);
            if (!bed && a.st.theme != THEME_EMBER) playHourlyChime();
        }
        G.lastChimeHour = hourIdx;
    }
}

// the refresh rate of the screen the window is on (60 when Windows will not say)
double displayHz() {
    if (!G.hwnd) return 60.0;
    MONITORINFOEXW mi; mi.cbSize = sizeof mi;
    DEVMODEW dm; memset(&dm, 0, sizeof dm); dm.dmSize = sizeof dm;
    if (GetMonitorInfoW(MonitorFromWindow(G.hwnd, MONITOR_DEFAULTTONEAREST), &mi) && EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)
        && dm.dmDisplayFrequency >= 24 && dm.dmDisplayFrequency < 1000) return (double)dm.dmDisplayFrequency;
    return 60.0;
}

// Rebuilds the window's render target when a frame said it was lost (App::wantRecreate) or when an earlier rebuild failed and there is
// no target at all. Returns true when there is a target to draw into. A failed rebuild is not forgotten: it is tried again about once a
// second, and until it works nothing is drawn and nothing touches the missing target (before, one failed attempt left the window blank for good).
bool ensureTarget(double now, int w, int h) {
    App& a = G.app;
    if (!a.wantRecreate && G.gfx.hasTarget()) return true;
    if (now < G.recreateWaitUntil) return G.gfx.hasTarget();
    static bool busy = false;                                      // (making a target can send the window a message that draws a frame: never rebuild inside a rebuild)
    if (busy) return G.gfx.hasTarget();
    struct Busy { bool& f; Busy(bool& b) : f(b) { f = true; } ~Busy() { f = false; } } busyGuard(busy);
    a.wantRecreate = false;
    a.releaseDeviceResources(G.gfx);
    if (G.gfx.createWindowTarget(G.hwnd, w, h)) {
        if (G.targetLogged) weatherLog("Graphics: the render target is back");
        G.recreateWaitUntil = 0; G.targetLogged = false;
        return true;
    }
    G.recreateWaitUntil = now + 1.0;
    if (!G.targetLogged) { G.targetLogged = true; weatherLog("Graphics: could not rebuild the render target, trying again every second"); }
    return false;
}

void renderFrame() {
    if (!G.hwnd) return;
    RECT rc; GetClientRect(G.hwnd, &rc);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    // Minimized (or too small to draw into): only the DRAWING is skipped. Time, timers, alarms, downloads and saving go on, and the
    // next look is always scheduled (below). Returning here without scheduling used to make the loop spin at full speed and left a
    // timer or alarm unchecked until the window came back.
    const bool tiny = IsIconic(G.hwnd) || w < 8 || h < 8;
    App& a = G.app;
    double now = unixNow();
    if (!tiny && !(G.saver && G.leaving)) ensureTarget(now, w, h);   // a render target lost earlier (GPU reset, display change) is rebuilt before anything is drawn
    const bool noTarget = tiny || !G.gfx.hasTarget();              // (no target at all: only the drawing waits, everything else carries on, as when minimised)
    if (G.saver && G.leaving && noTarget) saverQuit("the picture could not be drawn");        // (a screen saver on its way out never waits for graphics to come back)
    LARGE_INTEGER q0; QueryPerformanceCounter(&q0);
    G.paceStart = now;
    robust::noteRunning();
    // Was the system clock set forwards or back since the last frame (a time-sync correction, someone changing the time)? A running
    // countdown and stopwatch are counted on the clock, so they are moved by the same amount and carry on as if nothing happened.
    // (The alarm is NOT moved: "7:00 AM" means 7:00 on the clock, whatever the clock says.)
    const double step = G.clock.sample(now, (double)GetTickCount64() / 1000.0);
    if (step != 0.0) {
        TimerState& tm = a.timer;
        if (tm.running) tm.endAt += step;
        if (tm.swRunning) tm.swStart += step;
        if (a.focus.running) a.focus.endAt += step;
        a.lastMouseMove += step; a.faceChangedAt += step; a.toastUntil += step;
        a.skyRot.switchAt += step; a.wgtRot.switchAt += step;
        a.spinT0 += step; a.alienAt += step; a.textFocusAt += step;
        if (a.aliensNuts) a.aliensNutsSince += step;
        G.lastChimeHour = -1;
        a.lastItemsAt += step; a.themeScanAt += step;
        a.minuteCacheAt += step; a.planetCacheAt += step; a.eventsAt += step; a.moonDetail.builtAt += step;
        if (a.revealStart > 0) a.revealStart += step;
        if (a.leaveStart > 0) a.leaveStart += step;
        G.sirenStart += step; G.lastFrame += step; G.lastDraw += step; G.lastEnd += step; G.paceStart += step;
        G.saveAt += step; G.holCheckAt += step; G.geoAt += step; G.recreateWaitUntil += step; G.refreshAt += step;
        weatherLog(fmt("Clock: the system time moved by %+.1f s", step));
    }
    double dt = G.lastFrame > 0 ? std::max(0.0, std::min(1.0, now - G.lastFrame)) : 1.0;
    G.lastFrame = now;
    if (G.deferWorkers && !a.revealBusy() && a.revealFrames >= 2) { G.deferWorkers = false; W.start(); SW.start(); }      // (the screen saver: the downloads wait until its start is over)
    pollWeather();
    pollStorms();
    pollLocate();
    pollHolidays(now);
    const int sysTz = localOffset(now);
    const bool usePlaceTz = a.placeIdx >= 0 && a.st.placeLocalTime && a.wx.valid;
    a.tzAt = usePlaceTz ? nullptr : localOffset;
    a.sysTzAt = localOffset;
    a.setNow(now, usePlaceTz ? a.wx.utcOffset : sysTz, dt);
    tickTimers(now);
    ufoaudio::setSuppressed(G.sirenOn);
    if (G.geoPending && now >= G.geoAt) command(CMD_GEO);
    if (noTarget) {
        ufoaudio::stop();
        G.smooth = false;                                          // (so invalidate() works the moment the window is back)
        saveIfDue(now);
        double next = now + 0.5;                                   // a slow beat is enough: nothing is on show
        if (a.timer.running) next = std::min(next, std::max(now + 0.02, a.timer.endAt));
        if (a.focus.running) next = std::min(next, std::max(now + 0.02, a.focus.endAt));
        if (a.timer.alarmOn) next = std::min(next, std::max(now + 0.02, a.timer.alarmAt));
        G.nextFrame = next;
        return;
    }
    a.update(G.gfx);
    const bool hidden = G.gfx.occluded();                          // covered by another window, or the screen is off: nobody sees a frame
    if (G.saver && G.leaving && hidden) saverQuit("the screen was covered or off");          // nobody can see a fade on a screen that is off
    // A covered window is not drawn, except once every few seconds: if "covered" were ever reported wrongly, the clock would
    // still catch up within moments instead of standing still.
    const bool drew = !hidden || now - G.lastDraw > 5.0;
    if (drew) { a.render(G.gfx, w, h); G.lastDraw = now; } else ufoaudio::stop();
    if (drew) {   // how long drawing took before the frame was handed to the screen (smoothed): the pacing below starts the next frame that much early
        static double qf = 0; if (qf == 0) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); qf = (double)f.QuadPart; }
        const double cost = (double)(G.gfx.lastPreEndTicks() - q0.QuadPart) / qf;
        if (cost > 0 && cost < 0.1) G.drawCost += (cost - G.drawCost) * 0.2;
    }
    if (G.saver && G.leaving && (a.leaveDone || a.wantRecreate)) saverQuit(a.leaveDone ? "the fade finished" : "the graphics were lost");       // the last (black) frame was shown: the screen goes back to the desktop at once
    if (a.wantRecreate) ensureTarget(now, w, h);                   // the frame just drawn said the target was lost: rebuild it at once
    updateCaption(false);
    saveIfDue(now);

    // schedule the next frame
    bool fast = (now - a.faceChangedAt < 0.7) || (now - a.lastMouseMove < 0.8) || (now < a.toastUntil) || !paletteSettled() || a.dockBusy() || a.dockPillFading() || a.focusOrbBusy() || a.glassBusy() || a.eventsBusy() || a.holBusy() || a.panelBusy() || a.animatingOther() || a.packBusy() ||
                (a.st.face == FACE_TIMER && (a.timer.swRunning || a.timer.finished || a.timer.ringing || a.timer.entry.size() > 0)) ||
                (a.st.face == FACE_FOCUS && (a.focus.running || a.focus.finished || a.focus.entry.size() > 0));
    // Rain, snow and wind are drawn on the screen's own beat: one frame per refresh (two when the screen runs faster than 90 Hz). Drawing waits for the
    // refresh when it presents, so the next frame is started just early enough to finish before the next one (its drawing time plus a margin). That
    // gives frames that arrive evenly, which is what looks smooth. A timer that rounds to 15 ms, or 24 frames on a 60 Hz screen, does not.
    const bool spaceNow = spaceBusyNow(a, now);                  // Midnight: a meteor, the comet or the saucer is on screen
    const bool focusSmooth = a.focusFxBusy() && a.st.quietSmooth &&
        (a.st.focusFx == 2 || a.st.focusFx == 3 || a.st.focusFx == 6 || a.st.focusFx == 7 || a.st.focusFx == 8 ||
         a.st.focusFx == 9 || a.st.focusFx == 10 || a.st.focusFx == 11 || a.st.focusFx == 12 ||
         (a.st.focusFx == 1 && a.qfxFastActive()));
    const bool smooth = (a.quietFxBusy() && a.st.quietSmooth && a.qfxFastActive()) || focusSmooth || spaceNow || a.revealBusy() || a.dockBusy() || a.focusOrbBusy() || a.glassBusy();     // fluid Liquid Glass UI morphs (dock, focus orb, buttons/cards) & saver reveal run on screen refresh too
    G.smooth = smooth;
    double next;
    a.frameTarget = 0;
    if (smooth) {
        const double tEnd = unixNow();
        if (tEnd - G.refreshAt > 5.0) { G.refreshHz = displayHz(); G.refreshAt = tEnd; }
        const double Tf = std::max(1, (int)std::ceil(G.refreshHz / 90.0 - 1e-6)) / G.refreshHz;
        a.frameTarget = Tf;
        if (G.lastEnd > 0 && tEnd - G.lastEnd < 0.8 * Tf) { if (++G.shortRun > 12) G.noVsync = true; } else G.shortRun = 0;
        G.lastEnd = tEnd;
        if (G.noVsync) next = std::max(tEnd, G.paceStart + Tf);                         // the screen is not setting the pace: keep time ourselves
        else next = tEnd + std::max(0.0, 0.65 * Tf - G.drawCost);
    }
    else if (fast) next = now + 1.0 / 30.0;
    else if (a.quietFxBusy() || a.packFps() > 0) {                  // an all-day display: slow clouds at 15 frames a second, wind and rain at 24; a theme scene at the rate it asks for
        double fps = 1.0;
        if (a.quietFxBusy()) fps = std::max(fps, (double)a.quietFps());
        fps = std::max(fps, (double)a.packFps());
        next = now + 1.0 / fps;
    }
    else {
        next = std::floor(now) + 1.0 + 0.012;
        if (a.showSettings && a.textTarget) next = std::min(next, now + 0.45);       // blink the focused text caret cleanly without burning 30 fps
        if (a.timer.running) { double r = a.timer.endAt - now; double f = r - std::floor(r); next = std::min(next, now + (f < 0.004 ? 1.0 : f) + 0.004); }
        if (a.focus.running) { double r = a.focus.endAt - now; double f = r - std::floor(r); next = std::min(next, now + (f < 0.004 ? 1.0 : f) + 0.004); }
        if (a.timer.alarmOn) next = std::min(next, std::max(now + 0.05, a.timer.alarmAt - 0.0));
    }
    {   // Midnight: wake up for the next meteor, comet or saucer, so a short one is not missed between slow frames
        const double ns = spaceNextStart(a, now);
        if (ns < 1e17) next = std::min(next, std::max(now + 0.005, ns - 0.06));
    }
    if (hidden) {                                                  // look again only now and then, but never miss a timer or an alarm
        G.smooth = false;                                          // (so invalidate() works again the moment the window is uncovered)
        next = now + 0.5;
        if (a.timer.running) next = std::min(next, std::max(now + 0.02, a.timer.endAt));
        if (a.focus.running) next = std::min(next, std::max(now + 0.02, a.focus.endAt));
        if (a.timer.alarmOn) next = std::min(next, std::max(now + 0.02, a.timer.alarmAt));
    }
    G.nextFrame = next;
}

// runs the control under the pointer; returns false when nothing was hit
bool handleClick(float x, float y, bool holding = true) {
    App& a = G.app;
    for (auto it = a.hits.rbegin(); it != a.hits.rend(); ++it) {
        if (!(x >= it->x && x <= it->x + it->w && y >= it->y && y <= it->y + it->h)) continue;
        const uint64_t gkey = a.glassKeyForHit(it->id);
        if (gkey != 0) {
            const bool isCard = (it->id == HIT_FCARD_CLOCK || it->id == HIT_FCARD_WEATHER || it->id == HIT_SKY_AQI);
            a.glass.onPress(gkey, x, y, it->x, it->y, it->w, it->h, isCard ? 1.40f : 1.0f, holding);
        }
        if (it->id >= HIT_ACTION0) {
            size_t k = (size_t)(it->id - HIT_ACTION0);
            if (k < a.actions.size()) { std::function<void()> f = a.actions[k]; if (f) f(); }
        } else perform(it->id);
        return true;
    }
    return false;
}

void syncMouseFromCursor(HWND hwnd, bool requireVisible = false) {
    App& a = G.app;
    POINT ptScreen{}; RECT rc{};
    if (hwnd && GetCursorPos(&ptScreen)) {
        if (requireVisible && WindowFromPoint(ptScreen) != hwnd) { a.mouseX = a.mouseY = -1.f; return; }
        POINT pt = ptScreen;
        if (ScreenToClient(hwnd, &pt) && GetClientRect(hwnd, &rc) &&
            pt.x >= 0 && pt.x < rc.right && pt.y >= 0 && pt.y < rc.bottom) {
            a.mouseX = (float)pt.x; a.mouseY = (float)pt.y;
            return;
        }
    }
    a.mouseX = a.mouseY = -1.f;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    App& a = G.app;
    if (G.saver) {                                                  // screen saver: any use of the keyboard or mouse gives the screen back
        if (msg == WM_SETCURSOR) { SetCursor(nullptr); return TRUE; }
        if (msg == WM_SYSCOMMAND && (wp & 0xFFF0) == SC_SCREENSAVE) return 0;
        // any use of the keyboard or mouse: the picture fades out the way it came in (reveal.h, 0.6 seconds), and then the window goes. The black windows on
        // the other monitors ask for the same thing with saver::kLeaveMessage.
        if (saver::isInput(msg, wp, lp) || msg == saver::kLeaveMessage) {
            if (!G.leaving) {
                G.leaving = true; G.leaveAt = unixNow();
#ifdef MERIDIAN_DEV
                if (const char* t = std::getenv("MERIDIAN_TEST_STALL_MS")) Sleep((DWORD)std::max(0, std::atoi(t)));      // tests: a main thread that is stuck (the watchdog in saver.cpp must still end the process)
                if (std::getenv("MERIDIAN_TEST_NOFADE")) saverQuit("test: no fade");
#endif
                if (!saverCanFade(G.leaveAt)) saverQuit("no picture was being drawn, so no fade");       // nothing to fade: do not wait for a frame that may never come
                a.beginLeave(); G.nextFrame = 0;
            }
            return 0;
        }
        if (G.leaving && ((msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || (msg >= WM_KEYFIRST && msg <= WM_KEYLAST))) return 0;      // fading out: a click or key now must not press anything on the clock
        if (msg == WM_TIMER) {                                      // the quarter-second tick: has the saver been on long enough for the PC to go to sleep?
            if (saver::sleepTick(wp)) saverQuit("time to sleep");
            else if (G.leaving && unixNow() - G.leaveAt > 1.0) saverQuit("the fade took too long");      // the fade should be over by now (the picture is covered or stuck): never keep the screen waiting
            return 0;
        }
    }
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(hwnd, &ps); EndPaint(hwnd, &ps); renderFrame(); return 0; }
        case WM_SIZE:
            if (wp != SIZE_MINIMIZED && G.gfx.width() > 0) {
                G.gfx.resize(LOWORD(lp), HIWORD(lp));
                if (!a.st.fullscreen && wp != SIZE_MAXIMIZED) { RECT r; GetWindowRect(hwnd, &r); a.st.winW = r.right - r.left; a.st.winH = r.bottom - r.top; }
                syncMouseFromCursor(hwnd);
                renderFrame(); markDirty();
            }
            return 0;
        case WM_MOVE: if (!a.st.fullscreen && !IsZoomed(hwnd) && !IsIconic(hwnd)) { RECT r; GetWindowRect(hwnd, &r); a.st.winX = r.left; a.st.winY = r.top; markDirty(); } return 0;
        case WM_GETMINMAXINFO: { MINMAXINFO* mm = (MINMAXINFO*)lp; mm->ptMinTrackSize.x = 560; mm->ptMinTrackSize.y = 360; return 0; }
        case WM_KEYDOWN: case WM_SYSKEYDOWN:
            if (msg == WM_SYSKEYDOWN && wp != VK_RETURN) break;       // leave Alt+F4, Alt+Space etc. to Windows
            onKey(wp, (GetKeyState(VK_SHIFT) & 0x8000) != 0, (GetKeyState(VK_CONTROL) & 0x8000) != 0, (GetKeyState(VK_MENU) & 0x8000) != 0, (lp & 0x40000000) != 0);
            a.lastMouseMove = unixNow(); invalidate(); return 0;
        case WM_MOUSEMOVE: {
            const float mx = (float)(short)LOWORD(lp), my = (float)(short)HIWORD(lp);
            if (mx == a.mouseX && my == a.mouseY) return 0;
            a.mouseX = mx; a.mouseY = my; a.lastMouseMove = unixNow();
            TRACKMOUSEEVENT tme{sizeof tme, TME_LEAVE, hwnd, 0}; TrackMouseEvent(&tme);
            invalidateSoon(); return 0;
        }
        case WM_MOUSELEAVE: a.mouseX = a.mouseY = -1; a.glass.cancelHold(); invalidate(); return 0;
        case WM_CAPTURECHANGED: a.glass.cancelHold(); invalidate(); return 0;
        case WM_LBUTTONUP: a.glass.onRelease(); invalidate(); return 0;
        case WM_CHAR: {
            static wchar_t hi = 0;
            wchar_t ch = (wchar_t)wp;
            if (ch >= 0xD800 && ch < 0xDC00) { hi = ch; return 0; }
            if (ch >= 0xDC00 && ch < 0xE000 && hi) { std::wstring pr; pr.push_back(hi); pr.push_back(ch); hi = 0; textType(pr); invalidate(); return 0; }
            hi = 0;
            if (a.textTarget && ch >= 32) { textType(std::wstring(1, ch)); invalidate(); }
            return 0;
        }
        case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: {
            float x = (float)(short)LOWORD(lp), y = (float)(short)HIWORD(lp);
            a.lastMouseMove = unixNow();
            a.settingsFocus = -1;
            if (a.showHelp) { a.showHelp = false; invalidate(); return 0; }
            a.mouseX = x; a.mouseY = y;                                // a text box places its caret where you clicked
            a.textClicked = false; a.dblClick = msg == WM_LBUTTONDBLCLK;
            const bool handled = handleClick(x, y);
            if (!a.textClicked) textBlur();                            // a click anywhere else leaves the text box
            if (handled) { invalidate(); return 0; }
            if (msg == WM_LBUTTONDBLCLK) { setFullscreen(!a.st.fullscreen); invalidate(); }
            return 0;
        }
        case WM_CONTEXTMENU: {                                     // right-click, the menu key, Shift+F10, or a long press on a touch screen
            POINT pt{(LONG)(short)LOWORD(lp), (LONG)(short)HIWORD(lp)};
            if (lp != -1 && DefWindowProcW(hwnd, WM_NCHITTEST, 0, lp) != HTCLIENT) return DefWindowProcW(hwnd, msg, wp, lp);   // title bar: Windows' own menu
            if (lp == -1) { RECT r; GetWindowRect(hwnd, &r); pt.x = (r.left + r.right) / 2; pt.y = (r.top + r.bottom) / 2; }
            showContextMenu(hwnd, pt); syncMouseFromCursor(hwnd); a.lastMouseMove = unixNow(); invalidate(); return 0;
        }
        case WM_MOUSEWHEEL: {
            int d = GET_WHEEL_DELTA_WPARAM(wp);
            if (a.showSettings) { a.settingsWheel(d); a.lastMouseMove = unixNow(); invalidate(); return 0; }
            // One step per wheel notch (120). A touchpad or a free-spinning wheel sends many small movements: they add up until they make a notch, so a
            // flick no longer drops the brightness to the bottom or moves a timer by minutes. (Movement the other way starts counting again.)
            if ((d > 0) != (G.wheelAcc > 0) && G.wheelAcc != 0) G.wheelAcc = 0;
            G.wheelAcc += d;
            int steps = G.wheelAcc / WHEEL_DELTA;
            G.wheelAcc -= steps * WHEEL_DELTA;
            steps = std::max(-5, std::min(5, steps));
            if (steps != 0) {
                if (a.st.face == FACE_TIMER && a.timer.mode != 1) { a.timerAdjust(60.0 * steps); markDirty(); }
                else if (a.st.face == FACE_FOCUS) { a.focusAdjust(60.0 * steps); markDirty(); }
                else { a.st.brightness = std::max(0.15f, std::min(1.0f, a.st.brightness + 0.05f * (float)steps)); markDirty(); }
            }
            a.lastMouseMove = unixNow(); invalidate(); return 0;
        }
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT && a.st.fullscreen && unixNow() - a.lastMouseMove > 3.5) { SetCursor(nullptr); return TRUE; }
            break;
        case WM_ACTIVATE:
            if (LOWORD(wp) != WA_INACTIVE) { syncMouseFromCursor(hwnd); a.lastMouseMove = unixNow(); invalidate(); }
            else {
                // Guarantee normal windows yield z-order cleanly to other apps when clicked away
                if (!G.saver && !a.st.topmost && (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0) {
                    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
                }
                syncMouseFromCursor(hwnd, true); a.glass.cancelHold(); invalidate();
            }
            return 0;
        case WM_DISPLAYCHANGE: case WM_DPICHANGED:
            if (msg == WM_DPICHANGED) { RECT* r = (RECT*)lp; SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE); }
            G.refreshAt = -1e9; G.noVsync = false; G.shortRun = 0;       // look at the screen's refresh rate again
            invalidate(); return 0;
        case WM_POWERBROADCAST:
            if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND) {
                G.clock.reset();                                  // the clocks may disagree about the time spent asleep: not a clock step
                applyKeepAwake();
                if (wp == PBT_APMRESUMEAUTOMATIC) W.request(a.st.geo.lat, a.st.geo.lon);
                invalidate();
            } else if (wp == PBT_APMPOWERSTATUSCHANGE) applyKeepAwake();     // the charger was plugged in or pulled out
            return TRUE;
        case WM_CLOSE: DestroyWindow(hwnd); return 0;
        case WM_DESTROY: G.running = false; PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------- screenshot mode (used for testing)
// Only in test builds (build.sh / build.bat with MERIDIAN_DEV=1); the shipped Meridian.exe has none of this.
#ifdef MERIDIAN_DEV
struct ShotArgs {
    std::wstring out, weatherFile, airFile, stormFile, holidaysFile; std::string countries, homeCountry, homeRegion; int ukPref = -1; float aboutScroll = 0, reveal = -1, leave = -1; int w = 1920, h = 1080; int face = -1, theme = -1; std::string pack, packFace, themesDir; std::string time; int tz = 19800; bool tzSet = false;
    int mode = -1, wxprov = 0; bool offline = false; double duration = -1, elapsed = -1; bool running = false, h24 = false, useF = false, help = false, dock = false; double lat = 1e9, lon = 1e9; int bright = -1;
    bool timerFinished = false, icons = false, hint = false; float mx = -1, my = -1;
    bool settings = false; int locDemo = 0; int aboutSub = 0; int tab = 0, wsel = -1, pin = -2, rotsub = 0; std::string widgets, note, cdLabel, cdDate; bool hasWidgets = false;
    std::vector<std::pair<float, float>> clicks; std::wstring typeText;
    double advance = 0;                                                  // --advance S: let S seconds pass after the clicks (for a test shot of a fade)
    int panel = -1, transPrev = -1; float transK = -1; std::string panels; int style = -1; int calMonth = 0;
    float heading = -1; int spin = 0, quiet = -1, wgper = -1, wgpage = -1; double spinAt = 0;
    int focusfx = -1; bool alien = false, dayStars = false; float meteor = -1.f, dockT = -1.f;
    int bench = 0, benchTheme2 = -1, benchSwitch = 30; double benchFps = 24;      // --bench N: time N frames, switching theme at frame benchSwitch; writes a text report to --shot
};

int runShot(const ShotArgs& sa) {
    App& a = G.app;
    if (!G.gfx.init()) return 2;
    moonsprite::prewarm(); while (!moonsprite::ready()) Sleep(5);
    if (!G.gfx.createOffscreenTarget(sa.w, sa.h)) return 3;
    if (sa.icons) {      // icon contact sheet for visual checks
        using weather::Icon;
        const gfx::Palette& P = sa.theme == 1 ? gfx::PAL_DAY : sa.theme == 5 ? gfx::PAL_DAWN : gfx::PAL_NIGHT;
        G.gfx.begin(); G.gfx.vGradientRect(0, 0, (float)sa.w, (float)sa.h, P.bgTop, P.bgBottom);
        Icon ic[11] = {Icon::ClearDay, Icon::ClearNight, Icon::PartlyDay, Icon::PartlyNight, Icon::Cloudy, Icon::Fog, Icon::Drizzle, Icon::Rain, Icon::Showers, Icon::Thunder, Icon::Snow};
        for (int i = 0; i < 11; ++i) {
            float cx = 110.f + (i % 6) * 200.f, cy = 110.f + (i / 6) * 200.f;
            G.gfx.weatherIcon(ic[i], cx, cy, 130.f, P.ink, P.accent, P.plate);
            G.gfx.weatherIcon(ic[i], cx, cy + 82.f, 34.f, P.ink, P.accent, P.plate);
        }
        for (int k = 0; k < 6; ++k) { float sz[6] = {18.f, 24.f, 34.f, 48.f, 64.f, 96.f}; G.gfx.weatherIcon(Icon::Rain, 60.f + k * 150.f, 360.f, sz[k], P.ink, P.accent, P.plate); }
        G.gfx.end();
        return G.gfx.savePng(sa.out.c_str()) ? 0 : 4;
    }
    if (sa.face >= 0) a.st.face = sa.face;
    if (sa.theme >= 0) a.st.theme = sa.theme;
    a.st.h24 = sa.h24; a.st.useF = sa.useF; a.showHelp = sa.help;
    if (sa.lat < 1e8) { a.st.geo.lat = sa.lat; a.st.geo.lon = sa.lon; a.st.geo.name = "Custom"; }
    double now = unixNow();
    if (!sa.time.empty()) {
        int y, mo, d, hh = 0, mm = 0, ss = 0;
        if (sscanf(sa.time.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &hh, &mm, &ss) >= 4)
            now = astro::daysFromCivil(y, mo, d) * 86400.0 + hh * 3600.0 + mm * 60.0 + ss - sa.tz;
    }
    if (!sa.weatherFile.empty()) {
        std::string js, err; weather::Data d;
        d.utcOffset = sa.tz;
        if (readFile(sa.weatherFile, js) && weather::parseProvider(sa.wxprov, js, a.st.geo.lat, a.st.geo.lon, d, err)) {
            std::string air; if (!sa.airFile.empty() && readFile(sa.airFile, air)) weather::parseAir(air, d, err);
            d.fetched = now; a.wx = d;
        }
    }
    if (!sa.stormFile.empty()) {
        std::string js, err; std::vector<storms::Storm> v;
        if (readFile(sa.stormFile, js) && storms::parseAny(js, now, v, err)) { a.stormData.valid = true; a.stormData.list = v; a.stormData.source = "sample"; a.stormData.fetched = now; }
    }
    if (sa.offline) { a.wxOffline = true; a.wxStatus = "Offline · could not connect"; a.wxDetail = "tried 4 sources · retrying"; }
    a.timer.mode = sa.mode >= 0 ? sa.mode : 0;
    a.setNow(now, sa.tz, 1.0);
    {   // theme packs
        std::vector<std::filesystem::path> roots = themeRoots();
        if (!sa.themesDir.empty()) roots = {std::filesystem::path(util::widen(sa.themesDir))};
        themes::setRoots(roots); themes::rescan(&G.gfx);
        if (!sa.pack.empty()) a.st.themePack = sa.pack;
        if (!sa.packFace.empty()) { a.st.face = FACE_PACK; a.st.facePack = sa.packFace; }
        a.syncPacks();
    }
    a.stormsChanged();
    if (sa.duration > 0) {
        if (a.st.face == FACE_FOCUS) a.focusSetDuration(sa.duration);
        else a.timerSetDuration(sa.duration);
    }
    if (sa.running) {
        if (a.st.face == FACE_FOCUS) a.focusStartPause();
        else if (a.timer.mode == 0) a.timerStartPause();
        else if (a.timer.mode == 1) { a.timer.swElapsed = sa.elapsed > 0 ? sa.elapsed : 754.32; a.timer.swRunning = false; a.timer.laps = {61.2, 140.55, 254.1, 401.9}; }
        else a.timerStartPause();
    }
    if (sa.timerFinished) { if (a.st.face == FACE_FOCUS) a.focus.finished = true; else a.timer.finished = true; }
    if ((sa.mode == 0 || a.st.face == FACE_FOCUS) && sa.running) a.setNow(now + (sa.elapsed > 0 ? sa.elapsed : 0), sa.tz, 1.0);
    a.lastMouseMove = (sa.dock || sa.hint || sa.mx >= 0) ? now : -1e9;
    if (sa.mx >= 0) { a.mouseX = sa.mx; a.mouseY = sa.my; a.lastMouseMove = 1e12; }       // (the pointer counts as having just moved, so hover cards show)
    if (sa.dock) { a.mouseX = sa.w * 0.5f; a.mouseY = (float)sa.h - 24.f; a.dockT = a.dockTarget = 1.f; }
    if (sa.dockT >= 0.f) a.dockOverride = sa.dockT;
    if (sa.bright > 0) a.st.brightness = sa.bright / 100.f;
    if (sa.settings) { a.showSettings = true; a.settingsTab = sa.tab; a.aboutSub = sa.aboutSub; }
    // screenshots of Settings > Place in each state: 1 searching, 2 failed, 3 found by IP address, 4 not found yet, 5 found by Windows location
    if (sa.locDemo == 1) a.locBusy = true;
    if (sa.locDemo == 2) a.locErr = "Could not find your location: Windows is not letting desktop apps use your location, and the internet lookup failed too. Check the connection, and that Location is on in Windows Settings.";
    if (sa.locDemo == 3) { a.locSource = "IP address"; a.locAcc = -1; a.locAt = unixNow() - 120; }
    if (sa.locDemo == 4) a.located = false;
    if (sa.locDemo == 5) { a.locSource = "Windows location"; a.locAcc = 38; a.locAt = unixNow() - 90; }
    if (sa.wsel >= 0) a.widgetSel = sa.wsel;
    a.rotSub = sa.rotsub;
    if (sa.hasWidgets) { a.st.widgets.clear(); for (auto& t : splitCsv(sa.widgets)) { int id = atoi(t.c_str()); if (id >= 0 && id < WG_COUNT) a.st.widgets.push_back(id); } }
    if (sa.pin > -2) a.st.pinned = sa.pin;
    if (!sa.note.empty()) a.st.note = unescapeNl(sa.note, true);
    if (!sa.cdDate.empty()) { a.st.cdLabel = sa.cdLabel; a.st.cdDate = sa.cdDate; }
    if (sa.panels.size() >= 4) { a.st.elToday = sa.panels[0] == '1'; a.st.elEvents = sa.panels[1] == '1'; a.st.elHolidays = sa.panels[2] == '1'; a.st.elWorld = sa.panels[3] == '1'; }
    if (sa.style >= 0) a.st.panelStyle = sa.style;
    a.calOffset = sa.calMonth;
    if (!sa.holidaysFile.empty()) { std::string t; readFile(sa.holidaysFile, t); a.holStatus = holiday::setOverrides(t); a.holCount = holiday::overrideCount(); }
    if (!sa.countries.empty()) {      // --countries lk,uk,us (or "none") switches those on by hand; "auto" keeps the automatic choice
        a.st.holAuto = sa.countries == "auto"; a.st.holMask = 0;
        if (sa.countries.find("lk") != std::string::npos) a.st.holMask |= holiday::C_LK;
        if (sa.countries.find("uk") != std::string::npos) a.st.holMask |= holiday::C_UK;
        if (sa.countries.find("us") != std::string::npos) a.st.holMask |= holiday::C_US;
    }
    if (sa.ukPref >= 0) a.st.ukPref = sa.ukPref;
    if (sa.reveal >= 0) a.revealOverride = sa.reveal;
    if (sa.leave >= 0) a.leaveOverride = sa.leave;
    if (sa.aboutScroll > 0) { a.aboutScroll = sa.aboutScroll; a.aboutScrollMax = 1e9f; }      // --aboutscroll N: What's new scrolled down by N layout units
    if (!sa.homeCountry.empty()) { a.homeCountry = sa.homeCountry; a.homeRegion = sa.homeRegion; }
    syncHolidayCountries();
    if (sa.heading >= 0) { a.st.skyHeading = sa.heading; a.spinBase = sa.heading; a.spinT0 = now; }
    if (sa.spin > 0) { a.st.skySpin = true; a.st.skySpinSecs = sa.spin; a.spinT0 = now - sa.spinAt; }
    if (sa.quiet >= 0) a.st.quietPreview = sa.quiet;
    if (sa.focusfx >= 0) a.st.focusFx = std::max(0, std::min(13, sa.focusfx));
    if (sa.alien) { a.st.aliensEnabled = true; a.alienSeq = 1; a.alienAt = now - 4.2; }
    if (sa.dayStars) a.st.dayStars = true;
    if (sa.meteor > 0.f) a.meteorDemoU = sa.meteor;
    if (sa.wgper >= 0) a.st.wgPerPage = sa.wgper;
    a.update(G.gfx);
    for (int i = 0; i < 14; ++i) quietFxStep(a);                  // let the Quiet-face weather reach full strength in a single frame
    if (!sa.bench) a.spriteBudget = 99;                           // a still picture: draw every cached image at once
    a.palInit = false; a.update(G.gfx);                           // and let the light settle on that weather
    if (sa.panel >= 0) { a.skyRot.cur = sa.panel; a.skyRot.switchAt = now - 5.0; }
    if (sa.transK >= 0 && sa.panel >= 0) { a.skyRot.prev = sa.transPrev; a.skyRot.switchAt = now - sa.transK * (a.st.panelStyle == 2 ? 0.9 : 1.05); }
    if (sa.wgpage >= 0) { a.wgtRot.cur = sa.wgpage; a.wgtRot.switchAt = now - 5.0; if (sa.transK >= 0 && sa.panel < 0) { a.wgtRot.prev = sa.transPrev; a.wgtRot.switchAt = now - sa.transK * (a.st.panelStyle == 2 ? 0.9 : 1.05); } }
    if (sa.bench > 0) {                                           // timing run: N frames at a steady rate, optionally switching theme part-way
        std::vector<double> ms; ms.reserve(sa.bench);
        LARGE_INTEGER fq, t0, t1; QueryPerformanceFrequency(&fq);
        double t = now; const double dtf = 1.0 / sa.benchFps;
        const int builds0 = gSpriteBuilds;
        std::vector<int> bl; bl.reserve(sa.bench);
        for (int i = 0; i < sa.bench; ++i) {
            if (i == sa.benchSwitch && sa.benchTheme2 >= 0) a.st.theme = sa.benchTheme2;
            t += dtf; a.setNow(t, sa.tz, dtf);
            const int b0 = gSpriteBuilds;
            QueryPerformanceCounter(&t0);
            a.update(G.gfx); a.render(G.gfx, sa.w, sa.h);
            QueryPerformanceCounter(&t1);
            ms.push_back((double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)fq.QuadPart);
            bl.push_back(gSpriteBuilds - b0);
        }
        auto stat = [&](int from, int to, const char* name, std::string& o) {
            from = std::max(0, from); to = std::min((int)ms.size(), to); if (to <= from) return;
            std::vector<double> v(ms.begin() + from, ms.begin() + to); double sum = 0, mx = 0; int nb = 0;
            for (double x : v) { sum += x; mx = std::max(mx, x); }
            for (int i = from; i < to; ++i) nb += bl[i];
            std::sort(v.begin(), v.end());
            char buf[200]; snprintf(buf, sizeof buf, "%-12s frames %3d  avg %7.2f ms  p95 %7.2f  max %7.2f  sprite builds %d\n", name, to - from, sum / v.size(), v[(size_t)(v.size() * 0.95)], mx, nb); o += buf;
        };
        std::string rep;
        stat(2, sa.benchSwitch, "before", rep);
        stat(sa.benchSwitch, sa.benchSwitch + 60, "transition", rep);
        stat(sa.benchSwitch + 60, sa.bench, "after", rep);
        char tot[100]; snprintf(tot, sizeof tot, "total sprite builds %d\n", gSpriteBuilds - builds0); rep += tot;
        FILE* f = _wfopen(sa.out.c_str(), L"wb"); if (f) { fwrite(rep.data(), 1, rep.size(), f); fclose(f); }
        return 0;
    }
    int lastBuilds = -1;
    a.render(G.gfx, sa.w, sa.h);
    for (int i = 0; i < 30 && gSpriteBuilds != lastBuilds; ++i) { lastBuilds = gSpriteBuilds; a.render(G.gfx, sa.w, sa.h); }       // cached drawings are made a few per frame: draw until they are all there
    if (!sa.clicks.empty()) a.dt = 0.016;
    for (auto& ck : sa.clicks) { a.mouseX = ck.first; a.mouseY = ck.second; a.textClicked = false; handleClick(ck.first, ck.second, false); if (!a.textClicked) textBlur(); a.update(G.gfx); a.render(G.gfx, sa.w, sa.h); }
    if (!sa.typeText.empty()) textType(sa.typeText);
    if (sa.advance > 0) {
        const int steps = std::max(1, std::min(12, (int)std::lround(sa.advance / 0.016)));
        const double stepDt = sa.advance / (double)steps;
        for (int i = 0; i < steps; ++i) { a.setNow(a.now + stepDt, a.tzOff, stepDt); a.update(G.gfx); a.render(G.gfx, sa.w, sa.h); }
    }
    if (sa.mx >= 0 || !sa.clicks.empty() || !sa.typeText.empty()) { a.render(G.gfx, sa.w, sa.h); }      // second pass: hover targets exist after the first frame
    return G.gfx.savePng(sa.out.c_str()) ? 0 : 4;
}

bool parseArgs(ShotArgs& sa, bool& isShot) {
    int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return false;
    auto num = [&](int i) { return i < argc ? _wtof(argv[i]) : 0.0; };
    for (int i = 1; i < argc; ++i) {
        std::wstring k = argv[i];
        if (k == L"--shot" && i + 1 < argc) { sa.out = argv[++i]; isShot = true; }
        else if (k == L"--size" && i + 1 < argc) { swscanf(argv[++i], L"%dx%d", &sa.w, &sa.h); }
        else if (k == L"--face" && i + 1 < argc) sa.face = _wtoi(argv[++i]);
        else if (k == L"--theme" && i + 1 < argc) sa.theme = _wtoi(argv[++i]);
        else if (k == L"--time" && i + 1 < argc) sa.time = util::narrow(argv[++i]);
        else if (k == L"--pack" && i + 1 < argc) sa.pack = util::narrow(argv[++i]);                 // theme pack id to use as the theme
        else if (k == L"--packface" && i + 1 < argc) sa.packFace = util::narrow(argv[++i]);         // "pack/face" to show
        else if (k == L"--themes" && i + 1 < argc) sa.themesDir = util::narrow(argv[++i]);          // another themes folder
        else if (k == L"--tz" && i + 1 < argc) { int sg = 1, hh = 0, mm = 0; std::wstring v = argv[++i]; if (!v.empty() && (v[0] == L'-' || v[0] == L'+')) { if (v[0] == L'-') sg = -1; v = v.substr(1); } swscanf(v.c_str(), L"%d:%d", &hh, &mm); sa.tz = sg * (hh * 3600 + mm * 60); }
        else if (k == L"--weather" && i + 1 < argc) sa.weatherFile = argv[++i];
        else if (k == L"--air" && i + 1 < argc) sa.airFile = argv[++i];
        else if (k == L"--storms" && i + 1 < argc) sa.stormFile = argv[++i];
        else if (k == L"--mode" && i + 1 < argc) sa.mode = _wtoi(argv[++i]);
        else if (k == L"--duration" && i + 1 < argc) sa.duration = num(++i);
        else if (k == L"--elapsed" && i + 1 < argc) sa.elapsed = num(++i);
        else if (k == L"--running") sa.running = true;
        else if (k == L"--finished") sa.timerFinished = true;
        else if (k == L"--h24") sa.h24 = true;
        else if (k == L"--fahrenheit") sa.useF = true;
        else if (k == L"--help-overlay") sa.help = true;
        else if (k == L"--dock") sa.dock = true;
        else if (k == L"--dockt" && i + 1 < argc) sa.dockT = (float)num(++i);
        else if (k == L"--hint") sa.hint = true;
        else if (k == L"--mouse" && i + 2 < argc) { sa.mx = (float)num(++i); sa.my = (float)num(++i); }
        else if (k == L"--wxprov" && i + 1 < argc) sa.wxprov = (int)num(++i);
        else if (k == L"--offline") sa.offline = true;
        else if (k == L"--icons") sa.icons = true;
        else if (k == L"--lat" && i + 1 < argc) sa.lat = num(++i);
        else if (k == L"--lon" && i + 1 < argc) sa.lon = num(++i);
        else if (k == L"--brightness" && i + 1 < argc) sa.bright = (int)num(++i);
        else if (k == L"--settings") sa.settings = true;
        else if (k == L"--tab" && i + 1 < argc) sa.tab = (int)num(++i);
        else if (k == L"--locdemo" && i + 1 < argc) sa.locDemo = (int)num(++i);
        else if (k == L"--about" && i + 1 < argc) sa.aboutSub = (int)num(++i);                                  // About tab: 1 = features and what's new
        else if (k == L"--wsel" && i + 1 < argc) sa.wsel = (int)num(++i);
        else if (k == L"--rotsub" && i + 1 < argc) sa.rotsub = (int)num(++i);
        else if (k == L"--pin" && i + 1 < argc) sa.pin = (int)num(++i);
        else if (k == L"--widgets" && i + 1 < argc) { sa.widgets = util::narrow(argv[++i]); sa.hasWidgets = true; }
        else if (k == L"--note" && i + 1 < argc) sa.note = util::narrow(argv[++i]);
        else if (k == L"--countdown" && i + 2 < argc) { sa.cdLabel = util::narrow(argv[i + 1]); sa.cdDate = util::narrow(argv[i + 2]); i += 2; }
        else if (k == L"--click" && i + 2 < argc) { float cx = (float)num(i + 1), cy = (float)num(i + 2); i += 2; sa.clicks.push_back({cx, cy}); }
        else if (k == L"--advance" && i + 1 < argc) sa.advance = num(++i);
        else if (k == L"--type" && i + 1 < argc) sa.typeText = argv[++i];
        else if (k == L"--panel" && i + 1 < argc) sa.panel = (int)num(++i);
        else if (k == L"--trans" && i + 2 < argc) { sa.transK = (float)num(i + 1); sa.transPrev = (int)num(i + 2); i += 2; }
        else if (k == L"--panels" && i + 1 < argc) sa.panels = util::narrow(argv[++i]);
        else if (k == L"--style" && i + 1 < argc) sa.style = (int)num(++i);
        else if (k == L"--calmonth" && i + 1 < argc) sa.calMonth = (int)num(++i);
        else if (k == L"--countries" && i + 1 < argc) sa.countries = util::narrow(argv[++i]);          // holiday countries: lk,uk,us / none / auto
        else if (k == L"--aboutscroll" && i + 1 < argc) sa.aboutScroll = (float)num(++i);
        else if (k == L"--reveal" && i + 1 < argc) sa.reveal = (float)num(++i);      // --reveal P: the screen saver's start, frozen at P (0 to 1)
        else if (k == L"--leave" && i + 1 < argc) sa.leave = (float)num(++i);        // --leave Q: the screen saver's fade-out, frozen at Q (0 to 1)
        else if (k == L"--ukpref" && i + 1 < argc) sa.ukPref = (int)num(++i);                     // UK nation: 0 auto, 1 E&W, 2 Scotland, 3 NI
        else if (k == L"--homecountry" && i + 1 < argc) sa.homeCountry = util::narrow(argv[++i]);       // pretend the home place is in this country (GB, US, LK, DE ...)
        else if (k == L"--homeregion" && i + 1 < argc) sa.homeRegion = util::narrow(argv[++i]);
        else if (k == L"--holidays" && i + 1 < argc) sa.holidaysFile = argv[++i];              // a holidays.txt to read, as the real app does at start-up
        else if (k == L"--heading" && i + 1 < argc) sa.heading = (float)num(++i);
        else if (k == L"--spin" && i + 2 < argc) { sa.spin = (int)num(i + 1); sa.spinAt = num(i + 2); i += 2; }       // seconds per turn, and how many seconds into the turn
        else if (k == L"--quiet" && i + 1 < argc) sa.quiet = (int)num(++i);
        else if ((k == L"--focusfx" || k == L"--timerfx") && i + 1 < argc) sa.focusfx = (int)num(++i);
        else if (k == L"--alien") sa.alien = true;
        else if (k == L"--daystars") sa.dayStars = true;
        else if (k == L"--meteor" && i + 1 < argc) sa.meteor = (float)num(++i);
        else if (k == L"--wgper" && i + 1 < argc) sa.wgper = (int)num(++i);
        else if (k == L"--wgpage" && i + 1 < argc) sa.wgpage = (int)num(++i);
        else if (k == L"--bench" && i + 1 < argc) sa.bench = (int)num(++i);
        else if (k == L"--theme2" && i + 1 < argc) sa.benchTheme2 = (int)num(++i);
        else if (k == L"--switch" && i + 1 < argc) sa.benchSwitch = (int)num(++i);
        else if (k == L"--fps" && i + 1 < argc) sa.benchFps = num(++i);
    }
    LocalFree(argv);
    return true;
}
#endif // MERIDIAN_DEV

} // namespace

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nShow) {
    // Windows runs a screen saver as "Meridian.scr /s" (see saver.h). With no switch none of the saver code is used.
    HWND saverParent = nullptr;
    const saver::Mode runMode = saver::modeFromCommandLine(&saverParent);
    if (runMode == saver::Mode::Preview || runMode == saver::Mode::Nothing) return 0;      // the preview box is left blank on purpose
    if (runMode == saver::Mode::Configure) { saver::configure(saverParent); return 0; }
    G.saver = runMode == saver::Mode::Run;
    G.app.isSaver = G.saver;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#ifdef MERIDIAN_DEV
    ShotArgs sa; bool isShot = false;
    parseArgs(sa, isShot);
    if (isShot) { int rc = runShot(sa); moonsprite::shutdown(); G.app.releaseDeviceResources(G.gfx); G.gfx.shutdown(); CoUninitialize(); return rc; }
#endif
    ufoaudio::setEnabled(!G.saver);
    weather::userAgentBase() = std::string("Meridian/") + MERIDIAN_VERSION;
    const int restarted = robust::restartCountFromCommandLine();   // started again by the crash handler? (see robust.cpp)
    if (G.saver) { if (!saver::oneAtATime()) { CoUninitialize(); return 0; } }              // a screen saver runs beside a normal Meridian window
    else if (!robust::becomeSingleInstance(L"MeridianWnd", restarted > 0)) { CoUninitialize(); return 0; }

    G.settingsPath = dataDir(false) + L"\\settings.ini";
    std::wstring ld = dataDir(true);
    G.forecastCache = ld + L"\\forecast.json"; G.airCache = ld + L"\\air.json"; G.logPath = ld + L"\\weather.log";
    G.holPath = dataDir(false) + L"\\holidays.txt";
    static const std::string versionTag = std::string(MERIDIAN_VERSION) + (G.saver ? " screen saver" : "");
    robust::installCrashHandler(ld + L"\\crash.log", versionTag.c_str(), G.saver ? 0 : restarted, ld + L"\\crash", !G.saver);   // screen saver logs crashes but never relaunches as a clock window
#ifdef MERIDIAN_DEV
    {   // test builds only: MERIDIAN_TEST_CRASH=1 makes the first start crash on purpose, to check the crash note, the dump and the restart
        wchar_t b[8];
        if (restarted == 0 && !G.saver && GetEnvironmentVariableW(L"MERIDIAN_TEST_CRASH", b, 8)) { Sleep(300); volatile int* nowhere = nullptr; *nowhere = 1; }
    }
#endif
    bool firstRun = GetFileAttributesW(G.settingsPath.c_str()) == INVALID_FILE_ATTRIBUTES;
    {   // the country of the Windows region settings ("LK", "GB", "US"): the last resort for which holidays to show
        wchar_t loc[LOCALE_NAME_MAX_LENGTH] = {0}, cc[16] = {0};
        if (GetUserDefaultLocaleName(loc, LOCALE_NAME_MAX_LENGTH) > 0 && GetLocaleInfoEx(loc, LOCALE_SISO3166CTRYNAME, cc, 16) > 0) G.app.sysCountry = util::narrow(cc);
    }
    loadSettings();
    loadHolidays(false);
    if (G.saver) { G.app.st.fullscreen = true; G.app.st.topmost = true; G.app.st.keepAwake = false; }      // (in memory only: saveSettings does nothing in saver mode)
    G.app.spinBase = G.app.st.skyHeading; G.app.spinT0 = unixNow();         // free rotate starts from the saved heading
    G.app.home = G.app.home.name.empty() ? astro::Geo() : G.app.home;
    applyGeoFromPlace();                                                    // "your location", or the saved place that was on show when it was closed
    G.app.timer.pausedRemaining = G.app.timer.durationSec;
    G.app.tzAt = localOffset;
    G.app.sysTzAt = localOffset;                                            // wall-clock DST rules for alarms & display
    if (!G.saver && G.app.timer.alarmOn) { G.app.setNow(unixNow(), localOffset(unixNow()), 1.0); G.app.timer.alarmOn = false; G.app.timer.mode = 2; G.app.timerStartPause(); G.app.timer.mode = 0; }
    if (firstRun) { G.app.located = false; saveSettings(); }      // a new install looks for its location once, at start

    moonsprite::prewarm();
    if (!G.gfx.init()) { MessageBoxW(nullptr, L"Meridian could not start Direct2D / DirectWrite.", L"Meridian", MB_ICONERROR); return 1; }

    WNDCLASSEXW wc{sizeof wc};
    wc.style = CS_DBLCLKS; wc.lpfnWndProc = WndProc; wc.hInstance = hInst; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    wc.hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wc.hbrBackground = nullptr; wc.lpszClassName = L"MeridianWnd";
    RegisterClassExW(&wc);

    Settings& st = G.app.st;
    int x = st.winX, y = st.winY, w = std::max(640, st.winW), h = std::max(400, st.winH);
    // Is the saved position still on a screen? Any connected monitor counts, not just the main one (a clock that lives on the
    // second monitor used to come back on the first).
    bool place = x != -1 && y != -1 && robust::rectReachable(x, y, w, h);
    if (G.saver) {                                                  // a plain full-screen window on the main monitor, no taskbar button
        MONITORINFO mi{sizeof mi};
        GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
        G.hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"MeridianWnd", L"Meridian", WS_POPUP | WS_CLIPCHILDREN, mi.rcMonitor.left, mi.rcMonitor.top,
                                 mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top, nullptr, nullptr, hInst, nullptr);
    } else
    G.hwnd = CreateWindowExW(0, L"MeridianWnd", L"Meridian", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, place ? x : CW_USEDEFAULT, place ? y : CW_USEDEFAULT, w, h, nullptr, nullptr, hInst, nullptr);
    if (!G.hwnd) return 1;
    RECT rc; GetClientRect(G.hwnd, &rc);
    if (!G.gfx.createWindowTarget(G.hwnd, rc.right, rc.bottom)) { MessageBoxW(nullptr, L"Meridian could not create its render target.", L"Meridian", MB_ICONERROR); return 1; }

    G.app.setNow(unixNow(), localOffset(unixNow()), 1.0);
    themes::setStateDir(std::filesystem::path(ld));                                     // for the "skip a pack that crashed the last start" note
    themes::setRoots(themeRoots()); themes::rescan(&G.gfx); G.app.syncPacks();          // theme packs
    loadWeatherCache();
    G.app.wxStatus = G.app.wx.valid ? "" : "Connecting…";
    G.app.command = command;
    W.lat = st.geo.lat; W.lon = st.geo.lon; W.tz = localOffset(unixNow()); applyWeatherConfig();
    applyStormConfig();
    if (G.saver) G.deferWorkers = true; else { W.start(); SW.start(); }       // (the screen saver shows the saved weather while it comes up, and starts the downloads afterwards)
    weatherLog(fmt("Start: showing %s (%.4f, %.4f), %s", st.geo.name.c_str(), st.geo.lat, st.geo.lon, G.app.placeIdx >= 0 ? "a saved place" : "your location"));
    if (!G.app.located && !G.saver) startLocate(true);
    applyKeepAwake();
    updateCaption(true);
    if (G.saver) G.app.revealArmed = true;                          // Windows started the screen saver: come up out of the dark instead of popping on (reveal.h)
    ShowWindow(G.hwnd, nShow);
    applyTopmost();
    if (st.fullscreen) { st.fullscreen = false; setFullscreen(true); }
    UpdateWindow(G.hwnd);
    if (G.saver) { SetForegroundWindow(G.hwnd); saver::begin(hInst, G.hwnd); }

    G.hrTimer = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
    MSG msg;
    while (G.running) {
        double now = unixNow();
        if (G.nextFrame > now + 2.5) G.nextFrame = now;            // the clock was set back: do not wait for a time that has already gone
        if (G.nextFrame > now) {
            const double delay = std::min(1.0, G.nextFrame - now);
            if (G.hrTimer) {                                                       // accurate to well under a millisecond
                LARGE_INTEGER due; due.QuadPart = -(LONGLONG)(delay * 1e7);
                SetWaitableTimer(G.hrTimer, &due, 0, nullptr, nullptr, FALSE);
                MsgWaitForMultipleObjects(1, &G.hrTimer, FALSE, (DWORD)(delay * 1000.0) + 50, QS_ALLINPUT);
            } else MsgWaitForMultipleObjects(0, nullptr, FALSE, (DWORD)std::max(1.0, delay * 1000.0), QS_ALLINPUT);
        }
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { G.running = false; break; }
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        if (!G.running) break;
        if (unixNow() >= G.nextFrame) {
            G.nextFrame = unixNow() + 0.25;                         // renderFrame sets the real time; this only guards against a path that forgets to
            renderFrame();
        }
    }
    if (G.saver) { saver::finish(); TerminateProcess(GetCurrentProcess(), 0); ExitProcess(0); }               // the screen is back (or the PC is about to sleep: see saver.h): leave at once (Windows waits for a saver to end before it carries on)
    sirenStop();
    ufoaudio::shutdown();
    // Closing while a download is running: tell the workers to stop (no new request starts, a running one stops reading), give them
    // a moment, and if one is still stuck in the network, leave without it. Waiting for a stalled connection used to keep the
    // process (and its single-instance lock) alive for a long time after the window had gone.
    weather::cancelRequests();
    W.stopAsync(); SW.stopAsync();
    const bool weatherDone = W.finish(1500);
    const bool stormsDone = SW.finish(weatherDone ? 1500 : 100);
    const bool locDone = LC.finish(weatherDone && stormsDone ? 500 : 100);
    saveSettings();
    SetThreadExecutionState(ES_CONTINUOUS);
    if (!weatherDone || !stormsDone || !locDone) ExitProcess(0);    // a stuck worker is still running: end the process before anything it uses is torn down
    moonsprite::shutdown();
    G.app.releaseDeviceResources(G.gfx);
    G.gfx.shutdown();                                               // release all COM objects & unregister DirectWrite loader BEFORE CoUninitialize
    CoUninitialize();
    return 0;
}
