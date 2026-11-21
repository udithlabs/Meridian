// Regression tests covering Meridian 2.9.2 fixes (C1-C6, H1-H31, M1-M28). Portable C++17.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "astro.h"
#include "expr.h"
#include "holidays.h"
#include "minijson.h"
#include "skyevents.h"
#include "storms.h"
#include "textedit.h"
#include "today.h"
#include "util.h"
#include "weather.h"

static int gFails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::fprintf(stderr, "FAIL [%s:%d] %s\n", __FILE__, __LINE__, (msg)); ++gFails; } } while (0)

static bool validUtf8(const std::string& s) {
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { ++i; continue; }
        size_t need = 0;
        if ((c & 0xE0) == 0xC0) need = 2;
        else if ((c & 0xF0) == 0xE0) need = 3;
        else if ((c & 0xF8) == 0xF0) need = 4;
        else return false;
        if (i + need > n) return false;
        for (size_t k = 1; k < need; ++k) if (((unsigned char)s[i + k] & 0xC0) != 0x80) return false;
        i += need;
    }
    return true;
}

static void testUtilAndIni() {
    // C1/C2: util::fmt dynamic sizing beyond 512 bytes
    std::string big(1200, 'A');
    std::string f = util::fmt("pre-%s-post", big.c_str());
    CHECK(f.size() == 1200 + 9, "util::fmt truncated >512B string");
    CHECK(f.substr(0, 4) == "pre-" && f.substr(f.size() - 5) == "-post", "util::fmt content mismatch");

    // C1: forecastPath must not be truncated
    std::string fp = weather::forecastPath(6.9271, 79.8612);
    CHECK(fp.find("wind_gusts_10m_max") != std::string::npos, "forecastPath truncated tail parameters");
    CHECK(fp.find("timezone=auto") != std::string::npos, "forecastPath missing timezone=auto");
    CHECK(fp.find("latitude=6.93") != std::string::npos, "forecastPath rounding");

    // M27: Ini UTF-8 BOM stripping & non-finite rejection
    util::Ini ini = util::Ini::parse("\xEF\xBB\xBF[sec]\nkey=hello\nnanv=nan\ninfv=inf\nneg=-inf\nok=3.5\n");
    CHECK(ini.get("sec", "key") == "hello", "Ini failed to strip UTF-8 BOM");
    CHECK(ini.getNum("sec", "nanv", 42.0) == 42.0, "Ini accepted NaN");
    CHECK(ini.getNum("sec", "infv", 42.0) == 42.0, "Ini accepted +Inf");
    CHECK(ini.getNum("sec", "neg", 42.0) == 42.0, "Ini accepted -Inf");
    CHECK(std::fabs(ini.getNum("sec", "ok", 0.0) - 3.5) < 1e-9, "Ini valid number");
}

static void testTextEditAndMiniJson() {
    // M25: visualLineStart / visualLineEnd bounds clamping
    std::string s = "hello";
    std::vector<size_t> lines = {0, 3, 999};
    CHECK(textedit::visualLineStart(s, lines, 500) <= s.size(), "visualLineStart OOB");
    CHECK(textedit::visualLineEnd(s, lines, 500) <= s.size(), "visualLineEnd OOB");
    CHECK(textedit::visualLineEnd(s, lines, 4) == s.size(), "visualLineEnd clamp");

    // M17: minijson Parser resets `out` on failure
    mj::Value v;
    std::string err;
    CHECK(mj::parse("{\"a\":1}", v, err), "minijson valid parse failed");
    CHECK(v.type == mj::Value::Object, "minijson expected object");
    CHECK(!mj::parse("{broken", v, err), "minijson invalid parse unexpectedly succeeded");
    CHECK(v.type == mj::Value::Null, "minijson did not reset out on parse error");
}

