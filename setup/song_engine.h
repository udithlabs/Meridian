// The setup's music, the part with no Windows in it (so tests/song_test.cpp can run it on any machine): how the song is kept inside the setup,
// how it is turned back into sound, and the volume (fade in, mute, fade out). song.cpp is the thin Windows part that sends it to the speakers.
//
// How the song is kept: setup\song.dat (made by make_song.py) is the MP3 plus its artist and title, all run through a keystream so that it is not an
// MP3 you could find or cut out of the exe. Its hash is also the key that unlocks the packed Meridian files (payload.h, pack.py), so a setup with
// the song removed or swapped in a resource editor cannot unpack anything: the song is part of the setup, not an extra. To take it out you rebuild
// from the source (replace song.dat with make_song.py, or change this file and payload.h).
//
// Include this from ONE .cpp per program (it carries the whole MP3 decoder, minimp3, which is public domain / CC0).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "song_lock.h"
#include "song_mp3.h"

namespace songeng {

struct Song {
    std::string artist, title;                                      // UTF-8
    std::vector<uint8_t> mp3;
};
// song.dat as stored  ->  the song. false: not a song file.
inline bool unlock(const uint8_t* blob, size_t n, Song& out) {
    if (!blob || n < 12) return false;
    std::vector<uint8_t> b(blob, blob + n); scramble(b.data(), b.size(), kSongSeed);
    if (memcmp(b.data(), "MSG1", 4) != 0) return false;
    uint16_t al, tl; uint32_t ml; memcpy(&al, &b[4], 2); memcpy(&tl, &b[6], 2); memcpy(&ml, &b[8], 4);
    if (12 + (size_t)al + tl + (size_t)ml != n) return false;
    out.artist.assign((const char*)&b[12], al); out.title.assign((const char*)&b[12 + al], tl); out.mp3.assign(b.begin() + 12 + al + tl, b.end());
    return true;
}

struct Pcm { int rate = 0, ch = 0; std::vector<int16_t> s; size_t frames() const { return ch ? s.size() / (size_t)ch : 0; } };
// MP3 -> 16-bit samples. Skips an ID3v2 tag if there is one. false: nothing could be decoded.
inline bool decode(const uint8_t* mp3, size_t n, Pcm& out) {
    size_t pos = 0;
    if (n > 10 && memcmp(mp3, "ID3", 3) == 0) { size_t sz = ((size_t)(mp3[6] & 0x7F) << 21) | ((size_t)(mp3[7] & 0x7F) << 14) | ((size_t)(mp3[8] & 0x7F) << 7) | (mp3[9] & 0x7F); pos = std::min(n, 10 + sz); }
    mp3dec_t d; mp3dec_init(&d);
    static thread_local int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    out = Pcm();
    if (n > pos) out.s.reserve(std::min<size_t>((n - pos) * 6, 32u * 1024u * 1024u));
    while (pos < n) {
        mp3dec_frame_info_t fi; const int samples = mp3dec_decode_frame(&d, mp3 + pos, (int)std::min<size_t>(n - pos, 1 << 30), pcm, &fi);
        if (fi.frame_bytes <= 0) break;
        pos += (size_t)fi.frame_bytes;
        if (samples <= 0 || fi.channels < 1) continue;
        if (!out.rate) { out.rate = fi.hz; out.ch = fi.channels; }
        if (fi.hz != out.rate || fi.channels != out.ch) continue;     // (a stream that changes format half way is not worth playing)
        out.s.insert(out.s.end(), pcm, pcm + (size_t)samples * (size_t)fi.channels);
    }
    return out.rate > 0 && out.frames() > 0;
}

// The volume and the read position. fill() hands out the next stretch of sound, already at the right volume.
struct Mixer {
    const Pcm* pcm = nullptr;
    size_t pos = 0;                      // next frame to play
    float gain = 0.f;                    // the volume right now (0 to 1)
    float target = 1.f;                  // the volume it is moving to
    float perFrame = 0.f;                // how far it moves each frame
    float master = 0.8f;                 // overall level: a setup should not shout
    bool stopping = false;

    void to(float t, float seconds) { target = t; perFrame = seconds <= 0 ? 1.f : 1.f / ((float)(pcm ? pcm->rate : 44100) * seconds); }
    void startFadeIn(float seconds) { gain = 0.f; to(1.f, seconds); }
    void fadeOutAndStop(float seconds) { stopping = true; to(0.f, seconds); }
    // true once there is nothing more to play (the song ended, or the fade-out is complete)
    bool finished() const { return !pcm || pos >= pcm->frames() || (stopping && gain <= 0.f); }
    // writes nFrames frames (silence after the end); returns false when finished
    bool fill(int16_t* out, size_t nFrames) {
        const int ch = pcm ? pcm->ch : 2; const size_t total = pcm ? pcm->frames() : 0;
        for (size_t f = 0; f < nFrames; ++f) {
            if (gain < target) gain = std::min(target, gain + perFrame); else if (gain > target) gain = std::max(target, gain - perFrame);
            if (pos < total && !(stopping && gain <= 0.f)) {
                const int16_t* in = &pcm->s[pos * (size_t)ch]; const float k = gain * master;
                for (int c = 0; c < ch; ++c) { float v = (float)in[c] * k; out[f * (size_t)ch + (size_t)c] = (int16_t)std::lrintf(std::max(-32768.f, std::min(32767.f, v))); }
                ++pos;
            } else for (int c = 0; c < ch; ++c) out[f * (size_t)ch + (size_t)c] = 0;
        }
        return !finished();
    }
};

} // namespace songeng
