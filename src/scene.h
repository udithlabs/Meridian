// Shared scene helpers (implemented in scene_common.cpp, used by scene.cpp / scene_faces.cpp).
#pragma once
#include "app.h"

namespace app {

inline std::wstring Ws(const std::string& s) { return util::widen(s); }

struct ClockParts { std::wstring hm, sec, ampm; };
ClockParts clockParts(const App& a);
std::string tempStr(const App& a, double celsius);             // "28°"
std::string clockStr(const App& a, double t);                  // "5:58 AM" / "17:58"
std::string clockStrDay(const App& a, double t);               // same, prefixed with the weekday when not today

gfx::Color tint(const gfx::Palette& P, const gfx::Color& c);   // pull a fixed colour toward ember when the red theme is on

struct SkyCol { gfx::Color zen, hor; };
SkyCol skyColors(double sunAlt, float cloud);

void addHit(App& a, float x, float y, float w, float h, int id);
void drawButton(Ctx& c, float x, float y, float w, float h, const std::wstring& label, bool active, int hitId, gfx::FontId font = gfx::F_MONO_M, float size = 15.f);

// sky face parts
void drawDome(Ctx& c, float cx, float cy, float R, bool labels);
void drawDayBand(Ctx& c, float x, float y, float w, float h);
void drawHourly(Ctx& c, float x, float y, float w, float h);
void drawWeatherBlock(Ctx& c, float x, float y, float w, float h);
void drawTodayList(Ctx& c, float x, float y, float w, float h);
void drawSunMoonLines(Ctx& c, float x, float y, float w);

// weather behind the clock on the Quiet face (scene_quiet.cpp)
void drawQuietFx(Ctx& c);
void drawSunFlare(Ctx& c);
void drawScenery(Ctx& c);                                // behind the Quiet and Sky faces: the theme's own scenery (Midnight sky, Sunlit II rays) and the sun flare
void drawSpace(Ctx& c);                                  // Midnight: stars, nebula, planet, meteors, comet
void drawSunRays(Ctx& c);                                // Sunlit II: rays of sunlight and wisps of cloud
void drawSpaceFront(Ctx& c);                             // Midnight: the saucer, in front of everything, once a day
bool spaceBusyNow(const App& a, double now);             // a meteor, the comet or the saucer is on screen right now (the frame loop draws fast frames)
double spaceNextStart(const App& a, double now);         // when the next one starts (unix seconds), so a short meteor is never missed between slow frames
void spaceRelease(gfx::Gfx& g);
void drawSkyRain(Ctx& c);                                // rain & snow behind everything on the Sky face (Settings > Quiet > Rain & snow on the Sky tab)
void drawFocusBackdrop(Ctx& c);                          // Focus tab ambient weather / starry backdrop
void drawSpaceBackdropOnly(Ctx& c);                      // Midnight deep sky & subtle meteors (no saucer), used by Focus tab Stars mode
void drawQuietFront(Ctx& c, float midY, float clockH);   // the one cloud (and gentle snow/rain) that drifts in front of the time (drawn after the text)
std::string quietFxDescribe(const App& a);

// theme packs (scene_theme.cpp): the scene behind the faces, and the extra tabs a pack brings. mul fades the scene (0..1)
void drawThemeBackdrop(Ctx& c, themes::Pack& p, bool full, float mul);
void drawThemeFace(Ctx& c, themes::Pack& p, themes::Face& face);

// widgets (scene_widgets.cpp)
void drawWidgetTile(Ctx& c, int id, float x, float y, float w, float h);

// the rotating panel and its parts (scene_panel.cpp)
void drawSkyPanel(Ctx& c, float x, float y, float w, float h);
// the rotor shared by the Sky panel and the Widgets face
bool rotorUpdate(App& a, Rotor& r, const std::vector<int>& list, int secs, bool pointerOver);
void rotorDraw(Ctx& c, Rotor& r, float travel, const std::function<void(Ctx&, int)>& draw);
void rotorDots(Ctx& c, Rotor& r, const std::vector<int>& list, int secs, float x, float cy, int align);
void drawEventsPanel(Ctx& c, float x, float y, float w, float h);
void drawHolidayPanel(Ctx& c, float x, float y, float w, float h, bool head = true, int maxRows = 3);
void drawInfoRow(Ctx& c, float x, float yy, float w, const gfx::Color& col, const std::string& tag, const std::string& note, const std::string& title, const std::string& detail);
void drawWorldClocks(Ctx& c, float x, float y, float w, float h);       // scene_widgets.cpp
void holidaysBeginFrame(App& a);                                        // resolves holiday hover from last frame's targets
void addHolidayHit(App& a, float x, float y, float w, float h, int ymdKey);
gfx::Color holidayColor(const gfx::Palette& P, int group);
void drawHolidayTooltip(Ctx& c);
int ymdKey(int y, int m, int d);

// sky events (scene_events.cpp)
void eventsBeginFrame(App& a);                                          // resolves hover from last frame's targets
void drawEventsBelow(Ctx& c, float cx, float cy, float R, bool labels); // meteors, shadow, links: under the Sun and Moon
void drawEventsAbove(Ctx& c, float cx, float cy, float R, bool labels, float moonSize, float sunX, float sunY, bool sunUp); // rings, labels, insets
void drawEventChips(Ctx& c, float x, float y, float w);                 // one row of small pills
void drawEventTooltip(Ctx& c);                                          // hover card, drawn last
std::string eventText(const App& a, const std::string& tokens);
std::string eventKindLabel(int kind);
gfx::Color eventKindColor(int kind);
std::string eventWhenShort(const App& a, const skyev::Event& e);
void addEventRowHit(App& a, float x, float y, float w, float h, int idx);
bool eventNamesPlanet(const App& a, int planetId);                       // a conjunction label already names this planet         // expands {t:..}, {d:..}, {dt:..}

} // namespace app
