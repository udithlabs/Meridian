// A small, self-contained "inflate" (raw deflate, RFC 1951) so the setup needs no zlib and no other DLL.
// Written after the well-known "puff" design: slow-and-simple bit-by-bit Huffman decoding, which is plenty for a few megabytes.
#pragma once
#include <cstdint>
#include <cstddef>

namespace inflate {

struct Huff { uint16_t count[16]; uint16_t symbol[320]; };
struct State {
    const uint8_t* in; size_t inLen, inPos;
    uint32_t bitBuf; int bitCnt;
    uint8_t* out; size_t outLen, outPos;
    bool err;
};

inline int bits(State& s, int need) {
    uint32_t v = s.bitBuf;
    while (s.bitCnt < need) {
        if (s.inPos >= s.inLen) { s.err = true; return 0; }
        v |= (uint32_t)s.in[s.inPos++] << s.bitCnt;
        s.bitCnt += 8;
    }
    s.bitBuf = v >> need; s.bitCnt -= need;
    return (int)(v & ((1u << need) - 1));
}

// returns the number of unused codes (negative: over-subscribed, a broken table)
inline int build(Huff& h, const uint8_t* len, int n) {
    for (int i = 0; i < 16; ++i) h.count[i] = 0;
    for (int i = 0; i < n; ++i) h.count[len[i]]++;
    if (h.count[0] == n) return 0;
    int left = 1;
    for (int l = 1; l < 16; ++l) { left <<= 1; left -= h.count[l]; if (left < 0) return left; }
    uint16_t offs[16]; offs[1] = 0;
    for (int l = 1; l < 15; ++l) offs[l + 1] = (uint16_t)(offs[l] + h.count[l]);
    for (int i = 0; i < n; ++i) if (len[i]) h.symbol[offs[len[i]]++] = (uint16_t)i;
    return left;
}

inline int decode(State& s, const Huff& h) {
    int code = 0, first = 0, index = 0;
    for (int l = 1; l < 16; ++l) {
        code |= bits(s, 1);
        if (s.err) return -1;
        int count = h.count[l];
        if (code - count < first) return h.symbol[index + (code - first)];
        index += count; first += count; first <<= 1; code <<= 1;
    }
    return -1;
}

inline bool codes(State& s, const Huff& lc, const Huff& dc) {
    static const uint16_t lens[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static const uint16_t lext[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static const uint16_t dists[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static const uint16_t dext[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    int sym;
    do {
        sym = decode(s, lc);
        if (sym < 0) return false;
        if (sym < 256) {
            if (s.outPos >= s.outLen) return false;
            s.out[s.outPos++] = (uint8_t)sym;
        } else if (sym > 256) {
            sym -= 257;
            if (sym >= 29) return false;
            size_t len = lens[sym] + (size_t)bits(s, lext[sym]);
            int ds = decode(s, dc);
            if (ds < 0 || ds >= 30) return false;
            size_t dist = dists[ds] + (size_t)bits(s, dext[ds]);
            if (s.err || dist > s.outPos || s.outPos + len > s.outLen) return false;
            while (len--) { s.out[s.outPos] = s.out[s.outPos - dist]; ++s.outPos; }
        }
    } while (sym != 256);
    return !s.err;
}

inline bool stored(State& s) {
    s.bitBuf = 0; s.bitCnt = 0;
    if (s.inPos + 4 > s.inLen) return false;
    unsigned len = s.in[s.inPos] | (s.in[s.inPos + 1] << 8);
    unsigned nlen = s.in[s.inPos + 2] | (s.in[s.inPos + 3] << 8);
    s.inPos += 4;
    if (len != (~nlen & 0xFFFF)) return false;
    if (s.inPos + len > s.inLen || s.outPos + len > s.outLen) return false;
    for (unsigned i = 0; i < len; ++i) s.out[s.outPos++] = s.in[s.inPos++];
    return true;
}

inline bool fixedBlock(State& s) {
    static bool built = false; static Huff lc, dc;
    if (!built) {
        uint8_t l[288]; int i = 0;
        for (; i < 144; ++i) l[i] = 8;
        for (; i < 256; ++i) l[i] = 9;
        for (; i < 280; ++i) l[i] = 7;
        for (; i < 288; ++i) l[i] = 8;
        build(lc, l, 288);
        uint8_t d[30]; for (i = 0; i < 30; ++i) d[i] = 5;
        build(dc, d, 30);
        built = true;
    }
    return codes(s, lc, dc);
}

inline bool dynamicBlock(State& s) {
    static const uint8_t order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    int nlen = bits(s, 5) + 257, ndist = bits(s, 5) + 1, ncode = bits(s, 4) + 4;
    if (s.err || nlen > 286 || ndist > 30) return false;
    uint8_t lengths[320]; int idx;
    for (idx = 0; idx < ncode; ++idx) lengths[order[idx]] = (uint8_t)bits(s, 3);
    for (; idx < 19; ++idx) lengths[order[idx]] = 0;
    Huff lc, dc;
    if (build(lc, lengths, 19) != 0) return false;
    idx = 0;
    while (idx < nlen + ndist) {
        int sym = decode(s, lc);
        if (sym < 0) return false;
        if (sym < 16) lengths[idx++] = (uint8_t)sym;
        else {
            int len = 0, rep;
            if (sym == 16) { if (idx == 0) return false; len = lengths[idx - 1]; rep = 3 + bits(s, 2); }
            else if (sym == 17) rep = 3 + bits(s, 3);
            else rep = 11 + bits(s, 7);
            if (s.err || idx + rep > nlen + ndist) return false;
            while (rep--) lengths[idx++] = (uint8_t)len;
        }
    }
    if (lengths[256] == 0) return false;
    int e = build(lc, lengths, nlen);
    if (e < 0 || (e > 0 && nlen - lc.count[0] != 1)) return false;
    e = build(dc, lengths + nlen, ndist);
    if (e < 0 || (e > 0 && ndist - dc.count[0] != 1)) return false;
    return codes(s, lc, dc);
}

// Inflates `src` (a raw deflate stream) into exactly `dstLen` bytes. Returns false on any damage.
inline bool raw(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen) {
    State s{src, srcLen, 0, 0, 0, dst, dstLen, 0, false};
    int last;
    do {
        last = bits(s, 1);
        int type = bits(s, 2);
        if (s.err) return false;
        bool ok = type == 0 ? stored(s) : type == 1 ? fixedBlock(s) : type == 2 ? dynamicBlock(s) : false;
        if (!ok) return false;
    } while (!last);
    return s.outPos == dstLen;
}

} // namespace inflate

// CRC-32 (the same one zip and zlib use)
namespace crc {
inline uint32_t crc32(const uint8_t* p, size_t n, uint32_t c = 0) {
    static uint32_t table[256]; static bool built = false;
    if (!built) {
        for (uint32_t i = 0; i < 256; ++i) { uint32_t x = i; for (int k = 0; k < 8; ++k) x = x & 1 ? 0xEDB88320u ^ (x >> 1) : x >> 1; table[i] = x; }
        built = true;
    }
    c = ~c;
    for (size_t i = 0; i < n; ++i) c = table[(c ^ p[i]) & 255] ^ (c >> 8);
    return ~c;
}
}
