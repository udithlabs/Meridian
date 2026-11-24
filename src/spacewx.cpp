// Live geomagnetic space weather (NOAA SWPC Planetary Kp-Index).
#include "spacewx.h"
#include "minijson.h"
#include "astro.h"
#ifdef _WIN32
#include "weather.h"
#endif
#include "util.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

namespace spacewx {

namespace {

bool isLeapYear(int y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); }

int daysInMonth(int y, int m) {
    static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m < 1 || m > 12) return 0;
    return (m == 2 && isLeapYear(y)) ? 29 : kDays[m - 1];
}

bool parseFixedDigits(const std::string& s, size_t pos, size_t len, int& out) {
    if (pos + len > s.size()) return false;
    int v = 0;
    for (size_t i = 0; i < len; ++i) {
        char c = s[pos + i];
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
    }
    out = v;
    return true;
}

double parseNoaaTime(const std::string& raw) {
    // Strictly handles "YYYY-MM-DD HH:MM:SS[.fff][Z]" or "YYYY-MM-DDTHH:MM:SS[.fff][Z]"
    size_t a = 0, b = raw.size();
    while (a < b && (raw[a] == ' ' || raw[a] == '\t')) ++a;
    while (b > a && (raw[b - 1] == ' ' || raw[b - 1] == '\t')) --b;
    const std::string s = raw.substr(a, b - a);
    if (s.size() < 19) return 0.0;
    if (s[4] != '-' || s[7] != '-' || (s[10] != ' ' && s[10] != 'T') || s[13] != ':' || s[16] != ':') return 0.0;

    int y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0;
    if (!parseFixedDigits(s, 0, 4, y) || !parseFixedDigits(s, 5, 2, mo) || !parseFixedDigits(s, 8, 2, d) ||
        !parseFixedDigits(s, 11, 2, h) || !parseFixedDigits(s, 14, 2, mi) || !parseFixedDigits(s, 17, 2, sec)) {
        return 0.0;
    }
    // Validate optional fractional seconds & optional UTC 'Z' suffix; reject any trailing garbage
    size_t p = 19;
    if (p < s.size() && s[p] == '.') {
        ++p;
        size_t fStart = p;
        while (p < s.size() && s[p] >= '0' && s[p] <= '9') ++p;
        if (p == fStart) return 0.0;
    }
    if (p < s.size() && (s[p] == 'Z' || s[p] == 'z')) ++p;
    if (p != s.size()) return 0.0;

    // Strict calendar & clock range checks (never normalize 24:99:00 or Feb 30!)
    if (y < 1970 || y > 2100) return 0.0;
    if (mo < 1 || mo > 12) return 0.0;
    if (d < 1 || d > daysInMonth(y, mo)) return 0.0;
    if (h < 0 || h > 23 || mi < 0 || mi > 59 || sec < 0 || sec > 59) return 0.0;
    return astro::daysFromCivil(y, mo, d) * 86400.0 + h * 3600.0 + mi * 60.0 + sec;
}

bool readKpNumber(const mj::Value& v, double& out) {
    if (v.type == mj::Value::Number) {
        if (!std::isfinite(v.num) || v.num < 0.0 || v.num > 9.0) return false;
        out = v.num;
        return true;
    }
    if (v.type == mj::Value::String && !v.str.empty()) {
        size_t a = 0, b = v.str.size();
        while (a < b && (v.str[a] == ' ' || v.str[a] == '\t')) ++a;
        while (b > a && (v.str[b - 1] == ' ' || v.str[b - 1] == '\t')) --b;
        if (a == b) return false;
        const std::string trimmed = v.str.substr(a, b - a);
        char* end = nullptr;
        double x = std::strtod(trimmed.c_str(), &end);
        // Require the entire string to be a valid finite number in [0, 9.0] (rejects "6.0unknown", "NaN", etc.)
        if (end != trimmed.c_str() + trimmed.size() || !std::isfinite(x) || x < 0.0 || x > 9.0) return false;
        out = x;
        return true;
    }
    return false;
}

} // namespace

