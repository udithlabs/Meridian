// skyev_cli "YYYY-MM-DD HH:MM" [windowDays] [tzHours] [lat] [lon]   (time given in UTC)
#include "../src/skyevents.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
using namespace astro;
static double tzh = 5.5;
static std::string clk(double t, int mode) {
    long long s = (long long)std::llround(t + tzh * 3600);
    long long days = (s >= 0 ? s : s - 86399) / 86400; long long sod = s - days * 86400;
    int y, m, d; civilFromDays(days, y, m, d);
    static const char* WD[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char* MN[12] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    char b[64];
    if (mode == 0) snprintf(b, sizeof b, "%02d:%02d", (int)(sod / 3600), (int)(sod / 60 % 60));
    else if (mode == 1) snprintf(b, sizeof b, "%s %d %s", WD[dayOfWeek(y, m, d)], d, MN[m - 1]);
    else snprintf(b, sizeof b, "%s %d %s, %02d:%02d", WD[dayOfWeek(y, m, d)], d, MN[m - 1], (int)(sod / 3600), (int)(sod / 60 % 60));
    return b;
}
static std::string expand(const std::string& in) {
    std::string out; size_t i = 0;
    while (i < in.size()) {
        if (in[i] == '{') {
            size_t c = in.find(':', i), e = in.find('}', i);
            std::string k = in.substr(i + 1, c - i - 1); double v = atof(in.substr(c + 1, e - c - 1).c_str());
            out += clk(v, k == "t" ? 0 : k == "d" ? 1 : 2); i = e + 1;
        } else out += in[i++];
    }
    return out;
}
int main(int argc, char** argv) {
    int y = 2026, mo = 10, d = 20, hh = 0, mi = 0;
    if (argc > 1) sscanf(argv[1], "%d-%d-%d %d:%d", &y, &mo, &d, &hh, &mi);
    double now = daysFromCivil(y, mo, d) * 86400.0 + hh * 3600 + mi * 60;
    skyev::Options o; if (argc > 2) o.windowDays = atof(argv[2]);
    if (argc > 3) tzh = atof(argv[3]);
    Geo g; if (argc > 5) { g.lat = atof(argv[4]); g.lon = atof(argv[5]); }
    auto ev = skyev::find(now, g, o);
    static const char* K[] = {"METEOR", "CONJ", "MOON", "OPPO", "ELONG", "PARADE", "SOLAR", "LUNAR"};
    printf("now %s local  (%zu events, window %.1f d)\n", clk(now, 2).c_str(), ev.size(), o.windowDays);
    for (auto& e : ev) {
        printf("\n[%s] %s  | %s\n   peak %s (%.2f d)%s  start %s end %s  vis=%d type=%d mag=%.4f gamma=%.4f obsc=%.3f sep=%.3f\n",
               K[e.kind], e.title.c_str(), expand(e.subtitle).c_str(), clk(e.tPeak, 2).c_str(), e.daysAway(now), "",
               clk(e.tStart, 2).c_str(), clk(e.tEnd, 2).c_str(), e.visible, e.type, e.magnitude, e.gamma, e.obscuration, e.sepDeg);
        for (auto& dt : e.details) printf("     %-18s %s\n", dt.label.c_str(), expand(dt.value).c_str());
    }
    return 0;
}
