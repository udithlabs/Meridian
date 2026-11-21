// Tests for the parsers of "find my location" (src/locate.cpp). No Windows needed:
//   g++ -std=c++17 -I src tests/locate_test.cpp src/locate.cpp -o locate_test && ./locate_test
#include "locate.h"
#include <cstdio>
#include <cmath>
using namespace locate;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
int main() {
    { Found f; CHECK(parseIpWho(R"({"ip":"1.2.3.4","success":true,"type":"IPv4","country":"Sri Lanka","region":"Southern Province","city":"Galle","latitude":6.0535,"longitude":80.2210})", f));
      CHECK(f.name == "Galle" && std::fabs(f.lat - 6.0535) < 1e-6 && std::fabs(f.lon - 80.2210) < 1e-6); }
    { Found f; CHECK(!parseIpWho(R"({"success":false,"message":"Invalid IP address"})", f)); }
    { Found f; CHECK(!parseIpWho("not json", f)); }
    { Found f; CHECK(parseIpWho(R"({"success":true,"city":"","region":"","country":"Sri Lanka","latitude":7.0,"longitude":81.0})", f)); CHECK(f.name == "Sri Lanka"); }
    { Found f; CHECK(!parseIpWho(R"({"success":true,"latitude":0,"longitude":0})", f)); }                       // 0,0 is "unknown", not a place
    { Found f; CHECK(parseIpApiCo(R"({"ip":"1.2.3.4","city":"Colombo","region":"Western","country_name":"Sri Lanka","latitude":6.9344,"longitude":79.8428})", f)); CHECK(f.name == "Colombo"); }
    { Found f; CHECK(!parseIpApiCo(R"({"error":true,"reason":"RateLimited"})", f)); }
    { Found f; CHECK(parseFreeIp(R"({"ipVersion":4,"latitude":51.5,"longitude":-0.12,"countryName":"United Kingdom","cityName":"London","regionName":"England"})", f)); CHECK(f.name == "London" && f.lon < 0); }
    { std::string n; CHECK(parseBigData(R"({"latitude":6.93,"longitude":79.86,"countryName":"Sri Lanka","principalSubdivision":"Western Province","city":"Colombo","locality":"Colombo"})", n)); CHECK(n == "Colombo"); }
    { std::string n; CHECK(parseBigData(R"({"city":"","locality":"Greenwich","principalSubdivision":"England"})", n)); CHECK(n == "Greenwich"); }
    { std::string n; CHECK(!parseBigData(R"({"city":"","locality":"","principalSubdivision":"","countryName":""})", n)); }
    { std::string n; CHECK(parseNominatim(R"({"name":"","address":{"village":"Haworth","state_district":"West Yorkshire","country":"United Kingdom"}})", n)); CHECK(n == "Haworth"); }
    { std::string n; CHECK(parseNominatim(R"({"name":"Somewhere","address":{}})", n)); CHECK(n == "Somewhere"); }
    // country codes and regions (they decide which holidays a place gets)
    { Found f; CHECK(parseIpWho(R"({"success":true,"country":"United Kingdom","country_code":"GB","region":"Scotland","city":"Edinburgh","latitude":55.95,"longitude":-3.19})", f)); CHECK(f.country == "GB" && f.region == "Scotland"); }
    { Found f; CHECK(parseIpWho(R"({"success":true,"country":"Sri Lanka","country_code":"LK","region":"Western Province","city":"Colombo","latitude":6.93,"longitude":79.86})", f)); CHECK(f.country == "LK"); }
    { Found f; CHECK(parseIpApiCo(R"({"city":"Austin","region":"Texas","country_code":"US","country_name":"United States","latitude":30.27,"longitude":-97.74})", f)); CHECK(f.country == "US" && f.region == "Texas" && f.name == "Austin"); }
    { Found f; CHECK(parseFreeIp(R"({"cityName":"Belfast","regionName":"Northern Ireland","countryCode":"GB","countryName":"United Kingdom","latitude":54.6,"longitude":-5.93})", f)); CHECK(f.country == "GB" && f.region == "Northern Ireland"); }
    { Found f; CHECK(parseIpWho(R"({"success":true,"country_code":"gbr","latitude":51.5,"longitude":-0.1})", f)); CHECK(f.country.empty()); }        // not a two-letter code: ignored
    { std::string n, c, r; CHECK(parseBigData(R"({"city":"Glasgow","countryCode":"GB","principalSubdivision":"Scotland"})", n, &c, &r)); CHECK(n == "Glasgow" && c == "GB" && r == "Scotland"); }
    { std::string n, c, r; CHECK(parseNominatim(R"({"address":{"city":"Cardiff","state":"Wales","country_code":"gb"}})", n, &c, &r)); CHECK(n == "Cardiff" && c == "GB" && r == "Wales"); }
    { std::string n, c, r; CHECK(parseNominatim(R"({"name":"Somewhere","address":{}})", n, &c, &r)); CHECK(c.empty() && r.empty()); }
    printf(fails ? "%d FAILED\n" : "locate parsers: all passed\n", fails);
    return fails ? 1 : 0;
}
