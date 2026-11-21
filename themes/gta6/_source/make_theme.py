#!/usr/bin/env python3
"""Writes theme.json of the GTA VI pack (the countdown tab with its Launch Map view).

You do not need this to use the theme: theme.json is already made, and you can edit it by hand like any other pack.
It is here because the pack has 14 launch waves, and writing the same few layers 14 times by hand is dull. Change WAVES or a layer below and run
        python3 make_theme.py        (writes ../theme.json)
"""
import json, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.environ.get("THEME_OUT", os.path.join(HERE, "..", "theme.json"))
MARKS = os.path.join(HERE, "marks.json")
WORLD = os.environ.get("WORLD_JSON", os.path.join(HERE, "..", "art", "world.json"))

PINK, TEAL, GOLD, INK, VIOLET, PLATE = "#FF3E91", "#2FE6D3", "#FFD36B", "#FFF3E6", "#8F6BFF", "#12052C"

# ---------------------------------------------------------------------------------------------------------------------------------
#  the 14 launch waves (the rolling midnight launch: every region unlocks at 00:00 its own time, so the world goes live from east to west)
#  code, region name, countries (Natural Earth ids), cities, time zone name, hours from UTC, pin lon, pin lat, chip centre on the map (x, y as 0..1 of the map), where the leader line leaves the chip
# ---------------------------------------------------------------------------------------------------------------------------------
WAVES = [
    ("NZ",     "NEW ZEALAND",                     ["554"],                      "Auckland, Wellington, Christchurch",       "NZDT",       13,   174.8, -37.0, (0.965, 1.052), "t"),
    ("AUS",    "AUSTRALIA",                       ["036"],                      "Sydney, Melbourne, Brisbane, Perth",       "AEDT",       11,   151.2, -33.9, (0.835, 1.052), "t"),
    ("JPN",    "JAPAN  ·  SOUTH KOREA",           ["392", "410"],               "Tokyo, Osaka, Seoul, Busan",               "JST",         9,   139.7,  35.7, (0.925, 0.440), "t"),
    ("SGP",    "SINGAPORE  ·  MALAYSIA  ·  TAIWAN", ["702", "458", "158"],      "Singapore, Kuala Lumpur, Taipei",          "SGT",         8,   103.8,   1.35, (0.880, -0.052), "b"),
    ("TH/ID",  "THAILAND  ·  INDONESIA",          ["764", "360"],               "Bangkok, Jakarta, Bali",                   "ICT / WIB",   7,   100.5,  13.75, (0.705, 1.052), "t"),
    ("IND/LK", "INDIA  ·  SRI LANKA",             ["356", "144"],               "Delhi, Mumbai, Colombo",                   "IST",         5.5,  78.9,  21.0, (0.575, 1.052), "t"),
    ("UAE",    "UNITED ARAB EMIRATES",            ["784"],                      "Dubai, Abu Dhabi",                         "GST",         4,    55.3,  25.2, (0.445, 1.052), "t"),
    ("SA/TR",  "SAUDI ARABIA  ·  TURKEY",         ["682", "792"],               "Riyadh, Jeddah, Istanbul",                 "AST / TRT",   3,    46.7,  24.7, (0.750, -0.052), "b"),
    ("E.EU",   "EASTERN EUROPE",                  ["300", "100", "196", "246", "233", "428", "440", "642"], "Athens, Sofia, Helsinki, Bucharest", "EET", 2, 26.1, 44.4, (0.620, -0.052), "b"),
    ("C.EU",   "CENTRAL EUROPE",                  ["040", "056", "191", "276", "250", "724", "380", "528", "756", "616", "203", "703", "705", "348", "208", "752", "578", "442"], "Berlin, Paris, Madrid, Rome", "CET", 1, 13.4, 52.5, (0.490, -0.052), "b"),
    ("UK/PT",  "UK  ·  IRELAND  ·  PORTUGAL",     ["826", "372", "620", "352"], "London, Dublin, Lisbon, Reykjavik",        "GMT",         0,    -0.13, 51.5, (0.360, -0.052), "b"),
    ("BRA",    "BRAZIL  ·  ARGENTINA  ·  CHILE",  ["076", "032", "152", "858", "600"], "Sao Paulo, Buenos Aires, Santiago", "BRT / ART", -3,   -46.6, -23.5, (0.315, 1.052), "t"),
    ("USA",    "USA  ·  CANADA  ·  COLOMBIA  ·  PERU", ["840", "124", "170", "604", "218", "591"], "All US and Canada zones unlock together", "EST", -5, -74.0, 40.7, (0.200, -0.052), "b"),
    ("MEX",    "MEXICO  ·  CENTRAL AMERICA",      ["484", "188", "558", "320", "222", "340"], "Mexico City, Guatemala City, San Jose", "CST", -6, -99.1, 19.4, (0.185, 1.052), "t"),
]
N = len(WAVES)
LAT_TOP, LAT_BOT = 83.6, -56.0

def unit(lon, lat):
    return ((lon + 180.0) / 360.0, (LAT_TOP - lat) / (LAT_TOP - LAT_BOT))

# ---------------------------------------------------------------------------------------------------------------------------------
#  EVERY country goes live with its time zone. The waves above name the main countries; every other country on the map (China, Russia, Pakistan, Egypt,
#  Bolivia ...) is given to the wave whose time is nearest to its own, so by the last wave the whole map is lit. Russia is cut into strips (see make_map.py).
# ---------------------------------------------------------------------------------------------------------------------------------
TZ_OF = {                                                  # the countries whose time zone is not where their middle is (id: hours from UTC)
    "643": 3, "643b": 4, "643c": 5.5, "643d": 7, "643e": 8, "643f": 9, "643g": 11, "643h": 13,                      # Russia, strip by strip
    "156": 8, "496": 8, "608": 8, "096": 8, "398": 6, "860": 5, "795": 5, "762": 5, "417": 6, "004": 4.5, "364": 3.5,   # China Mongolia Philippines Brunei Kazakhstan Uzbekistan Turkmenistan Tajikistan Kyrgyzstan Afghanistan Iran
    "524": 5.75, "064": 6, "050": 6, "104": 6.5, "598": 10, "242": 12, "090": 11, "548": 11, "540": 11,                 # Nepal Bhutan Bangladesh Myanmar PapuaNG Fiji Solomon Vanuatu NewCaledonia
    "068": -4, "862": -4, "328": -4, "214": -4, "192": -5, "388": -5, "332": -5, "044": -5, "304": -3,                  # Bolivia Venezuela Guyana DomRep Cuba Jamaica Haiti Bahamas Greenland
    "012": 1, "504": 1, "788": 1, "434": 2, "818": 2,                                                                  # Algeria Morocco Tunisia Libya Egypt
}

def load_world():
    try:
        return json.load(open(WORLD))["groups"]
    except Exception as e:
        sys.exit("make_theme.py needs the map file (%s): %s" % (WORLD, e))

def ring_area(r):
    xs, ys = r[0::2], r[1::2]
    return abs(sum(xs[i] * ys[(i + 1) % len(xs)] - xs[(i + 1) % len(xs)] * ys[i] for i in range(len(xs)))) / 2

def by_id(world):
    """country id -> its groups (an id can have more than one: Australia and its far islands)"""
    d = {}
    for g in world: d.setdefault(g["id"], []).append(g)
    return d

def biggest_ring_x(groups):
    best, bx = -1.0, 0.5
    for g in groups:
        for r in g["rings"]:
            a = ring_area(r)
            if a > best: best, bx = a, sum(r[0::2]) / len(r[0::2])
    return bx

def assign_all(waves, world):
    """-> for each wave, the list of EVERY country id that goes live with it (the named ones first)"""
    offs = [w[5] for w in waves]
    full = [list(w[2]) for w in waves]
    named = {c for w in waves for c in w[2]}
    for gid, groups in by_id(world).items():
        if gid.startswith("_") or gid in named: continue
        o = TZ_OF.get(gid)
        if o is None: o = round((biggest_ring_x(groups) * 360.0 - 180.0) / 7.5) / 2.0
        k = min(range(len(waves)), key=lambda i: (abs(offs[i] - o), i))            # nearest time; a tie goes to the earlier wave
        full[k].append(gid)
    return full

def wave_fronts(full, world):
    """x (0..1 along the map) where the lit zone starts after k waves are live: FR[0] = 1 (nothing), ... FR[14] = 0 (the whole map).
    Each country counts by its biggest piece (so French Guiana, the Azores or the Aleutians do not drag the edge far from the mainland)."""
    ids = by_id(world)
    fr = [1.0]
    for wave_ids in full:
        lo = 1.0
        for gid in wave_ids:
            best, bring = -1.0, None
            for g in ids.get(gid, []):
                for r in g["rings"]:
                    a = ring_area(r)
                    if a > best: best, bring = a, r
            if bring is not None and sum(bring[0::2]) / len(bring[0::2]) > 0.04: lo = min(lo, min(bring[0::2]))
        fr.append(min(fr[-1], lo))
    fr[-1] = 0.0
    return fr

WORLD_GROUPS = load_world()
FULL = assign_all(WAVES, WORLD_GROUPS)
FR = wave_fronts(FULL, WORLD_GROUPS)

def utc_text(h):
    sign = "+" if h >= 0 else "-"; a = abs(h)
    return "UTC%s%d%s" % (sign, int(a), ":30" if a % 1 else "") if h != 0 else "UTC+0"

def r4(x): return round(x, 4)

