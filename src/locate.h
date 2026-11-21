// Meridian "find my location": where you are, from Windows' own location service (GPS or Wi-Fi), or failing that from your internet address.
// The parsers at the top are portable and tested on any computer (tests/locate_test.cpp); the lookup itself is Windows only.
//
// How it is used: Settings > Place has a FIND MY LOCATION button, and a brand new install runs the same lookup once on its first start.
// The answer is saved in settings.ini and is never looked up again by itself: only pressing the button (or deleting settings.ini) asks again.
// A place you chose yourself (typed by hand, or a saved place you picked) is never replaced by the lookup that runs by itself.
#pragma once
#include <string>

namespace locate {

struct Found {
    bool ok = false;
    double lat = 0, lon = 0;
    std::string name;            // a short place name such as "London"; empty when none could be found
    std::string country;         // the two-letter country code ("LK", "GB", "US"); empty when not known
    std::string region;          // the state or nation ("Scotland", "Texas"); empty when not known
    std::string source;          // "Windows location" or "IP address"
    double accuracyM = -1;       // how far off it may be, in metres (-1: not known)
    std::string err;             // when ok is false: what went wrong, in a sentence for the user
};

// ---- parsers of the answers of the web services. Each fills what it can and returns true when it found a position (or a name).
bool parseIpWho(const std::string& json, Found& out);              // https://ipwho.is/
bool parseIpApiCo(const std::string& json, Found& out);            // https://ipapi.co/json/
bool parseFreeIp(const std::string& json, Found& out);             // https://freeipapi.com/api/json
// the reverse lookups also give the country code and the region when asked for them
bool parseBigData(const std::string& json, std::string& name, std::string* country = nullptr, std::string* region = nullptr);     // api.bigdatacloud.net
bool parseNominatim(const std::string& json, std::string& name, std::string* country = nullptr, std::string* region = nullptr);   // nominatim.openstreetmap.org

#ifdef _WIN32
// Blocking, takes up to about 25 seconds: run it on a worker thread. userAgent identifies the app to the web services.
// When allowIpOnDenied is false (used for automatic first-start lookups), an explicit Windows Location denial stops instead of calling online IP geolocation.
Found find(const std::string& userAgent, bool allowIpOnDenied = true);
#endif

} // namespace locate
