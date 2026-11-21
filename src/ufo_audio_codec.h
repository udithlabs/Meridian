// Meridian: embedded audio clip loader and stereo fly-by panner for the Midnight UFO easter egg.
// Pure C++17 (no Windows headers) so it can be unit-tested standalone and shared with build tools.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>

namespace ufoaudio {

// Binary format ("MUF1"): 16-byte little-endian header followed by signed 16-bit mono PCM samples.
//   bytes 0..3   : "MUF1"
//   bytes 4..7   : sampleRate (Hz, e.g. 44100)
//   bytes 8..11  : totalSamples
//   bytes 12..15 : reserved (0)
//   bytes 16..   : int16_t mono[totalSamples] (little-endian)
constexpr char kMagic[4] = {'M', 'U', 'F', '1'};

struct Clip {
    uint32_t sampleRate = 0;
    std::vector<int16_t> mono;
};

inline uint32_t readU32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
inline void writeU32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

inline std::vector<uint8_t> encodeClip(const int16_t* mono, uint32_t totalSamples, uint32_t sampleRate) {
    std::vector<uint8_t> out;
    if (!mono || totalSamples == 0 || sampleRate < 8000 || sampleRate > 192000) return out;
    out.resize(16 + (size_t)totalSamples * 2);
    std::memcpy(out.data(), kMagic, 4);
    writeU32(out.data() + 4, sampleRate);
    writeU32(out.data() + 8, totalSamples);
    writeU32(out.data() + 12, 0);
    for (uint32_t i = 0; i < totalSamples; ++i) {
        const uint16_t u = (uint16_t)mono[i];
        out[16 + (size_t)i * 2]     = (uint8_t)(u & 0xFF);
        out[16 + (size_t)i * 2 + 1] = (uint8_t)((u >> 8) & 0xFF);
    }
    return out;
}

inline bool decodeClip(const uint8_t* data, size_t size, Clip& out) {
    out = Clip{};
    if (!data || size < 16 || std::memcmp(data, kMagic, 4) != 0) return false;
    const uint32_t sr = readU32(data + 4);
    const uint32_t total = readU32(data + 8);
    if (sr < 8000 || sr > 192000 || total == 0 || total > 4800000) return false;
    if (size < 16 + (size_t)total * 2) return false;
    out.sampleRate = sr;
    out.mono.resize(total);
    for (uint32_t i = 0; i < total; ++i) {
        const uint16_t u = (uint16_t)data[16 + (size_t)i * 2] | ((uint16_t)data[16 + (size_t)i * 2 + 1] << 8);
        out.mono[i] = (int16_t)u;
    }
    return true;
}

// Converts an interleaved 16-bit stereo buffer (e.g. decoded from AAC) into a normalized mono fly-by Clip,
// trimming leading encoder padding up to `trimFrames` and capping length to `maxFrames`.
inline bool buildFlybyMonoClip(const int16_t* stereo, size_t stereoSamples, uint32_t sampleRate, Clip& out,
                               size_t trimFrames = 1024, size_t maxFrames = 44100 * 10 + 22050) {
    out = Clip{};
    if (!stereo || stereoSamples < 2 || sampleRate < 8000 || sampleRate > 192000) return false;
    const size_t totalFrames = stereoSamples / 2;
    const size_t start = totalFrames > trimFrames + 1024 ? trimFrames : 0;
    const size_t count = std::min(maxFrames, totalFrames - start);
    if (count == 0) return false;
    out.sampleRate = sampleRate;
    out.mono.resize(count);
    int peak = 1;
    for (size_t i = 0; i < count; ++i) {
        const size_t k = (start + i) * 2;
        const int m = ((int)stereo[k] + (int)stereo[k + 1]) / 2;
        const int a = m < 0 ? -m : m;
        if (a > peak) peak = a;
        out.mono[i] = (int16_t)m;
    }
    if (peak > 64 && peak < 30000) {
        const float scale = 30146.f / (float)peak;                 // ~92% peak headroom
        for (int16_t& s : out.mono) {
            const int v = (int)std::lround((float)s * scale);
            s = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
        }
    }
    return true;
}

// ---------------------------------------------------------------- stereo fly-by panning
struct PanGains {
    float left;
    float right;
};

// Equal-power stereo panning law:
//   pan = +1.0 -> full right speaker (left = 0.0, right = 1.0)
//   pan =  0.0 -> dead center       (left = 0.707, right = 0.707)
//   pan = -1.0 -> full left speaker (left = 1.0, right = 0.0)
inline PanGains equalPowerPan(float pan) {
    if (pan < -1.f) pan = -1.f;
    if (pan > 1.f) pan = 1.f;
    const float theta = (pan + 1.f) * 0.7853981633974483f;   // (pan + 1) * pi / 4
    float l = std::cos(theta);
    float r = std::sin(theta);
    if (l < 1e-6f) l = 0.f;
    if (r < 1e-6f) r = 0.f;
    return {l, r};
}

// Computes target stereo pan (-1..+1) and fly-by envelope gain (0..1) from:
//   xNorm = saucer X / screen width (1.16 -> -0.16 when flying right-to-left)
//   u     = normalized flight progress [0..1]
inline void flybyParams(float xNorm, float u, float& panOut, float& gainOut) {
    auto clamp01f = [](float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); };
    auto smooth = [&](float v) { v = clamp01f(v); return v * v * (3.f - 2.f * v); };
    // Map [0.05..0.95] of the screen width to full [-1..+1] stereo pan so the song starts
    // purely in the right speaker as the alien enters, crosses center smoothly in the middle,
    // and finishes purely in the left speaker as it exits left.
    const float xn = clamp01f((xNorm - 0.05f) / 0.90f);
    panOut = xn * 2.f - 1.f;
    const float fadeIn = smooth(u / 0.09f);
    const float fadeOut = 1.f - smooth((u - 0.90f) / 0.10f);
    // Subtle proximity swell: louder as the saucer passes center, softer near screen edges.
    const float prox = 0.62f + 0.38f * (1.f - 0.75f * panOut * panOut);
    gainOut = fadeIn * fadeOut * prox;
}

