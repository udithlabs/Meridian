// Meridian application state, caches and scene rendering.
#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>
#include "gfx.h"
#include "astro.h"
#include "today.h"
#include "weather.h"
#include "moonsprite.h"
#include "skyevents.h"
#include "holidays.h"
#include "storms.h"
#include "util.h"
#include "themes.h"
#include "textedit.h"

namespace app {

enum PanelId { PANEL_TODAY = 0, PANEL_EVENTS, PANEL_HOLIDAYS, PANEL_WORLD, PANEL_PINNED, PANEL_COUNT };
enum Face { FACE_SKY = 0, FACE_QUIET, FACE_TIMER, FACE_ALMANAC, FACE_WIDGETS, FACE_FOCUS, FACE_COUNT,
            FACE_PACK = 100 };              // an extra tab brought by a theme pack (Settings::facePack says which); not counted in FACE_COUNT
enum WidgetId { WG_WORLD = 0, WG_MONTH, WG_SYSTEM, WG_NOTE, WG_COUNTDOWN, WG_PHASES, WG_WEEK,
                WG_TODAY, WG_HOLIDAYS, WG_HOURLY, WG_DAYLIGHT, WG_SKY, WG_AIR, WG_YEAR, WG_COUNT };
const char* widgetName(int id);
const char* widgetBlurb(int id);
int cityCount();
const char* cityName(int i);
enum ThemeMode { THEME_AUTO = 0, THEME_DAY, THEME_DUSK, THEME_NIGHT, THEME_EMBER, THEME_DAWN, THEME_SUNSET, THEME_SUNLIT, THEME_SUNLIT2, THEME_MIDNIGHT, THEME_COUNT };
// the order themes are offered in (the numbers above are what settings.ini stores, so they never move)
constexpr int THEME_ORDER[THEME_COUNT] = {THEME_AUTO, THEME_DAY, THEME_SUNLIT, THEME_SUNLIT2, THEME_DAWN, THEME_SUNSET, THEME_DUSK, THEME_NIGHT, THEME_MIDNIGHT, THEME_EMBER};
inline int themeSlot(int theme) { for (int i = 0; i < THEME_COUNT; ++i) if (THEME_ORDER[i] == theme) return i; return 0; }

// The version shown in Settings > About. Change it here (and in src/app.rc) when you make a new build.
constexpr const char* MERIDIAN_VERSION = "2.9.7";
constexpr const char* MERIDIAN_CODENAME = "Snowfall";
// An "edition" is a name for one hand-made line of the app (the Astra Edition is another one). It is shown in Settings > About and in the setup.
constexpr const char* MERIDIAN_EDITION = "Sonnet Edition";

struct Settings {
    int theme = THEME_AUTO;
    int face = FACE_SKY;
    bool h24 = false;
    bool seconds = true;
    bool useF = false;
    astro::Geo geo;                         // default: Greenwich (Prime Meridian)
    bool poya = true;
    bool pixelShift = true;
    bool keepAwake = true;
    bool topmost = false;
    bool fullscreen = false;
    float brightness = 1.0f;
    bool autoDim = false;                   // ease the brightness down as the Sun sets, back up as it rises (Dim with the sun)
    float dimNight = 0.40f;                 // how bright it gets by the end of twilight, as a share of the Brightness setting (settings.ini: dim_at_night)
    bool emberAtNight = false;              // switch to the red palette during bedtime hours
    int emberFrom = 22, emberTo = 6;
    bool hourlyChime = false;               // optional gentle glass chime on the hour (off by default; silent at bedtime & saver)
    bool placeLocalTime = false;            // when viewing a saved place (N), show the clock in that place's own local timezone
    int focusFx = 1;                        // Focus tab ambient backdrop: 0 off, 1 live, 2 rain, 3 snow, 4 clouds, 5 fog, 6 storm, 7 stars
    int winX = -1, winY = -1, winW = 1280, winH = 720;
    std::vector<today::CustomEvent> events;
    std::vector<astro::Geo> places;
    bool provEnabled[weather::PV_COUNT] = {true, true, true, true, true, true, true, true};   // weather sources (8 providers)
    std::string contact;                    // contact sent to MET Norway / NWS in the User-Agent (optional)
    std::string weatherApiKey;              // optional free WeatherAPI.com API key (enables WeatherAPI fallback + AQI)
    std::string openWeatherKey;             // optional free OpenWeatherMap API key (enables OWM fallback + AQI)
    // sky events (shown only when they are close)
    bool evMeteors = true, evPlanets = true, evEclipses = true, evChips = true, evAnimate = true;
    int evDays = 5;
    // elements of the Sky face that can be switched off
    bool elToday = true, elWeather = true, elSunMoon = true, elBand = true, elLabels = true, elConst = true,
         elMilky = true, elEcliptic = true, elClouds = true, elTracks = true;
    // widgets
    std::vector<int> widgets = {WG_WORLD, WG_MONTH, WG_PHASES, WG_WEEK, WG_HOURLY, WG_HOLIDAYS, WG_DAYLIGHT, WG_SKY, WG_AIR, WG_YEAR};   // enabled widgets, in display order (WIDGETS face)
    int pinned = -1;                        // widget that joins the rotating panel of the Sky face
    std::vector<int> clocks = {0, 1, 2};    // world clock cities (indices into the city table)
    std::string note, cdLabel, cdDate;      // Note text; Countdown label and YYYY-MM-DD
    bool weekMon = false;                   // month calendar starts on Monday
    // the rotating panel on the Sky face: Today (elToday), Sky events, World clocks, Holidays
    bool elEvents = true, elWorld = true, elHolidays = true;
    int panelSecs = 12;                     // seconds each panel stays
    int panelStyle = 0;                     // 0 slide, 1 rise, 2 fade
    bool panelHold = true;                  // stay on a panel while the pointer is over it
    bool panelDots = true;                  // page dots
    bool holCal = true;                     // holidays on the month calendar
    // whose holidays: any mix of Sri Lanka (1), the UK (2) and the USA (4). Automatic = the country the place is in; otherwise the ones switched on by hand
    bool holAuto = true;
    int holMask = 1;                        // the countries switched on by hand (used when holAuto is false)
    int ukPref = 0;                         // which UK nation: 0 from the place, 1 England & Wales, 2 Scotland, 3 Northern Ireland
    // the Widgets face takes turns too: pages of widgets, with the same transition, pause and dots as the Sky panel
    int wgPerPage = 2;                      // widgets per page; 0 shows them all at once
    int wgSecs = 15;                        // seconds each page stays
    // sky map orientation: the compass direction that points to the top of the map, or a slow continuous turn
    float skyHeading = 0;                   // degrees clockwise from north; 0 is north up
    bool skySpin = false;                   // free rotate
    int skySpinSecs = 120;                  // seconds for one full turn
    int skySpinDir = 1;                     // 1 clockwise, -1 counter-clockwise
    // weather drawn behind the clock on the Quiet face
    bool quietFx = true;
    float quietStrength = 1.0f;             // 0.5..2: how strong the Quiet-face weather is drawn
    int quietPreview = 0;                   // 0 live weather, 1 clouds, 2 wind, 3 clouds and wind, 4 rain, 5 tornado storm, 6 thunder with no rain, 7 thunderstorm with rain, 8 fog, 9 sun flare, 10 snow
    bool spaceFx = true;                    // Midnight: the stars, meteors, comet and saucer behind the Quiet and Sky faces
    bool aliensEnabled = true;              // hidden toggle on Settings > About (aliensdontexist / aliensdoexisit)
    bool skyRain = true;                    // rain & snow fall behind the map on the Sky face too
    bool quietSmooth = true;                // rain and wind on the screen's own beat (60 fps and more); off: 30 frames a second, lighter on a laptop battery
    bool quietFlare = true;                 // a soft sun flare from the side the Sun is on, in the morning and the evening (Quiet face)
    // the light follows the weather: heavy cloud and storms darken and cool every theme, fog flattens and mutes it (all faces)
    bool weatherMood = true;
    float moodStrength = 1.0f;              // 0.5..1.5
    // theme packs: folders in the themes folder (see themes.h and themes/README.txt)
    std::string themePack;                  // id of the pack used as the theme; "" means one of the built-in themes
    std::string facePack;                   // "pack/face": the extra tab shown while face == FACE_PACK
    std::vector<std::string> packsOff;      // packs switched off in Settings (still installed)
    // named storms (cyclones, typhoons, hurricanes) near the place
    bool stormWatch = true;
    int stormRadius = 1500;                 // km
};

// a set of pages that take turns on screen (the Sky panel and the Widgets face each have one)
struct Rotor {
    int cur = -1, prev = -1;                // id on show, and the id sliding out (-1: none)
    double switchAt = -1e9;                 // when the current one started to come in
    int pending = 0;                        // Tab key: +1 next, -1 previous, consumed while drawing
    int want = -1;                          // id the page dots asked for
    bool busy() const { return prev >= 0; }
};

struct TrackPt { double t; float alt, az; };

struct TimerState {
    int mode = 0;                           // 0 timer, 1 stopwatch, 2 alarm
    double durationSec = 300;               // timer length
    bool running = false;
    double endAt = 0;                       // unix, when running
    double pausedRemaining = 300;           // when paused
    bool finished = false; double finishedAt = 0;
    // stopwatch
    bool swRunning = false; double swStart = 0, swElapsed = 0;
    std::vector<double> laps;
    // alarm
    int alarmMinutes = 7 * 60;              // minutes after local midnight
    bool alarmOn = false; bool ringing = false; double ringStart = 0;
    double alarmAt = 0;                     // the instant it rings; 0 = armed but not worked out yet (just loaded)
    int alarmTz = 0;                        // the UTC offset that was in force when alarmAt was worked out
    std::string entry;                      // digits typed so far
};

struct FocusState {
    double durationSec = 1500;              // 25m default Pomodoro focus session
    bool running = false;
    double endAt = 0;
    double pausedRemaining = 1500;
    bool finished = false; double finishedAt = 0;
    std::string entry;                      // digits typed so far on the Focus tab
};

constexpr double TIMER_PRESETS[7] = {60, 180, 300, 600, 900, 1800, 3600};
constexpr double FOCUS_PRESETS[7] = {300, 600, 900, 1500, 1800, 2700, 3600};
constexpr double TIMER_MIN_SEC = 1.0, TIMER_MAX_SEC = 99.0 * 3600 + 59 * 60 + 59;     // one length limit for every way of setting the countdown: typing, + and -, the mouse wheel and settings.ini

struct HitRect { float x, y, w, h; int id; };
struct EventHit { float x, y, w, h; int idx; bool round; };      // hover target for a sky event (screen coordinates)
struct HolHit { float x, y, w, h; int key; };                      // hover target for a holiday date (key = y*10000 + m*100 + d)
enum Cmd { CMD_TOPMOST = 1, CMD_FULLSCREEN, CMD_KEEPAWAKE, CMD_WEATHER_CFG, CMD_WEATHER_REFRESH, CMD_GEO, CMD_GEO_SOON, CMD_SAVE, CMD_OPEN_INI, CMD_OPEN_LOG, CMD_STORM_CFG, CMD_OPEN_THEMES, CMD_RESCAN_THEMES, CMD_STORM_REFRESH, CMD_LOCATE, CMD_OPEN_LOCATION_SETTINGS, CMD_OPEN_HOLIDAYS };
enum HitId { HIT_ACTION0 = 1000, HIT_FACE0 = 100, HIT_THEME = 200, HIT_THEME_PREV = 205, HIT_FULLSCREEN = 201, HIT_HELP = 202, HIT_PIN = 203, HIT_SETTINGS = 204,
             HIT_TAB0 = 300, HIT_PRESET0 = 320, HIT_START = 340, HIT_RESET = 341, HIT_LAP = 342,
             HIT_FPRESET0 = 360, HIT_FSTART = 370, HIT_FRESET = 371, HIT_FOCUSFX0 = 380,
             HIT_PACKFACE0 = 400 };          // 400..463: the dock buttons of pack tabs

struct SysInfo {
    int cpu = -1, ram = -1, battery = -1; bool charging = false, hasBattery = false; double uptimeSec = 0;
    double polledAt = -1e9; unsigned long long idle0 = 0, kern0 = 0, user0 = 0;
};

struct MoonDetail {
    double builtAt = -1e9;
    double prevNew = 0, nextNew = 0, nextFQ = 0, nextFull = 0, nextLQ = 0, prevFull = 0;
    double ageDays = 0;
    astro::RiseSet rs;                      // next moonrise / moonset after 'now'
    double riseAz = 0;                      // cached azimuth at next moonrise
};

struct PlanetRow { astro::PlanetInfo info; astro::RiseSet rs; };

class App {
public:
    Settings st;
    weather::Data wx;
    double wxFetchedAt = 0; bool wxOffline = false; std::string wxStatus, wxDetail;
    std::string holStatus; int holCount = 0;        // what holidays.txt said the last time it was read ("" = every line understood) and how many dates it gave
    std::vector<weather::ProviderStatus> wxProv = std::vector<weather::ProviderStatus>(weather::PV_COUNT);
    TimerState timer;
    FocusState focus;
    bool showHelp = false;
    bool showSettings = false;
    bool isSaver = false;
    bool wantRecreate = false;
    double lastItemsAt = -1e9;
    float dockT = 0, dockTarget = 0;        // auto-hide dock: 0 hidden .. 1 shown
    bool dockBusy() const { return std::fabs(dockT - dockTarget) > 0.004f; }
    float focusOrbT = 0, focusOrbTarget = 0; // Focus Studio collapsible orb -> expanded Ambience/Theme card: 0 orb .. 1 card
    bool focusOrbBusy() const { return std::fabs(focusOrbT - focusOrbTarget) > 0.004f; }

