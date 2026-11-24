#include "weather.h"
#include <atomic>
#include <algorithm>
#include <mutex>
#include "minijson.h"
#include "util.h"
#include "astro.h"
#include <cmath>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <map>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#endif

namespace weather {

using util::fmt;

double toUnit(double c, bool useF) { return useF ? c * 9.0 / 5.0 + 32.0 : c; }

Kind describe(int code, bool isDay) {
    switch (code) {
        case 0: return {isDay ? Icon::ClearDay : Icon::ClearNight, isDay ? "Clear sky" : "Clear night"};
        case 1: return {isDay ? Icon::ClearDay : Icon::ClearNight, "Mainly clear"};
        case 2: return {isDay ? Icon::PartlyDay : Icon::PartlyNight, "Partly cloudy"};
        case 3: return {Icon::Cloudy, "Overcast"};
        case 45: return {Icon::Fog, "Fog"};
        case 48: return {Icon::Fog, "Rime fog"};
        case 51: return {Icon::Drizzle, "Light drizzle"};
        case 53: return {Icon::Drizzle, "Drizzle"};
        case 55: return {Icon::Drizzle, "Heavy drizzle"};
        case 56: case 57: return {Icon::Drizzle, "Freezing drizzle"};
        case 61: return {Icon::Rain, "Light rain"};
        case 63: return {Icon::Rain, "Rain"};
        case 65: return {Icon::Rain, "Heavy rain"};
        case 66: case 67: return {Icon::Rain, "Freezing rain"};
        case 71: return {Icon::Snow, "Light snow"};
        case 73: return {Icon::Snow, "Snow"};
        case 75: return {Icon::Snow, "Heavy snow"};
        case 77: return {Icon::Snow, "Snow grains"};
        case 80: return {Icon::Showers, "Light showers"};
        case 81: return {Icon::Showers, "Showers"};
        case 82: return {Icon::Showers, "Violent showers"};
        case 85: case 86: return {Icon::Snow, "Snow showers"};
        case 95: return {Icon::Thunder, "Thunderstorm"};
        case 96: case 99: return {Icon::Thunder, "Thunderstorm with hail"};
        default: return {Icon::Cloudy, "Cloudy"};
    }
}

const char* aqiLabel(double a, AqiScale scale) {
    if (scale == AqiScale::Eu) {
        // Official EEA / Open-Meteo European AQI bands (0..100+)
        if (a <= 20.0) return "Good";
        if (a <= 40.0) return "Fair";
        if (a <= 60.0) return "Moderate";
        if (a <= 80.0) return "Poor";
        if (a <= 100.0) return "Very poor";
        return "Extremely poor";
    }
    if (a <= 50.0) return "Good";
    if (a <= 100.0) return "Moderate";
    if (a <= 150.0) return "Unhealthy for some";
    if (a <= 200.0) return "Unhealthy";
    if (a <= 300.0) return "Very unhealthy";
    return "Hazardous";
}

const char* aqiScaleName(AqiScale scale) {
    switch (scale) {
        case AqiScale::Us: return "US AQI";
        case AqiScale::Eu: return "EU AQI";
        case AqiScale::Pm25Est: return "Est. US AQI (PM2.5)";
        default: return "AQI";
    }
}

const char* aqiScaleShort(AqiScale scale) {
    switch (scale) {
        case AqiScale::Us: return "US AQI";
        case AqiScale::Eu: return "EU AQI";
        case AqiScale::Pm25Est: return "EST AQI";
        default: return "AQI";
    }
}

bool airFresh(const Data& d, double now, double maxAgeSec) {
    if (!d.hasAir) return false;
    if (now <= 0.0 || maxAgeSec <= 0.0) return true;
    const double ts = d.airObsTime > 0.0 ? d.airObsTime : d.fetched;
    if (ts <= 0.0) return false;
    return std::fabs(now - ts) <= maxAgeSec;
}

const char* uvLabel(double uv) {
    if (uv < 3) return "Low";
    if (uv < 6) return "Moderate";
    if (uv < 8) return "High";
    if (uv < 11) return "Very high";
    return "Extreme";
}

std::string forecastPath(double lat, double lon) {
    return fmt("/v1/forecast?latitude=%.2f&longitude=%.2f"
               "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,precipitation,weather_code,cloud_cover,pressure_msl,wind_speed_10m,wind_direction_10m,wind_gusts_10m"
               "&hourly=temperature_2m,apparent_temperature,relative_humidity_2m,precipitation_probability,precipitation,weather_code,cloud_cover,wind_speed_10m,uv_index,is_day"
               "&daily=weather_code,temperature_2m_max,temperature_2m_min,apparent_temperature_max,apparent_temperature_min,sunrise,sunset,uv_index_max,precipitation_sum,precipitation_probability_max,wind_speed_10m_max,wind_gusts_10m_max"
               "&timezone=auto&forecast_days=7&wind_speed_unit=kmh", lat, lon);
}

std::string airPath(double lat, double lon) {
    return fmt("/v1/air-quality?latitude=%.2f&longitude=%.2f&current=us_aqi,european_aqi,pm2_5,pm10&hourly=us_aqi,pm2_5&forecast_days=1&timezone=auto", lat, lon);
}

// "2026-10-18T07:00" or "2026-10-18" (local time of the forecast location) -> unix
static bool validIsoDateTime(int y, int m, int d, int hh, int mm, int ss) {
    return y >= 1970 && y <= 2200 && m >= 1 && m <= 12 && d >= 1 && d <= 31 &&
           hh >= 0 && hh <= 23 && mm >= 0 && mm <= 59 && ss >= 0 && ss <= 59;
}

static bool parseLocalIso(const std::string& s, int utcOffset, double& out) {
    int y = 0, m = 0, d = 0, hh = 0, mm = 0;
    if (sscanf(s.c_str(), "%d-%d-%dT%d:%d", &y, &m, &d, &hh, &mm) < 3) return false;
    if (!validIsoDateTime(y, m, d, hh, mm, 0)) return false;
    out = astro::daysFromCivil(y, m, d) * 86400.0 + hh * 3600.0 + mm * 60.0 - utcOffset;
    return true;
}

static const mj::Value* sect(const mj::Value& root, const char* k) { const mj::Value* v = root.get(k); return (v && v->type == mj::Value::Object) ? v : nullptr; }
static double numAt(const mj::Value* arr, size_t i, double def = 0) {
    if (!arr || arr->type != mj::Value::Array || i >= arr->arr.size()) return def;
    return arr->arr[i].type == mj::Value::Number ? arr->arr[i].num : def;
}
static const mj::Value* arr(const mj::Value* o, const char* k) { if (!o) return nullptr; const mj::Value* v = o->get(k); return (v && v->type == mj::Value::Array) ? v : nullptr; }
static double num(const mj::Value* o, const char* k, double def = 0) { if (!o) return def; const mj::Value* v = o->get(k); return v ? v->number(def) : def; }
static bool reqNum(const mj::Value* o, const char* k, double& out) {
    if (!o) return false;
    const mj::Value* v = o->get(k);
    if (!v || v->type != mj::Value::Number || !std::isfinite(v->num)) return false;
    out = v->num;
    return true;
}

bool parseForecast(const std::string& json, Data& out, std::string& err) {
    mj::Value root;
    if (!mj::parse(json, root, err)) return false;
    if (root.type != mj::Value::Object) { err = "unexpected response"; return false; }
    if (const mj::Value* e = root.get("error")) { if (e->type == mj::Value::Bool && e->b) { const mj::Value* r = root.get("reason"); err = r ? r->str : "service error"; return false; } }
    Data d;
    double uoVal = 0.0;
    d.hasUtcOffset = reqNum(&root, "utc_offset_seconds", uoVal);
    d.utcOffset = (int)std::lround(uoVal);
    if (const mj::Value* tz = root.get("timezone")) d.timezone = tz->str;

    if (const mj::Value* c = sect(root, "current")) {
        const mj::Value* t = c->get("time");
        if (t && t->type == mj::Value::String) parseLocalIso(t->str, d.utcOffset, d.cur.time);
        double codeD = 0;
        if (!reqNum(c, "temperature_2m", d.cur.temp) || !reqNum(c, "weather_code", codeD)) {
            err = "incomplete current conditions";
            return false;
        }
        d.cur.code = (int)std::lround(codeD);
        d.cur.feels = num(c, "apparent_temperature", d.cur.temp);
        d.cur.hasHumidity = reqNum(c, "relative_humidity_2m", d.cur.humidity);
        d.cur.pressure = num(c, "pressure_msl", 0);
        d.cur.hasWind = reqNum(c, "wind_speed_10m", d.cur.wind);
        d.cur.windDir = num(c, "wind_direction_10m", 0);
        d.cur.hasGust = reqNum(c, "wind_gusts_10m", d.cur.gust);
        d.cur.hasCloud = reqNum(c, "cloud_cover", d.cur.cloud);
        d.cur.precip = num(c, "precipitation", 0);
        d.cur.isDay = num(c, "is_day", 1) > 0.5;
    } else { err = "no current conditions"; return false; }

    if (const mj::Value* h = sect(root, "hourly")) {
        const mj::Value* times = arr(h, "time");
        if (times) {
            const mj::Value *temp = arr(h, "temperature_2m"), *feels = arr(h, "apparent_temperature"), *hum = arr(h, "relative_humidity_2m"),
                *pp = arr(h, "precipitation_probability"), *pr = arr(h, "precipitation"), *wc = arr(h, "weather_code"),
                *cc = arr(h, "cloud_cover"), *ws = arr(h, "wind_speed_10m"), *uv = arr(h, "uv_index"), *isd = arr(h, "is_day");
            for (size_t i = 0; i < times->arr.size(); ++i) {
                Hour x;
                if (times->arr[i].type != mj::Value::String || !parseLocalIso(times->arr[i].str, d.utcOffset, x.t)) continue;
                x.temp = numAt(temp, i); x.feels = numAt(feels, i, x.temp); x.humidity = numAt(hum, i);
                x.precipProb = numAt(pp, i); x.precip = numAt(pr, i); x.code = (int)numAt(wc, i);
                x.cloud = numAt(cc, i); x.wind = numAt(ws, i); x.uv = numAt(uv, i); x.isDay = numAt(isd, i, 1) > 0.5;
                d.hourly.push_back(x);
            }
        }
    }
    if (const mj::Value* dy = sect(root, "daily")) {
        const mj::Value* times = arr(dy, "time");
        if (times) {
            const mj::Value *wc = arr(dy, "weather_code"), *tx = arr(dy, "temperature_2m_max"), *tn = arr(dy, "temperature_2m_min"),
                *fx = arr(dy, "apparent_temperature_max"), *fn = arr(dy, "apparent_temperature_min"), *sr = arr(dy, "sunrise"), *ss = arr(dy, "sunset"),
                *uv = arr(dy, "uv_index_max"), *ps = arr(dy, "precipitation_sum"), *pp = arr(dy, "precipitation_probability_max"),
                *wm = arr(dy, "wind_speed_10m_max"), *gm = arr(dy, "wind_gusts_10m_max");
            for (size_t i = 0; i < times->arr.size(); ++i) {
                Day x;
                if (times->arr[i].type != mj::Value::String || !parseLocalIso(times->arr[i].str, d.utcOffset, x.t0)) continue;
                x.code = (int)numAt(wc, i); x.tmax = numAt(tx, i); x.tmin = numAt(tn, i);
                x.feelsMax = numAt(fx, i, x.tmax); x.feelsMin = numAt(fn, i, x.tmin);
                x.uvMax = numAt(uv, i); x.precipSum = numAt(ps, i); x.precipProbMax = numAt(pp, i);
                x.windMax = numAt(wm, i); x.gustMax = numAt(gm, i);
                if (sr && i < sr->arr.size() && sr->arr[i].type == mj::Value::String) parseLocalIso(sr->arr[i].str, d.utcOffset, x.sunrise);
                if (ss && i < ss->arr.size() && ss->arr[i].type == mj::Value::String) parseLocalIso(ss->arr[i].str, d.utcOffset, x.sunset);
                d.daily.push_back(x);
            }
        }
    }
    d.valid = true;
    out.valid = true; out.utcOffset = d.utcOffset; out.hasUtcOffset = d.hasUtcOffset; out.timezone = d.timezone; out.cur = d.cur; out.hourly = d.hourly; out.daily = d.daily;
    out.hasUv = true; out.source = providerName(PV_OPENMETEO);
    return true;
}

// Revised 2024 US EPA PM2.5 (ug/m3) breakpoint table (40 CFR Part 58 Appendix G):
// Good 0.0-9.0 (0-50), Moderate 9.1-35.4 (51-100), USG 35.5-55.4 (101-150),
// Unhealthy 55.5-125.4 (151-200), Very Unhealthy 125.5-225.4 (201-300), Hazardous 225.5-325.4+ (301-500).
static double pm25ToUsAqi(double c) {
    if (c < 0.0) return 0.0;
    struct Bp { double clo, chi, ilo, ihi; };
    static constexpr Bp kBp[] = {
        {0.0, 9.0, 0, 50}, {9.1, 35.4, 51, 100}, {35.5, 55.4, 101, 150},
        {55.5, 125.4, 151, 200}, {125.5, 225.4, 201, 300}, {225.5, 325.4, 301, 500}
    };
    const double t = std::floor(c * 10.0) / 10.0;
    for (const Bp& b : kBp) {
        if (t <= b.chi) return b.ilo + (b.ihi - b.ilo) * (std::max(b.clo, t) - b.clo) / std::max(0.1, b.chi - b.clo);
    }
    return 500.0;
}

static double haversineKm(double lat1, double lon1, double lat2, double lon2) {
    const double dLat = astro::rad(lat2 - lat1), dLon = astro::rad(lon2 - lon1);
    const double a = std::sin(dLat * 0.5) * std::sin(dLat * 0.5) +
                     std::cos(astro::rad(lat1)) * std::cos(astro::rad(lat2)) * std::sin(dLon * 0.5) * std::sin(dLon * 0.5);
    return 6371.0 * 2.0 * std::atan2(std::sqrt(std::max(0.0, a)), std::sqrt(std::max(0.0, 1.0 - a)));
}

static double epaIndexToUsAqi(int idx) {
    switch (idx) {
        case 1: return 25.0; case 2: return 75.0; case 3: return 125.0;
        case 4: return 175.0; case 5: return 250.0; case 6: return 350.0;
        default: return 50.0;
    }
}

static double owmIndexToUsAqi(int idx) {
    switch (idx) {
        case 1: return 25.0; case 2: return 65.0; case 3: return 115.0;
        case 4: return 175.0; case 5: return 260.0;
        default: return 50.0;
    }
}

static AqiScale parseAqiScaleTag(const std::string& tag) {
    if (tag == "us") return AqiScale::Us;
    if (tag == "eu") return AqiScale::Eu;
    if (tag == "pm25_est") return AqiScale::Pm25Est;
    return AqiScale::None;
}

static const char* aqiScaleTag(AqiScale s) {
    switch (s) {
        case AqiScale::Us: return "us";
        case AqiScale::Eu: return "eu";
        case AqiScale::Pm25Est: return "pm25_est";
        default: return "none";
    }
}

bool parseAir(const std::string& json, Data& out, std::string& err, double lat, double lon, double nowOverride) {
    mj::Value root;
    if (!mj::parse(json, root, err)) return false;
    if (root.type != mj::Value::Object) { err = "unexpected air response"; return false; }

    constexpr double kMaxObsAgeSec = 6.0 * 3600.0;
    const double refNow = (nowOverride > 0.0) ? nowOverride : (double)time(nullptr);
    auto obsAgeOk = [&](double obsTs) -> bool {
        if (obsTs <= 0.0 || refNow <= 0.0) return true;
        return std::fabs(refNow - obsTs) <= kMaxObsAgeSec;
    };

    // 1. Normalized Meridian air cache snapshot (v2 carries explicit scale + observation time; v1 carried us_aqi only)
    if (const mj::Value* mk = root.get("meridian_air")) {
        if (mk->type == mj::Value::Number && mk->num > 0.5) {
            double aq = 0.0;
            AqiScale sc = AqiScale::None;
            if (const mj::Value* sv = root.get("scale"); sv && sv->type == mj::Value::String) {
                sc = parseAqiScaleTag(sv->str);
                if (sc == AqiScale::None || !reqNum(&root, "aqi", aq)) { err = "invalid cached AQI"; return false; }
            } else if (reqNum(&root, "us_aqi", aq)) {
                sc = AqiScale::Us;
            } else {
                err = "no cached AQI"; return false;
            }
            double obsTs = 0.0;
            if (reqNum(&root, "obs_time", obsTs) && obsTs > 0.0) {
                if (!obsAgeOk(obsTs)) { err = "cached air observation too old"; return false; }
                out.airObsTime = obsTs;
            } else {
                out.airObsTime = 0.0;
            }
            out.aqi = aq;
            out.aqiScale = sc;
            out.pm25 = num(&root, "pm2_5", 0.0);
            out.pm10 = num(&root, "pm10", 0.0);
            if (const mj::Value* src = root.get("source"); src && src->type == mj::Value::String && !src->str.empty())
                out.airSource = src->str;
            else if (out.airSource.empty()) out.airSource = "Open-Meteo";
            out.hasAir = true;
            return true;
        }
    }

    // 2. WeatherAPI.com ("current.air_quality")
    if (const mj::Value* cur = sect(root, "current")) {
        if (const mj::Value* aqObj = sect(*cur, "air_quality")) {
            double obsTs = 0.0;
            if (reqNum(cur, "last_updated_epoch", obsTs) && obsTs > 0.0 && !obsAgeOk(obsTs)) {
                err = "WeatherAPI air reading too old"; return false;
            }
            double p25 = 0.0, epa = 0.0;
            const bool hasP25 = reqNum(aqObj, "pm2_5", p25);
            out.pm25 = hasP25 ? p25 : 0.0;
            out.pm10 = num(aqObj, "pm10", 0.0);
            if (hasP25) { out.aqi = pm25ToUsAqi(p25); out.aqiScale = AqiScale::Pm25Est; }
            else if (reqNum(aqObj, "us-epa-index", epa)) { out.aqi = epaIndexToUsAqi((int)std::lround(epa)); out.aqiScale = AqiScale::Us; }
            else { err = "no WeatherAPI AQI"; return false; }
            out.airObsTime = obsTs > 0.0 ? obsTs : refNow;
            out.airSource = "WeatherAPI.com";
            out.hasAir = true;
            return true;
        }
    }

    // 3. Open-Meteo Air Quality ("current" and/or "hourly" fallback)
    if (const mj::Value* c = sect(root, "current")) {
        const int uo = (int)num(&root, "utc_offset_seconds", out.utcOffset);
        double obsTs = 0.0;
        if (const mj::Value* ct = c->get("time"); ct && ct->type == mj::Value::String) {
            parseLocalIso(ct->str, uo, obsTs);
        }
        if (obsTs <= 0.0 || obsAgeOk(obsTs)) {
            double aq = 0.0;
            const bool hasPm25 = reqNum(c, "pm2_5", out.pm25);
            out.pm10 = num(c, "pm10");
            if (reqNum(c, "us_aqi", aq)) {
                out.aqi = aq; out.aqiScale = AqiScale::Us; out.airObsTime = obsTs > 0.0 ? obsTs : refNow;
                out.airSource = "Open-Meteo"; out.hasAir = true; return true;
            }
            if (reqNum(c, "european_aqi", aq)) {
                out.aqi = aq; out.aqiScale = AqiScale::Eu; out.airObsTime = obsTs > 0.0 ? obsTs : refNow;
                out.airSource = "Open-Meteo"; out.hasAir = true; return true;
            }
            if (hasPm25) {
                out.aqi = pm25ToUsAqi(out.pm25); out.aqiScale = AqiScale::Pm25Est; out.airObsTime = obsTs > 0.0 ? obsTs : refNow;
                out.airSource = "Open-Meteo"; out.hasAir = true; return true;
            }
        }
    }
    if (const mj::Value* h = sect(root, "hourly")) {
        const mj::Value* times = arr(h, "time");
        const mj::Value* ua = arr(h, "us_aqi");
        const mj::Value* ea = arr(h, "european_aqi");
        const mj::Value* p2 = arr(h, "pm2_5");
        if (times && (ua || ea || p2)) {
            const int uo = (int)num(&root, "utc_offset_seconds", out.utcOffset);
            double bestDist = 1e18, bestAqi = -1.0, bestPm25 = 0.0, bestTs = 0.0;
            AqiScale bestScale = AqiScale::None;
            for (size_t i = 0; i < times->arr.size(); ++i) {
                double ts = 0.0;
                if (times->arr[i].type != mj::Value::String || !parseLocalIso(times->arr[i].str, uo, ts)) continue;
                const double dist = std::fabs(ts - refNow);
                if (refNow > 0.0 && dist > kMaxObsAgeSec) continue;    // reject stale historical samples (e.g., 2020 in 2026)
                if (ua && i < ua->arr.size() && ua->arr[i].type == mj::Value::Number && std::isfinite(ua->arr[i].num)) {
                    if (dist < bestDist) {
                        bestDist = dist; bestAqi = ua->arr[i].num; bestScale = AqiScale::Us; bestTs = ts;
                        bestPm25 = (p2 && i < p2->arr.size() && p2->arr[i].type == mj::Value::Number) ? p2->arr[i].num : 0.0;
                    }
                } else if (ea && i < ea->arr.size() && ea->arr[i].type == mj::Value::Number && std::isfinite(ea->arr[i].num)) {
                    if (dist < bestDist) {
                        bestDist = dist; bestAqi = ea->arr[i].num; bestScale = AqiScale::Eu; bestTs = ts;
                        bestPm25 = (p2 && i < p2->arr.size() && p2->arr[i].type == mj::Value::Number) ? p2->arr[i].num : 0.0;
                    }
                } else if (p2 && i < p2->arr.size() && p2->arr[i].type == mj::Value::Number && std::isfinite(p2->arr[i].num)) {
                    if (dist < bestDist) {
                        bestDist = dist; bestPm25 = p2->arr[i].num; bestAqi = pm25ToUsAqi(bestPm25); bestScale = AqiScale::Pm25Est; bestTs = ts;
                    }
                }
            }
            if (bestAqi >= 0.0 && bestScale != AqiScale::None) {
                out.aqi = bestAqi; out.aqiScale = bestScale; out.pm25 = bestPm25; out.airObsTime = bestTs;
                out.airSource = "Open-Meteo"; out.hasAir = true; return true;
            }
            err = "no fresh Open-Meteo air quality sample";
            return false;
        }
    }

    // 4. OpenWeatherMap Air Pollution ("list[0].components")
    if (const mj::Value* lst = arr(&root, "list"); lst && !lst->arr.empty() && lst->arr[0].type == mj::Value::Object) {
        const mj::Value& it = lst->arr[0];
        double obsTs = 0.0;
        if (reqNum(&it, "dt", obsTs) && obsTs > 0.0 && !obsAgeOk(obsTs)) {
            err = "OpenWeatherMap air reading too old"; return false;
        }
        const mj::Value* comp = sect(it, "components");
        const mj::Value* mn = sect(it, "main");
        double p25 = 0.0, owmIdx = 0.0;
        const bool hasP25 = comp && reqNum(comp, "pm2_5", p25);
        if (hasP25 || (mn && reqNum(mn, "aqi", owmIdx))) {
            out.pm25 = hasP25 ? p25 : 0.0;
            out.pm10 = comp ? num(comp, "pm10", 0.0) : 0.0;
            out.aqi = hasP25 ? pm25ToUsAqi(p25) : owmIndexToUsAqi((int)std::lround(owmIdx));
            out.aqiScale = hasP25 ? AqiScale::Pm25Est : AqiScale::Us;
            out.airObsTime = obsTs > 0.0 ? obsTs : refNow;
            out.airSource = "OpenWeatherMap";
            out.hasAir = true;
            return true;
        }
    }

    // 5. WAQI / AQICN Geo Feed ("status": "ok", "data": { "aqi": ..., "city": { "geo": [lat, lon] } })
    if (const mj::Value* stv = root.get("status"); stv && stv->type == mj::Value::String && stv->str == "ok") {
        if (const mj::Value* dat = sect(root, "data")) {
            // Guard against WAQI demo returning a distant station (e.g. Shanghai) for another region
            if (lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0) {
                const mj::Value* city = sect(*dat, "city");
                const mj::Value* geo = arr(city, "geo");
                if (!geo || geo->arr.size() < 2 || geo->arr[0].type != mj::Value::Number || geo->arr[1].type != mj::Value::Number) {
                    err = "WAQI station missing coordinates";
                    return false;
                }
                const double distKm = haversineKm(lat, lon, geo->arr[0].num, geo->arr[1].num);
                if (distKm > 500.0) {
                    err = fmt("WAQI station too far (%.0f km)", distKm);
                    return false;
                }
            }
            double obsTs = 0.0;
            if (const mj::Value* tm = sect(*dat, "time")) {
                if (reqNum(tm, "v", obsTs) && obsTs > 0.0 && !obsAgeOk(obsTs)) {
                    err = "WAQI reading too old"; return false;
                }
            }
            double aq = 0.0;
            if (!reqNum(dat, "aqi", aq) || aq < 0.0) { err = "no WAQI AQI value"; return false; }
            out.aqi = aq;
            out.aqiScale = AqiScale::Us;
            out.airObsTime = obsTs > 0.0 ? obsTs : refNow;
            if (const mj::Value* iaqi = sect(*dat, "iaqi")) {
                if (const mj::Value* p25 = sect(*iaqi, "pm25")) out.pm25 = num(p25, "v", 0.0);
                if (const mj::Value* p10 = sect(*iaqi, "pm10")) out.pm10 = num(p10, "v", 0.0);
            }
            out.airSource = "WAQI";
            out.hasAir = true;
            return true;
        }
    }

    err = "no air quality data";
    return false;
}

static std::string jsonEsc(const std::string& s) {
    std::string o; o.reserve(s.size());
    for (char c : s) {
        if (c == '"' || c == '\\') { o.push_back('\\'); o.push_back(c); }
        else if ((unsigned char)c >= 0x20) o.push_back(c);
    }
    return o;
}

std::string formatAirJson(const Data& d) {
    const AqiScale sc = (d.aqiScale == AqiScale::None && d.hasAir) ? AqiScale::Us : d.aqiScale;
    const std::string src = jsonEsc(d.airSource.empty() ? "Open-Meteo" : d.airSource);
    const double obsTs = d.airObsTime > 0.0 ? d.airObsTime : d.fetched;
    if (sc == AqiScale::Us) {
        return fmt("{\"meridian_air\":2,\"source\":\"%s\",\"scale\":\"us\",\"aqi\":%.1f,\"us_aqi\":%.1f,\"obs_time\":%.0f,\"pm2_5\":%.2f,\"pm10\":%.2f}",
                   src.c_str(), d.aqi, d.aqi, obsTs, d.pm25, d.pm10);
    }
    // Never mislabel European AQI or PM2.5 estimate under `us_aqi` on disk!
    return fmt("{\"meridian_air\":2,\"source\":\"%s\",\"scale\":\"%s\",\"aqi\":%.1f,\"obs_time\":%.0f,\"pm2_5\":%.2f,\"pm10\":%.2f}",
               src.c_str(), aqiScaleTag(sc), d.aqi, obsTs, d.pm25, d.pm10);
}

std::string formatAirCache(double lat, double lon, double fetched, const std::string& json) {
    return fmt("%.4f,%.4f %.0f\n", lat, lon, fetched) + json;
}

bool parseAirCache(const std::string& raw, double lat, double lon, double now, double maxAgeSec, Data& out, std::string& err) {
    const size_t nl = raw.find('\n');
    if (nl == std::string::npos) { err = "air cache missing header"; return false; }
    double la = 0, lo = 0, ts = 0;
    if (sscanf(raw.c_str(), "%lf,%lf %lf", &la, &lo, &ts) != 3) { err = "air cache bad header"; return false; }
    if (std::fabs(la - lat) > 0.01 || std::fabs(lo - lon) > 0.01) { err = "air cache place mismatch"; return false; }
    if (maxAgeSec > 0 && (ts <= 0 || std::fabs(now - ts) > maxAgeSec)) { err = "air cache stale"; return false; }
    if (!parseAir(raw.substr(nl + 1), out, err, lat, lon, now)) return false;
    if (maxAgeSec > 0 && out.airObsTime > 0.0 && now > 0.0 && std::fabs(now - out.airObsTime) > maxAgeSec) {
        out.hasAir = false;
        out.aqiScale = AqiScale::None;
        err = "cached air observation stale";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- summary text
static bool snowCode(int c) { return (c >= 71 && c <= 77) || c == 85 || c == 86; }
static bool wetCode(int c) { return (c >= 51 && c <= 67) || (c >= 80 && c <= 82) || c >= 95 || snowCode(c); }
static bool isWet(const Hour& h) { return h.precipProb >= 50.0 || h.precip >= 0.3 || (wetCode(h.code) && h.precipProb >= 30.0); }

std::string summarize(const Data& d, double now, int tzOff, bool h24, bool useF, int (*tzAt)(double)) {
    if (!d.valid) return "";
    auto clock = [&](double t) { return util::fmtClock(t, tzAt ? tzAt(t) : tzOff, h24); };
    auto temp = [&](double c) { return fmt("%d\xC2\xB0", (int)std::lround(toUnit(c, useF))); };
    std::vector<const Hour*> win;
    for (const auto& h : d.hourly) if (h.t >= now - 1800.0 && h.t <= now + 15 * 3600.0) win.push_back(&h);
    if (win.empty()) return "";
    std::string s;
    const int hrs = std::max(1, (int)std::lround((win.back()->t - win.front()->t) / 3600.0) + 1);
    // wet spans
    std::vector<std::pair<double, double>> spans;
    bool in = false; double a = 0;
    int snowHits = 0, wetHits = 0;
    for (const Hour* h : win) {
        bool w = isWet(*h);
        if (w) { ++wetHits; if (snowCode(h->code)) ++snowHits; }
        if (w && !in) { in = true; a = h->t; }
        if (!w && in) { in = false; spans.push_back({a, h->t}); }
    }
    if (in) spans.push_back({a, win.back()->t + 3600.0});
    const bool snowNow = snowCode(d.cur.code);
    const bool snowPred = (wetHits > 0 && snowHits * 2 >= wetHits) || (wetHits == 0 && snowNow);
    const char* precipWord = snowPred ? "Snow" : "Rain";
    const bool rainingNow = wetCode(d.cur.code) || d.cur.precip >= 0.1;
    if (spans.empty()) {
        if (rainingNow) s = snowNow ? "Light snow easing soon." : "Light rain easing soon.";
        else s = fmt("Dry for the next %d %s.", hrs, hrs == 1 ? "hour" : "hours");
    } else if (spans.front().first <= now + 3600.0) {
        double end = spans.front().second;
        if (end >= win.back()->t + 3500.0) s = fmt("%s likely to continue through the next %d %s.", precipWord, hrs, hrs == 1 ? "hour" : "hours");
        else s = std::string(precipWord) + " likely until about " + clock(end) + ".";
        if (spans.size() > 1) s += " More from " + clock(spans[1].first) + ".";
    } else {
        s = std::string(precipWord) + " expected from about " + clock(spans.front().first) + (spans.front().second - spans.front().first >= 3 * 3600.0 ? "" : " to " + clock(spans.front().second)) + ".";
    }
    // temperature trend over the window
    const Hour *hi = win[0], *lo = win[0];
    for (const Hour* h : win) { if (h->temp > hi->temp) hi = h; if (h->temp < lo->temp) lo = h; }
    const double w0 = win[0]->t;
    const bool hiEarly = hi->t - w0 < 2 * 3600.0, loEarly = lo->t - w0 < 2 * 3600.0;
    if (hi->temp - lo->temp < 3.0) s += " Steady near " + temp(win[0]->temp) + ".";
    else if (hiEarly && !loEarly) s += " Cooling to " + temp(lo->temp) + " by " + clock(lo->t) + ".";
    else if (loEarly && !hiEarly) s += " Warming to " + temp(hi->temp) + " by " + clock(hi->t) + ".";
    else if (hi->t < lo->t) s += " Peaking near " + temp(hi->temp) + " around " + clock(hi->t) + ", then cooling to " + temp(lo->temp) + ".";
    else s += " Cooling to " + temp(lo->temp) + " by " + clock(lo->t) + ", then warming to " + temp(hi->temp) + ".";
    // Smart comfort & frost cues (at most one concise note when conditions genuinely stand out)
    {
        double maxUv6 = 0.0;
        for (const Hour* h : win) if (h->t <= now + 6 * 3600.0 && h->isDay) maxUv6 = std::max(maxUv6, h->uv);
        if (lo->temp <= 0.0) {
            if (d.cur.temp <= 0.0 && hi->temp <= 0.5) s += " Staying below freezing.";
            else s += " Freezing tonight, frost possible.";
        } else if (d.cur.temp <= 5.0 && (d.cur.temp - d.cur.feels) >= 4.0) {
            s += " Feels like " + temp(d.cur.feels) + " in the wind.";
        } else if (d.cur.feels >= 36.0) {
            s += " High heat index.";
        } else if (d.hasUv && maxUv6 >= 8.0) {
            s += " High UV ahead.";
        }
    }
    return s;
}

// ---------------------------------------------------------------- other providers
const char* providerName(int id) {
    static const char* n[] = {
        "Open-Meteo", "MET Norway", "wttr.in", "7Timer",
        "Bright Sky", "NWS / NOAA", "WeatherAPI.com", "OpenWeatherMap"
    };
    return id >= 0 && id < PV_COUNT ? n[id] : "";
}
const char* providerId(int id) {
    static const char* n[] = {
        "openmeteo", "metno", "wttr", "7timer",
        "brightsky", "nws", "weatherapi", "openweather"
    };
    return id >= 0 && id < PV_COUNT ? n[id] : "";
}
bool providerNeedsKey(int id) {
    return id == PV_WEATHERAPI || id == PV_OPENWEATHER;
}

// Parse ISO-8601 timestamp with Z or +/-HH:MM offset into unix UTC seconds (and optional local UTC offset seconds).
static bool parseIso8601(const std::string& s, double& out, int* tzOut = nullptr) {
    int y = 0, m = 0, d = 0, hh = 0, mm = 0, ss = 0;
    if (sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &y, &m, &d, &hh, &mm, &ss) < 5) return false;
    if (!validIsoDateTime(y, m, d, hh, mm, ss)) return false;
    int tzSec = 0;
    const size_t tpos = s.find('T');
    if (tpos != std::string::npos) {
        size_t p = s.find_first_of("+-Z", tpos + 1);
        if (p != std::string::npos && (s[p] == '+' || s[p] == '-')) {
            int oh = 0, om = 0;
            const int sg = (s[p] == '-') ? -1 : 1;
            if (sscanf(s.c_str() + p + 1, "%2d:%2d", &oh, &om) == 2 || sscanf(s.c_str() + p + 1, "%2d%2d", &oh, &om) >= 1)
                tzSec = sg * (oh * 3600 + om * 60);
        }
    }
    out = astro::daysFromCivil(y, m, d) * 86400.0 + hh * 3600.0 + mm * 60.0 + ss - tzSec;
    if (tzOut) *tzOut = tzSec;
    return true;
}

static double compassDeg(const std::string& dir) {
    static const char* kD[16] = {"N","NNE","NE","ENE","E","ESE","SE","SSE","S","SSW","SW","WSW","W","WNW","NW","NNW"};
    const std::string u = util::upper(dir);
    for (int i = 0; i < 16; ++i) if (u == kD[i]) return i * 22.5;
    return 0.0;
}

static bool parseUtcIso(const std::string& s, double& out) {
    int y = 0, m = 0, d = 0, hh = 0, mm = 0, ss = 0;
    if (sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &y, &m, &d, &hh, &mm, &ss) < 5) return false;
    if (!validIsoDateTime(y, m, d, hh, mm, ss)) return false;
    out = astro::daysFromCivil(y, m, d) * 86400.0 + hh * 3600.0 + mm * 60.0 + ss;
    return true;
}
static std::string strAt(const mj::Value* o, const char* k) { if (!o) return ""; const mj::Value* v = o->get(k); return (v && v->type == mj::Value::String) ? v->str : ""; }
// number that may be stored as a JSON number or as a string ("27")
static double numS(const mj::Value* o, const char* k, double def = 0) {
    if (!o) return def;
    const mj::Value* v = o->get(k);
    if (!v) return def;
    if (v->type == mj::Value::Number) return v->num;
    if (v->type == mj::Value::String) { char* e = nullptr; double x = strtod(v->str.c_str(), &e); return e != v->str.c_str() ? x : def; }
    return def;
}

static bool wetCode(int c);
static double severity(int c) {
    if (c >= 95) return 9;
    if (c >= 85) return 7.5;
    if (c >= 80) return 6.5;
    if (c >= 71) return 8;
    if (c >= 61) return 6;
    if (c >= 51) return 4;
    if (c >= 45) return 3;
    if (c == 3) return 2;
    if (c == 2) return 1.5;
    if (c == 1) return 1;
    return 0;
}
static double apparentC(double t, double rh, double windKmh) {         // Australian Bureau of Meteorology apparent temperature
    double e = rh / 100.0 * 6.105 * std::exp(17.27 * t / (237.7 + t));
    return t + 0.33 * e - 0.70 * (windKmh / 3.6) - 4.0;
}
static bool sunUp(double t, double lat, double lon) { astro::Geo g; g.lat = lat; g.lon = lon; return astro::sunInfo(t, g).hor.alt > -0.833; }

// Provider samples are often 3- or 6-hourly: put them on an hourly grid so every chart can assume one sample per hour.
static void resampleHourly(const std::vector<Hour>& in, std::vector<Hour>& out, int utcOffset, double lat, double lon) {
    out.clear();
    if (in.size() < 2) { out = in; return; }
    double t0 = std::ceil((in.front().t + utcOffset) / 3600.0) * 3600.0 - utcOffset;   // local hour boundaries (matters for :30 zones)
    const double tEnd = std::min(in.back().t + 1.0, t0 + 336 * 3600.0);                // cap at 14 days so corrupt timestamps can never hang/OOM
    size_t j = 0;
    for (double t = t0; t <= tEnd; t += 3600.0) {
        while (j + 2 < in.size() && in[j + 1].t <= t) ++j;
        const Hour &a = in[j], &b = in[j + 1];
        double f = b.t > a.t ? std::min(1.0, std::max(0.0, (t - a.t) / (b.t - a.t))) : 0.0;
        auto L = [&](double x, double y) { return x + (y - x) * f; };
        Hour h = (t >= b.t - 1.0) ? b : a;
        h.t = t;
        h.temp = L(a.temp, b.temp); h.feels = L(a.feels, b.feels); h.humidity = L(a.humidity, b.humidity);
        h.cloud = L(a.cloud, b.cloud); h.wind = L(a.wind, b.wind); h.uv = L(a.uv, b.uv);
        h.isDay = sunUp(t, lat, lon);
        out.push_back(h);
    }
}

// Daily summary from hourly samples (for providers that only give a time series).
static void buildDaily(Data& d, double lat, double lon) {
    d.daily.clear();
    astro::Geo g; g.lat = lat; g.lon = lon;
    struct Acc { Day day; int n = 0; double bestSev = -1e9; bool first = true; };
    std::map<long long, Acc> days;
    for (const Hour& h : d.hourly) {
        long long di = (long long)std::floor((h.t + d.utcOffset) / 86400.0);
        Acc& a = days[di];
        if (a.first) { a.first = false; a.day.t0 = di * 86400.0 - d.utcOffset; a.day.tmax = a.day.tmin = h.temp; a.day.feelsMax = a.day.feelsMin = h.feels; }
        a.n++;
        a.day.tmax = std::max(a.day.tmax, h.temp); a.day.tmin = std::min(a.day.tmin, h.temp);
        a.day.feelsMax = std::max(a.day.feelsMax, h.feels); a.day.feelsMin = std::min(a.day.feelsMin, h.feels);
        a.day.precipSum += h.precip; a.day.precipProbMax = std::max(a.day.precipProbMax, h.precipProb);
        a.day.windMax = std::max(a.day.windMax, h.wind); a.day.uvMax = std::max(a.day.uvMax, h.uv);
        int hr = ((int)std::floor((h.t + d.utcOffset) / 3600.0) % 24 + 24) % 24;
        double sev = severity(h.code) + (hr >= 7 && hr <= 20 ? 0.0 : -20.0);
        if (sev > a.bestSev) { a.bestSev = sev; a.day.code = h.code; }
    }
    bool firstDay = true;
    for (auto& kv : days) {
        Acc& a = kv.second;
        if (a.n < 8 && !firstDay) continue;
        firstDay = false;
        a.day.gustMax = a.day.windMax * 1.4;
        astro::RiseSet rs = astro::sunRiseSet(a.day.t0, a.day.t0 + 86400.0, g);
        if (rs.hasRise) a.day.sunrise = rs.rise;
        if (rs.hasSet) a.day.sunset = rs.set;
        d.daily.push_back(a.day);
        if (d.daily.size() >= 8) break;
    }
}

static void finishSeries(Data& d, double lat, double lon, std::vector<Hour>& raw, const Current& cur) {
    resampleHourly(raw, d.hourly, d.utcOffset, lat, lon);
    d.cur = cur;
    d.hasUv = false;
    buildDaily(d, lat, lon);
    d.valid = !d.hourly.empty() && !d.daily.empty();
}

static int metCode(const std::string& sym, bool& isDay) {
    isDay = true;
    std::string s = sym;
    size_t u = s.rfind('_');
    if (u != std::string::npos) {
        std::string suf = s.substr(u + 1);
        if (suf == "night") isDay = false;
        if (suf == "day" || suf == "night" || suf == "polartwilight") s = s.substr(0, u);
    }
    auto has = [&](const char* k) { return s.find(k) != std::string::npos; };
    const bool light = has("light"), heavy = has("heavy"), showers = has("showers");
    if (has("thunder")) return 95;
    if (has("sleet")) return showers ? 85 : 67;
    if (has("snow")) return showers ? (heavy ? 86 : 85) : (light ? 71 : heavy ? 75 : 73);
    if (has("rain")) return showers ? (light ? 80 : heavy ? 82 : 81) : (light ? 61 : heavy ? 65 : 63);
    if (has("fog")) return 45;
    if (s == "partlycloudy") return 2;
    if (s == "cloudy") return 3;
    if (s == "fair") return 1;
    if (s == "clearsky") return 0;
    return 3;
}

static bool parseMetNo(const std::string& json, double lat, double lon, Data& out, std::string& err) {
    mj::Value root;
    if (!mj::parse(json, root, err)) return false;
    const mj::Value* props = sect(root, "properties");
    const mj::Value* ts = props ? arr(props, "timeseries") : nullptr;
    if (!ts || ts->arr.empty()) { err = "no forecast in response"; return false; }
    std::vector<Hour> raw; Current cur; bool haveCur = false;
    for (size_t idx = 0; idx < ts->arr.size(); ++idx) {
        const mj::Value& e = ts->arr[idx];
        if (e.type != mj::Value::Object) continue;
        double t; if (!parseUtcIso(strAt(&e, "time"), t)) continue;
        const mj::Value* data = sect(e, "data"); if (!data) continue;
        const mj::Value* inst = sect(*data, "instant");
        const mj::Value* det = inst ? sect(*inst, "details") : nullptr; if (!det) continue;
        double nextGapH = 1.0;
        if (idx + 1 < ts->arr.size() && ts->arr[idx + 1].type == mj::Value::Object) {
            double tNext; if (parseUtcIso(strAt(&ts->arr[idx + 1], "time"), tNext) && tNext > t) nextGapH = (tNext - t) / 3600.0;
        }
        const mj::Value* nx = nullptr; double span = 1;
        // At the 1h->6h transition, prefer next_6_hours so a 1-hour shower isn't multiplied 6x across the 6-hour gap.
        if (nextGapH >= 5.5) {
            if ((nx = sect(*data, "next_6_hours"))) span = 6;
            else if ((nx = sect(*data, "next_12_hours"))) span = 12;
            else if ((nx = sect(*data, "next_1_hours"))) span = 1;
        } else {
            if ((nx = sect(*data, "next_1_hours"))) span = 1;
            else if ((nx = sect(*data, "next_6_hours"))) span = 6;
            else if ((nx = sect(*data, "next_12_hours"))) span = 12;
        }
        Hour h; h.t = t;
        if (!reqNum(det, "air_temperature", h.temp)) continue;
        double humVal = 0, cloudVal = 0, windVal = 0;
        const bool hasH = reqNum(det, "relative_humidity", humVal);
        const bool hasC = reqNum(det, "cloud_area_fraction", cloudVal);
        const bool hasW = reqNum(det, "wind_speed", windVal);
        h.humidity = humVal; h.cloud = cloudVal; h.wind = windVal * 3.6;
        h.feels = apparentC(h.temp, h.humidity, h.wind);
        bool day = sunUp(t, lat, lon);
        h.code = h.cloud > 85 ? 3 : h.cloud > 40 ? 2 : h.cloud > 15 ? 1 : 0; h.isDay = day;
        if (nx) {
            const mj::Value* sm = sect(*nx, "summary"); const mj::Value* dt = sect(*nx, "details");
            std::string sym = strAt(sm, "symbol_code");
            if (!sym.empty()) { bool d2; h.code = metCode(sym, d2); h.isDay = day; }
            double amt = num(dt, "precipitation_amount");
            h.precip = amt / span;
            h.precipProb = h.precip >= 0.05 ? std::min(95.0, 50.0 + h.precip * 60.0) : (wetCode(h.code) ? 60.0 : 0.0);
        }
        if (!haveCur) {
            haveCur = true;
            cur.time = t; cur.temp = h.temp; cur.feels = h.feels; cur.humidity = h.humidity; cur.pressure = num(det, "air_pressure_at_sea_level");
            cur.wind = h.wind; cur.windDir = num(det, "wind_from_direction");
            double gustMs = 0.0;
            cur.hasGust = reqNum(det, "wind_speed_of_gust", gustMs);
            cur.gust = cur.hasGust ? gustMs * 3.6 : 0.0;
            cur.cloud = h.cloud;
            cur.hasHumidity = hasH; cur.hasWind = hasW; cur.hasCloud = hasC;
            cur.precip = h.precip; cur.code = h.code; cur.isDay = day;
        }
        raw.push_back(h);
    }
    if (raw.size() < 2) { err = "forecast too short"; return false; }
    // MET Norway is UTC-only and does not return a remote timezone offset.
    // Never adopt an unrelated host PC offset (e.g. Colombo UTC+5:30 for New York); keep an explicit place offset only if caller marked hasUtcOffset, otherwise estimate solar zone from longitude.
    Data d;
    if (out.hasUtcOffset) {
        d.utcOffset = out.utcOffset;
        d.hasUtcOffset = true;
    } else {
        const double solarOff = lon * 240.0;
        d.utcOffset = (std::fabs(out.utcOffset - solarOff) <= 3.5 * 3600.0) ? out.utcOffset : (int)(std::lround(lon / 15.0) * 3600);
        d.hasUtcOffset = false;
    }
    finishSeries(d, lat, lon, raw, cur);
    if (!d.valid) { err = "could not build forecast"; return false; }
    d.source = providerName(PV_METNO);
    out = d;
    return true;
}

// WorldWeatherOnline codes (wttr.in) -> WMO codes
static int wwoToWmo(int c) {
    switch (c) {
        case 113: return 0; case 116: return 2; case 119: case 122: return 3; case 143: case 248: case 260: return 45;
        case 176: return 80; case 179: return 71; case 182: case 317: case 320: return 67; case 185: case 281: case 284: return 56;
        case 200: case 386: case 389: case 392: case 395: return 95; case 227: case 329: case 332: return 73; case 230: case 335: case 338: return 75;
        case 263: case 266: return 51; case 293: case 296: return 61; case 299: case 302: return 63; case 305: case 308: return 65;
        case 311: case 314: return 66; case 323: case 326: return 71; case 350: case 374: case 377: return 77;
        case 353: return 80; case 356: return 81; case 359: return 82; case 362: case 365: case 368: return 85; case 371: return 86;
        default: return 3;
    }
}

static bool parseWttr(const std::string& json, double lat, double lon, Data& out, std::string& err) {
    mj::Value root;
    if (!mj::parse(json, root, err)) return false;
    const mj::Value* cc = arr(&root, "current_condition");
    const mj::Value* wk = arr(&root, "weather");
    if (!cc || cc->arr.empty() || !wk || wk->arr.empty()) { err = "no forecast in response"; return false; }
    const mj::Value& c0 = cc->arr[0];
    // local UTC offset from "localObsDateTime" (local) vs "observation_time" (UTC)
    int off = out.hasUtcOffset ? out.utcOffset : (int)(std::lround(lon / 15.0) * 3600);
    bool parsedOff = false;
    {
        int y, m, d, h1, m1; char ap[4] = {0}; int h2, m2; char ap2[4] = {0};
        std::string lo = strAt(&c0, "localObsDateTime"), ob = strAt(&c0, "observation_time");
        if (sscanf(lo.c_str(), "%d-%d-%d %d:%d %3s", &y, &m, &d, &h1, &m1, ap) == 6 && sscanf(ob.c_str(), "%d:%d %3s", &h2, &m2, ap2) == 3) {
            auto to24 = [](int h, const char* ap) { h %= 12; if (ap[0] == 'P' || ap[0] == 'p') h += 12; return h; };
            int a = to24(h1, ap) * 60 + m1, b = to24(h2, ap2) * 60 + m2;
            int diff = ((a - b) % 1440 + 1440) % 1440;
            // Disambiguate UTC-10/-11 vs UTC+13/+14 using longitude solar offset (lon * 4 min)
            const double solarMin = lon * 4.0;
            if (std::fabs((diff - 1440) - solarMin) < std::fabs(diff - solarMin)) diff -= 1440;
            off = (int)(std::lround(diff / 15.0) * 15 * 60);
            parsedOff = true;
        }
    }
    std::vector<Hour> raw;
    for (const mj::Value& day : wk->arr) {
        std::string ds = strAt(&day, "date"); int y, m, d;
        if (sscanf(ds.c_str(), "%d-%d-%d", &y, &m, &d) != 3) continue;
        const mj::Value* hr = arr(&day, "hourly"); if (!hr) continue;
        for (const mj::Value& h : hr->arr) {
            int tm = (int)numS(&h, "time"); Hour x;
            x.t = astro::daysFromCivil(y, m, d) * 86400.0 + (tm / 100) * 3600.0 + (tm % 100) * 60.0 - off;
            x.temp = numS(&h, "tempC"); x.feels = numS(&h, "FeelsLikeC", x.temp); x.humidity = numS(&h, "humidity");
            x.cloud = numS(&h, "cloudcover"); x.wind = numS(&h, "windspeedKmph"); x.uv = numS(&h, "uvIndex");
            x.precip = numS(&h, "precipMM") / 3.0; x.precipProb = numS(&h, "chanceofrain");
            x.code = wwoToWmo((int)numS(&h, "weatherCode", 119)); x.isDay = sunUp(x.t, lat, lon);
            raw.push_back(x);
        }
    }
    if (raw.size() < 2) { err = "forecast too short"; return false; }
    Current cur;
    auto reqNumS = [](const mj::Value* o, const char* k, double& out) -> bool {
        if (!o) return false;
        const mj::Value* v = o->get(k);
        if (!v) return false;
        if (v->type == mj::Value::Number && std::isfinite(v->num)) { out = v->num; return true; }
        if (v->type == mj::Value::String && !v->str.empty()) {
            char* e = nullptr; double d = std::strtod(v->str.c_str(), &e);
            if (e != v->str.c_str() && std::isfinite(d)) { out = d; return true; }
        }
        return false;
    };
    double wCode = 0;
    if (!reqNumS(&c0, "temp_C", cur.temp) || !reqNumS(&c0, "weatherCode", wCode)) { err = "incomplete current conditions"; return false; }
    cur.feels = numS(&c0, "FeelsLikeC", cur.temp);
    cur.hasHumidity = reqNumS(&c0, "humidity", cur.humidity);
    cur.pressure = numS(&c0, "pressure");
    cur.hasWind = reqNumS(&c0, "windspeedKmph", cur.wind);
    cur.windDir = numS(&c0, "winddirDegree");
    cur.hasCloud = reqNumS(&c0, "cloudcover", cur.cloud);
    cur.precip = numS(&c0, "precipMM");
    cur.code = wwoToWmo((int)std::lround(wCode));
    cur.gust = 0.0; cur.hasGust = false;
    {   // observation time (UTC) -> unix, using the first day's date shifted by the offset
        int y, m, d, h1, m1; char ap[4] = {0};
        std::string lo = strAt(&c0, "localObsDateTime");
        if (sscanf(lo.c_str(), "%d-%d-%d %d:%d %3s", &y, &m, &d, &h1, &m1, ap) == 6) {
            h1 %= 12; if (ap[0] == 'P' || ap[0] == 'p') h1 += 12;
            cur.time = astro::daysFromCivil(y, m, d) * 86400.0 + h1 * 3600.0 + m1 * 60.0 - off;
        } else cur.time = raw.front().t;
    }
    cur.isDay = sunUp(cur.time, lat, lon);
    Data dd; dd.utcOffset = off; dd.hasUtcOffset = parsedOff || out.hasUtcOffset;
    // Find the raw 3h block closest to cur.time for current precipProb, and keep today's earlier blocks so daily tmax/tmin stay accurate.
    double curProb = raw.front().precipProb;
    { double bd = 1e18; for (const Hour& h : raw) { double d = std::fabs(h.t - cur.time); if (d < bd) { bd = d; curProb = h.precipProb; } } }
    Hour now; now.t = cur.time; now.temp = cur.temp; now.feels = cur.feels; now.humidity = cur.humidity; now.cloud = cur.cloud; now.wind = cur.wind;
    now.precip = cur.precip; now.code = cur.code; now.isDay = cur.isDay; now.precipProb = curProb;
    std::vector<Hour> all;
    for (const Hour& h : raw) if (h.t < now.t - 600) all.push_back(h);
    all.push_back(now);
    for (const Hour& h : raw) if (h.t > now.t + 600) all.push_back(h);
    finishSeries(dd, lat, lon, all, cur);
    if (!dd.valid) { err = "could not build forecast"; return false; }
    dd.hasUv = true;
    {   // wttr gives UV per 3 h block: rebuild uv max per day from the raw blocks
        for (Day& day : dd.daily) for (const Hour& h : raw) if (h.t >= day.t0 && h.t < day.t0 + 86400.0) day.uvMax = std::max(day.uvMax, h.uv);
        for (Hour& h : dd.hourly) { double best = 0, bd = 1e18; for (const Hour& r : raw) { double dt = std::fabs(r.t - h.t); if (dt < bd) { bd = dt; best = r.uv; } } h.uv = best; }
    }
    dd.source = providerName(PV_WTTR);
    out = dd;
    return true;
}

static int sevenTimerCode(const std::string& w) {
    auto has = [&](const char* k) { return w.find(k) != std::string::npos; };
    if (has("rainsnow")) return 67;
    if (has("lightsnow")) return 71;     // must precede "ts" check ("lighTSnow" contains "ts")
    if (has("snow")) return 73;
    if (has("tsrain") || has("ts")) return 95;
    if (has("lightrain")) return 61;
    if (has("oshower")) return 80;
    if (has("ishower")) return 81;
    if (has("rain")) return 63;
    if (has("humid")) return 45;
    if (has("pcloudy")) return 2;
    if (has("mcloudy") || has("cloudy")) return 3;
    if (has("clear")) return 0;
    return 3;
}

static bool parse7Timer(const std::string& json, double lat, double lon, Data& out, std::string& err) {
    mj::Value root;
    if (!mj::parse(json, root, err)) return false;
    const mj::Value* ds = arr(&root, "dataseries");
    std::string init = strAt(&root, "init");
    int y, m, d, hh;
    if (!ds || ds->arr.empty() || init.size() < 10 || sscanf(init.c_str(), "%4d%2d%2d%2d", &y, &m, &d, &hh) != 4) { err = "no forecast in response"; return false; }
    const double t0 = astro::daysFromCivil(y, m, d) * 86400.0 + hh * 3600.0;
    static const double cloudPct[] = {0, 3, 12, 25, 37, 50, 62, 75, 87, 97};
    static const double windMs[] = {0, 0.15, 1.9, 5.7, 9.4, 14.0, 20.9, 28.6, 35.0};
    static const double rainMm[] = {0, 0.1, 0.6, 2.5, 7, 13, 23, 40, 62, 80};
    std::vector<Hour> raw;
    for (const mj::Value& e : ds->arr) {
        Hour h; h.t = t0 + numS(&e, "timepoint") * 3600.0;
        h.temp = numS(&e, "temp2m");
        h.humidity = numS(&e, "rh2m");
        if (const mj::Value* rv = e.get("rh2m")) if (rv->type == mj::Value::String) h.humidity = atof(rv->str.c_str());
        int cc = (int)numS(&e, "cloudcover", 5); h.cloud = cloudPct[std::max(0, std::min(9, cc))];
        const mj::Value* wv = sect(e, "wind10m"); int ws = (int)numS(wv, "speed", 2); h.wind = windMs[std::max(0, std::min(8, ws))] * 3.6;
        int pa = (int)numS(&e, "prec_amount"); h.precip = rainMm[std::max(0, std::min(9, pa))] / 3.0;   // 7Timer prec_amount is per 3-hour step
        std::string w = strAt(&e, "weather");
        h.code = sevenTimerCode(w);
        if (strAt(&e, "prec_type") == "snow" && h.code < 71 && h.precip > 0) h.code = 73;
        h.isDay = sunUp(h.t, lat, lon);
        h.feels = apparentC(h.temp, h.humidity, h.wind);
        h.precipProb = h.precip >= 0.05 ? std::min(95.0, 50.0 + h.precip * 20.0) : (wetCode(h.code) ? 60.0 : 0.0);
        raw.push_back(h);
    }
    if (raw.size() < 2) { err = "forecast too short"; return false; }
    // current conditions = the sample nearest to the present (clamped to forecast span so skewed clocks never cause huge gaps)
    double nowT = (double)time(nullptr);
    size_t best = 0; for (size_t i = 0; i < raw.size(); ++i) if (std::fabs(raw[i].t - nowT) < std::fabs(raw[best].t - nowT)) best = i;
    const Hour& b = raw[best];
    Current cur; cur.time = b.t; cur.temp = b.temp; cur.feels = b.feels; cur.humidity = b.humidity; cur.wind = b.wind; cur.gust = 0.0; cur.hasGust = false;
    cur.cloud = b.cloud; cur.precip = b.precip; cur.code = b.code; cur.isDay = b.isDay;
    if (raw.front().t > nowT && raw.front().t - nowT <= 6 * 3600.0) {
        Hour h0 = raw.front(); h0.t = std::floor(nowT / 3600.0) * 3600.0; raw.insert(raw.begin(), h0);
    }
    Data dd;
    if (out.hasUtcOffset) { dd.utcOffset = out.utcOffset; dd.hasUtcOffset = true; }
    else {
        const double solarOff = lon * 240.0;
        dd.utcOffset = (std::fabs(out.utcOffset - solarOff) <= 3.5 * 3600.0) ? out.utcOffset : (int)(std::lround(lon / 15.0) * 3600);
        dd.hasUtcOffset = false;
    }
    finishSeries(dd, lat, lon, raw, cur);
    if (!dd.valid) { err = "could not build forecast"; return false; }
    dd.source = providerName(PV_7TIMER);
    out = dd;
    return true;
}

// ---------------------------------------------------------------- Bright Sky (DWD open data, api.brightsky.dev)
static int brightSkyCode(const std::string& cond, const std::string& icon, double precip, double cloud) {
    if (cond == "thunderstorm" || icon == "thunderstorm") return 95;
    if (cond == "hail" || icon == "hail") return 96;
    if (cond == "snow" || icon == "snow") return precip >= 2.0 ? 75 : (precip >= 0.5 ? 73 : 71);
    if (cond == "sleet" || icon == "sleet") return 67;
    if (cond == "rain" || icon == "rain") return precip >= 4.0 ? 65 : (precip >= 1.0 ? 63 : 61);
    if (cond == "fog" || icon == "fog") return 45;
    if (icon == "cloudy") return 3;
    if (icon == "partly-cloudy-day" || icon == "partly-cloudy-night") return 2;
    if (icon == "clear-day" || icon == "clear-night") return cloud >= 25.0 ? 1 : 0;
    if (cloud >= 80.0) return 3;
    if (cloud >= 35.0) return 2;
    if (cloud >= 15.0) return 1;
    return 0;
}

static bool parseBrightSky(const std::string& json, double lat, double lon, Data& out, std::string& err) {
    mj::Value root;
    if (!mj::parse(json, root, err)) return false;
    const mj::Value* ws = arr(&root, "weather");
    if (!ws || ws->arr.empty()) { err = "no Bright Sky forecast"; return false; }
    std::vector<Hour> raw;
    std::vector<double> rawPres, rawDir, rawGust;
    std::vector<bool> rawHasCloud, rawHasWind, rawHasGust, rawHasHum;
    raw.reserve(ws->arr.size());
    for (const mj::Value& e : ws->arr) {
        double ts = 0.0, tVal = 0.0;
        if (!parseIso8601(strAt(&e, "timestamp"), ts)) continue;
        if (!reqNum(&e, "temperature", tVal)) continue;
        Hour h;
        h.t = ts;
        h.temp = tVal;
        double humV = 60.0, cldV = 40.0, wndV = 0.0, gstV = 0.0;
        const bool hasH = reqNum(&e, "relative_humidity", humV);
        const bool hasC = reqNum(&e, "cloud_cover", cldV);
        const bool hasW = reqNum(&e, "wind_speed", wndV);
        const bool hasG = reqNum(&e, "wind_gust_speed", gstV);
        h.humidity = hasH ? humV : 60.0;
        h.cloud = hasC ? cldV : 40.0;
        h.wind = hasW ? wndV : 0.0;                 // Bright Sky default dwd units: km/h
        h.precip = num(&e, "precipitation", 0.0);
        const std::string cond = strAt(&e, "condition"), icon = strAt(&e, "icon");
        h.code = brightSkyCode(cond, icon, h.precip, h.cloud);
        h.isDay = sunUp(ts, lat, lon);
        h.feels = apparentC(h.temp, h.humidity, h.wind);
        h.precipProb = num(&e, "precipitation_probability", h.precip >= 0.1 ? std::min(95.0, 55.0 + h.precip * 18.0) : (wetCode(h.code) ? 55.0 : 0.0));
        raw.push_back(h);
        rawPres.push_back(num(&e, "pressure_msl", 0.0));
        rawDir.push_back(num(&e, "wind_direction", 0.0));
        rawGust.push_back(hasG ? gstV : 0.0);
        rawHasHum.push_back(hasH); rawHasCloud.push_back(hasC); rawHasWind.push_back(hasW); rawHasGust.push_back(hasG);
    }
    if (raw.size() < 2) { err = "Bright Sky forecast too short"; return false; }
    const double nowT = (double)time(nullptr);
    size_t best = 0;
    for (size_t i = 0; i < raw.size(); ++i) if (std::fabs(raw[i].t - nowT) < std::fabs(raw[best].t - nowT)) best = i;
    const Hour& b = raw[best];
    Current cur;
    cur.time = b.t; cur.temp = b.temp; cur.feels = b.feels; cur.humidity = b.humidity;
    cur.pressure = rawPres[best]; cur.wind = b.wind; cur.windDir = rawDir[best]; cur.gust = rawGust[best];
    cur.hasHumidity = rawHasHum[best]; cur.hasCloud = rawHasCloud[best]; cur.hasWind = rawHasWind[best]; cur.hasGust = rawHasGust[best];
    cur.cloud = b.cloud; cur.precip = b.precip; cur.code = b.code; cur.isDay = b.isDay;
    Data dd;
    if (out.hasUtcOffset) { dd.utcOffset = out.utcOffset; dd.hasUtcOffset = true; }
    else {
        const double solarOff = lon * 240.0;
        dd.utcOffset = (std::fabs(out.utcOffset - solarOff) <= 3.5 * 3600.0) ? out.utcOffset : (int)(std::lround(lon / 15.0) * 3600);
        dd.hasUtcOffset = false;
    }
    finishSeries(dd, lat, lon, raw, cur);
    if (!dd.valid) { err = "could not build Bright Sky forecast"; return false; }
    dd.source = providerName(PV_BRIGHTSKY);
    out = dd;
    return true;
}

// ---------------------------------------------------------------- NWS / NOAA (US National Weather Service, api.weather.gov)
static int nwsCode(const std::string& text, double pop, double& cloudOut, double& precipOut) {
    std::string s = util::upper(text);
    auto has = [&](const char* k) { return s.find(k) != std::string::npos; };
    cloudOut = 40.0; precipOut = 0.0;
    if (has("THUNDER") || has("T-STORM") || has("TSTM")) { cloudOut = 92.0; precipOut = 3.0; return 95; }
    if (has("BLIZZARD") || has("HEAVY SNOW")) { cloudOut = 95.0; precipOut = 2.5; return 75; }
    if (has("SNOW") || has("FLURR")) { cloudOut = 88.0; precipOut = 0.8; return has("LIGHT") ? 71 : 73; }
    if (has("FREEZING") || has("SLEET") || has("ICE")) { cloudOut = 90.0; precipOut = 1.0; return 66; }
    if (has("HEAVY RAIN")) { cloudOut = 94.0; precipOut = 5.0; return 65; }
    if (has("SHOWER")) { cloudOut = 78.0; precipOut = std::max(0.4, pop * 0.02); return has("LIGHT") || has("SLIGHT") || has("ISOLATED") ? 80 : 81; }
    if (has("DRIZZLE")) { cloudOut = 85.0; precipOut = 0.3; return 51; }
    if (has("RAIN")) { cloudOut = 88.0; precipOut = std::max(0.6, pop * 0.025); return has("LIGHT") || has("SLIGHT") ? 61 : 63; }
    if (has("FOG") || has("MIST") || has("HAZE")) { cloudOut = 85.0; return 45; }
    // Match specific cloud qualifiers BEFORE broad "CLOUDY" so "Partly Cloudy" yields WMO 2 (48%) instead of Overcast!
    if (has("PARTLY CLOUDY") || has("PARTLY SUNNY")) { cloudOut = 48.0; return 2; }
    if (has("MOSTLY CLEAR") || has("MOSTLY SUNNY")) { cloudOut = 20.0; return 1; }
    if (has("MOSTLY CLOUDY")) { cloudOut = 78.0; return 3; }
    if (has("OVERCAST") || has("CLOUDY")) { cloudOut = 92.0; return 3; }
    if (has("CLEAR") || has("SUNNY") || has("FAIR")) { cloudOut = 5.0; return 0; }
    return 2;
}

static double parseNwsWindKmh(const std::string& ws, double& gustOut, bool& hasWindOut, bool& hasGustOut) {
    double vals[4] = {0}; int cnt = 0;
    const char* p = ws.c_str();
    while (*p && cnt < 4) {
        if (*p >= '0' && *p <= '9') {
            char* e = nullptr;
            vals[cnt++] = strtod(p, &e);
            p = e;
        } else ++p;
    }
    hasWindOut = (cnt > 0);
    hasGustOut = (cnt >= 2);
    double v = cnt == 0 ? 0.0 : (cnt == 1 ? vals[0] : (vals[0] + vals[cnt - 1]) * 0.5);
    double g = cnt >= 2 ? vals[cnt - 1] : v;
    const std::string u = util::upper(ws);
    const double scale = (u.find("KM") != std::string::npos) ? 1.0 : (u.find("KT") != std::string::npos ? 1.852 : 1.60934);
    gustOut = hasGustOut ? (g * scale) : 0.0;
    return v * scale;
}

static bool parseNws(const std::string& json, double lat, double lon, Data& out, std::string& err) {
    mj::Value root;
    if (!mj::parse(json, root, err)) return false;
    const mj::Value* props = sect(root, "properties");
    const mj::Value* periods = arr(props, "periods");
    if (!periods || periods->arr.empty()) { err = "no NWS forecast periods"; return false; }
    std::vector<Hour> raw;
    std::vector<double> rawDir, rawGust;
    std::vector<unsigned char> rawHasHum, rawHasWind, rawHasGust;
    raw.reserve(periods->arr.size());
    int tzFromNws = out.utcOffset; bool haveTz = false;
    for (const mj::Value& p : periods->arr) {
        double ts = 0.0; int tzSec = 0;
        if (!parseIso8601(strAt(&p, "startTime"), ts, &tzSec)) continue;
        if (!haveTz) { tzFromNws = tzSec; haveTz = true; }
        double tVal = 0.0;
        if (!reqNum(&p, "temperature", tVal)) continue;
        const std::string unit = strAt(&p, "temperatureUnit");
        if (unit == "F" || unit == "f") tVal = (tVal - 32.0) * 5.0 / 9.0;
        Hour h;
        h.t = ts;
        h.temp = tVal;
        const mj::Value* rh = sect(p, "relativeHumidity");
        const bool hasH = (rh && rh->get("value") && rh->get("value")->type == mj::Value::Number);
        h.humidity = hasH ? rh->get("value")->num : 55.0;
        const mj::Value* pop = sect(p, "probabilityOfPrecipitation");
        h.precipProb = (pop && pop->get("value") && pop->get("value")->type == mj::Value::Number) ? pop->get("value")->num : 0.0;
        double gust = 0.0; bool hasW = false, hasG = false;
        h.wind = parseNwsWindKmh(strAt(&p, "windSpeed"), gust, hasW, hasG);
        h.code = nwsCode(strAt(&p, "shortForecast"), h.precipProb, h.cloud, h.precip);
        const mj::Value* iday = p.get("isDaytime");
        h.isDay = (iday && iday->type == mj::Value::Bool) ? iday->b : sunUp(ts, lat, lon);
        h.feels = apparentC(h.temp, h.humidity, h.wind);
        raw.push_back(h);
        rawDir.push_back(compassDeg(strAt(&p, "windDirection")));
        rawGust.push_back(gust);
        rawHasHum.push_back(hasH ? 1 : 0);
        rawHasWind.push_back(hasW ? 1 : 0);
        rawHasGust.push_back(hasG ? 1 : 0);
    }
    if (raw.size() < 2) { err = "NWS forecast too short"; return false; }
    const double nowT = (double)time(nullptr);
    size_t best = 0;
    for (size_t i = 0; i < raw.size(); ++i) if (std::fabs(raw[i].t - nowT) < std::fabs(raw[best].t - nowT)) best = i;
    const Hour& b = raw[best];
    Current cur;
    cur.time = b.t; cur.temp = b.temp; cur.feels = b.feels; cur.humidity = b.humidity;
    cur.wind = b.wind; cur.windDir = rawDir[best]; cur.gust = rawGust[best];
    cur.hasHumidity = rawHasHum[best] != 0;
    cur.hasWind = rawHasWind[best] != 0;
    cur.hasGust = rawHasGust[best] != 0;
    cur.cloud = b.cloud; cur.precip = b.precip; cur.code = b.code; cur.isDay = b.isDay;
    Data dd; dd.utcOffset = tzFromNws; dd.hasUtcOffset = haveTz || out.hasUtcOffset;
    finishSeries(dd, lat, lon, raw, cur);
    if (!dd.valid) { err = "could not build NWS forecast"; return false; }
    dd.source = providerName(PV_NWS);
    out = dd;
    return true;
}

// ---------------------------------------------------------------- WeatherAPI.com (free API key + built-in AQI)
static int weatherApiCode(int c) {
    switch (c) {
        case 1000: return 0;
        case 1003: return 2;
        case 1006: case 1009: return 3;
        case 1030: case 1135: return 45;
        case 1147: return 48;
        case 1063: case 1180: case 1183: return 61;
        case 1186: case 1189: return 63;
        case 1192: case 1195: return 65;
        case 1150: case 1153: return 51;
        case 1168: case 1171: return 56;
        case 1198: case 1201: return 66;
        case 1066: case 1210: case 1213: return 71;
        case 1216: case 1219: return 73;
        case 1114: case 1117: case 1222: case 1225: return 75;
        case 1237: case 1261: case 1264: return 77;
        case 1069: case 1072: case 1204: case 1207: return 67;
        case 1240: return 80;
        case 1243: return 81;
        case 1246: return 82;
        case 1249: case 1252: case 1255: return 85;
        case 1258: return 86;
        case 1087: case 1273: case 1276: case 1279: case 1282: return 95;
        default: return 3;
    }
}

static bool parseWeatherApi(const std::string& json, double lat, double lon, Data& out, std::string& err) {
    mj::Value root;
    if (!mj::parse(json, root, err)) return false;
    if (const mj::Value* eo = sect(root, "error")) {
        std::string msg = strAt(eo, "message");
        err = msg.empty() ? "WeatherAPI error" : msg;
        return false;
    }
    const mj::Value* c = sect(root, "current");
    if (!c) { err = "no current WeatherAPI data"; return false; }
    Data dd;
    dd.utcOffset = out.utcOffset;
    dd.hasUtcOffset = out.hasUtcOffset;
    if (const mj::Value* loc = sect(root, "location")) {
        dd.timezone = strAt(loc, "tz_id");
        const std::string lt = strAt(loc, "localtime");
        const double lEpoch = num(loc, "localtime_epoch", 0.0);
        int y, m, d, hh = 0, mm = 0;
        if (lEpoch > 0 && sscanf(lt.c_str(), "%d-%d-%d %d:%d", &y, &m, &d, &hh, &mm) == 5) {
            const double localAsUtc = astro::daysFromCivil(y, m, d) * 86400.0 + hh * 3600.0 + mm * 60.0;
            dd.utcOffset = (int)(std::llround((localAsUtc - lEpoch) / 900.0) * 900LL);
            dd.hasUtcOffset = true;
        }
    }
    Current cur;
    if (!reqNum(c, "temp_c", cur.temp)) { err = "missing WeatherAPI temp"; return false; }
    cur.time = num(c, "last_updated_epoch", (double)time(nullptr));
    cur.feels = num(c, "feelslike_c", cur.temp);
    cur.hasHumidity = reqNum(c, "humidity", cur.humidity);
    cur.pressure = num(c, "pressure_mb", 0.0);
    cur.hasWind = reqNum(c, "wind_kph", cur.wind);
    cur.windDir = num(c, "wind_degree", 0.0);
    cur.hasGust = reqNum(c, "gust_kph", cur.gust);
    cur.hasCloud = reqNum(c, "cloud", cur.cloud);
    cur.precip = num(c, "precip_mm", 0.0);
    cur.isDay = num(c, "is_day", 1.0) > 0.5;
    if (const mj::Value* co = sect(*c, "condition")) cur.code = weatherApiCode((int)num(co, "code", 1003));

    // Built-in Air Quality right inside the WeatherAPI response!
    if (const mj::Value* aqObj = sect(*c, "air_quality")) {
        double p25 = 0.0, epa = 0.0;
        const bool hasP25 = reqNum(aqObj, "pm2_5", p25);
        dd.pm25 = hasP25 ? p25 : 0.0;
        dd.pm10 = num(aqObj, "pm10", 0.0);
        if (hasP25) { dd.aqi = pm25ToUsAqi(p25); dd.aqiScale = AqiScale::Pm25Est; dd.airObsTime = cur.time; dd.hasAir = true; dd.airSource = "WeatherAPI.com"; }
        else if (reqNum(aqObj, "us-epa-index", epa)) { dd.aqi = epaIndexToUsAqi((int)std::lround(epa)); dd.aqiScale = AqiScale::Us; dd.airObsTime = cur.time; dd.hasAir = true; dd.airSource = "WeatherAPI.com"; }
    }

    std::vector<Hour> raw;
    if (const mj::Value* fc = sect(root, "forecast")) {
        if (const mj::Value* fdays = arr(fc, "forecastday")) {
            for (const mj::Value& fd : fdays->arr) {
                if (const mj::Value* hrs = arr(&fd, "hour")) {
                    for (const mj::Value& he : hrs->arr) {
                        Hour h;
                        h.t = num(&he, "time_epoch", 0.0);
                        if (h.t <= 0.0) continue;
                        h.temp = num(&he, "temp_c", cur.temp);
                        h.feels = num(&he, "feelslike_c", h.temp);
                        h.humidity = num(&he, "humidity", 55.0);
                        h.precipProb = std::max(num(&he, "chance_of_rain", 0.0), num(&he, "chance_of_snow", 0.0));
                        h.precip = num(&he, "precip_mm", 0.0);
                        h.cloud = num(&he, "cloud", 0.0);
                        h.wind = num(&he, "wind_kph", 0.0);
                        h.uv = num(&he, "uv", 0.0);
                        h.isDay = num(&he, "is_day", 1.0) > 0.5;
                        if (const mj::Value* co = sect(he, "condition")) h.code = weatherApiCode((int)num(co, "code", 1003));
                        raw.push_back(h);
                    }
                }
            }
        }
    }
    if (raw.size() < 2) { err = "no WeatherAPI hourly data"; return false; }
    finishSeries(dd, lat, lon, raw, cur);
    if (!dd.valid) { err = "could not build WeatherAPI forecast"; return false; }
    dd.hasUv = true;
    dd.source = providerName(PV_WEATHERAPI);
    out = dd;
    return true;
}

// ---------------------------------------------------------------- OpenWeatherMap (free tier 5-day/3-hour forecast)
static int owmCode(int id) {
    if (id >= 200 && id < 300) return 95;
    if (id >= 300 && id < 320) return id == 300 ? 51 : (id >= 302 ? 55 : 53);
    if (id >= 320 && id < 400) return 53;
    if (id == 500) return 61;
    if (id == 501) return 63;
    if (id >= 502 && id <= 504) return 65;
    if (id == 511) return 66;
    if (id == 520) return 80;
    if (id == 521) return 81;
    if (id >= 522 && id < 600) return 82;
    if (id == 600) return 71;
    if (id == 601) return 73;
    if (id == 602) return 75;
    if (id >= 611 && id <= 616) return 67;
    if (id >= 620 && id < 700) return id >= 622 ? 86 : 85;
    if (id >= 700 && id < 800) return 45;
    if (id == 800) return 0;
    if (id == 801) return 1;
    if (id == 802) return 2;
    return 3;
}

static bool parseOpenWeather(const std::string& json, double lat, double lon, Data& out, std::string& err) {
    mj::Value root;
    if (!mj::parse(json, root, err)) return false;
    if (const mj::Value* cod = root.get("cod")) {
        const bool bad = (cod->type == mj::Value::String && cod->str != "200") ||
                         (cod->type == mj::Value::Number && (int)std::lround(cod->num) != 200);
        if (bad) {
            std::string msg = strAt(&root, "message");
            err = msg.empty() ? "OpenWeatherMap error" : msg;
            return false;
        }
    }
    const mj::Value* lst = arr(&root, "list");
    if (!lst || lst->arr.empty()) { err = "no OpenWeatherMap forecast"; return false; }
    Data dd;
    dd.utcOffset = out.utcOffset;
    dd.hasUtcOffset = out.hasUtcOffset;
    if (const mj::Value* city = sect(root, "city")) {
        double tzV = 0.0;
        if (reqNum(city, "timezone", tzV)) { dd.utcOffset = (int)std::lround(tzV); dd.hasUtcOffset = true; }
    }

    std::vector<Hour> raw;
    std::vector<double> rawPres, rawDir, rawGust;
    std::vector<bool> rawHasHum, rawHasCloud, rawHasWind, rawHasGust;
    raw.reserve(lst->arr.size());
    for (const mj::Value& e : lst->arr) {
        const double ts = num(&e, "dt", 0.0);
        const mj::Value* mn = sect(e, "main");
        if (ts <= 0.0 || !mn) continue;
        double tVal = 0.0;
        if (!reqNum(mn, "temp", tVal)) continue;
        Hour h;
        h.t = ts;
        h.temp = tVal;
        h.feels = num(mn, "feels_like", tVal);
        double humVal = 55.0;
        const bool hasH = reqNum(mn, "humidity", humVal);
        h.humidity = hasH ? humVal : 55.0;
        const mj::Value* cl = sect(e, "clouds");
        double cldVal = 0.0;
        const bool hasC = reqNum(cl, "all", cldVal);
        h.cloud = hasC ? cldVal : 0.0;
        const mj::Value* wi = sect(e, "wind");
        double wMs = 0.0;
        const bool hasW = reqNum(wi, "speed", wMs);
        h.wind = hasW ? wMs * 3.6 : 0.0;                     // m/s -> km/h
        h.precipProb = num(&e, "pop", 0.0) * 100.0;
        double r3 = 0.0;
        if (const mj::Value* rn = sect(e, "rain")) r3 += num(rn, "3h", num(rn, "1h", 0.0));
        if (const mj::Value* sn = sect(e, "snow")) r3 += num(sn, "3h", num(sn, "1h", 0.0));
        h.precip = r3 / 3.0;                                 // 3-hour step -> mm/h
        if (const mj::Value* wa = arr(&e, "weather"); wa && !wa->arr.empty())
            h.code = owmCode((int)num(&wa->arr[0], "id", 803));
        const mj::Value* sy = sect(e, "sys");
        const std::string pod = strAt(sy, "pod");
        h.isDay = pod.empty() ? sunUp(ts, lat, lon) : (pod == "d");
        raw.push_back(h);
        rawPres.push_back(num(mn, "pressure", 0.0));
        rawDir.push_back(num(wi, "deg", 0.0));
        double gMs = 0.0;
        const bool hasG = reqNum(wi, "gust", gMs);
        rawGust.push_back(hasG ? gMs * 3.6 : 0.0);
        rawHasHum.push_back(hasH);
        rawHasCloud.push_back(hasC);
        rawHasWind.push_back(hasW);
        rawHasGust.push_back(hasG);
    }
    if (raw.size() < 2) { err = "OpenWeatherMap forecast too short"; return false; }
    const double nowT = (double)time(nullptr);
    size_t best = 0;
    for (size_t i = 0; i < raw.size(); ++i) if (std::fabs(raw[i].t - nowT) < std::fabs(raw[best].t - nowT)) best = i;
    const Hour& b = raw[best];
    Current cur;
    cur.time = b.t; cur.temp = b.temp; cur.feels = b.feels; cur.humidity = b.humidity;
    cur.pressure = rawPres[best]; cur.wind = b.wind; cur.windDir = rawDir[best]; cur.gust = rawGust[best];
    cur.hasHumidity = rawHasHum[best]; cur.hasCloud = rawHasCloud[best];
    cur.hasWind = rawHasWind[best]; cur.hasGust = rawHasGust[best];
    cur.cloud = b.cloud; cur.precip = b.precip; cur.code = b.code; cur.isDay = b.isDay;
    finishSeries(dd, lat, lon, raw, cur);
    if (!dd.valid) { err = "could not build OpenWeatherMap forecast"; return false; }
    dd.source = providerName(PV_OPENWEATHER);
    out = dd;
    return true;
}

bool parseProvider(int id, const std::string& body, double lat, double lon, Data& out, std::string& err) {
    // `out.utcOffset` may be pre-set by the caller (`out.hasUtcOffset == true` when from an authoritative zone)
    Data seed; seed.utcOffset = out.utcOffset; seed.hasUtcOffset = out.hasUtcOffset;
    bool ok = false;
    switch (id) {
        case PV_OPENMETEO: ok = parseForecast(body, seed, err); break;
        case PV_METNO: ok = parseMetNo(body, lat, lon, seed, err); break;
        case PV_WTTR: ok = parseWttr(body, lat, lon, seed, err); break;
        case PV_7TIMER: ok = parse7Timer(body, lat, lon, seed, err); break;
        case PV_BRIGHTSKY: ok = parseBrightSky(body, lat, lon, seed, err); break;
        case PV_NWS: ok = parseNws(body, lat, lon, seed, err); break;
        case PV_WEATHERAPI: ok = parseWeatherApi(body, lat, lon, seed, err); break;
        case PV_OPENWEATHER: ok = parseOpenWeather(body, lat, lon, seed, err); break;
        default: err = "unknown provider"; return false;
    }
    if (ok) { double f = out.fetched; out = seed; out.fetched = f; }
    return ok;
}

// ---------------------------------------------------------------- HTTP (Windows)
#ifdef _WIN32
namespace {
std::string winErr(DWORD e) {
    switch (e) {
        case 0x80004004UL: return "request aborted";         // E_ABORT (2147500036): TLS/HTTP2/timeout abort
        case 12002: return "timed out";
        case 12007: return "name not resolved (DNS)";
        case 12017: return "cancelled";
        case 12029: return "could not connect";
        case 12030: return "connection dropped";
        case 12031: return "connection reset";
        case 12152: return "incomplete server response";
        case 12157: case 12175: case 12179: return "secure connection failed";
        case 12037: case 12038: case 12045: return "certificate problem";
        case 12163: return "no internet";
        default: return fmt("network error %lu", (unsigned long)e);
    }
}

std::atomic<bool> g_cancel{false};       // set when the app is closing: no new request starts and a running one stops reading
std::mutex g_reqMu;
struct ActiveHandle { HINTERNET h = nullptr; };
std::vector<ActiveHandle*> g_activeReqs;

void sleepInterruptible(DWORD ms) {
    const ULONGLONG t0 = GetTickCount64();
    while (!g_cancel && GetTickCount64() - t0 < ms) Sleep(25);
}

std::string trimKey(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= ' ') ++a;
    while (b > a && (unsigned char)s[b - 1] <= ' ') --b;
    return s.substr(a, b - a);
}

std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o; o.reserve(s.size());
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
            o.push_back((char)c);
        else { o.push_back('%'); o.push_back(hex[c >> 4]); o.push_back(hex[c & 0xF]); }
    }
    return o;
}

