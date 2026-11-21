#!/usr/bin/env python3
"""Builds the two data files the setup program carries inside itself.

  python3 pack.py <app-folder> <licenses-folder> <settings-source.cpp> <out-folder>

  <app-folder>      the folder with Meridian.exe, README.txt and themes/ (what the setup installs)
  <licenses-folder> the licences (installed to <install>\\licenses)
  <settings-source> src/scene_settings.cpp: the "what's new" list is read from it, so the setup always shows the same notes as the app
  <out-folder>      gets payload.bin (all files, each deflated, with a CRC) and notes.txt

payload.bin:  "MPK1", u32 count, then per file: u16 pathLength, path (UTF-8, '/' separators), u8 group, u8 flags (1 = deflated, 0 = stored),
              u32 size, u32 storedSize (bytes in the blob), u32 crc32 (of the unpacked file), u32 offset (counts from the end of the table), then the data
              (each file's stored bytes are XORed with a keystream seeded from the hash of song.dat and the file's offset: payload.h undoes it).
groups: 0 = always installed, 1 = GTA VI theme pack, 2 = Halloween theme pack.
"""
import ast, os, re, struct, sys, zlib
sys.dont_write_bytecode = True                   # (no __pycache__ folder next to the sources)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_song                                  # the keystream arithmetic (also in song_lock.h)

def collect(app, lic):
    files = []                                    # (relative path, absolute path, group)
    def add(rel, absp, group): files.append((rel.replace(os.sep, "/"), absp, group))
    add("Meridian.exe", os.path.join(app, "Meridian.exe"), 0)
    add("README.txt", os.path.join(app, "README.txt"), 0)
    for root, _, names in os.walk(lic):
        for n in sorted(names):
            if n == "Anton-OFL.txt": continue
            p = os.path.join(root, n); add(os.path.join("licenses", os.path.relpath(p, lic)), p, 0)
    themes = os.path.join(app, "themes")
    add("themes/README.txt", os.path.join(themes, "README.txt"), 0)
    for sub, group in (("_template", 0), ("gta6", 1), ("halloween", 2)):
        base = os.path.join(themes, sub)
        for root, dirs, names in os.walk(base):
            dirs[:] = [d for d in sorted(dirs) if d not in ("_source", "alternatives")]
            for n in sorted(names):
                if n in ("Anton-OFL.txt", "ArchivoBlack-OFL.txt", "BigShoulders-OFL.txt"): continue
                p = os.path.join(root, n); add(os.path.join("themes", sub, os.path.relpath(p, base)), p, group)
    files.sort(key=lambda f: (f[2], f[0]))
    return files

def notes(cpp):
    src = open(cpp, encoding="utf-8").read()
    body = src[src.index("static const std::vector<Release> r = {"):]
    body = body[:body.index("\n    };")]
    out = []
    for m in re.finditer(r'\{"([0-9.x]+)", "([^"]*)", \{(.*?)\n        \}(?:, "([^"]*)")?\}', body, re.S):      # (a version may end with its release date: }, "15 Nov 2026"},)
        out.append(f"## {m.group(1)} | {m.group(2)}" + (f" | {m.group(4)}" if m.group(4) else ""))
        for s in re.finditer(r'"((?:[^"\\]|\\.)*)"', m.group(3)):
            out.append("- " + ast.literal_eval('"' + s.group(1) + '"'))
        out.append("")
    return "\n".join(out)

def main():
    app, lic, cpp, out = sys.argv[1:5]
    os.makedirs(out, exist_ok=True)
    files = collect(app, lic)
    key = make_song.hash64(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "song.dat"), "rb").read())    # locks every packed file (payload.h)
    table, data = [], bytearray()
    for rel, absp, group in files:
        raw = open(absp, "rb").read()
        c = zlib.compressobj(9, zlib.DEFLATED, -15); packed = c.compress(raw) + c.flush()
        flag = 1
        if len(packed) >= len(raw): packed, flag = raw, 0                # not worth deflating: stored as it is
        packed = make_song.scramble(packed, key ^ ((len(data) * 0x9E3779B97F4A7C15) & make_song.M64))
        table.append((rel.encode("utf-8"), group, flag, len(raw), len(packed), zlib.crc32(raw) & 0xFFFFFFFF, len(data)))
        data += packed
    head = b"MPK1" + struct.pack("<I", len(table))
    tab = bytearray()
    for rel, group, flag, raw, stored, crc, off in table:
        tab += struct.pack("<H", len(rel)) + rel + struct.pack("<BBIIII", group, flag, raw, stored, crc, off)
    open(os.path.join(out, "payload.bin"), "wb").write(head + tab + data)
    n = notes(cpp)
    open(os.path.join(out, "notes.txt"), "w", encoding="utf-8", newline="\n").write(n)
    total = sum(t[3] for t in table)
    print(f"payload.bin: {len(table)} files, {total/1e6:.2f} MB -> {(len(head)+len(tab)+len(data))/1e6:.2f} MB;  notes.txt: {n.count('## ')} versions")

main()