# ---------------------------------------------------------------------------------------------------------------------------------
#  writing helpers: the file is mostly one layer per line, with notes
# ---------------------------------------------------------------------------------------------------------------------------------
out = []
def raw(s=""): out.append(s)
def note(s, indent=8):
    for line in s.strip("\n").split("\n"): out.append(" " * indent + "// " + line if line.strip() else "")
def layer(type_, **kw):
    d = {"type": type_}
    d.update(kw)
    s = json.dumps(d, ensure_ascii=False)
    out.append("        " + s.replace('": ', '": ').replace(', "', ', "') + ",")
    return d
n_layers = [0]
def L(type_, **kw):
    n_layers[0] += 1
    return layer(type_, **kw)

def f(x):
    """number or formula -> as it should appear in the file"""
    return x

# ---------------------------------------------------------------------------------------------------------------------------------
#  the head of the file
# ---------------------------------------------------------------------------------------------------------------------------------
raw("""// ============================================================================================================
//  GTA VI  -  a Meridian theme pack            (version 2: a city, a launch map, boats, a helicopter and planes)
// ============================================================================================================
//  This file is the whole theme. Everything after // is a note for you and is ignored by Meridian.
//  How the format works: see themes/README.txt (next to this folder). Quick version:
//    - numbers are fractions of the window: x and w of the WIDTH, y and h of the HEIGHT (0 = left/top, 1 = right/bottom)
//    - anywhere a number goes you can write a formula in quotes, like "0.5 + 0.01*sin(t)"  (t = time in seconds)
//    - colours are "#RRGGBB", or "#RRGGBBAA" with transparency, or "#RRGGBB/0.5" (the same with a plain 0..1 transparency)
//    - save the file and Meridian reloads it by itself within a few seconds
//  This pack only adds ONE tab to the dock ("GTA VI"). Its colours, font and artwork are used on that tab and nowhere else:
//  Sky, Quiet, Timer, Almanac and Widgets look exactly as they always do.
//  To remove this theme: delete the folder  themes/gta6  (or it hides itself on the "expires" date below).
//
//  THE TAB HAS TWO VIEWS.   COUNTDOWN: the big number, the police stars, the city, boats, a helicopter and planes.
//                           LAUNCH MAP: the world, lit up from east to west as the game goes live: every country, region by region, and the sea between them.
//  Click the pill at the top (or press Tab or Space) to switch. On the map: click a region card, hover one to preview it,
//  or use the Up / Down keys (or the < > buttons) to step from wave to wave.
//
//  This file is written by  _source/make_theme.py  (14 waves are tedious to type), but it is an ordinary theme file:
//  you can edit it by hand. If you run make_theme.py again it is made anew.
//
//  THE POLICE STARS.  Six stars, as in GTA VI. They FILL UP as the date comes closer: six days out none are lit, five days out one,
//  and so on, so 2 days to go = 4 of 6 stars, and the last day all six burn. Once the game is out they blink on and off together,
//  red and blue, like the stars do when the police are chasing you.   (Want it the other way round, one star per day LEFT?  Change the
//  line "starLit" below to  release_done ? 6 : clamp(release_days, 0, 6) .)
//
//  The artwork is original (a made-up Art Deco sunset city, palm trees, boats, a police helicopter, a far-away plane, a banner plane). It does not use the game's logo or characters.
//  The world map is made from Natural Earth country shapes (public domain). See art/ and _source/ for how it was made.
//  The launch times are the rolling-midnight schedule reported from PlayStation Store data (not announced by Rockstar): every region
//  unlocks at 00:00 its own local time, from New Zealand (UTC+13) in the east to Mexico (UTC-6) in the west. Times are shown in YOUR
//  clock's time zone. The 14 waves name their main countries; every other country on the map (China, Russia, Pakistan, Egypt, Bolivia ...) goes live with the wave
//  whose time is nearest to its own, so when the last wave arrives the whole map is lit. Russia is cut into strips, one per time zone band.
// ============================================================================================================
{
  "id": "gta6",
  "name": "GTA VI",
  "author": "Made for Meridian",
  "version": 2,
  "description": "Neon sunset city with a release countdown, police stars and a clickable launch map. Goes away by itself on 20 December 2026.",

  "global": false,                      // false (the default): only the tab(s) below. true: can also be chosen as a theme for every tab
  "expires": "2026-12-20",              // the theme and its tab disappear on this date (YYYY-MM-DD). Delete this line to keep it forever.

  // ---- colours of the GTA VI tab itself
  "base": "night",                       // start from a built-in theme (day, dawn, sunset, dusk, night, ember) and change what you list
  "palette": {
    "bgTop":    "#1A0A40",
    "bgBottom": "#A8307A",
    "ink":      "#FFF3E6",               // main text
    "ink2":     "#FFC4DC",               // second text
    "ink3":     "#D9A3DE",               // quiet text
    "line":     "#FFC7E642",
    "accent":   "#FF5CA8",
    "accent2":  "#35E6D4",
    "panel":    "#FFFFFF16",
    "plate":    "#2A0F4D",
    "starVis":  0.7,
    "neon":     1.0,                     // 0..1: glow around the big clock digits
    "dark":     true,
    "neonColor": "#FF3E91"               // colour of that glow
  },

  // ---- fonts: any role you list is replaced while this tab is on (TrueType or OpenType files, placed INSIDE this folder).
  //      roles: display (big clock digits), light, medium, serif, serifItalic, mono, monoMedium
  //      This theme uses two:   display    = the big number, hours:minutes:seconds and the region names  -> Bungee (chunky neon-sign letters)
  //                             monoMedium = every small spaced-out line (header, labels, cards, footer, banner) -> Chakra Petch SemiBold
  //      TO CHANGE A FONT: copy the .ttf into the  fonts  folder next to this file and change the path below.
  //      Other pairings are already in  fonts/alternatives  (just swap the path):
  //          display:  "fonts/alternatives/ArchivoBlack-Regular.ttf"        wide, clean, premium
  //                    "fonts/alternatives/BigShouldersDisplay-Black.ttf"   tall and narrow, like a street sign
  //                    "fonts/alternatives/Anton-Regular.ttf"               the first version of this theme
  //      Every font needs its licence file in fonts/licenses if you share the theme. All of these are free (SIL Open Font License).
  "fonts": {
    "display":    "fonts/Bungee-Regular.ttf",
    "monoMedium": "fonts/ChakraPetch-SemiBold.ttf"
  },
""")

# countdowns
raw("  // ---- dates to count to. 'release' is the big one: midnight at the start of Thursday 19 November 2026 WHERE YOU ARE.")
raw("  //      w1 .. w14 are the launch waves. Each one is midnight at its own place ('utc' = its hours from UTC), so each lands at a different moment on your clock.")
raw("  //      Every countdown gives:  NAME_days NAME_hrs NAME_min NAME_sec  (time left, or since once it has passed)  NAME_done (1 once passed)  NAME_left / NAME_past (seconds)")
raw("  //      NAME_daysf (days left as a fraction)  and the words  {NAME_when} {NAME_clock} {NAME_date} {NAME_short} {NAME_dow}  (in the clock's own time zone)")
raw('  "countdowns": {')
raw('    "release": "2026-11-19 00:00",')
for i, w in enumerate(WAVES, 1):
    raw('    "w%d": { "at": "2026-11-19 00:00", "utc": %s }%s       // %s  (%s, %s)' % (i, w[5], "," if i < N else " ", w[0], w[4], utc_text(w[5])))
raw("  },")
raw("")
raw("  // ---- numbers that a click (or a key) changes, and numbers that follow the pointer. 'view_t' / 'sel_t' are the seconds since they last changed.")
raw('  "state": { "view": 0, "sel": 0 },     // view: 0 countdown, 1 launch map.   sel: the wave you pinned (1..14), or 0 = follow the next wave to launch')
raw('  "hover": { "hov": 0 },                 // hov: the wave card the pointer is over (0 = none)')
raw("")

# vars
raw("  // ---- your own names for numbers used below. Written once, used many times. They are worked out in this order.")
W_DONE = " + ".join("w%d_done" % i for i in range(1, N + 1))
def PICK(vals, key="doneN"):
    """a formula that gives vals[k] when `key` is k (k = 0 .. len-1): a chain of ?: choices"""
    vals = [("%.4f" % v).rstrip("0").rstrip(".") if isinstance(v, float) else str(v) for v in vals]
    e = vals[-1]
    for k in range(len(vals) - 2, -1, -1): e = "%s == %d ? %s : (%s)" % (key, k, vals[k], e)
    return e
