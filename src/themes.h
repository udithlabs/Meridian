// Meridian: theme packs.
//
// A theme pack is a folder you drop into the "themes" folder next to Meridian.exe (or into %APPDATA%\Meridian\themes).
// It holds a theme.json (colours, scenes, extra tabs) and, if it wants them, picture and font files. Meridian reads every
// pack it finds, lists them in Settings > Themes and in the T key cycle, and draws them. Delete the folder to remove a pack.
// The built-in themes (Auto, Day, Dawn, Sunset, Dusk, Night, Ember) and faces are compiled into the program and never
// depend on any of this.
//
// The file format is documented in themes/README.txt. This header is the in-memory shape of a loaded pack:
//
//   themes.h / themes.cpp      what a pack is, reading theme.json, finding packs on disk, loading their pictures and fonts
//   expr.h  / expr.cpp         the small formula language that theme files may use instead of a number
//   scene_theme.cpp            drawing: turns a pack's layers into Direct2D calls (this is where to add a new kind of layer)
//
// HOW TO ADD A NEW KIND OF LAYER (for example "ring")
//   1. add it to enum class Kind below, and any new setting it needs to struct Layer
//   2. themes.cpp:     in loadLayer(), add a branch that reads its keys from the JSON
//   3. scene_theme.cpp: in drawLayer(), add a case that draws it
//   4. themes/README.txt: describe it
#pragma once
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <filesystem>
#include "gfx.h"
#include "expr.h"

namespace themes {

// ---------------------------------------------------------------- values a theme file can contain
// A number that is either written plainly (0.25) or is a formula ("0.5 + 0.1 * sin(t)").
struct Num {
    bool set = false;                       // was it written in the file at all?
    double c = 0;                           // the value, when it is a plain number
    std::shared_ptr<expr::Program> prog;    // the compiled formula, when it is one
    double eval(const double* v, double def = 0) const { return !set ? def : prog ? prog->eval(v) : c; }
};

// A colour: a fixed one ("#FF3E91", "#FF3E9180" with alpha) or one that follows the live palette ("ink", "accent" ...),
// so a layer can change with the time of day or the weather like the built-in themes do.
struct Col {
    enum Src { Fixed, Ink, Ink2, Ink3, Accent, Accent2, Line, Panel, Plate } src = Fixed;
    gfx::Color c{1, 1, 1, 1};
    bool set = false;
};

// Text with {tokens}: "{release_days:2} DAYS" -> "21 DAYS". Numbers come from variables, words from the clock (see README).
struct TextPiece {
    enum Type { Lit, Var, Tok } type = Lit;
    std::string s;                          // the literal text, or the token name
    int var = -1;                           // variable slot, for numbers
    int width = 0;                          // zero-pad width ("{x:2}" -> 2)
};
struct Tpl { std::vector<TextPiece> pieces; };
// Text that depends on the situation: a list of cases, the first whose "when" is true wins; each case has one or more
// texts that take turns (a new one every "period" seconds on a banner).
struct TextCase { Num when; std::vector<Tpl> texts; };
struct Choice { std::vector<TextCase> cases; };

// ---------------------------------------------------------------- layers
enum class Kind { Gradient, Rect, Circle, Image, Glow, Stars, Skyline, Streaks, Text, Shapes, Banner, Confetti, Shade, Line, Button, Map, Traffic };

// One drawn thing. Not every setting is used by every kind; themes/README.txt lists which kind reads which.
// Positions and sizes are fractions of the window: x, w, rx, x2 of the width; y, h, ry, y2, r, size, cap, thick of the height.
struct Layer {
    Kind kind = Kind::Rect;
    int index = 0;                          // position in its list, for messages
    Num when, opacity;                      // draw only while "when" is non-zero; opacity 0..1 (default 1)
    Num x, y, x2, y2, w, w2, h, r, rx, ry, size, size2, cap, rotate, thick, thick2, count, seed, speed, lit, flash, gap, maxw, tracking, radius,
        period, fly, y0, y1, sway, fade, sides, planeH, alpha, alpha2, neonStrength, outlineW, glow,
        to, hoverTo, trail, dir, active;                   // button: to, hoverTo   traffic: trail, dir, active
    std::string src, plane, anchor, pivot, font, shape, align;
    std::string set, hover, vehicle, key;                  // button: the state it sets, the hover variable it drives, the key that also presses it;  traffic: what flies
    int slotSet = -1, slotHover = -1;                      // button: variable slots of 'set' and 'hover' (found when the file is read)
    std::vector<std::string> groups;                       // map: which countries / groups of the map file to draw ("*" = every country)
    Choice text;
    Col color, color2, color3, outline, neon;
    std::vector<Col> colors, windows;
    std::vector<double> at;
    bool upper = false, digits = false, flip = false;
    // drawn once and reused (the skyline): rebuilt when the window size changes
    mutable gfx::Bitmap sprite;
    mutable uint64_t spriteKey = 0;
    mutable float spriteX = 0, spriteY = 0;                 // map: where the picture sits, relative to the map's top-left corner
};

struct Scene {
    std::vector<Layer> layers;
    int fps = 0;                            // 0: nothing moves (drawn when the clock ticks); otherwise how many frames a second it needs
};

struct Face {                               // an extra tab: appears in the dock while its pack is installed
    std::string id, name;
    Scene scene;
};

// A date the theme counts down to or up from. 'at' is "YYYY-MM-DD" or "YYYY-MM-DD HH:MM", local time where the clock is.
// 'utc' (optional): read 'at' as the local time of a place that is that many hours from UTC (13, 5.5, -6 ...), not where the clock is. The parts
// and the {X_...} words are still shown in the clock's own time. This is how one date can count down in many time zones.
struct Countdown { std::string id, at; int firstVar = 0; bool hasUtc = false; double utc = 0; };

// A number the viewer changes by clicking (a "button" layer), kept while Meridian runs. "state": {"view": 0}. A "hover" variable is instead worked out
// again every frame: it has its default until the pointer is over a button that names it.
struct StateVar { std::string name; double def = 0; bool hover = false; int slot = 0, slotT = -1; };

// A map picture for the "map" layer (themes/README.txt): countries as outlines in a box of 0..1 x 0..1.
struct MapData {
    double aspect = 2.0;                                    // width / height of the box
    struct Group { std::string id; bool open = false; std::vector<std::vector<gfx::Pt>> rings; float x0 = 0, y0 = 0, x1 = 0, y1 = 0; };   // open: lines (a grid), not shapes
    std::vector<Group> groups;
};

struct Pack {
    // ---- identity
    std::string id, name, author, description;
    int version = 1;
    std::filesystem::path dir;
    std::string startsStr, expiresStr;      // optional: the pack is hidden before 'starts' and after 'expires' (YYYY-MM-DD)
    bool yearly = false;                    // "yearly": true  repeats that window every year (the dates may then be written MM-DD)

