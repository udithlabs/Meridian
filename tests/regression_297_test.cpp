// Regression tests for Meridian 2.9.7 "Snowfall":
//   - DST 48-hour notice & per-hour summary timestamps (strictly DST zones only; zero impact on non-DST zones like Sri Lanka)
//   - Snow vs rain summary wording + smart frost / wind-chill / heat / UV weather comfort cues
//   - Golden Hour (+6 deg) & Blue Hour (-4 deg) chronological spans + live/approaching Golden Hour Today cue
//   - Weekly alternating 6-week meteor direction cycle (Week 1 R->L, Week 2 L->R, Week 3 R->L, ..., Week 7 resets to Week 1)
#include "astro.h"
#include "daily.h"
#include "dockanim.h"
#include "today.h"
#include "weather.h"
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <limits>
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
        astro::Geo nyc{40.7128, -74.0060, "New York", ""};
        astro::Geo cmb{6.9271, 79.8612, "Colombo", ""};

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
        astro::Geo lon{51.5072, -0.1276, "London", ""};
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
        CHECK(d3.hasAir && std::fabs(d3.aqi - 56.0) < 0.5 && d3.aqiScale == weather::AqiScale::Pm25Est);  // 2024 US EPA PM2.5: 12.0 ug/m3 = AQI 56
        // Open-Meteo hourly fallback when current is all null
        const double refNov21 = astro::daysFromCivil(2026, 11, 21) * 86400.0 + 14 * 3600.0;
        CHECK(weather::parseAir(R"({"utc_offset_seconds":0,"current":{"us_aqi":null,"pm2_5":null},"hourly":{"time":["2026-11-21T12:00","2026-11-21T13:00"],"us_aqi":[null,58],"pm2_5":[null,15.2]}})", d4, err, 999.0, 999.0, refNov21));
        CHECK(d4.hasAir && std::fabs(d4.aqi - 58.0) < 0.1 && d4.airSource == "Open-Meteo");
        // WeatherAPI.com current.air_quality
        CHECK(weather::parseAir(R"({"current":{"air_quality":{"pm2_5":12.0,"pm10":20.0,"us-epa-index":1}}})", d5, err));
        CHECK(d5.hasAir && std::fabs(d5.aqi - 56.0) < 0.5 && d5.airSource == "WeatherAPI.com");
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
        CHECK(wa.valid && wa.source == "WeatherAPI.com" && wa.hasAir && wa.airSource == "WeatherAPI.com" && wa.aqi >= 44.0 && wa.aqi <= 50.0);  // 2024 US EPA: 8.4 ug/m3 -> AQI 47

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

    // 7. 42-constellation star atlas integrity & year-round global visibility (North, Equatorial, South)
    {
        const auto& S = astro::stars();
        const auto& C = astro::constellations();
        CHECK(S.size() >= 200);
        CHECK(C.size() >= 40);
        for (const auto& con : C) {
            CHECK(!con.lines.empty());
            for (const auto& ln : con.lines) {
                CHECK(ln.a >= 0 && (size_t)ln.a < S.size());
                CHECK(ln.b >= 0 && (size_t)ln.b < S.size());
                CHECK(ln.a != ln.b);
            }
        }
        // Check 4 seasons (Jan, Apr, Jul, Nov) x 3 latitudes (London 51.5N, Tropics 7.0N, Sydney 33.9S)
        struct Site { double lat, lon; int tzH; };
        const Site sites[3] = {{51.48, 0.0, 0}, {7.0, 80.0, 5}, {-33.87, 151.21, 10}};
        const int months[4] = {1, 4, 7, 11};
        for (const Site& st : sites) {
            astro::Geo g{st.lat, st.lon, "Test", ""};
            for (int m : months) {
                const double tMid = astro::daysFromCivil(2026, m, 21) * 86400.0 + (23 - st.tzH) * 3600.0;
                int visibleCons = 0;
                for (const auto& con : C) {
                    int up = 0;
                    for (const auto& ln : con.lines) {
                        auto h1 = astro::toHorizontal(astro::precessJ2000({S[ln.a].raH * 15.0, S[ln.a].decDeg}, tMid), tMid, g);
                        auto h2 = astro::toHorizontal(astro::precessJ2000({S[ln.b].raH * 15.0, S[ln.b].decDeg}, tMid), tMid, g);
                        if (h1.alt >= 10.0 && h2.alt >= 10.0) ++up;
                    }
                    if (up >= 2) ++visibleCons;
                }
                CHECK(visibleCons >= 6);
            }
        }
    }

    // 8. Fluid Pill-to-Dock Morph Stress Test (all corners, 5Hz..360Hz, all screen sizes, hysteresis, click-gating, NaN/Inf resilience)
    {
        auto layoutScale = [](float W, float H) { return W < H * 0.90f ? W / 1100.f : std::min(H / 1080.f, W / 1440.f); };
        auto mkIn = [&](float W, float H, float mx, float my, float dt = 1.f / 60.f, float idle = 0.f, bool blocked = false, float expW = 0.f) {
            dockanim::Input in{};
            in.W = W; in.H = H; in.s = layoutScale(W, H);
            in.expW = expW > 0.f ? expW : std::min(W - 40.f * in.s, 1140.f * in.s);
            in.expH = 58.f * in.s;
            in.mouseX = mx; in.mouseY = my; in.dt = dt; in.idleSec = idle; in.blocked = blocked;
            return in;
        };

        // 8a. Corners (bottom-left weather chart, bottom-right Focus Orb / taskbar corner) must NEVER trigger dock
        {
            dockanim::State st{};
            const float pts[6][2] = {{20.f, 1075.f}, {200.f, 1060.f}, {650.f, 1070.f}, {1270.f, 1070.f}, {1880.f, 1075.f}, {960.f, 540.f}};
            for (auto& p : pts) {
                for (int i = 0; i < 30; ++i) {
                    auto f = dockanim::step(mkIn(1920, 1080, p[0], p[1]), st);
                    CHECK(!f.inPill && st.target == 0.f && st.t == 0.f && !f.canHit);
                }
            }
        }

        // 8b. Hovering pill center + forgiving edges expands monotonically to full dock; stationary hover for 20s stays open & visible
        {
            dockanim::State st{};
            float prevT = -1.f;
            for (int i = 0; i < 60; ++i) {
                auto f = dockanim::step(mkIn(1920, 1080, 960.f, 1062.f, 1.f / 60.f, 0.f), st);
                CHECK(f.inPill && st.target == 1.f);
                CHECK(st.t >= prevT - 1e-6f);
                prevT = st.t;
            }
            CHECK(st.t == 1.f);
            // Hold completely motionless on the pill spot for 20s (idle >> 3.1s): dock MUST stay open and 100% opaque
            auto fHold = dockanim::step(mkIn(1920, 1080, 960.f, 1062.f, 1.f / 60.f, 20.f), st);
            CHECK(st.target == 1.f && st.t == 1.f && fHold.visible && fHold.vis > 0.99f && fHold.contentA > 0.99f && fHold.canHit);
        }

        // 8c. High-Hz (360Hz, 144Hz, 60Hz, 30Hz, lag-spike 5Hz) instant diagonal dash from pill -> SKY (leftmost) and ? (rightmost) + 0-dt guard
        {
            const float dts[5] = {1.f / 360.f, 1.f / 144.f, 1.f / 60.f, 1.f / 30.f, 0.2f};
            for (float dt : dts) {
                for (int side = 0; side < 2; ++side) {
                    dockanim::State st{};
                    // Trigger on a 0-dt paint frame first to test the worst-case race
                    dockanim::step(mkIn(1920, 1080, 960.f, 1078.f, 0.f), st);
                    CHECK(st.target == 1.f);
                    // Next frame, pointer instantly teleports diagonally to leftmost SKY or rightmost ?
                    const float targetX = side == 0 ? 410.f : 1510.f;
                    const float targetY = 1020.f;
                    int steps = (int)std::ceil(1.2f / dt) + 5;
                    for (int i = 0; i < steps; ++i) {
                        auto f = dockanim::step(mkIn(1920, 1080, targetX, targetY, dt), st);
                        CHECK(st.target == 1.f);
                        (void)f;
                    }
                    CHECK(st.t == 1.f);
                }
            }
        }

        // 8d. Frame-rate independence: exact 0.20s elapsed at 30Hz (6f), 60Hz (12f), 120Hz (24f), 240Hz (48f) must match tightly
        {
            auto simOpen = [&](int hz) {
                dockanim::State st{};
                const float dt = 1.f / (float)hz;
                const int n = (int)std::lround(0.20f * hz);
                for (int i = 0; i < n; ++i) dockanim::step(mkIn(1920, 1080, 960.f, 1065.f, dt), st);
                return st.t;
            };
            const float t30 = simOpen(30), t60 = simOpen(60), t120 = simOpen(120), t240 = simOpen(240);
            CHECK(std::fabs(t30 - t60) < 1e-4f && std::fabs(t60 - t120) < 1e-4f && std::fabs(t120 - t240) < 1e-4f);
        }

        // 8e. Clicking THEME shrinks dock width by 50px while hovering leftmost/rightmost edge button -> must stay open
        {
            dockanim::State st{1.f, 1.f};
            auto fWide = dockanim::step(mkIn(1920, 1080, 410.f, 1022.f, 1.f / 60.f, 0.f, false, 1140.f), st);
            CHECK(st.target == 1.f && fWide.inExp);
            auto fNarrow = dockanim::step(mkIn(1920, 1080, 410.f, 1022.f, 1.f / 60.f, 0.f, false, 1090.f), st);
            CHECK(st.target == 1.f && fNarrow.inExp && st.t == 1.f);
        }

        // 8f. Smooth collapse on leave, mid-collapse catch, and late-collapse far-side safety
        {
            dockanim::State st{1.f, 1.f};
            // Leave upwards -> collapses smoothly
            for (int i = 0; i < 9; ++i) dockanim::step(mkIn(1920, 1080, 960.f, 600.f, 1.f / 60.f), st);
            CHECK(st.target == 0.f && st.t > 0.20f && st.t < 0.55f);
            // Dip back into visible shrinking capsule -> smoothly catches and re-opens!
            auto fCatch = dockanim::step(mkIn(1920, 1080, 960.f, 1045.f, 1.f / 60.f), st);
            CHECK(st.target == 1.f && fCatch.visible);
            for (int i = 0; i < 60; ++i) dockanim::step(mkIn(1920, 1080, 960.f, 1045.f, 1.f / 60.f), st);
            CHECK(st.t == 1.f);
            // Collapse almost fully (st.t < 0.10): moving mouse to far-left old button x=420 must NOT pop dock back open
            for (int i = 0; i < 22; ++i) dockanim::step(mkIn(1920, 1080, 960.f, 400.f, 1.f / 60.f), st);
            CHECK(st.t < 0.10f && st.t > 0.f);
            dockanim::step(mkIn(1920, 1080, 420.f, 1040.f, 1.f / 60.f), st);
            CHECK(st.target == 0.f);
            for (int i = 0; i < 60; ++i) dockanim::step(mkIn(1920, 1080, 420.f, 1040.f, 1.f / 60.f), st);
            CHECK(st.t == 0.f);
        }

        // 8g. WM_MOUSELEAVE (-1,-1), stale fullscreen-exit coords (mouseY > H), and blocked overlays (Settings/Help/Saver)
        {
            dockanim::State st{1.f, 1.f};
            for (int i = 0; i < 120; ++i) dockanim::step(mkIn(1920, 1080, -1.f, -1.f, 1.f / 60.f), st);
            CHECK(st.t == 0.f && st.target == 0.f);

            // Stale fullscreen coordinate (960, 1060) on a smaller 1280x720 window -> must collapse, NEVER stay stuck open
            dockanim::State stFs{1.f, 1.f};
            auto fFs = dockanim::step(mkIn(1280, 720, 960.f, 1060.f, 1.f / 60.f), stFs);
            CHECK(!fFs.inPill && !fFs.inExp && stFs.target == 0.f);

            // Blocked overlay -> immediate zeroed state, invisible, zero click targets
            dockanim::State stBlk{0.9f, 1.f};
            auto fBlk = dockanim::step(mkIn(1920, 1080, 960.f, 1062.f, 1.f / 60.f, 0.f, true), stBlk);
            CHECK(stBlk.t == 0.f && stBlk.target == 0.f && !fBlk.visible && !fBlk.canHit && !fBlk.swallowClick && !fBlk.busy);
        }

        // 8h. Click-gating safety: outer buttons blocked while capsule is still expanding, all buttons clickable when open
        {
            dockanim::State st{};
            auto in = mkIn(1920, 1080, 960.f, 1062.f, 1.f / 60.f, 0.f, false, 1140.f);
            in.overrideT = 0.30f;
            auto fEarly = dockanim::step(in, st);
            CHECK(!fEarly.canHit && !dockanim::buttonClickable(fEarly, 920.f, 80.f, 1.f));
            in.overrideT = 0.65f;
            auto fMid = dockanim::step(in, st);
            CHECK(fMid.canHit);
            CHECK(dockanim::buttonClickable(fMid, 920.f, 80.f, 1.f));    // center button inside capsule -> clickable
            CHECK(!dockanim::buttonClickable(fMid, 405.f, 70.f, 1.f));   // outer button still outside capsule -> blocked!
            in.overrideT = 1.0f;
            auto fFull = dockanim::step(in, st);
            CHECK(dockanim::buttonClickable(fFull, 405.f, 70.f, 1.f) && dockanim::buttonClickable(fFull, 1455.f, 60.f, 1.f));
        }

        // 8i. 12,000-frame chaotic fuzz & resize stress test (560x360 .. 3840x2160, portrait, NaN/Inf inputs, violent jiggling)
        {
            const float sizes[7][2] = {{560, 360}, {800, 600}, {1080, 1920}, {1280, 720}, {1920, 1080}, {2560, 1440}, {3840, 2160}};
            uint32_t rng = 0x9E3779B9u;
            auto nextf = [&]() {
                rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                return (float)(rng & 0xFFFFu) / 65535.f;
            };
            dockanim::State st{};
            for (int iter = 0; iter < 12000; ++iter) {
                const auto& sz = sizes[iter % 7];
                const float W = sz[0], H = sz[1];
                float mx = (iter % 19 == 0) ? -1.f : (nextf() * (W + 120.f) - 40.f);
                float my = (iter % 19 == 0) ? -1.f : (nextf() * (H + 120.f) - 40.f);
                float dt = 0.0005f + nextf() * 0.25f;
                float idle = nextf() * 6.f;
                bool blk = (iter % 53 == 0);
                auto in = mkIn(W, H, mx, my, dt, idle, blk, W * (0.4f + nextf() * 0.9f));
                if (iter == 500) { in.mouseX = std::numeric_limits<float>::quiet_NaN(); in.dt = std::numeric_limits<float>::infinity(); }
                auto f = dockanim::step(in, st);
                CHECK(std::isfinite(st.t) && st.t >= 0.f && st.t <= 1.f);
                CHECK(std::isfinite(f.curX) && std::isfinite(f.curY) && std::isfinite(f.curW) && std::isfinite(f.curH) && std::isfinite(f.curR));
                CHECK(f.curW >= f.pillW - 1e-3f && f.curW <= f.expW + 1e-3f);
                CHECK(f.curH >= f.pillH - 1e-3f && f.curH <= f.expH + 1e-3f);
                CHECK(std::fabs(f.curR - f.curH * 0.5f) < 1e-4f);
                CHECK(f.curX >= -1e-3f && f.curX + f.curW <= W + 1e-3f);
                CHECK(f.curY >= -1e-3f && f.curY + f.curH <= H + 1e-3f);
                if (f.e <= 0.62f) CHECK(!f.canHit);
            }
        }
    }

    if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
    std::printf("regression_297 ok\n");
    return 0;
}