vars_ = [
    ("pf",         "portrait ? 0.62 : 1",                                         "palm trees a bit smaller in a tall window"),
    ("horizon",    "portrait ? 0.82 : 0.81",                                       "where the sea meets the sky (fraction of height)"),
    ("sunR",       "portrait ? min(0.105, 0.20*W/H) : 0.145",                        "sun radius (fraction of height)"),
    ("top",        "portrait ? 0.07 : 0",                                          "in a tall window everything under the top row moves down a bit, to make room for the view pill"),
    ("numCap",     "portrait ? 0.20 : 0.235",                                      "height of the big number"),
    ("numY",       "(portrait ? 0.215 : 0.196) + top + numCap + 0.01",             "its baseline"),
    ("hmsCap",     "min(0.074, 0.10*W/H)",                                         "height of hours:minutes:seconds"),
    ("hmsY",       "numY + 0.053 + hmsCap + 0.05",                                 ""),
    ("releaseDay", "release_done && release_past < 86400",                          "1 during the first 24 hours after release"),
    ("starLit",    "release_done ? 6 : clamp(6 - release_days, 0, 6)",              "POLICE STARS lit (of six): six minus the full days to go. 2 days = 4 stars, the last day = 6. (Change to  clamp(release_days, 0, 6)  for one star per day left)"),
    ("blinkW",     "releaseDay ? 9 : 4.4",                                         "how fast the stars blink once the game is out (fast on launch day, calmer after)"),
    ("starsShown", "release_done ? 6 * (sin(t*blinkW) > 0) : starLit",             "stars drawn filled this moment: the steady count before release, on-off-on-off after it"),
    ("nextStar",   "(release_done == 0 && release_days >= 1 && release_days <= 6) ? 1 : 0", "1 while the next star is slowly filling up"),
    ("starFrac",   "1 - mod(release_left, 86400) / 86400",                        "how far through the current day we are (the next star fills as the day goes by)"),
    ("starsY",     "0.138 + top",                                                  "height of the star row"),
    ("starGap",    "2.7 * 0.0235 * H / W",                                         "distance between two stars (fraction of width)"),
    ("vm",         "view ? smoothstep(0, 0.5, view_t) : 1 - smoothstep(0, 0.5, view_t)", "how much of the launch map view is showing (0 countdown .. 1 map): it fades over half a second"),
    ("cdA",        "1 - vm",                                                       "how much of the countdown view is showing"),
    ("mapOn",      "vm > 0.01",                                                    "1 while the launch map view is (even partly) showing"),
    ("cdOn",       "vm < 0.99",                                                    "1 while the countdown view is (even partly) showing"),
    ("doneN",      W_DONE,                                                         "launch waves already live"),
    ("frTo",       PICK(FR),                                                       "where the lit part of the world starts along the map (0..1, 1 = nothing yet, 0 = the whole map) once the live waves are in"),
    ("frFrom",     PICK([FR[0]] + FR[:-1], "doneN"),                               "the same, one wave earlier (the lit part slides from this to the one above)"),
    ("lastPast",   PICK(["99999"] + ["w%d_past" % i for i in range(1, N + 1)]),    "seconds since the latest wave went live"),
    ("frX",        "frFrom + (frTo - frFrom) * smoothstep(0, 4, lastPast)",       "the edge of the lit part of the world, gliding west for four seconds each time a wave goes live"),
    ("autoW",      "min(14, doneN + 1)",                                           "the next wave to launch (14 once all are out)"),
    ("selW",       "sel > 0 ? sel : autoW",                                        "the wave you pinned, or the next one"),
    ("focusW",     "hov > 0 ? hov : selW",                                         "the wave shown in the panel: the card under the pointer, else the pinned one"),
    ("pulse",      "0.62 + 0.38*sin(t*3.2)",                                       "a slow throb for things that wait for you"),
    # --- the launch map's box (landscape: map in the middle with a column of notes at each side; portrait: map across the window)
    ("mapW",       "portrait ? 0.86 : min(0.70, 0.40*2.9*H/W)",                    "width of the world map (fraction of width). The map file is 2.9 times wider than tall"),
    ("mapH",       "mapW * W / (2.9 * H)",                                         "its height (fraction of height)"),
    ("mapX",       "0.5 - mapW/2",                                                 ""),
    ("mapY",       "portrait ? 0.205 : 0.180",                                     ""),
    ("arrD",       "0.0165 * H / W",                                               "radius of the < > buttons, as a fraction of the width (they are round, so it depends on the window's shape)"),
    ("chipW",      "0.115 * mapW",                                                 "size of the little region labels on the map"),
    ("chipH",      "portrait ? 0.016 : 0.026",                                     ""),
    ("midX",       "mod(0.5 - mod(hour - tzHours, 24) / 24, 1)",                   "where it is midnight right now, along the map (0 = left edge, 1 = right edge): the line that sweeps west"),
    ("youX",       "mod(0.5 + tzHours / 24, 1)",                                   "where YOU are, along the map (your clock's time zone)"),
    # --- the city
    ("cw",         "portrait ? 2.4 : max(1.02, 1.82*H/W)",                         "width of the city pictures (fraction of the window width)"),
    ("ch",         "cw * W / (6 * H)",                                             "and their height (they are 6 times wider than tall)"),
    ("cityB",      "horizon + 0.003",                                              "the ground line of the city"),
    # --- text sizes (fractions of the height, kept small enough for a narrow window)
    ("fxs",        "min(0.0092, 0.0100*W/H)",                                      "smallest text"),
    ("fs",         "min(0.0112, 0.0122*W/H)",                                      "small text"),
    ("fm",         "min(0.0135, 0.0148*W/H)",                                      "medium text"),
    # --- the view pill at the top
    ("pillW",      "portrait ? 0.74 : 0.255",                                      "the COUNTDOWN | LAUNCH MAP pill: width"),
    ("pillX",      "0.5 - pillW/2",                                                ""),
    ("seg0",       "portrait ? 0.29 : 0.105",                                      "width of its left half"),
    ("pillY",      "portrait ? 0.105 : 0.050",                                     ""),
    ("pillH",      "portrait ? 0.032 : 0.043",                                     ""),
    # --- the launch map view: its frame, the panel under it, the 14 region cards
    ("mpY",        "mapY - (portrait ? 0.03 : 0.053)",                             "frame around the map"),
    ("mpH",        "mapH + (portrait ? 0.06 : 0.105)",                             ""),
    ("ipY",        "mpY + mpH + 0.012",                                            "the panel that tells about the picked wave"),
    ("ipH",        "portrait ? 0.105 : 0.118",                                     ""),
    ("cardCols",   "portrait ? 2 : 7",                                             "the wave cards: columns"),
    ("cardGX",     "portrait ? 0.01 : 0.0075",                                     "gap between cards (fraction of width)"),
    ("cardGY",     "portrait ? 0.006 : 0.008",                                     "gap between rows (fraction of height)"),
    ("cardW",      "(0.92 - (cardCols - 1)*cardGX) / cardCols",                    ""),
    ("cardH",      "portrait ? 0.052 : 0.086",                                     ""),
    ("cardX0",     "0.04",                                                         ""),
    ("cardY0",     "ipY + ipH + (portrait ? 0.01 : 0.012)",                        ""),
    ("fdone",      " + ".join("(focusW == %d)*w%d_done" % (i, i) for i in range(1, N + 1)), "1 if the wave in the panel is already live"),
    ("fnext",      "(focusW == autoW) * (1 - fdone)",                              "1 if the wave in the panel is the next one to launch"),
]
for _i in range(1, N + 1):                                                         # where card i sits
    vars_.append(("cx%d" % _i, "cardX0 + mod(%d, cardCols)*(cardW + cardGX)" % (_i - 1), "card %d: left edge" % _i if _i == 1 else ""))
    vars_.append(("cy%d" % _i, "cardY0 + floor(%d / cardCols)*(cardH + cardGY)" % (_i - 1), "and top edge" if _i == 1 else ""))
raw('  "vars": {')
for k, (name, expr, why) in enumerate(vars_):
    comma = "," if k < len(vars_) - 1 else " "
    pad = " " * max(1, 14 - len(name))
    line = '    "%s":%s"%s"%s' % (name, pad, expr, comma)
    if why: line = line.ljust(112) + " // " + why
    raw(line)
raw("  },")
raw("")
raw("  // ============================================================================================================")
raw("  //  extra tabs: this theme adds one to the dock")
raw("  // ============================================================================================================")
raw('  "faces": [')
raw("    {")
raw('      "id": "countdown",')
raw('      "name": "GTA VI",                  // the name on the dock button')
raw('      "fps": 24,')
raw('      "layers": [')

# ---------------------------------------------------------------------------------------------------------------------------------
#  THE BACKDROP: sky, planes, sun, city, sea, boats, palms   (shared by both views; the map view darkens it)
# ---------------------------------------------------------------------------------------------------------------------------------
note("""
==== THE SKY: a gradient, stars, the glow of the sun, and one far-away plane drifting by with a long trail that fades away ====
""")
L("gradient", y0=0, y1="horizon", colors=["#0E0330", "#3A0F73", "#C2307F", "#FF7A45"], at=[0, 0.5, 0.86, 1])
L("stars", count=70, y0=0, y1="horizon*0.8", colors=[INK, TEAL], opacity="light")
L("glow", x=0.5, y="horizon", rx=0.80, ry=0.34, color=PINK, opacity="0.42*light")
L("glow", x=0.5, y="horizon", rx=0.46, ry=0.20, color="#FF7A45", opacity="0.55*light")
L("glow", x=0.5, y="horizon", rx="2.4*sunR*H/W", ry="1.7*sunR", color=GOLD, opacity="0.30*light")
note("""one plane, far away, crawling across the sky with a long trail that melts behind it. 'traffic' with "vehicle": "jet".
y0..y1 is the band of sky it uses, period how long a crossing takes (seconds, so bigger = slower), size how big, trail how long the trail is (seconds),
fade where in the trip it starts to melt away (0.6 = after 60% of the way), active how much of the time one is up. Each trip picks a new height and direction.""")
L("traffic", vehicle="jet", count=1, seed=3, y0="portrait ? 0.265 : 0.19", y1="portrait ? 0.40 : 0.34", period=330, size="portrait ? 0.0095 : 0.0115", trail=70, fade=0.72, active=0.92, color="#FFE9F4", color2="#FFB6D9", opacity=0.95)
L("image", src="art/sun.png", x=0.5, y="horizon", h="2*sunR/0.94", opacity="light")

