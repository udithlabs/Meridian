MERIDIAN THEME PACKS
====================

A theme pack is a FOLDER. Drop it in this "themes" folder and Meridian finds it by itself (within about 4 seconds, no
restart). Delete the folder and it is gone again. The built-in themes (Auto, Day, Sunlit, Sunlit II, Dawn, Sunset, Dusk, Night, Midnight, Galaxy, Ember) and the
built-in tabs (Sky, Quiet, Timer, Almanac, Widgets, Focus) are part of the program, so a pack can never change or break them.

Where Meridian looks (first one wins if two packs have the same id):
    1.  the "themes" folder next to Meridian.exe            (this folder)
    2.  %APPDATA%\Meridian\themes                            (type that in the Explorer address bar)

What a pack adds
    - its own TAB(S) in the dock (for example "GTA VI"). A pack's colours, fonts and artwork are used on its own tab only.
      Sky, Quiet, Timer, Almanac and Widgets look exactly as they always do.
    - OPTIONAL: if theme.json says   "global": true   the pack can also be picked as a theme for every tab (Settings > Themes,
      or the T key), and its "backdrop" scene is drawn behind the other tabs. Leave it out (or false) for a tab-only pack.

Settings > Themes lists every pack, shows if it loaded or why not (with the line to fix), lets you switch one off without
deleting it, and shows when it hides itself.

Make your own in 3 steps
    1. copy the folder  _template  and rename it (a folder whose name starts with _ or . is ignored, so _template itself
       never shows up; your copy must NOT start with _)
    2. open  theme.json  in any text editor (Notepad is fine) and change things. Every line has a note.
    3. save. Meridian reloads it by itself. If something is wrong, Settings > Themes says which line.

Remove a pack
    - delete its folder (or put a _ in front of the folder name to park it)
    - or switch it off in Settings > Themes
    - or set  "expires": "YYYY-MM-DD"  in its theme.json and it hides itself on that day (the GTA VI pack does this on
      20 December 2026 and the Halloween pack on 3 November 2026; delete that line to keep it, or just delete the folder earlier)


THE FILE: theme.json
====================
It is JSON with two extras: you may write notes after // (or between /* and */), and a comma after the last item is fine.
Text goes in "double quotes". Numbers are plain: 0.25, not "0.25" (though a quoted formula is allowed where a number goes).
Apart from those two extras the file must be strict JSON, and Meridian checks all of it: anything after the closing } is refused, a number
must be a real one (0.5, not .5 or 05), and in text only these escapes exist:  \"  \\  \/  \b  \f  \n  \r  \t  \uXXXX
(so a backslash is written \\ ). Settings > Themes names the line when something is wrong.

    {
      "id": "mytheme",              short name for the pack (letters, digits, - and _). Defaults to the folder name.
      "name": "My theme",           the name shown in Settings
      "author": "me",
      "version": 1,
      "description": "one line",
      "global": false,              true: can also be a theme for every tab (see above). Default false.
      "weather": false,             true: cloud and fog may tint this pack (see WEATHER below). Default false: it always looks the same.
      "starts":  "2026-11-01",      optional: hidden before this date
      "expires": "2026-12-20",      optional: hidden from this date on (YYYY-MM-DD)
      "yearly": false,              true: the starts/expires window repeats every year, and the dates are written without a year (MM-DD)
      "base": "night",              start colours from a built-in theme: day sunlit sunlit2 dawn sunset dusk night midnight galaxy ember
      "palette": { ... },           colours you want to change
      "fonts": { ... },             your own fonts
      "countdowns": { ... },        dates to count to
      "vars": { ... },              your own names for numbers
      "faces": [ { ... } ],         the extra tab(s)
      "backdrop": { ... },          (global packs only) scene behind the Quiet tab
      "backdropLite": { ... }       (global packs only) scene behind every other tab
    }

Unknown keys are reported in Settings > Themes as warnings ("unknown setting 'colour'"), so typos do not fail silently.


PALETTE
-------
Every key is optional; what you leave out comes from "base".
    bgTop, bgBottom        the background gradient
    ink, ink2, ink3        main text, second text, quiet text
    line                   thin lines          accent, accent2      highlight colours
    panel                  card background     plate                dark plates
    starVis                0..1 how visible the stars are
    skyTop, skyHorizon     the round sky map on the Sky face: the colour overhead and the colour at the rim (day-time look). Left out, they are
                           made from bgTop and bgBottom. Late at night Meridian deepens both by itself and brings the stars out.
    neon                   0..1 glow around big clock digits       neonColor   colour of that glow
    dark                   true / false: is the theme dark (changes a few shadows)


