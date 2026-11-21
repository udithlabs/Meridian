#!/usr/bin/env python3
"""Draws the pictures of the Halloween theme pack (moon, bats, ghosts, pumpkins, spider, witch, haunted house, trees, graves,
cat, zombie hands, eyes, lightning bolt, clouds, hills, cobweb) into ../art.

You do not need this to use the theme: the PNG files in ../art are already made. It is here so you can change a shape or a
colour and make them again:     python3 make_art.py          (needs Pillow and numpy:  pip install pillow numpy)
Draw only one thing:            python3 make_art.py witch bat

Everything is drawn four times larger than needed and shrunk, which gives smooth edges.
All of it is original artwork made for this theme.
"""
import math, os, random, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "art")
os.makedirs(OUT, exist_ok=True)
SS = 4                                            # supersampling

NIGHT = (14, 4, 26)                               # the colour of all the dark silhouettes
NIGHT2 = (26, 9, 44)                              # a little lighter, for the rim of a silhouette


# ------------------------------------------------------------------------------------------------ helpers
def canvas(w, h):
    img = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
    return img, ImageDraw.Draw(img)

def fin(img, w, h):
    return img.resize((w, h), Image.LANCZOS)

def P(pts):
    return [(x * SS, y * SS) for x, y in pts]

def save(img, name, w, h):
    fin(img, w, h).save(os.path.join(OUT, name), optimize=True)
    print("  ", name, w, "x", h)

def smooth(pts, n=12, closed=False):
    """Catmull-Rom curve through the points (a smooth outline from a few corners)."""
    out = []
    if closed:
        Q = [pts[-1]] + list(pts) + [pts[0], pts[1]]
        rng = range(1, len(pts) + 1)
    else:
        Q = [pts[0]] + list(pts) + [pts[-1]]
        rng = range(1, len(Q) - 2)
    for i in rng:
        p0, p1, p2, p3 = Q[i - 1], Q[i], Q[i + 1], Q[i + 2]
        for k in range(n):
            t = k / n
            out.append(tuple(0.5 * ((2 * p1[j]) + (-p0[j] + p2[j]) * t + (2 * p0[j] - 5 * p1[j] + 4 * p2[j] - p3[j]) * t * t
                                    + (-p0[j] + 3 * p1[j] - 3 * p2[j] + p3[j]) * t * t * t) for j in range(2)))
    if not closed:
        out.append(pts[-1])
    return out

def mirror(pts, cx):
    return [(2 * cx - x, y) for x, y in pts]

def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(len(a)))

def vgrad(w, h, top, bottom, y0=0.0, y1=1.0):
    """w x h (already supersampled) vertical gradient as an RGBA image."""
    ys = np.clip((np.arange(h) / max(1, h - 1) - y0) / max(1e-6, y1 - y0), 0, 1)[:, None, None]
    top = tuple(top) + (255,) * (4 - len(top)); bottom = tuple(bottom) + (255,) * (4 - len(bottom))
    a = np.array(top, float)[None, None, :]
    b = np.array(bottom, float)[None, None, :]
    arr = a + (b - a) * ys
    arr = np.repeat(arr, w, axis=1)
    return Image.fromarray(arr.astype(np.uint8), "RGBA")

def fill_clipped(img, mask, top, bottom, y0=0.0, y1=1.0):
    """paints a vertical gradient into the picture, only where the mask (mode L, same size) is white."""
    g = vgrad(img.width, img.height, top, bottom, y0, y1)
    img.paste(g, (0, 0), mask)

def soft_glow(mask, radius, color, strength=1.0):
    """a blurred, coloured halo made from a mask (mode L)."""
    b = mask.filter(ImageFilter.GaussianBlur(radius * SS))
    b = b.point(lambda v: min(255, int(v * strength)))
    g = Image.new("RGBA", mask.size, color + (0,))
    g.putalpha(b)
    return g

def thick_line(d, pts, width, fill, joint=True):
    d.line(P(pts), fill=fill, width=int(width * SS), joint="curve" if joint else None)
    r = width * SS / 2.0                             # round ends
    for x, y in (P(pts)[0], P(pts)[-1]):
        d.ellipse([x - r, y - r, x + r, y + r], fill=fill)

def hashf(i, salt):
    x = (i * 374761393 + salt * 668265263) & 0xFFFFFFFF
    x = ((x ^ (x >> 13)) * 1274126177) & 0xFFFFFFFF
    x ^= x >> 16
    return (x & 0xFFFF) / 65535.0


# ------------------------------------------------------------------------------------------------ the moon
def make_moon(size=1024):
    """A huge harvest moon: orange, lit from the top left, with craters."""
    big = size * SS
    yy, xx = np.mgrid[0:big, 0:big].astype(np.float32)
    cx = cy = big / 2.0
    r = big * 0.485
    dx, dy = (xx - cx) / r, (yy - cy) / r
    d = np.sqrt(dx * dx + dy * dy)
    inside = np.clip((1.0 - d) * r / (1.6 * SS), 0, 1)             # anti-aliased edge
    top, bot = np.array([255, 210, 100], np.float32), np.array([255, 98, 18], np.float32)
    t = np.clip((dy + 1) / 2, 0, 1)[..., None]
    col = top + (bot - top) * t
    light = np.clip(1.0 - 0.30 * ((dx + 0.35) ** 2 + (dy + 0.35) ** 2) ** 0.5 / 1.4, 0.7, 1.0)       # brighter at the top left
    col = col * light[..., None]
    limb = np.clip(1.0 - 0.16 * d ** 3, 0, 1)[..., None]                                                # darker toward the edge
    col = np.clip(col * limb * 1.18, 0, 255)
    # craters and seas: soft dark patches, each with a bright rim on the lit side
    rnd = random.Random(7)
    spots = [(-0.36, -0.30, 0.20), (0.30, -0.42, 0.13), (0.02, 0.05, 0.27), (0.45, 0.22, 0.17), (-0.46, 0.34, 0.15), (-0.08, 0.58, 0.12),
             (0.62, -0.12, 0.09), (-0.20, -0.62, 0.08), (0.28, 0.58, 0.10), (-0.64, 0.02, 0.08)]
    for sx, sy, sr in spots:
        dd = np.sqrt((dx - sx) ** 2 + (dy - sy) ** 2)
        m = np.clip(1.0 - dd / sr, 0, 1) ** 0.7
        col = col * (1.0 - 0.20 * m[..., None])
        rim = np.exp(-((dd - sr) / (0.018)) ** 2) * 0.35
        rim = rim * np.clip(((dx - sx) * -0.7 + (dy - sy) * -0.7) / sr + 0.2, 0, 1)                     # only on the side facing the light
        col = col + rim[..., None] * np.array([255, 220, 150], np.float32) * 0.45
    arr = np.dstack([np.clip(col, 0, 255), inside * 255]).astype(np.uint8)
    return Image.fromarray(arr, "RGBA").resize((size, size), Image.LANCZOS)