# ---- the sea (always solid, so the sun's lower half never shows through), the city's reflection and the sun's glitter
note("""
==== THE SEA: a solid gradient, the city reflected in it (the same picture turned upside down, very faint), and the sun's glitter ====
""")
L("gradient", y0="horizon", y1=1, colors=["#6A1F7D", "#14072B"])
L("image", src="art/city_mid.png", x=0.5, y="cityB", anchor="bottom-center", w="cw", rotate=180, opacity="0.14*light")
L("streaks", x=0.5, y0="horizon + 0.008", y1="horizon + 0.70*(1-horizon) + 0.008", w="sunR*H/W", w2="0.5*sunR*H/W", color=GOLD, color2=PINK, opacity="light")

# ---- the city
marks = json.load(open(MARKS))
def mark_xy(layer, m):
    """where a mark of the city picture lands in the window (the pictures are cw wide, ch tall, standing on the ground line)"""
    k = "0.94*" if layer == "far" else ""
    base = "cityB - 0.004" if layer == "far" else "cityB"
    x = "0.5 + %.4f*cw%s" % (m["x"] - 0.5, "*0.94" if layer == "far" else "")
    y = "%s - %.4f*ch%s" % (base, 1 - m["y"], "*0.94" if layer == "far" else "")
    return x, y

def city_marks(layer):
    """aerial lights that blink on the masts, the stadium floodlights, the hotel signs and the lighthouse"""
    ms = marks[layer]
    for j, m in enumerate(ms):
        x, y = mark_xy(layer, m)
        ph = round(1.37 * j, 2)
        if m["kind"] == "beacon":
            blink = "light*pow(max(0, sin(t*%.2f + %.2f)), 5)" % (m["rate"] * 2.6, ph)
            if layer == "far":
                L("glow", x=x, y=y, r=0.0030, color=m["color"], opacity=blink)
            else:
                L("glow", x=x, y=y, r=0.0042, color=m["color"], opacity=blink)
                L("circle", x=x, y=y, r=0.0011, color=m["color"], opacity=blink)
        elif m["kind"] == "neon":
            L("glow", x=x, y=y, r=0.0075, color=m["color"], opacity="light*(0.34 + 0.18*sin(t*%.2f + %.2f) + 0.10*(sin(t*%.2f) > 0.92))" % (1.9 + 0.3 * j, ph, 11.0 + j))
    fl = [m for m in ms if m["kind"] == "flood"]
    if fl:
        fx = sum(m["x"] for m in fl) / len(fl); fy = sum(m["y"] for m in fl) / len(fl)
        x, y = mark_xy(layer, {"x": fx, "y": fy})
        L("glow", x=x, y=y, r=0.015, color=fl[0]["color"], opacity="light*0.34")
    lh = [m for m in ms if m["kind"] == "lighthouse"]
    for m in lh:
        x, y = mark_xy(layer, m)
        L("glow", x=x, y=y, r=0.009, color=m["color"], opacity="light*(0.55 + 0.35*sin(t*2.6))")
        # the beam swings out over the sea and back: it only shows while it points away from you
        L("glow", x="(%s) + 0.075*cos(t*0.9)" % x, y="(%s) + 0.004" % y, rx=0.085, ry=0.0042, color=m["color"], opacity="light*0.55*max(0, sin(t*0.9))")

note("""
==== THE CITY: three layers of Art Deco towers (far and hazy, middle, near and dark), with red aerial lights that blink, floodlights, glowing hotel signs,
a Ferris wheel that turns, and a lighthouse whose beam sweeps the sea. The pictures are art/city_far.png, city_mid.png and city_near.png (see _source/make_city.py) ====
""")
L("image", src="art/city_far.png", x=0.5, y="cityB - 0.004", anchor="bottom-center", w="cw*0.94", opacity="0.92*light")
city_marks("far")
L("image", src="art/city_mid.png", x=0.5, y="cityB", anchor="bottom-center", w="cw")
city_marks("mid")
note("the Ferris wheel: a picture turned slowly round its middle. x, y are the middle of it; w is its width.")
L("image", src="art/wheel.png", x="0.5 + 0.27*cw", y="cityB - 0.344*ch", anchor="center", w="0.0872*cw", rotate="t*3.5", opacity="light")
L("image", src="art/city_near.png", x=0.5, y="cityB", anchor="bottom-center", w="cw")
city_marks("near")

# ---- boats, the police helicopter, the banner plane
note("""
==== THE WATER: boats cruising along the sea, each with a wake and a faint reflection. 'count' boats, each trip picks its own lane (y0..y1), direction and speed ====
""")
L("traffic", vehicle="boat", count=7, seed=2, y0="horizon + 0.04", y1=0.955, period=95, size="portrait ? 0.075 : 0.105", color="#150629", color2="#FFD9B0", color3=PINK, opacity="light")

note("""
==== THE POLICE HELICOPTER: just one, always on patrol low over the city roofs, its searchlight sweeping the streets (never the sea). 'fly' is the height its light lands on ====
""")
L("traffic", vehicle="heli", count=1, seed=5, y0="horizon - 0.125", y1="horizon - 0.075", period=80, size="portrait ? 0.045 : 0.056", fly="horizon - 0.022", color="#12052A", color2="#FFF3D6", color3="#FF2B3D")

note("""
==== A PLANE DRAGS A BANNER now and then, with a message that depends on how close the date is. It is drawn BEFORE the palm trees and the text, so it flies BEHIND the big number and the clock digits.
Every other trip it takes the other lane: one time behind the big number, the next time behind hours:minutes:seconds.
"plane" is the picture art/plane.png (nose pointing right, tail at its left edge); "y" is the height of the lane (the middle of the banner), "planeH" its size,
"period" / "fly" how often it comes and how many seconds a trip takes. ====
""")
BAN = [
    ("doneN > 0 && release_done == 0", ["{doneN} OF 14 REGIONS ARE ALREADY PLAYING", "SOMEWHERE IT'S ALREADY THURSDAY", "NEW ZEALAND GOES FIRST. NO SPOILERS.", "THE ROLLOUT HAS STARTED"]),
    ("releaseDay",         ["IT'S HERE. GO. NOW.", "WELCOME BACK TO VICE CITY", "OCEAN VIEW CHECK-IN IS OPEN", "ALL YOU HAD TO DO WAS WAIT FOR LAUNCH", "SNACKS. CONTROLLER. GO.", "MISSION PASSED. RESPECT +", "FLASH FM IS TAKING REQUESTS"]),
    ("release_done",       ["ENJOY THE TRIP", "ANOTHER DAY IN VICE CITY", "GREETINGS FROM OCEAN DRIVE", "FROM LIBERTY CITY TO LEONIDA", "TAKE A BREAK. DRINK WATER.", "STILL SIX STARS? SAME."]),
    ("release_daysf > 60", ["PLENTY OF TIME TO CLEAR THE BACKLOG", "TUNED TO V-ROCK ON OCEAN BEACH", "THE HYPE IS REAL. SO IS THE WAIT.", "FREE UP SOME STORAGE", "GOOD THINGS TAKE A WHILE"]),
    ("release_daysf > 30", ["LESS THAN TWO MONTHS", "GROVE STREET TO VICE BEACH SOON", "KEEP THE CONTROLLER CHARGED", "PORTLAND TO CHILIAD TO LEONIDA", "THE COUNTDOWN IS ON"]),
    ("release_daysf > 7",  ["CLEAR YOUR CALENDAR", "WEEKS, NOT MONTHS", "STATUE OF HAPPINESS APPROVES", "ALL YOU HAD TO DO WAS FOLLOW THE HYPE", "DON'T BELIEVE THE SPOILERS", "STOCK UP ON SNACKS"]),
    ("release_daysf > 1",  ["THIS WEEK. FOR REAL.", "CORTEZ YACHT LEAVES THIS WEEK", "CHARGE EVERYTHING", "LAST FLIGHT FROM LOS SANTOS", "SLEEP NOW, PLAY LATER", "NOT LONG NOW"]),
    (None,                 ["TOMORROW. SERIOUSLY.", "PACK YOUR BAGS FOR OCEAN BEACH", "ONE MORE SLEEP", "LAST NIGHT BEFORE VICE CITY", "TRY TO SLEEP TONIGHT"]),
]
cases = [({"when": w, "text": t} if w else {"text": t}) for w, t in BAN]
L("banner", y="mod(floor(t/44), 2) < 1 ? numY - numCap*0.52 : hmsY - hmsCap*0.5", h="portrait ? 0.04 : 0.05", size="portrait ? 0.021 : 0.026", plane="art/plane.png", planeH="portrait ? 0.062 : 0.085",
  period=44, fly=24, fill="#1A0A40EB", edge="#FF5CA8", ink="#FFF3E6", opacity="cdA", text=cases)

note("""
==== palm trees in front, swaying ====
""")
L("image", src="art/palm_a.png", x=0.055, y=1.015, anchor="bottom-center", h="0.59*pf", rotate="1.3*sin(t*0.42) + 0.5*sin(t*1.1)", opacity="light")
L("image", src="art/palm_b.png", x=0.145, y=1.015, anchor="bottom-center", h="0.40*pf", rotate="1.3*sin(t*0.49 + 1.9) + 0.5*sin(t*1.2 + 0.7)", opacity="light")
L("image", src="art/palm_c.png", x=0.945, y=1.015, anchor="bottom-center", h="0.555*pf", rotate="1.3*sin(t*0.56 + 3.8) + 0.5*sin(t*1.3 + 1.4)", opacity="light")
L("image", src="art/palm_d.png", x=0.855, y=1.015, anchor="bottom-center", h="0.366*pf", rotate="1.3*sin(t*0.63 + 5.7) + 0.5*sin(t*1.4 + 2.1)", opacity="light")

