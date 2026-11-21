// Regression tests for Meridian 2.9.7 "Snowfall":
//   - DST 48-hour notice & per-hour summary timestamps (strictly DST zones only; zero impact on non-DST zones like Sri Lanka)
//   - Snow vs rain summary wording + smart frost / wind-chill / heat / UV weather comfort cues
//   - Golden Hour (+6 deg) & Blue Hour (-4 deg) chronological spans + live/approaching Golden Hour Today cue
//   - Weekly alternating 6-week meteor direction cycle (Week 1 R->L, Week 2 L->R, Week 3 R->L, ..., Week 7 resets to Week 1)
#include "astro.h"
#include "daily.h"
#include "today.h"
#include "weather.h"
#include <cstdio>
#include <cmath>
#include <string>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

// Mock US Eastern (EST = -5h, EDT = -4h; 2026 spring forward: Mar 8 07:00 UTC, fall back: Nov 1 06:00 UTC)
static double kSpringUtc = 0;
static double kFallUtc   = 0;
static int tzUsEastern(double t) {
    return (t >= kSpringUtc && t < kFallUtc) ? -4 * 3600 : -5 * 3600;
}
// Mock Sri Lanka (constant UTC+5:30 year-round, zero DST)
static int tzSriLanka(double) { return 5 * 3600 + 1800; }

static bool hasTitleSub(const std::vector<today::TodayItem>& items, const char* sub) {
    for (const auto& it : items) if (it.title.find(sub) != std::string::npos) return true;
    return false;
}