static void testWeatherFixes() {
    // H3/M10/H4: 7Timer lightsnowday -> snow (71), NOT thunder (95); 3h rainMm divided by 3; late-night buildDaily keeps real code
    std::string json7 =
        "{\"init\":\"2026112018\",\"dataseries\":["
        "{\"timepoint\":3,\"cloudcover\":6,\"prec_type\":\"snow\",\"prec_amount\":2,\"temp2m\":-2,\"rh2m\":\"80%\",\"wind10m\":{\"direction\":\"N\",\"speed\":3},\"weather\":\"lightsnowday\"},"
        "{\"timepoint\":6,\"cloudcover\":8,\"prec_type\":\"rain\",\"prec_amount\":6,\"temp2m\":4,\"rh2m\":\"90%\",\"wind10m\":{\"direction\":\"NE\",\"speed\":4},\"weather\":\"rainday\"},"
        "{\"timepoint\":9,\"cloudcover\":8,\"prec_type\":\"rain\",\"prec_amount\":6,\"temp2m\":5,\"rh2m\":\"88%\",\"wind10m\":{\"direction\":\"NE\",\"speed\":4},\"weather\":\"rainday\"}"
        "]}";
    weather::Data d; std::string err;
    CHECK(weather::parseProvider(weather::PV_7TIMER, json7, 51.5, 0.0, d, err), "7Timer parse failed");
    CHECK(d.valid && !d.hourly.empty(), "7Timer hourly empty");
    CHECK(d.hourly[0].code == 71, "7Timer lightsnowday misclassified (expected WMO 71, not 95)");
    // Check 3h bucket precip_amount=6 (rainMm[6]=23 mm over 3h -> ~7.67 mm/h)
    bool sawRate = false;
    for (const auto& h : d.hourly) if (h.code == 63 && std::fabs(h.precip - 23.0 / 3.0) < 0.6) sawRate = true;
    CHECK(sawRate, "7Timer 3h precip not divided by 3");
    // Late-night daily entry (init 18:00 + 3h = 21:00 UTC) must still pick snow code (71) instead of 0
    CHECK(!d.daily.empty() && d.daily[0].code == 71, "buildDaily after 20:59 left code=0");

    // M13: summarize reports Snow when snow hours predominate
    weather::Data dsnow = d;
    dsnow.cur.code = 73;
    for (auto& h : dsnow.hourly) { h.code = 73; h.precipProb = 80.0; }
    double tNow = dsnow.hourly[0].t - 600.0;
    std::string sum = weather::summarize(dsnow, tNow, 0, true, false);
    CHECK(sum.find("Snow") != std::string::npos || sum.find("snow") != std::string::npos, "summarize did not mention snow");

    // H5: wttr.in UTC-10 (Honolulu, lon=-157.86) must yield utcOffset == -36000, not +50400
    std::string jsonWttr =
        "{\"current_condition\":[{\"temp_C\":\"27\",\"FeelsLikeC\":\"29\",\"humidity\":\"65\",\"pressure\":\"1015\","
        "\"windspeedKmph\":\"18\",\"winddirDegree\":\"70\",\"cloudcover\":\"25\",\"precipMM\":\"0.0\","
        "\"weatherCode\":\"116\",\"observation_time\":\"06:00 PM\",\"localObsDateTime\":\"2026-11-20 08:00 AM\"}],"
        "\"weather\":[{\"date\":\"2026-11-20\",\"maxtempC\":\"29\",\"mintempC\":\"22\",\"uvIndex\":\"8\",\"hourly\":["
        "{\"time\":\"900\",\"tempC\":\"27\",\"FeelsLikeC\":\"29\",\"chanceofrain\":\"10\",\"precipMM\":\"0.0\",\"windspeedKmph\":\"18\",\"cloudcover\":\"25\",\"humidity\":\"65\",\"weatherCode\":\"116\"},"
        "{\"time\":\"1200\",\"tempC\":\"28\",\"FeelsLikeC\":\"30\",\"chanceofrain\":\"15\",\"precipMM\":\"0.0\",\"windspeedKmph\":\"20\",\"cloudcover\":\"30\",\"humidity\":\"62\",\"weatherCode\":\"116\"}"
        "]}]}";
    weather::Data dw;
    CHECK(weather::parseProvider(weather::PV_WTTR, jsonWttr, 21.31, -157.86, dw, err), "wttr.in Honolulu parse failed");
    CHECK(dw.utcOffset == -36000, "wttr.in UTC-10 disambiguation failed (got wrong hemisphere offset)");
}

static void testStormsFixes() {
    // H21/M15: GDACS "Category 4" must not become 4 km/h; Atlantic lon=-65 -> Hurricane
    double now = astro::daysFromCivil(2026, 10, 10) * 86400.0;
    std::string gdacs =
        "{\"type\":\"FeatureCollection\",\"features\":[{"
        "\"geometry\":{\"type\":\"Point\",\"coordinates\":[-65.0, 22.0]},"
        "\"properties\":{\"eventtype\":\"TC\",\"name\":\"Hurricane ALPHA-26\",\"alertlevel\":\"Red\","
        "\"todate\":\"2026-10-10T06:00:00\",\"severitydata\":{\"severity\":0,\"severitytext\":\"Category 4 hurricane with maximum sustained winds 215 km/h\"}}"
        "}]}";
    std::vector<storms::Storm> out; std::string err;
    CHECK(storms::parseGdacs(gdacs, now, out, err), "GDACS parse failed");
    CHECK(out.size() == 1, "GDACS expected 1 storm");
    if (out.size() == 1) {
        CHECK(std::fabs(out[0].windKmh - 215.0) < 0.1, "GDACS numberIn matched Category 4 instead of 215 km/h");
        CHECK(out[0].kind == "Hurricane", "GDACS Atlantic storm kind should be Hurricane");
        CHECK(out[0].name == "Alpha", "GDACS stripYear/prefix failed");
    }

    // EONET non-wind magnitudeUnit must not be treated as wind speed
    std::string eonet =
        "{\"events\":[{\"title\":\"Tropical Storm Beta\",\"geometry\":[{"
        "\"date\":\"2026-10-10T06:00:00Z\",\"type\":\"Point\",\"coordinates\":[85.0, 12.0],"
        "\"magnitudeValue\":5000,\"magnitudeUnit\":\"acres\"}]}]}";
    std::vector<storms::Storm> eout;
    CHECK(storms::parseEonet(eonet, now, eout, err), "EONET parse failed");
    CHECK(eout.size() == 1 && eout[0].windKmh == 0.0, "EONET non-wind magnitudeUnit treated as wind speed");
}

