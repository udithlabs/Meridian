#!/usr/bin/env python3
"""Makes art/world.json, the map the GTA VI pack's Launch Map draws.

You do not need this to use the theme: art/world.json is already made. It is here so the map can be made again (or changed).
    npm install world-atlas            (Natural Earth country shapes, public domain, as TopoJSON)
    python3 make_map.py node_modules/world-atlas/countries-50m.json

The file holds each country's outline as rings of x,y points in a box of 0..1 x 0..1 (plate carree, 83.6 N to 56 S, squeezed a little to
a box 2.9 times wider than tall), plus two helper groups: _grid (latitude / longitude lines) and _bands (alternate time zone stripes).
"""
import json, math, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "art", "world.json")

LAT_TOP, LAT_BOT = 83.6, -56.0
ASPECT = 2.9

def project(lon, lat):
    """lon/lat in degrees -> (x, y) in the unit box"""
    return ((lon + 180.0) / 360.0, (LAT_TOP - lat) / (LAT_TOP - LAT_BOT))

# ---------------------------------------------------------------- TopoJSON decoding
def decode_arcs(topo):
    sx, sy = topo["transform"]["scale"]; tx, ty = topo["transform"]["translate"]
    arcs = []
    for arc in topo["arcs"]:
        x = y = 0; pts = []
        for dx, dy in arc:
            x += dx; y += dy
            pts.append((x * sx + tx, y * sy + ty))
        arcs.append(pts)
    return arcs

def ring_of(arcs, idxs):
    pts = []
    for i in idxs:
        a = arcs[i] if i >= 0 else list(reversed(arcs[~i]))
        pts.extend(a if not pts else a[1:])
    return pts

def polygons_of(geom, arcs):
    """list of outer rings (holes are left out) of a Polygon / MultiPolygon"""
    out = []
    if geom["type"] == "Polygon": polys = [geom["arcs"]]
    elif geom["type"] == "MultiPolygon": polys = geom["arcs"]
    else: return out
    for poly in polys:
        if poly: out.append(ring_of(arcs, poly[0]))
    return out

# ---------------------------------------------------------------- geometry helpers
def area_deg2(r):
    s = 0.0
    for i in range(len(r)):
        x1, y1 = r[i]; x2, y2 = r[(i + 1) % len(r)]
        s += x1 * y2 - x2 * y1
    return abs(s) / 2.0

def clip_south(r, lat0):
    """keep the part of a ring north of lat0 (Sutherland-Hodgman against one line)"""
    out = []
    n = len(r)
    for i in range(n):
        a = r[i]; b = r[(i + 1) % n]
        ina, inb = a[1] >= lat0, b[1] >= lat0
        if ina: out.append(a)
        if ina != inb:
            t = (lat0 - a[1]) / (b[1] - a[1])
            out.append((a[0] + (b[0] - a[0]) * t, lat0))
    return out

def clip_lon(r, lon0, keep_below):
    """keep the part of a ring west of lon0 (keep_below) or east of it (Sutherland-Hodgman against one vertical line)"""
    out = []
    n = len(r)
    for i in range(n):
        a = r[i]; b = r[(i + 1) % n]
        ina = a[0] <= lon0 if keep_below else a[0] >= lon0
        inb = b[0] <= lon0 if keep_below else b[0] >= lon0
        if ina: out.append(a)
        if ina != inb:
            t = (lon0 - a[0]) / (b[0] - a[0])
            out.append((lon0, a[1] + (b[1] - a[1]) * t))
    return out

def split_antimeridian(r):
    """a ring that runs across the 180th meridian (Russia, Fiji, the Aleutians) becomes two rings, one on each edge of the map"""
    if not any(abs(r[i][0] - r[(i + 1) % len(r)][0]) > 180 for i in range(len(r))): return [r]
    un = [r[0]]
    for lon, lat in r[1:]:
        pl = un[-1][0]
        while lon - pl > 180: lon -= 360
        while lon - pl < -180: lon += 360
        un.append((lon, lat))
    shift = -360 * math.floor((min(p[0] for p in un) + 180) / 360)
    un = [(lon + shift, lat) for lon, lat in un]
    parts = []
    west = clip_lon(un, 180.0, True)
    east = [(lon - 360.0, lat) for lon, lat in clip_lon(un, 180.0, False)]
    for q in (west, east):
        if len(q) >= 3: parts.append(q)
    return parts

# Russia crosses nine time zones, so on the launch map it is cut into strips (each strip lights up with its own wave): ids 643, 643b, 643c ...
RUSSIA_CUTS = [50, 62, 80, 105, 125, 140, 170]          # longitudes where a new strip starts (the part east of 170 and the Chukotka tip across the date line are the last strip)
RUSSIA_SUFFIX = ["", "b", "c", "d", "e", "f", "g", "h"]

def russia_strips(r):
    """a ring (lon, lat) of Russia -> [(suffix, ring)], cut along the meridians above"""
    if max(p[0] for p in r) < 0:                                   # the tip beyond the date line, drawn at the far left of the map
        return [("h", r)]
    out = []
    edges = [-1000.0] + RUSSIA_CUTS + [1000.0]
    for k in range(len(edges) - 1):
        q = r
        if edges[k] > -999: q = clip_lon(q, edges[k], False)
        if len(q) >= 3 and edges[k + 1] < 999: q = clip_lon(q, edges[k + 1], True)
        if len(q) >= 3: out.append((RUSSIA_SUFFIX[k], q))
    return out