# ------------------------------------------------------------------------------------------------ a bat (wings spread up: squash it to flap)
def make_bat(w=512, h=256):
    img, d = canvas(w, h)
    col = NIGHT + (255,)
    cx = w / 2
    wing = [(246, 134), (214, 92), (176, 66), (120, 44), (62, 30), (14, 30),           # the top edge, out to the tip
            (46, 62), (60, 100), (72, 118),                                              # the first scallop
            (98, 100), (118, 134),                                                        # the second
            (150, 112), (170, 150),                                                       # the third
            (206, 128), (226, 168), (246, 172)]                                           # back in to the body
    L = smooth(wing, 8)
    d.polygon(P(L), fill=col)
    d.polygon(P(mirror(L, cx)), fill=col)
    d.ellipse(P([(cx - 20, 112), (cx + 20, 112)]) [0] + P([(cx + 20, 190)])[0], fill=col)  # body
    d.ellipse(P([(cx - 17, 92)])[0] + P([(cx + 17, 126)])[0], fill=col)                    # head
    d.polygon(P([(cx - 15, 100), (cx - 21, 70), (cx - 5, 92)]), fill=col)                  # ears
    d.polygon(P([(cx + 15, 100), (cx + 21, 70), (cx + 5, 92)]), fill=col)
    d.ellipse(P([(cx - 9, 104)])[0] + P([(cx - 4, 109)])[0], fill=(255, 70, 70, 255))      # tiny red eyes
    d.ellipse(P([(cx + 4, 104)])[0] + P([(cx + 9, 109)])[0], fill=(255, 70, 70, 255))
    save(img, "bat.png", w, h)


# ------------------------------------------------------------------------------------------------ ghosts
def make_ghost(name, mood, w=500, h=600):
    img, d = canvas(w, h)
    cx = w / 2
    # the sheet: a dome on top, long sides, a wavy hem. Built as a mask so the gradient and the glow can be clipped to it.
    body = Image.new("L", img.size, 0)
    bd = ImageDraw.Draw(body)
    bd.ellipse(P([(cx - 140, 40)])[0] + P([(cx + 140, 320)])[0], fill=255)
    bd.rectangle(P([(cx - 140, 180)])[0] + P([(cx + 140, 470)])[0], fill=255)
    hem = [(cx - 140, 470)]
    teeth = 5
    for i in range(teeth):                                   # the wavy bottom: bumps down, dips up
        x0 = cx - 140 + i * 280 / teeth
        x1 = cx - 140 + (i + 1) * 280 / teeth
        hem += [((x0 + x1) / 2, 540 - (i % 2) * 18), (x1, 470 + 14 * ((i + 1) % 2))]
    bd.polygon(P(hem + [(cx + 140, 450), (cx - 140, 450)]), fill=255)
    # arms: two soft stubs out to the sides
    if mood == "scared":
        bd.polygon(P(smooth([(cx - 120, 250), (cx - 190, 150), (cx - 205, 118), (cx - 172, 128), (cx - 150, 172), (cx - 110, 300)], 8, False)), fill=255)
        bd.polygon(P(smooth([(cx + 120, 250), (cx + 190, 150), (cx + 205, 118), (cx + 172, 128), (cx + 150, 172), (cx + 110, 300)], 8, False)), fill=255)
    else:
        bd.polygon(P(smooth([(cx - 125, 270), (cx - 190, 300), (cx - 202, 330), (cx - 170, 340), (cx - 125, 330)], 8, False)), fill=255)
        bd.polygon(P(smooth([(cx + 125, 250), (cx + 195, 200), (cx + 212, 176), (cx + 182, 170), (cx + 122, 215)], 8, False)), fill=255)
    body = body.filter(ImageFilter.GaussianBlur(0.8 * SS))
    img.alpha_composite(soft_glow(body, 18, (150, 255, 220), 0.75))                  # cold green halo around the sheet
    sheet = Image.new("RGBA", img.size, (0, 0, 0, 0))
    fill_clipped(sheet, body, (246, 255, 255, 236), (150, 200, 235, 140), 0.12, 1.0)
    img.alpha_composite(sheet)
    d = ImageDraw.Draw(img)
    ink = (22, 10, 40, 255)
    if mood == "scared":
        for sx in (-1, 1):
            d.ellipse(P([(cx + sx * 52 - 28, 150)])[0] + P([(cx + sx * 52 + 28, 232)])[0], fill=ink)         # tall round eyes
            d.ellipse(P([(cx + sx * 52 - 9, 164)])[0] + P([(cx + sx * 52 + 3, 178)])[0], fill=(255, 255, 255, 230))
        d.ellipse(P([(cx - 36, 262)])[0] + P([(cx + 36, 372)])[0], fill=ink)                                    # a big "oh" mouth
    elif mood == "evil":
        for sx in (-1, 1):
            e = [(cx + sx * 26, 190), (cx + sx * 86, 160 + 0), (cx + sx * 82, 205), (cx + sx * 50, 222)]
            d.polygon(P(smooth(e, 6, True)), fill=ink)
            d.ellipse(P([(cx + sx * 60 - 7, 180)])[0] + P([(cx + sx * 60 + 7, 196)])[0], fill=(120, 255, 90, 255))  # green pupils
        grin = smooth([(cx - 78, 268), (cx - 40, 322), (cx, 336), (cx + 40, 322), (cx + 78, 268), (cx + 40, 300), (cx, 308), (cx - 40, 300)], 8, True)
        d.polygon(P(grin), fill=ink)
        for tx in (-44, -18, 18, 44):                                                                             # teeth
            d.polygon(P([(cx + tx - 7, 296 + abs(tx) * 0.02), (cx + tx + 7, 296), (cx + tx, 316 - abs(tx) * 0.15)]), fill=(246, 255, 255, 255))
    else:  # "happy"
        for sx in (-1, 1):
            d.ellipse(P([(cx + sx * 50 - 20, 170)])[0] + P([(cx + sx * 50 + 20, 226)])[0], fill=ink)
            d.ellipse(P([(cx + sx * 50 - 8, 180)])[0] + P([(cx + sx * 50 + 2, 192)])[0], fill=(255, 255, 255, 235))
            d.ellipse(P([(cx + sx * 88 - 20, 236)])[0] + P([(cx + sx * 88 + 20, 262)])[0], fill=(255, 150, 190, 120))  # cheeks
        d.arc(P([(cx - 46, 240)])[0] + P([(cx + 46, 330)])[0], 20, 160, fill=ink, width=int(9 * SS))
    save(img, name, w, h)


