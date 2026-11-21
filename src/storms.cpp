#include "storms.h"
#include "minijson.h"
#include "astro.h"
#include "util.h"
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <cctype>
#ifdef _WIN32
#include "weather.h"
#endif

namespace storms {

using util::fmt;

namespace {

const double PI = 3.14159265358979323846;
double rad(double d) { return d * PI / 180.0; }

// "2026-10-26T06:00:00" or "...Z": UTC to unix; 0 when unreadable
double parseTime(const std::string& s) {
    int y, mo, d, hh = 0, mm = 0, ss = 0;
    if (sscanf(s.c_str(), "%d-%d-%d%*[T ]%d:%d:%d", &y, &mo, &d, &hh, &mm, &ss) >= 3 && y > 1990)
        return astro::daysFromCivil(y, mo, d) * 86400.0 + hh * 3600.0 + mm * 60.0 + ss;
    return 0;
}

std::string lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }

// "MONTHA" -> "Montha", "MAN-YI" -> "Man-Yi"
std::string titleCase(const std::string& s) {
    std::string o; bool up = true;
    for (unsigned char c : s) {
        if (std::isalpha(c)) { o.push_back((char)(up ? std::toupper(c) : std::tolower(c))); up = false; }
        else { o.push_back((char)c); up = (c == ' ' || c == '-' || c == '\''); }
    }
    return o;
}

std::string trim(std::string s) {
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    size_t i = 0; while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    return s.substr(i);
}

// drop a trailing season suffix: "MONTHA-25" -> "MONTHA"
std::string stripYear(std::string s) {
    size_t d = s.rfind('-');
    if (d != std::string::npos && d + 1 < s.size() && s.size() - d - 1 <= 2) {
        bool digits = true; for (size_t i = d + 1; i < s.size(); ++i) if (!std::isdigit((unsigned char)s[i])) digits = false;
        if (digits) s.erase(d);
    }
    return s;
}

// "Tropical Cyclone MONTHA" -> "MONTHA"; keeps the rest when no known prefix is found
std::string stripKind(const std::string& title) {
    static const char* pre[] = {"super typhoon ", "tropical cyclone ", "severe tropical storm ", "tropical storm ", "tropical depression ", "hurricane ", "typhoon ", "cyclone ", "storm "};
    std::string t = trim(title), l = lower(t);
    for (const char* p : pre) { size_t n = std::char_traits<char>::length(p); if (l.compare(0, n, p) == 0) return trim(t.substr(n)); }
    return t;
}

std::string kindFor(double windKmh, double lat, double lon) {
    if (windKmh <= 0) return "Tropical storm";
    if (windKmh < 62) return "Tropical depression";
    if (windKmh < 118) return "Tropical storm";
    if (lat > 0 && lon >= 100 && lon <= 180) return "Typhoon";
    if (lat > 0 && (lon <= 0 || lon > 180)) return "Hurricane";
    return "Cyclone";
}

double numberIn(const std::string& s) {        // wind speed in "maximum wind speed of 111 km/h" (skipping "Category 4" etc.)
    const std::string l = lower(s);
    auto findWithUnit = [&](const char* unit, double scale) -> double {
        size_t pos = 0;
        while ((pos = l.find(unit, pos)) != std::string::npos) {
            size_t j = pos;
            while (j > 0 && std::isspace((unsigned char)l[j - 1])) --j;
            size_t end = j;
            while (j > 0 && (std::isdigit((unsigned char)l[j - 1]) || l[j - 1] == '.')) --j;
            if (j < end) { double v = atof(l.c_str() + j); if (v > 0) return v * scale; }
            pos += 1;
        }
        return 0;
    };
    if (double v = findWithUnit("km/h", 1.0); v > 0) return v;
    if (double v = findWithUnit("kph", 1.0); v > 0) return v;
    if (double v = findWithUnit("knots", 1.852); v > 0) return v;
    if (double v = findWithUnit("kts", 1.852); v > 0) return v;
    if (double v = findWithUnit("kt", 1.852); v > 0) return v;
    if (double v = findWithUnit("mph", 1.609344); v > 0) return v;
    if (double v = findWithUnit("m/s", 3.6); v > 0) return v;
    return 0;
}

bool isUnnamed(const std::string& n) {
    if (n.empty()) return true;
    std::string l = lower(n);
    return l == "unnamed" || l == "unknown" || l == "invest" || l.compare(0, 3, "td-") == 0 || l.compare(0, 6, "invest") == 0;
}

} // namespace

