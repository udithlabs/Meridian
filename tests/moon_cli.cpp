#include "../src/moonsprite.h"
#include <cstdio>
#include <cstdlib>
// usage: moon_cli [size] -> tests/moons.ppm : a contact sheet of phases
int main(int argc, char** argv) {
    const int N = argc > 1 ? atoi(argv[1]) : 300;
    struct C { double i, chi; } cs[] = {{180, 270}, {150, 270}, {120, 270}, {90, 270}, {55, 270}, {25, 270}, {2, 270}, {90, 90}};
    const int cols = 4, rows = 2;
    std::vector<uint8_t> sheet((size_t)N * cols * N * rows * 3, 0);
    for (int k = 0; k < 8; ++k) {
        moonsprite::Params p; p.size = N; p.phaseAngleDeg = cs[k].i; p.brightLimbPA = cs[k].chi; p.earthshine = 0.07;
        std::vector<uint8_t> px; moonsprite::render(p, px);
        int cx = (k % cols) * N, cy = (k / cols) * N;
        for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) {
            const uint8_t* s = &px[((size_t)y * N + x) * 4];
            uint8_t* d = &sheet[((size_t)(cy + y) * N * cols + cx + x) * 3];
            double bgR = 8, bgG = 12, bgB = 28; double a = s[3] / 255.0;
            d[0] = (uint8_t)(s[2] + bgR * (1 - a)); d[1] = (uint8_t)(s[1] + bgG * (1 - a)); d[2] = (uint8_t)(s[0] + bgB * (1 - a));
        }
    }
    FILE* f = fopen("tests/moons.ppm", "wb"); fprintf(f, "P6\n%d %d\n255\n", N * cols, N * rows); fwrite(sheet.data(), 1, sheet.size(), f); fclose(f);
}