static void testTodayAndHolidaysFixes() {
    // M1/H6: Sri Lanka observances must not leak when LK is disabled; all Today strings valid UTF-8
    holiday::setCountries(holiday::C_US, holiday::R_UK_EW);
    today::Input in;
    in.now = astro::daysFromCivil(2026, 10, 1) * 86400.0 + 12 * 3600.0; // Oct 1 (SL Children's Day)
    in.tzOff = -5 * 3600;
    in.geo = {40.71, -74.00, "New York"};
    in.poya = true;
    auto items = today::buildToday(in);
    for (const auto& it : items) {
        CHECK(validUtf8(it.tag) && validUtf8(it.title) && validUtf8(it.detail), "Today item has corrupted UTF-8");
        CHECK(it.detail.find("Sri Lanka") == std::string::npos, "Sri Lanka observance leaked into US locale");
    }

    // M3: double Islamic lunar holiday in one year + Dec 31 Vesak no spillover + overrideVersion increments
    holiday::setCountries(holiday::C_LK, holiday::R_UK_EW);
    int v0 = holiday::overrideVersion();
    std::string perr = holiday::setOverrides("2028-01-05 eid-fitr\n2028-12-25 eid-fitr\n2028-12-31 vesak\n");
    CHECK(perr.empty(), "Valid holidays.txt overrides produced error");
    CHECK(holiday::overrideVersion() > v0, "overrideVersion did not increment");
    auto y28 = holiday::forYear(2028);
    int eidCnt = 0; bool badVesakAfter = false;
    for (const auto& h : y28) {
        if (h.id == holiday::H_EID_FITR) ++eidCnt;
        if (h.id == holiday::H_VESAK_AFTER && h.y != 2028) badVesakAfter = true;
    }
    CHECK(eidCnt == 2, "Two Islamic lunar occurrences in one year collapsed into one");
    CHECK(!badVesakAfter, "Dec 31 Vesak spilled vesak-after into wrong year");
    holiday::setOverrides("");
}

static void testAstroAndSkyEventsFixes() {
    // M18: planetInfo OOB safety & equation of time finite
    astro::Geo colombo{6.93, 79.86, "Colombo"};
    double t = astro::daysFromCivil(2026, 11, 20) * 86400.0;
    auto badP = astro::planetInfo(99, t, colombo);
    CHECK(badP.id >= 0 && badP.id < astro::PLANET_COUNT, "planetInfo OOB not clamped");
    double eot = astro::equationOfTimeMinutes(t);
    CHECK(std::isfinite(eot) && eot > 10.0 && eot < 18.0, "equationOfTimeMinutes out of range for Nov 20");

    // H13: Polar transition day length strictly in [0, 86400]
    astro::Geo tromso{69.65, 18.96, "Tromso"};
    for (int day = 15; day <= 23; ++day) {
        double tm = astro::daysFromCivil(2026, 5, day) * 86400.0 - 7200.0;
        auto de = astro::dayEvents(tm, tromso);
        CHECK(de.dayLengthSec >= 0.0 && de.dayLengthSec <= 86400.0, "Polar transition dayLengthSec out of [0, 86400]");
    }

    // H14: Meteor showers wrap cleanly across year boundary (Dec 20 looking ahead 20 days finds Quadrantids)
    skyev::Options o; o.meteors = true; o.planets = false; o.eclipses = false; o.windowDays = 20.0;
    double dec20 = astro::daysFromCivil(2026, 12, 20) * 86400.0;
    auto evs = skyev::find(dec20, colombo, o);
    bool foundQuad = false;
    for (const auto& e : evs) if (e.kind == skyev::METEOR && e.title.find("Quadrant") != std::string::npos && e.tPeak > dec20) foundQuad = true;
    CHECK(foundQuad, "Meteor shower year rollover failed to find upcoming Quadrantids");
}