// ---------------------------------------------------------------- GDACS (GeoJSON feature list)
bool parseGdacs(const std::string& json, double now, std::vector<Storm>& out, std::string& err) {
    mj::Value root;
    if (!mj::parse(json, root, err)) return false;
    const mj::Value* feats = root.get("features");
    if (!feats || feats->type != mj::Value::Array) { err = "no features"; return false; }
    std::vector<std::string> seen;
    for (const mj::Value& f : feats->arr) {
        const mj::Value* pr = f.get("properties"); const mj::Value* ge = f.get("geometry");
        if (!pr || !ge) continue;
        if (const mj::Value* et = pr->get("eventtype")) if (et->type == mj::Value::String && et->str != "TC") continue;
        const mj::Value* gt = ge->get("type");
        if (gt && gt->type == mj::Value::String && gt->str != "Point") continue;          // tracks and wind areas are drawn as other shapes
        const mj::Value* co = ge->get("coordinates");
        if (!co || co->type != mj::Value::Array || co->arr.size() < 2) continue;
        if (co->arr[0].type != mj::Value::Number || co->arr[1].type != mj::Value::Number) continue;
        Storm s; s.lon = co->arr[0].num; s.lat = co->arr[1].num;
        if (std::fabs(s.lat) > 90.0 || std::fabs(s.lon) > 180.0) continue;
        if (const mj::Value* ic = pr->get("iscurrent")) {
            const bool cur = ic->type == mj::Value::Bool ? ic->b : (ic->type == mj::Value::String ? lower(ic->str) == "true" : true);
            if (!cur) continue;
        }
        double to = 0;
        if (const mj::Value* v = pr->get("todate")) if (v->type == mj::Value::String) to = parseTime(v->str);
        if (to <= 0) if (const mj::Value* v = pr->get("fromdate")) if (v->type == mj::Value::String) to = parseTime(v->str);
        if (to > 0 && now - to > 72 * 3600.0) continue;                                  // an old advisory
        s.updated = to;
        std::string nm;
        if (const mj::Value* v = pr->get("eventname")) if (v->type == mj::Value::String) nm = trim(v->str);
        if (nm.empty()) if (const mj::Value* v = pr->get("name")) if (v->type == mj::Value::String) nm = stripKind(v->str);
        if (isUnnamed(nm)) s.name.clear();
        else { nm = stripYear(nm); s.name = isUnnamed(nm) ? "" : titleCase(nm); }
        if (const mj::Value* sd = pr->get("severitydata")) {
            if (const mj::Value* v = sd->get("severity")) s.windKmh = v->type == mj::Value::Number ? v->num : atof(v->str.c_str());
            if (s.windKmh <= 0) if (const mj::Value* v = sd->get("severitytext")) if (v->type == mj::Value::String) s.windKmh = numberIn(v->str);
        }
        if (const mj::Value* v = pr->get("alertlevel")) if (v->type == mj::Value::String) s.alert = v->str;
        s.kind = kindFor(s.windKmh, s.lat, s.lon);
        std::string key = s.name + fmt("|%.1f|%.1f", s.lat, s.lon);
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
        seen.push_back(key);
        out.push_back(s);
    }
    return true;
}

// ---------------------------------------------------------------- NASA EONET (events with a track of dated points)
bool parseEonet(const std::string& json, double now, std::vector<Storm>& out, std::string& err) {
    mj::Value root;
    if (!mj::parse(json, root, err)) return false;
    const mj::Value* evs = root.get("events");
    if (!evs || evs->type != mj::Value::Array) { err = "no events"; return false; }
    for (const mj::Value& e : evs->arr) {
        const mj::Value* cl = e.get("closed");
        if (cl && cl->type == mj::Value::String && !cl->str.empty()) continue;
        std::string title; if (const mj::Value* v = e.get("title")) if (v->type == mj::Value::String) title = v->str;
        const std::string lt = lower(title);
        if (lt.find("cyclone") == std::string::npos && lt.find("hurricane") == std::string::npos && lt.find("typhoon") == std::string::npos &&
            lt.find("tropical") == std::string::npos && lt.find("storm") == std::string::npos) continue;
        const mj::Value* geo = e.get("geometry");
        if (!geo || geo->type != mj::Value::Array) continue;
        const mj::Value* best = nullptr; double bestT = -1;
        for (const mj::Value& g : geo->arr) {
            const mj::Value* d = g.get("date"); const mj::Value* co = g.get("coordinates"); const mj::Value* ty = g.get("type");
            if (!d || d->type != mj::Value::String || !co || co->type != mj::Value::Array || co->arr.size() < 2) continue;
            if (co->arr[0].type != mj::Value::Number || co->arr[1].type != mj::Value::Number) continue;
            if (ty && ty->type == mj::Value::String && ty->str != "Point") continue;
            const double t = parseTime(d->str);
            if (t <= 0 || t > now + 6 * 3600.0) continue;                                // unreadable or forecast point
            if (t > bestT) { bestT = t; best = &g; }
        }
        if (!best || bestT <= 0 || now - bestT > 72 * 3600.0) continue;
        Storm s; s.lon = best->get("coordinates")->arr[0].num; s.lat = best->get("coordinates")->arr[1].num; s.updated = bestT;
        if (std::fabs(s.lat) > 90.0 || std::fabs(s.lon) > 180.0) continue;
        if (const mj::Value* mv = best->get("magnitudeValue"); mv && mv->type == mj::Value::Number && mv->num > 0) {
            double v = mv->num; std::string u; if (const mj::Value* mu = best->get("magnitudeUnit")) u = lower(trim(mu->str));
            if (u == "kts" || u == "kt" || u == "knots") s.windKmh = v * 1.852;
            else if (u == "mph") s.windKmh = v * 1.609344;
            else if (u == "m/s") s.windKmh = v * 3.6;
            else if (u == "km/h" || u == "kph" || u == "kmh") s.windKmh = v;
        }
        std::string nm = stripKind(title);
        // some titles read "Tropical Cyclone MONTHA-25"
        if (isUnnamed(nm)) s.name.clear();
        else { nm = stripYear(nm); s.name = isUnnamed(nm) ? "" : titleCase(nm); }
        s.kind = kindFor(s.windKmh, s.lat, s.lon);
        out.push_back(s);
    }
    return true;
}

