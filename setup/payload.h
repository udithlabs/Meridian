// The files the setup carries inside itself (see pack.py for the format). Read straight from the program's own resources.
#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include "resource.h"
#include "inflate.h"
#include "song_lock.h"          // the packed files are locked with a key made from the song inside the setup (see song_engine.h)

struct PayloadEntry {
    std::string path;            // 'themes/gta6/theme.json'
    uint8_t group = 0;           // 0 always, 1 GTA VI pack, 2 Halloween pack
    bool deflated = false;
    uint32_t size = 0, stored = 0, crc = 0, offset = 0;
    uint64_t seed = 0;           // the keystream this file was locked with: song key x its place in the blob
    const uint8_t* data = nullptr;
};

struct Payload {
    std::vector<PayloadEntry> e;

    static bool validRelPath(const std::string& p) {
        if (p.empty() || p[0] == '/' || p[0] == '\\') return false;
        size_t segStart = 0;
        for (size_t i = 0; i <= p.size(); ++i) {
            if (i < p.size()) {
                const unsigned char ch = (unsigned char)p[i];
                if (ch == 0 || ch == '\\' || ch == ':' || ch < 0x20u) return false;
            }
            if (i == p.size() || p[i] == '/') {
                const size_t segLen = i - segStart;
                if (segLen == 0) return false;
                if (segLen == 2 && p[segStart] == '.' && p[segStart + 1] == '.') return false;
                segStart = i + 1;
            }
        }
        return true;
    }

    bool loadFromBuffer(const uint8_t* p, size_t n, uint64_t key) {
        if (!key || !p || n < 8 || memcmp(p, "MPK1", 4) != 0) return false;
        uint32_t count = 0; memcpy(&count, p + 4, 4);
        if (count > (n - 8) / 20) return false;
        size_t pos = 8;
        std::vector<PayloadEntry> v;
        v.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            if (pos > n || n - pos < 2) return false;
            uint16_t pl = 0; memcpy(&pl, p + pos, 2); pos += 2;
            if (n - pos < (size_t)pl + 18) return false;
            PayloadEntry en; en.path.assign((const char*)p + pos, pl); pos += pl;
            if (!validRelPath(en.path)) return false;
            en.group = p[pos]; en.deflated = (p[pos + 1] & 1) != 0; pos += 2;
            uint32_t off = 0; memcpy(&en.size, p + pos, 4); memcpy(&en.stored, p + pos + 4, 4); memcpy(&en.crc, p + pos + 8, 4); memcpy(&off, p + pos + 12, 4);
            pos += 16;
            en.offset = off; en.seed = key ^ ((uint64_t)off * 0x9E3779B97F4A7C15ULL);
            en.data = (const uint8_t*)(uintptr_t)off;                      // fixed up below, once the table's end is known
            v.push_back(en);
        }
        for (auto& en : v) {
            const size_t off = (size_t)(uintptr_t)en.data;
            if (pos > n || off > n - pos || (size_t)en.stored > n - pos - off) return false;
            en.data = p + pos + off;
        }
        e.swap(v);
        return true;
    }

    // key: song::key(), the hash of the song inside this setup. Without the right one nothing unpacks (so the song cannot be removed from a built setup).
    bool load(uint64_t key) {
        if (!key) return false;
        HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_PAYLOAD), RT_RCDATA);
        if (!r) return false;
        HGLOBAL h = LoadResource(nullptr, r);
        const uint8_t* p = h ? (const uint8_t*)LockResource(h) : nullptr;
        size_t n = SizeofResource(nullptr, r);
        return loadFromBuffer(p, n, key);
    }

    // unpacks one file and checks its CRC. false: the setup file itself is damaged.
    static bool unpack(const PayloadEntry& en, std::vector<uint8_t>& out) {
        out.assign(en.size, 0);
        std::vector<uint8_t> raw(en.data, en.data + en.stored);                 // (the resource is read-only: unlock a copy)
        songeng::scramble(raw.data(), raw.size(), en.seed);
        if (en.deflated) { if (!inflate::raw(raw.data(), raw.size(), out.data(), out.size())) return false; }
        else { if (en.stored != en.size) return false; if (en.size) memcpy(out.data(), raw.data(), en.size); }
        return crc::crc32(out.data(), out.size()) == en.crc;
    }
};
