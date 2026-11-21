// Regression tests for Meridian 2.9.5 ("Anchor") fixes: air-cache isolation, strict weather parsing, optional null vs zero.
#include "weather.h"
#include "storms.h"
#include <cstdio>
#include <cmath>
#include <string>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main() {
    // 1. Air quality cache must reject mismatched coordinates, stale entries and untagged legacy JSON
    {
        const std::string colomboAir = R"({"current":{"us_aqi":177,"pm2_5":68.2,"pm10":95.1}})";
        const double t0 = 1763700000.0;
        const std::string tagged = weather::formatAirCache(6.9271, 79.8612, t0, colomboAir);

        weather::Data london;
        std::string err;
        // Must NOT accept Colombo AQI for London
        CHECK(!weather::parseAirCache(tagged, 51.5072, -0.1276, t0 + 60.0, 6 * 3600.0, london, err));
        CHECK(!london.hasAir);

        // Must NOT accept stale Colombo AQI (> 6 hours old)
        weather::Data colStale;
        CHECK(!weather::parseAirCache(tagged, 6.9271, 79.8612, t0 + 7 * 3600.0, 6 * 3600.0, colStale, err));
        CHECK(!colStale.hasAir);

        // Must NOT accept untagged headerless JSON
        weather::Data untagged;
        CHECK(!weather::parseAirCache(colomboAir, 6.9271, 79.8612, t0 + 60.0, 6 * 3600.0, untagged, err));
        CHECK(!untagged.hasAir);

        // MUST accept fresh matching-city AQI
        weather::Data colOk;
        CHECK(weather::parseAirCache(tagged, 6.9271, 79.8612, t0 + 300.0, 6 * 3600.0, colOk, err));
        CHECK(colOk.hasAir && std::fabs(colOk.aqi - 177.0) < 0.01);
    }

    // 2. Open-Meteo response with null temperature_2m or null weather_code MUST be rejected (not accepted as 0C Clear sky)
    {
        const std::string nullBoth = R"({"utc_offset_seconds":0,"current":{"time":"2026-11-21T12:00","temperature_2m":null,"weather_code":null}})";
        const std::string nullCode = R"({"utc_offset_seconds":0,"current":{"time":"2026-11-21T12:00","temperature_2m":12.5,"weather_code":null}})";
        const std::string nullTemp = R"({"utc_offset_seconds":0,"current":{"time":"2026-11-21T12:00","temperature_2m":null,"weather_code":2}})";
        weather::Data d; std::string err;
        CHECK(!weather::parseForecast(nullBoth, d, err));
        CHECK(!weather::parseForecast(nullCode, d, err));
        CHECK(!weather::parseForecast(nullTemp, d, err));
    }

    // 3. Real zero readings must stay valid while absent/null optional fields are marked unavailable
    {
        const std::string realZero = R"({"utc_offset_seconds":0,"current":{"time":"2026-11-21T12:00","temperature_2m":0,"weather_code":0,"relative_humidity_2m":null,"wind_speed_10m":0,"cloud_cover":null,"is_day":1}})";
        weather::Data d; std::string err;
        CHECK(weather::parseForecast(realZero, d, err));
        CHECK(d.valid);
        CHECK(std::fabs(d.cur.temp - 0.0) < 1e-6);
        CHECK(d.cur.code == 0);
        CHECK(!d.cur.hasHumidity);
        CHECK(d.cur.hasWind && std::fabs(d.cur.wind - 0.0) < 1e-6);
        CHECK(!d.cur.hasCloud);
    }

    // 4. wttr.in missing temp_C must be rejected
    {
        const std::string badWttr = R"({"current_condition":[{"temp_C":"","weatherCode":"113"}],"weather":[{"date":"2026-11-21","hourly":[{"time":"0","tempC":"20"},{"time":"300","tempC":"21"}]}]})";
        weather::Data d; std::string err;
        CHECK(!weather::parseProvider(weather::PV_WTTR, badWttr, 51.5, -0.1, d, err));
    }

    if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
    std::printf("regression_295_test: all checks passed\n");
    return 0;
}
