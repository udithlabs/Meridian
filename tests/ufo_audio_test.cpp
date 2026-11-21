// Tests for the Midnight UFO stereo audio clip codec, panning law, StereoMixer,
// embedded M4A resources and Settings > About cheat codes (no Windows needed).
#include "ufo_audio_codec.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static bool nearf(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

static bool checkM4aAsset(const char* path, long minBytes) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    unsigned char hdr[16] = {};
    size_t got = std::fread(hdr, 1, sizeof hdr, f);
    std::fclose(f);
    return got >= 12 && sz >= minBytes && std::memcmp(hdr + 4, "ftyp", 4) == 0;
}

int main() {
    // 1. Round-trip lossless PCM clip encode/decode
    {
        std::vector<int16_t> pcm(2048);
        for (size_t i = 0; i < pcm.size(); ++i)
            pcm[i] = (int16_t)std::lround(std::sin((double)i * 0.13) * 28000.0);
        auto blob = ufoaudio::encodeClip(pcm.data(), (uint32_t)pcm.size(), 44100);
        CHECK(blob.size() == 16 + pcm.size() * 2);
        ufoaudio::Clip dec;
        CHECK(ufoaudio::decodeClip(blob.data(), blob.size(), dec));
        CHECK(dec.sampleRate == 44100);
        CHECK(dec.mono == pcm);
        // Corrupt/truncated blobs must fail cleanly
        CHECK(!ufoaudio::decodeClip(blob.data(), blob.size() - 1, dec));
        CHECK(!ufoaudio::decodeClip(blob.data(), 10, dec));
        blob[0] = 'X';
        CHECK(!ufoaudio::decodeClip(blob.data(), blob.size(), dec));
    }

    // 2. Stereo-to-mono normalized fly-by clip builder
    {
        const size_t frames = 44100 * 11;
        std::vector<int16_t> stereo(frames * 2, 0);
        for (size_t i = 0; i < frames; ++i) {
            const int16_t v = (int16_t)std::lround(std::sin((double)i * 0.07) * 10000.0);
            stereo[i * 2] = v;
            stereo[i * 2 + 1] = v;
        }
        ufoaudio::Clip clip;
        CHECK(ufoaudio::buildFlybyMonoClip(stereo.data(), stereo.size(), 44100, clip));
        CHECK(clip.sampleRate == 44100);
        CHECK(clip.mono.size() == 44100u * 10u + 22050u);
        int peak = 0;
        for (int16_t s : clip.mono) peak = std::max(peak, std::abs((int)s));
        CHECK(peak >= 29500 && peak <= 31000);
    }

    // 3. Equal-power pan law invariants
    {
        auto pL = ufoaudio::equalPowerPan(-1.f);
        auto pC = ufoaudio::equalPowerPan(0.f);
        auto pR = ufoaudio::equalPowerPan(1.f);
        CHECK(nearf(pL.left, 1.f) && nearf(pL.right, 0.f));
        CHECK(nearf(pC.left, 0.70710678f) && nearf(pC.right, 0.70710678f));
        CHECK(nearf(pR.left, 0.f) && nearf(pR.right, 1.f));
        for (int i = -100; i <= 100; ++i) {
            auto g = ufoaudio::equalPowerPan((float)i / 100.f);
            CHECK(nearf(g.left * g.left + g.right * g.right, 1.f, 1e-3f));
        }
    }

    // 4. Fly-by parameters track BOTH left-to-right and right-to-left directions accurately
    {
        float pan = 0, gain = 0;
        ufoaudio::flybyParams(-0.16f, 0.0f, pan, gain); CHECK(nearf(pan, -1.f) && nearf(gain, 0.f));
        ufoaudio::flybyParams(0.0f, 0.12f, pan, gain);  CHECK(nearf(pan, -1.f) && gain > 0.6f);
        ufoaudio::flybyParams(0.5f, 0.50f, pan, gain);  CHECK(nearf(pan,  0.f) && nearf(gain, 1.f));
        ufoaudio::flybyParams(1.0f, 0.88f, pan, gain);  CHECK(nearf(pan,  1.f) && gain > 0.6f);
        ufoaudio::flybyParams(1.16f, 1.0f, pan, gain);  CHECK(nearf(pan,  1.f) && nearf(gain, 0.f));
        ufoaudio::flybyParams(1.16f, 0.0f, pan, gain);  CHECK(nearf(pan,  1.f) && nearf(gain, 0.f));
        ufoaudio::flybyParams(1.0f, 0.12f, pan, gain);  CHECK(nearf(pan,  1.f) && gain > 0.6f);
        ufoaudio::flybyParams(0.5f, 0.50f, pan, gain);  CHECK(nearf(pan,  0.f) && nearf(gain, 1.f));
        ufoaudio::flybyParams(0.0f, 0.88f, pan, gain);  CHECK(nearf(pan, -1.f) && gain > 0.6f);
        ufoaudio::flybyParams(-0.16f, 1.0f, pan, gain); CHECK(nearf(pan, -1.f) && nearf(gain, 0.f));
    }

    // 5. Embedded M4A assets present & valid + StereoMixer directional sweep check (L->R and R->L)
    {
        CHECK(checkM4aAsset("assets/ufo_song.m4a", 500000));
        CHECK(checkM4aAsset("assets/researched.m4a", 500000));

        ufoaudio::Clip clip;
        clip.sampleRate = 44100;
        clip.mono.resize(44100 * 10);
        for (size_t i = 0; i < clip.mono.size(); ++i)
            clip.mono[i] = (int16_t)std::lround(std::sin((double)i * 0.062831853) * 24000.0);

        auto runSweep = [&](float side, double& lEarly, double& rEarly, double& lMid, double& rMid, double& lLate, double& rLate) {
            const size_t total = 44100 * 9;
            const size_t chunk = 1024;
            std::vector<int16_t> out(total * 2, 0);
            ufoaudio::StereoMixer mx;
            mx.clip = &clip;
            mx.reset(side > 0 ? -1.f : 1.f);
            const float slew = 1.f - std::exp(-1.f / (0.015f * 44100.f));
            for (size_t off = 0; off < total; off += chunk) {
                const size_t n = std::min(chunk, total - off);
                const float u = (float)((double)off / (double)total);
                const float xNorm = side > 0 ? (-0.16f + 1.32f * u) : (1.16f - 1.32f * u);
                float pan = 0, gain = 0;
                ufoaudio::flybyParams(xNorm, u, pan, gain);
                mx.fill(&out[off * 2], n, pan, gain, slew, slew);
            }
            auto rms = [&](double t0, double t1, int ch) {
                size_t i0 = (size_t)(t0 * 44100.0), i1 = (size_t)(t1 * 44100.0);
                double sq = 0;
                for (size_t i = i0; i < i1; ++i) { double v = out[i * 2 + ch]; sq += v * v; }
                return std::sqrt(sq / (double)(i1 - i0));
            };
            lEarly = rms(0.2, 1.8, 0); rEarly = rms(0.2, 1.8, 1);
            lMid   = rms(3.8, 5.2, 0); rMid   = rms(3.8, 5.2, 1);
            lLate  = rms(7.2, 8.8, 0); rLate  = rms(7.2, 8.8, 1);
        };

        double lE, rE, lM, rM, lL, rL;
        runSweep(+1.f, lE, rE, lM, rM, lL, rL);
        CHECK(lE > 1500.0 && lE > rE * 15.0);
        CHECK(lM > 1500.0 && rM > 1500.0 && std::fabs(lM - rM) / lM < 0.15);
        CHECK(rL > 1500.0 && rL > lL * 15.0);

        runSweep(-1.f, lE, rE, lM, rM, lL, rL);
        CHECK(rE > 1500.0 && rE > lE * 15.0);
        CHECK(lM > 1500.0 && rM > 1500.0 && std::fabs(lM - rM) / lM < 0.15);
        CHECK(lL > 1500.0 && lL > rL * 15.0);

        ufoaudio::StereoMixer mx; mx.clip = &clip; mx.reset(0.f); mx.curGain = 1.f;
        std::vector<int16_t> fade(44100 / 2 * 2, 0);
        const float slewOut = 1.f - std::exp(-1.f / (0.030f * 44100.f));
        mx.fill(fade.data(), 44100 / 2, 0.f, 0.f, slewOut, slewOut);
        CHECK(mx.curGain < 0.0005f);
        CHECK(std::abs((int)fade.back()) <= 2);
    }

    // 6. Hidden Settings > About GTA-style cheat code matcher
    {
        auto typeAll = [](std::string& b, const wchar_t* s) {
            ufoaudio::SecretAction last = ufoaudio::SecretAction::None;
            for (; *s; ++s) {
                auto a = ufoaudio::feedSecretChar(b, *s);
                if (a != ufoaudio::SecretAction::None) last = a;
            }
            return last;
        };
        std::string b;
        CHECK(typeAll(b, L"aliensdontexist") == ufoaudio::SecretAction::DisableAliens);
        CHECK(b.empty());
        CHECK(typeAll(b, L"aliensdoexisit") == ufoaudio::SecretAction::EnableAliens);
        CHECK(b.empty());
        CHECK(typeAll(b, L"aliensdoexist") == ufoaudio::SecretAction::EnableAliens);
        CHECK(b.empty());
        CHECK(typeAll(b, L"HippyShit") == ufoaudio::SecretAction::ToggleHippy);
        CHECK(b.empty());
        CHECK(typeAll(b, L"hippyshit") == ufoaudio::SecretAction::ToggleHippy);
        CHECK(b.empty());
        CHECK(typeAll(b, L"HIPPYSHIT") == ufoaudio::SecretAction::ToggleHippy);
        CHECK(b.empty());
        CHECK(typeAll(b, L"hIpPiEsHiT") == ufoaudio::SecretAction::ToggleHippy);
        CHECK(b.empty());
        CHECK(typeAll(b, L"researched") == ufoaudio::SecretAction::ToggleResearched);
        CHECK(b.empty());
        CHECK(typeAll(b, L"RESEARCHED") == ufoaudio::SecretAction::ToggleResearched);
        CHECK(b.empty());
        CHECK(typeAll(b, L"aliensarenuts") == ufoaudio::SecretAction::AliensNuts);
        CHECK(b.empty());
        CHECK(typeAll(b, L"ALIENSARENUTS") == ufoaudio::SecretAction::AliensNuts);
        CHECK(b.empty());
        CHECK(typeAll(b, L"aliennohere") == ufoaudio::SecretAction::AliensNormal);
        CHECK(b.empty());
        CHECK(typeAll(b, L"AlIeNsNoHeRe") == ufoaudio::SecretAction::AliensNormal);
        CHECK(b.empty());
        CHECK(typeAll(b, L"jukebox") == ufoaudio::SecretAction::ToggleJukebox);
        CHECK(b.empty());
        CHECK(typeAll(b, L"JUKEBOX") == ufoaudio::SecretAction::ToggleJukebox);
        CHECK(b.empty());
        CHECK(typeAll(b, L"killjukebox") == ufoaudio::SecretAction::KillJukebox);
        CHECK(b.empty());
        CHECK(typeAll(b, L"xyz KiLlJuKeBoX") == ufoaudio::SecretAction::KillJukebox);
        CHECK(b.empty());
        // Case-insensitive, stray prefix, spaces/apostrophes, and Backspace typo correction
        CHECK(typeAll(b, L"xyz Aliens Don't Exx\bist") == ufoaudio::SecretAction::DisableAliens);
        CHECK(typeAll(b, L"abc Hippy Shix\bt") == ufoaudio::SecretAction::ToggleHippy);
        CHECK(typeAll(b, L"zz reser\barched") == ufoaudio::SecretAction::ToggleResearched);
        CHECK(typeAll(b, L"qqq killjukebxx\b\box") == ufoaudio::SecretAction::KillJukebox);
        CHECK(typeAll(b, L"randomwords") == ufoaudio::SecretAction::None);
    }

    if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
    std::printf("ufo_audio ok\n");
    return 0;
}