int main() {
    kSpringUtc = astro::daysFromCivil(2026, 3, 8) * 86400.0 + 7 * 3600.0;
    kFallUtc   = astro::daysFromCivil(2026, 11, 1) * 86400.0 + 6 * 3600.0;

    // 1. DST Today card appears ONLY in DST timezones within 48h, NEVER in Sri Lanka (non-DST)
    {
        astro::Geo nyc{40.7128, -74.0060, "New York"};
        astro::Geo cmb{6.9271, 79.8612, "Colombo"};

        today::Input in;
        in.geo = nyc; in.h24 = false; in.poya = false; in.tzAt = tzUsEastern;
        // 18 hours before spring forward
        in.now = kSpringUtc - 18 * 3600.0; in.tzOff = tzUsEastern(in.now);
        CHECK(hasTitleSub(today::buildToday(in), "spring forward"));
        // 18 hours before fall back
        in.now = kFallUtc - 18 * 3600.0; in.tzOff = tzUsEastern(in.now);
        CHECK(hasTitleSub(today::buildToday(in), "fall back"));
        // Mid-summer: no DST notice
        in.now = astro::daysFromCivil(2026, 7, 15) * 86400.0 + 16 * 3600.0; in.tzOff = tzUsEastern(in.now);
        CHECK(!hasTitleSub(today::buildToday(in), "spring forward"));
        CHECK(!hasTitleSub(today::buildToday(in), "fall back"));

        // Non-DST zone (Sri Lanka): never shows any DST notice even on US transition dates
        in.geo = cmb; in.tzAt = tzSriLanka;
        in.now = kSpringUtc - 18 * 3600.0; in.tzOff = tzSriLanka(in.now);
        CHECK(!hasTitleSub(today::buildToday(in), "spring forward"));
        CHECK(!hasTitleSub(today::buildToday(in), "fall back"));
        in.now = kFallUtc - 18 * 3600.0; in.tzOff = tzSriLanka(in.now);
        CHECK(!hasTitleSub(today::buildToday(in), "spring forward"));
        CHECK(!hasTitleSub(today::buildToday(in), "fall back"));
    }

    // 2. Weather summary: snow wording + frost / wind-chill / heat / UV cues + DST-aware timestamps
    {
        const double t0 = astro::daysFromCivil(2026, 1, 15) * 86400.0 + 12 * 3600.0;
        // 2a. Snow + below freezing
        weather::Data d; d.valid = true; d.hasUv = true;
        d.cur.code = 73; d.cur.temp = -2.0; d.cur.feels = -7.0; d.cur.precip = 0.8;
        for (int i = 0; i < 12; ++i) {
            weather::Hour h; h.t = t0 + i * 3600.0; h.code = i < 4 ? 73 : 3;
            h.precipProb = i < 4 ? 85.0 : 10.0; h.precip = i < 4 ? 0.6 : 0.0;
            h.temp = -2.0 - 0.2 * i; h.feels = h.temp - 4.0; h.isDay = i < 5; h.uv = 1.0;
            d.hourly.push_back(h);
        }
        std::string s = weather::summarize(d, t0, 0, true, false);
        CHECK(s.find("Snow likely") != std::string::npos);
        CHECK(s.find("Rain likely") == std::string::npos);
        CHECK(s.find("Staying below freezing.") != std::string::npos);

        // 2b. Freezing tonight frost cue (starts above 0, drops <= 0)
        d.cur.code = 2; d.cur.temp = 4.0; d.cur.feels = 3.0; d.cur.precip = 0.0;
        for (size_t i = 0; i < d.hourly.size(); ++i) {
            d.hourly[i].code = 2; d.hourly[i].precipProb = 5.0; d.hourly[i].precip = 0.0;
            d.hourly[i].temp = 4.0 - 0.6 * (double)i; d.hourly[i].feels = d.hourly[i].temp - 1.0;
        }
        s = weather::summarize(d, t0, 0, true, false);
        CHECK(s.find("frost possible") != std::string::npos);

        // 2c. Wind-chill cue
        d.cur.temp = 3.0; d.cur.feels = -3.0;
        for (auto& h : d.hourly) { h.temp = 3.0; h.feels = -3.0; }
        s = weather::summarize(d, t0, 0, true, false);
        CHECK(s.find("in the wind.") != std::string::npos);

        // 2d. High heat index cue
        d.cur.temp = 33.0; d.cur.feels = 39.0;
        for (auto& h : d.hourly) { h.temp = 33.0; h.feels = 39.0; }
        s = weather::summarize(d, t0, 0, true, false);
        CHECK(s.find("High heat index.") != std::string::npos);

        // 2e. High UV cue
        d.cur.temp = 27.0; d.cur.feels = 28.0;
        for (size_t i = 0; i < d.hourly.size(); ++i) { d.hourly[i].temp = 27.0; d.hourly[i].feels = 28.0; d.hourly[i].isDay = true; d.hourly[i].uv = i == 2 ? 9.5 : 4.0; }
        s = weather::summarize(d, t0, 0, true, false);
        CHECK(s.find("High UV ahead.") != std::string::npos);

        // 2f. DST-aware weather summary timestamp across spring-forward (2:00 AM -> 3:00 AM EDT)
        weather::Data dst; dst.valid = true;
        const double pre = kSpringUtc - 2 * 3600.0;                     // 00:00 EST (UTC-5)
        dst.cur.code = 2; dst.cur.temp = 10.0; dst.cur.feels = 10.0;
        for (int i = 0; i < 8; ++i) {
            weather::Hour h; h.t = pre + i * 3600.0;
            h.code = i >= 3 ? 61 : 2; h.precipProb = i >= 3 ? 80.0 : 10.0; h.precip = i >= 3 ? 1.2 : 0.0;
            h.temp = 10.0; h.feels = 10.0;
            dst.hourly.push_back(h);
        }
        // i=3 is kSpringUtc + 1h = 08:00 UTC = 04:00 EDT (NOT 03:00 EST!)
        std::string sd = weather::summarize(dst, pre, tzUsEastern(pre), true, false, tzUsEastern);
        CHECK(sd.find("04:00") != std::string::npos);
    }

    // 3. Golden Hour (+6 deg) & Blue Hour (-4 deg) ordering + Today Golden Hour cue
    {
        astro::Geo lon{51.5072, -0.1276, "London"};
        const double mid = astro::daysFromCivil(2026, 3, 21) * 86400.0;
        const astro::DayEvents de = astro::dayEvents(mid, lon);
        CHECK(de.civil.hasRise && de.blue.hasRise && de.sun.hasRise && de.golden.hasRise);
        CHECK(de.golden.hasSet && de.sun.hasSet && de.blue.hasSet && de.civil.hasSet);
        CHECK(de.civil.rise < de.blue.rise && de.blue.rise < de.sun.rise && de.sun.rise < de.golden.rise);
        CHECK(de.golden.rise < de.golden.set && de.golden.set < de.sun.set && de.sun.set < de.blue.set && de.blue.set < de.civil.set);

        // Today card shows Golden hour while live or within 45 min
        today::Input in; in.geo = lon; in.tzOff = 0; in.h24 = true; in.poya = false;
        in.now = de.golden.set + 300.0;                                 // 5 minutes into evening golden hour
        CHECK(hasTitleSub(today::buildToday(in), "golden hour right now"));
        in.now = de.golden.set - 1200.0;                                // 20 minutes before evening golden hour
        CHECK(hasTitleSub(today::buildToday(in), "golden hour in"));
        in.now = mid + 12 * 3600.0;                                     // high noon: no golden hour cue
        CHECK(!hasTitleSub(today::buildToday(in), "golden hour"));
    }

    // 4. Weekly alternating 6-week meteor direction cycle (Week 1 R->L, Week 2 L->R, Week 3 R->L, ..., Week 7 resets to Week 1)
    {
        const long long jan1 = astro::daysFromCivil(2026, 1, 1);        // Day 0 of Week 1
        float s1, a1, s2, a2, s3, a3, s4, a4, s5, a5, s6, a6, s7, a7;
        daily::meteorWeekPattern(jan1 + 0 * 7, s1, a1);                 // Week 1
        daily::meteorWeekPattern(jan1 + 1 * 7, s2, a2);                 // Week 2
        daily::meteorWeekPattern(jan1 + 2 * 7, s3, a3);                 // Week 3
        daily::meteorWeekPattern(jan1 + 3 * 7, s4, a4);                 // Week 4
        daily::meteorWeekPattern(jan1 + 4 * 7, s5, a5);                 // Week 5
        daily::meteorWeekPattern(jan1 + 5 * 7, s6, a6);                 // Week 6
        daily::meteorWeekPattern(jan1 + 6 * 7, s7, a7);                 // Week 7 -> resets to Week 1!

        CHECK(s1 < 0.f);                                                // Week 1: right-to-left
        CHECK(s2 > 0.f);                                                // Week 2: left-to-right
        CHECK(s3 < 0.f && std::fabs(a3 - a1) < 1e-4f);                  // Week 3: same pattern as Week 1
        CHECK(s4 > 0.f);                                                // Week 4: left-to-right
        CHECK(s5 < 0.f);                                                // Week 5: right-to-left
        CHECK(s6 > 0.f);                                                // Week 6: left-to-right
        CHECK(daily::meteorWeekCycle(jan1 + 6 * 7) == 0);               // Week 7 resets to cycle 0
        CHECK(s7 == s1 && std::fabs(a7 - a1) < 1e-4f);                  // Week 7 identical to Week 1

        // Every day inside Week 1 stays strictly Right->Left (never flips mid-week)
        for (int d = 0; d < 7; ++d) {
            float sd, ad; daily::meteorWeekPattern(jan1 + d, sd, ad);
            CHECK(sd == s1 && std::fabs(ad - a1) < 1e-4f);
        }
        // Every day inside Week 2 stays strictly Left->Right
        for (int d = 7; d < 14; ++d) {
            float sd, ad; daily::meteorWeekPattern(jan1 + d, sd, ad);
            CHECK(sd == s2 && std::fabs(ad - a2) < 1e-4f);
        }
    }

    // 5. Multi-source Air Quality parser: Open-Meteo (current + hourly fallback), WeatherAPI, OWM, WAQI (+ 500km guard), normalized cache
    {
        weather::Data d1, d2, d3, d4, d5, d6, d7, d8; std::string err;
        CHECK(weather::parseAir(R"({"current":{"us_aqi":42,"pm2_5":9.5,"pm10":18}})", d1, err));
        CHECK(d1.hasAir && std::fabs(d1.aqi - 42.0) < 0.01 && d1.airSource == "Open-Meteo");
        CHECK(weather::parseAir(R"({"current":{"us_aqi":null,"european_aqi":35,"pm2_5":8.0}})", d2, err));
        CHECK(d2.hasAir && std::fabs(d2.aqi - 35.0) < 0.01);
        CHECK(weather::parseAir(R"({"current":{"us_aqi":null,"european_aqi":null,"pm2_5":12.0}})", d3, err));
        CHECK(d3.hasAir && std::fabs(d3.aqi - 50.0) < 0.5);
        // Open-Meteo hourly fallback when current is all null
        CHECK(weather::parseAir(R"({"utc_offset_seconds":0,"current":{"us_aqi":null,"pm2_5":null},"hourly":{"time":["2026-11-21T12:00","2026-11-21T13:00"],"us_aqi":[null,58],"pm2_5":[null,15.2]}})", d4, err));
        CHECK(d4.hasAir && std::fabs(d4.aqi - 58.0) < 0.1 && d4.airSource == "Open-Meteo");
        // WeatherAPI.com current.air_quality
        CHECK(weather::parseAir(R"({"current":{"air_quality":{"pm2_5":12.0,"pm10":20.0,"us-epa-index":1}}})", d5, err));
        CHECK(d5.hasAir && std::fabs(d5.aqi - 50.0) < 0.5 && d5.airSource == "WeatherAPI.com");
        // OpenWeatherMap Air Pollution
        CHECK(weather::parseAir(R"({"list":[{"main":{"aqi":2},"components":{"pm2_5":23.7,"pm10":31.0}}]})", d6, err));
        CHECK(d6.hasAir && d6.aqi >= 70.0 && d6.aqi <= 80.0 && d6.airSource == "OpenWeatherMap");
        // WAQI nearby station accepted, distant demo station rejected!
        const std::string waqiSample = R"({"status":"ok","data":{"aqi":63,"city":{"geo":[51.51,-0.12]},"iaqi":{"pm25":{"v":18.0},"pm10":{"v":24.0}}}})";
        CHECK(weather::parseAir(waqiSample, d7, err, 51.48, 0.0));
        CHECK(d7.hasAir && std::fabs(d7.aqi - 63.0) < 0.1 && d7.airSource == "WAQI");
        weather::Data dFar;
        CHECK(!weather::parseAir(waqiSample, dFar, err, 6.93, 79.85));  // London station must be rejected for Colombo!
        // Normalized cache round-trip preserves airSource & values
        const std::string norm = weather::formatAirJson(d7);
        CHECK(weather::parseAir(norm, d8, err));
        CHECK(d8.hasAir && std::fabs(d8.aqi - 63.0) < 0.1 && d8.airSource == "WAQI");
    }

    // 6. All 4 new weather fallback parsers: Bright Sky, NWS/NOAA, WeatherAPI.com (+ embedded AQI), OpenWeatherMap
    {
        std::string err;
        // Bright Sky (DWD)
        weather::Data bs; bs.utcOffset = 3600;
        const std::string bsJson = R"({"weather":[
            {"timestamp":"2026-11-21T09:00:00+00:00","temperature":7.5,"relative_humidity":82,"cloud_cover":75,"wind_speed":14.2,"wind_direction":240,"wind_gust_speed":24.0,"precipitation":0.6,"pressure_msl":1012,"condition":"rain","icon":"rain"},
            {"timestamp":"2026-11-21T12:00:00+00:00","temperature":9.0,"relative_humidity":78,"cloud_cover":60,"wind_speed":16.0,"wind_direction":250,"wind_gust_speed":26.0,"precipitation":0.0,"pressure_msl":1014,"condition":"dry","icon":"partly-cloudy-day"},
            {"timestamp":"2026-11-21T18:00:00+00:00","temperature":6.2,"relative_humidity":85,"cloud_cover":90,"wind_speed":12.0,"wind_direction":230,"wind_gust_speed":20.0,"precipitation":0.2,"pressure_msl":1015,"condition":"rain","icon":"rain"}
        ]})";
        CHECK(weather::parseProvider(weather::PV_BRIGHTSKY, bsJson, 52.52, 13.40, bs, err));
        CHECK(bs.valid && bs.source == "Bright Sky" && !bs.hourly.empty() && !bs.daily.empty());

        // NWS / NOAA (Fahrenheit -> Celsius, mph -> km/h, ISO offset)
        weather::Data nws;
        const std::string nwsJson = R"({"properties":{"periods":[
            {"startTime":"2026-11-21T08:00:00-05:00","isDaytime":true,"temperature":50,"temperatureUnit":"F","relativeHumidity":{"value":70},"probabilityOfPrecipitation":{"value":20},"windSpeed":"10 mph","windDirection":"NW","shortForecast":"Partly Cloudy"},
            {"startTime":"2026-11-21T11:00:00-05:00","isDaytime":true,"temperature":59,"temperatureUnit":"F","relativeHumidity":{"value":62},"probabilityOfPrecipitation":{"value":60},"windSpeed":"10 to 15 mph","windDirection":"W","shortForecast":"Light Rain"},
            {"startTime":"2026-11-21T14:00:00-05:00","isDaytime":true,"temperature":55,"temperatureUnit":"F","relativeHumidity":{"value":68},"probabilityOfPrecipitation":{"value":10},"windSpeed":"8 mph","windDirection":"NW","shortForecast":"Mostly Sunny"}
        ]}})";
        CHECK(weather::parseProvider(weather::PV_NWS, nwsJson, 38.89, -77.04, nws, err));
        CHECK(nws.valid && nws.source == "NWS / NOAA" && nws.utcOffset == -18000 && std::fabs(nws.hourly.front().temp - 10.0) < 3.0);

        // WeatherAPI.com (+ embedded Air Quality)
        weather::Data wa;
        const std::string waJson = R"({"location":{"tz_id":"Europe/London","localtime_epoch":1795262400,"localtime":"2026-11-21 12:00"},"current":{"last_updated_epoch":1795262400,"temp_c":14.5,"feelslike_c":13.8,"humidity":71,"pressure_mb":1016,"wind_kph":18.4,"wind_degree":210,"gust_kph":27.0,"cloud":50,"precip_mm":0.0,"is_day":1,"uv":3.0,"condition":{"code":1003},"air_quality":{"pm2_5":8.4,"pm10":14.0,"us-epa-index":1}},"forecast":{"forecastday":[{"hour":[
            {"time_epoch":1795258800,"temp_c":13.0,"feelslike_c":12.2,"humidity":74,"chance_of_rain":20,"chance_of_snow":0,"precip_mm":0.0,"cloud":45,"wind_kph":15.0,"uv":2.0,"is_day":1,"condition":{"code":1003}},
            {"time_epoch":1795262400,"temp_c":14.5,"feelslike_c":13.8,"humidity":71,"chance_of_rain":40,"chance_of_snow":0,"precip_mm":0.4,"cloud":70,"wind_kph":18.4,"uv":3.0,"is_day":1,"condition":{"code":1183}},
            {"time_epoch":1795266000,"temp_c":15.0,"feelslike_c":14.4,"humidity":68,"chance_of_rain":10,"chance_of_snow":0,"precip_mm":0.0,"cloud":30,"wind_kph":14.0,"uv":2.0,"is_day":1,"condition":{"code":1000}}
        ]}]}})";
        CHECK(weather::parseProvider(weather::PV_WEATHERAPI, waJson, 51.51, -0.12, wa, err));
        CHECK(wa.valid && wa.source == "WeatherAPI.com" && wa.hasAir && wa.airSource == "WeatherAPI.com" && wa.aqi >= 30.0 && wa.aqi <= 40.0);

        // OpenWeatherMap (5-day / 3-hour forecast)
        weather::Data ow;
        const std::string owJson = R"({"cod":"200","city":{"timezone":28800},"list":[
            {"dt":1795258800,"main":{"temp":24.2,"feels_like":25.0,"humidity":82,"pressure":1010},"clouds":{"all":85},"wind":{"speed":3.0,"deg":180,"gust":5.0},"pop":0.75,"rain":{"3h":3.6},"weather":[{"id":500}],"sys":{"pod":"d"}},
            {"dt":1795269600,"main":{"temp":22.8,"feels_like":23.4,"humidity":86,"pressure":1011},"clouds":{"all":92},"wind":{"speed":2.5,"deg":190,"gust":4.2},"pop":0.40,"rain":{"3h":0.9},"weather":[{"id":501}],"sys":{"pod":"n"}},
            {"dt":1795280400,"main":{"temp":21.5,"feels_like":22.0,"humidity":88,"pressure":1012},"clouds":{"all":60},"wind":{"speed":2.0,"deg":200,"gust":3.5},"pop":0.10,"weather":[{"id":802}],"sys":{"pod":"n"}}
        ]})";
        CHECK(weather::parseProvider(weather::PV_OPENWEATHER, owJson, 1.35, 103.82, ow, err));
        CHECK(ow.valid && ow.source == "OpenWeatherMap" && ow.utcOffset == 28800 && std::fabs(ow.cur.wind - 10.8) < 4.5);
    }

    if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
    std::printf("regression_297 ok\n");
    return 0;
}