# ------------------------------------------------------------------------------------------------ pumpkins (with a glowing face)
def make_pumpkin(name, face, w=440, h=400, body=((255, 150, 40), (214, 76, 8))):
    img, d = canvas(w, h)
    cx = w / 2
    mask = Image.new("L", img.size, 0)
    md = ImageDraw.Draw(mask)
    lobes = [(-150, 100), (-85, 128), (0, 140), (85, 128), (150, 100)]           # five bulging ribs, the middle one in front
    top, bot = 100, 360
    for dx, rx in sorted(lobes, key=lambda p: -abs(p[0])):
        md.ellipse(P([(cx + dx - rx * 0.62, top + 14 + abs(dx) * 0.12)])[0] + P([(cx + dx + rx * 0.62, bot - abs(dx) * 0.08)])[0], fill=255)
    mask = mask.filter(ImageFilter.GaussianBlur(0.6 * SS))
    fill_clipped(img, mask, body[0], body[1], 0.22, 1.0)
    # rib shading: darker lines between the lobes and a lighter stripe on every lobe
    d = ImageDraw.Draw(img)
    shade = Image.new("RGBA", img.size, (0, 0, 0, 0))
    sd = ImageDraw.Draw(shade)
    for dx, rx in lobes:
        sd.arc(P([(cx + dx - rx * 0.62, top + 14 + abs(dx) * 0.12)])[0] + P([(cx + dx + rx * 0.62, bot - abs(dx) * 0.08)])[0], 0, 360, fill=(120, 36, 0, 150), width=int(5 * SS))
        sd.ellipse(P([(cx + dx - rx * 0.20, top + 60)])[0] + P([(cx + dx + rx * 0.05, top + 110)])[0], fill=(255, 220, 130, 60))
    shade = shade.filter(ImageFilter.GaussianBlur(1.2 * SS))
    shade.putalpha(Image.composite(shade.getchannel("A"), Image.new("L", img.size, 0), mask))
    img.alpha_composite(shade)
    # stem: a curled brown-green stub
    stem = smooth([(cx - 22, 118), (cx - 14, 76), (cx + 6, 50), (cx + 34, 40), (cx + 38, 54), (cx + 20, 64), (cx + 26, 118)], 8, True)
    d.polygon(P(stem), fill=(70, 62, 22, 255))
    d.polygon(P(smooth([(cx - 6, 112), (cx + 2, 80), (cx + 14, 62)], 8)) + P(smooth([(cx + 14, 72), (cx + 8, 92), (cx + 4, 112)], 4)), fill=(106, 98, 36, 255))
    # the face: bright shapes, light that spills into the flesh around them
    fm = Image.new("L", img.size, 0)
    fd = ImageDraw.Draw(fm)
    if face == "grin":
        fd.polygon(P([(cx - 88, 196), (cx - 40, 178), (cx - 52, 236)]), fill=255)                                # triangle eyes
        fd.polygon(P([(cx + 88, 196), (cx + 40, 178), (cx + 52, 236)]), fill=255)
        fd.polygon(P([(cx - 8, 238), (cx + 8, 238), (cx, 256)]), fill=255)                                       # nose
        mouth = smooth([(cx - 118, 262), (cx - 62, 306), (cx, 322), (cx + 62, 306), (cx + 118, 262), (cx + 80, 272), (cx, 282), (cx - 80, 272)], 8, True)
        fd.polygon(P(mouth), fill=255)
        for tx, up in ((-44, True), (22, False), (68, True)):                                                    # a few teeth (dark = flesh) in the grin
            pass
        teeth = Image.new("L", img.size, 0)
        td = ImageDraw.Draw(teeth)
        td.polygon(P([(cx - 52, 268), (cx - 30, 270), (cx - 40, 296)]), fill=255)
        td.polygon(P([(cx + 30, 270), (cx + 52, 268), (cx + 42, 296)]), fill=255)
        td.polygon(P([(cx - 12, 276), (cx + 12, 276), (cx, 306)]), fill=255)
        fm = Image.composite(Image.new("L", img.size, 0), fm, teeth)
    elif face == "happy":
        fd.ellipse(P([(cx - 84, 186)])[0] + P([(cx - 36, 238)])[0], fill=255)                                    # round eyes
        fd.ellipse(P([(cx + 36, 186)])[0] + P([(cx + 84, 238)])[0], fill=255)
        fd.polygon(P(smooth([(cx - 96, 268), (cx - 50, 312), (cx, 324), (cx + 50, 312), (cx + 96, 268), (cx + 48, 296), (cx, 302), (cx - 48, 296)], 8, True)), fill=255)
        teeth = Image.new("L", img.size, 0)
        ImageDraw.Draw(teeth).rectangle(P([(cx - 18, 290)])[0] + P([(cx + 4, 312)])[0], fill=255)
        fm = Image.composite(Image.new("L", img.size, 0), fm, teeth)
    else:  # "evil": slanted eyes and a long zigzag mouth
        fd.polygon(P([(cx - 100, 178), (cx - 30, 206), (cx - 56, 240), (cx - 96, 214)]), fill=255)
        fd.polygon(P([(cx + 100, 178), (cx + 30, 206), (cx + 56, 240), (cx + 96, 214)]), fill=255)
        zig = [(cx - 112, 276), (cx - 84, 296), (cx - 58, 272), (cx - 30, 300), (cx - 2, 274), (cx + 26, 302), (cx + 54, 274), (cx + 82, 298), (cx + 112, 272),
               (cx + 112, 292), (cx + 82, 322), (cx + 54, 298), (cx + 26, 326), (cx - 2, 298), (cx - 30, 324), (cx - 58, 298), (cx - 84, 322), (cx - 112, 296)]
        fd.polygon(P(zig), fill=255)
    fm = Image.composite(fm, Image.new("L", img.size, 0), mask)
    img.alpha_composite(soft_glow(fm, 16, (255, 190, 50), 0.9))
    glowy = Image.new("RGBA", img.size, (0, 0, 0, 0))
    fill_clipped(glowy, fm, (255, 240, 130, 255), (255, 154, 28, 255), 0.4, 0.95)
    img.alpha_composite(glowy)
    save(img, name, w, h)


