#!/usr/bin/env python3
"""Draws the city of the GTA VI theme pack (three layers of skyline, a Ferris wheel) into ../art, and writes marks.json with the
places where the theme puts blinking lights (beacons on masts, neon signs, floodlights).

You do not need this to use the theme: the PNG files in ../art are already made. It is here so the city can be changed and made again:
    python3 make_city.py        (needs the program  rsvg-convert  from librsvg, or change PNG() to use cairosvg)

The drawing is original: made-up towers in an Art Deco / Miami-style silhouette. Nothing here is taken from a game.
"""
import json, math, os, random, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.environ.get("CITY_OUT", os.path.join(HERE, "..", "art"))
W, H = 3840, 640                     # one layer: 6 : 1, the buildings stand on the bottom edge
SUN_X = W / 2.0

def PNG(svg_text, name, width):
    path = os.path.join(OUT, name)
    p = subprocess.run(["rsvg-convert", "-w", str(width), "-o", path], input=svg_text.encode(), capture_output=True)
    if p.returncode: sys.exit("rsvg-convert failed: " + p.stderr.decode())
    print("wrote", path, os.path.getsize(path), "bytes")

def hexmix(a, b, t):
    a = [int(a[i:i + 2], 16) for i in (1, 3, 5)]; b = [int(b[i:i + 2], 16) for i in (1, 3, 5)]
    return "#%02X%02X%02X" % tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))