COLOURS
-------
    "#RRGGBB"   "#RRGGBBAA" (AA = transparency, 00 invisible, FF solid)   "#RGB"   "transparent"
    "ink" "ink2" "ink3" "accent" "accent2" "line" "panel" "plate"    follow the theme's palette
    any of them can end in /0.4 to be 40% opaque:   "#FFF3E6/0.62"   "ink/0.5"


NUMBERS, UNITS AND FORMULAS
---------------------------
Positions and sizes are FRACTIONS OF THE WINDOW, so the layout fits any window shape:
    x, w, rx, x2             of the width      0 = left edge,  1 = right edge
    y, h, ry, y2, r, size, cap, thick   of the height     0 = top,   1 = bottom
Anywhere a number goes you may write a formula in quotes:   "x": "0.5 + 0.01*sin(t)"
Operators:  + - * / %   comparisons < > <= >= == !=   logic && || !   choice  a ? b : c
Functions:  sin cos tan atan2 abs sqrt pow exp log floor ceil round fract sign min max clamp(x,lo,hi) mix(a,b,t)
            smoothstep(a,b,x) step(edge,x) hash(n) mod(a,b)       constant: pi
A comparison is 1 (true) or 0 (false), so "0.4 * (hour > 18)" works.

Names you can use in any formula:
    t          seconds since 1970 (huge; use it inside sin() or fract(), e.g. sin(t*0.4) )
    W H        window size in pixels            s    H/1080 (a scale factor)
    portrait   1 when the window is taller than wide
    gloom      0..1  how cloudy/stormy it is outside        haze   0..1 how foggy
    light      0..1  how bright the weather leaves the scene: (1 - 0.45*gloom)*(1 - 0.55*haze). Good as an opacity.
    hour       local time of day, 0..24 (14.5 = 2:30 pm)    sunAlt   height of the Sun in degrees
    tzHours    how far the clock's time zone is from UTC, in hours (5.5 for India, -5 for New York)
    weekday day month year minute second       local date and time parts
    + every name in your "vars" and every countdown part (see below)

"vars": your own names, written once, used many times. Worked out top to bottom, so later ones may use earlier ones.
    "vars": { "horizon": "portrait ? 0.80 : 0.81", "sunY": "horizon - 0.02" }
You cannot reuse a built-in name (t, W, hour ...). A formula that uses an unknown name is reported with its line.


DATES AND EVERY-YEAR PACKS
--------------------------
Dates are checked: 31 February, month 13 or 24:00 are never turned into some other day. A bad "starts" or "expires" is ignored, and a countdown with
a bad date is left out; Settings > Themes shows a note that names the line to fix (select the pack to read its notes).
    "starts": "2026-11-01", "expires": "2026-12-20"          a pack that is shown between these two days, once
A pack for an occasion that comes round every year says  "yearly": true  and writes the dates without a year:
    "yearly": true, "starts": "10-01", "expires": "11-03"    on from 1 October, off on 3 November, and back next 1 October
Both dates are needed then. 29 February counts as 28 February in years that have none. In a yearly pack a countdown date may leave out the
year too:  "countdowns": { "hal": "10-31 00:00" }  is the next 31 October inside the window (the Halloween pack does this). Settings > Themes
shows "comes back every year" for such a pack and the dates of the window it is in, or the next one.

COUNTDOWNS
----------
    "countdowns": { "release": "2026-11-19 00:00" }          local time where the clock is ("YYYY-MM-DD" = midnight)