struct HandleGuard {
    ActiveHandle slot;
    explicit HandleGuard(HINTERNET h) {
        std::lock_guard<std::mutex> l(g_reqMu);
        slot.h = h;
        g_activeReqs.push_back(&slot);
    }
    ~HandleGuard() { close(); }
    void close() {
        HINTERNET h = nullptr;
        {
            std::lock_guard<std::mutex> l(g_reqMu);
            h = slot.h;
            slot.h = nullptr;
            auto it = std::find(g_activeReqs.begin(), g_activeReqs.end(), &slot);
            if (it != g_activeReqs.end()) g_activeReqs.erase(it);
        }
        if (h) WinHttpCloseHandle(h);
    }
};

struct ProxyCfg { std::wstring manual, bypass, pac; bool have = false; };
ProxyCfg readSystemProxy() {
    ProxyCfg c;
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG ie{};
    if (WinHttpGetIEProxyConfigForCurrentUser(&ie)) {
        if (ie.lpszProxy) c.manual = ie.lpszProxy;
        if (ie.lpszProxyBypass) c.bypass = ie.lpszProxyBypass;
        if (ie.lpszAutoConfigUrl) c.pac = ie.lpszAutoConfigUrl;
        c.have = !c.manual.empty() || !c.pac.empty();
        if (ie.lpszProxy) GlobalFree(ie.lpszProxy);
        if (ie.lpszProxyBypass) GlobalFree(ie.lpszProxyBypass);
        if (ie.lpszAutoConfigUrl) GlobalFree(ie.lpszAutoConfigUrl);
    }
    return c;
}
// Refresh proxy settings at most once per 60 seconds so VPN/proxy changes take effect without restarting.
ProxyCfg systemProxy() {
    static std::mutex mu;
    static ProxyCfg cached;
    static ULONGLONG fetchedAt = 0;
    std::lock_guard<std::mutex> l(mu);
    const ULONGLONG now = GetTickCount64();
    if (fetchedAt == 0 || now - fetchedAt > 60000ULL) { cached = readSystemProxy(); fetchedAt = now; }
    return cached;
}

