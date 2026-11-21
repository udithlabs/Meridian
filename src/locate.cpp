#include "locate.h"
#include "minijson.h"
#include <cmath>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
#include <initguid.h>          // so the ids of the location service (declared in locationapi.h) get defined in this file
#include <locationapi.h>
#include "weather.h"
#endif

namespace locate {

namespace {

bool parseJson(const std::string& s, mj::Value& v) { std::string e; return mj::parse(s, v, e) && v.type == mj::Value::Object; }
std::string strOf(const mj::Value& v, const char* k) { const mj::Value* x = v.get(k); return x && x->type == mj::Value::String ? x->str : std::string(); }
bool numOf(const mj::Value& v, const char* k, double& out) { const mj::Value* x = v.get(k); if (!x || x->type != mj::Value::Number) return false; out = x->num; return true; }
bool validPos(double la, double lo) { return std::fabs(la) <= 90.0 && std::fabs(lo) <= 180.0 && !(la == 0.0 && lo == 0.0); }
// a two-letter code in capitals ("gb" -> "GB"); anything that is not two letters gives ""
std::string upper2(const std::string& s) {
    if (s.size() != 2) return std::string();
    std::string r;
    for (char c : s) { if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return std::string(); r += (char)((c >= 'a' && c <= 'z') ? c - 32 : c); }
    return r;
}
std::string firstNonEmpty(const std::string& a, const std::string& b = "", const std::string& c = "", const std::string& d = "") {
    if (!a.empty()) return a;
    if (!b.empty()) return b;
    if (!c.empty()) return c;
    return d;
}

} // namespace

bool parseIpWho(const std::string& json, Found& out) {
    mj::Value v; if (!parseJson(json, v)) return false;
    const mj::Value* ok = v.get("success"); if (ok && ok->type == mj::Value::Bool && !ok->b) return false;
    double la, lo; if (!numOf(v, "latitude", la) || !numOf(v, "longitude", lo) || !validPos(la, lo)) return false;
    out.lat = la; out.lon = lo; out.name = firstNonEmpty(strOf(v, "city"), strOf(v, "region"), strOf(v, "country"));
    out.country = upper2(strOf(v, "country_code")); out.region = strOf(v, "region");
    return true;
}

bool parseIpApiCo(const std::string& json, Found& out) {
    mj::Value v; if (!parseJson(json, v)) return false;
    const mj::Value* er = v.get("error"); if (er && er->type == mj::Value::Bool && er->b) return false;
    double la, lo; if (!numOf(v, "latitude", la) || !numOf(v, "longitude", lo) || !validPos(la, lo)) return false;
    out.lat = la; out.lon = lo; out.name = firstNonEmpty(strOf(v, "city"), strOf(v, "region"), strOf(v, "country_name"));
    out.country = upper2(firstNonEmpty(strOf(v, "country_code"), strOf(v, "country"))); out.region = strOf(v, "region");
    return true;
}

bool parseFreeIp(const std::string& json, Found& out) {
    mj::Value v; if (!parseJson(json, v)) return false;
    double la, lo; if (!numOf(v, "latitude", la) || !numOf(v, "longitude", lo) || !validPos(la, lo)) return false;
    out.lat = la; out.lon = lo; out.name = firstNonEmpty(strOf(v, "cityName"), strOf(v, "regionName"), strOf(v, "countryName"));
    out.country = upper2(strOf(v, "countryCode")); out.region = strOf(v, "regionName");
    return true;
}

bool parseBigData(const std::string& json, std::string& name, std::string* country, std::string* region) {
    mj::Value v; if (!parseJson(json, v)) return false;
    name = firstNonEmpty(strOf(v, "city"), strOf(v, "locality"), strOf(v, "principalSubdivision"), strOf(v, "countryName"));
    if (country) *country = upper2(strOf(v, "countryCode"));
    if (region) *region = strOf(v, "principalSubdivision");
    return !name.empty();
}

bool parseNominatim(const std::string& json, std::string& name, std::string* country, std::string* region) {
    mj::Value v; if (!parseJson(json, v)) return false;
    const mj::Value* a = v.get("address");
    if (a && a->type == mj::Value::Object) {
        if (country) *country = upper2(strOf(*a, "country_code"));
        if (region) *region = firstNonEmpty(strOf(*a, "state"), strOf(*a, "region"), strOf(*a, "state_district"));
        name = firstNonEmpty(strOf(*a, "city"), strOf(*a, "town"), strOf(*a, "village"), strOf(*a, "hamlet"));
        if (name.empty()) name = firstNonEmpty(strOf(*a, "suburb"), strOf(*a, "county"), strOf(*a, "state_district"), strOf(*a, "state"));
    }
    if (name.empty()) name = strOf(v, "name");
    return !name.empty();
}

#ifdef _WIN32
namespace {

// Windows' own location service (the one behind Settings > Privacy & security > Location): GPS if the computer has it, otherwise nearby
// Wi-Fi networks. Needs "Location services" and "Let desktop apps access your location" to be on. Polls for up to about 15 seconds.
bool windowsLocation(double& lat, double& lon, double& accM, std::string& why, bool& denied) {
    denied = false;
    const HRESULT ci = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninit = SUCCEEDED(ci);                                  // RPC_E_CHANGED_MODE: someone else set it up; use it, do not undo it
    bool ok = false;
    ILocation* loc = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_Location, nullptr, CLSCTX_INPROC_SERVER, IID_ILocation, (void**)&loc);
    if (FAILED(hr) || !loc) { why = "this PC has no Windows location service"; if (uninit) CoUninitialize(); return false; }
    IID types[1] = {IID_ILatLongReport};
    loc->RequestPermissions(nullptr, types, 1, FALSE);                   // asks Windows to allow this app, where it still shows a prompt
    loc->SetDesiredAccuracy(IID_ILatLongReport, LOCATION_DESIRED_ACCURACY_HIGH);
    const ULONGLONG t0 = GetTickCount64();
    for (;;) {
        if (weather::cancelled()) { why = "cancelled"; break; }
        LOCATION_REPORT_STATUS status = REPORT_INITIALIZING;
        if (FAILED(loc->GetReportStatus(IID_ILatLongReport, &status))) { why = "Windows location status failed"; break; }
        if (status == REPORT_ACCESS_DENIED) { denied = true; why = "Windows is not letting desktop apps use your location"; break; }
        if (status == REPORT_NOT_SUPPORTED) { why = "this PC has no GPS or Wi-Fi to find its position with"; break; }
        if (status == REPORT_ERROR) { why = "Windows location service reported an error"; break; }
        ILocationReport* rep = nullptr;
        hr = loc->GetReport(IID_ILatLongReport, &rep);
        if (SUCCEEDED(hr) && rep) {
            ILatLongReport* ll = nullptr;
            if (SUCCEEDED(rep->QueryInterface(IID_ILatLongReport, (void**)&ll)) && ll) {
                DOUBLE la = 0, lo = 0, er = 0;
                if (SUCCEEDED(ll->GetLatitude(&la)) && SUCCEEDED(ll->GetLongitude(&lo)) && validPos(la, lo)) {
                    if (FAILED(ll->GetErrorRadius(&er))) er = -1;
                    lat = la; lon = lo; accM = er; ok = true;
                }
                ll->Release();
            }
            rep->Release();
            if (ok) break;
        }
        if (GetTickCount64() - t0 > 15000) { why = "Windows did not answer in time"; break; }
        for (int i = 0; i < 6 && !weather::cancelled(); ++i) Sleep(50);
    }
    loc->Release();
    if (uninit) CoUninitialize();
    return ok;
}

bool get(const wchar_t* host, const std::string& path, const std::string& ua, std::string& body) {
    std::string e;
    return weather::httpGet(host, path, true, ua, body, e);
}

std::string fmt1(const char* f, double a, double b) { char buf[160]; snprintf(buf, sizeof buf, f, a, b); return buf; }

// the name of the town at a position, and its country and region
std::string placeName(double lat, double lon, const std::string& ua, std::string& country, std::string& region) {
    std::string body, name;
    if (get(L"api.bigdatacloud.net", fmt1("/data/reverse-geocode-client?latitude=%.3f&longitude=%.3f&localityLanguage=en", lat, lon), ua, body) && parseBigData(body, name, &country, &region)) return name;
    body.clear();
    if (get(L"nominatim.openstreetmap.org", fmt1("/reverse?format=jsonv2&zoom=12&lat=%.3f&lon=%.3f", lat, lon), ua, body) && parseNominatim(body, name, &country, &region)) return name;
    return std::string();
}

} // namespace

