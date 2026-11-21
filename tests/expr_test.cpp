// Native test of the theme expression language: g++ -std=c++17 -I src tests/expr_test.cpp src/expr.cpp && ./a.out
#include "expr.h"
#include <cstdio>
#include <cmath>
#include <string>
#include <cstring>
int fails = 0;
void check(const char* src, double want, const double* v, const expr::VarTable& vt) {
    expr::Program p; std::string err;
    if (!p.compile(src, vt, err)) { printf("FAIL compile '%s': %s\n", src, err.c_str()); ++fails; return; }
    double got = p.eval(v);
    if (std::fabs(got - want) > 1e-9) { printf("FAIL '%s' = %g, wanted %g\n", src, got, want); ++fails; }
}
void bad(const char* src, const expr::VarTable& vt) {
    expr::Program p; std::string err;
    const std::string shown = std::strlen(src) > 40 ? std::string(src, 28) + "... (" + std::to_string(std::strlen(src)) + " chars)" : std::string(src);
    if (p.compile(src, vt, err)) { printf("FAIL '%s' should not compile\n", shown.c_str()); ++fails; } else printf("ok   '%s' -> %s\n", shown.c_str(), err.c_str());
}
int main() {
    expr::VarTable vt; vt.add("t"); vt.add("days"); vt.add("hour");
    double v[3] = {2.0, 21.0, 19.5};
    check("1+2*3", 7, v, vt); check("(1+2)*3", 9, v, vt); check("-t + 5", 3, v, vt); check("days < 20", 0, v, vt);
    check("days >= 20 && hour > 18", 1, v, vt); check("days < 20 ? 10 : 20", 20, v, vt); check("clamp(5 - ceil(days/20), 0, 5)", 3, v, vt);
    check("min(3, 4) + max(1, 2)", 5, v, vt); check("mix(10, 20, 0.25)", 12.5, v, vt); check("7 % 4", 3, v, vt); check("-1 % 4", 3, v, vt);
    check("!0 + !1", 1, v, vt); check("1 != 2", 1, v, vt); check("sin(pi/2)", 1, v, vt); check("1/0", 0, v, vt); check("smoothstep(0,10,5)", 0.5, v, vt);
    check("hash(3) >= 0 && hash(3) <= 1", 1, v, vt); check("0.5+.25", 0.75, v, vt); check("t*t*t", 8, v, vt);
    bad("1 +", vt); bad("foo + 1", vt); bad("sin(1, 2)", vt); bad("(1+2", vt); bad("1 2", vt); bad("nope(1)", vt); bad("a ? 1", vt);
    // ---- limits: a broken or hostile formula is refused with a message, it never crashes
    auto repeat = [](const std::string& a, size_t n) { std::string s; s.reserve(a.size() * n); for (size_t i = 0; i < n; ++i) s += a; return s; };
    check(("1" + repeat("+1", 100)).c_str(), 101, v, vt);                                    // a long but flat sum is fine
    check((repeat("(", 20) + "1" + repeat(")", 20)).c_str(), 1, v, vt);                      // sensible nesting is fine
    check((repeat("-", 20) + "1").c_str(), 1, v, vt);
    bad((repeat("(", 30000) + "1" + repeat(")", 30000)).c_str(), vt);                         // the crash from the review: about 60 KB of brackets
    bad((repeat("(", 200) + "1" + repeat(")", 200)).c_str(), vt);
    bad((repeat("-", 100000) + "1").c_str(), vt);                                             // a very long unary chain
    bad((repeat("!", 5000) + "1").c_str(), vt);
    bad((repeat("1?1:", 2000) + "1").c_str(), vt);                                            // nested ternaries
    bad((repeat("sin(", 5000) + "1" + repeat(")", 5000)).c_str(), vt);                        // nested function calls
    // short enough to pass the length limit, so these are stopped by the nesting limit itself
    bad((repeat("(", 900) + "1" + repeat(")", 900)).c_str(), vt);
    bad((repeat("-", 1900) + "1").c_str(), vt);
    bad((repeat("1?1:", 450) + "1").c_str(), vt);
    bad((repeat("sin(", 300) + "1" + repeat(")", 300)).c_str(), vt);
    bad((repeat("1?(", 300) + "1" + repeat("):1", 300)).c_str(), vt);
    bad(("1" + repeat("+1", 5000)).c_str(), vt);                                              // over the length limit
    bad("1e999", vt);                                                                          // not a finite number
    bad(("1 + 9" + std::string(400, '9') + "9").c_str(), vt);                                           // would be infinity
    {   // the same checks for formulas the way they arrive from a theme file (std::string with embedded NUL)
        expr::Program p; std::string err, s = std::string("1") + '\0' + "+2";
        if (p.compile(s, vt, err)) { printf("FAIL embedded NUL accepted\n"); ++fails; }
    }
    check("1e300 * 1e300", 0, v, vt);                                                          // overflow while running gives 0, never infinity
    check("hash(1e300) >= 0", 1, v, vt);
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