note("""
==== release day: police lights at the edges, and confetti ====
""")
L("glow", x=0, y=0.5, rx=0.30, ry=0.7, color="#FF2B3D", opacity="0.34*(sin(t*7) > 0)*releaseDay")
L("glow", x=1, y=0.5, rx=0.30, ry=0.7, color="#2B7BFF", opacity="0.34*(sin(t*7) <= 0)*releaseDay")
L("confetti", when="releaseDay", count=60, colors=[PINK, TEAL, GOLD, "#7B3FE4"])
note("weather: this pack always looks the same (gloom and haze stay 0 unless the file says \"weather\": true), so this layer draws nothing unless you switch that on")
L("shade", alpha="0.46*gloom + 0.22*haze", alpha2="0.30*haze")
L("gradient", y0=0.90, y1=1, colors=["#0D031F00", "#0D031F9E"])
note("""
==== the launch map view darkens everything behind it (it fades in and out with the pill) ====
""")
L("rect", when="mapOn", x=0, y=0, w=1, h=1, color="#0E0426", opacity="vm*0.86")

# ---------------------------------------------------------------------------------------------------------------------------------
#  THE TEXT AND THE TWO VIEWS
# ---------------------------------------------------------------------------------------------------------------------------------
UNREL = "#B79CFF"                      # a region that is not live yet
SHORT = ["NEW ZEALAND", "AUSTRALIA", "JAPAN", "SINGAPORE", "THAILAND", "INDIA", "UAE", "SAUDI ARABIA", "EASTERN EUROPE", "CENTRAL EUROPE", "UK & PORTUGAL", "BRAZIL", "USA & CANADA", "MEXICO"]

def T(text, x, y, size="fs", **kw):
    kw.setdefault("font", "monoMedium")
    return L("text", text=text, x=x, y=y, size=size, **kw)

def per_wave(fn, key="focusW"):
    """one text with a case for every wave: fn(i, wave) gives the words, or a list of (extra condition, words)"""
    cs = []
    for i, w in enumerate(WAVES, 1):
        t = fn(i, w)
        for cond, txt in (t if isinstance(t, list) else [(None, t)]):
            cs.append({"when": "%s == %d%s" % (key, i, (" && " + cond) if cond else ""), "text": txt})
    return cs

note("""
==== THE TOP ROW (both views): the title, the time, and the pill that switches views ====
""")
T("GRAND THEFT AUTO VI", 0.05, 0.075, "min(0.026, 0.028*W/H)", align="left", color=TEAL, tracking=0.32, maxw="portrait ? 0.5 : pillX - 0.06", opacity=0.95)
T("NOW  {time}", 0.95, 0.075, "min(0.026, 0.028*W/H)", align="right", color=INK, tracking=0.20, opacity=0.8)
note("""the view pill: two halves (the lit half is the view you are in). Click a half, or press Tab / Space to switch. 'button' is an invisible click area:
"set" is the number it changes (see "state" above), "to" what it is changed to, "key" a key that does the same.""")
L("rect", x="pillX", y="pillY", w="pillW", h="pillH", radius="pillH/2", color=PLATE + "/0.62")
L("rect", x="pillX + 0.0025", y="pillY + 0.004", w="seg0 - 0.0025", h="pillH - 0.008", radius="(pillH - 0.008)/2", color=PINK, opacity="cdA")
L("rect", x="pillX + seg0", y="pillY + 0.004", w="pillW - seg0 - 0.0025", h="pillH - 0.008", radius="(pillH - 0.008)/2", color=PINK, opacity="vm")
L("rect", x="pillX", y="pillY", w="pillW", h="pillH", radius="pillH/2", thick=0.0012, color=PINK + "/0.85")
for dark, op0, op1 in ((True, "cdA", "vm"), (False, "vm", "cdA")):
    col = "#12052A" if dark else INK
    T("COUNTDOWN", "pillX + seg0/2", "pillY + pillH/2 + fs*0.36", "fs", color=col, tracking=0.16, maxw="seg0*0.9", opacity=op0)
    T("LAUNCH MAP  ·  {utc}", "pillX + seg0 + (pillW - seg0)/2", "pillY + pillH/2 + fs*0.36", "fs", color=col, tracking=0.16, maxw="(pillW - seg0)*0.92", opacity=op1)
L("button", x="pillX", y="pillY", w="seg0", h="pillH", radius="pillH/2", set="view", to=0, color="#FFFFFF/0.07")
L("button", x="pillX + seg0", y="pillY", w="pillW - seg0", h="pillH", radius="pillH/2", set="view", to=1, color="#FFFFFF/0.07")
L("button", x=0, y=0, w=0, h=0, set="view", to="1 - view", key="tab")
L("button", x=0, y=0, w=0, h=0, set="view", to="1 - view", key="space")

# =================================================================================================================================
note("""
==== VIEW 1: THE COUNTDOWN ====
Everything in this view has  "when": "cdOn"  (only while this view is showing) and fades with  cdA  (the fade between the two views).
""")
note("""the police stars (six, as in GTA VI): the number lit is "starsShown": six minus the days to go (2 days = 4 stars), all six on the last day.
The NEXT star glows a little more every hour of the day, then lights fully at midnight.
Once the game is out all six blink on and off together, red and blue like the lights of a police car, fast on launch day and calmer after.""")
L("shapes", when="cdOn && releaseDay == 0", shape="star", count=6, x=0.5, y="starsY", size=0.0235, lit="starsShown", color=PINK, opacity="cdA")
L("shapes", when="cdOn && releaseDay", shape="star", count=6, x=0.5, y="starsY", size=0.0235, lit="starsShown", color="#FF2B3D", color2="#2B7BFF", flash=2.4, opacity="cdA")
L("shapes", when="cdOn && nextStar", shape="star", count=1, x="0.5 + (starLit - 2.5)*starGap", y="starsY", size=0.0235, lit=1, color=PINK, glow=0.2, opacity="cdA*0.8*starFrac")
T([{"when": "release_done", "text": "MAXIMUM WANTED LEVEL"},
   {"when": "release_days > 6", "text": "SIX STARS  ·  ONE MORE LIGHTS UP EACH DAY FROM SIX DAYS OUT"},
   {"when": "release_days == 0", "text": "ALL SIX STARS  ·  JUST HOURS LEFT"},
   {"text": "WANTED LEVEL {starLit} OF 6  ·  ONE MORE STAR EACH DAY"}],
  0.5, "starsY + 0.044", "fs", when="cdOn", color=INK, tracking=0.26, maxw=0.8, opacity="cdA*0.85", upper=True)

note("the big number: days to go (or since launch), then a label, then hours:minutes:seconds")
L("text", when="cdOn", digits=True, font="display", cap="numCap", x=0.5, y="numY", maxw=0.66, color=GOLD, color2=PINK, tracking=0.02, neon=PINK, neonStrength="cdA", opacity="cdA",
  text=[{"when": "releaseDay", "text": "NOW"}, {"text": "{release_days:2}"}])
T([{"when": "releaseDay", "text": "OUT NOW"}, {"when": "release_done && release_days == 1", "text": "DAY SINCE LAUNCH"}, {"when": "release_done", "text": "DAYS SINCE LAUNCH"},
   {"when": "release_days == 1", "text": "DAY TO GO"}, {"text": "DAYS TO GO"}],
  0.5, "numY + 0.053", 0.028, when="cdOn", tracking=0.42, color=INK, opacity="cdA*0.92", upper=True)
L("text", when="cdOn", digits=True, font="display", cap="hmsCap", x=0.5, y="hmsY", color=INK, color2="#FFC4DC", gap=0.62, tracking=0.035, maxw=0.80, neon=TEAL, neonStrength="0.9*cdA", opacity="cdA",
  text="{release_hrs:2}:{release_min:2}:{release_sec:2}")
T([{"when": "release_done", "text": "SINCE LAUNCH"}, {"text": "HOURS  ·  MINUTES  ·  SECONDS"}], 0.5, "hmsY + 0.039", 0.017, when="cdOn", maxw=0.9, tracking=0.34, color=INK + "/0.62", opacity="cdA")
T("{release_when}  ·  PS5  ·  XBOX SERIES X|S", 0.5, 0.965, 0.0215, when="cdOn", upper=True, maxw=0.94, tracking=0.22, color=INK, opacity="cdA*0.92")

note("""
the world rollout pill (top left): how many regions are live, and who is next. Click it to open the map.
""")
T([{"when": "doneN == 14", "text": "WORLD ROLLOUT COMPLETE  ·  14 / 14 LIVE"},
   {"when": "doneN == 0", "text": "WORLD ROLLOUT  ·  ALL 14 UNRELEASED  ·  STARTS {w1_dow} {w1_short} {w1_clock}"}] +
  per_wave(lambda i, w: "WORLD ROLLOUT  ·  {doneN} / 14 LIVE  ·  NEXT  %s  {w%d_dow} {w%d_clock}" % (SHORT[i - 1], i, i), "autoW"),
  0.062, 0.1165, "fs", when="cdOn && !portrait", align="left", color=PINK, tracking=0.12, maxw="min(0.30, pillX - 0.098)", opacity="cdA", upper=True)
L("rect", when="cdOn && !portrait", x=0.05, y=0.098, w="min(0.33, pillX - 0.058)", h=0.027, radius=0.0135, thick=0.0011, color=PINK + "/0.7", opacity="cdA")
L("button", when="cdOn && !portrait", x=0.05, y=0.098, w="min(0.33, pillX - 0.058)", h=0.027, radius=0.0135, set="view", to=1, color="#FFFFFF/0.06", color2=PINK)

