// Meridian settings.ini persistence, session recovery, saved-place timezone resolution, and downgrade-safe unknown-field preservation.
#pragma once
#include "app.h"
#include "textesc.h"
#include "robust.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace app {

constexpr int SETTINGS_VERSION = 1;

inline const char* const SETTINGS_WKEY[WG_COUNT] = {
    "world", "month", "system", "note", "countdown", "phases", "week", "today",
    "holidays", "hourly", "daylight", "sky", "conditions", "year", "orrery"
};

inline std::string settingsLowerTrim(std::string v) {
    v = util::trim(v);
    for (auto& ch : v) if (ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32);
    return v;
}

inline std::string iniSingleLine(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char ch : s) if (ch != '\r' && ch != '\n' && ch != '\0') out.push_back(ch);
    return out;
}

inline std::vector<std::string> settingsSplitCsv(const std::string& v) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i <= v.size()) {
        size_t e = v.find(',', i);
        if (e == std::string::npos) e = v.size();
        std::string t = util::trim(v.substr(i, e - i));
        if (!t.empty()) out.push_back(t);
        i = e + 1;
    }
    return out;
}

// Resolves a saved place's local UTC offset (seconds) with Windows Dynamic DST authority:
// 1. Saved place's persisted Windows/IANA tz id (`geo.tz`),
// 2. Known city/coordinate Windows TZ resolution (`resolvePlaceWinTz`),
// 3. Authoritative remote timezone offset from the weather provider (`wx.valid && wx.hasUtcOffset`),
// 4. Longitude solar zone estimate (never silently falls back to the host PC's unrelated timezone).
inline int resolvePlaceOffset(const astro::Geo& geo, const weather::Data& wx, double t, bool* dynamicDstOut = nullptr, std::string* resolvedTzOut = nullptr) {
    if (dynamicDstOut) *dynamicDstOut = false;
    if (resolvedTzOut) resolvedTzOut->clear();
    int off = 0;
    if (!geo.tz.empty() && lookupWinTzOffset(geo.tz, t, off)) {
        if (dynamicDstOut) *dynamicDstOut = true;
        if (resolvedTzOut) *resolvedTzOut = geo.tz;
        return off;
    }
    const std::string inferred = resolvePlaceWinTz(geo.name, geo.lat, geo.lon);
    if (!inferred.empty() && lookupWinTzOffset(inferred, t, off)) {
        if (dynamicDstOut) *dynamicDstOut = true;
        if (resolvedTzOut) *resolvedTzOut = inferred;
        return off;
    }
    if (wx.valid && !wx.timezone.empty()) {
        const std::string fromWx = ianaToWinTz(wx.timezone);
        if (!fromWx.empty() && lookupWinTzOffset(fromWx, t, off)) {
            if (dynamicDstOut) *dynamicDstOut = true;
            if (resolvedTzOut) *resolvedTzOut = fromWx;
            return off;
        }
    }
    if (wx.valid && wx.hasUtcOffset) {
        return wx.utcOffset;
    }
    return (int)(std::lround(geo.lon / 15.0) * 3600);
}

inline bool isKnownSettingsKey(const std::string& sec, const std::string& key) {
    if (sec == "places" || sec == "events") return true;
    static const std::set<std::pair<std::string, std::string>> kKnown = {
        {"meridian", "settings_version"}, {"meridian", "written_by"},
        {"location", "name"}, {"location", "lat"}, {"location", "lon"}, {"location", "tz"},
        {"location", "located"}, {"location", "source"}, {"location", "accuracy_m"}, {"location", "found_at"},
        {"location", "country"}, {"location", "region"}, {"location", "showing"}, {"location", "showing_coords"},
        {"location", "place_local_time"},
        {"view", "theme"}, {"view", "face"}, {"view", "theme_pack"}, {"view", "face_pack"}, {"view", "face_is_pack"},
        {"view", "h24"}, {"view", "seconds"}, {"view", "fahrenheit"}, {"view", "poya"}, {"view", "pixel_shift"},
        {"view", "keep_awake"}, {"view", "topmost"}, {"view", "fullscreen"}, {"view", "brightness"},
        {"view", "ember_at_bedtime"}, {"view", "bedtime_from"}, {"view", "bedtime_to"}, {"view", "auto_dim"},
        {"view", "dim_at_night"}, {"view", "hourly_chime"},
        {"weather", "contact"}, {"weather", "weatherapi_key"}, {"weather", "openweather_key"},
        {"sky", "days"}, {"sky", "meteors"}, {"sky", "planets"}, {"sky", "eclipses"}, {"sky", "chips"}, {"sky", "animate"},
        {"elements", "today"}, {"elements", "weather"}, {"elements", "sun_moon"}, {"elements", "day_band"},
        {"elements", "labels"}, {"elements", "constellations"}, {"elements", "milky_way"}, {"elements", "ecliptic"},
        {"elements", "clouds"}, {"elements", "tracks"}, {"elements", "day_stars"},
        {"panel", "sky_events"}, {"panel", "holidays"}, {"panel", "world_clocks"}, {"panel", "seconds"},
        {"panel", "transition"}, {"panel", "pause_on_hover"}, {"panel", "dots"}, {"panel", "calendar_holidays"},
        {"holidays", "auto"}, {"holidays", "countries"}, {"holidays", "uk_nation"},
        {"skymap", "heading"}, {"skymap", "free_rotate"}, {"skymap", "seconds_per_turn"}, {"skymap", "direction"}, {"skymap", "orrery"},
        {"quiet", "weather"}, {"quiet", "strength"}, {"quiet", "sun_flare"}, {"quiet", "smooth_motion"},
        {"quiet", "sky_rain"}, {"quiet", "midnight_sky"}, {"quiet", "aurora_auto"}, {"quiet", "glass_drops"},
        {"quiet", "aliens"}, {"quiet", "weather_light"}, {"quiet", "weather_light_strength"},
        {"storms", "watch"}, {"storms", "radius_km"},
        {"themes", "disabled"},
        {"widgets", "list"}, {"widgets", "per_page"}, {"widgets", "page_seconds"}, {"widgets", "layout"},
        {"widgets", "pinned"}, {"widgets", "clocks"}, {"widgets", "week_starts_monday"}, {"widgets", "note"},
        {"widgets", "note_format"}, {"widgets", "countdown_label"}, {"widgets", "countdown_date"},
        {"window", "x"}, {"window", "y"}, {"window", "w"}, {"window", "h"},
        {"timer", "duration"}, {"timer", "mode"}, {"timer", "state"}, {"timer", "end_at"}, {"timer", "paused_remaining"},
        {"timer", "sw_running"}, {"timer", "sw_start"}, {"timer", "sw_elapsed"}, {"timer", "sw_laps"},
        {"timer", "alarm_minutes"}, {"timer", "alarm_on"},
        {"focus", "duration"}, {"focus", "backdrop"}, {"focus", "state"}, {"focus", "end_at"}, {"focus", "paused_remaining"},
    };
    if (sec == "weather") {
        for (int i = 0; i < weather::PV_COUNT; ++i) {
            if (key == weather::providerId(i)) return true;
        }
    }
    return kKnown.count({sec, key}) > 0;
}