    // time
    double now = 0;
    int tzOff = 0;
    int (*tzAt)(double unix) = nullptr;     // UTC offset at any instant for the displayed clock (null when showing a saved place's fixed offset)
    int (*sysTzAt)(double unix) = nullptr;  // PC's real system timezone (always used for alarms so peeking at a saved place never shifts a desk alarm)
    double dt = 0.016;

    // input / UI
    float mouseX = -1, mouseY = -1;
    double lastMouseMove = -1e9;
    std::vector<HitRect> hits;
    std::wstring toast; double toastUntil = 0;
    double faceChangedAt = -1e9;
    float shiftX = 0, shiftY = 0;           // burn-in pixel shift currently applied to content

    // palette state
    gfx::Palette pal = gfx::PAL_NIGHT;      // what is drawn this frame; chases palTarget
    gfx::Palette palTarget = gfx::PAL_NIGHT;// where it is heading (theme, time of day and weather light)
    bool palInit = false, palMoving = false;

    // caches
    long long dayIndex = -1;
    int dayCacheTzOff = -999999;            // tracks timezone/DST offset so 2 AM DST transitions refresh day caches immediately
    astro::DayEvents dayEv, dayEvPrev;
    std::vector<today::TodayItem> todayItems;
    std::string dayLenText;
    double dayCacheGeoLat = 1e9, dayCacheGeoLon = 1e9;
    std::vector<float> bandSunAlt;          // 288 samples across the local day
    std::vector<unsigned char> bandMoonUp;
    double minuteCacheAt = -1e9;
    std::vector<TrackPt> sunTrack, moonTrack;
    astro::RiseSet sunNext;                 // next sunrise/sunset after now
    MoonDetail moonDetail;
    std::vector<PlanetRow> planets;
    double planetCacheAt = -1e9;
    astro::SunInfo sun;
    astro::MoonInfo moon;
    double eqTimeMin = 0;