# ---- the two little cards: the next wave (left) and the mini map (right). Not shown in a tall window.
CY, CHT, CW_ = 0.215, 0.200, 0.215
note("""the NEXT WAVE card (left): which region goes live next, when (on your clock) and how long to wait, and a bar of how many regions are already live.""")
L("rect", when="cdOn && !portrait", x=0.05, y=CY, w=CW_, h=CHT, radius=0.008, color=PLATE + "/0.50", opacity="cdA")
L("rect", when="cdOn && !portrait", x=0.05, y=CY, w=CW_, h=CHT, radius=0.008, thick=0.001, color=TEAL + "/0.55", opacity="cdA")
T("NEXT WAVE", "0.05 + 0.012", CY + 0.027, "fs", when="cdOn && !portrait", align="left", color=TEAL, tracking=0.3, opacity="cdA")
T([{"when": "doneN == 14", "text": "ALL 14 LIVE"}, {"text": "WAVE {autoW} OF 14"}], "0.05 + 0.215 - 0.012", CY + 0.027, "fs", when="cdOn && !portrait", align="right", color=INK, tracking=0.2, opacity="cdA")
L("text", when="cdOn && !portrait", font="display", x="0.05 + 0.012", y=CY + 0.083, size=0.03, align="left", color=GOLD, maxw=0.19, opacity="cdA",
  text=[{"when": "doneN == 14", "text": "EVERY REGION"}] + per_wave(lambda i, w: SHORT[i - 1], "autoW"))
T([{"when": "doneN == 14", "text": "THE WHOLE WORLD IS PLAYING"}] + per_wave(lambda i, w: "{w%d_dow} {w%d_short}  ·  {w%d_clock}" % (i, i, i), "autoW"),
  "0.05 + 0.012", CY + 0.117, "fm", when="cdOn && !portrait", align="left", color=INK, tracking=0.1, maxw=0.19, opacity="cdA", upper=True)
T([{"when": "doneN == 14", "text": ""}] + per_wave(lambda i, w: [("w%d_days > 0" % i, "IN {w%d_days}D {w%d_hrs:2}H {w%d_min:2}M" % (i, i, i)), (None, "IN {w%d_hrs:2}H {w%d_min:2}M {w%d_sec:2}S" % (i, i, i))], "autoW"),
  "0.05 + 0.012", CY + 0.151, "fm", when="cdOn && !portrait", align="left", color=TEAL, tracking=0.12, maxw=0.19, opacity="cdA")
L("rect", when="cdOn && !portrait", x="0.05 + 0.012", y=CY + 0.172, w=0.191, h=0.006, radius=0.003, color="#FFFFFF/0.14", opacity="cdA")
L("rect", when="cdOn && !portrait && doneN > 0", x="0.05 + 0.012", y=CY + 0.172, w="0.191*doneN/14", h=0.006, radius=0.003, color=PINK, opacity="cdA")
L("button", when="cdOn && !portrait", x=0.05, y=CY, w=CW_, h=CHT, radius=0.008, set="view", to=1, color="#FFFFFF/0.05", color2=TEAL)

MX0 = 0.735
note("""the MINI MAP card (right): the same world map, small. Regions that are live are lit up, and the next one pulses gold. Click it to open the big map.""")
L("rect", when="cdOn && !portrait", x=MX0, y=CY, w=CW_, h=CHT, radius=0.008, color=PLATE + "/0.50", opacity="cdA")
L("rect", when="cdOn && !portrait", x=MX0, y=CY, w=CW_, h=CHT, radius=0.008, thick=0.001, color=PINK + "/0.55", opacity="cdA")
T("LAUNCH MAP", MX0 + 0.012, CY + 0.027, "fs", when="cdOn && !portrait", align="left", color=TEAL, tracking=0.3, opacity="cdA")
T("{doneN} / 14 LIVE", MX0 + CW_ - 0.012, CY + 0.027, "fs", when="cdOn && !portrait", align="right", color=INK, tracking=0.2, opacity="cdA")
def mini(**kw):
    kw.setdefault("when", "cdOn && !portrait")
    return L("map", src="art/world.json", x=MX0 + 0.012, y=CY + 0.043, w=0.191, **kw)
mini(countries=["_bands"], color="#FFFFFF/0.04", opacity="cdA")
mini(countries=["*"], color="#3A2370/0.95", color2=VIOLET + "/0.5", thick=0.0007, opacity="cdA")
note("the lit zone: the part of the world where the game is already live (a soft pink sheen over the sea as well, from the east to the edge of the newest region)")
L("rect", when="cdOn && !portrait && doneN > 0", x="%.4f + frX*0.191" % (MX0 + 0.012), y=CY + 0.043, w="(1 - frX)*0.191", h="0.191*W/(2.9*H)", color=PINK + "/0.11", opacity="cdA")
for i, w in enumerate(WAVES, 1):
    mini(countries=FULL[i - 1], color=PINK + "/0.75", color2="#FF8CC0", thick=0.0008, glow=0.006, opacity="cdA*w%d_done" % i, when="cdOn && !portrait && w%d_done" % i)
for i, w in enumerate(WAVES, 1):
    mini(countries=FULL[i - 1], color=GOLD + "/0.38", color2=GOLD, thick=0.0010, glow=0.006, opacity="cdA*pulse", when="cdOn && !portrait && autoW == %d && w%d_done == 0" % (i, i))
T([{"when": "doneN == 14", "text": "ALL 14 REGIONS ARE LIVE"}] + per_wave(lambda i, w: "NEXT  ·  %s  ·  {w%d_dow} {w%d_clock}" % (SHORT[i - 1], i, i), "autoW"),
  MX0 + CW_/2, CY + CHT - 0.016, "fs", when="cdOn && !portrait", color=INK, tracking=0.12, maxw=0.19, opacity="cdA*0.9", upper=True)
L("button", when="cdOn && !portrait", x=MX0, y=CY, w=CW_, h=CHT, radius=0.008, set="view", to=1, color="#FFFFFF/0.05", color2=PINK)

# =================================================================================================================================
note("""
==== VIEW 2: THE LAUNCH MAP ====
Everything in this view has  "when": "mapOn"  and fades with  vm  (the fade between the two views).
""")
M = "mapOn"
L("rect", when=M, x=0.04, y="mpY", w=0.92, h="mpH", radius=0.012, color="#1A0B3D/0.66", opacity="vm")
L("rect", when=M, x=0.04, y="mpY", w=0.92, h="mpH", radius=0.012, thick=0.001, color=VIOLET + "/0.45", opacity="vm")
note("""the world: the time-zone stripes, the lat/long grid, every country, and then (on top) the regions of each wave:
  pink  = the game is live there       gold, pulsing = the next wave to launch       cyan = the wave shown in the panel below""")
def wmap(**kw):
    kw.setdefault("when", M)
    return L("map", src="art/world.json", x="mapX", y="mapY", w="mapW", **kw)
wmap(countries=["_bands"], color="#FFFFFF/0.035", opacity="vm")
wmap(countries=["_grid"], color2=VIOLET + "/0.26", thick=0.0007, opacity="vm")
wmap(countries=["*"], color="#3A2370/0.95", color2=VIOLET + "/0.55", thick=0.0009, opacity="vm")
note("""the lit zone: everything east of the newest live region glows pink, the sea included, and the edge glides west each time a wave goes live.
When the last wave is out the whole map is lit and the frame turns pink.""")
L("rect", when="%s && doneN > 0" % M, x="mapX + frX*mapW", y="mapY", w="(1 - frX)*mapW", h="mapH", color=PINK + "/0.10", opacity="vm*(1 + 0.35*(doneN == 14)*pulse)")
L("glow", when="%s && doneN > 0 && doneN < 14" % M, x="mapX + frX*mapW", y="mapY + mapH/2", rx=0.007, ry="mapH*0.52", color=PINK, opacity="vm*0.30")
L("rect", when="%s && doneN == 14" % M, x=0.04, y="mpY", w=0.92, h="mpH", radius=0.012, thick=0.002, color=PINK, opacity="vm*(0.55 + 0.45*pulse)")
note("then each wave's countries: pink = live, gold = next, cyan = the one in the panel. (A wave lights ALL the countries that share its time, not only the ones its card names.)")
for i, w in enumerate(WAVES, 1):
    wmap(countries=FULL[i - 1], color=PINK + "/0.62", color2="#FF8CC0", thick=0.0012, glow=0.010, opacity="vm*w%d_done" % i, when="%s && w%d_done" % (M, i))
for i, w in enumerate(WAVES, 1):
    wmap(countries=FULL[i - 1], color=GOLD + "/0.30", color2=GOLD, thick=0.0014, glow=0.009, opacity="vm*pulse", when="%s && autoW == %d && w%d_done == 0" % (M, i, i))
for i, w in enumerate(WAVES, 1):
    wmap(countries=FULL[i - 1], color=TEAL + "/0.26", color2=TEAL, thick=0.0018, glow=0.012, opacity="vm*(0.72 + 0.28*sin(t*4))", when="%s && focusW == %d" % (M, i))