# ------------------------------------------------------------------------------------------------ a spider (hangs from a thread)
def make_spider(w=300, h=372):
    img, d = canvas(w, h)
    cx = w / 2
    col = (10, 4, 18, 255)
    legs = []
    # four legs on each side: three joints, bending up and then down
    for side in (-1, 1):
        for i, (ly, kx, ky, ex, ey) in enumerate([(150, 70, 84, 126, 170), (166, 92, 124, 134, 238), (182, 96, 178, 118, 296), (196, 76, 228, 82, 322)][:4]):
            legs.append([(cx + side * 12, ly + 28 - i * 2), (cx + side * kx * 0.9, ky + 40), (cx + side * (kx + 38), ky + 28 + i * 8), (cx + side * ex, ey + 8)])
    for lg in legs:
        thick_line(d, smooth(lg, 10), 7.5, col)
    d.line(P([(cx, 0), (cx, 118)]), fill=(210, 205, 230, 255), width=int(2 * SS))                           # the thread, down into its head
    d.ellipse(P([(cx - 58, 160)])[0] + P([(cx + 58, 296)])[0], fill=col)                                         # abdomen
    d.ellipse(P([(cx - 36, 112)])[0] + P([(cx + 36, 176)])[0], fill=col)                                         # head
    # red hourglass on the back
    d.polygon(P([(cx - 14, 196), (cx + 14, 196), (cx + 3, 226), (cx + 14, 256), (cx - 14, 256), (cx - 3, 226)]), fill=(224, 24, 44, 255))
    for ex in (-13, 13):                                                                                            # eyes
        d.ellipse(P([(cx + ex - 7, 134)])[0] + P([(cx + ex + 7, 148)])[0], fill=(255, 250, 240, 255))
        d.ellipse(P([(cx + ex - 3, 138)])[0] + P([(cx + ex + 3, 146)])[0], fill=(210, 20, 40, 255))
    save(img, "spider.png", w, h)


# ------------------------------------------------------------------------------------------------ the witch (pulls the banner)
def make_witch(w=720, h=240):
    """A witch and her cat on a broom, side view, flying to the RIGHT. The broom's bristles reach the left edge, half way down:
    Meridian ties the banner's tow line there, so keep it that way."""
    img, d = canvas(w, h)
    col = NIGHT + (255,)
    # the broom: a long handle rising a little toward the nose, and a fan of straw at the tail
    thick_line(d, [(128, 128), (380, 118), (612, 106)], 8.5, col)
    straw = smooth([(150, 122), (112, 98), (62, 90), (14, 102), (2, 120), (14, 140), (62, 152), (112, 144), (150, 134)], 8, True)
    d.polygon(P(straw), fill=col)
    for i in range(7):                                                  # a few lighter straws, so it reads as a broom
        yy = 100 + i * 6.6
        d.line(P([(150, 124 + (i - 3) * 2), (30, yy + 2)]), fill=(52, 22, 78, 255), width=int(1.6 * SS))
    d.rectangle(P([(138, 112)])[0] + P([(152, 144)])[0], fill=(120, 64, 22, 255))      # the binding
    # the cat, sitting behind her
    d.ellipse(P([(276, 82)])[0] + P([(320, 124)])[0], fill=col)
    d.ellipse(P([(298, 60)])[0] + P([(326, 88)])[0], fill=col)
    d.polygon(P([(302, 66), (304, 46), (314, 62)]), fill=col)
    d.polygon(P([(314, 62), (322, 46), (326, 66)]), fill=col)
    thick_line(d, smooth([(280, 112), (256, 108), (244, 86), (254, 66)], 8), 7, col)
    d.ellipse(P([(311, 70)])[0] + P([(316, 75)])[0], fill=(255, 220, 60, 255))
    d.ellipse(P([(318, 69)])[0] + P([(323, 74)])[0], fill=(255, 220, 60, 255))
    # the witch: cloak streaming back, legs hanging, one arm out to the handle, a long nose, a bent hat
    cloak = smooth([(430, 78), (410, 76), (380, 84), (340, 96), (292, 112), (236, 120), (206, 136), (250, 134), (214, 156), (280, 142), (330, 134),
                    (380, 128), (420, 124), (438, 106)], 8, True)
    d.polygon(P(cloak), fill=col)
    thick_line(d, smooth([(404, 118), (410, 150), (432, 168), (452, 176)], 8), 13, col)     # front leg and pointed shoe
    d.polygon(P([(446, 166), (476, 178), (446, 186)]), fill=col)
    thick_line(d, smooth([(386, 120), (384, 156), (396, 182)], 8), 13, col)                 # back leg
    d.polygon(P([(386, 176), (410, 190), (384, 194)]), fill=col)
    thick_line(d, smooth([(426, 88), (452, 100), (478, 112)], 6), 11, col)                   # the arm on the handle
    d.ellipse(P([(468, 104)])[0] + P([(488, 120)])[0], fill=col)
    d.ellipse(P([(422, 62)])[0] + P([(452, 92)])[0], fill=col)                               # head
    d.polygon(P([(446, 70), (472, 80), (446, 82)]), fill=col)                                # the nose
    d.polygon(P([(448, 84), (458, 96), (440, 90)]), fill=col)                                # chin
    d.polygon(P(smooth([(426, 66), (390, 74), (350, 70), (330, 62), (352, 84), (392, 92), (426, 88)], 8, True)), fill=col)   # hair in the wind
    d.ellipse(P([(386, 46)])[0] + P([(470, 62)])[0], fill=col)                               # hat brim
    hat = smooth([(404, 54), (456, 54), (446, 30), (430, 12), (402, 4), (376, 12), (358, 26), (376, 30), (404, 26), (414, 38)], 8, True)
    d.polygon(P(hat), fill=col)
    d.polygon(P([(404, 52), (458, 52), (454, 44), (406, 44)]), fill=(224, 110, 20, 255))     # the hat band
    d.rectangle(P([(424, 44)])[0] + P([(434, 53)])[0], fill=(255, 200, 70, 255))             # the buckle
    save(img, "witch.png", w, h)