std::atomic<int> g_goodMode{0};     // connection mode that worked last time (written and read by several threads)

// mode 0: Windows automatic proxy detection, 1: configured system proxy (manual or PAC), 2: direct
// fallbackTls12: false = OS-managed Schannel TLS + gzip decompression;
//                true  = explicit TLS 1.2 + HTTP/1.1 + identity encoding + Connection: close
//                        (recovers from Windows Schannel TLS 1.3 / HTTP/2 / middlebox E_ABORT 2147500036).
bool httpOnce(int mode, bool fallbackTls12, const std::wstring& host, const std::string& path, bool https,
              const std::string& ua, std::string& body, std::string& err, bool& answered, DWORD& httpStatus, DWORD& rawErr) {
    body.clear(); answered = false; httpStatus = 0; rawErr = 0;
    if (g_cancel) { err = "cancelled"; return false; }
    std::wstring wua = util::widen(ua);
    const ProxyCfg pc = systemProxy();
    HINTERNET ses = nullptr;
    if (mode == 0) {
        ses = WinHttpOpen(wua.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!ses) ses = WinHttpOpen(wua.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    } else if (mode == 1) {
        if (!pc.manual.empty()) ses = WinHttpOpen(wua.c_str(), WINHTTP_ACCESS_TYPE_NAMED_PROXY, pc.manual.c_str(), pc.bypass.empty() ? WINHTTP_NO_PROXY_BYPASS : pc.bypass.c_str(), 0);
        else ses = WinHttpOpen(wua.c_str(), WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    } else {
        ses = WinHttpOpen(wua.c_str(), WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (!ses) { rawErr = GetLastError(); err = winErr(rawErr); return false; }
    // Generous timeouts (10s DNS, 15s connect, 15s send, 25s receive) prevent E_ABORT (2147500036) on IPv6->IPv4 fallback or slower routes.
    WinHttpSetTimeouts(ses, 10000, 15000, 15000, 25000);
    // Note: do NOT enable WINHTTP_OPTION_DECOMPRESSION (118) — Windows WinHTTP's gzip decompressor aborts with
    // E_ABORT (0x80004004 / 2147500036) on Open-Meteo's streaming gzip responses inside WinHttpQueryDataAvailable.
    if (fallbackTls12) {
        // Explicit TLS 1.2 + pure HTTP/1.1 avoids Schannel TLS 1.3 / HTTP/2 stream aborts on strict middleboxes.
        const DWORD only12 = 0x00000800;
        WinHttpSetOption(ses, WINHTTP_OPTION_SECURE_PROTOCOLS, (LPVOID)&only12, sizeof only12);
        const DWORD http11 = 0;
        WinHttpSetOption(ses, 133 /* WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL */, (LPVOID)&http11, sizeof http11);
    }
    HINTERNET con = WinHttpConnect(ses, host.c_str(), https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT, 0);
    if (!con) { rawErr = GetLastError(); err = winErr(rawErr); WinHttpCloseHandle(ses); return false; }
    std::wstring wpath = util::widen(path);
    HINTERNET req = WinHttpOpenRequest(con, L"GET", wpath.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0);
    bool ok = false;
    if (req) {
        HandleGuard reqGuard(req);
        bool pacFailed = false;
        if (mode == 1 && pc.manual.empty() && !pc.pac.empty()) {      // PAC script
            WINHTTP_AUTOPROXY_OPTIONS ao{}; ao.dwFlags = WINHTTP_AUTOPROXY_CONFIG_URL; ao.lpszAutoConfigUrl = pc.pac.c_str(); ao.fAutoLogonIfChallenged = TRUE;
            std::wstring url = std::wstring(https ? L"https://" : L"http://") + host + wpath;
            WINHTTP_PROXY_INFO pi{};
            if (WinHttpGetProxyForUrl(ses, url.c_str(), &ao, &pi)) {
                WinHttpSetOption(req, WINHTTP_OPTION_PROXY, &pi, sizeof pi);
                if (pi.lpszProxy) GlobalFree(pi.lpszProxy);
                if (pi.lpszProxyBypass) GlobalFree(pi.lpszProxyBypass);
            } else {
                pacFailed = true;
                rawErr = GetLastError();
                err = winErr(rawErr);
            }
        }
        if (!pacFailed && !g_cancel) {
            const std::wstring hdrs = fallbackTls12
                ? L"Accept: application/geo+json, application/json;q=0.9, */*;q=0.1\r\nConnection: close\r\n"
                : L"Accept: application/geo+json, application/json;q=0.9, */*;q=0.1\r\n";
            if (WinHttpSendRequest(req, hdrs.c_str(), (DWORD)hdrs.size(), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(req, nullptr)) {
                DWORD status = 0, sz = sizeof(status);
                WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
                httpStatus = status;
                if (status != 200) {
                    answered = (status != 407);                       // real HTTP 4xx/5xx reply (not proxy auth): no need to switch routes
                    err = fmt("HTTP %lu", (unsigned long)status);
                } else {
                    bool readErr = false;
                    char chunk[65536];
                    for (;;) {
                        if (g_cancel) break;
                        DWORD avail = 0;
                        if (!WinHttpQueryDataAvailable(req, &avail)) { readErr = true; rawErr = GetLastError(); err = winErr(rawErr); break; }
                        if (avail == 0) break;
                        const DWORD toRead = std::min<DWORD>(avail, (DWORD)sizeof(chunk));
                        DWORD got = 0;
                        if (!WinHttpReadData(req, chunk, toRead, &got)) { readErr = true; rawErr = GetLastError(); err = winErr(rawErr); break; }
                        if (got == 0) break;
                        body.append(chunk, got);
                        if (body.size() > 6u * 1024 * 1024) { readErr = true; err = "response too large"; break; }
                    }
                    if (g_cancel) { body.clear(); err = "cancelled"; }
                    else if (readErr) { body.clear(); answered = false; }  // mid-stream drop: allow Pass 2 retry/fallback
                    else { answered = true; ok = true; }
                }
            } else {
                rawErr = GetLastError();
                err = g_cancel ? std::string("cancelled") : winErr(rawErr);
            }
        }
        reqGuard.close();
    } else { rawErr = GetLastError(); err = winErr(rawErr); }
    WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
    return ok;
}
} // namespace

bool httpGet(const std::wstring& host, const std::string& path, bool https, const std::string& ua, std::string& body, std::string& err) {
    const int order[3][3] = {{0, 1, 2}, {1, 0, 2}, {2, 0, 1}};
    const int good = std::min(2, std::max(0, g_goodMode.load()));
    std::string firstErr;
    DWORD lastStatus = 0, lastRawErr = 0;
    bool gotServerReply = false;

    // Pass 1: standard proxy order with OS-managed Schannel TLS
    for (int k = 0; k < 3; ++k) {
        if (g_cancel) { err = "cancelled"; return false; }
        int mode = order[good][k];
        if (mode == 1 && !systemProxy().have) continue;
        bool answered = false; DWORD st = 0, re = 0; std::string e;
        if (httpOnce(mode, false, host, path, https, ua, body, e, answered, st, re)) { g_goodMode = mode; err.clear(); return true; }
        if (g_cancel) { err = "cancelled"; return false; }
        if (firstErr.empty()) firstErr = e;
        lastStatus = st; lastRawErr = re;
        if (answered) {
            gotServerReply = true;
            // Brief retry ONLY on transient rate-limit/service-busy (429 / 503); permanent 404/401/403 return immediately.
            if (st == 429 || st == 503) {
                sleepInterruptible(600);
                if (!g_cancel && httpOnce(mode, false, host, path, https, ua, body, e, answered, st, re)) { g_goodMode = mode; err.clear(); return true; }
            }
            err = e;
            return false;
        }
    }

    // Pass 2: if a transport/TLS/abort failure occurred (e.g. E_ABORT 2147500036, 12175, 12030, 12002), stagger briefly
    // and recover via explicit TLS 1.2 + HTTP/1.1 + identity encoding + Connection: close.
    if (!gotServerReply && !g_cancel && lastRawErr != 12007 /* DNS not found */ && lastRawErr != 12163 /* no internet */) {
        sleepInterruptible(450);
        const int fbModes[2] = {2, 1};
        for (int m : fbModes) {
            if (g_cancel) { err = "cancelled"; return false; }
            if (m == 1 && !systemProxy().have) continue;
            bool answered = false; DWORD st = 0, re = 0; std::string e;
            if (httpOnce(m, true, host, path, https, ua, body, e, answered, st, re)) { g_goodMode = m; err.clear(); return true; }
            if (answered) { err = e; return false; }
        }
    }
    (void)lastStatus;
    err = firstErr.empty() ? "no connection" : firstErr;
    return false;
}

void cancelRequests() {
    g_cancel = true;
    std::vector<HINTERNET> toClose;
    {
        std::lock_guard<std::mutex> l(g_reqMu);
        for (ActiveHandle* slot : g_activeReqs) {
            if (slot->h) { toClose.push_back(slot->h); slot->h = nullptr; }
        }
    }
    for (HINTERNET h : toClose) WinHttpCloseHandle(h);
}
bool cancelled() { return g_cancel; }

bool fetchAir(double lat, double lon, std::string& airBody, std::string& err, const std::string& weatherApiKey, const std::string& openWeatherKey) {
    airBody.clear(); err.clear();
    const std::string ua = userAgentBase() + " (always-on clock)";
    lat = std::round(lat * 100.0) / 100.0; lon = std::round(lon * 100.0) / 100.0;
    std::string firstErr;

    // 1. Primary: Open-Meteo Air Quality (global, free, no API key)
    {
        std::string b, e;
        if (httpGet(L"air-quality-api.open-meteo.com", airPath(lat, lon), true, ua, b, e)) {
            Data probe; std::string pe;
            if (parseAir(b, probe, pe, lat, lon)) { airBody = b; return true; }
            firstErr = "Open-Meteo AQI: " + pe;
        } else if (!cancelled()) {
            firstErr = "Open-Meteo AQI: " + e;
        } else { err = "cancelled"; return false; }
    }

    // 2. Fallback: WeatherAPI.com Air Quality (when user entered a free WeatherAPI key)
    const std::string wk = trimKey(weatherApiKey);
    if (!wk.empty() && !cancelled()) {
        std::string b, e;
        const std::string p = fmt("/v1/current.json?key=%s&q=%.2f,%.2f&aqi=yes", urlEncode(wk).c_str(), lat, lon);
        if (httpGet(L"api.weatherapi.com", p, true, ua, b, e)) {
            Data probe; std::string pe;
            if (parseAir(b, probe, pe, lat, lon)) { airBody = b; return true; }
        }
    }

    // 3. Fallback: OpenWeatherMap Air Pollution (when user entered a free OWM key)
    const std::string ok = trimKey(openWeatherKey);
    if (!ok.empty() && !cancelled()) {
        std::string b, e;
        const std::string p = fmt("/data/2.5/air_pollution?lat=%.2f&lon=%.2f&appid=%s", lat, lon, urlEncode(ok).c_str());
        if (httpGet(L"api.openweathermap.org", p, true, ua, b, e)) {
            Data probe; std::string pe;
            if (parseAir(b, probe, pe, lat, lon)) { airBody = b; return true; }
        }
    }

    // 4. Free zero-key global backup: WAQI nearby station (strictly rejects stations > 500 km away)
    if (!cancelled()) {
        std::string b, e;
        const std::string p = fmt("/feed/geo:%.2f;%.2f/?token=demo", lat, lon);
        if (httpGet(L"api.waqi.info", p, true, ua, b, e)) {
            Data probe; std::string pe;
            if (parseAir(b, probe, pe, lat, lon)) { airBody = b; return true; }
        }
    }

    err = firstErr.empty() ? "air quality unavailable" : firstErr;
    return false;
}

bool fetchProvider(int id, double lat, double lon, const std::string& contact, std::string& body, std::string& airBody, std::string& err, const std::string& apiKey) {
    airBody.clear(); body.clear();
    // Contact in Settings > Weather is sent only to MET Norway and NWS (which ask apps to identify themselves).
    std::string safeContact;
    safeContact.reserve(contact.size());
    for (unsigned char ch : contact) if (ch >= 0x20u && ch != 0x7Fu) safeContact.push_back((char)ch);
    const std::string ua = userAgentBase() + " (always-on clock)";
    const std::string uaId = userAgentBase() + " (" + (safeContact.empty() ? std::string("always-on clock") : safeContact) + ")";
    // Two decimals (~1 km) protects exact location privacy while remaining finer than any forecast grid.
    lat = std::round(lat * 100.0) / 100.0; lon = std::round(lon * 100.0) / 100.0;
    switch (id) {
        case PV_OPENMETEO:
            // Forecast only — air quality is fetched separately after a calm stagger in Worker::run() to prevent connection bursts.
            return httpGet(L"api.open-meteo.com", forecastPath(lat, lon), true, ua, body, err);
        case PV_METNO:
            return httpGet(L"api.met.no", fmt("/weatherapi/locationforecast/2.0/compact?lat=%.2f&lon=%.2f", lat, lon), true, uaId, body, err);
        case PV_WTTR:
            return httpGet(L"wttr.in", fmt("/%.2f,%.2f?format=j1", lat, lon), true, ua, body, err);
        case PV_7TIMER:
            return httpGet(L"www.7timer.info", fmt("/bin/api.pl?lon=%.2f&lat=%.2f&product=civil&output=json", lon, lat), true, ua, body, err);
        case PV_BRIGHTSKY: {
            const double nowT = (double)time(nullptr);
            const util::LocalTime d0 = util::toLocal(nowT - 86400.0, 0);
            const util::LocalTime d1 = util::toLocal(nowT + 6.0 * 86400.0, 0);
            const std::string p = fmt("/weather?lat=%.2f&lon=%.2f&date=%04d-%02d-%02d&last_date=%04d-%02d-%02d&max_dist=100000",
                                      lat, lon, d0.y, d0.m, d0.d, d1.y, d1.m, d1.d);
            return httpGet(L"api.brightsky.dev", p, true, ua, body, err);
        }
        case PV_NWS: {
            std::string ptsBody;
            if (!httpGet(L"api.weather.gov", fmt("/points/%.2f,%.2f", lat, lon), true, uaId, ptsBody, err)) return false;
            mj::Value root; std::string pe;
            if (!mj::parse(ptsBody, root, pe)) { err = "invalid NWS grid reply"; return false; }
            const mj::Value* props = sect(root, "properties");
            const std::string url = strAt(props, "forecastHourly");
            const std::string prefix = "https://api.weather.gov";
            if (url.compare(0, prefix.size(), prefix) != 0 || url.size() <= prefix.size()) {
                err = "outside US / NWS coverage";
                return false;
            }
            return httpGet(L"api.weather.gov", url.substr(prefix.size()), true, uaId, body, err);
        }
        case PV_WEATHERAPI: {
            const std::string k = trimKey(apiKey);
            if (k.empty()) { err = "needs free API key"; return false; }
            const std::string p = fmt("/v1/forecast.json?key=%s&q=%.2f,%.2f&days=7&aqi=yes&alerts=no", urlEncode(k).c_str(), lat, lon);
            return httpGet(L"api.weatherapi.com", p, true, ua, body, err);
        }
        case PV_OPENWEATHER: {
            const std::string k = trimKey(apiKey);
            if (k.empty()) { err = "needs free API key"; return false; }
            const std::string p = fmt("/data/2.5/forecast?lat=%.2f&lon=%.2f&units=metric&appid=%s", lat, lon, urlEncode(k).c_str());
            return httpGet(L"api.openweathermap.org", p, true, ua, body, err);
        }
        default: err = "unknown provider"; return false;
    }
}
#endif

} // namespace weather
