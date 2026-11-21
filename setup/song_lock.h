// The few lines that hide the song inside the setup and lock the packed Meridian files to it (the background is in song_engine.h).
// No decoder in here, so payload.h can use it without pulling the MP3 code into every file. pack.py / make_song.py hold the same arithmetic in Python.
#pragma once
#include <cstddef>
#include <cstdint>

namespace songeng {

constexpr uint64_t kSongSeed = 0x4D45524944494E31ULL;                 // the keystream that hides song.dat

inline uint64_t mix64(uint64_t z) { z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL; z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL; return z ^ (z >> 31); }

// XOR with a keystream: the same call hides and reveals. (pack.py has the same code; tests/song_test.cpp checks the two agree.)
inline void scramble(uint8_t* p, size_t n, uint64_t seed) {
    uint64_t st = seed; size_t i = 0;
    while (i < n) { st += 0x9E3779B97F4A7C15ULL; const uint64_t z = mix64(st); for (int b = 0; b < 8 && i < n; ++b, ++i) p[i] ^= (uint8_t)(z >> (8 * b)); }
}
inline uint64_t hash64(const uint8_t* p, size_t n) {
    uint64_t h = 0xCBF29CE484222325ULL;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 0x100000001B3ULL; }
    return mix64(h) | 1;                                            // never 0: 0 means "no song"
}

} // namespace songeng
