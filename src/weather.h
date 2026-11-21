// Weather data from free and optional-key providers (Open-Meteo, MET Norway, wttr.in, 7Timer, Bright Sky, NWS/NOAA,
// WeatherAPI.com, OpenWeatherMap): parsing, a common data model, summary text and (on Windows) a hardened WinHTTP fetcher.
#pragma once
#include <string>
#include <vector>

namespace weather {

enum class Icon { ClearDay, ClearNight, PartlyDay, PartlyNight, Cloudy, Fog, Drizzle, Rain, Showers, Thunder, Snow };

struct Current {
    double time = 0;
    double temp = 0, feels = 0, humidity = 0, pressure = 0;
    double wind = 0, windDir = 0, gust = 0, cloud = 0, precip = 0;
    int code = 0;
    bool isDay = true;
    bool hasHumidity = true, hasWind = true, hasCloud = true;
};
struct Hour {
    double t = 0;
    double temp = 0, feels = 0, precipProb = 0, precip = 0, wind = 0, uv = 0, cloud = 0, humidity = 0;
    int code = 0;
    bool isDay = true;
};
struct Day {
    double t0 = 0;          // local midnight as unix
    double tmax = 0, tmin = 0, feelsMax = 0, feelsMin = 0;
    double precipSum = 0, precipProbMax = 0, uvMax = 0, windMax = 0, gustMax = 0;
    double sunrise = 0, sunset = 0;
    int code = 0;
};

struct Data {
    bool valid = false;
    double fetched = 0;               // unix time when downloaded
    int utcOffset = 0;                // seconds
    std::string timezone;
    Current cur;
    std::vector<Hour> hourly;
    std::vector<Day> daily;
    bool hasAir = false;
    double aqi = 0, pm25 = 0, pm10 = 0;
    std::string airSource;            // provider that supplied air quality (Open-Meteo, WeatherAPI.com, OpenWeatherMap, WAQI)
    bool hasUv = true;                // false when the provider does not report UV
    std::string source;               // provider that produced this data
};

// ---- providers (tried in this order; the first that answers wins) ----
enum Provider {
    PV_OPENMETEO = 0,
    PV_METNO,
    PV_WTTR,
    PV_7TIMER,
    PV_BRIGHTSKY,
    PV_NWS,
    PV_WEATHERAPI,
    PV_OPENWEATHER,
    PV_COUNT
};

// "Meridian/2.3.1": the first part of the User-Agent every request sends (weather, storms, location). The program sets it once
// at start. MET Norway and NWS ask apps to identify themselves with a contact too; that comes from Settings > Weather.
inline std::string& userAgentBase() { static std::string s = "Meridian"; return s; }
const char* providerName(int id);
const char* providerId(int id);          // short ini key: openmeteo, metno, wttr, 7timer, brightsky, nws, weatherapi, openweather
bool providerNeedsKey(int id);           // true for WeatherAPI.com and OpenWeatherMap (skipped automatically when blank)
struct ProviderStatus {
    bool enabled = true, tried = false, ok = false;
    double lastTry = 0, lastOk = 0;
    std::string err;
};
// Parse one provider's response body into the common model. Portable (used for the cache and tests).
bool parseProvider(int id, const std::string& body, double lat, double lon, Data& out, std::string& err);

struct Kind { Icon icon; const char* text; };
Kind describe(int wmoCode, bool isDay);
const char* aqiLabel(double usAqi);
const char* uvLabel(double uv);

bool parseForecast(const std::string& json, Data& out, std::string& err);
// Parse air quality JSON from Open-Meteo, WeatherAPI.com, OpenWeatherMap, WAQI, or normalized cache.
// When lat/lon are finite (within [-90,90]/[-180,180]), station-based feeds (WAQI) reject stations > 500 km away.
bool parseAir(const std::string& json, Data& out, std::string& err, double lat = 999.0, double lon = 999.0);
// Produce a compact normalized JSON snapshot of d's AQI fields so any AQI provider caches identically.
std::string formatAirJson(const Data& d);
// Tagged air-quality cache: stores coordinates and fetch time so a previous city's AQI can never leak into a new city.
std::string formatAirCache(double lat, double lon, double fetched, const std::string& json);
bool parseAirCache(const std::string& raw, double lat, double lon, double now, double maxAgeSec, Data& out, std::string& err);
std::string forecastPath(double lat, double lon);
std::string airPath(double lat, double lon);

// Plain-language summary of the next hours. useF formats temperatures in Fahrenheit; optional tzAt handles DST transitions cleanly.
std::string summarize(const Data& d, double now, int tzOff, bool h24, bool useF, int (*tzAt)(double) = nullptr);
double toUnit(double celsius, bool useF);

#ifdef _WIN32
// Download one provider's forecast. apiKey is used only for keyed providers (WeatherAPI.com, OpenWeatherMap).
// Tries system proxy, explicit proxy, direct connection, plus TLS 1.2 + HTTP/1.1 recovery on E_ABORT (2147500036).
bool fetchProvider(int id, double lat, double lon, const std::string& contact, std::string& body, std::string& airBody, std::string& err, const std::string& apiKey = "");
// Download air quality independently with multi-source fallback:
// Open-Meteo Air Quality -> WeatherAPI.com (if key set) -> OpenWeatherMap (if key set) -> WAQI nearby station.
bool fetchAir(double lat, double lon, std::string& airBody, std::string& err, const std::string& weatherApiKey = "", const std::string& openWeatherKey = "");
bool httpGet(const std::wstring& host, const std::string& path, bool https, const std::string& userAgent, std::string& body, std::string& err);
// The app is closing: from now on no request starts, a running one stops reading and every httpGet returns "cancelled" at once.
void cancelRequests();
bool cancelled();
#endif

} // namespace weather
