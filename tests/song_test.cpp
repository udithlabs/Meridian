// The setup's music (setup/song_engine.h): how the song is hidden and read back, the MP3 decoding, and the volume. No Windows, no speakers needed.
//   g++ -std=c++17 -I setup tests/song_test.cpp -o song_test && ./song_test [--dump out.raw]     (run from the source folder)
#include "song_engine.h"
#include <cstdio>
#include <fstream>
#include <iterator>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static std::vector<uint8_t> readFile(const char* path) { std::ifstream f(path, std::ios::binary); return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); }

int main(int argc, char** argv) {
    using namespace songeng;

    // the hiding arithmetic must match make_song.py / pack.py exactly (these numbers come from the Python side)
    { uint8_t z[16] = {0}; scramble(z, 16, kSongSeed);
      const uint8_t want[16] = {0x82, 0x06, 0x56, 0x5c, 0x05, 0xfa, 0xbf, 0xbc, 0x24, 0x69, 0x16, 0xfd, 0xdd, 0x22, 0x18, 0xed};
      CHECK(memcmp(z, want, 16) == 0);
      uint8_t y[5] = {0}; scramble(y, 5, 0x0123456789ABCDEFULL); const uint8_t w2[5] = {0x9d, 0xaa, 0x8f, 0xa4, 0x07}; CHECK(memcmp(y, w2, 5) == 0);
      uint8_t r[256]; for (int i = 0; i < 256; ++i) r[i] = (uint8_t)i;
      CHECK(hash64(r, 256) == 0xef2687f0d4371fe3ULL); CHECK(hash64(r, 0) == 0xf52a15e9a9b5e89bULL);
      uint8_t a[100], b[100]; for (int i = 0; i < 100; ++i) a[i] = b[i] = (uint8_t)(i * 7); scramble(b, 100, 5); CHECK(memcmp(a, b, 100) != 0); scramble(b, 100, 5); CHECK(memcmp(a, b, 100) == 0); }   // it undoes itself

    // the song file that ships with the setup
    std::vector<uint8_t> blob = readFile("setup/song.dat");
    if (blob.empty()) std::printf("(setup/song.dat not found: the song checks are skipped)\n");
    else {
        Song s; CHECK(unlock(blob.data(), blob.size(), s));
        CHECK(!s.artist.empty() && !s.title.empty() && s.mp3.size() > 100000);
        CHECK(hash64(blob.data(), blob.size()) != 0);
        // a damaged or foreign file is refused, never half-read
        std::vector<uint8_t> bad = blob; bad[3] ^= 0x55; Song t; CHECK(!unlock(bad.data(), bad.size(), t));
        bad = blob; bad.pop_back(); CHECK(!unlock(bad.data(), bad.size(), t));
        CHECK(!unlock(nullptr, 0, t)); uint8_t tiny[4] = {1, 2, 3, 4}; CHECK(!unlock(tiny, 4, t));
        // the key changes with any change to the song file (this is what locks the packed Meridian files to it)
        bad = blob; bad[blob.size() / 2] ^= 1; CHECK(hash64(bad.data(), bad.size()) != hash64(blob.data(), blob.size()));
        // it is not an MP3 you could carve out of the finished file
        CHECK(blob.size() < 4 || memcmp(blob.data(), "MSG1", 4) != 0); bool hasId3 = false; for (size_t i = 0; i + 3 < std::min<size_t>(blob.size(), 4096); ++i) if (!memcmp(&blob[i], "ID3", 3)) hasId3 = true; CHECK(!hasId3);

        Pcm pcm; CHECK(decode(s.mp3.data(), s.mp3.size(), pcm));
        CHECK(pcm.ch == 1 || pcm.ch == 2); CHECK(pcm.rate >= 32000 && pcm.rate <= 48000);
        const double secs = (double)pcm.frames() / (double)std::max(1, pcm.rate); CHECK(secs > 20 && secs < 900);
        int peak = 0; for (int16_t v : pcm.s) peak = std::max(peak, std::abs((int)v)); CHECK(peak > 1000);      // real sound, not silence
        std::printf("song: %d Hz, %d ch, %.1f s, peak %d\n", pcm.rate, pcm.ch, secs, peak);
        for (int i = 1; i + 1 < argc; ++i) if (!strcmp(argv[i], "--dump")) { std::ofstream o(argv[i + 1], std::ios::binary); o.write((const char*)pcm.s.data(), (std::streamsize)(pcm.s.size() * 2)); }
    }

    // the volume, on a made-up sound: 1 second of a full-scale square wave, stereo (left +, right -), 8000 Hz
    {
        Pcm p; p.rate = 8000; p.ch = 2; p.s.resize(8000 * 2); for (size_t i = 0; i < 8000; ++i) { p.s[i * 2] = 32767; p.s[i * 2 + 1] = -32768; }
        // fade in over 0.5 s: starts silent, rises steadily, never louder than master x full
        { Mixer m; m.pcm = &p; m.startFadeIn(0.5f); std::vector<int16_t> o(4000 * 2); m.fill(o.data(), 4000);
          CHECK(o[0] >= 0 && o[0] < 50); int prev = -1; bool rising = true; for (size_t f = 0; f < 4000; ++f) { int v = o[f * 2]; if (v < prev) rising = false; prev = v; }
          CHECK(rising); CHECK(std::abs((int)o[3999 * 2] - (int)std::lrintf(32767 * m.master)) <= 2);
          CHECK(o[2 * 3000 + 1] < 0 && o[2 * 3000] > 0);                                      // left stays left, right stays right
          const int lim = (int)std::lrintf(32768 * m.master) + 1; for (size_t i = 0; i < o.size(); ++i) CHECK(std::abs((int)o[i]) <= lim); }     // never louder than the master level
        // mute: down to silence in 0.25 s, and back up again
        { Mixer m; m.pcm = &p; m.gain = 1.f; m.target = 1.f; m.to(0.f, 0.25f); std::vector<int16_t> o(3000 * 2); m.fill(o.data(), 3000);
          CHECK(m.gain == 0.f); CHECK(o[2 * 2500] == 0); CHECK(o[2 * 100] > 0); m.to(1.f, 0.25f); m.fill(o.data(), 3000); CHECK(m.gain == 1.f); }
        // fade out and stop: finishes by itself after about the fade time, silent after that
        { Mixer m; m.pcm = &p; m.gain = 1.f; m.target = 1.f; m.fadeOutAndStop(0.5f); std::vector<int16_t> o(1000 * 2);
          int calls = 0; bool more = true; while (more && calls < 100) { more = m.fill(o.data(), 1000); ++calls; }
          CHECK(!more); CHECK(m.finished()); CHECK(calls >= 4 && calls <= 6); CHECK(o[0] == 0 && o[1999] == 0); }
        // the song ending by itself: the tail is silence and fill() says so
        { Mixer m; m.pcm = &p; m.gain = 1.f; m.target = 1.f; std::vector<int16_t> o(3000 * 2); bool more = true; int n = 0; while (more && n < 10) { more = m.fill(o.data(), 3000); ++n; }
          CHECK(!more); CHECK(n == 3); CHECK(o[2 * 2999] == 0 && o[2 * 2999 + 1] == 0); CHECK(m.pos >= p.frames()); }
        // nothing loaded: silent and finished, no crash
        { Mixer m; std::vector<int16_t> o(64); CHECK(!m.fill(o.data(), 32)); CHECK(m.finished()); }
    }

    if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
    std::printf("song ok\n");
    return 0;
}
