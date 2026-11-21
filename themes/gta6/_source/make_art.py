#!/usr/bin/env python3
"""Draws the pictures of the GTA VI theme pack (the striped sun and the palm trees) into ../art.

You do not need this to use the theme: the PNG files in ../art are already made. It is here so you can change the shapes
or colours and make them again:    python3 make_art.py        (needs Pillow:  pip install pillow)

Everything is drawn four times larger than needed and shrunk, which gives smooth edges.
"""
import math, os
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "art")
os.makedirs(OUT, exist_ok=True)
SS = 4                                   # supersampling

def hashf(i, salt):                      # same little hash the program uses: the same (i, salt) always gives the same 0..1
    x = (i * 374761393 + salt * 668265263) & 0xFFFFFFFF
    x = ((x ^ (x >> 13)) * 1274126177) & 0xFFFFFFFF
    x ^= x >> 16
    return (x & 0xFFFF) / 65535.0

def lerp(a, b, t): return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))

# ---------------------------------------------------------------- the sun
def make_sun(size=1024, pad=0.03):
    big = size * SS
    img = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    cx = cy = big / 2.0
    r = big * (0.5 - pad)
    top, bot = (0xFF, 0xD3, 0x6B), (0xFF, 0x3E, 0x91)            # yellow at the top, hot pink at the bottom
    # a vertical gradient clipped to a circle
    grad = Image.new("RGBA", (big, big))
    gp = grad.load()
    for y in range(big):
        t = min(1.0, max(0.0, (y - (cy - r)) / (2 * r)))
        c = lerp(top, bot, t)
        for x in range(0, big):
            gp[x, y] = c + (255,)
    mask = Image.new("L", (big, big), 0)
    ImageDraw.Draw(mask).ellipse([cx - r, cy - r, cx + r, cy + r], fill=255)
    # the stripes: gaps in the upper half that get wider toward the horizon (fractions of the radius above the centre)
    at = [0.66, 0.53, 0.41, 0.29, 0.18, 0.075]
    th = [0.018, 0.026, 0.036, 0.048, 0.062, 0.078]
    md = ImageDraw.Draw(mask)
    for a, t in zip(at, th):
        y0 = cy - r * a
        md.rectangle([0, y0, big, y0 + r * t], fill=0)
    img.paste(grad, (0, 0), mask)
    return img.resize((size, size), Image.LANCZOS)