Found find(const std::string& ua, bool allowIpOnDenied) {
    Found r;
    double lat = 0, lon = 0, acc = -1; std::string why; bool denied = false;
    if (windowsLocation(lat, lon, acc, why, denied)) {
        r.ok = true; r.lat = lat; r.lon = lon; r.accuracyM = acc; r.source = "Windows location";
    } else if (denied && !allowIpOnDenied) {
        r.err = why + ". Press FIND MY LOCATION in Settings > Place to use an IP lookup, or enter your place by hand.";
        return r;
    } else {
        // no Windows location: ask the internet where this connection is. City level only, and for some providers it is the provider's hub.
        std::string body; Found f;
        if ((get(L"ipwho.is", "/", ua, body) && parseIpWho(body, f)) ||
            (body.clear(), get(L"ipapi.co", "/json/", ua, body) && parseIpApiCo(body, f)) ||
            (body.clear(), get(L"freeipapi.com", "/api/json", ua, body) && parseFreeIp(body, f))) {
            r = f; r.ok = true; r.source = "IP address"; r.accuracyM = -1;
        } else {
            r.err = "Could not find your location: " + why + ", and the internet lookup failed too. Check the connection, and that Location is on in Windows Settings.";
            return r;
        }
    }
    std::string cc, reg;
    const std::string n = placeName(r.lat, r.lon, ua, cc, reg);
    if (!cc.empty()) {
        if (cc != r.country) r.region = reg;                // country changed: never retain a stale region from the old country
        else if (!reg.empty()) r.region = reg;
        r.country = cc;
    }
    if (!n.empty()) r.name = n;                  // the town name from the coordinates beats the one from the IP provider (it knows the real spot)
    if (r.name.empty()) r.name = "My location";
    return r;
}
#endif

} // namespace locate