inline void parseSettingsText(App& a, const std::string& txt, double now, int* fileVersionOut = nullptr) {
    Settings& st = a.st;
    TimerState& tm = a.timer;
    util::Ini ini = util::Ini::parse(txt);
    const int fileVersion = (int)ini.getNum("meridian", "settings_version", 0);
    a.loadedSettingsVersion = std::max(SETTINGS_VERSION, fileVersion);
    if (fileVersionOut) *fileVersionOut = fileVersion;

    // Preserve every unrecognized section/key verbatim so downgrading from a newer schema never discards newer-only settings.
    a.unknownIni.clear();
    for (const auto& sp : ini.orderedData) {
        std::vector<std::pair<std::string, std::string>> extras;
        for (const auto& kv : sp.second) {
            if (!isKnownSettingsKey(sp.first, kv.first)) extras.push_back(kv);
        }
        if (!extras.empty()) a.unknownIni.push_back({sp.first, std::move(extras)});
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

    a.home.name = ini.get("location", "name", a.home.name);
    a.home.lat = std::max(-89.0, std::min(89.0, ini.getNum("location", "lat", a.home.lat)));
    a.home.lon = std::max(-180.0, std::min(180.0, ini.getNum("location", "lon", a.home.lon)));
    a.home.tz = util::trim(ini.get("location", "tz", a.home.tz));
    if (a.home.tz.empty()) a.home.tz = resolvePlaceWinTz(a.home.name, a.home.lat, a.home.lon);
    st.placeLocalTime = ini.getBool("location", "place_local_time", st.placeLocalTime);
    a.located = ini.getBool("location", "located", true);
    a.locSource = ini.get("location", "source", "");
    a.locAcc = ini.getNum("location", "accuracy_m", -1);
    a.locAt = ini.getNum("location", "found_at", 0);
    a.homeCountry = ini.get("location", "country", "");
    a.homeRegion = ini.get("location", "region", "");

    {
        st.holAuto = ini.getBool("holidays", "auto", st.holAuto);
        const std::string list = settingsLowerTrim(ini.get("holidays", "countries", "lk"));
        int mk = 0;
        if (list.find("lk") != std::string::npos) mk |= holiday::C_LK;
        if (list.find("uk") != std::string::npos) mk |= holiday::C_UK;
        if (list.find("us") != std::string::npos) mk |= holiday::C_US;
        st.holMask = mk;
        const std::string nat = settingsLowerTrim(ini.get("holidays", "uk_nation", "auto"));
        st.ukPref = nat.find("scot") != std::string::npos ? 2 : nat.find("north") != std::string::npos ? 3 : nat.find("eng") != std::string::npos || nat.find("wales") != std::string::npos ? 1 : 0;
    }

    st.winX = (int)ini.getNum("window", "x", st.winX);
    st.winY = (int)ini.getNum("window", "y", st.winY);
    st.winW = (int)ini.getNum("window", "w", st.winW);
    st.winH = (int)ini.getNum("window", "h", st.winH);

    st.places.clear();
    for (auto& kv : ini.ordered("places")) {
        if (st.places.size() >= 200) break;
        double la = 0, lo = 0;
        const size_t bar = kv.second.find('|');
        const std::string coords = (bar == std::string::npos) ? kv.second : kv.second.substr(0, bar);
        std::string tz = (bar == std::string::npos) ? std::string() : util::trim(kv.second.substr(bar + 1));
        if (std::sscanf(coords.c_str(), "%lf , %lf", &la, &lo) == 2 && std::fabs(la) <= 89.0 && std::fabs(lo) <= 180.0) {
            astro::Geo g;
            g.name = kv.first;
            g.lat = la;
            g.lon = lo;
            g.tz = tz.empty() ? resolvePlaceWinTz(g.name, g.lat, g.lon) : tz;
            st.places.push_back(g);
        }
    }

    {
        const std::string showing = ini.get("location", "showing", "");
        const std::string sc = ini.get("location", "showing_coords", "");
        double sla = 0, slo = 0;
        const bool hasCoords = !sc.empty() && std::sscanf(sc.c_str(), "%lf , %lf", &sla, &slo) == 2;
        a.placeIdx = -1;
        if (hasCoords) {
            for (size_t i = 0; i < st.places.size(); ++i)
                if ((showing.empty() || st.places[i].name == showing) &&
                    std::fabs(st.places[i].lat - sla) < 0.001 && std::fabs(st.places[i].lon - slo) < 0.001) { a.placeIdx = (int)i; break; }
            if (a.placeIdx < 0)
                for (size_t i = 0; i < st.places.size(); ++i)
                    if (std::fabs(st.places[i].lat - sla) < 0.001 && std::fabs(st.places[i].lon - slo) < 0.001) { a.placeIdx = (int)i; break; }
        }
        if (a.placeIdx < 0 && !showing.empty())
            for (size_t i = 0; i < st.places.size(); ++i)
                if (st.places[i].name == showing) { a.placeIdx = (int)i; break; }
    }

    st.events.clear();
    for (auto& kv : ini.ordered("events")) {
        today::CustomEvent e; e.title = kv.second;
        std::string key = kv.first; if (key.compare(0, 2, "--") == 0) key.erase(0, 2);
        const int used = util::parseEventDatePrefix(key.c_str(), e.y, e.m, e.d);
        if (!used || !util::onlySpace(key.c_str() + used) || e.title.empty()) continue;
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
    { std::string k = settingsLowerTrim(ini.get("panel", "transition", "slide")); st.panelStyle = k == "rise" ? 1 : k == "fade" ? 2 : 0; }
    st.panelHold = ini.getBool("panel", "pause_on_hover", st.panelHold); st.panelDots = ini.getBool("panel", "dots", st.panelDots);

    st.skyHeading = (float)astro::norm360(ini.getNum("skymap", "heading", st.skyHeading));
    st.skySpin = ini.getBool("skymap", "free_rotate", st.skySpin);
    st.skySpinSecs = (int)std::max(5.0, std::min(7200.0, ini.getNum("skymap", "seconds_per_turn", st.skySpinSecs)));
    st.skySpinDir = settingsLowerTrim(ini.get("skymap", "direction", st.skySpinDir > 0 ? "clockwise" : "counterclockwise")).compare(0, 7, "counter") == 0 ? -1 : 1;
    st.skyViewMode = ini.getBool("skymap", "orrery", st.skyViewMode == 1) ? 1 : 0;

    st.quietFx = ini.getBool("quiet", "weather", st.quietFx);
    st.stormWatch = ini.getBool("storms", "watch", st.stormWatch);
    st.stormRadius = (int)std::max(100.0, std::min(6000.0, ini.getNum("storms", "radius_km", st.stormRadius)));
    st.quietStrength = (float)std::max(0.5, std::min(2.0, ini.getNum("quiet", "strength", 100.0) / 100.0));
    st.quietFlare = ini.getBool("quiet", "sun_flare", st.quietFlare);
    st.quietSmooth = ini.getBool("quiet", "smooth_motion", st.quietSmooth);
    st.skyRain = ini.getBool("quiet", "sky_rain", st.skyRain);
    st.spaceFx = ini.getBool("quiet", "midnight_sky", st.spaceFx);
    st.auroraAuto = ini.getBool("quiet", "aurora_auto", st.auroraAuto);
    st.glassDrops = ini.getBool("quiet", "glass_drops", st.glassDrops);
    st.aliensEnabled = ini.getBool("quiet", "aliens", st.aliensEnabled);
    st.weatherMood = ini.getBool("quiet", "weather_light", st.weatherMood);
    st.moodStrength = (float)std::max(0.5, std::min(1.5, ini.getNum("quiet", "weather_light_strength", 100.0) / 100.0));

    st.themePack = ini.get("view", "theme_pack", "");
    st.facePack = ini.get("view", "face_pack", "");
    if (ini.getBool("view", "face_is_pack", false) && !st.facePack.empty()) st.face = FACE_PACK;
    st.packsOff = settingsSplitCsv(ini.get("themes", "disabled", ""));

    if (ini.sections.count("widgets")) {
        if (ini.sections["widgets"].count("list")) {
            st.widgets.clear();
            for (auto& t : settingsSplitCsv(ini.get("widgets", "list"))) {
                std::string k = settingsLowerTrim(t);
                for (int i = 0; i < WG_COUNT; ++i) if (k == SETTINGS_WKEY[i] && std::find(st.widgets.begin(), st.widgets.end(), i) == st.widgets.end()) st.widgets.push_back(i);
            }
        }
        if (!ini.sections["widgets"].count("layout")) {
            for (int id : {WG_HOURLY, WG_HOLIDAYS, WG_DAYLIGHT, WG_SKY, WG_AIR, WG_YEAR})
                if (std::find(st.widgets.begin(), st.widgets.end(), id) == st.widgets.end()) st.widgets.push_back(id);
        }
        { std::string k = settingsLowerTrim(ini.get("widgets", "per_page", st.wgPerPage <= 0 ? "all" : std::to_string(st.wgPerPage)));
          st.wgPerPage = k == "all" ? 0 : std::max(1, std::min(6, std::atoi(k.c_str()))); }
        st.wgSecs = (int)std::max(4.0, std::min(300.0, ini.getNum("widgets", "page_seconds", st.wgSecs)));
        st.pinned = -1;
        { std::string k = settingsLowerTrim(ini.get("widgets", "pinned", "none")); for (int i = 0; i < WG_COUNT; ++i) if (k == SETTINGS_WKEY[i] && i != WG_ORRERY) st.pinned = i; }
        if (ini.sections["widgets"].count("clocks")) {
            st.clocks.clear();
            for (auto& t : settingsSplitCsv(ini.get("widgets", "clocks"))) {
                std::string k = settingsLowerTrim(t);
                for (int i = 0; i < cityCount(); ++i) if (k == settingsLowerTrim(cityName(i)) && st.clocks.size() < 4 && std::find(st.clocks.begin(), st.clocks.end(), i) == st.clocks.end()) st.clocks.push_back(i);
            }
        }
        st.weekMon = ini.getBool("widgets", "week_starts_monday", st.weekMon);
        st.note = unescapeNl(ini.get("widgets", "note", st.note), ini.getNum("widgets", "note_format", 1) >= 2);
        st.cdLabel = ini.get("widgets", "countdown_label", st.cdLabel);
        st.cdDate = ini.get("widgets", "countdown_date", st.cdDate);
    }

    // ---- Timer, Stopwatch (+ laps), and Alarm active session restoration
    tm.durationSec = std::max(TIMER_MIN_SEC, std::min(TIMER_MAX_SEC, ini.getNum("timer", "duration", tm.durationSec)));
    tm.mode = std::max(0, std::min(2, (int)ini.getNum("timer", "mode", tm.mode)));
    tm.running = false; tm.finished = false; tm.downtimeExpired = false; tm.endAt = 0; tm.finishedAt = 0;
    tm.pausedRemaining = tm.durationSec;
    {
        const std::string tState = settingsLowerTrim(ini.get("timer", "state", "idle"));
        if (tState == "running") {
            const double endAt = ini.getNum("timer", "end_at", 0.0);
            if (endAt > 0.0 && now > 0.0) {
                if (endAt > now) {
                    tm.running = true;
                    tm.endAt = endAt;
                    tm.pausedRemaining = std::max(0.0, endAt - now);
                } else {
                    // Deadline passed while Meridian was closed: restore as acknowledged finished (00:00) without surprise siren.
                    tm.finished = true;
                    tm.downtimeExpired = true;
                    tm.finishedAt = endAt;
                    tm.pausedRemaining = 0.0;
                }
            }
        } else if (tState == "paused") {
            tm.pausedRemaining = std::max(0.0, std::min(TIMER_MAX_SEC, ini.getNum("timer", "paused_remaining", tm.durationSec)));
        } else if (tState == "finished") {
            tm.finished = true;
            tm.downtimeExpired = true;
            tm.pausedRemaining = 0.0;
            tm.finishedAt = ini.getNum("timer", "end_at", now);
        }
        tm.swRunning = ini.getBool("timer", "sw_running", false);
        tm.swElapsed = std::max(0.0, ini.getNum("timer", "sw_elapsed", 0.0));
        tm.swStart = ini.getNum("timer", "sw_start", 0.0);
        if (tm.swRunning && (tm.swStart <= 0.0 || (now > 0.0 && tm.swStart > now + 60.0))) {
            tm.swStart = now > 0.0 ? now : 0.0;
        }
        tm.laps.clear();
        for (const std::string& tok : settingsSplitCsv(ini.get("timer", "sw_laps", ""))) {
            if (tm.laps.size() >= 500) break;
            char* e = nullptr;
            double v = std::strtod(tok.c_str(), &e);
            if (e != tok.c_str() && std::isfinite(v) && v >= 0.0) tm.laps.push_back(v);
        }
    }
    tm.alarmMinutes = (int)std::max(0.0, std::min(1439.0, ini.getNum("timer", "alarm_minutes", tm.alarmMinutes)));
    tm.alarmOn = ini.getBool("timer", "alarm_on", false);

    // ---- Focus Studio active session restoration
    {
        FocusState& fc = a.focus;
        fc.durationSec = std::max(TIMER_MIN_SEC, std::min(TIMER_MAX_SEC, ini.getNum("focus", "duration", fc.durationSec)));
        fc.running = false; fc.finished = false; fc.downtimeExpired = false; fc.endAt = 0; fc.finishedAt = 0;
        fc.pausedRemaining = fc.durationSec;
        const std::string fState = settingsLowerTrim(ini.get("focus", "state", "idle"));
        if (fState == "running") {
            const double endAt = ini.getNum("focus", "end_at", 0.0);
            if (endAt > 0.0 && now > 0.0) {
                if (endAt > now) {
                    fc.running = true;
                    fc.endAt = endAt;
                    fc.pausedRemaining = std::max(0.0, endAt - now);
                } else {
                    fc.finished = true;
                    fc.downtimeExpired = true;
                    fc.finishedAt = endAt;
                    fc.pausedRemaining = 0.0;
                }
            }
        } else if (fState == "paused") {
            fc.pausedRemaining = std::max(0.0, std::min(TIMER_MAX_SEC, ini.getNum("focus", "paused_remaining", fc.durationSec)));
        } else if (fState == "finished") {
            fc.finished = true;
            fc.downtimeExpired = true;
            fc.pausedRemaining = 0.0;
            fc.finishedAt = ini.getNum("focus", "end_at", now);
        }
        static const char* const kFx[15] = {"off", "live", "rain", "snow", "clouds", "fog", "storm", "stars", "blizzard", "drizzle", "sleet", "hail", "gale", "midnight", "aurora"};
        const std::string bk = settingsLowerTrim(ini.get("focus", "backdrop", "live"));
        st.focusFx = 1;
        for (int i = 0; i < 15; ++i) if (bk == kFx[i]) { st.focusFx = i; break; }
    }
}

inline std::string serializeSettingsText(const App& a) {
    using util::fmt;
    const Settings& st = a.st;
    const TimerState& tm = a.timer;
    const FocusState& fc = a.focus;
    std::set<std::string> emittedExtraSecs;
    auto appendExtrasFor = [&](std::string& o, const std::string& sec) {
        for (const auto& sp : a.unknownIni) {
            if (sp.first == sec) {
                emittedExtraSecs.insert(sec);
                for (const auto& kv : sp.second) o += kv.first + "=" + kv.second + "\n";
            }
        }
    };

    std::string o;
    o += "; Meridian settings. Most of this is easier from the Settings panel in the app (press the comma key).\n";
    o += "; If you edit this file by hand, do it while the app is closed. Everything is saved automatically.\n\n";
    const int outVer = std::max(SETTINGS_VERSION, a.loadedSettingsVersion);
    o += fmt("[meridian]\n; which layout this file has, so a newer Meridian can convert an older file (and an older one can tell it is looking at a newer file). Leave it alone.\nsettings_version=%d\nwritten_by=%s\n", outVer, MERIDIAN_VERSION);
    appendExtrasFor(o, "meridian");
    o += "\n";

    o += "[location]\n; used for the sky, sunrise/sunset, the moon and the weather\n";
    o += "name=" + iniSingleLine(a.home.name) + "\n" + fmt("lat=%.4f\nlon=%.4f\n", a.home.lat, a.home.lon);
    if (!a.home.tz.empty()) o += "tz=" + iniSingleLine(a.home.tz) + "\n";
    o += "; FIND MY LOCATION (Settings > Place) fills this in and it is kept until you press the button again. located=0 only on a new install that has not found\n; its location yet: it then looks once at the next start. source is where the position came from, accuracy_m how far off it may be.\n";
    o += fmt("located=%d\nsource=", a.located ? 1 : 0) + iniSingleLine(a.locSource) + fmt("\naccuracy_m=%.0f\nfound_at=%.0f\n", a.locAcc, a.locAt);
    o += "; the country (two-letter code) and region the lookup reported. They pick the holidays when [holidays] auto=1. Empty for a place typed by hand.\ncountry=" + iniSingleLine(a.homeCountry) + "\nregion=" + iniSingleLine(a.homeRegion) + "\n";
    const bool showSaved = a.placeIdx >= 0 && a.placeIdx < (int)st.places.size();
    o += "; which place is on show: the name and coordinates of one of the [places] below, or empty for the place above. Kept across restarts.\nshowing=" +
         (showSaved ? iniSingleLine(st.places[(size_t)a.placeIdx].name) : std::string()) + "\nshowing_coords=" +
         (showSaved ? fmt("%.4f,%.4f", st.places[(size_t)a.placeIdx].lat, st.places[(size_t)a.placeIdx].lon) : std::string()) +
         fmt("\n; when viewing a saved place (N), show the clock in that place's local timezone. 1 = on, 0 = off\nplace_local_time=%d\n", st.placeLocalTime ? 1 : 0);
    appendExtrasFor(o, "location");
    o += "\n";

    o += "[places]\n; extra places, cycle with N.  Name=lat,lon|WindowsTimeZone\n";
    for (const auto& p : st.places) {
        std::string safe = p.name;
        for (char& ch : safe) if (ch == '=' || ch == '\r' || ch == '\n') ch = '-';
        while (!safe.empty() && (safe[0] == ';' || safe[0] == '#' || safe[0] == '[')) safe.erase(safe.begin());
        if (safe.empty()) safe = "Place";
        std::string tz = p.tz.empty() ? resolvePlaceWinTz(p.name, p.lat, p.lon) : p.tz;
        for (char& ch : tz) if (ch == '\r' || ch == '\n' || ch == '|') ch = ' ';
        tz = util::trim(tz);
        if (!tz.empty()) o += safe + fmt("=%.4f,%.4f|%s\n", p.lat, p.lon, tz.c_str());
        else o += safe + fmt("=%.4f,%.4f\n", p.lat, p.lon);
    }

    o += "\n[events]\n; your own dates shown under TODAY.  MM-DD repeats every year, YYYY-MM-DD happens once.\n";
    for (const auto& e : st.events) o += (e.y ? fmt("%04d-%02d-%02d", e.y, e.m, e.d) : fmt("%02d-%02d", e.m, e.d)) + "=" + iniSingleLine(e.title) + "\n";

    o += "\n[view]\n";
    o += fmt("theme=%d\n; 0 auto, 1 day, 2 dusk, 3 night, 4 ember, 5 dawn, 6 sunset, 7 sunlit, 8 sunlit II, 9 galaxy, 10 midnight\nface=%d\n; 0 sky, 1 quiet, 2 timer, 3 almanac, 4 widgets, 5 focus\n", st.theme, st.face == FACE_PACK ? (int)FACE_QUIET : st.face);
    o += "; a theme pack (a folder in the themes folder) used as the theme, and the pack tab that was open. Leave empty for the built-in themes.\ntheme_pack=" + iniSingleLine(st.themePack) + "\nface_pack=" + iniSingleLine(st.facePack) + fmt("\nface_is_pack=%d\n", st.face == FACE_PACK ? 1 : 0);
    o += fmt("h24=%d\nseconds=%d\nfahrenheit=%d\npoya=%d\npixel_shift=%d\nkeep_awake=%d\ntopmost=%d\nfullscreen=%d\nbrightness=%.2f\n",
             st.h24, st.seconds, st.useF, st.poya, st.pixelShift, st.keepAwake, st.topmost, st.fullscreen, st.brightness);
    o += fmt("ember_at_bedtime=%d\nbedtime_from=%d\nbedtime_to=%d\nauto_dim=%d\ndim_at_night=%.2f\nhourly_chime=%d\n", st.emberAtNight, st.emberFrom, st.emberTo, st.autoDim, st.dimNight, st.hourlyChime ? 1 : 0);
    appendExtrasFor(o, "view");
    o += "\n";

    o += "[weather]\n; sources are tried in this order until one answers. 1 = on, 0 = off\n";
    for (int i = 0; i < weather::PV_COUNT; ++i) o += fmt("%s=%d\n", weather::providerId(i), st.provEnabled[i] ? 1 : 0);
    o += "; optional: an email or website. MET Norway and NWS ask apps to identify themselves in the User-Agent\ncontact=" + iniSingleLine(st.contact) + "\n";
    o += "; optional free API keys for WeatherAPI.com and OpenWeatherMap (stay local, also provide backup Air Quality)\n";
    o += "weatherapi_key=" + iniSingleLine(st.weatherApiKey) + "\nopenweather_key=" + iniSingleLine(st.openWeatherKey) + "\n";
    appendExtrasFor(o, "weather");
    o += "\n";

    o += "[sky]\n; events appear on the sky map when they are this many days away or less. 1 = on, 0 = off\n";
    o += fmt("days=%d\nmeteors=%d\nplanets=%d\neclipses=%d\nchips=%d\nanimate=%d\n", st.evDays, st.evMeteors, st.evPlanets, st.evEclipses, st.evChips, st.evAnimate);
    appendExtrasFor(o, "sky");
    o += "\n";

    o += fmt("[elements]\n; parts of the Sky face. 1 = on, 0 = off\ntoday=%d\nweather=%d\nsun_moon=%d\nday_band=%d\nlabels=%d\nconstellations=%d\nmilky_way=%d\necliptic=%d\nclouds=%d\ntracks=%d\nday_stars=%d\n",
             st.elToday, st.elWeather, st.elSunMoon, st.elBand, st.elLabels, st.elConst, st.elMilky, st.elEcliptic, st.elClouds, st.elTracks, st.dayStars ? 1 : 0);
    appendExtrasFor(o, "elements");
    o += "\n";

    o += fmt("[panel]\n; the left side of the Sky face rotates between Today (see today= above), Sky events, Holidays and World clocks. 1 = on, 0 = off\nsky_events=%d\nholidays=%d\nworld_clocks=%d\n", st.elEvents, st.elHolidays, st.elWorld);
    o += fmt("; seconds each panel stays, and how the next one arrives: slide, rise or fade\nseconds=%d\ntransition=%s\npause_on_hover=%d\ndots=%d\n", st.panelSecs, st.panelStyle == 1 ? "rise" : st.panelStyle == 2 ? "fade" : "slide", st.panelHold, st.panelDots);
    o += fmt("; mark holidays on the month calendar widget (hover a date for details)\ncalendar_holidays=%d\n", st.holCal);
    appendExtrasFor(o, "panel");
    o += "\n";

    o += fmt("[holidays]\n; whose holidays to show, everywhere: any mix of lk (Sri Lanka), uk (United Kingdom) and us (United States).\n; auto=1 picks the country your place is in (nothing for any other country); auto=0 uses the countries listed here.\nauto=%d\n", st.holAuto);
    { std::string l; if (st.holMask & holiday::C_LK) l += "lk"; if (st.holMask & holiday::C_UK) l += l.empty() ? "uk" : ",uk"; if (st.holMask & holiday::C_US) l += l.empty() ? "us" : ",us"; o += "countries=" + l + "\n"; }
    o += std::string("; which UK list: auto (from your place), england-wales, scotland or northern-ireland\nuk_nation=") + (st.ukPref == 1 ? "england-wales" : st.ukPref == 2 ? "scotland" : st.ukPref == 3 ? "northern-ireland" : "auto") + "\n";
    appendExtrasFor(o, "holidays");
    o += "\n";

    o += fmt("[skymap]\n; which compass direction is at the top of the sky map, in degrees (0 north, 90 east, 180 south, 270 west)\nheading=%.0f\n", st.skyHeading);
    o += fmt("; free_rotate=1 turns the whole sky slowly, one full turn every seconds_per_turn seconds; direction is clockwise or counterclockwise\nfree_rotate=%d\nseconds_per_turn=%d\ndirection=%s\n; 0 = Sky Dome, 1 = Live Solar System Orrery\norrery=%d\n",
             st.skySpin, st.skySpinSecs, st.skySpinDir > 0 ? "clockwise" : "counterclockwise", st.skyViewMode == 1 ? 1 : 0);
    appendExtrasFor(o, "skymap");
    o += "\n";

    o += fmt("[quiet]\n; draw the weather behind the clock on the Quiet face (clouds, wind, rain, a tornado in a thunderstorm). 1 = on, 0 = off\nweather=%d\n; how strongly it is drawn, in percent (50 to 200). 100 is the default\nstrength=%.0f\n; a soft sun flare from the side the Sun is on, in the morning and the evening. 1 = on, 0 = off\nsun_flare=%d\n; rain and wind drawn at your screen's own refresh rate (up to 90 frames a second) so they stay smooth. 0 = 30 frames a second, lighter on a laptop battery\nsmooth_motion=%d\n", st.quietFx, st.quietStrength * 100.f, st.quietFlare, st.quietSmooth);
    o += fmt("; rain also falls behind the map on the Sky face. 1 = on, 0 = off\nsky_rain=%d\n; the Galaxy theme's stars, meteors and comet behind the Quiet and Sky faces. 1 = on, 0 = off\nmidnight_sky=%d\n; live aurora borealis at night during NOAA geomagnetic storms (1 = on, 0 = off)\naurora_auto=%d\n; wet-horizon clock reflections on Quiet during rain (1 = on, 0 = off)\nglass_drops=%d\n", st.skyRain, st.spaceFx, st.auroraAuto ? 1 : 0, st.glassDrops ? 1 : 0);
    if (!st.aliensEnabled) o += "aliens=0\n";
    o += fmt("; the light follows the weather on every face: heavy cloud and storms darken and cool the theme, fog flattens it. 1 = on, 0 = off\nweather_light=%d\n; how strong that is, in percent (50 to 150)\nweather_light_strength=%.0f\n", st.weatherMood, st.moodStrength * 100.f);
    appendExtrasFor(o, "quiet");
    o += "\n";

    o += fmt("[storms]\n; show named storms (cyclones, typhoons, hurricanes) when one is near you: its name, distance and direction. 1 = on, 0 = off\nwatch=%d\n; how far away still counts as near, in km\nradius_km=%d\n", st.stormWatch, st.stormRadius);
    appendExtrasFor(o, "storms");
    o += "\n";

    {
        std::string off; for (const std::string& id : st.packsOff) { const std::string sid = iniSingleLine(id); if (!sid.empty()) off += std::string(off.empty() ? "" : ",") + sid; }
        o += "[themes]\n; theme packs are folders in the 'themes' folder next to Meridian.exe (or in %APPDATA%\\Meridian\\themes). Drop one in, or delete it, and Meridian notices.\n; packs listed here are switched off (they stay installed). Easier from Settings > Themes.\ndisabled=" + off + "\n";
        appendExtrasFor(o, "themes");
        o += "\n";
    }
    {
        std::string wl, cl;
        for (int id : st.widgets) if (id >= 0 && id < WG_COUNT) wl += std::string(wl.empty() ? "" : ",") + SETTINGS_WKEY[id];
        for (int i : st.clocks) cl += std::string(cl.empty() ? "" : ",") + cityName(i);
        o += "[widgets]\n; the Widgets face, in order: world, month, system, note, countdown, phases, week, today, holidays, hourly, daylight, sky, conditions, year, orrery\nlist=" + wl + "\n";
        o += std::string("; widgets shown together on one page (1 to 6, or all) and how many seconds each page stays; the transition is the one set under [panel]\nper_page=") + (st.wgPerPage <= 0 ? std::string("all") : std::to_string(st.wgPerPage)) + fmt("\npage_seconds=%d\nlayout=2\n", st.wgSecs);
        o += std::string("; one widget can join the rotating panel on the Sky face (a widget name, or none)\npinned=") + (st.pinned >= 0 && st.pinned < WG_COUNT ? SETTINGS_WKEY[st.pinned] : "none") + "\n";
        o += "; world clock cities, up to four\nclocks=" + cl + "\n";
        o += fmt("week_starts_monday=%d\n", st.weekMon) + "note=" + escapeNl(st.note) + "\nnote_format=2\ncountdown_label=" + iniSingleLine(st.cdLabel) + "\ncountdown_date=" + iniSingleLine(st.cdDate) + "\n";
        appendExtrasFor(o, "widgets");
        o += "\n";
    }
    o += fmt("[window]\nx=%d\ny=%d\nw=%d\nh=%d\n", st.winX, st.winY, st.winW, st.winH);
    appendExtrasFor(o, "window");
    o += "\n";

    // Active Timer / Stopwatch / Alarm session state
    {
        const char* tState = tm.running ? "running" : (tm.finished ? "finished" : (std::fabs(tm.pausedRemaining - tm.durationSec) > 0.05 ? "paused" : "idle"));
        std::string lapsStr;
        for (size_t i = 0; i < tm.laps.size(); ++i) {
            if (i) lapsStr += ",";
            lapsStr += fmt("%.3f", tm.laps[i]);
        }
        o += fmt("[timer]\nduration=%.0f\nmode=%d\nstate=%s\nend_at=%.3f\npaused_remaining=%.3f\nsw_running=%d\nsw_start=%.3f\nsw_elapsed=%.3f\nsw_laps=%s\nalarm_minutes=%d\nalarm_on=%d\n",
                 tm.durationSec, tm.mode, tState,
                 tm.running ? tm.endAt : (tm.finished ? tm.finishedAt : 0.0),
                 tm.pausedRemaining,
                 tm.swRunning ? 1 : 0, tm.swRunning ? tm.swStart : 0.0, tm.swElapsed, lapsStr.c_str(),
                 tm.alarmMinutes, tm.alarmOn ? 1 : 0);
        appendExtrasFor(o, "timer");
        o += "\n";
    }

    // Active Focus Studio session state
    {
        static const char* const kFxNames[15] = {"off", "live", "rain", "snow", "clouds", "fog", "storm", "stars", "blizzard", "drizzle", "sleet", "hail", "gale", "midnight", "aurora"};
        const int fxIdx = std::max(0, std::min(14, st.focusFx));
        const char* fState = fc.running ? "running" : (fc.finished ? "finished" : (std::fabs(fc.pausedRemaining - fc.durationSec) > 0.05 ? "paused" : "idle"));
        o += fmt("[focus]\nduration=%.0f\nbackdrop=%s\nstate=%s\nend_at=%.3f\npaused_remaining=%.3f\n",
                 fc.durationSec, kFxNames[fxIdx], fState,
                 fc.running ? fc.endAt : (fc.finished ? fc.finishedAt : 0.0),
                 fc.pausedRemaining);
        appendExtrasFor(o, "focus");
    }

    // Emit any completely unknown sections from newer schemas verbatim at the end
    for (const auto& sp : a.unknownIni) {
        if (emittedExtraSecs.count(sp.first)) continue;
        o += "\n[" + sp.first + "]\n";
        for (const auto& kv : sp.second) o += kv.first + "=" + kv.second + "\n";
    }
    return o;
}

#ifdef _WIN32
// Preserves the pristine newer-schema snapshot across consecutive older-version launches,
// while refreshing & verifying it whenever a newer version writes a changed settings.ini.
inline bool backupNewerSettingsSnapshot(const std::wstring& settingsPath, const std::string& rawText, int fileVersion) {
    if (fileVersion <= SETTINGS_VERSION) return true;
    const std::wstring bak = settingsPath + L".from-newer-version-" + std::to_wstring(fileVersion) + L".bak";
    auto readAll = [](const std::wstring& p, std::string& out) -> bool {
        HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER sz{}; GetFileSizeEx(h, &sz);
        if (sz.QuadPart < 0 || sz.QuadPart > 8 * 1024 * 1024) { CloseHandle(h); return false; }
        out.assign((size_t)sz.QuadPart, '\0'); DWORD got = 0;
        bool ok = sz.QuadPart == 0 || ReadFile(h, &out[0], (DWORD)sz.QuadPart, &got, nullptr);
        CloseHandle(h); out.resize(got);
        return ok;
    };
    const std::string writtenBy = util::Ini::parse(rawText).get("meridian", "written_by", "");
    std::string curBak;
    const bool hasBak = readAll(bak, curBak);
    if (hasBak && writtenBy == MERIDIAN_VERSION) return true;   // already backed up; do not overwrite with our own re-save
    if (hasBak && curBak == rawText) return true;
    if (!robust::atomicWrite(bak, rawText)) return false;
    std::string verifyBak;
    return readAll(bak, verifyBak) && verifyBak == rawText;
}
#endif

} // namespace app