For a countdown called X you get these names (formulas) and {tokens} (text):
    X_left      seconds left (0 once reached)         X_past     seconds since it was reached (0 before)
    X_done      1 once reached                         X_daysf    days left as a fraction
    X_days X_hrs X_min X_sec    the time left, split up (after the date: the time SINCE it)
    {X_when}    "THURSDAY 19 NOVEMBER 2026"   {X_clock} "12:00 AM"   {X_date} "19 NOVEMBER"
    {X_short}   "19 Nov"                       {X_dow}   "Thu"          (all in the clock's own time zone)    {utc}  "UTC+5:30"
A countdown can also be a moment in a PLACE's time instead of the clock's:
    "countdowns": { "nz": { "at": "2026-11-19 00:00", "utc": 13 } }      midnight in UTC+13 (New Zealand), whatever time zone the clock is in
"utc" is hours from UTC (-14 to 14, halves allowed like 5.5). The words above then show that moment on the clock's own time.
The GTA VI pack uses this: its fourteen launch waves are w1 .. w14, one for each time zone.


STATE: NUMBERS A CLICK CHANGES
------------------------------
A tab can react to the mouse and keyboard. Declare the numbers first (with their start value), then use them in any formula and set them from "button" layers:
    "state": { "view": 0, "sel": 0 },     numbers a click (or a key) changes. Each also gets NAME_t: the seconds since it last changed (use it to fade a change in).
    "hover": { "hov": 0 }                 numbers that follow the pointer: set while the pointer is over a button, back to the start value when it leaves
State is kept while Meridian runs (it starts again from the start values next time). Up to 40 names in all.


FONTS
-----
    "fonts": { "display": "fonts/Anton-Regular.ttf" }       path inside the pack folder; .ttf or .otf
Roles:  display (the big clock digits)   light   medium   serif   serifItalic   mono   monoMedium
A role you list replaces that font while your tab is shown. Keep the licence file of any font you add in the pack.
The font must contain the characters you use (digits, : and the letters in your text). Fonts that miss a letter show a gap.


FACES (extra tabs)
------------------
    "faces": [ { "id": "countdown", "name": "GTA VI", "fps": 24, "layers": [ ... ] } ]
    name   the label on the dock button
    fps    how many frames a second the tab needs. 0 = nothing moves (it is only redrawn when the clock ticks, cheapest).
           Use a low number (12-24) for slow movement. Higher costs more battery/CPU.
    layers drawn in order: the first is at the back, the last at the front.

Settings every layer can have:
    "type":    which kind (list below)
    "when":    drawn only while this is non-zero,  "when": "release_done"    or   "when": "!portrait"
    "opacity": 0..1 (formula allowed)  e.g.  "opacity": "light"  so weather dims it
    "note":    ignored; write what you like


LAYER TYPES
-----------
gradient   bands of colour across the whole width
    y0, y1 (top and bottom, default 0 and 1)   colors ["#..", "#.."...] (2 or more)   at [0, 0.5, 1] where each colour sits (optional)

rect       a rectangle
    x y w h   radius (rounded corners)   color   thick (draws only the outline, that thick, instead of filling it)

circle     a disc, a vertical gradient disc, or a ring
    x y r   color   color2 (gives a top-to-bottom gradient)   thick (draws a ring of that thickness instead)

image      a picture (PNG with transparency is best; JPG also works)
    src "art/palm.png"   x y = where   w and/or h = size (give one and the other follows the picture's shape)
    anchor  which point of the picture sits at x,y:  "center" (default) "bottom-center" "top-left" "right" ...
    rotate  degrees (formula ok)   pivot "center" to turn around the middle (default: around x,y)   flip true

glow       a soft light
    x y   r (round)  or  rx ry (oval)   color   opacity = how strong

stars      twinkling points
    count (max 500)  seed  y0 y1 (the band they fill)  size size2 (smallest/largest)  speed  fade  colors [..]

skyline    a row of city buildings, drawn once and reused
    y (the ground line)  h (tallest)  seed  colors [far, near]  windows [colour, colour]  sides 0..1 (taller at the edges)

streaks    the shimmer of light on water
    x  y0 y1  count (max 60)  w w2 (width at top / bottom)  color color2  thick thick2  speed  sway

text       words, or a big clock-style number
    text   "plain"  |  ["one", "two"]  |  [ { "when": "release_done", "text": "OUT NOW" }, { "text": "SOON" } ]
           the first case whose "when" is true wins; with several texts in a case they take turns every "period" seconds
           {tokens}: {hm} {ampm} {sec} {time} {weekday} {weekdayShort} {day} {monthname} {mon} {year} {date} {utc}
                     any variable or countdown number: {release_days}  and zero-padded:  {release_hrs:2} -> 05
    x y    position (y is the text baseline)   align "left" | "center" | "right"
    font   one of the roles above   size (font size)  or  cap (height of capital letters)   maxw (shrink to fit this width)
    color  color2 (top-to-bottom gradient)   tracking (extra space between letters, in em)   upper true (CAPITALS)
    digits true   draws it with the big-clock digit style (tabular, with a narrow colon). Use for numbers.
    gap    (digits) width of the colon relative to a digit (0.42)
    neon   a colour, to put a glow around the text   neonStrength 0..1

shapes     a row of stars, circles or diamonds, some lit (like police stars)
    shape "star" | "circle" | "diamond"   count (max 12)   x y (centre of the row)   size   gap (spacing)
    lit    how many are filled (formula ok)   color   color2 + flash (alternate the two colours, flash = speed)   outline  outlineW  glow

banner     a small plane pulling a banner across now and then
    y   period (seconds between trips)   fly (seconds a trip takes)   h   size   font
    text   (same choice list as "text" above; a new message each trip)
    fill edge ink   colours of the banner, its border and its writing
    plane  "art/plane.png" optional picture, nose pointing right   planeH

confetti   little bars falling and turning (use with "when")
    count (max 300)  seed  colors [..]  speed  size  sway

shade      darkening by weather, on top of everything drawn so far
    color + alpha   (dark layer)     color2 + alpha2   (grey mist)
    typical:   { "type": "shade", "alpha": "0.46*gloom + 0.22*haze", "alpha2": "0.30*haze" }

line       a straight line
    x y x2 y2   thick   color

button     an area the pointer can click: it changes a "state" number. It is invisible, except that while the pointer is over it
           it shows a pad (color) and an outline (color2) if you give them
    x y w h   radius   color (the pad)   color2 (the outline)
    set    the name of a "state" number to change      to   the value it is changed to (a formula is fine: "1 - view" flips it, "sel == 3 ? 0 : 3" toggles)
    hover  the name of a "hover" number to set while the pointer is over the button      hoverTo   the value it gets
    key    "tab" "space" "enter" "up" or "down": the same change when that key is pressed (use w 0, h 0 for a key with no area)
    A button only works while its "when" is true.

map        a world map (or any shapes) made of country outlines that you light up
    src "art/world.json"   x y w   (the left, the top and the width; the height follows the picture)
    countries   a list of country ids to draw, like ["554", "036"]: "*" every country, "_grid" the latitude/longitude lines, "_bands" the time zone stripes,
                "_equator" the equator. Leave it out for all of them.
    color (fill)   color2 (outline)   thick (outline)   glow (a soft halo around the shapes, as a fraction of the height)
    The shapes are drawn once and kept, so use "opacity" (a formula) to pulse or fade them, and a "when" to show a layer only for the countries you want:
        { "type": "map", "src": "art/world.json", "x": 0.2, "y": 0.2, "w": 0.6, "countries": ["356"], "color": "#FF3E91/0.6", "opacity": "india_done" }
    The map file is JSON:  { "aspect": 2.9, "groups": [ { "id": "356", "name": "India", "rings": [ [x0, y0, x1, y1, ...], ... ] }, ... ] }
    every ring is a closed outline in a box from 0 to 1 across and 0 to 1 down (aspect = how many times wider than tall that box is). A group with  "open": true
    is a set of lines, not a shape. Several groups may share one id. art/world.json of the GTA VI pack is made from Natural Earth by _source/make_map.py.

traffic    things that travel across the scene on their own, each trip a new height, direction and speed: planes, boats, a helicopter
    vehicle "jet" | "boat" | "heli"      count (jets, boats up to 12, helicopters up to 4)     seed   a different number gives a different pattern
    y0 y1  the band of the window the trips use (fractions of the height)     period   seconds a crossing takes (bigger = slower)     size  how big (of the height)
    jet:   trail (seconds the trail is long)   fade (how far through the trip it starts to melt away, 0..1)   active (how much of the time one is up)   dir (1 or -1: one way only)
           speed   color (the plane) color2 (the trail)
    boat:  color (hull) color2 (lights) color3 (sails and white parts)    each boat has a wake and a faint reflection; far boats are smaller and slower
    heli:  fly (the height its searchlight lands on)   color (body) color2 (the light) color3 (the red tail light)


LIMITS AND SAFE START
---------------------
A pack that asks for too much is refused with a message, so a mistake cannot freeze or crash Meridian:
    theme.json up to 1 MB      one picture or font file up to 32 MB      up to 32 countdowns, 200 vars, 40 state names, 16 tabs, 700 layers in a scene
    a formula up to 2000 characters, brackets nested up to about 30 deep      a text may not be nested deeper than 64 levels
If Meridian is ever stopped while it is reading a pack's theme.json (a file that trips a bug in Meridian), the next start
notes that pack in  themes-skipped.txt  (in %LOCALAPPDATA%\Meridian), leaves it out, and starts normally. The pack is tried again as soon
as its theme.json is saved with a change. Settings > Themes lists a left-out pack with that reason.


WEATHER
-------
Cloud and fog change the light of the BUILT-IN themes: night goes deeper (blue-black, never grey), dusk and dawn get richer purples and
oranges, a bright day turns to slate. A theme pack is a finished picture, so by default the weather does NOT touch it: its colours and its
scene always stay the same, and  gloom  and  haze  are 0 and  light  is 1 in every formula.
To let the weather act on your pack, put   "weather": true,   at the top of theme.json. Then your palette is tinted like a built-in one
(the base you chose decides how: a night base goes deeper, a dusk or dawn base gets more colourful) and  gloom  /  haze  /  light  carry the
real values, for your own layers (the "shade" layer above does it in one line).


TIPS
----
- Start by changing the template, one thing at a time, and look. Meridian reloads when you save.
- Check Settings > Themes after an edit: warnings there name the layer and the key.
- Pictures: PNG with a transparent background is best (JPG, BMP, GIF also load). Up to 8192 px a side; about 2000 px is plenty. Each is decoded once.
- Keep text short; use maxw so it shrinks instead of running off narrow windows.
- A tab needs one layer at least. The first gradient (or rect) is usually the background.
- To move or share a pack: copy the folder (it is self-contained: theme.json + art + fonts). Packs are folders, not zip files.