    // immediate-mode controls (settings panel): handlers registered while drawing, run on click
    std::vector<std::function<void()>> actions;
    void addAction(float x, float y, float w, float h, std::function<void()> fn);
    // theme packs: numbers a click sets ("button" layers; key "packid/name" -> value and the time it was set), and the keys a pack face answers to this frame
    std::unordered_map<std::string, std::pair<double, double>> packState;
    std::vector<std::pair<int, std::function<void()>>> packKeys;
    bool packKey(int vk) { for (auto& k : packKeys) if (k.first == vk && k.second) { k.second(); return true; } return false; }
    std::function<void(int)> command;       // side effects that live in main (set by main)
    void cmd(int id) { if (command) command(id); }
    std::function<void()> textChanged;      // runs after the focused text field changes
    astro::Geo home;                        // the home location; st.geo is the place currently shown (home or a saved place)
    std::string homeCountry, homeRegion;    // the home location's country code ("LK") and region ("Scotland"); empty when not known (a place typed by hand)
    std::string sysCountry;                 // the country of the Windows region settings, the last resort for the holidays
    int placeIdx = -1;                      // -1: home
    // "find my location" (Settings > Place). The answer is kept in settings.ini until the button is pressed again.
    bool located = true;                    // false only on a new install that has not found its location yet: it then looks it up once at start
    bool locBusy = false;                   // a lookup is running
    std::string locSource;                  // "Windows location", "IP address", "set by hand" (empty: saved by an older version)
    double locAcc = -1, locAt = 0;          // how far off it may be (metres), and when it was found (unix seconds)
    std::string locErr;                     // why the last lookup failed
    std::string edName, edLat, edLon, addName, addLat, addLon, evField;   // edit buffers of the settings panel
    int evPage = 0;                         // which page of "Your dates" is showing in the settings panel
    bool edInit = false;
    int settingsTab = 0, widgetSel = -1;
    int settingsFocus = -1;                 // keyboard focus index in Settings actions (-1 = none; 0/1 are scrim & card backdrop)
    std::string packSel;                    // theme pack shown in detail on Settings > Themes
    int aboutSub = 0;                       // About tab: 0 the app, 1 the features and What's new
    float aboutScroll = 0, aboutScrollMax = 0;   // What's new: how far it is scrolled and how far it can go (in layout units, not pixels)
    std::string secretBuf;                  // invisible cheat-code typing buffer on Settings > About
    bool inAboutTab() const;
    int rotSub = 0;                         // Rotation tab: 0 Sky face, 1 Widgets face
    std::string* textTarget = nullptr;      // text field that currently receives typing
    bool textMulti = false; size_t textMax = 200; bool textNumeric = false;
    double textFocusAt = 0;
    textedit::State ted;                    // caret, selection and undo of the text field that has the keyboard
    size_t textScroll = 0;                  // how far that field is scrolled: its first character (one line) or first line (the note)
    std::vector<size_t> textLines;          // the note: where each line starts on screen (bytes), from the last frame; used by Up / Down / Home / End
    bool textClicked = false;               // a text field took the click that is being handled
    bool dblClick = false;                  // that click was a double click
    SysInfo sys;
    gfx::Bitmap phaseBmp[4];                // new / first quarter / full / last quarter glyphs for the Moon phases widget
    bool phaseBuilt = false;
    void pollSystem();
    void renderWidgets(gfx::Gfx& g, int w, int h);
    void renderSettings(gfx::Gfx& g, int w, int h);
    void settingsWheel(int delta);          // the mouse wheel in Settings: scrolls What's new (About tab)
    bool settingsKey(unsigned vk, bool shift = false);          // Tab/Space/Enter navigation + arrows/PageUp/PageDown scrolling in What's new

