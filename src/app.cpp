// Meridian: state, caches, palette selection and timer logic.
#include "app.h"
#include <cmath>
#include <algorithm>

using namespace astro;
using gfx::smoothstep;

namespace app {

float gSkyHeading = 0.f;

void App::setNow(double t, int tz, double frameDt) { now = t; tzOff = tz; dt = frameDt; }

bool App::isDstActive() const {
    if (!tzAt) return false;
    util::LocalTime L = util::toLocal(now, tzOff);
    const double tJan = astro::daysFromCivil(L.y, 1, 15) * 86400.0 + 43200.0;
    const double tJul = astro::daysFromCivil(L.y, 7, 15) * 86400.0 + 43200.0;
    const int oJan = tzAt(tJan), oJul = tzAt(tJul);
    if (oJan == oJul) return false;                     // non-DST timezone (e.g. Sri Lanka, India, Japan): never show DST
    return tzOff > std::min(oJan, oJul);
}

// ---------------------------------------------------------------- sky map orientation
double App::skyHeadingNow() const {
    if (!st.skySpin) return astro::norm360((double)st.skyHeading);
    return astro::norm360(spinBase + (double)st.skySpinDir * 360.0 * (now - spinT0) / (double)std::max(5, st.skySpinSecs));
}
void App::skyHeadingSet(double deg) {
    deg = astro::norm360(deg);
    st.skyHeading = (float)deg;
    spinBase = deg; spinT0 = now;
    skyHeadingCur = deg; gSkyHeading = (float)deg;
}
void App::skySpinChanged() {
    // continue from where the map is now: switching on starts a turn from the current heading, switching off freezes it there
    if (st.skySpin) { spinBase = skyHeadingCur; spinT0 = now; }
    else { st.skyHeading = (float)std::round(skyHeadingCur); skyHeadingCur = st.skyHeading; gSkyHeading = st.skyHeading; }
}

const char* App::themeName() const {
    static const char* n[THEME_COUNT] = {"Auto", "Day", "Dusk", "Night", "Ember", "Dawn", "Sunset", "Sunlit", "Sunlit II", "Midnight"};
    if (themes::Pack* p = packTheme()) return p->name.c_str();                      // a theme pack is in use: its name
    return n[st.theme % THEME_COUNT];
}

int App::effectiveThemeForDisplay() const { return st.theme; }

gfx::Palette App::targetPalette() const { return gfx::moodPalette(basePalette(), gloom, haze); }

gfx::Palette App::basePalette() const {
    using namespace gfx;
    util::LocalTime L = util::toLocal(now, tzOff);
    bool bed = false;
    if (st.emberAtNight) {
        int h = L.hh;
        bed = st.emberFrom > st.emberTo ? (h >= st.emberFrom || h < st.emberTo) : (h >= st.emberFrom && h < st.emberTo);
    }
    {   // a theme pack: its palette. A pack's own tab always wears that pack's colours, whichever theme is chosen
        themes::Pack* p = nullptr; themes::Face* f = nullptr;
        if (st.face == FACE_PACK && packFace(p, f)) return p->pal;
        if ((p = packTheme())) return p->pal;
    }
    switch (st.theme) {
        case THEME_DAY: return PAL_DAY;
        case THEME_DUSK: return PAL_DUSK;
        case THEME_DAWN: return PAL_DAWN;
        case THEME_SUNSET: return PAL_SUNSET;
        case THEME_SUNLIT: return PAL_SUNLIT;
        case THEME_SUNLIT2: return PAL_SUNLIT2;
        case THEME_MIDNIGHT: return PAL_MIDNIGHT;
        case THEME_NIGHT: return bed ? PAL_EMBER : PAL_NIGHT;
        case THEME_EMBER: return PAL_EMBER;
        default: break;
    }
    if (bed) { Palette e = PAL_EMBER; e.skyOwn = 0; return e; }                       // Auto: the sky map keeps following the real Sun
    float alt = (float)sun.hor.alt;
    float t1 = smoothstep(14.f, 2.f, alt);
    float t2 = smoothstep(-2.f, -12.f, alt);
    // a low Sun in the east is a dawn, a low Sun in the west is a dusk
    const bool morning = sun.hor.az < 180.0;
    Palette p = lerpPalette(lerpPalette(PAL_DAY, morning ? PAL_DAWN : PAL_DUSK, t1), PAL_NIGHT, t2);
    p.skyOwn = 0;                                           // Auto: the sky map is painted from the real Sun, not from the theme's colours
    return p;
}

// ---------------------------------------------------------------- caches
void App::refreshDay() {
    util::LocalTime L = util::toLocal(now, tzOff);
    double d0 = util::localMidnight(now, tzOff);
    dayIndex = L.dayIndex;
    dayCacheTzOff = tzOff;
    dayCacheGeoLat = st.geo.lat; dayCacheGeoLon = st.geo.lon;
    dayEv = dayEvents(d0, st.geo);
    dayEvPrev = dayEvents(d0 - 86400.0, st.geo);
    today::Input in; in.now = now; in.tzOff = tzOff; in.tzAt = tzAt; in.geo = st.geo; in.h24 = st.h24; in.poya = st.poya; in.omitMoonFill = true; in.custom = st.events; in.storms = stormLines();
    todayItems = today::buildToday(in);
    dayLenText = today::dayLengthText(in);
    bandSunAlt.assign(288, -90.f);
    bandMoonUp.assign(288, 0);
    for (int i = 0; i < 288; ++i) {
        double t = d0 + i * 300.0 + 150.0;
        bandSunAlt[i] = (float)sunInfo(t, st.geo).hor.alt;
        bandMoonUp[i] = moonInfo(t, st.geo).hor.alt > -0.8 ? 1 : 0;
    }
}

void App::refreshMinute() {
    minuteCacheAt = now;
    double d0 = util::localMidnight(now, tzOff);
    sunTrack.clear(); moonTrack.clear();
    for (double t = d0; t <= d0 + 86400.0; t += 600.0) {
        SunInfo s = sunInfo(t, st.geo);
        sunTrack.push_back({t, (float)s.appAlt, (float)s.hor.az});
    }
    for (double t = now - 6 * 3600.0; t <= now + 20 * 3600.0; t += 600.0) {
        MoonInfo m = moonInfo(t, st.geo);
        moonTrack.push_back({t, (float)m.appAlt, (float)m.hor.az});
    }
    sunNext = sunRiseSet(now, now + 26 * 3600.0, st.geo);
    const bool moonPast = (moonDetail.rs.hasRise && moonDetail.rs.rise <= now) || (moonDetail.rs.hasSet && moonDetail.rs.set <= now);
    if (now - moonDetail.builtAt > 300.0 || now < moonDetail.builtAt || moonPast) {
        MoonDetail& d = moonDetail;
        d.builtAt = now;
        d.prevNew = prevPhaseTime(now, 0); d.prevFull = prevPhaseTime(now, 2);
        d.nextNew = nextPhaseTime(now, 0); d.nextFQ = nextPhaseTime(now, 1);
        d.nextFull = nextPhaseTime(now, 2); d.nextLQ = nextPhaseTime(now, 3);
        d.ageDays = (now - d.prevNew) / 86400.0;
        d.rs = moonRiseSet(now, now + 30 * 3600.0, st.geo);
        d.riseAz = d.rs.hasRise ? moonInfo(d.rs.rise, st.geo).hor.az : 0.0;
    }
    eqTimeMin = equationOfTimeMinutes(now);
    // keep today's list fresh through the day (conjunctions and "tomorrow" items change)
    if (now - lastItemsAt > 1800.0 || now < lastItemsAt) {
        lastItemsAt = now;
        today::Input in; in.now = now; in.tzOff = tzOff; in.tzAt = tzAt; in.geo = st.geo; in.h24 = st.h24; in.poya = st.poya; in.omitMoonFill = true; in.custom = st.events; in.storms = stormLines();
        todayItems = today::buildToday(in);
    }
}

std::vector<today::StormLine> App::stormLines() const {
    std::vector<today::StormLine> v;
    if (!st.stormWatch) return v;
    for (const auto& s : stormNear) {
        std::string d = storms::where(s) + " of " + (st.geo.name.empty() ? std::string("you") : st.geo.name);
        const std::string w = storms::winds(s); if (!w.empty()) d += " \xC2\xB7 " + w;
        v.push_back({storms::title(s), d});
    }
    return v;
}

// recompute which storms are close enough, expiring stale advisories (>72 h) during offline outages, and refresh the Today list when changed
void App::stormsChanged() {
    stormNear.clear();
    if (st.stormWatch && stormData.valid) {
        const bool fetchedStale = (stormData.fetched > 0 && now > 0 && now - stormData.fetched > 72 * 3600.0);
        if (!fetchedStale) {
            std::vector<storms::Storm> all;
            all.reserve(stormData.list.size());
            for (const auto& s : stormData.list) {
                if (s.updated > 0 && now > 0 && now - s.updated > 72 * 3600.0) continue;
                all.push_back(s);
            }
            storms::annotate(all, st.geo.lat, st.geo.lon);
            stormNear = storms::within(all, st.stormRadius);
        }
    }
    std::string sig;
    for (const auto& s : stormNear) sig += storms::title(s) + util::fmt("|%.0f|%.0f;", std::round(s.distKm / 10.0), std::round(s.windKmh / 5.0));
    if (sig == stormSig) return;
    stormSig = sig;
    today::Input in; in.now = now; in.tzOff = tzOff; in.tzAt = tzAt; in.geo = st.geo; in.h24 = st.h24; in.poya = st.poya; in.omitMoonFill = true; in.custom = st.events; in.storms = stormLines();
    todayItems = today::buildToday(in);
}

void App::refreshPlanets() {
    planetCacheAt = now;
    planets.clear();
    for (int p = 0; p < PLANET_COUNT; ++p) {
        PlanetRow r;
        r.info = planetInfo(p, now, st.geo);
        std::vector<double> ris, set;
        findCrossings([&](double t) { return planetInfo(p, t, st.geo).hor.alt + 0.5667; }, now - 3600.0, now + 24 * 3600.0, 600.0, ris, set);
        for (double t : ris) if (t > now) { r.rs.hasRise = true; r.rs.rise = t; break; }
        for (double t : set) if (t > now) { r.rs.hasSet = true; r.rs.set = t; break; }
        planets.push_back(r);
    }
}

// direction of celestial north on the dome at a given sky position, as a rotation in degrees (clockwise from screen up)
static float northRotationOnDome(double alt, double az, const Geo& g) {
    auto vec = [](double a, double z, double out[3]) {
        out[0] = std::cos(rad(a)) * std::cos(rad(z)); out[1] = std::cos(rad(a)) * std::sin(rad(z)); out[2] = std::sin(rad(a));
    };
    double m[3], n[3]; vec(alt, az, m); vec(g.lat, 0.0, n);
    double d = n[0] * m[0] + n[1] * m[1] + n[2] * m[2];
    double t[3] = {n[0] - d * m[0], n[1] - d * m[1], n[2] - d * m[2]};
    double l = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
    if (l < 1e-9) return 0.f;
    for (double& x : t) x /= l;
    // step 1 degree toward the pole, project both points to the dome plane (E left, N up)
    double eps = rad(1.0);
    double p[3] = {m[0] * std::cos(eps) + t[0] * std::sin(eps), m[1] * std::cos(eps) + t[1] * std::sin(eps), m[2] * std::cos(eps) + t[2] * std::sin(eps)};
    auto proj = [](const double v[3], double& x, double& y) {
        double alt2 = std::asin(std::max(-1.0, std::min(1.0, v[2]))), az2 = std::atan2(v[1], v[0]);
        double r = (3.14159265358979 / 2 - alt2);
        x = r * std::sin(az2); y = -r * std::cos(az2);
    };
    double x0, y0, x1, y1; proj(m, x0, y0); proj(p, x1, y1);
    return (float)deg(std::atan2(x1 - x0, -(y1 - y0)));
}

// orientation of celestial north in the observer's view (up = zenith, right = increasing azimuth)
static float northRotationInView(double alt, double az, const Geo& g) {
    double ca = std::cos(rad(alt)), sa = std::sin(rad(alt)), cz = std::cos(rad(az)), sz = std::sin(rad(az));
    double m[3] = {ca * cz, ca * sz, sa};
    double n[3] = {std::cos(rad(g.lat)), 0.0, std::sin(rad(g.lat))};
    double d = n[0] * m[0] + n[1] * m[1] + n[2] * m[2];
    double t[3] = {n[0] - d * m[0], n[1] - d * m[1], n[2] - d * m[2]};
    double up[3] = {-sa * cz, -sa * sz, ca}, right[3] = {-sz, cz, 0.0};
    double tx = t[0] * right[0] + t[1] * right[1] + t[2] * right[2];
    double ty = t[0] * up[0] + t[1] * up[1] + t[2] * up[2];
    return (float)deg(std::atan2(tx, ty));
}

float moonDomeRotation(const MoonInfo& m, const Geo& g) { return northRotationOnDome(m.hor.alt, m.hor.az, g) - gSkyHeading; }   // compass projection: turning heading rotates the dome counter-clockwise
float moonViewRotation(const MoonInfo& m, const Geo& g) { return northRotationInView(m.hor.alt, m.hor.az, g); }

void App::refreshMoonSprites(gfx::Gfx& g) {
    if (!moonsprite::ready()) return;                // lunar map is still being generated
    int esLevel = (int)std::lround(smoothstep(0.f, -12.f, (float)sun.hor.alt) * 6.f);
    long long base = ((long long)std::lround(moon.phaseAngle * 2.0) * 1000 + (long long)std::lround(moon.brightLimbPA)) * 100
                     + esLevel * 10 + (int)std::lround(pal.ember * 4.f);
    // Earth's shadow on the Moon (lunar eclipse): quantised so the sprite is only rebuilt when it visibly moves
    const bool sh = lunarShadow.active;
    const double mr = std::max(1e-6, lunarShadow.moonR);
    const double shx = sh ? -lunarShadow.dx / mr : 0.0, shy = sh ? lunarShadow.dy / mr : 0.0;     // sprite frame: east is left
    const long long shKey = sh ? 1 + (std::llround(shx * 25.0) + 400) * 1000 + (std::llround(shy * 25.0) + 400) : 0;
    auto build = [&](gfx::Bitmap& bm, long long& key, int size) {
        long long k = (base * 4 + (size > 300 ? 1 : 0)) * 1000000LL + shKey;
        if (k == key && bm.bmp) return;
        moonsprite::Params p; p.size = size; p.phaseAngleDeg = moon.phaseAngle; p.brightLimbPA = moon.brightLimbPA;
        p.earthshine = 0.065 * esLevel / 6.0;
        if (sh) { p.shadow = true; p.shX = std::round(shx * 25.0) / 25.0; p.shY = std::round(shy * 25.0) / 25.0; p.umbraR = lunarShadow.umbra / mr; p.penumbraR = lunarShadow.penumbra / mr; }
        if (pal.ember > 0.01f) {
            gfx::Color t = gfx::emberize({1.0f, 0.985f, 0.94f, 1.f}, pal.ember);
            p.tint[0] = t.r; p.tint[1] = t.g; p.tint[2] = t.b;
            p.tint[0] *= 0.8f;
        }
        std::vector<uint8_t> px; moonsprite::render(p, px);
        g.freeBitmap(bm);
        bm = g.makeBitmap(px.data(), size, size);
        key = k;
    };
    build(moonSmall, moonSmallKey, 160);
    if (st.face == FACE_ALMANAC) build(moonLarge, moonLargeKey, 512);
}

void App::releaseDeviceResources(gfx::Gfx& g) {
    quietFxRelease(g);
    spaceRelease(g);
    themes::release(g);
    g.freeBitmap(moonSmall); g.freeBitmap(moonLarge);
    moonSmallKey = moonLargeKey = -1;
    for (auto& b : phaseBmp) g.freeBitmap(b);
    phaseBuilt = false;
}

void App::refreshEvents(bool force) {
    const int key = (st.evMeteors ? 1 : 0) | (st.evPlanets ? 2 : 0) | (st.evEclipses ? 4 : 0) | (st.evDays << 3);
    if (!force && key == eventsKey && now >= eventsAt && now - eventsAt < 600.0 &&
        std::fabs(st.geo.lat - eventsLat) < 1e-9 && std::fabs(st.geo.lon - eventsLon) < 1e-9) return;
    eventsAt = now; eventsKey = key; eventsLat = st.geo.lat; eventsLon = st.geo.lon;
    skyev::Options o; o.meteors = st.evMeteors; o.planets = st.evPlanets; o.eclipses = st.evEclipses; o.windowDays = std::max(1, std::min(30, st.evDays));
    events = skyev::find(now, st.geo, o);
    hoverEvent = -1;
    hasNextEvent = false;
    if (events.empty() && st.elEvents) {                         // for the empty state of the Sky events panel: what comes next
        skyev::Options o2 = o; o2.windowDays = 365;
        for (const auto& e : skyev::find(now, st.geo, o2)) if (e.tPeak > now) { nextEvent = e; hasNextEvent = true; break; }
    }
}

// ---------------------------------------------------------------- theme packs
themes::Pack* App::packTheme() const {
    if (st.themePack.empty()) return nullptr;
    themes::Pack* p = themes::find(st.themePack);
    return p && p->global && p->live(now, tzOff) ? p : nullptr;       // only a pack that says "global": true can theme the other faces
}
bool App::packFace(themes::Pack*& p, themes::Face*& f) const {
    if (st.facePack.empty()) return false;
    return themes::findFace(st.facePack, p, f) && p->live(now, tzOff);
}
std::vector<themes::Pack*> App::livePacks() const {
    std::vector<themes::Pack*> v;
    for (auto& p : themes::all()) if (p->live(now, tzOff)) v.push_back(p.get());
    return v;
}
std::vector<themes::Pack*> App::globalPacks() const {
    std::vector<themes::Pack*> v;
    for (themes::Pack* p : livePacks()) if (p->global) v.push_back(p);
    return v;
}
// Same choice as basePalette(): the pack tab on show first, else the pack chosen as the theme.
bool App::packHoldsColours() const {
    themes::Pack* p = nullptr; themes::Face* f = nullptr;
    if (st.face == FACE_PACK && packFace(p, f)) return !p->weather;
    if ((p = packTheme())) return !p->weather;
    return false;
}
int App::packFps() const {
    themes::Pack* p = nullptr; themes::Face* f = nullptr;
    if (st.face == FACE_PACK) return packFace(p, f) ? f->scene.fps : 0;
    if (!backdropId.empty() && backdropT > 0.002f && (p = themes::find(backdropId))) return st.face == FACE_QUIET ? p->backdrop.fps : p->backdropLite.fps;
    return 0;
}
void App::syncPacks() {
    for (auto& p : themes::all()) p->disabled = std::find(st.packsOff.begin(), st.packsOff.end(), p->id) != st.packsOff.end();
    if (!st.themePack.empty() && !packTheme()) st.themePack.clear();                      // gone, switched off, or past its date: back to a built-in theme
    if (st.face == FACE_PACK) { themes::Pack* p; themes::Face* f; if (!packFace(p, f)) { st.face = FACE_QUIET; faceChangedAt = now; } }
}
void App::rescanPacks(gfx::Gfx& g) { themes::rescan(&g); syncPacks(); }

void App::update(gfx::Gfx& g) {
    // theme packs: pick up added, removed or edited packs; drop a choice that is gone or past its 'expires' date; keep the pack's fonts in use
    if (now - themeScanAt >= 4.0 || now < themeScanAt) {
        themeScanAt = now;
        if (themes::changed()) rescanPacks(g);
    }
    syncPacks();
    {
        themes::Pack* fp = nullptr; themes::Face* ff = nullptr;
        themes::Pack* want = (st.face == FACE_PACK && packFace(fp, ff)) ? fp : packTheme();
        themes::syncFonts(g, want);
        // the backdrop fades out, the pack changes, then it fades in (so a switch is never a jump)
        const std::string wantId = packTheme() ? packTheme()->id : std::string();
        if (!palInit) { backdropId = wantId; backdropT = wantId.empty() ? 0.f : 1.f; }
        else if (wantId != backdropId) { backdropT = std::max(0.f, backdropT - (float)dt * 5.f); if (backdropT <= 0.002f) { backdropId = wantId; backdropT = 0.f; } }
        else if (!wantId.empty()) backdropT = std::min(1.f, backdropT + (float)dt * 4.f);
    }
    skyHeadingCur = skyHeadingNow(); gSkyHeading = (float)skyHeadingCur;
    quietFxStep(*this);
    sun = sunInfo(now, st.geo);
    moon = moonInfo(now, st.geo);
    refreshEvents();
    // geometry of an eclipse that is happening right now (drives the Moon sprite and the Sun inset)
    lunarShadow = skyev::LunarShadow(); solarCover = skyev::SolarCover();
    for (const auto& e : events) {
        if (!e.active(now)) continue;
        if (e.kind == skyev::LUNAR_ECLIPSE) lunarShadow = skyev::lunarShadowAt(now, st.geo);
        else if (e.kind == skyev::SOLAR_ECLIPSE && e.visible) solarCover = skyev::solarCoverAt(now, st.geo);
    }
    palTarget = targetPalette();
    if (!palInit) { pal = palTarget; palInit = true; }
    else pal = gfx::lerpPalette(pal, palTarget, (float)(1.0 - std::exp(-dt * 4.5)));
    {   // still on its way? (the frame scheduler keeps a steady rate until it arrives)
        auto d = [](const gfx::Color& x, const gfx::Color& y) { return std::fabs(x.r - y.r) + std::fabs(x.g - y.g) + std::fabs(x.b - y.b) + std::fabs(x.a - y.a); };
        palMoving = d(pal.bgTop, palTarget.bgTop) + d(pal.bgBottom, palTarget.bgBottom) + d(pal.ink, palTarget.ink) + d(pal.accent, palTarget.accent) + std::fabs(pal.neon - palTarget.neon) >= 0.006f;
    }
    util::LocalTime L = util::toLocal(now, tzOff);
    if (L.dayIndex != dayIndex || tzOff != dayCacheTzOff || std::fabs(st.geo.lat - dayCacheGeoLat) > 1e-9 || std::fabs(st.geo.lon - dayCacheGeoLon) > 1e-9) { refreshDay(); minuteCacheAt = -1e9; planetCacheAt = -1e9; moonDetail.builtAt = -1e9; }
    if (now - minuteCacheAt >= 20.0 || now < minuteCacheAt) refreshMinute();
    if (now - planetCacheAt >= 60.0 || now < planetCacheAt) refreshPlanets();
    refreshMoonSprites(g);
}

// ---------------------------------------------------------------- timer / stopwatch / alarm
double App::timerRemaining() const {
    if (timer.running) return std::max(0.0, timer.endAt - now);
    return timer.pausedRemaining;
}
double App::stopwatchElapsed() const { return timer.swElapsed + (timer.swRunning ? now - timer.swStart : 0.0); }

// The alarm is a time on the PC's own wall clock ("07:00"), so it always uses the system timezone's real daylight-saving rules
// (even when viewing a saved place in another timezone with placeLocalTime enabled).
static void armAlarm(App& a) {
    auto off = [&a](double t) -> int { return a.sysTzAt ? a.sysTzAt(t) : (a.tzAt ? a.tzAt(t) : a.tzOff); };
    a.timer.alarmAt = util::nextLocalClockTime(a.now, a.timer.alarmMinutes, off);
    a.timer.alarmTz = off(a.now);
}

void App::timerTick() {
    if (timer.running && now >= timer.endAt) {
        timer.running = false; timer.finished = true; timer.finishedAt = now; timer.pausedRemaining = 0;
    }
    if (focus.running && now >= focus.endAt) {
        focus.running = false; focus.finished = true; focus.finishedAt = now; focus.pausedRemaining = 0;
    }
    if (!isSaver) {
        const int curAlarmTz = sysTzAt ? sysTzAt(now) : (tzAt ? tzAt(now) : tzOff);
        if (timer.alarmOn && timer.alarmAt <= 0) armAlarm(*this);                        // switched on in the saved settings: work out when it rings
        else if (timer.alarmOn && now < timer.alarmAt && timer.alarmTz != curAlarmTz) armAlarm(*this);   // the zone or daylight saving changed: same wall-clock time
        if (timer.alarmOn && now >= timer.alarmAt) {
            timer.alarmOn = false; timer.ringing = true; timer.ringStart = now;
        }
    }
}

void App::timerStartPause() {
    TimerState& t = timer;
    if (t.mode == 0) {
        if (t.finished) { t.finished = false; t.pausedRemaining = t.durationSec; return; }
        if (t.running) { t.pausedRemaining = std::max(0.0, t.endAt - now); t.running = false; }
        else { if (t.pausedRemaining <= 0) t.pausedRemaining = t.durationSec; t.endAt = now + t.pausedRemaining; t.running = true; t.entry.clear(); }
    } else if (t.mode == 1) {
        if (t.swRunning) { t.swElapsed += now - t.swStart; t.swRunning = false; }
        else { t.swStart = now; t.swRunning = true; }
    } else {
        if (t.ringing) { t.ringing = false; return; }
        t.alarmOn = !t.alarmOn; t.entry.clear();
        if (t.alarmOn) armAlarm(*this);
    }
}

void App::timerReset() {
    TimerState& t = timer;
    if (t.mode == 0) { t.running = false; t.finished = false; t.pausedRemaining = t.durationSec; t.entry.clear(); }
    else if (t.mode == 1) { t.swRunning = false; t.swElapsed = 0; t.laps.clear(); }
    else { t.alarmOn = false; t.ringing = false; t.entry.clear(); }
}

void App::timerSetDuration(double sec) {
    TimerState& t = timer;
    t.durationSec = std::max(TIMER_MIN_SEC, std::min(sec, TIMER_MAX_SEC));
    t.finished = false;
    if (t.running) t.endAt = now + t.durationSec; else t.pausedRemaining = t.durationSec;
}

void App::timerAdjust(double delta) {
    TimerState& t = timer;
    if (t.mode == 0) {
        double nd = std::max(TIMER_MIN_SEC, std::min(t.durationSec + delta, TIMER_MAX_SEC));
        double diff = nd - t.durationSec;
        t.durationSec = nd; t.finished = false;
        if (t.running) t.endAt = std::max(now + TIMER_MIN_SEC, std::min(now + TIMER_MAX_SEC, t.endAt + diff));
        else t.pausedRemaining = std::max(TIMER_MIN_SEC, std::min(TIMER_MAX_SEC, t.pausedRemaining + diff));
        t.entry.clear();
    } else if (t.mode == 2) {
        int dm = (int)std::lround(delta / 60.0);
        t.alarmMinutes = ((t.alarmMinutes + dm) % 1440 + 1440) % 1440;
        if (t.alarmOn) armAlarm(*this);
        t.entry.clear();
    }
}

void App::alarmApplyEntry() {
    TimerState& t = timer;
    std::string e = t.entry; while (e.size() < 4) e = "0" + e;
    int hh = std::atoi(e.substr(e.size() - 4, 2).c_str()), mm = std::atoi(e.substr(e.size() - 2, 2).c_str());
    hh = std::min(hh, 23); mm = std::min(mm, 59);
    t.alarmMinutes = hh * 60 + mm;
    if (t.alarmOn) armAlarm(*this);
}

void App::timerDigit(int d) {
    TimerState& t = timer;
    if (t.mode == 1) return;
    if (t.entry.size() >= (t.mode == 0 ? 6u : 4u)) t.entry.erase(t.entry.begin());
    t.entry.push_back((char)('0' + d));
    if (t.mode == 0) {
        std::string e = t.entry; while (e.size() < 6) e = "0" + e;
        double sec = std::atoi(e.substr(0, 2).c_str()) * 3600.0 + std::atoi(e.substr(2, 2).c_str()) * 60.0 + std::atoi(e.substr(4, 2).c_str());
        if (sec < TIMER_MIN_SEC) sec = TIMER_MIN_SEC;
        t.durationSec = std::min(sec, TIMER_MAX_SEC);
        t.finished = false; t.running = false; t.pausedRemaining = t.durationSec;
    } else alarmApplyEntry();
}

void App::timerBackspace() {
    TimerState& t = timer;
    if (t.entry.empty()) return;
    t.entry.pop_back();
    if (t.mode == 0) {
        std::string e = t.entry; while (e.size() < 6) e = "0" + e;
        double sec = std::atoi(e.substr(0, 2).c_str()) * 3600.0 + std::atoi(e.substr(2, 2).c_str()) * 60.0 + std::atoi(e.substr(4, 2).c_str());
        if (sec < TIMER_MIN_SEC) sec = TIMER_MIN_SEC;
        t.durationSec = sec; t.running = false; t.pausedRemaining = sec;
    } else alarmApplyEntry();
}

void App::stopwatchLap() { if (timer.swRunning) timer.laps.push_back(stopwatchElapsed()); }

// ---------------------------------------------------------------- Focus tab session timer (independent from Timer)
double App::focusRemaining() const {
    if (focus.running) return std::max(0.0, focus.endAt - now);
    return focus.pausedRemaining;
}
void App::focusStartPause() {
    FocusState& f = focus;
    if (f.finished) { f.finished = false; f.pausedRemaining = f.durationSec; return; }
    if (f.running) { f.pausedRemaining = std::max(0.0, f.endAt - now); f.running = false; }
    else { if (f.pausedRemaining <= 0) f.pausedRemaining = f.durationSec; f.endAt = now + f.pausedRemaining; f.running = true; f.entry.clear(); }
}
void App::focusReset() {
    FocusState& f = focus;
    f.running = false; f.finished = false; f.pausedRemaining = f.durationSec; f.entry.clear();
}
void App::focusSetDuration(double sec) {
    FocusState& f = focus;
    f.durationSec = std::max(TIMER_MIN_SEC, std::min(sec, TIMER_MAX_SEC));
    f.finished = false; f.entry.clear();
    if (f.running) f.endAt = now + f.durationSec; else f.pausedRemaining = f.durationSec;
}
void App::focusAdjust(double delta) {
    FocusState& f = focus;
    double nd = std::max(TIMER_MIN_SEC, std::min(f.durationSec + delta, TIMER_MAX_SEC));
    double diff = nd - f.durationSec;
    f.durationSec = nd; f.finished = false;
    if (f.running) f.endAt = std::max(now + TIMER_MIN_SEC, std::min(now + TIMER_MAX_SEC, f.endAt + diff));
    else f.pausedRemaining = std::max(TIMER_MIN_SEC, std::min(TIMER_MAX_SEC, f.pausedRemaining + diff));
    f.entry.clear();
}
void App::focusDigit(int d) {
    FocusState& f = focus;
    if (f.entry.size() >= 6u) f.entry.erase(f.entry.begin());
    f.entry.push_back((char)('0' + d));
    std::string e = f.entry; while (e.size() < 6) e = "0" + e;
    double sec = std::atoi(e.substr(0, 2).c_str()) * 3600.0 + std::atoi(e.substr(2, 2).c_str()) * 60.0 + std::atoi(e.substr(4, 2).c_str());
    if (sec < TIMER_MIN_SEC) sec = TIMER_MIN_SEC;
    f.durationSec = std::min(sec, TIMER_MAX_SEC);
    f.finished = false; f.running = false; f.pausedRemaining = f.durationSec;
}
void App::focusBackspace() {
    FocusState& f = focus;
    if (f.entry.empty()) return;
    f.entry.pop_back();
    std::string e = f.entry; while (e.size() < 6) e = "0" + e;
    double sec = std::atoi(e.substr(0, 2).c_str()) * 3600.0 + std::atoi(e.substr(2, 2).c_str()) * 60.0 + std::atoi(e.substr(4, 2).c_str());
    if (sec < TIMER_MIN_SEC) sec = TIMER_MIN_SEC;
    f.durationSec = sec; f.running = false; f.pausedRemaining = sec;
}

} // namespace app