# ------------------------------------------------------------------------------------------------ the haunted house (with lit windows)
HOUSE_WINDOWS = []        # filled in by make_house: (name, centre x, centre y) as fractions of the picture, to put flickers on them

def make_house(w=1000, h=900):
    global HOUSE_WINDOWS
    img, d = canvas(w, h)
    col = NIGHT + (255,)
    # the hill it stands on
    d.polygon(P(smooth([(0, 900), (0, 846), (140, 818), (320, 790), (500, 776), (700, 790), (860, 812), (1000, 842), (1000, 900)], 10)), fill=col)
    # left wing, main block, tower
    d.polygon(P([(96, 800), (96, 560), (180, 470), (264, 560), (264, 800)]), fill=col)
    d.polygon(P([(236, 800), (236, 440), (292, 404), (668, 404), (724, 440), (724, 800)]), fill=col)
    d.polygon(P(smooth([(196, 452), (480, 214), (770, 452), (730, 446), (480, 262), (240, 446)], 6, True)), fill=col)        # the big roof (thin eaves)
    d.polygon(P([(206, 452), (480, 220), (762, 452)]), fill=col)                                                          # the gable is solid
    d.polygon(P([(600, 440), (600, 190), (710, 190), (710, 440)]), fill=col)                                              # tower
    d.polygon(P(smooth([(584, 196), (640, 150), (652, 40), (662, 150), (726, 196)], 6)), fill=col)                          # its leaning spire
    d.line(P([(652, 40), (655, 4)]), fill=col, width=int(5 * SS))
    d.polygon(P([(80, 566), (180, 434), (280, 566)]), fill=col)                                                           # wing roof
    d.polygon(P([(340, 330), (342, 250), (388, 252), (388, 340)]), fill=col)                                              # chimney
    d.polygon(P([(332, 252), (396, 252), (392, 236), (336, 236)]), fill=col)
        # porch with a roof and posts
    d.polygon(P([(404, 700), (556, 700), (566, 680), (394, 680)]), fill=col)
    for px in (410, 548):
        d.rectangle(P([(px - 4, 680)])[0] + P([(px + 4, 800)])[0], fill=col)
    # broken fence along the hill
    for i in range(26):
        x = 8 + i * 38 + (hashf(i, 3) - 0.5) * 8
        if 100 < x < 250 or 700 < x < 960 or x < 80:
            y0 = 790 + math.sin(x / 150.0) * 8 + (0 if x < 400 else 20) - 6
            top = y0 - 66 + hashf(i, 9) * 34
            d.polygon(P([(x - 6, y0 + 20), (x - 6, top + 12), (x, top), (x + 6, top + 12), (x + 6, y0 + 20)]), fill=col)
    d.line(P([(0, 760), (100, 752)]), fill=col, width=int(5 * SS))
    d.line(P([(700, 782), (1000, 800)]), fill=col, width=int(5 * SS))
    # glowing windows: warm light with crossbars, a round attic window, an open door
    yellow, deep = (255, 232, 140, 255), (255, 150, 40, 255)
    wins = [("w1", 262, 566, 380, 650), ("w2", 580, 566, 698, 650), ("w3", 440, 478, 524, 578), ("tw", 628, 236, 684, 338), ("ww", 134, 640, 214, 720)]
    glowmask = Image.new("L", img.size, 0)
    gd = ImageDraw.Draw(glowmask)
    def win(x0, y0, x1, y1):
        arch = smooth([(x0, y1), (x0, y0 + (x1 - x0) * 0.4), ((x0 + x1) / 2, y0), (x1, y0 + (x1 - x0) * 0.4), (x1, y1)], 8)
        return arch
    for name, x0, y0, x1, y1 in wins:
        gd.polygon(P(win(x0, y0, x1, y1)), fill=255)
        HOUSE_WINDOWS.append((name, (x0 + x1) / 2 / w, (y0 + y1) / 2 / h))
    gd.ellipse(P([(450, 296)])[0] + P([(510, 356)])[0], fill=255)                                                         # attic round window
    HOUSE_WINDOWS.append(("attic", 480 / w, 326 / h))
    gd.polygon(P(smooth([(446, 800), (446, 724), (480, 690), (514, 724), (514, 800)], 8)), fill=255)                      # the door
    HOUSE_WINDOWS.append(("door", 480 / w, 750 / h))
    gd.polygon(P([(140, 560), (180, 504), (220, 560)]), fill=255)                                                         # wing attic window
    HOUSE_WINDOWS.append(("wa", 180 / w, 548 / h))
    img.alpha_composite(soft_glow(glowmask, 14, (255, 160, 40), 0.55))
    lit = Image.new("RGBA", img.size, (0, 0, 0, 0))
    fill_clipped(lit, glowmask, yellow, deep, 0.35, 0.95)
    img.alpha_composite(lit)
    d = ImageDraw.Draw(img)
    for name, x0, y0, x1, y1 in wins:                                                                                      # crossbars
        d.line(P([((x0 + x1) / 2, y0), ((x0 + x1) / 2, y1)]), fill=col, width=int(5 * SS))
        d.line(P([(x0, (y0 + y1) / 2 + 8), (x1, (y0 + y1) / 2 + 8)]), fill=col, width=int(5 * SS))
    d.line(P([(480, 296), (480, 356)]), fill=col, width=int(4 * SS))
    d.line(P([(450, 326), (510, 326)]), fill=col, width=int(4 * SS))
    save(img, "house.png", w, h)