    // sky events (meteor showers, conjunctions, eclipses ...)
    std::vector<skyev::Event> events;
    double eventsAt = -1e9; double eventsLat = 1e9, eventsLon = 1e9; int eventsKey = -1;
    skyev::LunarShadow lunarShadow;         // current shadow on the Moon, active only during a lunar eclipse
    skyev::SolarCover solarCover;           // current Moon-over-Sun geometry
    bool eventsAnimating = false;           // meteors on screen: keep the frame rate up
    std::vector<EventHit> eventHits;
    int hoverEvent = -1; bool hoverOn = false; float hoverT = 0, hoverX = 0, hoverY = 0;
    bool eventsBusy() const { return eventsAnimating || std::fabs(hoverT - (hoverOn ? 1.f : 0.f)) > 0.004f; }
    void refreshEvents(bool force = false);

    // holidays: hover cards for dates on the calendar and rows of the holiday panel
    std::vector<HolHit> holHits;
    int holKey = -1; bool holOn = false; float holT = 0, holX = 0, holY = 0;
    bool holBusy() const { return std::fabs(holT - (holOn ? 1.f : 0.f)) > 0.004f; }
    int calOffset = 0;                      // months away from the current one on the month calendar

    // the rotating panel on the Sky face, and the pages of the Widgets face
    Rotor skyRot, wgtRot;
    bool panelBusy() const { return skyRot.busy() || wgtRot.busy(); }
    void panelNext(int dir = 1);            // jump to the next / previous panel (or widget page) now
    int wgPages = 0;                        // pages on the Widgets face (set while drawing it)
    // sky map orientation
    double skyHeadingCur = 0;               // heading in use this frame (follows the free rotation)
    double spinBase = 0, spinT0 = 0;
    double skyHeadingNow() const;
    void skyHeadingSet(double deg);         // face this direction (keeps spinning from there when free rotate is on)
    void skySpinChanged();                  // call after st.skySpin / speed / direction changed
    // weather on the Quiet / Sky / Focus Studio faces: eased strengths of each effect
    float qfx[7] = {0, 0, 0, 0, 0, 0, 0};   // clouds, wind, rain, tornado, thunder, fog, snow (eased)
    float qfxT[7] = {0, 0, 0, 0, 0, 0, 0};  // what the weather asks for right now
    float gloom = 0, haze = 0;              // the light under weather, 0..1: heavy cloud or storm, and fog (drives moodPalette)
    int spriteBudget = 2;                   // cached weather images (re)drawn per frame at most, so a colour change never causes one long frame
    bool weatherBusy() const { for (int i = 0; i < 7; ++i) if (std::fabs(qfx[i] - qfxT[i]) > 0.004f) return true; return false; }
    float fxQuality = 1.f, fxLate = 1.f;    // 0.3..1: how much detail the weather draws; lowered automatically if frames arrive late
    // Monotonic weather clocks so a new forecast or window resize never causes a jump.
    double fxCloudT = 0, fxFrontT = 0, fxFogT = 0, fxWindT = 0, fxSnowT = 0;
    float fxWk = 0;                         // wind speed (km/h) eased toward the forecast
    // wind, rain, snow, thunder and the tornado get smooth frame pacing
    int quietFps() const { return (qfx[1] + qfx[2] + qfx[3] + qfx[4] + qfx[6] > 0.05f) ? (st.quietSmooth ? 60 : 30) : 15; }
    double alienAt = -1e9;                 // when the saucer was called with the U key (Midnight)
    uint64_t alienSeq = 0;                 // increments each time U calls the saucer, so stereo audio restarts cleanly
    bool aliensNuts = false;               // hidden "aliensarenuts" mode: saucer flies twice every minute (stopped by "aliennohere")
    double aliensNutsSince = 0;            // anchor timestamp for the 2x/min saucer cycle
    bool spaceShipDrew = false;            // true on frames where the saucer is actively drawn on screen
    float meteorDemoU = -1.f;              // dev --shot preview only: pins shooting stars at progress u in (0..1)
    double frameTarget = 0;                // seconds between frames the loop is aiming for right now (0: use quietFps)
    // the screen saver's graceful start (reveal.h). main.cpp arms it for a saver start only; render() runs it. revealOverride >= 0 pins it at that
    // point (0 to 1) for test pictures.
    bool revealArmed = false; int revealFrames = 0; double revealStart = 0; float revealOverride = -1.f;
    // ... and its graceful end (the same picture played backwards, 0.6 seconds): beginLeave() is called when the keyboard or mouse is used; leaveDone
    // turns true on the last (black) frame, and main.cpp then lets the window go. leaveOverride pins it at that point (0 to 1) for test pictures.
    bool leaveArmed = false, leaveDone = false; double leaveStart = 0, leaveFrom = 1.0; float leaveOverride = -1.f; double revealP = -1.0;
    void beginLeave() { if (leaveArmed) return; leaveArmed = true; leaveDone = false; leaveStart = -1.0; leaveFrom = (revealArmed && revealP >= 0.0 && revealP < 1.0) ? revealP : 1.0; revealArmed = false; }
    bool revealBusy() const { return revealArmed || leaveArmed; }
    bool focusFxBusy() const { return st.face == FACE_FOCUS && st.focusFx > 0; }
    bool quietFxBusy() const { return (st.face == FACE_QUIET && st.quietFx && (qfx[0] + qfx[1] + qfx[2] + qfx[3] + qfx[4] + qfx[5] + qfx[6] > 0.01f)) || skyRainBusy() || focusFxBusy(); }
    bool skyRainBusy() const { return st.face == FACE_SKY && st.quietFx && st.skyRain && (qfx[2] > 0.01f || qfx[6] > 0.01f); }
    bool isDstActive() const;               // true ONLY in timezones that observe DST while DST is currently in effect (always false in Sri Lanka, etc.)
    // named storms: everything active in the world, and the ones inside the chosen radius (nearest first)
    storms::Data stormData;
    double stormTry = 0, stormOk = 0;       // when the storm feed was last asked, and when it last answered (for Settings)
    std::string stormErr;                   // why the last try failed (empty when it worked)
    bool stormBusy = false;                 // a download is running right now
    std::vector<storms::Storm> stormNear;
    std::string stormSig;
    std::vector<today::StormLine> stormLines() const;
    void stormsChanged();                   // call after stormData, the place or the radius changed
    bool animatingOther() const { return (st.face == FACE_SKY && st.skySpin) || (st.face == FACE_WIDGETS && wgtRot.busy()); }
    bool animating() const { return animatingOther() || quietFxBusy() || packFps() > 0 || packBusy(); }
    std::string edHeading;                  // edit buffer of the heading field
    skyev::Event nextEvent; bool hasNextEvent = false; double nextEventAt = -1e9; double nextEventKey = 0;   // closest event beyond the window, for the empty state