    // ---- state of loading
    bool ok = false;                        // false: theme.json could not be used; 'error' says why
    std::string error;
    std::vector<std::string> warnings;      // things that were skipped (unknown layer type, missing picture ...)

    // ---- look
    gfx::Palette pal;
    gfx::Color neonColor{1.f, 0.24f, 0.57f, 1.f};          // the colour of the glow around big digits when pal.neon > 0
    std::string fontFile[gfx::F_COUNT];                    // theme-relative file per font role ("" = keep the built-in font)
    std::vector<uint8_t> fontData[gfx::F_COUNT];           // loaded when the pack becomes active

    // ---- formulas
    expr::VarTable vars;                    // every name a formula in this pack may use
    int userVarFirst = 0;
    std::vector<std::pair<int, std::shared_ptr<expr::Program>>> userVars;      // "vars" of the file, computed in order every frame
    std::vector<Countdown> countdowns;
    std::vector<StateVar> states;

    // ---- scenes
    Scene backdrop;                         // behind the Quiet face (the full scene)
    Scene backdropLite;                     // behind every other face (a quieter version, or nothing)
    std::vector<Face> faces;

    // ---- loaded pictures (by theme-relative path)
    std::unordered_map<std::string, gfx::Bitmap> images;
    std::unordered_map<std::string, bool> imageFailed;
    std::unordered_map<std::string, std::shared_ptr<MapData>> maps;      // map files, read when first drawn (nullptr: could not be read)