# ------------------------------------------------------------------------------------------------ dead trees (and a crow)
def _tree(w, h, seed, trunk=34, depth=7, crow=True, base_x=0.5):
    rnd = random.Random(seed)
    img, d = canvas(w, h)
    col = NIGHT + (255,)
    tips = []
    def branch(x, y, ang, length, wid, dep):
        if dep == 0 or wid < 1.2:
            tips.append((x, y, ang, wid, dep)); return
        pts = [(x, y)]
        a = ang
        n = 4
        for i in range(n):
            a += rnd.uniform(-0.28, 0.28)
            x += math.cos(a) * length / n; y += math.sin(a) * length / n
            pts.append((x, y))
        w1 = wid * 0.72
        # a branch is drawn as a tapering strip of short thick segments
        for i in range(len(pts) - 1):
            ww = wid + (w1 - wid) * (i / (len(pts) - 1))
            thick_line(d, [pts[i], pts[i + 1]], ww, col)
        if dep >= 5 and rnd.random() < 0.6:
            tips.append((pts[-1][0], pts[-1][1], a, w1, dep))
        nb = 2 if dep > 2 else rnd.choice([2, 3])
        for k in range(nb):
            da = rnd.uniform(0.35, 0.8) * (1 if k % 2 == 0 else -1)
            branch(x, y, a + da + (rnd.uniform(-0.1, 0.1)), length * rnd.uniform(0.66, 0.82), w1 * rnd.uniform(0.8, 0.95), dep - 1)
        if rnd.random() < 0.5:
            da = rnd.uniform(-0.9, 0.9)
            branch(pts[2][0], pts[2][1], a + da, length * 0.55, w1 * 0.75, dep - 2)
    bx = w * base_x
    # roots, then the trunk, leaning a little
    for r in (-1, 1):
        d.polygon(P(smooth([(bx + r * trunk * 0.4, h - 70), (bx + r * trunk * 1.5, h - 20), (bx + r * trunk * 2.6, h)], 6)) + [(bx * SS + r * trunk * 3 * SS, h * SS), (bx * SS, h * SS)], fill=col)
    branch(bx, h - 20, -math.pi / 2 + rnd.uniform(-0.12, 0.12), h * 0.185, trunk, depth)
    # the crow: perched on one of the higher tips
    if crow and tips:
        cand = sorted(tips, key=lambda t: t[1])[:6]
        cx, cy, ang, wid, dep = cand[min(2, len(cand) - 1)]
        d.ellipse(P([(cx - 26, cy - 34)])[0] + P([(cx + 24, cy + 4)])[0], fill=col)                         # body
        d.polygon(P([(cx - 20, cy - 14), (cx - 62, cy + 10), (cx - 30, cy - 2)]), fill=col)                 # tail
        d.ellipse(P([(cx + 12, cy - 50)])[0] + P([(cx + 38, cy - 24)])[0], fill=col)                        # head
        d.polygon(P([(cx + 34, cy - 42), (cx + 56, cy - 34), (cx + 34, cy - 32)]), fill=col)                # beak
        d.ellipse(P([(cx + 28, cy - 42)])[0] + P([(cx + 33, cy - 37)])[0], fill=(255, 210, 60, 255))
        d.line(P([(cx - 4, cy + 2), (cx - 4, cy + 14)]), fill=col, width=int(3 * SS))
        d.line(P([(cx + 8, cy + 2), (cx + 8, cy + 14)]), fill=col, width=int(3 * SS))
    return img

def _fits(img, margin=10):
    a = img.getchannel("A")
    W, H = a.size
    m = margin * SS
    return not (a.crop((0, 0, m, H)).getbbox() or a.crop((W - m, 0, W, H)).getbbox() or a.crop((0, 0, W, m)).getbbox())

def make_trees():
    for name, (w, h), kw in (("tree_a.png", (700, 900), dict(trunk=40, depth=7, crow=True)), ("tree_b.png", (560, 760), dict(trunk=30, depth=7, crow=False, base_x=0.45))):
        for seed in range(11, 200):
            im = _tree(w, h, seed, **kw)
            if _fits(im):
                break
        save(im, name, w, h)


# ------------------------------------------------------------------------------------------------ graves
def make_graves(w=1400, h=420):
    from PIL import ImageFont
    img, d = canvas(w, h)
    col = NIGHT + (255,)
    font_path = os.path.join(HERE, "..", "fonts", "Creepster-Regular.ttf")
    d.polygon(P(smooth([(0, 420), (0, 392), (300, 380), (700, 386), (1100, 378), (1400, 390), (1400, 420)], 10)), fill=col)
    def slab(x, y0, wd, ht, lean=0.0, text=None):
        s = Image.new("RGBA", img.size, (0, 0, 0, 0))
        sd = ImageDraw.Draw(s)
        top = smooth([(x, y0 + ht), (x, y0 + wd * 0.5), (x + wd / 2, y0), (x + wd, y0 + wd * 0.5), (x + wd, y0 + ht)], 8)
        sd.polygon(P(top), fill=col)
        sd.line(P([(x + wd * 0.16, y0 + wd * 0.5), (x + wd * 0.84, y0 + wd * 0.5)]), fill=(44, 18, 70, 255), width=int(2.5 * SS))
        if text:
            f = ImageFont.truetype(font_path, int(wd * 0.36 * SS))
            sd.text(((x + wd / 2) * SS, (y0 + wd * 0.8) * SS), text, font=f, fill=(58, 26, 88, 255), anchor="mm")
        s = s.rotate(lean, center=((x + wd / 2) * SS, (y0 + ht) * SS), resample=Image.BICUBIC)
        img.alpha_composite(s)
    def cross(x, y0, ht, lean=0.0):
        s = Image.new("RGBA", img.size, (0, 0, 0, 0))
        sd = ImageDraw.Draw(s)
        sd.rectangle(P([(x - 9, y0)])[0] + P([(x + 9, y0 + ht)])[0], fill=col)
        sd.rectangle(P([(x - 38, y0 + ht * 0.22)])[0] + P([(x + 38, y0 + ht * 0.22 + 16)])[0], fill=col)
        sd.polygon(P([(x - 20, y0 + ht), (x + 20, y0 + ht), (x + 30, y0 + ht + 14), (x - 30, y0 + ht + 14)]), fill=col)
        s = s.rotate(lean, center=(x * SS, (y0 + ht) * SS), resample=Image.BICUBIC)
        img.alpha_composite(s)
    slab(60, 214, 120, 180, -4, "RIP")
    cross(290, 196, 190, 5)
    slab(420, 262, 90, 130, 7)
    slab(640, 188, 136, 206, -2, "BOO")
    # an obelisk
    d.polygon(P([(900, 396), (906, 220), (930, 130), (954, 220), (960, 396)]), fill=col)
    d.polygon(P([(890, 396), (970, 396), (966, 376), (894, 376)]), fill=col)
    cross(1080, 214, 176, -6)
    slab(1180, 236, 100, 156, 5, "RIP")
    slab(1300, 276, 76, 116, -9)
    # dead grass at their feet
    for i in range(90):
        x = hashf(i, 1) * w
        hgt = 14 + hashf(i, 2) * 30
        lean = (hashf(i, 5) - 0.5) * 22
        d.polygon(P([(x - 2, 398), (x + lean, 398 - hgt), (x + 3, 398)]), fill=col)
    save(img, "graves.png", w, h)