static void testExprAndHalloweenPack() {
    // M26: expr negative constant folding, overflow-safe mod, round, NaN truthy
    expr::VarTable vt;
    expr::Program p; std::string err;
    CHECK(p.compile("-5", vt, err) && p.isConst() && p.constValue() == -5.0, "expr -5 not constant-folded");
    CHECK(p.compile("mod(1e20, 3)", vt, err), "expr mod compile failed");
    double mv = p.eval(nullptr);
    CHECK(std::isfinite(mv) && mv >= 0.0 && mv < 3.0, "expr mod(1e20, 3) invalid");
    CHECK(p.compile("round(-2.5)", vt, err) && p.eval(nullptr) == -3.0, "expr round(-2.5) != -3");
    CHECK(p.compile("(0/0) ? 1 : 2", vt, err) && p.eval(nullptr) == 2.0, "expr truthy(NaN) was true");

    // H2: Halloween theme.json parses cleanly, uses year-independent nxt_days, no hardcoded 2027-10-31
    std::ifstream ifs("themes/halloween/theme.json");
    CHECK(ifs.good(), "Cannot open themes/halloween/theme.json");
    std::stringstream ss; ss << ifs.rdbuf();
    std::string raw = ss.str();
    CHECK(raw.find("2027-10-31") == std::string::npos, "Halloween theme.json still has hardcoded 2027-10-31");
    // Strip // comments and trailing commas like themes.cpp does
    std::string stripped; bool inStr = false, esc = false;
    for (size_t i = 0; i < raw.size(); ++i) {
        char c = raw[i];
        if (inStr) { stripped += c; if (esc) esc = false; else if (c == '\\') esc = true; else if (c == '"') inStr = false; }
        else if (c == '"') { inStr = true; stripped += c; }
        else if (c == '/' && i + 1 < raw.size() && raw[i + 1] == '/') { while (i < raw.size() && raw[i] != '\n') ++i; if (i < raw.size()) stripped += '\n'; }
        else stripped += c;
    }
    // Remove trailing commas before } or ]
    std::string clean; inStr = false; esc = false;
    for (size_t i = 0; i < stripped.size(); ++i) {
        char c = stripped[i];
        if (inStr) { clean += c; if (esc) esc = false; else if (c == '\\') esc = true; else if (c == '"') inStr = false; continue; }
        if (c == '"') { inStr = true; clean += c; continue; }
        if (c == ',') {
            size_t j = i + 1; while (j < stripped.size() && (stripped[j] == ' ' || stripped[j] == '\t' || stripped[j] == '\r' || stripped[j] == '\n')) ++j;
            if (j < stripped.size() && (stripped[j] == '}' || stripped[j] == ']')) continue;
        }
        clean += c;
    }
    mj::Value root;
    CHECK(mj::parse(clean, root, err), "Halloween theme.json failed to parse");
    const mj::Value* vars = root.get("vars");
    const mj::Value* nxt = vars ? vars->get("nxt_days") : nullptr;
    CHECK(nxt && nxt->type == mj::Value::String, "Halloween theme.json missing vars.nxt_days");
    if (nxt && nxt->type == mj::Value::String) {
        expr::VarTable hvt;
        (void)hvt.add("year"); (void)hvt.add("hal_past");
        expr::Program hp;
        CHECK(hp.compile(nxt->str, hvt, err), "Halloween nxt_days formula failed to compile");
        double v[2] = {2026.0, 1.0 * 86400.0}; // Nov 1 2026 -> 364 days to Oct 31 2027
        CHECK(std::fabs(hp.eval(v) - 364.0) < 0.01, "Halloween nxt_days wrong for 2026");
        v[0] = 2027.0; // Nov 1 2027 -> next year 2028 is leap -> 365 days to Oct 31 2028
        CHECK(std::fabs(hp.eval(v) - 365.0) < 0.01, "Halloween nxt_days wrong for leap year 2028");
        v[0] = 2028.0; // Nov 1 2028 -> 364 days to Oct 31 2029
        CHECK(std::fabs(hp.eval(v) - 364.0) < 0.01, "Halloween nxt_days wrong for 2028");
    }
}

int main() {
    testUtilAndIni();
    testTextEditAndMiniJson();
    testWeatherFixes();
    testStormsFixes();
    testTodayAndHolidaysFixes();
    testAstroAndSkyEventsFixes();
    testExprAndHalloweenPack();
    if (gFails) {
        std::fprintf(stderr, "regression_292_test: %d FAILED\n", gFails);
        return 1;
    }
    std::printf("regression_292_test: all checks passed\n");
    return 0;
}