    // moon sprites
    gfx::Bitmap moonSmall, moonLarge;
    long long moonSmallKey = -1, moonLargeKey = -1;

    void setNow(double t, int tz, double frameDt);
    void update(gfx::Gfx& g);
    void render(gfx::Gfx& g, int w, int h);

    // helpers used by the scene and by main
    gfx::Palette basePalette() const;       // the theme (or the time of day for Auto), before the weather

    // ---- theme packs (themes.h). A pack is a theme (palette, fonts, a scene behind the faces) and may add tabs.
    bool packHoldsColours() const;                                     // a theme pack is on show and does not let the weather tint it (the default for a pack)
    themes::Pack* packTheme() const;                                   // the pack chosen as the theme, when it is usable right now
    bool packFace(themes::Pack*& p, themes::Face*& f) const;           // the pack tab on show, when there is one
    std::vector<themes::Pack*> livePacks() const;                      // usable packs (each may add tabs to the dock)
    std::vector<themes::Pack*> globalPacks() const;                    // the usable packs that can also be chosen as a theme for every face, in the order T cycles them
    std::vector<std::string> packFaceKeys;                             // "pack/face" of each pack button in the dock (rebuilt every frame)
    std::string backdropId; float backdropT = 0;                       // the pack whose backdrop is drawn, and how visible it is (it fades when the theme changes)
    double themeScanAt = -1e9;
    int packFps() const;                                               // frames a second the pack scene on show needs (0: nothing moves)
    bool packBusy() const { return std::fabs(backdropT - (backdropId.empty() || packTheme() == nullptr ? 0.f : 1.f)) > 0.004f; }
    void syncPacks();                                                  // applies the "switched off" list and drops a selection that is gone
    void rescanPacks(gfx::Gfx& g);                                     // read the themes folders again
    gfx::Palette targetPalette() const;     // basePalette with the weather's light on it
    int effectiveThemeForDisplay() const;
    const char* themeName() const;
    void setToast(const std::wstring& t, double secs = 2.2) { toast = t; toastUntil = now + secs; }
    double sunAltNow() const { return sun.hor.alt; }
    // The Brightness setting; with "Dim with the sun" on, eased down from sunset (0 degrees) to the end of twilight (12 degrees below).
    float effectiveBrightness() const {
        if (!st.autoDim) return st.brightness;
        const float t = (float)std::max(0.0, std::min(1.0, -sun.hor.alt / 12.0));
        const float e = t * t * (3.f - 2.f * t);
        return st.brightness * (1.f - e * (1.f - st.dimNight));
    }
    void releaseDeviceResources(gfx::Gfx& g);

