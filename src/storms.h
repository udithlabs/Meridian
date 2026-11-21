// Named storms (tropical cyclones, hurricanes, typhoons) near the user's place.
// Parsing and geometry are portable; the download (GDACS first, NASA EONET as a backup) is Windows only.
#pragma once
#include <string>
#include <vector>

namespace storms {

struct Storm {
    std::string name;            // "Montha"; empty when the agency has not named it yet
    std::string kind;            // "Cyclone", "Typhoon", "Hurricane", "Tropical storm", "Tropical depression"
    double lat = 0, lon = 0;     // latest position
    double windKmh = 0;          // sustained wind, 0 when unknown
    double updated = 0;          // unix time of that position, 0 when unknown
    std::string alert;           // GDACS alert colour (Green, Orange, Red) or empty
    double distKm = 0, bearing = 0;   // from the user's place, filled in by annotate()
};

struct Data {
    bool valid = false;
    double fetched = 0;
    std::string source;          // "GDACS" or "NASA EONET"
    std::vector<Storm> list;     // every active storm in the world
};

bool parseGdacs(const std::string& json, double now, std::vector<Storm>& out, std::string& err);
bool parseEonet(const std::string& json, double now, std::vector<Storm>& out, std::string& err);
bool parseAny(const std::string& json, double now, std::vector<Storm>& out, std::string& err);   // picks by shape (tests)

double distanceKm(double lat1, double lon1, double lat2, double lon2);
double bearingDeg(double lat1, double lon1, double lat2, double lon2);
const char* compass(double deg);                              // "NE"
void annotate(std::vector<Storm>& v, double lat, double lon); // distance and bearing, nearest first
std::vector<Storm> within(const std::vector<Storm>& v, double radiusKm);

std::string title(const Storm& s);                            // "Cyclone Montha"
std::string where(const Storm& s);                            // "820 km NE"
std::string winds(const Storm& s);                            // "winds 100 km/h" or ""

#ifdef _WIN32
bool fetch(double now, Data& out, std::string& err);
#endif

} // namespace storms