// Mixes `frames` stereo interleaved 16-bit samples from a mono clip with per-sample smoothed
// pan and gain, advancing `pos` (wrapping cleanly if the clip ever reaches its end).
struct StereoMixer {
    const Clip* clip = nullptr;
    size_t pos = 0;
    float curPan = 1.f;
    float curGain = 0.f;
    float masterVol = 0.72f;

    void reset(float initialPan) {
        pos = 0;
        curPan = std::max(-1.f, std::min(1.f, initialPan));
        curGain = 0.f;
    }

    void fill(int16_t* dstStereo, size_t frames, float targetPan, float targetGain, float slewPan, float slewGain) {
        if (!clip || clip->mono.empty() || frames == 0) {
            if (dstStereo && frames) std::memset(dstStereo, 0, frames * 2 * sizeof(int16_t));
            return;
        }
        const size_t n = clip->mono.size();
        const int16_t* src = clip->mono.data();
        targetPan = std::max(-1.f, std::min(1.f, targetPan));
        targetGain = std::max(0.f, std::min(1.f, targetGain));
        for (size_t i = 0; i < frames; ++i) {
            curPan += (targetPan - curPan) * slewPan;
            curGain += (targetGain - curGain) * slewGain;
            const float sample = (float)src[pos] * (curGain * masterVol);
            if (++pos >= n) pos = 0;
            const PanGains pg = equalPowerPan(curPan);
            const int il = (int)std::lround(sample * pg.left);
            const int ir = (int)std::lround(sample * pg.right);
            dstStereo[i * 2]     = (int16_t)(il > 32767 ? 32767 : (il < -32768 ? -32768 : il));
            dstStereo[i * 2 + 1] = (int16_t)(ir > 32767 ? 32767 : (ir < -32768 ? -32768 : ir));
        }
    }
};

// ---------------------------------------------------------------- hidden Settings > About cheat codes
enum class SecretAction { None = 0, DisableAliens, EnableAliens, ToggleHippy, ToggleResearched, AliensNuts, AliensNormal, ToggleJukebox, KillJukebox };

// Feeds a character or ASCII virtual key typed blindly on Settings > About (no visible text box, like classic game cheat codes).
// 100% case-insensitive, tolerates Backspace and spaces/apostrophes, and matches suffixes so stray earlier typing never breaks a code.
inline SecretAction feedSecretChar(std::string& buf, wchar_t ch) {
    if (ch == L'\b') {
        if (!buf.empty()) buf.pop_back();
        return SecretAction::None;
    }
    if ((ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z')) {
        buf.push_back((char)(ch >= L'A' && ch <= L'Z' ? (ch - L'A' + 'a') : ch));
        if (buf.size() > 32) buf.erase(0, buf.size() - 32);
        auto endsWith = [&](const char* w) {
            const size_t n = std::strlen(w);
            return buf.size() >= n && buf.compare(buf.size() - n, n, w) == 0;
        };
        if (endsWith("aliensdontexist") || endsWith("aliensdontexisit")) {
            buf.clear();
            return SecretAction::DisableAliens;
        }
        if (endsWith("aliensdoexisit") || endsWith("aliensdoexist")) {
            buf.clear();
            return SecretAction::EnableAliens;
        }
        if (endsWith("aliensarenuts")) {
            buf.clear();
            return SecretAction::AliensNuts;
        }
        if (endsWith("aliennohere") || endsWith("aliensnohere")) {
            buf.clear();
            return SecretAction::AliensNormal;
        }
        if (endsWith("hippyshit") || endsWith("hippieshit")) {
            buf.clear();
            return SecretAction::ToggleHippy;
        }
        if (endsWith("researched")) {
            buf.clear();
            return SecretAction::ToggleResearched;
        }
        if (endsWith("killjukebox")) {                              // checked BEFORE "jukebox" since "killjukebox" ends with "jukebox"
            buf.clear();
            return SecretAction::KillJukebox;
        }
        if (endsWith("jukebox")) {
            buf.clear();
            return SecretAction::ToggleJukebox;
        }
    }
    return SecretAction::None;
}

} // namespace ufoaudio