def simplify(pts, tol):
    """Douglas-Peucker on a closed ring (in unit-box coordinates)"""
    if len(pts) < 8: return pts
    def dp(p, lo, hi, keep):
        stack = [(lo, hi)]
        while stack:
            a, b = stack.pop()
            ax, ay = p[a]; bx, by = p[b]
            dx, dy = bx - ax, by - ay
            L2 = dx * dx + dy * dy
            best, bi = -1.0, -1
            for i in range(a + 1, b):
                px, py = p[i]
                if L2 == 0: d = math.hypot(px - ax, py - ay)
                else:
                    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / L2))
                    d = math.hypot(px - (ax + t * dx), py - (ay + t * dy))
                if d > best: best, bi = d, i
            if best > tol and bi > 0:
                keep[bi] = True; stack.append((a, bi)); stack.append((bi, b))
    keep = [False] * len(pts); keep[0] = keep[-1] = True
    # split a ring at its farthest point so the closed shape has two anchors
    far = max(range(len(pts)), key=lambda i: (pts[i][0] - pts[0][0]) ** 2 + (pts[i][1] - pts[0][1]) ** 2)
    keep[far] = True
    dp(pts, 0, far, keep); dp(pts, far, len(pts) - 1, keep)
    return [p for p, k in zip(pts, keep) if k]

# the countries each launch wave unlocks (ISO numeric ids) - their small islands are always kept
KEEP_SMALL = set("554 036 392 410 158 458 702 764 360 356 144 784 682 792 710 100 196 246 300 376 642 804 040 056 191 203 208 250 276 348 380 442 470 528 578 616 703 705 752 756 724 826 372 620 352 076 032 152 600 858 840 124 170 218 591 604 484 188 222 320 340 558".split())

def main(path):
    topo = json.load(open(path))
    arcs = decode_arcs(topo)
    groups = []
    for g in topo["objects"]["countries"]["geometries"]:
        gid = g.get("id")
        name = (g.get("properties") or {}).get("name", "")
        if gid is None: gid = "x_" + "".join(ch for ch in name.lower() if ch.isalnum())
        gid = str(gid)
        if gid == "010": continue                                    # Antarctica
        rings = {}                                                    # id -> rings (Russia becomes several ids)
        for r in polygons_of(g, arcs):
            if len(r) < 4: continue
            r = r[:-1] if r[0] == r[-1] else r
            for r in split_antimeridian(r):
                if max(p[1] for p in r) < LAT_BOT: continue
                if min(p[1] for p in r) < LAT_BOT: r = clip_south(r, LAT_BOT)
                if len(r) < 3: continue
                if max(p[0] for p in r) < -165 and area_deg2(r) < 1.0: continue      # a speck that crossed the date line (Chatham Islands, a Fijian islet): it would be a lonely dot at the far left of the map
                for suffix, q in (russia_strips(r) if gid == "643" else [("", r)]):
                    if area_deg2(q) < 0.35 and gid not in KEEP_SMALL: continue
                    pr = [project(lon, lat) for lon, lat in q]
                    pr = simplify(pr + [pr[0]], 0.00045)[:-1]
                    if len(pr) < 3: continue
                    rings.setdefault(gid + suffix, []).append(pr)
        for gid2, rs in rings.items(): groups.append((gid2, name, rs))

    def num(v): return round(v, 4)
    out = {"aspect": ASPECT, "note": "made by _source/make_map.py from Natural Earth (public domain) country shapes", "groups": []}
    for gid, name, rings in groups:
        out["groups"].append({"id": gid, "name": name, "rings": [[num(c) for p in r for c in p] for r in rings]})
    # helper groups: a lat/lon grid and the alternate time zone stripes
    grid = []
    for lon in range(-150, 181, 30):
        x, y0 = project(lon, LAT_TOP); _, y1 = project(lon, LAT_BOT); grid.append([num(x), num(y0), num(x), num(y1)])
    for lat in (60, 30, 0, -30):
        x0, y = project(-180, lat); x1, _ = project(180, lat); grid.append([num(x0), num(y), num(x1), num(y)])
    out["groups"].append({"id": "_grid", "open": True, "rings": grid})
    bands = []
    for k in range(-12, 13):
        if k % 2: continue
        xa, _ = project(max(-180, 15 * k - 7.5), 0); xb, _ = project(min(180, 15 * k + 7.5), 0)
        bands.append([num(xa), 0.0, num(xb), 0.0, num(xb), 1.0, num(xa), 1.0])
    out["groups"].append({"id": "_bands", "rings": bands})
    # the shape of the equator and the tropics, on their own, so they can be drawn brighter
    x0, y = project(-180, 0); x1, _ = project(180, 0)
    out["groups"].append({"id": "_equator", "open": True, "rings": [[num(x0), num(y), num(x1), num(y)]]})
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f: json.dump(out, f, separators=(",", ":"))
    print("wrote", OUT, os.path.getsize(OUT), "bytes,", len(out["groups"]), "groups,",
          sum(len(g["rings"]) for g in out["groups"]), "rings,", sum(len(r) // 2 for g in out["groups"] for r in g["rings"]), "points")

if __name__ == "__main__":
    if len(sys.argv) != 2: sys.exit(__doc__)
    main(sys.argv[1])