# ------------------------------------------------------------------------------------------------ the black cat, back arched
def make_cat(w=420, h=404):
    body, d = canvas(w, 440)
    col = NIGHT + (255,)
    back = smooth([(318, 168), (286, 140), (240, 112), (190, 98), (140, 108), (104, 138), (92, 186), (96, 250)], 10)
    belly = smooth([(318, 200), (290, 236), (240, 250), (176, 252), (126, 256)], 10)
    d.polygon(P(back + belly[::-1]), fill=col)
    d.polygon(P([(104, 136), (140, 108), (240, 112), (286, 140), (302, 240), (96, 262)]), fill=col)
    for i in range(14):                                                    # bristling fur along the arched back
        t = i / 13.0
        x = 296 - t * 196
        y = 150 - math.sin(t * math.pi) * 56 + t * 8
        d.polygon(P([(x - 8, y + 12), (x - 3 + (t - 0.5) * 8, y - 20 + 2), (x + 8, y + 12)]), fill=col)
    d.ellipse(P([(280, 120)])[0] + P([(362, 196)])[0], fill=col)           # head, ears
    d.polygon(P([(292, 134), (284, 82), (320, 120)]), fill=col)
    d.polygon(P([(330, 118), (350, 78), (358, 140)]), fill=col)
    d.polygon(P([(352, 164), (372, 176), (348, 188)]), fill=col)
    thick_line(d, smooth([(104, 220), (60, 214), (30, 170), (36, 112), (62, 68), (52, 34)], 10), 16, col)      # tail
    for ex in (305, 337):                                                  # eyes: green slits
        d.ellipse(P([(ex - 10, 146)])[0] + P([(ex + 10, 168)])[0], fill=(160, 255, 70, 255))
        d.ellipse(P([(ex - 2.5, 146)])[0] + P([(ex + 2.5, 168)])[0], fill=col)
    img = Image.new("RGBA", body.size, (0, 0, 0, 0))
    img.alpha_composite(body, (0, 74 * SS))                                # the body sits lower, so the legs are short
    d = ImageDraw.Draw(img)
    for lx, top in ((276, 290), (244, 296), (136, 300), (108, 300)):
        thick_line(d, [(lx, top), (lx + 1, 350), (lx + 1, 388)], 22, col)
        d.ellipse(P([(lx - 17, 378)])[0] + P([(lx + 21, 396)])[0], fill=col)
    img = img.crop((0, 0, w * SS, h * SS))
    save(img, "cat.png", w, h)


# ------------------------------------------------------------------------------------------------ a zombie hand climbing out of the ground
def make_hand(w=300, h=560):
    img, d = canvas(w, h)
    cx = w / 2
    skin, dark, nail = (118, 168, 92, 255), (54, 92, 58, 255), (40, 30, 20, 255)
    arm = Image.new("L", img.size, 0)
    ad = ImageDraw.Draw(arm)
    ad.polygon(P(smooth([(cx - 44, 560), (cx - 40, 400), (cx - 34, 310)], 8) + smooth([(cx + 34, 310), (cx + 38, 400), (cx + 44, 560)], 8)), fill=255)
    ad.ellipse(P([(cx - 62, 190)])[0] + P([(cx + 62, 330)])[0], fill=255)                                  # the palm
    fingers = [[(cx - 40, 240), (cx - 56, 190), (cx - 62, 120), (cx - 56, 80)],                            # index
               [(cx - 14, 228), (cx - 14, 160), (cx - 12, 90), (cx - 6, 40)],                              # middle
               [(cx + 14, 232), (cx + 24, 170), (cx + 30, 110), (cx + 40, 68)],                            # ring
               [(cx + 38, 250), (cx + 58, 204), (cx + 74, 160), (cx + 92, 134)],                           # little
               [(cx - 52, 296), (cx - 92, 268), (cx - 112, 224), (cx - 118, 190)]]                         # thumb
    for f in fingers:
        # tapering fingers: bone-thin at the tips
        pts = smooth(f, 8)
        for i in range(len(pts) - 1):
            t = i / (len(pts) - 1)
            thick_line(ad, [pts[i], pts[i + 1]], 24 - 8 * t, 255)
    arm = arm.filter(ImageFilter.GaussianBlur(0.5 * SS))
    fill_clipped(img, arm, (150, 198, 112, 255), (78, 124, 70, 255), 0.0, 1.0)
    d = ImageDraw.Draw(img)
    # knuckle creases, a gash, nails, a torn sleeve at the bottom
    for f in fingers:
        t = f[-1]
        d.ellipse(P([(t[0] - 7, t[1] - 5)])[0] + P([(t[0] + 7, t[1] + 12)])[0], fill=nail)
        for k in (1, 2):
            px, py = f[k]
            d.line(P([(px - 9, py), (px + 9, py + 3)]), fill=dark, width=int(2.5 * SS))
    d.line(P([(cx - 30, 340), (cx - 8, 366), (cx + 6, 352), (cx + 22, 380)]), fill=dark, width=int(3 * SS))
    d.polygon(P([(cx - 60, 470), (cx - 38, 456), (cx - 22, 478), (cx - 4, 458), (cx + 16, 480), (cx + 36, 456), (cx + 58, 472), (cx + 60, 560), (cx - 60, 560)]), fill=(60, 44, 70, 255))
    save(img, "hand.png", w, h)


# ------------------------------------------------------------------------------------------------ eyes in the dark
def make_eyes(w=320, h=140):
    img, d = canvas(w, h)
    m = Image.new("L", img.size, 0)
    md = ImageDraw.Draw(m)
    for ex in (88, 232):
        md.polygon(P(smooth([(ex - 50, 76), (ex - 6, 40), (ex + 48, 56), (ex + 6, 94)], 8, True)), fill=255)
    img.alpha_composite(soft_glow(m, 12, (255, 230, 60), 1.0))
    g = Image.new("RGBA", img.size, (0, 0, 0, 0))
    fill_clipped(g, m, (255, 252, 160, 255), (255, 176, 20, 255))
    img.alpha_composite(g)
    d = ImageDraw.Draw(img)
    for ex in (88, 232):
        d.ellipse(P([(ex - 5, 48)])[0] + P([(ex + 5, 86)])[0], fill=(20, 6, 12, 255))                        # slit pupils
    save(img, "eyes.png", w, h)