bool parseNoaaKp(const std::string& json, double now, Data& out, std::string& err) {
    mj::Value root;
    if (!mj::parse(json, root, err)) return false;
    if (root.type != mj::Value::Array || root.arr.empty()) {
        err = "expected JSON array";
        return false;
    }

    double bestT = -1.0, bestKp = 0.0;
    double max24 = 0.0;
    bool hasMax24 = false;
    int count = 0;

    auto acceptRow = [&](double t, double kp) {
        if (now > 0.0 && t > now + 15.0 * 60.0) return;   // ignore future/forecast entries beyond minor clock skew
        ++count;
        if (t >= bestT) { bestT = t; bestKp = kp; }
        if (now <= 0.0 || (now - t <= 24.0 * 3600.0 && now - t >= -15.0 * 60.0)) {
            max24 = hasMax24 ? std::max(max24, kp) : kp;
            hasMax24 = true;
        }
    };

    // Detect array-of-arrays (classic NOAA format: header row + data rows) vs array-of-objects
    if (root.arr[0].type == mj::Value::Array) {
        int timeCol = 0, kpCol = 1;
        size_t startRow = 0;
        const auto& hdr = root.arr[0].arr;
        if (!hdr.empty() && hdr[0].type == mj::Value::String && hdr[0].str.find("time") != std::string::npos) {
            startRow = 1;
            for (size_t i = 0; i < hdr.size(); ++i) {
                if (hdr[i].type != mj::Value::String) continue;
                std::string k = hdr[i].str;
                for (char& ch : k) if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
                if (k == "time_tag") timeCol = (int)i;
                else if (k == "kp" || k == "kp_index") kpCol = (int)i;
            }
        }
        for (size_t i = startRow; i < root.arr.size(); ++i) {
            const auto& row = root.arr[i];
            if (row.type != mj::Value::Array) continue;
            if ((int)row.arr.size() <= std::max(timeCol, kpCol)) continue;
            if (row.arr[timeCol].type != mj::Value::String) continue;
            double t = parseNoaaTime(row.arr[timeCol].str);
            double kp = 0.0;
            if (t <= 0.0 || !readKpNumber(row.arr[kpCol], kp)) continue;
            acceptRow(t, kp);
        }
    } else if (root.arr[0].type == mj::Value::Object) {
        for (const auto& obj : root.arr) {
            if (obj.type != mj::Value::Object) continue;
            const mj::Value* tv = obj.get("time_tag");
            const mj::Value* kv = obj.get("Kp");
            if (!kv) kv = obj.get("kp");
            if (!kv) kv = obj.get("kp_index");
            if (!tv || tv->type != mj::Value::String || !kv) continue;
            double t = parseNoaaTime(tv->str);
            double kp = 0.0;
            if (t <= 0.0 || !readKpNumber(*kv, kp)) continue;
            acceptRow(t, kp);
        }
    } else {
        err = "unexpected NOAA Kp row shape";
        return false;
    }

    if (count == 0) {
        err = "no valid Kp readings";
        return false;
    }
    if (now > 0.0 && bestT > 0.0 && now - bestT > kFeedMaxAgeSec) {
        err = "stale Kp feed (older than 24h)";
        return false;
    }

    out.valid = true;
    out.fetched = now > 0.0 ? now : bestT;
    out.updated = bestT;
    out.kp = bestKp;
    // Strict 24-hour window maximum (never inserts an observation outside [now - 24h, now + 15m]).
    out.kpMax24h = hasMax24 ? max24 : 0.0;
    out.hasMax24h = hasMax24;
    out.source = "NOAA SWPC";
    return true;
}

bool isLive(const Data& d, double now) {
    if (!d.valid || d.updated <= 0.0) return false;
    if (now <= 0.0) return true;
    const double age = now - d.updated;
    return age <= kLiveMaxAgeSec && age >= -15.0 * 60.0;
}