class Layer:
    def __init__(s, seed, top, bot, rim, rim_a, win_colors, lit_p, dot_alpha=0.0):
        s.r = random.Random(seed); s.top, s.bot, s.rim, s.rim_a = top, bot, rim, rim_a
        s.wc, s.lit_p, s.dot_alpha = win_colors, lit_p, dot_alpha
        s.parts, s.win, s.marks, s.rims = [], {}, [], []
        s.body_color = hexmix(top, bot, 0.5)

    # --- drawing helpers (everything is added to one list, windows are grouped by colour into single paths)
    def poly(s, pts, fill="url(#b)", extra=""):
        s.parts.append('<polygon points="%s" fill="%s" %s/>' % (" ".join("%.1f,%.1f" % p for p in pts), fill, extra))
    def rect(s, x, y, w, h, fill="url(#b)", extra=""):
        s.parts.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="%s" %s/>' % (x, y, w, h, fill, extra))
    def line(s, x1, y1, x2, y2, color, sw, extra=""):
        s.parts.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="%s" stroke-width="%.1f" stroke-linecap="round" %s/>' % (x1, y1, x2, y2, color, sw, extra))
    def win(s, x, y, w, h, color):
        s.win.setdefault(color, []).append("M%.1f %.1fh%.1fv%.1fh-%.1fz" % (x, y, w, h, w))
    def mark(s, kind, x, y, color, rate=1.0):
        s.marks.append({"kind": kind, "x": round(x / W, 5), "y": round(y / H, 5), "color": color, "rate": rate})
    def rim_light(s, x, y, w, h):
        cx = x + w / 2.0
        if s.rim_a <= 0: return
        if cx < SUN_X: s.rects_rim(x + w - 3.5, y + 2, 3.5, h - 2)
        else: s.rects_rim(x, y + 2, 3.5, h - 2)
        s.rects_rim(x, y, w, 2.2, 0.55)
    def rects_rim(s, x, y, w, h, k=1.0):
        s.rims.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="%s" fill-opacity="%.2f"/>' % (x, y, w, h, s.rim, s.rim_a * k))

    def windows(s, x, y, w, h, cw=15, ch=22, ww=6.5, wh=10, margin=9, lit_p=None, strips=0.0):
        p = s.lit_p if lit_p is None else lit_p
        cols = int((w - 2 * margin) // cw); rows = int((h - 2 * margin) // ch)
        if cols < 1 or rows < 1: return
        x0 = x + (w - cols * cw) / 2.0 + (cw - ww) / 2.0
        for rr in range(rows):
            if s.r.random() < strips:        # a whole floor lit
                col = s.r.choice(s.wc)
                s.win.setdefault(col, []).append("M%.1f %.1fh%.1fv%.1fh-%.1fz" % (x + margin, y + margin + rr * ch + 4, w - 2 * margin, 5, w - 2 * margin))
                continue
            for cc in range(cols):
                if s.r.random() < p: s.win_one(x0 + cc * cw, y + margin + rr * ch + 4, ww, wh)
                elif s.dot_alpha > 0 and s.r.random() < 0.5: s.win.setdefault("#FFFFFF", []).append("M%.1f %.1fh%.1fv%.1fh-%.1fz" % (x0 + cc * cw, y + margin + rr * ch + 4, ww, wh, ww))
    def win_one(s, x, y, w, h):
        s.win.setdefault(s.r.choice(s.wc), []).append("M%.1f %.1fh%.1fv%.1fh-%.1fz" % (x, y, w, h, w))

    # --- building types
    def slab(s, x, w, h, tall=False):
        y = H - h
        s.rect(x, y, w, h)
        if s.r.random() < 0.55: s.rect(x + w * s.r.uniform(0.12, 0.5), y - s.r.uniform(8, 18), w * s.r.uniform(0.2, 0.4), 18)
        if s.r.random() < (0.5 if tall else 0.2):
            ah = s.r.uniform(30, 90 if tall else 40); ax = x + w * s.r.uniform(0.3, 0.7)
            s.line(ax, y, ax, y - ah, s.body_color, 3.2)
            if tall and h > 250: s.mark("beacon", ax, y - ah, "#FF3040", s.r.uniform(0.7, 1.3))
        s.windows(x, y, w, h)
        s.rim_light(x, y, w, h)
    def stepped(s, x, w, h):
        tiers = [(1.0, 0.56), (0.76, 0.22), (0.54, 0.14), (0.32, 0.08)]
        yb = H
        cx = x + w / 2.0
        for i, (tw, th) in enumerate(tiers):
            tx = cx - w * tw / 2.0; hh = h * th
            s.rect(tx, yb - hh, w * tw, hh)
            s.windows(tx, yb - hh, w * tw, hh, lit_p=s.lit_p * 0.9)
            s.rim_light(tx, yb - hh, w * tw, hh)
            yb -= hh
        spire = s.r.uniform(40, 90) + h * 0.06
        s.poly([(cx - w * 0.05, yb), (cx + w * 0.05, yb), (cx, yb - spire)])
        s.mark("beacon", cx, yb - spire, "#FF3040", 0.9)
    def deco_round(s, x, w, h):
        y = H - h; r = w / 2.0
        s.parts.append('<path d="M%.1f %.1fV%.1fA%.1f %.1f 0 0 1 %.1f %.1fV%.1fz" fill="url(#b)"/>' % (x, H, y + r, r, r, x + w, y + r, H))
        s.windows(x, y + r * 0.8, w, h - r * 0.8, cw=13, ch=24, ww=5, wh=14)
        s.rim_light(x, y + r, w, h - r)
        s.line(x + w / 2, y, x + w / 2, y - 26, s.body_color, 3)
    def twin(s, x, w, h):
        tw = w * 0.38
        for tx in (x, x + w - tw):
            s.rect(tx, H - h, tw, h); s.windows(tx, H - h, tw, h, cw=14); s.rim_light(tx, H - h, tw, h)
            s.rect(tx + tw * 0.2, H - h - 12, tw * 0.6, 12)
        s.rect(x + tw - 2, H - h * 0.72, w - 2 * tw + 4, 9)
        s.win.setdefault(s.wc[0], []).append("M%.1f %.1fh%.1fv3h-%.1fz" % (x + tw, H - h * 0.72 + 3, w - 2 * tw, w - 2 * tw))
        s.mark("beacon", x + tw / 2, H - h - 12, "#FF3040", 1.1); s.mark("beacon", x + w - tw / 2, H - h - 12, "#FF3040", 1.4)
    def slant(s, x, w, h):
        y = H - h; drop = min(60, h * 0.2)
        flip = s.r.random() < 0.5
        s.poly([(x, H), (x, y + (drop if not flip else 0)), (x + w, y + (0 if not flip else drop)), (x + w, H)])
        s.windows(x, y + drop, w, h - drop); s.rim_light(x, y + drop, w, h - drop)
    def hotel(s, x, w, h, neon="#FF4FA3"):
        y = H - h
        s.rect(x, y, w, h); s.rect(x + w * 0.08, y - 22, w * 0.84, 22); s.rect(x + w * 0.2, y - 38, w * 0.6, 16)
        s.windows(x, y, w, h, cw=17, ch=26, ww=8, wh=11, strips=0.12); s.rim_light(x, y, w, h)
        # the neon sign on the roof: a frame with bars inside
        sw, sh = w * 0.5, 38; sx, sy = x + (w - sw) / 2.0, y - 38 - sh - 4
        s.rect(sx + 6, sy + sh + 4, 5, 8); s.rect(sx + sw - 11, sy + sh + 4, 5, 8)
        s.parts.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" rx="6" fill="#12052A" stroke="%s" stroke-width="3.5"/>' % (sx, sy, sw, sh, neon))
        n = 6
        for i in range(n): s.parts.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="%s" fill-opacity="0.9"/>' % (sx + 14 + i * (sw - 28) / n, sy + 11, (sw - 28) / n * 0.62, sh - 22, neon if i % 2 == 0 else "#33E6D4"))
        s.mark("neon", sx + sw / 2, sy + sh / 2, neon, 1.0)
    def mast(s, x, w, h):
        s.slab(x, w, h * 0.55)
        base = H - h * 0.55; mh = h * 0.7; mx = x + w / 2.0
        s.parts.append('<path d="M%.1f %.1fL%.1f %.1fL%.1f %.1f" fill="none" stroke="%s" stroke-width="4"/>' % (mx - 26, base, mx, base - mh, mx + 26, base, s.body_color))
        for i in range(1, 8):
            f = i / 8.0; yy = base - mh * f * 0.92; half = 26 * (1 - f * 0.92)
            s.line(mx - half, yy, mx + half, yy, s.body_color, 2.4)
            if i < 7: s.line(mx - half, yy, mx + 26 * (1 - (f + 1 / 8.0) * 0.92), base - mh * (f + 1 / 8.0) * 0.92, s.body_color, 1.8)
        for f in (0.45, 0.72, 1.0): s.mark("beacon", mx, base - mh * f * (0.98 if f < 1 else 1.0), "#FF3040", 0.8 + f * 0.6)
        s.line(mx, base - mh, mx, base - mh - 26, s.body_color, 3)
    def stadium(s, x, w):
        h = 70; y = H - h
        s.parts.append('<path d="M%.1f %.1fL%.1f %.1fQ%.1f %.1f %.1f %.1fL%.1f %.1fz" fill="url(#b)"/>' % (x, H, x + 22, y + 14, x + w / 2, y - 26, x + w - 22, y + 14, x + w, H))
        s.windows(x + 30, y + 22, w - 60, h - 22, cw=12, ch=16, ww=7, wh=6, margin=4, lit_p=0.5)
        for fx in (x + 18, x + w - 18):
            s.line(fx, y + 14, fx, y - 60, s.body_color, 4); s.rect(fx - 14, y - 74, 28, 14, "#12052A")
            s.mark("flood", fx, y - 67, "#FFF3D6", 1.0)
        s.line(x + w * 0.3, y - 4, x + w * 0.3, y - 48, s.body_color, 3); s.line(x + w * 0.7, y - 4, x + w * 0.7, y - 48, s.body_color, 3)
        s.mark("flood", x + w * 0.3, y - 52, "#FFF3D6", 1.0); s.mark("flood", x + w * 0.7, y - 52, "#FFF3D6", 1.0)

    def crane(s, x, facing=1, scale=1.0):
        c = s.body_color; hh = 190 * scale; leg = 70 * scale; boom = 260 * scale
        s.line(x, H, x, H - hh, c, 6 * scale); s.line(x + leg, H, x + leg, H - hh, c, 6 * scale)
        s.line(x, H - hh * 0.45, x + leg, H - hh, c, 3 * scale); s.line(x + leg, H - hh * 0.45, x, H - hh, c, 3 * scale)
        s.rect(x - 8 * scale, H - hh - 12 * scale, leg + 16 * scale, 14 * scale, c)
        s.line(x + leg / 2 - facing * 6 * scale, H - hh - 6 * scale, x + leg / 2 + facing * boom, H - hh - 14 * scale, c, 5 * scale)
        tipx = x + leg / 2 + facing * boom
        s.line(tipx - facing * 22 * scale, H - hh - 12 * scale, tipx - facing * 22 * scale, H - hh * 0.55, c, 2 * scale)
        s.rect(tipx - facing * 22 * scale - 8 * scale, H - hh * 0.55, 16 * scale, 8 * scale, c)
        s.mark("beacon", tipx, H - hh - 14 * scale, "#FF3040", 0.9 + (x % 7) / 10.0)
    def containers(s, x, w):
        xx = x
        while xx < x + w:
            cw = s.r.choice([34, 40, 46]); rows = s.r.randint(1, 4)
            for rr in range(rows):
                s.rect(xx, H - (rr + 1) * 17, cw - 2, 15, hexmix(s.top, "#5A2A8A", s.r.uniform(0.0, 0.35)))
            xx += cw
    def lighthouse(s, x):
        c = s.body_color
        s.poly([(x - 60, H), (x - 30, H - 26), (x + 24, H - 30), (x + 70, H)])
        s.poly([(x - 12, H - 28), (x + 12, H - 28), (x + 8, H - 168), (x - 8, H - 168)])
        s.rect(x - 14, H - 178, 28, 10); s.rect(x - 10, H - 202, 20, 24, "#FFE9A8", 'fill-opacity="0.95"')
        s.poly([(x - 14, H - 202), (x + 14, H - 202), (x, H - 222)]); s.mark("lighthouse", x, H - 190, "#FFE9A8", 1.0)
    def pier(s, x0, x1):
        c = s.body_color
        s.rect(x0, H - 16, x1 - x0, 6, c)
        for px in range(int(x0), int(x1), 34): s.rect(px, H - 10, 4, 10, c)
        for px in range(int(x0) + 10, int(x1), 64):
            s.line(px, H - 16, px, H - 40, c, 2.4); s.win.setdefault("#FFD9A0", []).append("M%.1f %.1fh8v5h-8z" % (px - 4, H - 46))

    def svg(s):
        d = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">' % (W, H, W, H),
             '<defs><linearGradient id="b" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="%s"/><stop offset="1" stop-color="%s"/></linearGradient></defs>' % (s.top, s.bot)]
        d += s.parts
        d += s.rims
        for col, ps in s.win.items():
            op = 0.92 if col != "#FFFFFF" else s.dot_alpha
            d.append('<path d="%s" fill="%s" fill-opacity="%.2f"/>' % ("".join(ps), col, op))
        d.append("</svg>")
        return "".join(d)