# ------------------------------------------------------------------------------------------------ a lightning bolt
def make_bolt(w=300, h=1000):
    rnd = random.Random(5)
    img, d = canvas(w, h)
    def bolt(x, y, ang, steps, step, wd, depth):
        pts = [(x, y)]
        for i in range(steps):
            ang += rnd.uniform(-0.55, 0.55)
            ang = max(-0.65 + math.pi / 2 - 0.0, min(math.pi / 2 + 0.65, ang)) if depth == 0 else ang
            x += math.cos(ang) * step * rnd.uniform(0.6, 1.2); y += math.sin(ang) * step * rnd.uniform(0.6, 1.2)
            pts.append((x, y))
            if depth < 2 and rnd.random() < 0.22:
                bolt(x, y, ang + rnd.choice([-1, 1]) * rnd.uniform(0.5, 1.0), max(3, steps // 3), step * 0.8, wd * 0.55, depth + 1)
        thick_line(d, pts, wd, (255, 255, 255, 255), joint=False)
    m = Image.new("RGBA", img.size, (0, 0, 0, 0))
    bolt(w * 0.5, 0, math.pi / 2, 16, 62, 15, 0)
    core = img
    glow = Image.new("RGBA", img.size, (0, 0, 0, 0))
    a = core.getchannel("A")
    g1 = soft_glow(a, 14, (160, 140, 255), 1.0)
    g2 = soft_glow(a, 4, (210, 210, 255), 1.0)
    out = Image.new("RGBA", img.size, (0, 0, 0, 0))
    out.alpha_composite(g1); out.alpha_composite(g2); out.alpha_composite(core)
    save(out, "bolt.png", w, h)


# ------------------------------------------------------------------------------------------------ a ragged strip of cloud
def make_cloud(w=1600, h=300):
    rnd = random.Random(3)
    img, d = canvas(w, h)
    for i in range(70):
        x = rnd.uniform(260, w - 260)
        y = h * 0.52 + math.sin(x / 190.0) * 24 + rnd.uniform(-26, 26)
        rx = rnd.uniform(70, 190); ry = rnd.uniform(16, 44)
        d.ellipse(P([(x - rx, y - ry)])[0] + P([(x + rx, y + ry)])[0], fill=(30, 10, 52, rnd.randint(60, 130)))
    img = img.filter(ImageFilter.GaussianBlur(9 * SS))
    save(img, "cloud.png", w, h)


# ------------------------------------------------------------------------------------------------ far and near hills
def make_hills(w=2000, h=360):
    for name, seed, base, amp, color, tufts in (("hills_far.png", 4, 0.46, 0.17, (34, 11, 58), False), ("hills_near.png", 9, 0.60, 0.10, NIGHT, True)):
        rnd = random.Random(seed)
        img, d = canvas(w, h)
        pts = [(-20, h * base)]
        x = 0.0
        while x < w + 120:
            x += rnd.uniform(160, 300)
            pts.append((x, h * (base + rnd.uniform(-amp, amp))))
        ridge = smooth(pts, 14)
        d.polygon(P(ridge + [(w + 200, h + 20), (-20, h + 20)]), fill=color + (255,))
        if tufts:
            for i in range(160):
                xx = rnd.uniform(0, w)
                # y of the ridge at xx
                yy = min(ridge, key=lambda p: abs(p[0] - xx))[1]
                hgt = rnd.uniform(10, 30); lean = rnd.uniform(-9, 9)
                d.polygon(P([(xx - 2, yy + 3), (xx + lean, yy - hgt), (xx + 3, yy + 3)]), fill=color + (255,))
        save(img, name, w, h)


# ------------------------------------------------------------------------------------------------ a cobweb for the top left corner (flip it for the right)
def make_web(w=520, h=520):
    img, d = canvas(w, h)
    spokes = 7
    col = (236, 232, 255, 190)
    ang = [i * (math.pi / 2) / (spokes - 1) for i in range(spokes)]
    R = 500
    for a in ang:
        d.line(P([(0, 0), (math.cos(a) * R, math.sin(a) * R)]), fill=col, width=int(2.4 * SS))
    for ring in (70, 130, 195, 265, 340, 420, 500):
        for i in range(spokes - 1):
            a0, a1 = ang[i], ang[i + 1]
            p0 = (math.cos(a0) * ring, math.sin(a0) * ring)
            p1 = (math.cos(a1) * ring, math.sin(a1) * ring)
            am = (a0 + a1) / 2
            ctrl = (math.cos(am) * ring * 0.86, math.sin(am) * ring * 0.86)
            pts = [((1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * ctrl[0] + t * t * p1[0], (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * ctrl[1] + t * t * p1[1]) for t in [k / 14 for k in range(15)]]
            d.line(P(pts), fill=col, width=int(1.8 * SS))
    for (px, py) in ((196, 130), (120, 300)):                                                             # dew drops
        d.ellipse(P([(px - 4, py - 4)])[0] + P([(px + 4, py + 4)])[0], fill=(255, 255, 255, 230))
    img = img.filter(ImageFilter.GaussianBlur(0.3 * SS))
    save(img, "web.png", w, h)


if __name__ == "__main__":
    want = set(sys.argv[1:])
    def todo(n): return not want or n in want
    print("drawing into", OUT)
    if todo("moon"): save(make_moon(1024), "moon.png", 1024, 1024)
    if todo("bat"): make_bat()
    if todo("ghost"):
        make_ghost("ghost_a.png", "scared")
        make_ghost("ghost_b.png", "evil")
        make_ghost("ghost_c.png", "happy")
    if todo("pumpkin"):
        make_pumpkin("pumpkin_a.png", "grin")
        make_pumpkin("pumpkin_b.png", "happy", body=((255, 164, 52), (220, 86, 12)))
        make_pumpkin("pumpkin_c.png", "evil", body=((255, 138, 28), (196, 62, 6)))
    if todo("spider"): make_spider()
    for extra in ("witch", "house", "trees", "graves", "cat", "hand", "eyes", "bolt", "cloud", "hills", "web"):
        fn = globals().get("make_" + extra)
        if fn and todo(extra): fn()
    if HOUSE_WINDOWS:
        print("house windows (fraction of the picture):", ", ".join("%s %.3f,%.3f" % w for w in HOUSE_WINDOWS))