note("""the midnight line: where it is midnight right now. Every launch happens at midnight local time, so the line sweeps west, 15 degrees an hour, and the game goes live in each region as it passes.""")
L("glow", when=M, x="mapX + midX*mapW", y="mapY + mapH/2", rx=0.004, ry="mapH*0.56", color=GOLD, opacity="vm*0.5")
L("line", when=M, x="mapX + midX*mapW", y="mapY", x2="mapX + midX*mapW", y2="mapY + mapH", color=GOLD + "/0.92", thick=0.0016, opacity="vm")
T("LOCAL MIDNIGHT", "mapX + midX*mapW + 0.005", "mapY + 0.06*mapH", "fxs", when="%s && midX < 0.84" % M, align="left", color=GOLD, tracking=0.2, opacity="vm*0.95")
T("LOCAL MIDNIGHT", "mapX + midX*mapW - 0.005", "mapY + 0.06*mapH", "fxs", when="%s && midX >= 0.84" % M, align="right", color=GOLD, tracking=0.2, opacity="vm*0.95")
note("you: a little teal tick where your own time zone is, and the time-zone numbers along the bottom edge")
L("line", when=M, x="mapX + youX*mapW", y="mapY + 0.945*mapH", x2="mapX + youX*mapW", y2="mapY + mapH", color=TEAL, thick=0.0016, opacity="vm")
T("YOU  ·  {utc}", "mapX + youX*mapW", "mapY + 0.925*mapH", "fxs", when=M, color=TEAL, tracking=0.16, opacity="vm")
for h in (-9, -6, -3, 0, 3, 6, 9, 12):
    T("UTC" if h == 0 else "UTC%+d" % h, "mapX + %.4f*mapW" % ((h * 15 + 180) / 360.0), "mapY + 0.985*mapH", "fxs", when=M, color=INK, tracking=0.1, opacity="vm*0.5")

note("""the 14 region labels: a pin on the map, a thin line, and a little label with the region's code and the time it goes live on YOUR clock.
They light up pink when that region is live, pulse gold when it is next, and glow cyan when it is the one you picked.""")
for i, w in enumerate(WAVES, 1):
    code, name, ids, cities, tz, off, lon, lat, (cx, cy), side = w
    ux, uy = unit(lon, lat)
    px, py = "mapX + %.4f*mapW" % ux, "mapY + %.4f*mapH" % uy
    ccx, ccy = "mapX + %.4f*mapW" % cx, "mapY + %.4f*mapH" % cy
    ax, ay = ccx, ("%s %s chipH/2" % (ccy, "+" if side == "b" else "-"))
    done, nxt, foc = "w%d_done" % i, "(autoW == %d)*(1 - w%d_done)" % (i, i), "(focusW == %d)" % i
    L("line", when=M, x=ax, y=ay, x2=px, y2=py, color=VIOLET + "/0.50", thick=0.0009, opacity="vm")
    L("line", when="%s && w%d_done" % (M, i), x=ax, y=ay, x2=px, y2=py, color=PINK + "/0.9", thick=0.0011, opacity="vm")
    L("line", when="%s && focusW == %d" % (M, i), x=ax, y=ay, x2=px, y2=py, color=TEAL, thick=0.0015, opacity="vm")
    L("circle", when=M, x=px, y=py, r=0.0040, color=UNREL, opacity="vm")
    L("circle", when="%s && w%d_done" % (M, i), x=px, y=py, r=0.0052, color=PINK, opacity="vm")
    L("circle", when="%s && autoW == %d && w%d_done == 0" % (M, i, i), x=px, y=py, r="0.007 + 0.007*fract(t*0.8)", thick=0.0011, color=GOLD, opacity="vm*(1 - fract(t*0.8))")
    L("circle", when="%s && w%d_done && w%d_past < 5" % (M, i, i), x=px, y=py, r="0.006 + 0.05*(w%d_past/5)" % i, thick=0.0016, color=PINK, opacity="vm*(1 - w%d_past/5)" % i)
    L("rect", when=M, x="%s - chipW/2" % ccx, y="%s - chipH/2" % ccy, w="chipW", h="chipH", radius="chipH/2", color=PLATE + "/0.92", opacity="vm")
    L("rect", when=M, x="%s - chipW/2" % ccx, y="%s - chipH/2" % ccy, w="chipW", h="chipH", radius="chipH/2", thick=0.0010, color=VIOLET + "/0.75", opacity="vm")
    L("rect", when="%s && w%d_done" % (M, i), x="%s - chipW/2" % ccx, y="%s - chipH/2" % ccy, w="chipW", h="chipH", radius="chipH/2", thick=0.0012, color=PINK, opacity="vm")
    L("rect", when="%s && autoW == %d && w%d_done == 0" % (M, i, i), x="%s - chipW/2" % ccx, y="%s - chipH/2" % ccy, w="chipW", h="chipH", radius="chipH/2", thick=0.0012, color=GOLD, opacity="vm*pulse")
    L("rect", when="%s && focusW == %d" % (M, i), x="%s - chipW/2" % ccx, y="%s - chipH/2" % ccy, w="chipW", h="chipH", radius="chipH/2", thick=0.0018, color=TEAL, opacity="vm")
    T("%s  ·  {w%d_clock}" % (code, i), ccx, "%s + fs*0.36" % ccy, "fs", color=INK, tracking=0.06, maxw="chipW*0.92", opacity="vm", when=M)

note("""
the panel about the wave you picked (or are pointing at). Hover a card below to look at a wave, click it to pin it (click again to let go); Up / Down keys or the < > buttons step through the waves.
""")
L("rect", when=M, x=0.04, y="ipY", w=0.92, h="ipH", radius=0.010, color=PLATE + "/0.62", opacity="vm")
L("rect", when=M, x=0.04, y="ipY", w=0.92, h="ipH", radius=0.010, thick=0.0012, color=TEAL + "/0.55", opacity="vm")
L("rect", when=M, x=0.04, y="ipY", w=0.0035, h="ipH", radius=0.002, color=TEAL, opacity="vm")
T(per_wave(lambda i, w: "WAVE %d OF 14  ·  %s  ·  %s" % (i, w[4], utc_text(w[5]))), 0.058, "ipY + (portrait ? 0.024 : 0.031)", "fs", when=M, align="left", color=TEAL, tracking=0.2, maxw="portrait ? 0.62 : 0.52", opacity="vm")
L("text", when=M, font="display", x=0.058, y="ipY + (portrait ? 0.056 : 0.076)", size="portrait ? 0.022 : 0.034", align="left", color=INK, maxw="portrait ? 0.62 : 0.52", opacity="vm", text=per_wave(lambda i, w: w[1]))
T(per_wave(lambda i, w: w[3]), 0.058, "ipY + (portrait ? 0.079 : 0.104)", "fm", when=M, align="left", color="#FFC4DC", tracking=0.06, maxw="portrait ? 0.62 : 0.52", opacity="vm")
T(per_wave(lambda i, w: "{w%d_dow} {w%d_short}  ·  {w%d_clock}  ·  IN {w%d_days}D {w%d_hrs:2}H" % (i, i, i, i, i)), 0.058, "ipY + 0.098", "fs", when="%s && portrait && fdone == 0" % M, align="left", color=GOLD, tracking=0.1, maxw=0.62, opacity="vm", upper=True)
T(per_wave(lambda i, w: "{w%d_dow} {w%d_short}  ·  {w%d_clock}  ·  LIVE FOR {w%d_hrs}H" % (i, i, i, i)), 0.058, "ipY + 0.098", "fs", when="%s && portrait && fdone" % M, align="left", color=PINK, tracking=0.1, maxw=0.62, opacity="vm", upper=True)
# the right side (wide window)
LANDS = "%s && !portrait" % M
T(per_wave(lambda i, w: [("w%d_days > 0" % i, "IN {w%d_days}D {w%d_hrs:2}H {w%d_min:2}M" % (i, i, i)), (None, "IN {w%d_hrs:2}H {w%d_min:2}M {w%d_sec:2}S" % (i, i, i))]),
  0.85, "ipY + 0.031", "fm", when="%s && fdone == 0" % LANDS, align="right", color=UNREL, tracking=0.14, maxw=0.28, opacity="vm")
T(per_wave(lambda i, w: [("w%d_days > 0" % i, "LIVE FOR {w%d_days}D {w%d_hrs}H" % (i, i)), (None, "LIVE FOR {w%d_hrs}H {w%d_min:2}M" % (i, i))]),
  0.85, "ipY + 0.031", "fm", when="%s && fdone" % LANDS, align="right", color=PINK, tracking=0.14, maxw=0.28, opacity="vm")
L("text", when=LANDS, font="display", x=0.85, y="ipY + 0.076", size=0.030, align="right", color=GOLD, maxw=0.30, opacity="vm", upper=True,
  text=per_wave(lambda i, w: "{w%d_dow} {w%d_short}  ·  {w%d_clock}" % (i, i, i)))
T("Region local:  Thu 19 Nov  ·  12:00 AM", 0.85, "ipY + 0.104", "fm", when=LANDS, align="right", color="#FFC4DC", tracking=0.06, maxw=0.30, opacity="vm")
# status tag above the arrows
for cond, label, colr in (("fnext", "NEXT WAVE", GOLD), ("fdone", "LIVE", PINK), ("(1 - fnext)*(1 - fdone)", "UNRELEASED", UNREL)):
    L("rect", when="%s && %s" % (LANDS, cond), x="0.945 - 2.7*arrD - 0.03325", y="ipY + 0.012", w=0.0665, h=0.022, radius=0.011, thick=0.0011, color=colr, opacity="vm")
    T(label, "0.945 - 2.7*arrD", "ipY + 0.0275", "fxs", when="%s && %s" % (LANDS, cond), color=colr, tracking=0.16, opacity="vm")