bool isStale(const Data& d, double now) {
    return d.valid && d.updated > 0.0 && now > 0.0 && (now - d.updated) > kLiveMaxAgeSec;
}

std::string ageLabel(double obsTime, double now) {
    if (obsTime <= 0.0 || now <= 0.0) return "";
    const double age = std::max(0.0, now - obsTime);
    if (age < 90.0) return "just now";
    if (age < 3600.0) return util::fmt("%dm ago", (int)std::lround(age / 60.0));
    if (age < 48.0 * 3600.0) return util::fmt("%dh ago", (int)std::lround(age / 3600.0));
    return util::fmt("%dd ago", (int)std::lround(age / 86400.0));
}

int stormScale(double kp) {
    // Official NOAA Space Weather Scales for third-step Planetary Kp:
    // G1 Minor: Kp 5 (5- = 4.67, 5o = 5.00, 5+ = 5.33)
    // G2 Moderate: Kp 6 (6- = 5.67, 6o = 6.00, 6+ = 6.33)
    // G3 Strong: Kp 7 (7- = 6.67, 7o = 7.00, 7+ = 7.33)
    // G4 Severe: Kp 8 including 9- (8- = 7.67, 8o = 8.00, 8+ = 8.33, 9- = 8.67)
    // G5 Extreme: Kp 9 (9o = 9.00)
    if (kp >= 8.95) return 5;
    if (kp >= 7.66) return 4;
    if (kp >= 6.66) return 3;
    if (kp >= 5.66) return 2;
    if (kp >= 4.66) return 1;
    return 0;
}

const char* scaleLabel(double kp) {
    const int g = stormScale(kp);
    switch (g) {
        case 5: return "G5 Extreme Storm";
        case 4: return "G4 Severe Storm";
        case 3: return "G3 Strong Storm";
        case 2: return "G2 Moderate Storm";
        case 1: return "G1 Minor Storm";
        default: break;
    }
    if (kp >= 4.0) return "Active";
    if (kp >= 3.0) return "Unsettled";
    return "Quiet";
}

double auroraMinAbsLat(double kp) {
    const double k = std::max(0.0, std::min(9.0, kp));
    // Auroral oval equatorward edge: ~66.5 deg at Kp 0, ~54 deg at Kp 5, ~47 deg at Kp 7, ~39 deg at Kp 9
    return std::max(36.0, 66.5 - 3.05 * k);
}

float auroraVisibility(double kp, double latDeg, bool globalStormGlow) {
    if (!std::isfinite(kp) || kp < 1.5) return 0.f;
    const double absLat = std::fabs(latDeg);
    const double edge = auroraMinAbsLat(kp);
    float vis = 0.f;
    if (absLat >= edge - 3.5) {
        const float latF = (float)std::max(0.0, std::min(1.0, (absLat - (edge - 3.5)) / 10.5));
        const float kpF = (float)std::max(0.28, std::min(1.0, (kp - 1.5) / 5.5));
        vis = latF * kpF;
    }
    if (globalStormGlow && kp >= 4.67) {
        const float gVis = (float)std::max(0.38, std::min(1.0, (kp - 4.0) / 4.2));
        vis = std::max(vis, gVis);
    }
    return std::max(0.f, std::min(1.f, vis));
}

#ifdef _WIN32
bool fetch(double now, Data& out, std::string& err) {
    const std::string ua = weather::userAgentBase() + " (always-on clock)";
    std::string body, e1;
    if (weather::httpGet(L"services.swpc.noaa.gov", "/products/noaa-planetary-k-index.json", true, ua, body, e1)) {
        if (parseNoaaKp(body, now, out, e1)) return true;
    }
    err = "NOAA SWPC: " + (e1.empty() ? std::string("no answer") : e1);
    return false;
}
#endif

} // namespace spacewx