    // timer logic (main drives ticks)
    double timerRemaining() const;
    double stopwatchElapsed() const;
    void timerTick();                       // detects completion / alarm
    void timerStartPause();
    void timerReset();
    void timerAdjust(double deltaSec);
    void timerSetDuration(double sec);
    void timerDigit(int d);
    void timerBackspace();
    void stopwatchLap();
    void alarmApplyEntry();
    // focus session logic (independent from Timer tab)
    double focusRemaining() const;
    void focusStartPause();
    void focusReset();
    void focusAdjust(double deltaSec);
    void focusSetDuration(double sec);
    void focusDigit(int d);
    void focusBackspace();

private:
    void refreshDay();
    void refreshMinute();
    void refreshPlanets();
    void refreshMoonSprites(gfx::Gfx& g);
    void renderSky(gfx::Gfx& g, int w, int h);
    void renderQuiet(gfx::Gfx& g, int w, int h);
    void renderTimer(gfx::Gfx& g, int w, int h);
    void renderFocus(gfx::Gfx& g, int w, int h);
    void renderAlmanac(gfx::Gfx& g, int w, int h);
    void renderDock(gfx::Gfx& g, int w, int h);
    void renderOverlays(gfx::Gfx& g, int w, int h);
    float layoutScale(int w, int h) const;
};

extern int gSpriteBuilds;                   // how many cached weather images were drawn so far (for timing runs)
void quietFxRelease(gfx::Gfx& g);            // frees the cached cloud images (device lost, or the window target was rebuilt)
void spaceRelease(gfx::Gfx& g);               // frees the Midnight and Sunlit II scenery images (scene_space.cpp)
void quietFxStep(App& a);                   // eases the Quiet-face weather effects toward what the weather asks for (scene_quiet.cpp)

// azimuth that points to the top of the sky map this frame (set by App::update); the dome drawing code reads it
extern float gSkyHeading;

// orientation of celestial north for the moon sprite: on the dome map, and in the observer's own view
float moonDomeRotation(const astro::MoonInfo& m, const astro::Geo& g);
float moonViewRotation(const astro::MoonInfo& m, const astro::Geo& g);

// shared drawing helpers
struct Ctx {
    gfx::Gfx& g;
    App& a;
    const gfx::Palette& P;
    float W, H, s;                          // s = H / 1080
    float alpha = 1.f;                      // extra fade for colours that do not come from the palette (the sliding panel)
};

} // namespace app
