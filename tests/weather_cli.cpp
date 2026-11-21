#include "../src/weather.h"
#include "../src/util.h"
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cmath>
#include <cstdio>
int main(int argc, char** argv) {
    std::ifstream f(argc > 1 ? argv[1] : "tests/sample_weather.json"); std::stringstream ss; ss << f.rdbuf();
    weather::Data d; std::string err; int prov = argc > 2 ? atoi(argv[2]) : 0;
    d.utcOffset = 19800;
    if (!weather::parseProvider(prov, ss.str(), 6.9271, 79.8612, d, err)) { printf("parse failed: %s\n", err.c_str()); return 1; }
    printf("source %s  uv %d  first hour %s  last hour %s\n", d.source.c_str(), (int)d.hasUv, util::fmtClock(d.hourly.front().t, 19800, false).c_str(), util::fmtClock(d.hourly.back().t, 19800, false).c_str());
    for (size_t i = 1; i < d.hourly.size(); ++i) if (std::fabs(d.hourly[i].t - d.hourly[i-1].t - 3600.0) > 1) { printf("NON-HOURLY step at %zu\n", i); break; }
    for (size_t i = 0; i < d.daily.size() && i < 8; ++i) printf("  day %zu: %s max %.1f min %.1f code %d rain%% %.0f sun %s-%s\n", i, util::fmtClock(d.daily[i].t0, 19800, false).c_str(), d.daily[i].tmax, d.daily[i].tmin, d.daily[i].code, d.daily[i].precipProbMax, util::fmtClock(d.daily[i].sunrise, 19800, false).c_str(), util::fmtClock(d.daily[i].sunset, 19800, false).c_str());
    printf("tz %s off %d  hourly %zu daily %zu  cur %.1f C code %d (%s)\n", d.timezone.c_str(), d.utcOffset, d.hourly.size(), d.daily.size(), d.cur.temp, d.cur.code, weather::describe(d.cur.code, d.cur.isDay).text);
    
    double nows[] = {1792260000.0 /*2026-10-18 ~17:30 local*/};
    // 2026-10-18T17:30+05:30 = 12:00 UTC
    double now = astro::daysFromCivil(2026,10,18)*86400.0 + 12*3600.0;
    for (int h : {0, 5, 13}) {
        double t = now + h*3600.0;
        printf("[%s] %s\n", util::fmtClock(t, d.utcOffset, false).c_str(), weather::summarize(d, t, d.utcOffset, false, false).c_str());
    }
    std::ifstream fa("tests/sample_air.json"); std::stringstream sa; sa << fa.rdbuf();
    if (weather::parseAir(sa.str(), d, err)) printf("AQI %.0f (%s) pm2.5 %.1f\n", d.aqi, weather::aqiLabel(d.aqi), d.pm25);
    (void)nows; (void)argc;
}
