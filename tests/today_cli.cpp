#include "../src/today.h"
#include <cstdio>
using namespace astro;
int main(int argc, char** argv) {
    const int tz = 19800;
    struct D { int y, m, d; } days[] = {{2026,10,18},{2026,10,21},{2026,10,26},{2026,11,9},{2026,9,23},{2026,12,21},{2026,9,5},{2027,4,9},{2026,10,10},{2026,10,19},{2026,10,20},{2026,10,4},{2026,5,31},{2026,7,31}};
    for (auto& d : days) {
        double t = (daysFromCivil(d.y, d.m, d.d) * 86400.0) - tz + 12 * 3600.0;   // local noon
        today::Input in; in.now = t; in.tzOff = tz;
        auto items = today::buildToday(in);
        printf("== %04d-%02d-%02d (%s)\n", d.y, d.m, d.d, util::WEEKDAYS[dayOfWeek(d.y, d.m, d.d)]);
        for (auto& it : items) printf("  [%-11s] %s | %s\n", it.tag.c_str(), it.title.c_str(), it.detail.c_str());
        printf("  %s\n", today::dayLengthText(in).c_str());
        auto Y = today::yearInfo(util::toLocal(t, tz));
        printf("  day %d of %d, %d left, ISO week %d/%d\n", Y.doy, Y.daysInYear, Y.daysLeft, Y.week, Y.isoYear);
    }
}