def profile(r, x, hmax, lo):
    """height of a building at x: low in the middle (the sun shows between), tall toward the sides"""
    e = abs(x / W - 0.5) * 2.0
    base = lo + (1.0 - lo) * min(1.0, e ** 0.85)
    return hmax * base * (0.62 + 0.5 * r.random())

def fill_city(L, hmax, wmin, wmax, lo, skip, kinds, tall_kinds):
    x = -20.0
    while x < W:
        w = L.r.uniform(wmin, wmax); cx = x + w / 2
        if any(a - 10 < cx < b + 10 for a, b in skip): x = max(x + 6, min(b for a, b in skip if a - 10 < cx < b + 10)) + 4; continue
        h = min(profile(L.r, cx, hmax, lo), hmax * 1.05)
        pool = tall_kinds if h > hmax * 0.5 else kinds
        kind = L.r.choice(pool)
        h = max(h, 50 if kind != "stepped" else 120)
        getattr(L, kind)(x, w, h) if kind != "slab" else L.slab(x, w, h, tall=h > hmax * 0.5)
        x += w + L.r.uniform(0, 12)

def build():
    os.makedirs(OUT, exist_ok=True)
    marks = {"far": [], "mid": [], "near": []}
    # ---- far: hazy, pink-violet, hardly any windows
    F = Layer(11, "#8A3A98", "#6A2A86", "#FFB07A", 0.35, ["#FFD9A0", "#FF9CCB"], 0.04, dot_alpha=0.0)
    fill_city(F, 330, 46, 100, 0.18, [], ["slab", "slab", "slant", "deco_round"], ["slab", "stepped", "slant", "deco_round", "twin"])
    PNG(F.svg(), "city_far.png", W)
    # ---- mid: the main skyline: deep violet, lit windows, rim light from the sun, neon on the hotels
    M = Layer(23, "#4E1B78", "#2C0E52", "#FF8A5C", 0.7, ["#FFC27A", "#FF6EB3", "#33E6D4", "#FFF0D6"], 0.22, dot_alpha=0.10)
    land = [(0.115, 150, 560, "stepped"), (0.285, 90, 470, "mast"), (0.20, 110, 300, "hotel"), (0.35, 150, 80, "stadium"),
            (0.885, 140, 540, "stepped"), (0.79, 100, 420, "twin"), (0.935, 120, 330, "hotel"), (0.64, 100, 210, "hotel"), (0.04, 110, 420, "deco_round")]
    skip = []
    for fx, w, h, kind in land:
        x = fx * W - w / 2.0; skip.append((x - 6, x + w + 6))
        if kind == "stepped": M.stepped(x, w, h)
        elif kind == "mast": M.mast(x, w, h)
        elif kind == "hotel": M.hotel(x, w, h, "#FF4FA3" if fx < 0.5 else "#33E6D4")
        elif kind == "twin": M.twin(x, w, h)
        elif kind == "stadium": M.stadium(x, w)
        elif kind == "deco_round": M.deco_round(x, w, h)
    fill_city(M, 470, 62, 150, 0.16, skip, ["slab", "slab", "slant", "deco_round", "slab"], ["slab", "stepped", "slant", "deco_round", "twin", "slab", "slab"])
    PNG(M.svg(), "city_mid.png", W)
    # ---- near: darkest, low things in front: warehouses, port cranes and containers, the lighthouse, the pier
    N = Layer(37, "#1F0A3C", "#12052A", "#FF9A66", 0.45, ["#FFC27A", "#FF6EB3", "#33E6D4"], 0.16, dot_alpha=0.0)
    for cx, f, sc in ((0.19, 1, 1.0), (0.235, 1, 0.82)): N.crane(cx * W, f, sc)
    N.containers(0.255 * W, 300)
    N.containers(0.145 * W, 160)
    N.lighthouse(0.655 * W)
    N.pier(0.69 * W, 0.84 * W)
    skip_n = [(0.14 * W, 0.30 * W), (0.64 * W, 0.85 * W)]
    fill_city(N, 210, 70, 190, 0.28, skip_n, ["slab", "slab", "slab", "slant", "hotel"], ["slab", "slant", "slab", "hotel"])
    PNG(N.svg(), "city_near.png", W)
    marks["mid"] = M.marks; marks["near"] = N.marks; marks["far"] = F.marks
    json.dump(marks, open(os.path.join(OUT, "..", "_source", "marks.json") if os.path.isdir(os.path.join(OUT, "..", "_source")) else os.path.join(OUT, "marks.json"), "w"), indent=1)
    # ---- the Ferris wheel (turned by the theme)
    S = 512
    cx = cy = S / 2.0; R = 214
    d = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">' % (S, S, S, S)]
    d.append('<g fill="none" stroke="#12052A" stroke-linecap="round"><circle cx="%.1f" cy="%.1f" r="%d" stroke-width="7"/><circle cx="%.1f" cy="%.1f" r="%d" stroke-width="4"/>' % (cx, cy, R, cx, cy, R - 26))
    for i in range(16):
        a = i * math.pi * 2 / 16
        d.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke-width="3.2"/>' % (cx, cy, cx + math.cos(a) * R, cy + math.sin(a) * R))
    for i in range(8):
        a = i * math.pi * 2 / 8
        d.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke-width="2"/>' % (cx + math.cos(a) * (R - 26), cy + math.sin(a) * (R - 26), cx + math.cos(a + 0.4) * (R - 90), cy + math.sin(a + 0.4) * (R - 90)))
    d.append('</g><circle cx="%.1f" cy="%.1f" r="16" fill="#12052A"/>' % (cx, cy))
    cols = ["#FF4FA3", "#33E6D4", "#FFC27A", "#FF6EB3"]
    for i in range(32):                                             # rim lights
        a = i * math.pi * 2 / 32
        d.append('<circle cx="%.1f" cy="%.1f" r="4.2" fill="%s"/>' % (cx + math.cos(a) * R, cy + math.sin(a) * R, cols[i % 4]))
    for i in range(16):                                             # gondolas
        a = i * math.pi * 2 / 16 + 0.1
        gx, gy = cx + math.cos(a) * R, cy + math.sin(a) * R
        d.append('<rect x="%.1f" y="%.1f" width="26" height="20" rx="5" fill="#12052A"/><rect x="%.1f" y="%.1f" width="18" height="9" rx="3" fill="%s" fill-opacity="0.95"/>' % (gx - 13, gy - 2, gx - 9, gy + 2, cols[i % 4]))
    d.append("</svg>")
    PNG("".join(d), "wheel.png", 512)

if __name__ == "__main__":
    build()