bool parseAny(const std::string& json, double now, std::vector<Storm>& out, std::string& err) {
    if (json.find("\"features\"") != std::string::npos) return parseGdacs(json, now, out, err);
    return parseEonet(json, now, out, err);
}

// ---------------------------------------------------------------- geometry
double distanceKm(double lat1, double lon1, double lat2, double lon2) {
    const double dl = rad(lat2 - lat1), dn = rad(lon2 - lon1);
    const double a = std::sin(dl / 2) * std::sin(dl / 2) + std::cos(rad(lat1)) * std::cos(rad(lat2)) * std::sin(dn / 2) * std::sin(dn / 2);
    return 6371.0 * 2.0 * std::atan2(std::sqrt(a), std::sqrt(std::max(0.0, 1.0 - a)));
}
double bearingDeg(double lat1, double lon1, double lat2, double lon2) {
    const double dn = rad(lon2 - lon1);
    const double y = std::sin(dn) * std::cos(rad(lat2));
    const double x = std::cos(rad(lat1)) * std::sin(rad(lat2)) - std::sin(rad(lat1)) * std::cos(rad(lat2)) * std::cos(dn);
    double b = std::atan2(y, x) * 180.0 / PI;
    return b < 0 ? b + 360.0 : b;
}
const char* compass(double deg) {
    static const char* n[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    return n[((int)std::floor(deg / 45.0 + 0.5)) & 7];
}
void annotate(std::vector<Storm>& v, double lat, double lon) {
    for (auto& s : v) { s.distKm = distanceKm(lat, lon, s.lat, s.lon); s.bearing = bearingDeg(lat, lon, s.lat, s.lon); }
    std::stable_sort(v.begin(), v.end(), [](const Storm& a, const Storm& b) { return a.distKm < b.distKm; });
}
std::vector<Storm> within(const std::vector<Storm>& v, double radiusKm) {
    std::vector<Storm> o; for (const auto& s : v) if (s.distKm <= radiusKm) o.push_back(s); return o;
}

std::string title(const Storm& s) { return s.name.empty() ? s.kind : s.kind + " " + s.name; }
std::string where(const Storm& s) {
    const double d = s.distKm < 100 ? std::round(s.distKm) : std::round(s.distKm / 10.0) * 10.0;
    return fmt("%.0f km %s", d, compass(s.bearing));
}
std::string winds(const Storm& s) { return s.windKmh > 0 ? fmt("winds %.0f km/h", std::round(s.windKmh / 5.0) * 5.0) : std::string(); }

// ---------------------------------------------------------------- download
#ifdef _WIN32
namespace {
std::string day(double t, int plus) {
    double d = std::floor(t / 86400.0) + plus; int y, m, dd; astro::civilFromDays((long long)d, y, m, dd);
    return fmt("%04d-%02d-%02d", y, m, dd);
}
}
bool fetch(double now, Data& out, std::string& err) {
    const std::string ua = weather::userAgentBase() + " (always-on clock)";
    std::string body, e1, e2;
    std::vector<Storm> v;
    // GDACS: tropical cyclones of the last few days, all alert levels. HTTP 204 No Content means 0 active cyclones.
    const std::string p1 = "/gdacsapi/api/events/geteventlist/SEARCH?eventlist=TC&alertlevel=Green%3BOrange%3BRed&pageSize=100&pageNumber=1&fromDate=" + day(now, -6) + "&toDate=" + day(now, 1);
    if (weather::httpGet(L"www.gdacs.org", p1, true, ua, body, e1)) {
        if (body.empty() || parseGdacs(body, now, v, e1)) { out.list = v; out.source = "GDACS"; out.valid = true; out.fetched = now; return true; }
    } else if (e1 == "HTTP 204") {
        out.list.clear(); out.source = "GDACS"; out.valid = true; out.fetched = now; return true;
    }
    v.clear(); body.clear();
    if (weather::httpGet(L"eonet.gsfc.nasa.gov", "/api/v3/events?category=severeStorms&status=open&days=7&limit=80", true, ua, body, e2) && parseEonet(body, now, v, e2)) {
        out.list = v; out.source = "NASA EONET"; out.valid = true; out.fetched = now; return true;
    }
    err = "GDACS: " + (e1.empty() ? std::string("no answer") : e1) + "; EONET: " + (e2.empty() ? std::string("no answer") : e2);
    return false;
}
#endif

} // namespace storms
