#!/usr/bin/env python3
"""Makes song.dat, the song the setup plays while it installs, updates and uninstalls.

  python3 make_song.py <song.mp3> "<Artist>" "<Title>" [out=song.dat]

It keeps only the sound (the ID3 tags and cover art are cut off), adds the artist and title the setup shows (hidden, on the Customise page), and runs it all
through the keystream in song_engine.h, so it is not a loose MP3 anywhere. build.sh / build-setup.bat pack song.dat into the setup, and pack.py also uses its
hash to lock the packed Meridian files, so the song cannot be cut out of a finished setup. To change the song, run this again and rebuild.
"""
import struct, sys

SEED = 0x4D45524944494E31
M64 = (1 << 64) - 1

def mix64(z):
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & M64
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & M64
    return z ^ (z >> 31)

def scramble(data, seed):
    out = bytearray(data); st = seed; i = 0; n = len(out)
    while i < n:
        st = (st + 0x9E3779B97F4A7C15) & M64
        z = mix64(st)
        for b in range(8):
            if i >= n: break
            out[i] ^= (z >> (8 * b)) & 0xFF; i += 1
    return bytes(out)

def hash64(data):
    h = 0xCBF29CE484222325
    for c in data: h = ((h ^ c) * 0x100000001B3) & M64
    return mix64(h) | 1

def strip_id3(mp3):
    if mp3[:3] == b"ID3":
        size = ((mp3[6] & 0x7F) << 21) | ((mp3[7] & 0x7F) << 14) | ((mp3[8] & 0x7F) << 7) | (mp3[9] & 0x7F)
        mp3 = mp3[10 + size:]
    if len(mp3) > 128 and mp3[-128:-125] == b"TAG": mp3 = mp3[:-128]
    return mp3

def build(mp3, artist, title):
    a, t = artist.encode("utf-8"), title.encode("utf-8")
    return scramble(b"MSG1" + struct.pack("<HHI", len(a), len(t), len(mp3)) + a + t + mp3, SEED)

if __name__ == "__main__":
    if len(sys.argv) < 4:
        print(__doc__); sys.exit(1)
    src, artist, title = sys.argv[1:4]
    out = sys.argv[4] if len(sys.argv) > 4 else "song.dat"
    mp3 = strip_id3(open(src, "rb").read())
    blob = build(mp3, artist, title)
    open(out, "wb").write(blob)
    print(f"{out}: {len(blob)/1e6:.2f} MB, hash {hash64(blob):016x}")
