// Prints a year of Sri Lankan holidays; with "check" compares the estimator with the official 2026/2027 lists.
#include "holidays.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
using namespace holiday;
int main(int argc, char** argv) {
    if (argc > 1 && !std::strcmp(argv[1], "check")) {
        int bad = 0, total = 0;
        for (int y = 2026; y <= 2027; ++y) {
            auto off = forYear(y), est = estimateYear(y);
            for (auto& h : off) {
                if (!h.poya() && h.id != H_VESAK_AFTER) continue;
                ++total; bool ok = false;
                for (auto& e : est) if (e.y == h.y && e.m == h.m && e.d == h.d && e.id == h.id && e.adhi == h.adhi) ok = true;
                if (!ok) { ++bad; std::printf("MISMATCH official %04d-%02d-%02d %s\n", h.y, h.m, h.d, name(h).c_str()); }
            }
            for (auto& e : est) if (e.poya() || e.id == H_VESAK_AFTER) {
                bool ok = false; for (auto& h : off) if (h.y == e.y && h.m == e.m && h.d == e.d && h.id == e.id) ok = true;
                if (!ok) { ++bad; std::printf("EXTRA estimate %04d-%02d-%02d %s\n", e.y, e.m, e.d, name(e).c_str()); }
            }
            // non-Poya estimates that exist in the official list
            for (auto& e : est) if (!e.poya() && e.id != H_VESAK_AFTER) {
                bool ok = false; for (auto& h : off) if (h.m == e.m && h.d == e.d && h.id == e.id) ok = true;
                if (!ok) std::printf("note: estimate %04d-%02d-%02d %s differs from official\n", e.y, e.m, e.d, name(e).c_str());
            }
        }
        std::printf("%d Poya-type dates checked, %d problems\n", total, bad);
        return bad != 0;
    }
    int y = argc > 1 ? std::atoi(argv[1]) : 2026;
    for (auto& h : forYear(y)) std::printf("%04d-%02d-%02d  %-42s %s%s\n", h.y, h.m, h.d, name(h).c_str(), categories(h).c_str(), h.estimated ? "  (estimated)" : "");
    if (argc > 4) { auto d = onDate(y, std::atoi(argv[2]), std::atoi(argv[3])); for (auto& h : d) std::printf("\n%s\n%s\nWHO: %s\nHOW: %s\n", tag(h).c_str(), what(h).c_str(), who(h).c_str(), how(h).c_str()); }
}
