// Meridian: Timer and Almanac faces.
#include "scene.h"
#include <cmath>
#include <algorithm>

namespace app {

using namespace gfx;
using util::fmt;

static std::string thousands(double v) {
    std::string s = fmt("%.0f", v), out;
    int n = (int)s.size();
    for (int i = 0; i < n; ++i) { out += s[i]; if ((n - i - 1) % 3 == 0 && i != n - 1) out += ','; }
    return out;
}

static std::string whenStr(const App& a, double t) {
    int off = a.tzAt ? a.tzAt(t + 30.0) : a.tzOff;
    util::LocalTime L = util::toLocal(t + 30.0, off);
    return fmt("%s %d %s, %s", util::WEEKDAYS3[L.wday], L.d, util::MONTHS3[L.m - 1], clockStr(a, t).c_str());
}

static std::string inStr(double sec) {
    if (sec < 0) sec = 0;
    if (sec >= 2 * 86400.0) return fmt("in %.0f d", std::floor(sec / 86400.0));
    int m = (int)(sec / 60.0);
    if (m >= 120) return fmt("in %d h %d m", m / 60, m % 60);
    return fmt("in %d m", m);
}

static std::wstring hmsStr(double sec, bool forceH) {
    long long t = (long long)std::ceil(sec - 1e-6); if (t < 0) t = 0;
    int h = (int)(t / 3600), m = (int)((t % 3600) / 60), s = (int)(t % 60);
    if (h > 0 || forceH) return Ws(fmt("%d:%02d:%02d", h, m, s));
    return Ws(fmt("%02d:%02d", m, s));
}

// ---------------------------------------------------------------- Timer / stopwatch / alarm (classic, untouched)
void App::renderTimer(Gfx& g, int w, int h) {
    const float W = (float)w, H = (float)h, s = layoutScale(w, h);
    const bool portrait = W < H * 0.90f;
    Ctx c{g, *this, pal, W, H, s};
    const Palette& P = pal;
    const TimerState& T = timer;
    const float m = 64 * s;

    // tabs
    {
        const wchar_t* names[3] = {L"TIMER", L"STOPWATCH", L"ALARM"};
        float bw[3], total = 0, gap = 10 * s, bh = 40 * s;
        for (int i = 0; i < 3; ++i) { bw[i] = g.textWidth(F_MONO_M, 15 * s, names[i], 0.08f) + 44 * s; total += bw[i]; }
        total += gap * 2;
        float x = (W - total) * 0.5f, y = 52 * s;
        for (int i = 0; i < 3; ++i) { drawButton(c, x, y, bw[i], bh, names[i], T.mode == i, HIT_TAB0 + i, F_MONO_M, 15.f); x += bw[i] + gap; }
    }
    // presets (timer only)
    if (T.mode == 0) {
        static const wchar_t* pl[7] = {L"1m", L"3m", L"5m", L"10m", L"15m", L"30m", L"1h"};
        float bh = 34 * s, gap = 8 * s, total = 0, bw[7];
        for (int i = 0; i < 7; ++i) { bw[i] = g.textWidth(F_MONO_M, 14 * s, pl[i], 0.06f) + 30 * s; total += bw[i]; }
        total += gap * 6;
        float x = (W - total) * 0.5f, y = 112 * s;
        for (int i = 0; i < 7; ++i) {
            bool act = std::fabs(T.durationSec - TIMER_PRESETS[i]) < 0.5 && T.entry.empty();
            drawButton(c, x, y, bw[i], bh, pl[i], act, HIT_PRESET0 + i, F_MONO_M, 14.f); x += bw[i] + gap;
        }
    }

    float cx = W * 0.5f, cy = H * 0.505f;
    float R = portrait ? std::min(0.40f * W, 0.25f * H) : std::min(0.262f * H, 0.27f * W);
    const float ringW = std::max(4.f, 10.f * s);

    // progress and text for the three modes
    float frac = 1.f; bool ready = false;
    std::wstring big, sub, tail; Color bigCol = P.ink; bool accentBig = false; bool pulse = false;
    double nowS = now;
    if (T.mode == 0) {
        double rem = timerRemaining();
        if (T.finished) { frac = 0; pulse = true; big = hmsStr(0, T.durationSec >= 3600); sub = L"TIME\u2019S UP"; }
        else {
            frac = (float)(rem / std::max(1.0, T.durationSec));
            bool forceH = T.durationSec >= 3600 || !T.entry.empty();
            if (!T.entry.empty()) { std::string e = T.entry; while (e.size() < 6) e = "0" + e; big = Ws(e.substr(0, 2) + ":" + e.substr(2, 2) + ":" + e.substr(4, 2)); bigCol = P.accent; accentBig = true; sub = L"TYPE DIGITS \xB7 SPACE TO START"; ready = true; }
            else {
                big = hmsStr(rem, forceH);
                if (T.running) sub = Ws("ENDS " + util::upper(clockStr(*this, T.endAt)));
                else if (rem < T.durationSec - 0.5) sub = L"PAUSED";
                else { sub = L"READY"; ready = true; }
            }
        }
    } else if (T.mode == 1) {
        double el = stopwatchElapsed();
        long long cs = (long long)(el * 100.0);
        int hh = (int)(cs / 360000), mm = (int)((cs / 6000) % 60), ss = (int)((cs / 100) % 60), cc = (int)(cs % 100);
        big = hh > 0 ? Ws(fmt("%d:%02d:%02d", hh, mm, ss)) : Ws(fmt("%02d:%02d", mm, ss));
        tail = Ws(fmt(".%02d", cc));
        frac = (float)std::fmod(el, 60.0) / 60.f;
        sub = T.swRunning ? L"RUNNING" : (el > 0 ? L"STOPPED" : L"READY");
        ready = !T.swRunning && el <= 0;
    } else {
        int hh = T.alarmMinutes / 60, mm = T.alarmMinutes % 60;
        if (!T.entry.empty()) { bigCol = P.accent; accentBig = true; }
        if (st.h24) big = Ws(fmt("%02d:%02d", hh, mm));
        else { int h12 = hh % 12 == 0 ? 12 : hh % 12; big = Ws(fmt("%d:%02d", h12, mm)); tail = hh < 12 ? L" AM" : L" PM"; }
        if (T.ringing) { pulse = true; sub = L"ALARM"; frac = 0; }
        else if (T.alarmOn) {
            double left = std::max(0.0, T.alarmAt - now); frac = (float)std::min(1.0, left / (12 * 3600.0));
            int tm = (int)(left / 60.0);
            sub = left < 60.0 ? L"RINGS IN < 1M" : Ws(fmt("RINGS IN %dH %02dM", tm / 60, tm % 60));
        } else { sub = L"OFF \xB7 TYPE A TIME, SPACE TO ARM"; ready = true; }
    }
    float pul = pulse ? 0.5f + 0.5f * (float)std::sin(nowS * 5.0) : 1.f;

    // glow + ring
    if (T.running || T.swRunning || T.alarmOn || pulse) g.glow(cx, cy, R * 1.25f, P.accent, 0.10f + (pulse ? 0.10f * pul : 0.f));
    g.strokeCircle(cx, cy, R, mulA(P.ink, 0.10f), ringW);
    {
        float sweep = 360.f * clamp01(frac);
        Color rc = pulse ? withA(P.accent, 0.35f + 0.65f * pul) : (ready ? withA(P.accent, 0.38f) : P.accent);
        if (sweep > 0.4f) {
            if (sweep > 359.5f) g.strokeCircle(cx, cy, R, rc, ringW);
            else g.arc(cx, cy, R, 0.f, sweep, rc, ringW, true);
        }
        if (!pulse && T.mode == 1 && T.swRunning) {
            float a = (float)astro::rad(sweep);
            g.fillCircle(cx + R * std::sin(a), cy - R * std::cos(a), ringW * 0.9f, P.ink);
        }
    }
    // big text
    {
        float tw1 = g.timeWidth(F_DISPLAY, 1.f, big + tail);
        float em = std::min(0.40f * R, 1.5f * R / std::max(1.f, tw1));
        float capH = g.capHeight(F_DISPLAY, em);
        float base = cy + capH * 0.5f - 0.03f * R;
        float tw = g.timeWidth(F_DISPLAY, em, big);
        float tailW = 0;
        float tailFs = em * (T.mode == 2 ? 0.2f : 0.34f);
        if (!tail.empty()) tailW = g.textWidth(T.mode == 2 ? F_MONO_M : F_LIGHT, tailFs, tail, T.mode == 2 ? 0.1f : 0.f) + em * 0.04f;
        float x0 = cx - (tw + tailW) * 0.5f;
        Color col = pulse ? mulA(bigCol, 0.45f + 0.55f * pul) : bigCol;
        g.timeText(F_DISPLAY, em, x0, base, big, col, lerp(col, P.ink2, accentBig ? 0.f : 0.5f), Align::Left);
        if (!tail.empty()) {
            if (T.mode == 2) g.text(F_MONO_M, tailFs, x0 + tw + em * 0.04f, base - capH + g.capHeight(F_MONO_M, tailFs), tail, P.ink2, Align::Left, 0.1f);
            else g.text(F_LIGHT, tailFs, x0 + tw + em * 0.04f, base, tail, P.ink3, Align::Left);
        }
        g.text(F_MONO, std::max(13.f * s, 0.085f * R), cx, base + 0.17f * R + 0.04f * R, sub, pulse ? P.accent : P.ink2, Align::Center, 0.16f);
    }

    // buttons
    {
        float bh = 62 * s, y = cy + R + 34 * s;
        bool running = T.mode == 0 ? T.running : (T.mode == 1 ? T.swRunning : (T.alarmOn || T.ringing));
        std::wstring pl = T.mode == 2 ? (T.ringing ? L"DISMISS" : (T.alarmOn ? L"DISARM" : L"ARM")) : (T.finished ? L"DISMISS" : (running ? L"PAUSE" : L"START"));
        float w1 = 210 * s, w2 = 150 * s, gap = 16 * s;
        float x = cx - (w1 + w2 + gap) * 0.5f;
        drawButton(c, x, y, w1, bh, pl, true, HIT_START, F_MONO_M, 17.f);
        drawButton(c, x + w1 + gap, y, w2, bh, (T.mode == 1 && T.swRunning) ? L"LAP" : L"RESET", false, (T.mode == 1 && T.swRunning) ? HIT_LAP : HIT_RESET, F_MONO_M, 17.f);
    }

    // stopwatch laps
    if (T.mode == 1 && !T.laps.empty()) {
        float x = cx + R + 80 * s, wcol = W - m - x;
        if (wcol > 220 * s) {
            g.text(F_MONO, 14.f * s, x, cy - R + 20 * s, L"LAPS", P.ink3, Align::Left, 0.22f);
            int n = (int)T.laps.size(), shown = std::min(n, 8);
            for (int i = 0; i < shown; ++i) {
                int idx = n - 1 - i;
                double t = T.laps[idx], prev = idx > 0 ? T.laps[idx - 1] : 0.0, d = t - prev;
                float yy = cy - R + 70 * s + i * 54 * s;
                int mm = (int)(t / 60), ss = (int)t % 60, cc = (int)((t - std::floor(t)) * 100);
                int dm = (int)(d / 60), ds = (int)d % 60, dc = (int)((d - std::floor(d)) * 100);
                g.text(F_MONO, 14.f * s, x, yy, Ws(fmt("%02d", idx + 1)), P.ink3, Align::Left, 0.1f);
                const std::wstring totW = Ws(fmt("%02d:%02d.%02d", mm, ss, cc));
                const std::wstring delW = Ws(fmt("+%02d:%02d.%02d", dm, ds, dc));
                const float delWpx = g.textWidth(F_MONO, 14.5f * s, delW, 0.04f);
                const float totRoom = std::max(60.f * s, wcol - 46 * s - delWpx - 12 * s);
                float totFs = 28.f * s;
                const float totPx = g.textWidth(F_LIGHT, totFs, totW);
                if (totPx > totRoom) totFs = std::max(14.f * s, totFs * totRoom / totPx);
                g.text(F_LIGHT, totFs, x + 46 * s, yy + 2 * s, totW, P.ink);
                g.text(F_MONO, 14.5f * s, x + wcol, yy, delW, P.ink2, Align::Right, 0.04f);
                g.line(x, yy + 16 * s, x + wcol, yy + 16 * s, P.line, std::max(1.f, s), false);
            }
        }
    }
    // hint line (fades out cleanly whenever the bottom dock blooms open)
    const float hintA = (1.f - smoothstep(0.f, 0.35f, dockT)) * 0.8f;
    if (hintA > 0.02f) {
        g.text(F_MONO, 13.f * s, cx, H - 40 * s, T.mode == 1 ? L"SPACE START/STOP  \xB7  L LAP  \xB7  R RESET" : L"0\u20139 SET TIME  \xB7  +/\u2212 ADJUST  \xB7  SPACE START/PAUSE  \xB7  R RESET", mulA(P.ink3, hintA), Align::Center, 0.12f);
    }
}

// ---------------------------------------------------------------- Focus Studio (dedicated lo-fi focus sanctuary tab)
void App::renderFocus(Gfx& g, int w, int h) {
    const float W = (float)w, H = (float)h, s = layoutScale(w, h);
    const bool portrait = W < H * 0.90f;
    Ctx c{g, *this, pal, W, H, s};
    const Palette& P = pal;
    const FocusState& F = focus;
    const float m = (portrait ? 34.f : 56.f) * s;

    // 1. Ambient weather / starry sky backdrop chosen by the user
    if (st.focusFx > 0) drawFocusBackdrop(c);

    // 2. Top studio corner cards: smaller live current clock + date (top-left) and live weather + icon + AQI (top-right)
    {
        const util::LocalTime L = util::toLocal(now, tzOff);
        const ClockParts cp = clockParts(*this);
        if (!portrait && W >= 960.f * s) {
            const float cardY = 38.f * s, cardH = 82.f * s, cardR = 20.f * s;
            const Color cardBg = withA(P.plate, P.dark ? (st.focusFx > 0 ? 0.56f : 0.36f) : (st.focusFx > 0 ? 0.62f : 0.42f));

            // Top-left card: smaller live clock & date
            const float cEm = 40.f * s, cCap = g.capHeight(F_DISPLAY, cEm);
            const float twC = g.timeWidth(F_DISPLAY, cEm, cp.hm);
            const float amW = cp.ampm.empty() ? 0.f : (g.textWidth(F_MONO_M, 11.5f * s, cp.ampm, 0.12f) + 8.f * s);
            const std::wstring dLine = Ws(fmt("%s, %d %s", util::WEEKDAYS3[L.wday], L.d, util::MONTHS[L.m - 1]));
            const float dwC = g.textWidth(F_SERIF_I, 18.f * s, dLine);
            const float leftW = std::min(W * 0.27f, std::max(twC + amW, dwC) + 40.f * s);
            g.fillRoundRect(m, cardY, leftW, cardH, cardR, cardBg);
            g.strokeRoundRect(m + 0.5f, cardY + 0.5f, leftW - 1.f, cardH - 1.f, cardR, mulA(P.line, 0.85f), std::max(1.f, s));
            const float cBase = cardY + 18.f * s + cCap;
            g.timeText(F_DISPLAY, cEm, m + 20.f * s, cBase, cp.hm, P.ink, lerp(P.ink, P.ink2, 0.42f), Align::Left);
            if (!cp.ampm.empty()) {
                const float afs = 11.5f * s;
                g.text(F_MONO_M, afs, m + 20.f * s + twC + 7.f * s, cBase - cCap + g.capHeight(F_MONO_M, afs) + 1.f * s, cp.ampm, P.ink2, Align::Left, 0.12f);
            }
            g.text(F_SERIF_I, 18.f * s, m + 20.f * s, cBase + 22.f * s, g.ellipsize(F_SERIF_I, 18.f * s, dLine, leftW - 36.f * s), P.ink2, Align::Left);

            // Top-right card: live weather icon + temp + condition + place (+ compact AQI if available)
            if (wx.valid) {
                const weather::Current& cu = wx.cur;
                const weather::Kind kd = weather::describe(cu.code, cu.isDay);
                const std::wstring tW = Ws(tempStr(*this, cu.temp));
                const float tFs = 34.f * s, tWpx = g.textWidth(F_LIGHT, tFs, tW);
                const std::wstring cW = Ws(kd.text);
                std::string meta = util::upper(st.geo.name);
                if (wx.hasAir) meta += fmt("  \xC2\xB7  AQI %.0f", wx.aqi);
                const std::wstring mW = Ws(meta);
                const float txtColW = std::min(W * 0.21f, std::max(g.textWidth(F_LIGHT, 16.5f * s, cW), g.textWidth(F_MONO, 10.5f * s, mW, 0.12f)) + 8.f * s);
                const float icoS = 40.f * s;
                const float rightW = std::min(W * 0.30f, 18.f * s + icoS + 12.f * s + tWpx + 14.f * s + txtColW + 18.f * s);
                const float rx = W - m - rightW;
                g.fillRoundRect(rx, cardY, rightW, cardH, cardR, cardBg);
                g.strokeRoundRect(rx + 0.5f, cardY + 0.5f, rightW - 1.f, cardH - 1.f, cardR, mulA(P.line, 0.85f), std::max(1.f, s));
                g.weatherIcon(kd.icon, rx + 18.f * s + icoS * 0.5f, cardY + cardH * 0.5f, icoS, P.ink, P.accent, P.plate);
                const float tx0 = rx + 18.f * s + icoS + 12.f * s;
                g.text(F_LIGHT, tFs, tx0, cardY + cardH * 0.5f + g.capHeight(F_LIGHT, tFs) * 0.5f, tW, P.ink);
                const float cx0 = tx0 + tWpx + 14.f * s;
                const float availTxt = std::max(60.f * s, rx + rightW - 18.f * s - cx0);
                g.text(F_LIGHT, 16.5f * s, cx0, cardY + 34.f * s, g.ellipsize(F_LIGHT, 16.5f * s, cW, availTxt), P.ink);
                g.text(F_MONO, 10.5f * s, cx0, cardY + 56.f * s, g.ellipsize(F_MONO, 10.5f * s, mW, availTxt, 0.12f), P.ink3, Align::Left, 0.12f);
            } else {
                const std::wstring pW = Ws(util::upper(st.geo.name));
                const float rightW = std::min(W * 0.25f, g.textWidth(F_MONO, 12.f * s, pW, 0.14f) + 40.f * s);
                const float rx = W - m - rightW;
                g.fillRoundRect(rx, cardY + 18.f * s, rightW, 44.f * s, 22.f * s, cardBg);
                g.strokeRoundRect(rx + 0.5f, cardY + 18.5f * s, rightW - 1.f, 43.f * s, 22.f * s, mulA(P.line, 0.85f), std::max(1.f, s));
                g.text(F_MONO, 12.f * s, rx + rightW * 0.5f, cardY + 40.f * s + g.capHeight(F_MONO, 12.f * s) * 0.5f, pW, P.ink2, Align::Center, 0.14f);
            }
        } else {
            // Compact / portrait top strip: smaller live clock, date, and weather icon + temp
            std::string line = util::narrow(cp.hm) + (cp.ampm.empty() ? "" : (" " + util::narrow(cp.ampm))) +
                               fmt("  \xC2\xB7  %s %d %s", util::WEEKDAYS3[L.wday], L.d, util::MONTHS3[L.m - 1]);
            if (wx.valid) {
                line += fmt("  \xC2\xB7  %s %s", tempStr(*this, wx.cur.temp).c_str(), util::upper(weather::describe(wx.cur.code, wx.cur.isDay).text).c_str());
                if (wx.hasAir) line += fmt("  \xC2\xB7  AQI %.0f", wx.aqi);
            }
            const std::wstring lw = g.ellipsize(F_MONO, 12.5f * s, Ws(util::upper(line)), W - 2 * m, 0.10f);
            g.text(F_MONO, 12.5f * s, W * 0.5f, 36.f * s, lw, P.ink2, Align::Center, 0.10f);
        }
    }

    // 3. Top-center: Focus Studio eyebrow + dedicated Focus duration presets (default 25m Pomodoro)
    {
        g.text(F_MONO_M, 13.f * s, W * 0.5f, (portrait ? 74.f : 62.f) * s, L"FOCUS STUDIO", P.accent, Align::Center, 0.24f);
        static const wchar_t* pl[7] = {L"5m", L"10m", L"15m", L"25m", L"30m", L"45m", L"1h"};
        float bh = 34 * s, gap = 8 * s, pad = 28 * s, pfs = 14.f, total = 0, bw[7];
        auto measureP = [&]() {
            total = 0;
            for (int i = 0; i < 7; ++i) { bw[i] = g.textWidth(F_MONO_M, pfs * s, pl[i], 0.06f) + pad * s; total += bw[i]; }
            total += gap * 6;
        };
        measureP();
        const float maxW = portrait ? (W - 2 * m) : std::min(W - 2 * m, W * 0.44f);
        if (total > maxW && total > 1.f) {
            const float f = std::max(0.68f, maxW / total);
            pfs *= f; pad *= f; gap *= f;
            measureP();
        }
        float x = (W - total) * 0.5f, y = (portrait ? 94.f : 84.f) * s;
        for (int i = 0; i < 7; ++i) {
            bool act = std::fabs(F.durationSec - FOCUS_PRESETS[i]) < 0.5 && F.entry.empty();
            drawButton(c, x, y, bw[i], bh, pl[i], act, HIT_FPRESET0 + i, F_MONO_M, pfs); x += bw[i] + gap;
        }
    }

    // 4. Center: Frosted glass focus medallion + calm breathing halo + focus countdown ring
    const float sidePanelW = 228.f * s;
    const float landR = std::min(0.248f * H, 0.258f * W);
    const float landSideRoom = (W - m) - (W * 0.5f + landR + 24.f * s);
    const bool useSidePanel = (!portrait && landSideRoom >= sidePanelW + 24.f * s);

    float cx = W * 0.5f;
    float cy = useSidePanel ? (H * 0.485f) : (H * (portrait ? 0.415f : 0.43f));
    float R = useSidePanel ? landR : std::min(0.355f * W, (portrait ? 0.205f : 0.215f) * H);
    const float ringW = std::max(4.f, 9.5f * s);

    double rem = focusRemaining();
    float frac = 1.f; bool ready = false;
    std::wstring big, sub; Color bigCol = P.ink; bool accentBig = false; bool pulse = false;
    if (F.finished) {
        frac = 0.f; pulse = true;
        big = hmsStr(0, F.durationSec >= 3600);
        sub = L"SESSION COMPLETE";
    } else if (!F.entry.empty()) {
        std::string e = F.entry; while (e.size() < 6) e = "0" + e;
        big = Ws(e.substr(0, 2) + ":" + e.substr(2, 2) + ":" + e.substr(4, 2));
        bigCol = P.accent; accentBig = true;
        sub = L"TYPE DIGITS \xB7 SPACE TO START";
        ready = true;
    } else {
        frac = (float)(rem / std::max(1.0, F.durationSec));
        big = hmsStr(rem, F.durationSec >= 3600);
        if (F.running) sub = Ws("ENDS " + util::upper(clockStr(*this, F.endAt)));
        else if (rem < F.durationSec - 0.5) sub = L"PAUSED";
        else { sub = L"FOCUS \xB7 READY"; ready = true; }
    }

    const float pul = pulse ? 0.5f + 0.5f * (float)std::sin(now * 5.0) : 1.f;
    const float breath = F.running ? (0.5f + 0.5f * (float)std::sin(now * 0.90)) : 0.f;

    // Frosted glass focus medallion keeps focus digits crisp over rain, snow, clouds or starry space
    {
        const float mr = R + 24.f * s;
        g.fillCircle(cx, cy, mr, withA(P.plate, P.dark ? (st.focusFx > 0 ? 0.56f : 0.34f) : (st.focusFx > 0 ? 0.48f : 0.30f)));
        g.strokeCircle(cx, cy, mr, mulA(P.line, 0.85f), std::max(1.f, s));
    }

    // Calm breathing glow + progress ring + luminous arc head dot
    if (F.running || pulse)
        g.glow(cx, cy, R * (1.24f + 0.08f * breath), P.accent, 0.10f + 0.07f * breath + (pulse ? 0.10f * pul : 0.f));
    g.strokeCircle(cx, cy, R, mulA(P.ink, 0.10f), ringW);
    {
        float sweep = 360.f * clamp01(frac);
        Color rc = pulse ? withA(P.accent, 0.35f + 0.65f * pul) : (ready ? withA(P.accent, 0.42f) : P.accent);
        if (sweep > 0.4f) {
            if (sweep > 359.5f) g.strokeCircle(cx, cy, R, rc, ringW);
            else g.arc(cx, cy, R, 0.f, sweep, rc, ringW, true);
        }
        if (!pulse && F.running && sweep > 1.f && sweep < 359.5f) {
            float a = (float)astro::rad(sweep);
            const float dx = cx + R * std::sin(a), dy = cy - R * std::cos(a);
            g.glow(dx, dy, ringW * 2.4f, P.accent, 0.55f);
            g.fillCircle(dx, dy, ringW * 0.72f, P.ink);
        }
    }

    // Center focus digits
    {
        float tw1 = g.timeWidth(F_DISPLAY, 1.f, big);
        float em = std::min(0.40f * R, 1.5f * R / std::max(1.f, tw1));
        float capH = g.capHeight(F_DISPLAY, em);
        float base = cy + capH * 0.5f - 0.03f * R;
        float tw = g.timeWidth(F_DISPLAY, em, big);
        float x0 = cx - tw * 0.5f;
        Color col = pulse ? mulA(bigCol, 0.45f + 0.55f * pul) : bigCol;
        g.timeText(F_DISPLAY, em, x0, base, big, col, lerp(col, P.ink2, accentBig ? 0.f : 0.5f), Align::Left);
        g.text(F_MONO, std::max(13.f * s, 0.085f * R), cx, base + 0.17f * R + 0.04f * R, sub, pulse ? P.accent : P.ink2, Align::Center, 0.16f);
    }

    // Start / Pause and Reset buttons
    const float btnH = (useSidePanel ? 56.f : 50.f) * s;
    const float btnY = cy + R + (useSidePanel ? 32.f : 24.f) * s;
    {
        std::wstring pl = F.finished ? L"DISMISS" : (F.running ? L"PAUSE" : L"START");
        float w1 = (useSidePanel ? 200.f : 184.f) * s, w2 = (useSidePanel ? 144.f : 132.f) * s, gap = 14.f * s;
        float x = cx - (w1 + w2 + gap) * 0.5f;
        drawButton(c, x, btnY, w1, btnH, pl, true, HIT_FSTART, F_MONO_M, useSidePanel ? 16.5f : 15.5f);
        drawButton(c, x + w1 + gap, btnY, w2, btnH, L"RESET", false, HIT_FRESET, F_MONO_M, useSidePanel ? 16.5f : 15.5f);
    }

    // 5. Collapsible Lo-Fi Ambience & Theme Studio Orb -> Expanded Card on Hover (zero clutter at rest, clear of bottom dock)
    {
        static const wchar_t* fxLabels[8] = {L"OFF", L"LIVE", L"RAIN", L"SNOW", L"CLOUDS", L"FOG", L"STORM", L"STARS"};
        const float mx = mouseX - shiftX, my = mouseY - shiftY;
        const float idleS = (float)(now - lastMouseMove);
        const std::wstring thName = Ws(util::upper(themeName()));

        const float orbD = 50.f * s;
        float expX = 0, expY = 0, expW = 0, expH = 0, cardR = 20.f * s;
        float orbX = 0, orbY = 0;

        const float pad = (useSidePanel ? 16.f : 14.f) * s;
        const float colGap = (useSidePanel ? 7.f : 6.f) * s, rowGap = (useSidePanel ? 7.f : 6.f) * s;
        const float ch = (useSidePanel ? 30.f : 28.f) * s, navD = (useSidePanel ? 28.f : 26.f) * s;
        if (useSidePanel) {
            const float gridH = 4.f * ch + 3.f * rowGap;
            expW = sidePanelW;
            expH = pad + 22.f * s + gridH + 14.f * s + 1.f + 14.f * s + 20.f * s + navD + pad;
            expX = W - m - expW;
            const float topLimit = 38.f * s + 82.f * s + 22.f * s;
            const float botLimit = std::max(topLimit, H - 116.f * s - expH);
            expY = std::max(topLimit, std::min(botLimit, cy - expH * 0.5f));
            orbX = W - m - orbD;
            orbY = std::max(topLimit, std::min(H - 116.f * s - orbD, cy - orbD * 0.5f));
            cardR = 20.f * s;
        } else {
            expW = std::min(W - 2.f * m, 440.f * s);
            expH = pad + 2.f * ch + rowGap + 10.f * s + 1.f + 10.f * s + navD + pad;
            expX = (W - expW) * 0.5f;
            expY = std::min(btnY + btnH + 22.f * s, H - 118.f * s - expH);
            orbX = cx - orbD * 0.5f;
            orbY = std::min(btnY + btnH + 28.f * s, H - 118.f * s - orbD);
            cardR = 18.f * s;
        }

        // Smooth hover morph with generous hysteresis so moving across buttons never flickers shut
        const bool ptrInWin = mouseX >= 0 && mouseX <= W && mouseY >= 0 && mouseY <= H;
        const bool inOrb = ptrInWin && mx >= orbX - 14.f * s && mx <= orbX + orbD + 14.f * s &&
                           my >= orbY - 14.f * s && my <= orbY + orbD + 14.f * s;
        const bool inExp = ptrInWin && mx >= expX - 22.f * s && mx <= expX + expW + 22.f * s &&
                           my >= expY - 22.f * s && my <= expY + expH + 22.f * s;
        focusOrbTarget = ((inOrb || (focusOrbT > 0.04f && inExp)) && !showSettings && !showHelp && !isSaver) ? 1.f : 0.f;
        const float kOrb = 1.f - std::exp(-(float)dt * (focusOrbTarget > focusOrbT ? 15.f : 7.5f));
        focusOrbT += (focusOrbTarget - focusOrbT) * kOrb;
        if (std::fabs(focusOrbT - focusOrbTarget) < 0.004f) focusOrbT = focusOrbTarget;
        const float e = smoothstep(0.f, 1.f, focusOrbT);

        // Liquid-glass container morphing seamlessly between round orb and full Studio Card
        auto mixf = [](float a0, float b0, float t0) { return a0 + (b0 - a0) * t0; };
        const float curX = mixf(orbX, expX, e);
        const float curY = mixf(orbY, expY, e);
        const float curW = mixf(orbD, expW, e);
        const float curH = mixf(orbD, expH, e);
        const float curR = mixf(orbD * 0.5f, cardR, e);

        const float orbIdle = (F.running && idleS > 4.0f) ? (1.f - 0.42f * clamp01((idleS - 4.0f) / 1.2f)) : 1.f;
        const float vis = mixf(orbIdle, 1.f, clamp01(e * 3.f));

        // Soft ambient halo behind orb when ambience is active + frosted glass container
        const float ocx = curX + curW * 0.5f, ocy = curY + curH * 0.5f;
        if (st.focusFx > 0 && e < 0.6f) {
            g.glow(ocx, ocy, orbD * 0.78f, P.accent, 0.16f * (1.f - e / 0.6f) * vis);
        }
        g.fillRoundRect(curX, curY + 3.f * s, curW, curH, curR, Color{0, 0, 0, (P.dark ? 0.22f : 0.10f) * vis});
        const float fillA = mixf(P.dark ? 0.68f : 0.74f, P.dark ? (st.focusFx > 0 ? 0.80f : 0.74f) : (st.focusFx > 0 ? 0.84f : 0.78f), e);
        g.fillRoundRect(curX, curY, curW, curH, curR, withA(P.plate, fillA * vis));
        const Color rimC = (inOrb && e < 0.5f) ? withA(P.accent, 0.55f * vis) : mulA(P.line, (0.95f + 0.25f * e) * vis);
        g.strokeRoundRect(curX + 0.5f, curY + 0.5f, curW - 1.f, curH - 1.f, curR, rimC, std::max(1.f, s));

        // Collapsed Orb Icon: minimalist 3-track studio tuner glyph + subtle active ambience dot
        const float orbA = (1.f - smoothstep(0.f, 0.36f, e)) * vis;
        if (orbA > 0.02f) {
            const float hw = 9.5f * s, dy = 6.2f * s, kr = 2.45f * s;
            const float trackY[3] = {ocy - dy, ocy, ocy + dy};
            const float knobX[3] = {ocx - 3.6f * s, ocx + 4.0f * s, ocx - 1.0f * s};
            const Color trkCol = mulA(P.ink2, 0.78f * orbA);
            const float lwTrk = std::max(1.3f, 1.55f * s);
            for (int r = 0; r < 3; ++r) {
                g.line(ocx - hw, trackY[r], ocx + hw, trackY[r], trkCol, lwTrk, true);
                const Color kCol = (r == 1 && st.focusFx > 0) ? withA(P.accent, orbA) : mulA(P.ink, 0.94f * orbA);
                g.fillCircle(knobX[r], trackY[r], kr, kCol);
            }
            if (st.focusFx > 0) {
                const float dx = curX + curW - 9.5f * s, ddy = curY + 9.5f * s;
                g.glow(dx, ddy, 7.5f * s, P.accent, 0.50f * orbA);
                g.fillCircle(dx, ddy, 2.5f * s, withA(P.accent, orbA));
            }
        }

        // Expanded Studio Card Contents (fades cleanly in after container opens past 40%)
        const float contentA = smoothstep(0.40f, 1.f, e);
        if (contentA > 0.02f) {
            const bool canHit = e > 0.65f;
            Palette Pb = P;
            for (Color* col : {&Pb.ink, &Pb.ink2, &Pb.ink3, &Pb.line, &Pb.panel, &Pb.plate, &Pb.accent}) col->a *= contentA;
            Ctx cb{g, *this, Pb, W, H, s};

            auto drawThemeNav = [&](float bx, float by, float d, int dir, int hitId) {
                const float br = d * 0.5f, cyB = by + br;
                const bool hov = canHit && mouseX >= 0 && mx >= bx - 3.f * s && mx <= bx + d + 3.f * s && my >= by - 3.f * s && my <= by + d + 3.f * s;
                Color bg = Pb.panel;
                bg.a = std::max(bg.a, P.dark ? 0.085f : 0.075f) * contentA;
                g.fillRoundRect(bx, by, d, d, br, bg);
                const Color ac = tint(P, P.accent);
                if (hov) g.fillRoundRect(bx, by, d, d, br, withA(ac, (P.dark ? 0.22f : 0.16f) * contentA));
                Color brd = hov ? withA(ac, (P.dark ? 0.78f : 0.72f) * contentA)
                                : withA(P.line, std::min(1.f, P.line.a * (P.dark ? 1.45f : 1.35f)) * contentA);
                g.strokeRoundRect(bx + 0.5f, by + 0.5f, d - 1.f, d - 1.f, br, brd, std::max(1.f, s));
                const float chv = d * 0.195f, cwv = d * 0.135f;
                const float ccx = bx + br + (float)dir * (cwv * 0.18f);
                const std::vector<Pt> chev = dir < 0
                    ? std::vector<Pt>{{ccx + cwv, cyB - chv}, {ccx - cwv, cyB}, {ccx + cwv, cyB + chv}}
                    : std::vector<Pt>{{ccx - cwv, cyB - chv}, {ccx + cwv, cyB}, {ccx - cwv, cyB + chv}};
                const Color col = hov ? mulA(P.ink, contentA) : mulA(P.ink2, (P.dark ? 0.92f : 0.96f) * contentA);
                g.polyline(chev, false, col, hov ? std::max(1.6f, 1.85f * s) : std::max(1.35f, 1.55f * s), true);
                if (canHit) addHit(*this, bx - 3.f * s, by - 3.f * s, d + 6.f * s, d + 6.f * s, hitId);
            };

            if (e > 0.35f) addHit(*this, curX, curY, curW, curH, HIT_DOCK_BG);
            g.pushRoundRectClip(curX + 1.f, curY + 1.f, std::max(1.f, curW - 2.f), std::max(1.f, curH - 2.f), std::max(0.5f, curR - 1.f));
            if (useSidePanel) {
                const float eyY = expY + pad + g.capHeight(F_MONO, 10.5f * s);
                g.text(F_MONO, 10.5f * s, expX + pad, eyY, L"AMBIENCE", Pb.ink3, Align::Left, 0.20f);
                if (st.focusFx > 0) {
                    const float dotX = expX + expW - pad - 4.f * s, dotY = eyY - g.capHeight(F_MONO, 10.5f * s) * 0.5f;
                    g.glow(dotX, dotY, 10.f * s, Pb.accent, 0.45f * contentA);
                    g.fillCircle(dotX, dotY, 3.0f * s, Pb.accent);
                }
                const float gridY = expY + pad + 22.f * s;
                const float cw = (expW - 2.f * pad - colGap) * 0.5f;
                for (int i = 0; i < 8; ++i) {
                    const int colI = i % 2, rowI = i / 2;
                    const float bx = expX + pad + colI * (cw + colGap);
                    const float by = gridY + rowI * (ch + rowGap);
                    drawButton(cb, bx, by, cw, ch, fxLabels[i], st.focusFx == i, canHit ? (HIT_FOCUSFX0 + i) : 0, F_MONO_M, 11.2f);
                }
                const float divY = gridY + (4.f * ch + 3.f * rowGap) + 14.f * s;
                g.line(expX + pad, divY, expX + expW - pad, divY, Pb.line, std::max(1.f, s), false);
                const float thEyY = divY + 14.f * s + g.capHeight(F_MONO, 10.5f * s);
                g.text(F_MONO, 10.5f * s, expX + pad, thEyY, L"THEME", Pb.ink3, Align::Left, 0.20f);
                const float navY = divY + 14.f * s + 20.f * s;
                const float lx = expX + pad, rx = expX + expW - pad - navD;
                drawThemeNav(lx, navY, navD, -1, HIT_THEME_PREV);
                const float midX = lx + navD + 6.f * s, midW = std::max(40.f * s, rx - midX - 6.f * s);
                const std::wstring thEll = g.ellipsize(F_MONO_M, 11.5f * s, thName, midW, 0.10f);
                g.text(F_MONO_M, 11.5f * s, midX + midW * 0.5f, navY + navD * 0.5f + g.capHeight(F_MONO_M, 11.5f * s) * 0.5f, thEll, Pb.ink2, Align::Center, 0.10f);
                if (canHit) addHit(*this, midX, navY, midW, navD, HIT_THEME);
                drawThemeNav(rx, navY, navD, +1, HIT_THEME);
            } else {
                const float cw = (expW - 2.f * pad - 3.f * colGap) * 0.25f;
                const float pfs = cw < 72.f * s ? 10.0f : 11.0f;
                for (int i = 0; i < 8; ++i) {
                    const int colI = i % 4, rowI = i / 4;
                    const float bx = expX + pad + colI * (cw + colGap);
                    const float by = expY + pad + rowI * (ch + rowGap);
                    drawButton(cb, bx, by, cw, ch, fxLabels[i], st.focusFx == i, canHit ? (HIT_FOCUSFX0 + i) : 0, F_MONO_M, pfs);
                }
                const float divY = expY + pad + 2.f * ch + rowGap + 10.f * s;
                g.line(expX + pad, divY, expX + expW - pad, divY, Pb.line, std::max(1.f, s), false);
                const float navY = divY + 10.f * s;
                g.text(F_MONO, 10.5f * s, expX + pad, navY + navD * 0.5f + g.capHeight(F_MONO, 10.5f * s) * 0.5f, L"THEME", Pb.ink3, Align::Left, 0.20f);
                const float rx = expX + expW - pad - navD;
                const float thBoxW = std::min(148.f * s, std::max(72.f * s, expW * 0.38f));
                const float lx = rx - thBoxW - navD;
                drawThemeNav(lx, navY, navD, -1, HIT_THEME_PREV);
                const std::wstring thEll = g.ellipsize(F_MONO_M, 11.2f * s, thName, thBoxW - 10.f * s, 0.10f);
                g.text(F_MONO_M, 11.2f * s, lx + navD + thBoxW * 0.5f, navY + navD * 0.5f + g.capHeight(F_MONO_M, 11.2f * s) * 0.5f, thEll, Pb.ink2, Align::Center, 0.10f);
                if (canHit) addHit(*this, lx + navD, navY, thBoxW, navD, HIT_THEME);
                drawThemeNav(rx, navY, navD, +1, HIT_THEME);
            }
            g.popRoundRectClip();
        }
    }

    // Subtle footer hint line (fades out cleanly whenever the main bottom dock slides up)
    const float hintA = (1.f - smoothstep(0.f, 0.35f, dockT)) * 0.72f;
    if (hintA > 0.02f) {
        g.text(F_MONO, 12.f * s, cx, H - 36 * s, L"0\u20139 SET TIME  \xB7  +/\u2212 ADJUST  \xB7  SPACE START/PAUSE  \xB7  R RESET  \xB7  T THEME", mulA(P.ink3, hintA), Align::Center, 0.12f);
    }
}

// ---------------------------------------------------------------- Almanac
// The Almanac is a set of blocks (the Moon, the Sun table, the Moon table, Today, the planets, the 7-day forecast, the year bar).
// They are measured first and then placed in 3, 2 or 1 columns, whichever lets the text be biggest for the window you have. Every
// block knows its own height (the H_ numbers, in "design units" = pixels at scale 1), so nothing can overlap when you resize.
// If you add a row to a block, add its height to that block's H_ number.
namespace {
enum Block { B_MOON, B_SUN, B_MOONTAB, B_TODAY, B_PLANETS, B_FORECAST, B_YEAR };
constexpr float M_X = 72, M_Y = 56, GAP_X = 56, GAP_Y = 26;                    // margins and gaps
constexpr float H_MOON_EXTRA = 350;                                              // under the Moon picture: name, numbers, four phase times
constexpr float H_SUN = 482, H_MOONTAB = 237, H_PLANETS = 346, H_TODAY = 278, H_FORECAST = 372, H_YEAR = 50;
float blockH(int b, float moonSize) {
    switch (b) {
        case B_MOON: return moonSize + H_MOON_EXTRA;
        case B_SUN: return H_SUN;
        case B_MOONTAB: return H_MOONTAB;
        case B_TODAY: return H_TODAY;
        case B_PLANETS: return H_PLANETS;
        case B_FORECAST: return H_FORECAST;
        default: return H_YEAR;
    }
}
struct AlmLayout { std::vector<std::vector<int>> cols; std::vector<float> share; float moonSize, needW, needH, maxColW; };
// the layouts to choose from: the one with the biggest scale wins
const std::vector<AlmLayout>& almLayouts() {
    static const std::vector<AlmLayout> v = [] {
        std::vector<AlmLayout> out;
        out.push_back({{{B_MOON}, {B_SUN, B_MOONTAB, B_TODAY}, {B_PLANETS, B_FORECAST, B_YEAR}}, {0.34f, 0.31f, 0.35f}, 400.f, 1440.f, 0.f, 520.f});
        out.push_back({{{B_MOON, B_PLANETS, B_YEAR}, {B_SUN, B_MOONTAB, B_TODAY, B_FORECAST}}, {0.5f, 0.5f}, 460.f, 1160.f, 0.f, 640.f});
        out.push_back({{{B_MOON, B_SUN, B_MOONTAB, B_TODAY, B_PLANETS, B_FORECAST, B_YEAR}}, {1.f}, 520.f, 640.f, 0.f, 760.f});
        for (auto& L : out) {
            float tall = 0;
            for (auto& col : L.cols) { float h = 0; for (size_t i = 0; i < col.size(); ++i) h += blockH(col[i], L.moonSize) + (i ? GAP_Y : 0); tall = std::max(tall, h); }
            L.needH = tall + 2 * M_Y;
        }
        return out;
    }();
    return v;
}
void header(Ctx& c, float x, float y, float w, const wchar_t* title, const Color& col) {
    c.g.text(F_MONO_M, 14.f * c.s, x, y, title, col, Align::Left, 0.22f);
    c.g.line(x, y + 12 * c.s, x + w, y + 12 * c.s, c.P.line, std::max(1.f, c.s), false);
}
float row(Ctx& c, float x, float y, float w, const std::string& label, const std::string& value, bool dimValue = false) {
    Gfx& g = c.g; float s = c.s;
    // the value keeps what it needs (up to 62% of the row) and the label gets the rest, so the two never touch
    const std::wstring vs = g.ellipsize(F_MEDIUM, 20.5f * s, Ws(value), w * 0.62f);
    const float vw = g.textWidth(F_MEDIUM, 20.5f * s, vs);
    g.text(F_LIGHT, 20.5f * s, x, y, g.ellipsize(F_LIGHT, 20.5f * s, Ws(label), std::max(20.f * s, w - vw - 14 * s)), c.P.ink2);
    g.text(F_MEDIUM, 20.5f * s, x + w, y, vs, dimValue ? c.P.ink3 : c.P.ink, Align::Right);
    g.line(x, y + 11 * s, x + w, y + 11 * s, mulA(c.P.line, 0.5f), std::max(1.f, s), false);
    return y + 35 * s;
}
std::string tm(const App& a, bool ok, double t) { return ok ? clockStr(a, t) : "\xE2\x80\x94"; }
}

void App::renderAlmanac(Gfx& g, int w, int h) {
    const float W = (float)w, H = (float)h;
    const astro::MoonInfo& mo = moon;
    const MoonDetail& md = moonDetail;

    // ---- 1. choose the layout and the scale: the biggest text that still fits
    const std::vector<AlmLayout>& opts = almLayouts();
    int best = 0; float bestU = -1.f;
    for (size_t i = 0; i < opts.size(); ++i) {
        const float u = std::min(W / opts[i].needW, H / opts[i].needH);
        if (u > bestU + 1e-4f) { bestU = u; best = (int)i; }
    }
    const AlmLayout& L = opts[(size_t)best];
    const float s = std::max(0.2f, bestU);                                        // pixels per design unit
    Ctx c{g, *this, pal, W, H, s};
    const Palette& P = pal;
    const float night = smoothstep(-4.f, -14.f, (float)sun.hor.alt);

    // ---- 2. column positions: the columns share the width, centred when the window is much wider than it needs
    const int nc = (int)L.cols.size();
    const float gapX = GAP_X * s;
    float usable = W - 2 * M_X * s - (nc - 1) * gapX;
    usable = std::min(usable, L.maxColW * s * nc * 1.12f);
    const float x0 = (W - usable - (nc - 1) * gapX) * 0.5f;
    float tallest = 0;
    for (auto& col : L.cols) { float hh = 0; for (size_t i = 0; i < col.size(); ++i) hh += blockH(col[i], L.moonSize) + (i ? GAP_Y : 0); tallest = std::max(tallest, hh); }
    const float y0 = std::max(M_Y * s, (H - tallest * s) * 0.5f);

    float cx0 = x0;
    for (int ci = 0; ci < nc; ++ci) {
        const float cw = usable * L.share[(size_t)ci];
        float y = y0;
        for (size_t bi = 0; bi < L.cols[(size_t)ci].size(); ++bi) {
            const int b = L.cols[(size_t)ci][bi];
            const float bh = blockH(b, L.moonSize) * s;
            if (b == B_YEAR) y = std::max(y, y0 + tallest * s - bh);                // the year bar sits at the bottom of the tallest column
            const float x = cx0, yy0 = y;
            switch (b) {
            case B_MOON: {
                const float size = std::min(L.moonSize * s, cw);
                const float cx = x + cw * 0.5f, cy = yy0 + 50 * s + size * 0.5f;
                g.glow(cx, cy, size * 0.85f, tint(P, rgb(0xBFD0FF)), 0.16f * (0.35f + 0.65f * night));
                g.drawBitmap(moonLarge, cx, cy, size, moonViewRotation(mo, st.geo), 1.f);
                float yy = cy + size * 0.5f + 70 * s;
                const std::wstring pn = Ws(astro::phaseName(mo.elongation));
                float pfs = 62 * s; const float ptw = g.textWidth(F_SERIF_I, pfs, pn); if (ptw > cw) pfs *= cw / ptw;          // never wider than its column
                g.text(F_SERIF_I, pfs, cx, yy, pn, P.ink, Align::Center);
                g.text(F_MONO, 14.5f * s, cx, yy + 40 * s, g.ellipsize(F_MONO, 14.5f * s, Ws(fmt("%.0f%% ILLUMINATED  \xC2\xB7  DAY %.1f", mo.illum * 100.0, md.ageDays)), cw, 0.14f), P.ink2, Align::Center, 0.14f);
                g.text(F_MONO, 14.5f * s, cx, yy + 64 * s, g.ellipsize(F_MONO, 14.5f * s, Ws(fmt("%s KM  \xC2\xB7  %.2f\xC2\xB0 WIDE", thousands(mo.distKm).c_str(), mo.angDiamDeg)), cw, 0.14f), P.ink3, Align::Center, 0.14f);
                // upcoming phases
                struct Ph { const wchar_t* n; double t; } ph[4] = {{L"NEW MOON", md.nextNew}, {L"FIRST QUARTER", md.nextFQ}, {L"FULL MOON", md.nextFull}, {L"LAST QUARTER", md.nextLQ}};
                std::sort(ph, ph + 4, [](const Ph& a, const Ph& b2) { return a.t < b2.t; });
                const float gy = yy + 112 * s, colW = cw * 0.5f;
                for (int i = 0; i < 4; ++i) {
                    const float gx = x + (i % 2) * colW, gyy = gy + (i / 2) * 72 * s;
                    g.text(F_MONO_M, 12.5f * s, gx, gyy, ph[i].n, P.accent2, Align::Left, 0.16f);
                    {
                        const std::wstring ws = Ws(whenStr(*this, ph[i].t));
                        float fs = 19.5f * s; const float tw = g.textWidth(F_LIGHT, fs, ws), room = colW - 12 * s;
                        if (tw > room) fs = std::max(14.f * s, fs * room / tw);                      // smaller text first, "..." only if it still does not fit
                        g.text(F_LIGHT, fs, gx, gyy + 24 * s, g.ellipsize(F_LIGHT, fs, ws, room), P.ink);
                    }
                    g.text(F_MONO, 12.5f * s, gx, gyy + 44 * s, Ws(util::upper(inStr(ph[i].t - now))), P.ink3, Align::Left, 0.1f);
                }
            } break;
            case B_SUN: {
                float yy = yy0 + 22 * s;
                header(c, x, yy, cw, L"SUN", P.accent);
                yy += 40 * s;
                yy = row(c, x, yy, cw, "Astronomical dawn", tm(*this, dayEv.astro.hasRise, dayEv.astro.rise));
                yy = row(c, x, yy, cw, "Nautical dawn", tm(*this, dayEv.nautical.hasRise, dayEv.nautical.rise));
                yy = row(c, x, yy, cw, "Civil dawn", tm(*this, dayEv.civil.hasRise, dayEv.civil.rise));
                yy = row(c, x, yy, cw, "Sunrise", tm(*this, dayEv.sun.hasRise, dayEv.sun.rise));
                yy = row(c, x, yy, cw, "Solar noon", clockStr(*this, dayEv.noon) + fmt("  \xC2\xB7  %.0f\xC2\xB0", dayEv.noonAlt));
                yy = row(c, x, yy, cw, "Sunset", tm(*this, dayEv.sun.hasSet, dayEv.sun.set));
                yy = row(c, x, yy, cw, "Civil dusk", tm(*this, dayEv.civil.hasSet, dayEv.civil.set));
                yy = row(c, x, yy, cw, "Nautical dusk", tm(*this, dayEv.nautical.hasSet, dayEv.nautical.set));
                yy = row(c, x, yy, cw, "Astronomical dusk", tm(*this, dayEv.astro.hasSet, dayEv.astro.set));
                {
                    auto compactSpan = [&](double t0, double t1) -> std::string {
                        std::string s0 = clockStr(*this, t0), s1 = clockStr(*this, t1);
                        if (!st.h24 && s0.size() > 3 && s1.size() > 3 && s0.substr(s0.size() - 3) == s1.substr(s1.size() - 3))
                            s0.resize(s0.size() - 3);                   // "7:29 AM".."8:24 AM" -> "7:29–8:24 AM"
                        return s0 + "\xE2\x80\x93" + s1;
                    };
                    auto spanPair = [&](bool aOk, double a0, double a1, bool bOk, double b0, double b1) -> std::string {
                        std::string out;
                        if (aOk) out += compactSpan(a0, a1);
                        if (bOk) out += (out.empty() ? "" : " \xC2\xB7 ") + compactSpan(b0, b1);
                        return out.empty() ? std::string("\xE2\x80\x94") : out;
                    };
                    const std::string gh = spanPair(dayEv.sun.hasRise && dayEv.golden.hasRise, dayEv.sun.rise, dayEv.golden.rise,
                                                    dayEv.golden.hasSet && dayEv.sun.hasSet, dayEv.golden.set, dayEv.sun.set);
                    const std::string bh = spanPair(dayEv.civil.hasRise && dayEv.blue.hasRise, dayEv.civil.rise, dayEv.blue.rise,
                                                    dayEv.blue.hasSet && dayEv.civil.hasSet, dayEv.blue.set, dayEv.civil.set);
                    yy = row(c, x, yy, cw, "Golden hour", gh);
                    yy = row(c, x, yy, cw, "Blue hour", bh);
                }
                { std::string dl = dayLenText; size_t p = dl.find(" of daylight"); if (p != std::string::npos) dl = dl.substr(0, p); yy = row(c, x, yy, cw, "Daylight", dl); }
                yy = row(c, x, yy, cw, "Equation of time", fmt("%+.1f min", eqTimeMin));
            } break;
            case B_MOONTAB: {
                float yy = yy0 + 22 * s;
                header(c, x, yy, cw, L"MOON", P.accent2);
                yy += 40 * s;
                yy = row(c, x, yy, cw, "Moonrise", (md.rs.hasRise ? clockStrDay(*this, md.rs.rise) : std::string("\xE2\x80\x94")));
                yy = row(c, x, yy, cw, "Moonset", (md.rs.hasSet ? clockStrDay(*this, md.rs.set) : std::string("\xE2\x80\x94")));
                yy = row(c, x, yy, cw, "Right now", mo.appAlt > -0.7 ? fmt("%.0f\xC2\xB0 %s", mo.appAlt, util::compass16(mo.hor.az).c_str()) : std::string("below horizon"), mo.appAlt <= -0.7);
                yy = row(c, x, yy, cw, "Last new moon", whenStr(*this, md.prevNew));
                yy = row(c, x, yy, cw, "Last full moon", whenStr(*this, md.prevFull));
            } break;
            case B_TODAY: {                                                          // what is special today (always has something: see today.cpp)
                float yy = yy0 + 22 * s;
                header(c, x, yy, cw, L"TODAY", P.accent);
                float r = yy0 + 62 * s - 8 * s;
                const float limit = yy0 + H_TODAY * s;
                int shown = 0;
                for (size_t i = 0; i < todayItems.size() && shown < 3; ++i) {
                    std::string tag = todayItems[i].tag, title = todayItems[i].title, detail = todayItems[i].detail;
                    if (tag == "ON THIS DAY") { tag += " \xC2\xB7 " + title; title = detail; detail.clear(); }
                    const float tfs = 21.f * s;
                    std::vector<std::wstring> tl = g.wrap(F_LIGHT, tfs, Ws(title), cw, 0, detail.empty() ? 2 : 1);
                    const float rowH = (20.f + 27.f * (float)tl.size() + (detail.empty() ? 4.f : 19.f)) * s;
                    if (shown > 0 && r + rowH > limit) break;
                    if (shown > 0) g.line(x, r - 8 * s, x + cw, r - 8 * s, mulA(P.line, 0.5f), std::max(1.f, s), false);
                    g.text(F_MONO_M, 12.f * s, x, r + 12 * s, g.ellipsize(F_MONO_M, 12.f * s, Ws(tag), cw, 0.16f), P.accent, Align::Left, 0.16f);
                    float ty = r + 38 * s;
                    for (auto& ln : tl) { g.text(F_LIGHT, tfs, x, ty, ln, P.ink); ty += 27 * s; }
                    if (!detail.empty()) g.text(F_LIGHT, 16.5f * s, x, ty - 5 * s, g.ellipsize(F_LIGHT, 16.5f * s, Ws(detail), cw), P.ink2);
                    r += rowH + 8 * s; ++shown;
                }
                if (shown == 0) g.text(F_SERIF_I, 26.f * s, x, yy0 + 80 * s, L"A quiet day.", P.ink2);
            } break;
            case B_PLANETS: {
                float yy = yy0 + 22 * s;
                header(c, x, yy, cw, L"PLANETS", P.ink2);
                yy += 44 * s;
                static const unsigned pc[astro::PLANET_COUNT] = {0xC9C2B8, 0xFFF4D2, 0xFF8D63, 0xFFEBC8, 0xF2D9A0};
                for (const auto& pr : planets) {
                    const astro::PlanetInfo& pi = pr.info;
                    const Color col = tint(P, rgb(pc[pi.id]));
                    g.fillCircle(x + 6 * s, yy - 7 * s, 4.5f * s, col);
                    g.text(F_MEDIUM, 21 * s, x + 24 * s, yy, Ws(pi.name), P.ink);
                    g.text(F_MONO, 14.f * s, x + cw, yy, Ws(fmt("MAG %+.1f", pi.mag)), P.ink3, Align::Right, 0.08f);
                    std::string l2;
                    if (pi.appAlt > 0) l2 = fmt("%.0f\xC2\xB0 %s", pi.appAlt, util::compass16(pi.hor.az).c_str()) + (pr.rs.hasSet ? "  \xC2\xB7  sets " + clockStrDay(*this, pr.rs.set) : "");
                    else l2 = pr.rs.hasRise ? "rises " + clockStrDay(*this, pr.rs.rise) : "below horizon";
                    g.text(F_LIGHT, 17.5f * s, x + 24 * s, yy + 22 * s, g.ellipsize(F_LIGHT, 17.5f * s, Ws(l2), cw - 24 * s), P.ink2);
                    yy += 56 * s;
                }
            } break;
            case B_FORECAST: {
                float yy = yy0 + 22 * s;
                header(c, x, yy, cw, L"NEXT 7 DAYS", P.ink2);
                yy += 42 * s;
                if (wx.valid && !wx.daily.empty()) {
                    const double d0 = util::localMidnight(now, wx.utcOffset);
                    size_t di = 0; double bestD = 1e18;
                    for (size_t i = 0; i < wx.daily.size(); ++i) {
                        double diff = std::fabs(wx.daily[i].t0 - d0);
                        if (diff < bestD && diff < 43200.0) { bestD = diff; di = i; }
                    }
                    for (size_t i = di; i < wx.daily.size() && i < di + 7; ++i) {
                        const weather::Day& d = wx.daily[i];
                        const util::LocalTime LT = util::toLocal(d.t0 + 7200, wx.utcOffset);
                        g.text(F_MONO_M, 15.f * s, x, yy - 4 * s, Ws(i == di ? "TODAY" : util::upper(util::WEEKDAYS3[LT.wday])), i == di ? P.accent : P.ink2, Align::Left, 0.1f);
                        g.weatherIcon(weather::describe(d.code, true).icon, x + cw * 0.30f, yy - 9 * s, 30 * s, P.ink, P.accent, P.plate, 0.9f);
                        g.text(F_MEDIUM, 20 * s, x + cw * 0.62f, yy, Ws(tempStr(*this, d.tmax)), P.ink, Align::Right);
                        g.text(F_LIGHT, 20 * s, x + cw * 0.80f, yy, Ws(tempStr(*this, d.tmin)), P.ink3, Align::Right);
                        g.text(F_MONO, 14.f * s, x + cw, yy, Ws(fmt("%.0f%%", d.precipProbMax)), d.precipProbMax >= 40 ? P.accent2 : P.ink3, Align::Right, 0.04f);
                        yy += 44 * s;
                    }
                } else g.text(F_LIGHT, 19 * s, x, yy, L"Forecast unavailable", P.ink3);
            } break;
            default: {                                                               // how far through the year
                const util::LocalTime LT = util::toLocal(now, tzOff);
                const today::YearInfo yi = today::yearInfo(LT);
                const float ly = yy0 + 38 * s;
                g.text(F_MONO, 13.5f * s, x, ly - 18 * s, Ws(fmt("DAY %d OF %d  \xC2\xB7  %d LEFT", yi.doy, yi.daysInYear, yi.daysLeft)), P.ink3, Align::Left, 0.14f);
                g.line(x, ly, x + cw, ly, mulA(P.ink, 0.13f), std::max(1.5f, 2.f * s), true);
                g.line(x, ly, x + cw * (float)yi.progress, ly, P.accent, std::max(1.5f, 2.f * s), true);
            } break;
            }
            y = yy0 + bh + GAP_Y * s;
        }
        cx0 += cw + gapX;
    }
}

} // namespace app