# ---------------------------------------------------------------- palm trees
def make_palm(h=700, lean=0.16, seed=1, color=(0x12, 0x05, 0x1F)):
    sw, sh = int(h * 1.50), int(h * 1.18)                          # the trunk's foot is at the middle of the bottom edge
    W, H = sw * SS, sh * SS
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    col = color + (255,)
    S = float(SS)
    hh = h * S
    bx = W / 2.0
    c0 = (bx, H)
    c1 = (bx + lean * hh * 0.15, H - hh * 0.5)
    c2 = (bx + lean * hh, H - hh * 0.9)
    L, R = [], []
    for i in range(17):                                            # the trunk: a curve that thins toward the top, slightly ringed
        t = i / 16.0; u = 1 - t
        px = u * u * c0[0] + 2 * u * t * c1[0] + t * t * c2[0]
        py = u * u * c0[1] + 2 * u * t * c1[1] + t * t * c2[1]
        dx = 2 * u * (c1[0] - c0[0]) + 2 * t * (c2[0] - c1[0])
        dy = 2 * u * (c1[1] - c0[1]) + 2 * t * (c2[1] - c1[1])
        ln = max(1e-3, math.hypot(dx, dy)); nx, ny = -dy / ln, dx / ln
        w = hh * (0.030 - 0.015 * t) * (1 + 0.10 * math.sin(t * 18 + seed))
        L.append((px + nx * w, py + ny * w)); R.append((px - nx * w, py - ny * w))
    d.polygon(L + R[::-1], fill=col)
    crown = c2
    # The fronds. Each one is a curved rib that leaves the crown going up and out, then droops under its own weight, with thin
    # leaflets along both sides (that is what makes it look like a palm and not like a fan). phi: launch angle above horizontal.
    fronds = []                                                    # (side, phi, length)
    for side in (1, -1):
        for j, phi in enumerate([10, 38, 66]):
            jitter = hashf(seed * 31 + j * 7 + (0 if side > 0 else 3), 9)
            fronds.append((side, phi + (jitter - 0.5) * 12.0, hh * (0.50 - 0.04 * j + 0.06 * jitter)))
    fronds.append((1, 84, hh * 0.24)); fronds.append((-1, 100, hh * 0.20))         # two short young leaves standing up in the middle
    lw = max(2, int(hh * 0.0058))
    for side, phi, Lf in fronds:
        ph = math.radians(phi)
        g = (1.25 if phi < 30 else 1.05 if phi < 80 else 0.45)
        def rib(t):
            return (crown[0] + side * math.cos(ph) * Lf * t, crown[1] - math.sin(ph) * Lf * t + g * Lf * t * t)
        pts = [rib(i / 24.0) for i in range(25)]
        d.line(pts, fill=col, width=lw * 2, joint="curve")
        n = 30
        for i in range(2, n):
            t = i / float(n)
            x, y = rib(t)
            x2, y2 = rib(min(1.0, t + 0.02))
            tx, ty = x2 - x, y2 - y
            ln = max(1e-3, math.hypot(tx, ty)); tx, ty = tx / ln, ty / ln
            nx, ny = -ty, tx
            env = math.sin(math.pi * min(1.0, 0.08 + t * 0.90)) ** 0.75
            ll = Lf * 0.17 * env * (1.0 - 0.35 * t) * (0.85 + 0.3 * hashf(i * 11 + int(phi) + (7 if side > 0 else 0), 4))
            for sgn in (1, -1):
                ang = math.radians(44)
                vx = tx * math.cos(ang) + sgn * nx * math.sin(ang)
                vy = ty * math.cos(ang) + sgn * ny * math.sin(ang)
                vy += 0.42                                      # leaflets hang a little
                vl = max(1e-3, math.hypot(vx, vy)); vx, vy = vx / vl, vy / vl
                d.line([(x, y), (x + vx * ll, y + vy * ll)], fill=col, width=lw)
    for k in range(3):                                             # coconuts
        x = crown[0] + (k - 1) * hh * 0.020
        y = crown[1] + hh * 0.024 + (hh * 0.014 if k == 1 else 0)
        rr = hh * 0.018
        d.ellipse([x - rr, y - rr, x + rr, y + rr], fill=col)
    return img.resize((sw, sh), Image.LANCZOS)

# ---------------------------------------------------------------- the banner plane
def smooth(pts, n=14):
    """Catmull-Rom curve through the points (a smooth outline from a few corners)."""
    out = []
    P = [pts[0]] + list(pts) + [pts[-1]]
    for i in range(1, len(P) - 2):
        p0, p1, p2, p3 = P[i - 1], P[i], P[i + 1], P[i + 2]
        for k in range(n):
            t = k / n
            out.append(tuple(0.5 * ((2 * p1[j]) + (-p0[j] + p2[j]) * t + (2 * p0[j] - 5 * p1[j] + 4 * p2[j] - p3[j]) * t * t
                                    + (-p0[j] + 3 * p1[j] - 3 * p2[j] + p3[j]) * t * t * t) for j in range(2)))
    out.append(pts[-1])
    return out