note("the < and > buttons (and the Up / Down keys) step through the waves")
for dx, dirn, nm, key in (("0.945 - 4.1*arrD", -1, "clamp(selW - 1, 1, 14)", "up"), ("0.945 - 1.3*arrD", 1, "clamp(selW + 1, 1, 14)", "down")):
    cxa, cya = dx, "ipY + 0.0745"
    L("circle", when=M, x=cxa, y=cya, r=0.0165, thick=0.0012, color=TEAL, opacity="vm*0.8")
    L("line", when=M, x="(%s) + %d*0.0045*H/W" % (cxa, dirn), y=cya, x2="(%s) - %d*0.0045*H/W" % (cxa, dirn), y2="(%s) - 0.0075" % cya, color=TEAL, thick=0.0014, opacity="vm")
    L("line", when=M, x="(%s) + %d*0.0045*H/W" % (cxa, dirn), y=cya, x2="(%s) - %d*0.0045*H/W" % (cxa, dirn), y2="(%s) + 0.0075" % cya, color=TEAL, thick=0.0014, opacity="vm")
    L("button", when=M, x="(%s) - arrD" % cxa, y="(%s) - 0.0165" % cya, w="2*arrD", h=0.033, radius=0.0165, set="sel", to=nm, key=key, color=TEAL + "/0.18")

note("""
the 14 region cards: the order of the rollout. Outline colours: violet = not yet, pink = live, gold = next, cyan = the one in the panel.
Hover one to look at it, click to pin it, click it again to follow the next wave again.
""")
for i, w in enumerate(WAVES, 1):
    cx_, cy_ = "cx%d" % i, "cy%d" % i
    done, nxtc, focc = "w%d_done" % i, "autoW == %d && w%d_done == 0" % (i, i), "focusW == %d" % i
    L("rect", when=M, x=cx_, y=cy_, w="cardW", h="cardH", radius=0.007, color=PLATE + "/0.70", opacity="vm")
    L("rect", when="%s && %s" % (M, done), x=cx_, y=cy_, w="cardW", h="cardH", radius=0.007, color=PINK + "/0.16", opacity="vm")
    L("rect", when=M, x=cx_, y=cy_, w="cardW", h="cardH", radius=0.007, thick=0.001, color=VIOLET + "/0.55", opacity="vm")
    L("rect", when="%s && %s" % (M, done), x=cx_, y=cy_, w="cardW", h="cardH", radius=0.007, thick=0.0013, color=PINK, opacity="vm")
    L("rect", when="%s && %s" % (M, nxtc), x=cx_, y=cy_, w="cardW", h="cardH", radius=0.007, thick=0.0013, color=GOLD, opacity="vm*pulse")
    L("rect", when="%s && %s" % (M, focc), x=cx_, y=cy_, w="cardW", h="cardH", radius=0.007, thick=0.002, color=TEAL, opacity="vm")
    T("%d  ·  %s" % (i, w[0]), "%s + 0.009" % cx_, "%s + (portrait ? 0.032 : 0.027)" % cy_, "fm", when=M, align="left", color=INK, tracking=0.1, maxw="cardW*0.6", opacity="vm")
    T("LIVE", "%s + cardW - 0.008" % cx_, "%s + 0.027" % cy_, "fxs", when="%s && !portrait && %s" % (M, done), align="right", color=PINK, tracking=0.2, opacity="vm")
    T("NEXT", "%s + cardW - 0.008" % cx_, "%s + 0.027" % cy_, "fxs", when="%s && !portrait && %s" % (M, nxtc), align="right", color=GOLD, tracking=0.2, opacity="vm*(0.6 + 0.4*pulse)")
    T("{w%d_clock}" % i, "%s + 0.009" % cx_, "%s + 0.057" % cy_, "fm*1.2", when="%s && !portrait" % M, align="left", color=INK, tracking=0.06, opacity="vm")
    T("{w%d_dow} {w%d_short}  ·  %s" % (i, i, utc_text(w[5])), "%s + 0.009" % cx_, "%s + 0.0785" % cy_, "fxs", when="%s && !portrait" % M, align="left", color="#D9A3DE", tracking=0.1, maxw="cardW*0.9", opacity="vm", upper=True)
    T("{w%d_clock}  ·  {w%d_dow} {w%d_short}" % (i, i, i), "%s + cardW - 0.009" % cx_, "%s + 0.032" % cy_, "fs", when="%s && portrait" % M, align="right", color="#FFC4DC", tracking=0.08, maxw="cardW*0.5", opacity="vm", upper=True)
    L("button", when=M, x=cx_, y=cy_, w="cardW", h="cardH", radius=0.007, set="sel", to="sel == %d ? 0 : %d" % (i, i), hover="hov", hoverTo=i, color="#FFFFFF/0.07")

note("""
the sides of the map (wide window only): a key to the colours, a tally of live regions, and the next wave
""")
LB = "%s && !portrait" % M
T("RELEASE ZONES", 0.058, "mpY + 0.034", "fs", when=LB, align="left", color=TEAL, tracking=0.28, opacity="vm")
for k, (colr, label) in enumerate(((PINK, "LIVE"), (GOLD, "NEXT WAVE"), (UNREL, "NOT YET"), (TEAL, "PICKED"))):
    yy = "mpY + %.3f" % (0.075 + 0.028 * k)
    L("circle", when=LB, x=0.0635, y=yy, r=0.0048, color=colr, opacity="vm")
    T(label, 0.074, "(%s) + fs*0.36" % yy, "fs", when=LB, align="left", color=INK, tracking=0.16, opacity="vm*0.9")
yy = "mpY + %.3f" % (0.075 + 0.028 * 4)
L("line", when=LB, x=0.0635, y="(%s) - 0.009" % yy, x2=0.0635, y2="(%s) + 0.009" % yy, color=GOLD, thick=0.0016, opacity="vm")
T("LOCAL MIDNIGHT", 0.074, "(%s) + fs*0.36" % yy, "fs", when=LB, align="left", color=INK, tracking=0.16, maxw=0.075, opacity="vm*0.9")
for k, line_ in enumerate(("COUNTRIES THE CARDS", "DO NOT NAME LIGHT UP", "WITH THEIR TIME ZONE")):
    T(line_, 0.058, "mpY + %.3f" % (0.236 + 0.021 * k), "fxs", when=LB, align="left", color="#D9A3DE", tracking=0.1, maxw=0.115, opacity="vm*0.85")
for k, line_ in enumerate(("HOVER A CARD TO PEEK", "CLICK IT TO PIN", "UP / DOWN: NEXT WAVE", "TAB: BACK TO COUNTDOWN")):
    T(line_, 0.058, "mpY + mpH - %.3f" % (0.085 - 0.021 * k), "fxs", when=LB, align="left", color="#D9A3DE", tracking=0.1, maxw=0.115, opacity="vm*0.85")
T("REGIONS LIVE", 0.945, "mpY + 0.034", "fs", when=LB, align="right", color=TEAL, tracking=0.28, opacity="vm")
L("text", when=LB, font="display", x=0.945, y="mpY + 0.105", size=0.052, align="right", color=GOLD, color2=PINK, maxw=0.115, neon=PINK, neonStrength="vm*0.8", opacity="vm", text="{doneN}/14")
L("rect", when=LB, x=0.83, y="mpY + 0.125", w=0.115, h=0.007, radius=0.0035, color="#FFFFFF/0.14", opacity="vm")
L("rect", when="%s && doneN > 0" % LB, x=0.83, y="mpY + 0.125", w="0.115*doneN/14", h=0.007, radius=0.0035, color=PINK, opacity="vm")
T("NEXT UP", 0.945, "mpY + 0.172", "fs", when=LB, align="right", color=TEAL, tracking=0.28, opacity="vm")
L("text", when=LB, font="display", x=0.945, y="mpY + 0.212", size=0.026, align="right", color=INK, maxw=0.115, opacity="vm", text=[{"when": "doneN == 14", "text": "ALL LIVE"}] + per_wave(lambda i, w: w[0], "autoW"))
T([{"when": "doneN == 14", "text": ""}] + per_wave(lambda i, w: "{w%d_dow} {w%d_clock}" % (i, i), "autoW"), 0.945, "mpY + 0.240", "fm", when=LB, align="right", color=INK, tracking=0.1, maxw=0.115, opacity="vm", upper=True)
T([{"when": "doneN == 14", "text": ""}] + per_wave(lambda i, w: [("w%d_days > 0" % i, "IN {w%d_days}D {w%d_hrs:2}H" % (i, i)), (None, "IN {w%d_hrs:2}H {w%d_min:2}M" % (i, i))], "autoW"),
  0.945, "mpY + 0.264", "fm", when=LB, align="right", color=GOLD, tracking=0.1, maxw=0.115, opacity="vm")

T("THURSDAY 19 NOVEMBER 2026  ·  ROLLING MIDNIGHT LAUNCH  ·  ALL TIMES IN {utc}", 0.5, "portrait ? 0.985 : 0.978", "portrait ? 0.0095 : 0.0165", when=M, maxw=0.92, tracking=0.2, color=INK, opacity="vm*0.85", upper=True)


# ---------------------------------------------------------------------------------------------------------------------------------
#  the end of the file
# ---------------------------------------------------------------------------------------------------------------------------------
# no comma after the last layer
for k in range(len(out) - 1, -1, -1):
    if out[k].strip() and not out[k].strip().startswith("//"):
        out[k] = out[k].rstrip(",")
        break
raw("      ]")
raw("    }")
raw("  ]")
raw("}")
text = "\n".join(out) + "\n"
with open(OUT, "w", encoding="utf-8", newline="\n") as fh:
    fh.write(text)
print("wrote", os.path.abspath(OUT), len(text), "bytes,", n_layers[0], "layers")
