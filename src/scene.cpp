// Meridian: frame composition, the Sky and Quiet faces, dock and overlays.
#include "scene.h"
#include "reveal.h"
#include "dockanim.h"
#include "ufo_audio.h"
#include <cmath>
#include <algorithm>

namespace app {

using namespace gfx;
using util::fmt;

float App::layoutScale(int w, int h) const {
    float W = (float)w, H = (float)h;
    if (W < H * 0.90f) return W / 1100.f;
    return std::min(H / 1080.f, W / 1440.f);
}

void App::render(Gfx& g, int w, int h) {
    hits.clear(); actions.clear(); packKeys.clear();
    spaceShipDrew = false;
    if (!showSettings) { edInit = false; textTarget = nullptr; settingsFocus = -1; }
    if (!inAboutTab()) secretBuf.clear();
    eventsBeginFrame(*this);
    holidaysBeginFrame(*this);
    glass.beginFrame((float)dt, isSaver);
    if (!g.begin()) return;
    const float W = (float)w, H = (float)h, s = layoutScale(w, h);
    g.vGradientRect(0, 0, W, H, pal.bgTop, pal.bgBottom);

    // The screen saver's start (reveal.h): where it is now, 0 to 1. The first frame is drawn black and pays for the first-time work (fonts,
    // pictures), so the clock for the animation starts on the second; after that it follows the wall clock.
    reveal::Look rv{}; bool revealing = false;
    {
        double rp = -1.0;
        if (leaveOverride >= 0.f) { rv = reveal::leaving(leaveOverride, W, H); revealing = true; }
        else if (leaveArmed) {
            // leaving: from the picture as it is (fully up, or part way through the start) back to black. A start that was still going plays backwards at
            // four times its speed; a finished picture gets the quick close-in. The clock starts on the first frame drawn after the request.
            if (leaveStart < 0.0) leaveStart = now;
            const double el = std::max(0.0, now - leaveStart);
            if (leaveFrom >= 1.0) {
                const double q = el / reveal::kLeaveSeconds;
                if (q >= 1.0) leaveDone = true;
                rv = reveal::leaving(q, W, H);
            } else {
                const double p = leaveFrom - el * 4.0 / reveal::kSeconds;
                if (p <= 0.0) leaveDone = true;
                rv = reveal::at(std::max(0.0, p), W, H);
            }
            revealing = true;
        }
        else {
            if (revealOverride >= 0.f) rp = revealOverride;
            else if (revealArmed) {
                if (++revealFrames == 2) revealStart = now;
                rp = revealFrames < 2 ? 0.0 : (now - revealStart) / reveal::kSeconds;
                if (rp >= 1.0) revealArmed = false;
                revealP = rp;
            }
            if (rp >= 0.0) { rv = reveal::at(rp, W, H); revealing = !rv.done; }
        }
    }
    if (revealing) g.setReveal(rv.scale, W * 0.5f, H * 0.5f);      // the picture settles from slightly large (the background above is plain, it needs no zoom)

    shiftX = shiftY = 0;
    if (st.pixelShift) {
        shiftX = std::round((float)std::sin(now / 2700.0 * 6.2831853) * 6.f * s);
        shiftY = std::round((float)std::cos(now / 3300.0 * 6.2831853) * 4.f * s);
    }
    g.setOffset(shiftX, shiftY);
    // theme packs: the neon bloom a pack asks for, and its scene behind the faces (the full one behind Quiet, a lighter one behind the rest)
    {
        themes::Pack* bp = backdropId.empty() ? nullptr : themes::find(backdropId);
        g.setNeon(bp ? bp->neonColor : gfx::Color{1.f, 0.24f, 0.57f, 1.f}, pal.neon);
        if (bp && bp->ok && st.face != FACE_PACK && backdropT > 0.002f) { Ctx c{g, *this, pal, W, H, s}; drawThemeBackdrop(c, *bp, st.face == FACE_QUIET, backdropT); }
    }
    if (st.face != FACE_FOCUS) { focusOrbT = focusOrbTarget = 0.f; }
    switch (st.face) {
        case FACE_QUIET: renderQuiet(g, w, h); break;
        case FACE_TIMER: renderTimer(g, w, h); break;
        case FACE_ALMANAC: renderAlmanac(g, w, h); break;
        case FACE_WIDGETS: renderWidgets(g, w, h); break;
        case FACE_FOCUS: renderFocus(g, w, h); break;
        case FACE_PACK: {                                           // an extra tab from a theme pack
            themes::Pack* tp = nullptr; themes::Face* tf = nullptr;
            if (packFace(tp, tf)) { Ctx c{g, *this, pal, W, H, s}; g.setNeon(tp->neonColor, pal.neon); drawThemeFace(c, *tp, *tf); }
        } break;
        default: renderSky(g, w, h); break;
    }
    if (!spaceShipDrew) ufoaudio::stop();
    g.setOffset(0, 0);
    shiftX = shiftY = 0;
    if (revealing) g.setReveal(1.f, 0.f, 0.f);                      // the dock, the overlays and the cover below are never zoomed

    float ft = clamp01((float)((now - faceChangedAt) / 0.35));
    if (ft < 1.f) g.vGradientRect(0, 0, W, H, withA(pal.bgTop, 1.f - ft), withA(pal.bgBottom, 1.f - ft));

    renderDock(g, w, h);
    renderOverlays(g, w, h);
    glass.endFrame();
    const float bright = effectiveBrightness();
    if (bright < 0.999f) g.fillRect(0, 0, W, H, Color{0, 0, 0, 1.f - std::max(0.08f, bright)});
    if (revealing) {                                                // last, over everything: the circle of light opening, and the whole picture coming up
        g.vignette(W * 0.5f, H * 0.5f, rv.inner, rv.outer, W, H);
        if (rv.dim > 0.002f) g.fillRect(0, 0, W, H, Color{0, 0, 0, rv.dim});
    }
    if (!g.end()) wantRecreate = true;
}

// ---------------------------------------------------------------- Sky
void App::renderSky(Gfx& g, int w, int h) {
    const float W = (float)w, H = (float)h, s = layoutScale(w, h);
    const bool portrait = W < H * 0.90f;
    Ctx c{g, *this, pal, W, H, s};
    const Palette& P = pal;
    drawScenery(c);                                             // behind everything: the theme's scenery and the soft sun flare (also on the Quiet face; the other faces have none)
    drawSkyRain(c);                                             // and the rain, when it rains
    const util::LocalTime L = util::toLocal(now, tzOff);
    const ClockParts cp = clockParts(*this);
    const float m = (portrait ? 56.f : 72.f) * s;

    float xL = m, xR, domeCx, domeCy, R, ringO, bandY, capY, bandX, bandW;
    float ty;                                   // top of the left column
    float timeEm, wL;

    const float tw1 = g.timeWidth(F_DISPLAY, 1.f, L"00:00");

    if (!portrait) {
        R = std::min(0.295f * H, 0.19f * W);
        ringO = R + 36 * s;
        float total = 2 * ringO + 270 * s;
        float topY = std::max(m * 0.6f, (H - total) * 0.5f);
        domeCx = W - m - ringO; domeCy = topY + ringO;
        xR = domeCx - ringO - 56 * s;
        wL = xR - xL;
        ty = topY;
        bandX = domeCx - ringO; bandW = 2 * ringO;
        bandY = domeCy + ringO + 40 * s;
        capY = bandY + 92 * s;
        timeEm = std::min(0.30f * H, wL / (tw1 + 0.36f));
    } else {
        xR = W - m; wL = xR - xL; ty = m * 0.6f;
        timeEm = std::min((wL) / (tw1 + 0.36f), 0.2f * H);
        R = 0; ringO = 0; domeCx = W * 0.5f; domeCy = 0; bandX = xL; bandW = wL; bandY = 0; capY = 0;
    }

    // dome first so nothing of the left column is painted over by its mask
    if (!portrait) {
        drawDome(c, domeCx, domeCy, R, true);
        if (st.elBand) drawDayBand(c, bandX, bandY, bandW, 12 * s);
        if (st.evChips) drawEventChips(c, bandX, domeCy + ringO + 5 * s, bandW);
        if (st.elSunMoon) drawSunMoonLines(c, bandX, capY, bandW);
    }

    // meta row
    {
        std::string loc = util::upper(st.geo.name) + fmt("  ·  %.2f°%c %.2f°%c", std::fabs(st.geo.lat), st.geo.lat >= 0 ? 'N' : 'S', std::fabs(st.geo.lon), st.geo.lon >= 0 ? 'E' : 'W');
        g.text(F_MONO, 15.f * s, xL, ty + 14 * s, g.ellipsize(F_MONO, 15.f * s, Ws(loc), wL * 0.72f, 0.14f), P.ink3, Align::Left, 0.14f);
        int off = tzOff / 60; std::string tz = fmt("UTC%c%d:%02d", off < 0 ? '-' : '+', std::abs(off) / 60, std::abs(off) % 60);
        if (isDstActive()) tz += " \xC2\xB7 DST";
        g.text(F_MONO, 15.f * s, xR, ty + 14 * s, Ws(tz), P.ink3, Align::Right, 0.14f);
        if (st.stormWatch && !stormNear.empty()) {         // a named storm is near: shown between the place and the time zone
            const storms::Storm& sm = stormNear[0];
            const std::wstring locT = g.ellipsize(F_MONO, 15.f * s, Ws(loc), wL * 0.72f, 0.14f);
            const float right = xR - g.textWidth(F_MONO, 15.f * s, Ws(tz), 0.14f) - 26 * s, left = xL + g.textWidth(F_MONO, 15.f * s, locT, 0.14f) + 26 * s;
            const float avail = right - left - 16 * s;
            if (avail > 150 * s) {
                const std::wstring t = g.ellipsize(F_MONO, 15.f * s, Ws(util::upper(storms::title(sm)) + "  \xC2\xB7  " + util::upper(storms::where(sm))), avail, 0.14f);
                const float tw = g.textWidth(F_MONO, 15.f * s, t, 0.14f);
                const Color sc = tint(P, P.accent);
                g.fillCircle(right - tw - 10 * s, ty + 14 * s - 5 * s, 4 * s, sc);
                g.text(F_MONO, 15.f * s, right, ty + 14 * s, t, sc, Align::Right, 0.14f);
            }
        }
    }

    // time
    float capH = g.capHeight(F_DISPLAY, timeEm);
    float base = ty + 46 * s + capH;
    float twA = g.timeWidth(F_DISPLAY, timeEm, cp.hm);
    g.timeText(F_DISPLAY, timeEm, xL, base, cp.hm, P.ink, lerp(P.ink, P.ink2, 0.55f), Align::Left);
    {
        float sx = xL + twA + 0.05f * timeEm;
        if (!cp.ampm.empty()) { float fs = 0.085f * timeEm; g.text(F_MONO_M, fs, sx, base - capH + g.capHeight(F_MONO_M, fs), cp.ampm, P.ink2, Align::Left, 0.14f); }
        if (st.seconds) g.text(F_LIGHT, 0.17f * timeEm, sx, base, cp.sec, P.ink3, Align::Left);
    }
    // seconds hairline
    {
        float hy = base + 0.115f * timeEm;
        float p = clamp01((float)((L.ss + L.fracSec) / 60.0));
        g.line(xL, hy, xL + twA, hy, mulA(P.ink, 0.13f), std::max(1.5f, 2.f * s), true);
        if (st.seconds && p > 0.004f) {
            g.line(xL, hy, xL + twA * p, hy, P.accent, std::max(1.5f, 2.f * s), true);
            g.fillCircle(xL + twA * p, hy, 3.4f * s, P.accent);
        }
        base = hy;
    }
    // date
    float dateBase = base + 74 * s;
    {
        std::wstring wd = Ws(util::WEEKDAYS[L.wday]);
        std::wstring dm = Ws(fmt("%d %s", L.d, util::MONTHS[L.m - 1]));
        int iy; int wk = astro::isoWeek(L.y, L.m, L.d, &iy);
        std::wstring wkStr = Ws(fmt("%d  ·  WEEK %d", L.y, wk));
        float wkW = g.textWidth(F_MONO, 15.f * s, wkStr, 0.14f);
        float fs = 56 * s;
        float need = g.textWidth(F_SERIF, fs, wd) + 18 * s + g.textWidth(F_SERIF_I, fs, dm);
        float avail = std::max(120 * s, wL - wkW - 24 * s);
        if (need > avail) fs *= avail / need;
        float ww = g.textWidth(F_SERIF, fs, wd);
        g.text(F_SERIF, fs, xL, dateBase, wd, P.ink);
        g.text(F_SERIF_I, fs, xL + ww + 18 * s * (fs / (56 * s)), dateBase, dm, P.ink2);
        g.text(F_MONO, 15.f * s, xR, dateBase, wkStr, P.ink3, Align::Right, 0.14f);
    }

    // portrait: dome sits between the date and the lists
    float listTop = dateBase + 70 * s;
    float weatherH = 262 * s;
    float bottomY = H - m * 0.8f;
    float weatherY = bottomY - weatherH;
    if (portrait) {
        const float extrasH = (st.elBand ? 78 * s : 24 * s) + (st.elSunMoon ? 168 * s : 0.f) + 34 * s;
        const float availH = std::max(160 * s, (st.elWeather ? weatherY : bottomY) - listTop);
        int rows = 2;
        float space = availH - extrasH - rows * 112 * s - 24 * s;
        R = std::min(wL * 0.5f - 40 * s, space * 0.5f - 36 * s);
        if (R < 150 * s) { rows = 1; space += 112 * s; R = std::min(wL * 0.5f - 40 * s, space * 0.5f - 36 * s); }
        if (R < 120 * s) { rows = 0; space += 112 * s; R = std::min(wL * 0.5f - 40 * s, space * 0.5f - 36 * s); }
        R = std::max(56.f * s, R);
        ringO = R + 36 * s;
        domeCy = listTop + ringO - 12 * s; domeCx = W * 0.5f;
        drawDome(c, domeCx, domeCy, R, false);
        bandY = domeCy + ringO + 34 * s;
        if (st.elBand) drawDayBand(c, xL, bandY, wL, 12 * s);
        if (st.evChips) drawEventChips(c, xL, domeCy + ringO + 3 * s, wL);
        capY = (st.elBand ? bandY + 78 * s : domeCy + ringO + 30 * s);
        if (st.elSunMoon) drawSunMoonLines(c, xL, capY, wL);
        float todayTop = st.elSunMoon ? capY + 172 * s : capY + 12 * s;
        float panelH = (st.elWeather ? weatherY : bottomY) - todayTop - 16 * s;
        if (rows > 0 && panelH > 80 * s) drawSkyPanel(c, xL, todayTop, wL, panelH);
        if (st.elWeather) drawWeatherBlock(c, xL, weatherY, wL, weatherH);
        drawSpaceFront(c);
        return;
    }

    float todayTop = listTop + 14 * s;
    drawSkyPanel(c, xL, todayTop, wL, weatherY - todayTop - 18 * s);
    if (st.elWeather) drawWeatherBlock(c, xL, weatherY, wL, weatherH);
    drawSpaceFront(c);                                          // Midnight: once a day, a saucer
}

// ---------------------------------------------------------------- Quiet
void App::renderQuiet(Gfx& g, int w, int h) {
    const float W = (float)w, H = (float)h, s = layoutScale(w, h);
    const Palette& P = pal;
    const util::LocalTime L = util::toLocal(now, tzOff);
    const ClockParts cp = clockParts(*this);
    { Ctx c{g, *this, pal, W, H, s}; drawQuietFx(c); }          // clouds, wind, rain or a tornado, behind everything
    float tw1 = g.timeWidth(F_DISPLAY, 1.f, L"00:00");
    float em = std::min(W * 0.86f / (tw1 + 0.1f), 0.74f * H);
    float capH = g.capHeight(F_DISPLAY, em);
    float dateFs = std::max(26.f * s, 0.048f * H);
    float gap = 0.075f * H;
    float blockH = capH + gap + dateFs * 0.7f;
    float base = (H - blockH) * 0.5f + capH;
    float twA = g.timeWidth(F_DISPLAY, em, cp.hm);
    float x0 = (W - twA) * 0.5f;
    g.timeText(F_DISPLAY, em, x0, base, cp.hm, P.ink, lerp(P.ink, P.ink2, 0.6f), Align::Left);
    if (!cp.ampm.empty()) { float fs = 0.06f * em; g.text(F_MONO_M, fs, x0 + twA + 0.035f * em, base - capH + g.capHeight(F_MONO_M, fs), cp.ampm, P.ink3, Align::Left, 0.14f); }
    std::wstring wd = Ws(util::WEEKDAYS[L.wday]), dm = Ws(fmt("%d %s", L.d, util::MONTHS[L.m - 1]));
    float ww = g.textWidth(F_SERIF, dateFs, wd), dw = g.textWidth(F_SERIF_I, dateFs, dm), gp = dateFs * 0.32f;
    float dx = (W - (ww + gp + dw)) * 0.5f, db = base + gap + dateFs * 0.7f;
    g.text(F_SERIF, dateFs, dx, db, wd, P.ink2);
    g.text(F_SERIF_I, dateFs, dx + ww + gp, db, dm, P.ink3);
    if (st.stormWatch && !stormNear.empty()) {            // a named storm is near: its name, distance and direction, quietly under the date
        const storms::Storm& sm = stormNear[0];
        std::string line = util::upper(storms::title(sm)) + "  \xC2\xB7  " + util::upper(storms::where(sm));
        const std::string w = storms::winds(sm); if (!w.empty()) line += "  \xC2\xB7  " + util::upper(w);
        if (stormNear.size() > 1) line += fmt("  \xC2\xB7  +%d MORE", (int)stormNear.size() - 1);
        const float fs = std::max(14.f * s, 0.0185f * H), dotR = fs * 0.30f;
        const std::wstring t = g.ellipsize(F_MONO_M, fs, Ws(line), W * 0.86f - 2 * dotR - 14 * s, 0.14f);
        const float tw = g.textWidth(F_MONO_M, fs, t, 0.14f), tot = tw + 2 * dotR + 14 * s, x0s = (W - tot) * 0.5f, ys = db + dateFs * 1.75f;
        const Color sc = tint(P, P.accent);
        g.fillCircle(x0s + dotR, ys - fs * 0.36f, dotR, sc);
        g.glow(x0s + dotR, ys - fs * 0.36f, dotR * 4.5f, sc, 0.35f);
        g.text(F_MONO_M, fs, x0s + 2 * dotR + 14 * s, ys, t, sc, Align::Left, 0.14f);
    }
    { Ctx c{g, *this, pal, W, H, s}; drawQuietFront(c, (base - capH + db) * 0.5f, db - (base - capH)); }   // one cloud passes in front, never hiding the time
    { Ctx c{g, *this, pal, W, H, s}; drawSpaceFront(c); }        // Midnight: once a day, a saucer
    (void)s;
}

// ---------------------------------------------------------------- Dock
// Rests as a sleek bottom-center pill; hovering over the pill fluidly morphs it into the frosted navigation dock
// (matching the Focus Studio Orb liquid-glass animation), and leaving smoothly collapses it back into the pill.
void App::renderDock(Gfx& g, int w, int h) {
    const float W = (float)w, H = (float)h, s = layoutScale(w, h);
    const float bh = 38 * s, pad = 20 * s, gap = 6 * s, fs = 14.f;
    struct B { std::wstring label; bool active; int id; };
    std::vector<B> bs = {{L"SKY", st.face == FACE_SKY, HIT_FACE0 + FACE_SKY}, {L"QUIET", st.face == FACE_QUIET, HIT_FACE0 + FACE_QUIET},
                         {L"TIMER", st.face == FACE_TIMER, HIT_FACE0 + FACE_TIMER}, {L"ALMANAC", st.face == FACE_ALMANAC, HIT_FACE0 + FACE_ALMANAC},
                         {L"WIDGETS", st.face == FACE_WIDGETS, HIT_FACE0 + FACE_WIDGETS}, {L"FOCUS", st.face == FACE_FOCUS, HIT_FACE0 + FACE_FOCUS},
                         {L"|", false, 0},
                         {Ws(std::string("THEME ") + util::upper(themeName())), false, HIT_THEME}, {L"TOP", st.topmost, HIT_PIN},
                         {L"FULL", st.fullscreen, HIT_FULLSCREEN}, {L"SETTINGS", showSettings, HIT_SETTINGS}, {L"?", showHelp, HIT_HELP}};
    // tabs brought by theme packs go right after the built-in ones
    packFaceKeys.clear();
    {
        size_t at = FACE_COUNT;
        for (themes::Pack* p : livePacks()) for (themes::Face& f : p->faces) {
            if (packFaceKeys.size() >= 60) break;
            const std::string key = themes::faceKey(*p, f);
            bs.insert(bs.begin() + (long)at++, B{Ws(util::upper(f.name)), st.face == FACE_PACK && st.facePack == key, HIT_PACKFACE0 + (int)packFaceKeys.size()});
            packFaceKeys.push_back(key);
        }
    }
    float total = 0; std::vector<float> ws;
    float dockFs = fs, dockPad = pad, dockGap = gap, dockInset = 12 * s;
    auto measureDock = [&]() {
        ws.clear(); total = 0;
        for (auto& b : bs) {
            float bw = b.label == L"|" ? std::max(4.f, 10.f * s * (dockFs / fs)) : g.textWidth(F_MONO_M, dockFs * s, b.label, 0.08f) + dockPad * 2;
            ws.push_back(bw); total += bw + dockGap;
        }
        if (!ws.empty()) total -= dockGap;
    };
    measureDock();
    const float maxOuterW = std::max(160.f, W - 24.f * s);
    const float maxTotal = std::max(120.f, maxOuterW - 2.f * dockInset);
    if (total > maxTotal) {
        const float f = std::max(0.42f, maxTotal / total);
        dockFs *= f; dockPad *= f; dockGap *= f; dockInset *= std::max(0.6f, f);
        measureDock();
        const float maxTotal2 = std::max(120.f, maxOuterW - 2.f * dockInset);
        if (total > maxTotal2 && total > 1.f) {
            const float f2 = std::max(0.55f, maxTotal2 / total);
            dockFs *= f2; dockPad = std::max(3.f, dockPad * f2); dockGap = std::max(2.f, dockGap * f2);
            measureDock();
        }
    }

    dockanim::Input din{};
    din.W = W; din.H = H; din.s = s;
    din.expW = total + 2.f * dockInset;
    din.expH = bh + 20.f * s;
    din.mouseX = mouseX; din.mouseY = mouseY;
    din.idleSec = (float)std::max(0.0, now - lastMouseMove);
    din.dt = (float)dt;
    din.blocked = showSettings || showHelp || isSaver;
    din.overrideT = dockOverride;
    dockanim::State dst{dockT, dockTarget};
    const dockanim::Frame df = dockanim::step(din, dst);
    dockT = dst.t; dockTarget = dst.target;
    if (!df.visible) return;

    // Subtle accent wake-up glow when pointer touches the resting pill
    if (df.pillGlowA > 0.01f) {
        g.glowEllipse(df.curX + df.curW * 0.5f, df.curY + df.curH * 0.5f, df.curW * 0.65f, std::max(10.f * s, df.curH * 1.8f), pal.accent, 0.16f * df.pillGlowA);
    }
    // Soft shadow emerging smoothly as the pill inflates into the floating dock
    if (df.shadowA > 0.005f) {
        g.fillRoundRect(df.curX, df.curY + 3.f * s, df.curW, df.curH, df.curR, Color{0, 0, 0, (pal.dark ? 0.22f : 0.11f) * df.shadowA});
    }
    // Single seamless liquid-glass capsule morphing from resting pill into frosted dock plate
    const Color pillBase = mulA(df.inPill ? lerp(pal.ink2, pal.ink, 0.35f) : pal.ink2, pal.dark ? 0.45f : 0.48f);
    const Color dockBase = withA(pal.plate, pal.dark ? 0.94f : 0.96f);
    const Color fillC = mulA(lerp(pillBase, dockBase, smoothstep(0.05f, 0.52f, df.e)), df.vis);
    g.fillRoundRect(df.curX, df.curY, df.curW, df.curH, df.curR, fillC);
    if (df.rimA > 0.005f) {
        const Color rimC = (df.inPill && df.e < 0.45f)
            ? lerp(withA(pal.accent, 0.55f * df.rimA), mulA(pal.line, df.rimA), smoothstep(0.10f, 0.45f, df.e))
            : mulA(pal.line, (0.95f + 0.15f * df.e) * df.rimA);
        g.strokeRoundRect(df.curX + 0.5f, df.curY + 0.5f, std::max(1.f, df.curW - 1.f), std::max(1.f, df.curH - 1.f), std::max(0.5f, df.curR - 0.5f), rimC, std::max(1.f, s));
    }
    // Absorb clicks over the dock capsule / active pill so tapping gaps never accidentally toggles fullscreen
    if (df.swallowClick) {
        if (df.e > 0.30f) addHit(*this, df.curX, df.curY, df.curW, df.curH, HIT_DOCK_BG);
        else addHit(*this, df.pillX - 24.f * s, df.pillY - 18.f * s, df.pillW + 48.f * s, H - (df.pillY - 18.f * s), HIT_DOCK_BG);
    }

    // Navigation buttons smoothly bloom inside the rounded capsule clip as it opens past 36%
    if (df.contentA > 0.02f) {
        Palette Pd = pal;
        for (Color* col : {&Pd.ink, &Pd.ink2, &Pd.ink3, &Pd.line, &Pd.panel, &Pd.plate, &Pd.accent}) col->a *= df.contentA;
        Ctx c{g, *this, Pd, W, H, s};
        const float btnY = (df.curY + df.curH * 0.5f) - bh * 0.5f;
        const bool needClip = df.e < 0.995f || total > df.curW - 2.f * s;
        if (needClip) g.pushRoundRectClip(df.curX + 1.f, df.curY + 1.f, std::max(1.f, df.curW - 2.f), std::max(1.f, df.curH - 2.f), std::max(0.5f, df.curR - 1.f));
        float x = (W - total) * 0.5f;
        std::vector<SegItem> faceSegs;
        size_t sepIdx = bs.size();
        for (size_t i = 0; i < bs.size(); ++i) {
            if (bs[i].label == L"|") { sepIdx = i; break; }
            const int hitId = dockanim::buttonClickable(df, x, ws[i], s) ? bs[i].id : 0;
            faceSegs.push_back({bs[i].label, x, ws[i], bs[i].active, hitId, (uint64_t)bs[i].id});
            x += ws[i] + dockGap;
        }
        drawSlidingButtons(c, glassfx::SLIDE_DOCK_FACE, btnY, bh, faceSegs, F_MONO_M, dockFs, df.canHit);
        for (size_t i = sepIdx; i < bs.size(); ++i) {
            if (bs[i].label == L"|") {
                g.line(x + ws[i] * 0.5f, btnY + 8.f * s, x + ws[i] * 0.5f, btnY + bh - 8.f * s, Pd.line, std::max(1.f, s), false);
            } else {
                const int hitId = dockanim::buttonClickable(df, x, ws[i], s) ? bs[i].id : 0;
                drawButton(c, x, btnY, ws[i], bh, bs[i].label, bs[i].active, hitId, F_MONO_M, dockFs, (uint64_t)bs[i].id, df.canHit);
            }
            x += ws[i] + dockGap;
        }
        if (needClip) g.popRoundRectClip();
    }
}

// ---------------------------------------------------------------- Overlays: toast and help
void App::renderOverlays(Gfx& g, int w, int h) {
    const float W = (float)w, H = (float)h, s = layoutScale(w, h);
    const Palette& P = pal;
    { Ctx c{g, *this, pal, W, H, s}; drawEventTooltip(c); drawHolidayTooltip(c); }
    if (showSettings) renderSettings(g, w, h);
    if (now < toastUntil && !toast.empty()) {
        float al = clamp01((float)(toastUntil - now) / 0.4f);
        float fs = 21 * s; float tw = g.textWidth(F_MEDIUM, fs, toast, 0.02f);
        float bw = tw + 52 * s, bh = 46 * s, bx = (W - bw) * 0.5f, by = H - 120 * s;
        g.fillRoundRect(bx, by, bw, bh, bh * 0.5f, withA(P.plate, 0.94f * al));
        g.strokeRoundRect(bx + 0.5f, by + 0.5f, bw - 1, bh - 1, bh * 0.5f, mulA(P.line, al), std::max(1.f, s));
        g.text(F_MEDIUM, fs, W * 0.5f, by + bh * 0.5f + g.capHeight(F_MEDIUM, fs) * 0.5f, toast, mulA(P.ink, al), Align::Center, 0.02f);
    }
    if (!showHelp) return;
    g.fillRect(0, 0, W, H, withA(P.plate, 0.965f));
    float cw = std::min(W - 80 * s, 1040 * s), x0 = (W - cw) * 0.5f, y0 = std::max(24 * s, (H - 780 * s) * 0.5f);
    g.text(F_SERIF_I, 76 * s, x0, y0 + 70 * s, L"Meridian", P.ink);
    g.text(F_LIGHT, 22 * s, x0, y0 + 108 * s, L"An always-on clock with the sky, the moon, the weather and what is special today.", P.ink2);
    struct K { const wchar_t* k; const wchar_t* d; };
    const bool packs = !livePacks().empty();
    const K ks[] = {
        {L"T", packs ? L"Cycle theme: all ten, then your theme packs" : L"Cycle theme: all ten"},
        {L"Left  Right", packFaceKeys.empty() ? L"Switch face: Sky, Quiet, Timer, Almanac, Widgets, Focus  (1–6 also)" : L"Switch face: Sky, Quiet, Timer, Almanac, Widgets, Focus, then theme packs  (1–9 also)"},
        {L"F   F11", L"Fullscreen  (double-click too, Esc leaves)"}, {L"Space", L"Start / pause Focus, timer, stopwatch or alarm; dismiss a ringing one"},
        {L"R   L", L"Reset  ·  stopwatch lap"}, {L"0–9", L"Type a time on Focus, Timer or Alarm, like a microwave"},
        {L"+   -", L"Adjust the timer or alarm by a minute  (Shift for more)"}, {L"S   C   H", L"Seconds on/off  ·  °C/°F  ·  12/24 hour"},
        {L"P   K", L"Keep window on top  ·  keep the screen awake"}, {L"[   ]", L"Dim / brighten  (mouse wheel too)"},
        {L"B", L"Red Ember theme during bedtime hours"}, {L"W   N", L"Refresh the weather  ·  next place"},
        {L"Tab", L"Next panel on the Sky face, next page on the Widgets face  (Shift for back)"},
        {L"M", L"Burn-in pixel shift on/off"}, {L",", L"Settings: widgets, rotation, sky map, quiet face, elements, weather, places"},
        {L"G", L"Open settings.ini"}, {L"?   Esc", L"This help"},
    };
    const int nk = (int)(sizeof ks / sizeof ks[0]);
    float ky = y0 + 170 * s;
    float rowH = std::max(26.f * s, std::min(42.f * s, (H - 70 * s - ky) / (nk + 1)));
    for (int i = 0; i < nk; ++i) {
        float yy = ky + i * rowH;
        g.line(x0, yy + 0.3f * rowH, x0 + cw, yy + 0.3f * rowH, P.line, std::max(1.f, s), false);
        g.text(F_MONO_M, 17 * s, x0, yy, ks[i].k, P.accent, Align::Left, 0.08f);
        g.text(F_LIGHT, 20 * s, x0 + 240 * s, yy, g.ellipsize(F_LIGHT, 20 * s, ks[i].d, std::max(120 * s, cw - 240 * s)), P.ink);
    }
    g.text(F_MONO, 13.5f * s, x0, ky + nk * rowH + 14 * s, L"Sun, moon and planets are computed on this PC. Weather and air quality from Open-Meteo.", P.ink3, Align::Left, 0.06f);
}

} // namespace app