def make_plane(w=720, h=240):
    """A small high-wing propeller plane, side view, nose pointing RIGHT, tail tip at the left edge, half way down.
    Meridian ties the banner's tow line to the left edge, so keep the tail there.  The plane is cream with a hot-pink stripe and
    a teal window so it reads against the dark purple sky and the sea."""
    W, H = w * SS, h * SS
    S = lambda pts: [(x * SS, y * SS) for x, y in pts]
    CREAM, CREAM2, PINK, TEAL, DARK = (255, 241, 232), (255, 196, 220), (255, 62, 145), (53, 230, 212), (42, 15, 77)
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    # ---- fuselage (a mask first, so the stripe and the shading can be clipped to it)
    top = smooth([(4, 119), (70, 111), (170, 100), (270, 88), (370, 81), (460, 79), (545, 81), (600, 88), (640, 100), (667, 114), (676, 125)])
    bot = smooth([(676, 125), (668, 141), (640, 158), (580, 170), (480, 175), (380, 171), (280, 159), (180, 142), (90, 129), (4, 119)])
    body = S(top + bot)
    mask = Image.new("L", (W, H), 0); ImageDraw.Draw(mask).polygon(body, fill=255)
    shade = Image.new("RGBA", (W, H))
    sp = shade.load()
    for y in range(H):                                                   # cream on top, a little pink underneath
        t = min(1.0, max(0.0, (y / SS - 78) / 100.0))
        c = lerp(CREAM, CREAM2, t ** 1.4) + (255,)
        for x in range(W): sp[x, y] = c
    layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ld = ImageDraw.Draw(layer)
    ld.polygon(S([(120, 133), (600, 133), (610, 144), (130, 142)]), fill=PINK + (255,))          # the stripe
    ld.polygon(S([(120, 146), (590, 146), (596, 150), (126, 150)]), fill=TEAL + (255,))          # a thin teal one under it
    shade.alpha_composite(layer)
    img.paste(shade, (0, 0), mask)
    d = ImageDraw.Draw(img)
    # ---- tail
    d.polygon(S([(34, 113), (14, 36), (46, 34), (126, 100)]), fill=CREAM + (255,))                 # fin
    d.polygon(S([(24, 80), (16, 36), (46, 34), (60, 62)]), fill=PINK + (255,))                    # pink top of the fin
    d.polygon(S(smooth([(10, 117), (40, 106), (120, 104), (150, 110), (120, 118), (40, 122), (10, 117)])), fill=CREAM2 + (255,))   # tailplane
    # ---- windows
    d.polygon(S([(468, 92), (566, 92), (606, 105), (598, 124), (468, 124)]), fill=TEAL + (255,))
    d.line(S([(522, 92), (522, 124)]), fill=DARK + (255,), width=4 * SS // 4 * 1)
    d.line(S([(468, 92), (566, 92), (606, 105), (598, 124), (468, 124), (468, 92)]), fill=DARK + (200,), width=SS * 2)
    # ---- high wing, sitting on the cabin roof with a strut down to the belly
    d.line(S([(330, 152), (384, 84)]), fill=DARK + (255,), width=SS * 6)
    d.polygon(S(smooth([(268, 72), (340, 61), (470, 57), (545, 63), (554, 72), (500, 82), (340, 84), (268, 72)])), fill=CREAM + (255,))
    d.line(S([(276, 73), (400, 66), (544, 68)]), fill=PINK + (255,), width=SS * 4)
    # ---- landing gear
    d.line(S([(448, 160), (470, 190)]), fill=DARK + (255,), width=SS * 7)
    d.line(S([(560, 164), (536, 190)]), fill=DARK + (255,), width=SS * 7)
    for cx in (470, 536):
        d.ellipse([(cx - 17) * SS, (190 - 17) * SS, (cx + 17) * SS, (190 + 17) * SS], fill=DARK + (255,))
        d.ellipse([(cx - 7) * SS, (190 - 7) * SS, (cx + 7) * SS, (190 + 7) * SS], fill=CREAM2 + (255,))
    d.ellipse([(52 - 8) * SS, (136 - 8) * SS, (52 + 8) * SS, (136 + 8) * SS], fill=DARK + (255,))
    # ---- propeller: a pale blur and a pink spinner
    blur = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(blur).ellipse([660 * SS, 62 * SS, 684 * SS, 188 * SS], fill=(255, 243, 230, 90))
    img.alpha_composite(blur)
    d = ImageDraw.Draw(img)
    d.ellipse([662 * SS, 112 * SS, 686 * SS, 138 * SS], fill=PINK + (255,))
    return img.resize((w, h), Image.LANCZOS)

if __name__ == "__main__":
    make_sun().save(os.path.join(OUT, "sun.png"), optimize=True)
    # (lean, seed): the first two lean right (for the left side of the screen), the last two lean left
    for name, lean, seed in [("palm_a", 0.16, 1), ("palm_b", 0.10, 2), ("palm_c", -0.14, 3), ("palm_d", -0.08, 4)]:
        make_palm(700, lean, seed).save(os.path.join(OUT, name + ".png"), optimize=True)
    print("art written to", os.path.abspath(OUT))
    make_plane().save(os.path.join(OUT, "plane.png"), optimize=True)