    // A pack normally only adds its own tab(s): its palette, fonts and scene are used on that tab and nowhere else, so every other
    // face looks exactly as it does without the pack. A pack that sets  "global": true  in theme.json can also be chosen as a
    // theme for the whole clock (Settings > Themes, or the T key), and then its 'backdrop' scenes are drawn behind the other faces.
    bool global = false;
    // Cloud and fog tint the built-in themes (darker, greyer, flatter). A theme pack is a finished picture, so by default the weather does
    // not touch it: its colours, its scene and the formula variables  gloom  and  haze  (always 0) and  light  (always 1) stay as they are.
    // A pack that wants the weather to colour it says  "weather": true  in theme.json.
    bool weather = false;
    bool disabled = false;                  // switched off in Settings (stays installed)
    // Usable right now: loaded fine, switched on, inside its starts/expires dates.
    bool live(double now, int tzOff) const;
    // The window the starts/expires dates give right now, as instants (0 = no limit on that side) and as YYYY-MM-DD text. A yearly pack
    // gets the window it is in, or else the next one that comes (or, when it just ended, the last one).
    struct Window { double start = 0, end = 0; std::string startText, endText; };
    Window window(double now, int tzOff) const;
    // When a countdown of this pack ends. A normal date is read as written; in a yearly pack a date may leave out the year ("10-31 00:00"),
    // then it is the next time that day comes round within the pack's window. 0 when the date cannot be read.
    double countdownAt(const Countdown& cd, double now, int tzOff) const;
    int fpsBackdrop(bool full) const { return (full ? backdrop : backdropLite).fps; }
};

// ---------------------------------------------------------------- finding packs
// Where to look. Called once at start; the first folder wins when two packs share an id.
void setRoots(const std::vector<std::filesystem::path>& roots);
const std::vector<std::filesystem::path>& roots();
// True when something in those folders changed since the last scan (a pack added, removed or edited). Cheap enough to
// call every few seconds.
bool changed();
// Where Meridian keeps two small notes for safe starting (a folder it may write to, e.g. %LOCALAPPDATA%\\Meridian). While a pack is being
// loaded its name is written down; if Meridian dies right there, the next start skips that pack (until its theme.json is edited)
// instead of dying again. Call before the first rescan; without it that protection is simply off.
void setStateDir(const std::filesystem::path& dir);
// Reads every pack again. Frees pictures first, so pass the drawing object (or nullptr if there is none yet).
void rescan(gfx::Gfx* g);
const std::vector<std::unique_ptr<Pack>>& all();
Pack* find(const std::string& id);
// "pack/face" keys for the extra tabs
std::string faceKey(const Pack& p, const Face& f);
bool findFace(const std::string& key, Pack*& pack, Face*& face);

// ---------------------------------------------------------------- resources
// A picture from the pack folder, loaded on first use. Returns nullptr while it cannot be shown (missing, or the budget
// of pictures to decode this frame is used up: ask again next frame). 'allowLoad' false only looks in the cache.
const gfx::Bitmap* image(gfx::Gfx& g, Pack& p, const std::string& rel, bool allowLoad);
// A map file of the pack (JSON, see themes/README.txt), read on first use. nullptr when missing or broken (it is then not tried again).
const MapData* mapData(Pack& p, const std::string& rel);
// Makes the fonts of this pack the ones in use (nullptr: the built-in ones). Cheap when nothing changes.
void syncFonts(gfx::Gfx& g, Pack* want);
// Frees every picture and cached drawing (device lost or window rebuilt).
void release(gfx::Gfx& g);

// parse "YYYY-MM-DD" / "YYYY-MM-DD HH:MM" as local time at the given UTC offset (seconds); 0 when it cannot be read.
// Strict: 31 February, 99:99 or anything after the time make it 0 (it is never turned into some other day).
double parseLocal(const std::string& s, int tzOff);

// The biggest files a pack may have (a bigger one is refused with a message, not loaded).
constexpr size_t kMaxManifestBytes = 1u << 20;       // theme.json: 1 MB
constexpr size_t kMaxFontBytes = 32u << 20;          // one font file: 32 MB
constexpr size_t kMaxPictureBytes = 32u << 20;       // one picture: 32 MB
constexpr size_t kMaxMapBytes = 4u << 20;            // one map file: 4 MB

} // namespace themes
