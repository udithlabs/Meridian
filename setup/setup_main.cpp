// Meridian Setup (Sonnet Edition): the graphical installer / uninstaller.
// One 64-bit program, drawn with the same Direct2D / DirectWrite code and fonts as Meridian itself, carrying its files inside.
// Pages: Welcome -> (Make it yours) -> While you're away (Meridian on after no use, then sleep) -> (What's new) -> Setting the sky (progress, with a Moon that fills up) -> All set.
// When Meridian is already installed, Welcome offers Update and Uninstall (Uninstall switches to the "Remove Meridian?" page; Cancel comes back).
// The Windows work (files, shortcuts, registry, undo) is in install.cpp; this file is only the window.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include "../src/gfx.h"
#include "../src/astro.h"
#include "../src/moonsprite.h"
#include "install.h"
#include "pathcheck.h"
#include "song.h"

using gfx::Color; using gfx::rgb; using gfx::Align;
using gfx::F_DISPLAY; using gfx::F_LIGHT; using gfx::F_MEDIUM; using gfx::F_SERIF; using gfx::F_SERIF_I; using gfx::F_MONO; using gfx::F_MONO_M;

static const wchar_t* VERSION_W  = L"2.9.9";
static const wchar_t* CODENAME_W = L"Nimbus";

// ------------------------------------------------------------------ look
static const int   W = 860, H = 540;                  // the window, in 96-dpi units (it is scaled for the screen)
static const float LPW = 288;                         // the left art panel
static const float RX = 328, RW = 492;                // the right side: where the words are
static const Color BG0 = rgb(0x060919), BG1 = rgb(0x0E1030);
static const Color INK = rgb(0xEAEEFF), INK2 = rgb(0x9FAAD3), INK3 = rgb(0x66719B);
static const Color GOLD = rgb(0xF2CC90), GOLD2 = rgb(0xE3A55A), LILAC = rgb(0xB8A4FF), SKY = rgb(0x8FBAFF);
static const Color LINEC = {0.60f, 0.67f, 0.88f, 0.16f}, PANEL = {1, 1, 1, 0.045f}, DARKINK = rgb(0x1A1330);

enum class Mode { Install, Uninstall };
enum class Page { Welcome, Customise, Away, Notes, Working, Done, Error };
enum Id { I_NONE = 0, I_PRIMARY, I_SECOND, I_BACK, I_LINK_NOTES, I_CHANGE, I_T_START, I_T_DESK, I_T_RUN, I_T_SAVER, I_T_GTA, I_T_HAL,
          I_T_DELSET, I_T_LAUNCH, I_MINUS, I_PLUS, I_CHIP0, I_CHIP1, I_CHIP2, I_CHIP3, I_UNINSTALL, I_LINK_CUSTOM,
          I_T_SLEEP, I_MINUS2, I_PLUS2, I_CHIP4, I_CHIP5, I_CHIP6, I_CHIP7, I_SONG, I_MUTE, I_COUNT };
static bool isStepper(int id) { return id == I_MINUS || id == I_PLUS || id == I_MINUS2 || id == I_PLUS2; }
static bool inSleepGroup(int id) { return id == I_T_SLEEP || id == I_MINUS2 || id == I_PLUS2 || (id >= I_CHIP4 && id <= I_CHIP7); }

// defined near WinMain; the "Uninstall" button needs them too
static std::wstring selfPath();
static bool startTempUninstaller(const std::wstring& self, const std::wstring& dir, bool silent, bool deleteSettings, int* silentRc = nullptr);

struct Rect { float x, y, w, h; bool has(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; } };
struct Hit { int id; Rect r; };
struct Star { float x, y, r, phase, speed; };
struct NoteSection { std::wstring title; std::vector<std::wstring> bullets; };

static float easeOut(float t) { t = gfx::clamp01(t); return 1 - (1 - t) * (1 - t) * (1 - t); }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static double nowSec() { LARGE_INTEGER f, c; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c); return (double)c.QuadPart / (double)f.QuadPart; }

// How far the bar is allowed to be after u (0 to 1) of its time: it speeds up, lingers a little, then runs on to the end.
static double paceCurve(double u) {
    static const double K[][2] = {{0, 0}, {0.07, 0.05}, {0.20, 0.19}, {0.33, 0.32}, {0.41, 0.35}, {0.58, 0.63}, {0.69, 0.69}, {0.72, 0.74}, {0.86, 0.93}, {0.95, 0.97}, {1, 1}};
    const int n = (int)(sizeof(K) / sizeof(K[0]));
    if (u <= 0) return 0; if (u >= 1) return 1;
    for (int i = 1; i < n; ++i) if (u <= K[i][0]) { double f = (u - K[i - 1][0]) / (K[i][0] - K[i - 1][0]); return K[i - 1][1] + f * (K[i][1] - K[i - 1][1]); }
    return 1;
}

struct App {
    gfx::Gfx g;
    HWND hwnd = nullptr;
    float dpi = 96;
    Mode mode = Mode::Install;
    Page page = Page::Welcome, prevPage = Page::Welcome;
    float pageT = 0;
    Payload pl;
    inst::Existing ex;
    inst::Options opt;
    std::wstring targetDir;                      // uninstall: the folder being removed
    bool deleteSettings = false, launchAfter = true;
    bool fromTemp = false;                       // the uninstaller runs from a copy in %TEMP% so that it can delete its own folder
    bool uninstallFromSetup = false;             // the Uninstall button on the Update page was pressed: Cancel goes back to that page

    // person and sky
    std::wstring userName, greeting;
    double moonPhaseAngle = 90, moonLimbPA = 270, moonElong = 90, moonIllum = 0.5;   // tonight's real Moon
    gfx::Bitmap moonBmp; long long moonKey = -1;

    // work in progress
    CRITICAL_SECTION cs;
    double progTarget = 0, progShown = 0;
    std::vector<std::pair<double, std::wstring>> progLog;     // what the real work said, and how far along it was when it said it (the words follow the bar, not the work)
    double workStart = 0, paceSec = 9;                          // the bar takes paceSec seconds (8 to 10) however fast the work really is
    bool songOpen = false; float songK = 0, toastT = 0; std::wstring toast;    // the hidden song panel, and the little note after pressing M
    bool workDone = false, errRunning = false; std::wstring errMsg;
    HANDLE worker = nullptr; double doneAt = 0;
    bool failedOnce = false;

    // interaction
    std::vector<Hit> hits;
    int hover = 0, down = 0, focus = 0;
    float hov[I_COUNT] = {0}, tog[I_COUNT] = {0}, stepAnim = 0, stepAnim2 = 0;
    float tlF = -1, tlEnd = 0;                   // the little timeline on the Away page (where Meridian turns on; whether it ends in sleep)
    Rect stepArea1{0, 0, 0, 0}, stepArea2{0, 0, 0, 0};
    float mx = -1, my = -1;
    double lastT = 0, t0 = 0, holdStart = 0, holdLast = 0, lastPaintAt = 0, activeUntil = 0;
    float alpha = 1;
    std::vector<Star> stars;
    gfx::Bitmap noBmp;

    // notes page
    std::vector<NoteSection> notes;
    float scrollTarget = 0, scrollShown = 0, scrollMax = 0, notesH = 0;
    struct NItem { int kind; float y; std::wstring text; std::vector<std::wstring> lines; };   // kind 0 heading, 1 bullet
    std::vector<NItem> nitems; float nlayoutW = 0;
    Rect notesView{0, 0, 0, 0};

    App() { InitializeCriticalSection(&cs); }
    ~App() { DeleteCriticalSection(&cs); }

    // ---------------------------------------------------------------- start
    void loadNotes() {
        HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_NOTES), RT_RCDATA);
        if (!r) return;
        HGLOBAL h = LoadResource(nullptr, r); const char* p = h ? (const char*)LockResource(h) : nullptr; size_t n = SizeofResource(nullptr, r);
        if (!p) return;
        std::string s(p, n); size_t pos = 0;
        while (pos < s.size()) {
            size_t e = s.find('\n', pos); if (e == std::string::npos) e = s.size();
            std::string line = s.substr(pos, e - pos); pos = e + 1;
            if (line.compare(0, 3, "## ") == 0) {
                std::string t = line.substr(3); size_t bar = t.find(" | ");
                std::string ver = bar == std::string::npos ? t : t.substr(0, bar), name = bar == std::string::npos ? "" : t.substr(bar + 3), date;
                size_t bar2 = name.find(" | "); if (bar2 != std::string::npos) { date = name.substr(bar2 + 3); name = name.substr(0, bar2); }     // "## 2.9.1 | Skylight | 15 Nov 2026"
                std::wstring title = inst::widen(ver); for (auto& c : name) c = (char)toupper((unsigned char)c); for (auto& c : date) c = (char)toupper((unsigned char)c);
                if (!name.empty()) title += L"  ·  " + inst::widen(name);
                if (!date.empty()) title += L"  ·  " + inst::widen(date);
                notes.push_back({title, {}});
            } else if (line.compare(0, 2, "- ") == 0 && !notes.empty()) notes.back().bullets.push_back(inst::widen(line.substr(2)));
        }
    }
    void loadPerson() {
        std::wstring name;
        typedef BOOLEAN (WINAPI *GetUserNameExWFn)(int, LPWSTR, PULONG);
        if (HMODULE s = LoadLibraryW(L"secur32.dll")) {
            if (auto fn = (GetUserNameExWFn)GetProcAddress(s, "GetUserNameExW")) { wchar_t b[256]; ULONG n = 256; if (fn(3 /*NameDisplay*/, b, &n) && n) name = b; }
        }
        if (name.empty()) { wchar_t b[256]; DWORD n = 256; if (GetUserNameW(b, &n) && n) name = b; }
        size_t sp = name.find_first_of(L" \t"); if (sp != std::wstring::npos) name = name.substr(0, sp);
        size_t bs = name.find_last_of(L"\\@"); if (bs != std::wstring::npos && name[bs] == L'\\') name = name.substr(bs + 1);
        bool ok = !name.empty() && name.size() <= 18;
        for (wchar_t c : name) if (c < 32 || (c >= L'0' && c <= L'9') || c == L'@' || c == L'\\') ok = false;
        if (ok) { name[0] = towupper(name[0]); for (size_t i = 1; i < name.size(); ++i) name[i] = towlower(name[i]); userName = name; }
        SYSTEMTIME st; GetLocalTime(&st);
        if (st.wHour >= 5 && st.wHour < 12) greeting = L"Good morning,";
        else if (st.wHour >= 12 && st.wHour < 17) greeting = L"Good afternoon,";
        else if (st.wHour >= 17 && st.wHour < 22) greeting = L"Good evening,";
        else greeting = L"Up late,";
    }
    void loadMoon() {
        astro::Geo geo; double now = (double)time(nullptr);
        astro::MoonInfo mi = astro::moonInfo(now, geo);
        moonPhaseAngle = mi.phaseAngle; moonLimbPA = mi.brightLimbPA; moonElong = mi.elongation; moonIllum = mi.illum;
    }
    void makeStars() {
        unsigned s = 20261109u; auto rnd = [&]() { s = s * 1664525u + 1013904223u; return (s >> 8) / 16777216.f; };
        for (int i = 0; i < 130; ++i) stars.push_back({rnd() * W, rnd() * H, 0.5f + rnd() * 1.0f, rnd() * 6.28f, 0.6f + rnd() * 1.6f});
    }
    void init() {
        loadNotes(); loadPerson(); loadMoon(); makeStars();
        t0 = lastT = nowSec();
        activeUntil = t0 + 1.0;
        if (opt.dir.empty()) opt.dir = ex.found ? ex.dir : inst::defaultDir();
        stepAnim = opt.saver ? 1.f : 0.f; stepAnim2 = (opt.saver && opt.sleep) ? 1.f : 0.f;
        tog[I_T_START] = opt.startMenu; tog[I_T_DESK] = opt.desktop; tog[I_T_RUN] = opt.runAtLogon; tog[I_T_SAVER] = opt.saver; tog[I_T_SLEEP] = opt.sleep;
        tog[I_T_GTA] = opt.gta; tog[I_T_HAL] = opt.halloween; tog[I_T_LAUNCH] = 1;
        moonsprite::prewarm();
    }

    // ---------------------------------------------------------------- drawing helpers (everything fades with the page: `alpha`)
    Color C(const Color& c) const { return gfx::mulA(c, alpha); }
    void T(gfx::FontId f, float size, float x, float base, const std::wstring& s, const Color& c, Align al = Align::Left, float tr = 0) { g.text(f, size, x, base, s, C(c), al, tr); }
    bool hotspot(int id, Rect r) { hits.push_back({id, r}); return hover == id; }
    float animTo(float& v, float target, float dt, float speed = 14.f) { v += (target - v) * std::min(1.f, dt * speed); if (std::fabs(target - v) < 0.002f) v = target; return v; }

    void ensureMoon(double phaseAngle, double limbPA) {
        const int sz = std::max(160, std::min(300, (int)std::ceil(150.f * dpi / 96.f)));
        // Quantize animated phase to 2-degree steps (90 crisp frames across install/uninstall) so we don't raymarch on every 60 Hz frame.
        const long long paQ = (page == Page::Working) ? (long long)std::llround(phaseAngle / 2.0) : (long long)std::llround(phaseAngle * 6.0);
        const long long key = ((long long)sz << 32) | ((paQ & 0xFFFFLL) << 16) | ((long long)std::llround(limbPA) & 0xFFFFLL);
        if (key == moonKey && moonBmp.bmp) return;
        if (!moonsprite::ready()) return;
        moonsprite::Params p; p.size = sz; p.phaseAngleDeg = phaseAngle; p.brightLimbPA = limbPA; p.earthshine = 0.11;
        std::vector<uint8_t> px; moonsprite::render(p, px);
        g.freeBitmap(moonBmp); moonBmp = g.makeBitmap(px.data(), p.size, p.size); moonKey = key;
    }

    void star(const Star& s, double t, float mul) {
        float a = (0.25f + 0.75f * (0.5f + 0.5f * std::sin((float)t * s.speed + s.phase))) * mul;
        g.fillCircle(s.x, s.y, s.r, gfx::withA(rgb(0xDDE6FF), a * 0.55f));
    }

    void button(int id, const std::wstring& label, Rect r, bool primary) {
        bool hv = hotspot(id, r); float h = hov[id]; bool dn = down == id;
        float cy = r.y + r.h / 2;
        if (primary) {
            Color c = gfx::lerp(GOLD2, GOLD, 0.55f + 0.45f * h);
            if (h > 0.01f) g.glow(r.x + r.w / 2, cy, r.w * 0.75f, GOLD, 0.30f * h * alpha);
            g.fillRoundRect(r.x, r.y + (dn ? 1.f : 0.f), r.w, r.h, 12, C(c));
            T(F_MEDIUM, 14.5f, r.x + r.w / 2, cy + g.capHeight(F_MEDIUM, 14.5f) / 2 + (dn ? 1.f : 0.f), label, DARKINK, Align::Center, 0.02f);
        } else {
            g.fillRoundRect(r.x, r.y, r.w, r.h, 12, C({1, 1, 1, 0.04f + 0.07f * h}));
            g.strokeRoundRect(r.x + 0.5f, r.y + 0.5f, r.w - 1, r.h - 1, 12, C({0.7f, 0.76f, 0.95f, 0.22f + 0.2f * h}), 1.f);
            T(F_MEDIUM, 14.5f, r.x + r.w / 2, cy + g.capHeight(F_MEDIUM, 14.5f) / 2, label, INK, Align::Center, 0.02f);
        }
        (void)hv;
        if (focus == id) g.strokeRoundRect(r.x - 3, r.y - 3, r.w + 6, r.h + 6, 14, C(gfx::withA(LILAC, 0.9f)), 1.6f);
    }

    // a row with a title, a line of explanation and a switch
    void toggleRow(int id, float y, const wchar_t* title, const wchar_t* desc, bool on, float dt, bool enabled = true) {
        Rect r{RX, y, RW, 38};
        const float keepAlpha = alpha; if (!enabled) alpha = keepAlpha * 0.42f;           // a row that cannot be used yet is dimmed and has no hot area
        if (enabled) hotspot(id, r);
        float h = enabled ? hov[id] : 0.f;
        animTo(tog[id], on ? 1.f : 0.f, dt, 12.f);
        if (h > 0.01f) g.fillRoundRect(r.x - 8, r.y, r.w + 16, r.h, 10, C({1, 1, 1, 0.035f * h}));
        T(F_MEDIUM, 14.f, r.x, y + 16.5f, title, INK);
        T(F_LIGHT, 11.5f, r.x, y + 31.5f, g.ellipsize(F_LIGHT, 11.5f, desc, RW - 56), INK3);
        float tx = r.x + r.w - 40, ty = y + 9;
        float k = tog[id];
        g.fillRoundRect(tx, ty, 38, 20, 10, C(gfx::lerp({1, 1, 1, 0.12f}, gfx::withA(GOLD2, 0.95f), k)));
        g.fillCircle(tx + lerpf(10, 28, k), ty + 10, 7.5f, C(gfx::lerp(rgb(0xB9C2E6), DARKINK, k * 0.85f)));
        g.line(r.x, y + 38, r.x + r.w, y + 38, C({0.6f, 0.67f, 0.88f, 0.09f}), 1.f, false);
        if (focus == id) g.strokeRoundRect(r.x - 8, r.y, r.w + 16, r.h, 10, C(gfx::withA(LILAC, 0.9f)), 1.6f);
        alpha = keepAlpha;
    }
    void chip(int id, Rect r, const std::wstring& label, bool on, float dt) {
        hotspot(id, r); float h = hov[id]; animTo(tog[id], on ? 1.f : 0.f, dt, 12.f);
        float k = tog[id];
        g.fillRoundRect(r.x, r.y, r.w, r.h, r.h / 2, C(gfx::lerp({1, 1, 1, 0.04f + 0.05f * h}, gfx::withA(GOLD, 0.16f), k)));
        g.strokeRoundRect(r.x + 0.5f, r.y + 0.5f, r.w - 1, r.h - 1, r.h / 2, C(gfx::lerp({0.7f, 0.76f, 0.95f, 0.22f}, gfx::withA(GOLD, 0.8f), k)), 1.f);
        float cx = r.x + 15, cy = r.y + r.h / 2;
        g.strokeCircle(cx, cy, 5.5f, C(gfx::lerp(gfx::withA(INK3, 0.9f), GOLD, k)), 1.3f);
        if (k > 0.01f) g.fillCircle(cx, cy, 3.1f * k, C(GOLD));
        T(F_MEDIUM, 12.5f, r.x + 28, cy + g.capHeight(F_MEDIUM, 12.5f) / 2, label, gfx::lerp(INK2, INK, k));
        if (focus == id) g.strokeRoundRect(r.x - 3, r.y - 3, r.w + 6, r.h + 6, r.h / 2 + 3, C(gfx::withA(LILAC, 0.9f)), 1.6f);
    }
    void checkMark(float x, float y, const Color& c) {            // a small tick, centred on (x, y)
        g.polyline({{x - 4.5f, y + 0.5f}, {x - 1.2f, y + 3.8f}, {x + 5, y - 3.6f}}, false, C(c), 1.9f);
    }
    float paragraph(gfx::FontId f, float size, float x, float y, float w, float lineH, const std::wstring& s, const Color& c, int maxLines = 99) {
        auto lines = g.wrap(f, size, s, w, 0, maxLines);
        for (auto& l : lines) { T(f, size, x, y, l, c); y += lineH; }
        return y;
    }
    void eyebrow(const std::wstring& s) { T(F_MONO_M, 11.f, RX, 54, s, GOLD, Align::Left, 0.18f); }

    // ---------------------------------------------------------------- the left panel: the sky
    void leftPanel(double t, float dt) {
        g.vGradientRect(0, 0, LPW, H, rgb(0x0A0E2E), rgb(0x2A1B4A));
        g.glow(LPW / 2, 190, 170, rgb(0x6A7BFF), 0.30f);
        g.glow(LPW / 2, H + 40, 200, rgb(0xB0568F), 0.22f);
        for (auto& s : stars) if (s.x < LPW) star(s, t, 1.f);
        g.line(LPW - 0.5f, 0, LPW - 0.5f, H, {0.7f, 0.76f, 1.f, 0.10f}, 1.f, false);

        T(F_MONO_M, 10.5f, 32, 46, L"MERIDIAN", INK2, Align::Left, 0.22f);

        const float cx = LPW / 2, cy = 190;
        bool working = page == Page::Working, finished = page == Page::Done;
        double pa = moonPhaseAngle, limb = moonLimbPA, elong = moonElong, illum = moonIllum;
        double p = std::min(1.0, progShown);
        std::wstring label = L"TONIGHT'S MOON";
        if (mode == Mode::Install && (working || finished)) {
            double q = finished ? 1.0 : p;
            pa = 180 * (1 - q); limb = 270; elong = 180 * q; illum = (1 + std::cos(pa * 3.14159265 / 180)) / 2;
            label = finished ? L"READY" : L"THE MOON IS FILLING";
        } else if (mode == Mode::Uninstall && (working || finished)) {
            double q = finished ? 1.0 : p;
            pa = 180 * q; limb = 90; elong = 180 + 180 * q; illum = (1 + std::cos(pa * 3.14159265 / 180)) / 2;
            label = finished ? L"UNTIL NEXT TIME" : L"THE MOON IS SETTING";
        }
        ensureMoon(pa, limb);
        // the dial: sixty ticks, a progress ring and a small planet going round
        for (int i = 0; i < 60; ++i) {
            float a = i * 6.f * 3.14159265f / 180.f, s = std::sin(a), c = std::cos(a);
            float r0 = (i % 5 == 0) ? 92.f : 96.f, r1 = 101.f;
            g.line(cx + s * r0, cy - c * r0, cx + s * r1, cy - c * r1, gfx::withA(INK2, (i % 5 == 0) ? 0.55f : 0.22f), (i % 5 == 0) ? 1.4f : 1.f, true);
        }
        g.strokeCircle(cx, cy, 112, {0.7f, 0.76f, 1.f, 0.12f}, 1.f);
        if (working || finished) {
            float sweep = (float)(finished ? 1.0 : p) * 359.9f;
            g.arc(cx, cy, 112, 0, sweep, gfx::withA(GOLD, 0.95f), 2.2f, true);
            float a = sweep * 3.14159265f / 180.f;
            g.glow(cx + std::sin(a) * 112, cy - std::cos(a) * 112, 12, GOLD, 0.9f);
        } else {
            float a = (float)(t * 0.22);
            float px = cx + std::sin(a) * 112, py = cy - std::cos(a) * 112;
            g.glow(px, py, 11, SKY, 0.8f);
            g.fillCircle(px, py, 2.6f, rgb(0xDDEBFF));
        }
        // the Moon
        const float ms = 150;
        g.glow(cx, cy, 120, rgb(0xCBD5FF), 0.22f);
        if (moonBmp.bmp) g.drawBitmap(moonBmp, cx, cy, ms, 0, 1.f);
        else g.fillCircle(cx, cy, ms / 2, {0.8f, 0.85f, 1.f, 0.10f});

        T(F_MONO, 10.f, cx, 338, label, INK3, Align::Center, 0.2f);
        T(F_SERIF_I, 25.f, cx, 368, inst::widen(astro::phaseName(std::fmod(elong + 360.0, 360.0))), INK, Align::Center);
        wchar_t b[48]; swprintf(b, 48, L"%d%% LIT", (int)std::lround(illum * 100));
        T(F_MONO, 10.5f, cx, 390, b, INK2, Align::Center, 0.16f);

        g.line(32, 452, LPW - 32, 452, {0.7f, 0.76f, 1.f, 0.14f}, 1.f, false);
        T(F_SERIF, 38.f, 30, 488, L"Sonnet", INK);
        T(F_MONO_M, 10.f, 32, 510, std::wstring(L"EDITION  ·  ") + VERSION_W + L"  ·  GLASS", GOLD, Align::Left, 0.14f);
    }

    // ---------------------------------------------------------------- the song's hidden corner
    // A faint note in the top right corner of the Customise page (and of the uninstall page, which has no Customise). Click it and the song's title, artist
    // and a Mute button slide out. While the song plays, the M key mutes it too. Nothing else on screen says there is music.
    void songCorner(float dt) {
        if (song::title().empty()) return;
        const float cx = RX + RW - 12, cy = 28;
        hotspot(I_SONG, {cx - 14, cy - 14, 28, 28});
        const float h = hov[I_SONG];
        animTo(songK, songOpen ? 1.f : 0.f, dt, 10.f);
        const Color c = gfx::withA(INK2, std::min(1.f, 0.16f + 0.5f * h + 0.6f * songK));
        g.fillCircle(cx - 3.5f, cy + 5.5f, 3.4f, C(c));
        g.line(cx - 0.4f, cy + 4.8f, cx - 0.4f, cy - 6.5f, C(c), 1.5f);
        g.polyline({{cx - 0.4f, cy - 6.5f}, {cx + 4.5f, cy - 4.f}, {cx + 4.f, cy - 0.5f}}, false, C(c), 1.5f);
        if (focus == I_SONG) g.strokeCircle(cx, cy, 15, C(gfx::withA(LILAC, 0.9f)), 1.6f);
        if (songK < 0.01f) return;
        const float save = alpha; alpha = save * songK;
        const bool mt = song::muted();
        const Rect chipR{cx - 24 - 78, cy - 12, 78, 24};
        if (songK > 0.9f) hotspot(I_MUTE, chipR);
        const float mh = hov[I_MUTE]; animTo(tog[I_MUTE], mt ? 1.f : 0.f, dt, 12.f); const float k = tog[I_MUTE];
        g.fillRoundRect(chipR.x, chipR.y, chipR.w, chipR.h, 12, C(gfx::lerp({1, 1, 1, 0.04f + 0.05f * mh}, gfx::withA(GOLD, 0.16f), k)));
        g.strokeRoundRect(chipR.x + 0.5f, chipR.y + 0.5f, chipR.w - 1, chipR.h - 1, 12, C(gfx::lerp({0.7f, 0.76f, 0.95f, 0.22f}, gfx::withA(GOLD, 0.8f), k)), 1.f);
        T(F_MEDIUM, 11.5f, chipR.x + chipR.w / 2, cy + 4.f, mt ? L"UNMUTE" : L"MUTE", gfx::lerp(INK2, GOLD, k), Align::Center, 0.1f);
        if (focus == I_MUTE) g.strokeRoundRect(chipR.x - 3, chipR.y - 3, chipR.w + 6, chipR.h + 6, 15, C(gfx::withA(LILAC, 0.9f)), 1.6f);
        std::wstring who = song::title() + L"  \x00B7  " + song::artist(); for (auto& ch : who) ch = (wchar_t)towupper(ch);
        T(F_MONO, 10.5f, chipR.x - 14, cy + 3.8f, g.ellipsize(F_MONO, 10.5f, who, chipR.x - 14 - (RX + 130)), INK2, Align::Right, 0.12f);
        alpha = save;
    }
    // "SOUND OFF" / "SOUND ON" for a moment after pressing M while the song plays
    void songToast(float dt) {
        if (toastT <= 0) return;
        toastT -= dt;
        const float a = std::min(1.f, std::max(0.f, toastT) * 2.5f);
        const float save = alpha; alpha = a;
        T(F_MONO, 10.5f, RX + RW, 34, toast, INK2, Align::Right, 0.16f);
        alpha = save;
    }

    // ---------------------------------------------------------------- pages
    void go(Page p) {
        prevPage = page; page = p; pageT = 0; hits.clear(); hover = down = focus = 0; songOpen = false;
        if (p == Page::Notes) scrollTarget = scrollShown = 0;
    }
    void buttonsRow(const wchar_t* secondLabel, int secondId, const wchar_t* primLabel) {
        const float y = 476, h = 44;
        button(I_PRIMARY, primLabel, {RX + RW - 150, y, 150, h}, true);
        if (secondLabel) button(secondId, secondLabel, {RX + RW - 150 - 12 - 124, y, 124, h}, false);
    }

    // a small text link with an arrow (What's new, Customise); `top` is the top of its 26 px high hot area
    void linkRow(int id, const wchar_t* label, float top) {
        Rect lr{RX, top, 120, 26}; hotspot(id, lr); float h = hov[id];
        Color lc = gfx::lerp(INK2, GOLD, h); float tw = g.textWidth(F_MEDIUM, 13.f, label), ax = RX + tw + 10 + 3 * h, base = top + 18;
        T(F_MEDIUM, 13.f, RX, base, label, lc);
        g.line(ax, base - 4.5f, ax + 12, base - 4.5f, C(lc), 1.4f); g.polyline({{ax + 8, base - 8.5f}, {ax + 12, base - 4.5f}, {ax + 8, base - 0.5f}}, false, C(lc), 1.4f);
        if (focus == id) g.strokeRoundRect(lr.x - 4, lr.y, lr.w + 4, lr.h, 8, C(gfx::withA(LILAC, 0.9f)), 1.6f);
    }
    // The Uninstall button: the same thing as Settings > Apps > Uninstall, with the same "Remove Meridian?" page.
    void beginUninstall() {
        if (!ex.found) return;
        // Opened as <install folder>\uninstall.exe: that file cannot delete itself while it runs, so a copy in %TEMP% takes over (as for Settings > Apps).
        if (inst::pathsEqual(inst::parentDir(selfPath()), ex.dir) && startTempUninstaller(selfPath(), ex.dir, false, false)) { DestroyWindow(hwnd); return; }
        mode = Mode::Uninstall; targetDir = ex.dir; deleteSettings = false; uninstallFromSetup = true;
        SetWindowTextW(hwnd, L"Uninstall Meridian");
        go(Page::Welcome);
    }
    void cancelUninstall() {                                         // back to the Update page
        mode = Mode::Install; uninstallFromSetup = false; deleteSettings = false;
        SetWindowTextW(hwnd, L"Meridian Setup  \xB7  Sonnet Edition");
        go(Page::Welcome);
    }

    void pageWelcome(float dt) {
        if (mode == Mode::Uninstall) {
            eyebrow(L"UNINSTALL");
            T(F_SERIF, 54.f, RX, 128, L"Remove", INK);
            T(F_SERIF_I, 54.f, RX, 190, L"Meridian?", GOLD);
            paragraph(F_LIGHT, 15.5f, RX, 238, RW, 25, L"This takes Meridian off your PC: its program files, shortcuts and start-up entry. If you made it your screen saver, your earlier one comes back.", INK2, 5);
            toggleRow(I_T_DELSET, 352, L"Also delete my settings, places and notes", L"Leave this off to keep them for next time.", deleteSettings, dt);
            T(F_MONO, 10.f, RX, 449, L"YOUR OWN THEME PACKS ARE NEVER TOUCHED", INK3, Align::Left, 0.10f);
            buttonsRow(L"Cancel", I_SECOND, L"Uninstall");
            songCorner(dt);
            return;
        }
        const bool up = ex.found;
        eyebrow(up ? L"UPDATE" : L"WELCOME");
        if (!userName.empty()) {
            T(F_SERIF, 54.f, RX, 128, up ? L"Welcome back," : greeting, INK);
            T(F_SERIF_I, 54.f, RX, 190, userName + L".", GOLD);
        } else T(F_SERIF, 54.f, RX, 128, up ? L"Welcome back." : L"Welcome.", INK);
        const bool same = up && !ex.version.empty() && ex.version == VERSION_W;
        std::wstring lead = !up ? L"Meridian is a living sky for your desktop: the real Moon, the planets, the weather, and a quiet clock that keeps watch while you are away."
            : same ? std::wstring(L"Meridian ") + VERSION_W + L" (" + CODENAME_W + L") is already on this PC. Update puts its files back as they should be and keeps your settings, your places and your notes."
                   : std::wstring(L"Meridian ") + (ex.version.empty() ? L"is" : ex.version + L" is") + L" already on this PC. Update brings it to " + VERSION_W + L" (" + CODENAME_W + L") and keeps your settings, your places and your notes.";
        float y = paragraph(F_LIGHT, 15.5f, RX, 238, RW, 25, lead, INK2, 5);
        paragraph(F_LIGHT, 13.5f, RX, y + 8, RW, 21, up ? L"Customise changes the folder, the shortcuts and when Meridian turns on while you are away. Uninstall takes Meridian off this PC again."
                                                       : L"It sets itself up for you alone, in about a minute, with no administrator rights. Next, you choose when it keeps watch while you are away.", INK3, 3);
        T(F_MONO, 10.5f, RX, 398, up ? L"INSTALLED IN" : L"INSTALLS TO", INK3, Align::Left, 0.16f);
        T(F_MONO, 11.5f, RX + 104, 398, g.ellipsize(F_MONO, 11.5f, opt.dir, RW - 104), INK2);
        T(F_MONO, 10.f, RX, 428, L"MERIDIAN  ·  SONNET EDITION", INK3, Align::Left, 0.10f);
        T(F_MONO, 10.f, RX, 445, L"NOT CODE-SIGNED, SO WINDOWS MAY ASK YOU TO CONFIRM", INK3, Align::Left, 0.10f);
        if (up) {                                                        // already installed: Update and Uninstall, the other two as links
            button(I_PRIMARY, L"Update", {RX + RW - 150, 476, 150, 44}, true);
            button(I_UNINSTALL, L"Uninstall", {RX + RW - 150 - 12 - 124, 476, 124, 44}, false);
            linkRow(I_LINK_CUSTOM, L"Customise", 468);
            linkRow(I_LINK_NOTES, L"What's new", 492);
        } else {
            buttonsRow(L"Customise", I_SECOND, L"Next");
            linkRow(I_LINK_NOTES, L"What's new", 486);
        }
    }

    void pageCustomise(float dt) {
        eyebrow(L"MAKE IT YOURS");
        T(F_SERIF, 40.f, RX, 108, L"Make it yours.", INK);
        T(F_LIGHT, 14.f, RX, 136, L"You can change any of this later by running setup again.", INK2);
        T(F_MONO, 10.5f, RX, 168, L"INSTALL TO", INK3, Align::Left, 0.16f);
        Rect pill{RX, 176, RW, 38};
        g.fillRoundRect(pill.x, pill.y, pill.w, pill.h, 11, C(PANEL));
        g.strokeRoundRect(pill.x + 0.5f, pill.y + 0.5f, pill.w - 1, pill.h - 1, 11, C(LINEC), 1.f);
        {   // a small folder
            float fx = pill.x + 15, fy = pill.y + 12;
            g.fillRoundRect(fx, fy, 9, 5, 1.5f, C(gfx::withA(GOLD, 0.85f)));
            g.fillRoundRect(fx, fy + 2.5f, 18, 11, 2.5f, C(gfx::withA(GOLD, 0.95f)));
        }
        T(F_MONO, 12.f, pill.x + 44, pill.y + 24, g.ellipsize(F_MONO, 12.f, opt.dir, RW - 44 - 104), INK);
        button(I_CHANGE, L"Change", {pill.x + RW - 92, pill.y + 5, 84, 28}, false);

        float y = 230;
        toggleRow(I_T_START, y, L"Start menu shortcut", L"Find Meridian in the Start menu.", opt.startMenu, dt); y += 38;
        toggleRow(I_T_DESK, y, L"Desktop shortcut", L"A Meridian icon on your desktop.", opt.desktop, dt); y += 38;
        toggleRow(I_T_RUN, y, L"Open when I sign in", L"Starts with Windows. Switch it off any time in Meridian's right-click menu.", opt.runAtLogon, dt); y += 38;
        y += 22;
        T(F_MEDIUM, 14.f, RX, y + 24, L"Extra tabs", INK);
        chip(I_T_GTA, {RX + 150, y + 6, 100, 28}, L"GTA VI", opt.gta, dt);
        chip(I_T_HAL, {RX + 258, y + 6, 124, 28}, L"Halloween", opt.halloween, dt);
        buttonsRow(L"Back", I_BACK, L"Next");
        songCorner(dt);
    }

    // ---------------------------------------------------------------- While you're away: Meridian on after no use, then the PC to sleep
    // - minus / number / plus with a few presets, shared by the two timers
    void stepperRow(int idMinus, int idPlus, int chipBase, float cy, const wchar_t* label, int value, const int* presets, float sa, float dt, Rect& area) {
        if (sa <= 0.01f) { area = {0, 0, 0, 0}; return; }
        float save = alpha; alpha = save * sa;
        area = {RX, cy - 18, RW, 36};
        T(F_LIGHT, 13.f, RX, cy + 4.5f, label, INK2);
        auto round = [&](int id, float cx, bool plus) {
            Rect r{cx - 13, cy - 13, 26, 26}; if (sa > 0.9f) hotspot(id, r); float h = hov[id];
            g.fillCircle(cx, cy, 13, C({1, 1, 1, 0.06f + 0.08f * h}));
            g.strokeCircle(cx, cy, 12.5f, C({0.7f, 0.76f, 0.95f, 0.25f + 0.25f * h}), 1.f);
            g.line(cx - 4.5f, cy, cx + 4.5f, cy, C(INK), 1.5f);
            if (plus) g.line(cx, cy - 4.5f, cx, cy + 4.5f, C(INK), 1.5f);
            if (focus == id) g.strokeCircle(cx, cy, 16, C(gfx::withA(LILAC, 0.9f)), 1.6f);
        };
        round(idMinus, RX + 156, false); round(idPlus, RX + 240, true);
        wchar_t b[16]; swprintf(b, 16, L"%d", value);
        T(F_SERIF, 27.f, RX + 198, cy + 9, b, INK, Align::Center);
        T(F_LIGHT, 12.5f, RX + 262, cy + 4.5f, value == 1 ? L"minute" : L"minutes", INK2);
        for (int i = 0; i < 4; ++i) {
            int id = chipBase + i; Rect r{RX + RW - 4 * 38 - 3 * 6 + i * 44.f, cy - 12, 38, 24};
            if (sa > 0.9f) hotspot(id, r);
            float h = hov[id]; bool on = value == presets[i];
            animTo(tog[id], on ? 1.f : 0.f, dt, 12.f); float k = tog[id];
            g.fillRoundRect(r.x, r.y, r.w, r.h, 12, C(gfx::lerp({1, 1, 1, 0.04f + 0.05f * h}, gfx::withA(GOLD, 0.16f), k)));
            g.strokeRoundRect(r.x + 0.5f, r.y + 0.5f, r.w - 1, r.h - 1, 12, C(gfx::lerp({0.7f, 0.76f, 0.95f, 0.22f}, gfx::withA(GOLD, 0.8f), k)), 1.f);
            wchar_t n[8]; swprintf(n, 8, L"%d", presets[i]);
            T(F_MEDIUM, 12.5f, r.x + r.w / 2, r.y + 16.5f, n, gfx::lerp(INK2, INK, k), Align::Center);
            if (focus == id) g.strokeRoundRect(r.x - 3, r.y - 3, r.w + 6, r.h + 6, 15, C(gfx::withA(LILAC, 0.9f)), 1.6f);
        }
        alpha = save;
    }
    static std::wstring minutesText(int m) { return std::to_wstring(m) + (m == 1 ? L" minute" : L" minutes"); }

    // - the little picture of what happens when
    void timeline(float py, float dt) {
        const Rect pr{RX, py, RW, 134};
        g.fillRoundRect(pr.x, pr.y, pr.w, pr.h, 14, C(PANEL));
        g.strokeRoundRect(pr.x + 0.5f, pr.y + 0.5f, pr.w - 1, pr.h - 1, 14, C(LINEC), 1.f);
        T(F_MONO, 10.f, RX + 20, py + 23, L"HOW IT PLAYS OUT", INK3, Align::Left, 0.16f);
        const bool on = opt.saver, sl = opt.saver && opt.sleep;
        const int n1 = opt.saverMinutes, n2 = opt.sleepMinutes, total = n1 + n2;
        const float x0 = RX + 22, x1 = RX + RW - 22, bw = x1 - x0, by = py + 56, bh = 6;
        float f1 = sl ? std::max(0.2f, std::min(0.8f, (float)n1 / (float)total)) : 0.45f;       // (not to scale for very short or very long waits)
        if (tlF < 0) tlF = f1;
        animTo(tlF, f1, dt, 9.f); animTo(tlEnd, sl ? 1.f : 0.f, dt, 9.f);
        const float px = x0 + bw * tlF;
        g.fillRoundRect(x0, by, bw, bh, 3, C({1, 1, 1, 0.08f}));
        if (on) {
            float gx = px, gw = x1 - px;
            g.fillRoundRect(x0, by, px - x0, bh, 3, C({0.7f, 0.76f, 0.95f, 0.22f}));                        // the PC is in use / idle
            g.fillRoundRect(gx, by, gw, bh, 3, C(gfx::lerp(GOLD2, GOLD, 0.5f)));                           // Meridian is on
            g.glow(px, by + bh / 2, 15, GOLD, 0.55f * alpha);
            g.fillCircle(px, by + bh / 2, 5.5f, C(GOLD)); g.fillCircle(px, by + bh / 2, 2.2f, C(DARKINK));
            // the end: asleep (a ring) or open-ended (an arrow)
            if (tlEnd > 0.01f) { float s0 = alpha; alpha = s0 * tlEnd; g.fillCircle(x1, by + bh / 2, 6.5f, C(rgb(0x1E2150))); g.strokeCircle(x1, by + bh / 2, 6.5f, C(LILAC), 1.6f); alpha = s0; }
            if (tlEnd < 0.99f) { float s0 = alpha; alpha = s0 * (1 - tlEnd); g.polyline({{x1 - 7, by + bh / 2 - 5}, {x1, by + bh / 2}, {x1 - 7, by + bh / 2 + 5}}, false, C(GOLD), 1.6f); alpha = s0; }
            std::wstring a = L"MERIDIAN ON  ·  " + std::to_wstring(n1) + L" MIN";
            float hw = g.textWidth(F_MONO_M, 10.f, a, 0.12f) / 2, cx = std::max(x0 + hw, std::min(x1 - hw, px));
            T(F_MONO_M, 10.f, cx, by - 13, a, GOLD, Align::Center, 0.12f);
            T(F_MONO, 10.f, x0, by + 26, L"NO ONE AT THE PC", INK3, Align::Left, 0.1f);
            if (sl) { float s0 = alpha; alpha = s0 * tlEnd; T(F_MONO, 10.f, x1, by + 26, L"PC SLEEPS  ·  " + std::to_wstring(total) + L" MIN IN ALL", LILAC, Align::Right, 0.1f); alpha = s0; }
            else { float s0 = alpha; alpha = s0 * (1 - tlEnd); T(F_MONO, 10.f, x1, by + 26, L"UNTIL YOU ARE BACK", INK3, Align::Right, 0.1f); alpha = s0; }
        } else {
            T(F_MONO, 10.f, x0, by + 26, L"NOTHING HAPPENS ON ITS OWN", INK3, Align::Left, 0.1f);
        }
        std::wstring s;
        if (!on) s = L"Meridian only opens when you start it. Turn the first switch on to have it appear by itself when you are away.";
        else if (sl) s = L"Meridian turns on after " + minutesText(n1) + L" without use. If nobody is back within " + std::to_wstring(n2) + L" more (" + std::to_wstring(total) + L" in all), it closes and the PC goes to sleep.";
        else s = L"Meridian turns on after " + minutesText(n1) + L" without use and stays on until you press a key or move the mouse.";
        paragraph(F_LIGHT, 12.f, RX + 20, py + 107, RW - 40, 16, s, INK2, 2);
    }

    void pageAway(float dt) {
        eyebrow(L"WHILE YOU ARE AWAY");
        T(F_SERIF, 40.f, RX, 108, L"While you're away.", INK);
        T(F_LIGHT, 14.f, RX, 134, L"Let Meridian keep watch, then let the PC rest if nobody comes back.", INK2);
        float y = 152;
        toggleRow(I_T_SAVER, y, L"Turn Meridian on when I am away", L"It becomes your screen saver. Any key or click closes it.", opt.saver, dt); y += 38;
        static const int presetsA[4] = {5, 10, 15, 30}, presetsB[4] = {30, 60, 90, 120};
        float sa1 = easeOut(stepAnim), sa2 = easeOut(stepAnim2);
        if (sa1 > 0.01f) stepperRow(I_MINUS, I_PLUS, I_CHIP0, y + 20, L"After no use for", opt.saverMinutes, presetsA, sa1, dt, stepArea1); else stepArea1 = {0, 0, 0, 0};
        y += 40 * sa1;
        toggleRow(I_T_SLEEP, y, L"Then put the PC to sleep", opt.saver ? L"Meridian closes and the PC sleeps if nobody comes back." : L"Switch the first one on first: the sleep timer starts when Meridian appears.",
                  opt.sleep && opt.saver, dt, opt.saver); y += 38;
        if (sa2 > 0.01f) stepperRow(I_MINUS2, I_PLUS2, I_CHIP4, y + 20, L"Sleep after another", opt.sleepMinutes, presetsB, sa2, dt, stepArea2); else stepArea2 = {0, 0, 0, 0};
        timeline(318, dt);
        buttonsRow(L"Back", I_BACK, ex.found ? L"Update" : L"Install");
    }

    void layoutNotes() {
        const float w = RW - 36 - 18;
        if (std::fabs(nlayoutW - w) < 0.1f && !nitems.empty()) return;
        nitems.clear(); nlayoutW = w; float y = 0;
        for (auto& s : notes) {
            if (y > 0) y += 18;
            nitems.push_back({0, y, s.title, {}}); y += 28;
            for (auto& b : s.bullets) { auto lines = g.wrap(F_LIGHT, 13.f, b, w, 0, 99); nitems.push_back({1, y, b, lines}); y += lines.size() * 19.f + 9; }
        }
        notesH = y;
    }
    void pageNotes(float dt) {
        eyebrow(L"WHAT'S NEW");
        T(F_SERIF, 40.f, RX, 108, L"What's new.", INK);
        T(F_LIGHT, 14.f, RX, 136, L"Newest first. Meridian keeps the same list in Settings, About.", INK2);
        layoutNotes();
        notesView = {RX, 152, RW, 304};
        g.fillRoundRect(notesView.x, notesView.y, notesView.w, notesView.h, 14, C(PANEL));
        g.strokeRoundRect(notesView.x + 0.5f, notesView.y + 0.5f, notesView.w - 1, notesView.h - 1, 14, C(LINEC), 1.f);
        scrollMax = std::max(0.f, notesH + 32 - notesView.h);
        scrollTarget = std::max(0.f, std::min(scrollMax, scrollTarget));
        animTo(scrollShown, scrollTarget, dt, 14.f);
        g.pushClip(notesView.x + 1, notesView.y + 2, notesView.w - 2, notesView.h - 4);
        const float top = notesView.y + 18 - scrollShown;
        for (auto& it : nitems) {
            float yy = top + it.y;
            if (yy > notesView.y + notesView.h + 10 || yy + it.lines.size() * 19.f + 30 < notesView.y - 10) continue;
            if (it.kind == 0) {
                T(F_MONO_M, 11.f, RX + 18, yy + 12, it.text, GOLD, Align::Left, 0.16f);
                g.line(RX + 18, yy + 20, RX + RW - 18, yy + 20, C({0.6f, 0.67f, 0.88f, 0.14f}), 1.f, false);
            } else {
                g.fillCircle(RX + 21, yy + 8.5f, 2.f, C(gfx::withA(INK3, 0.9f)));
                float ly = yy + 13;
                for (auto& l : it.lines) { T(F_LIGHT, 13.f, RX + 34, ly, l, INK2); ly += 19; }
            }
        }
        g.popClip();
        if (scrollMax > 0) {
            float th = notesView.h - 24, tb = std::max(28.f, th * notesView.h / (notesH + 32)), ty = notesView.y + 12 + (th - tb) * (scrollShown / scrollMax);
            g.fillRoundRect(RX + RW - 9, notesView.y + 12, 3, th, 1.5f, C({1, 1, 1, 0.05f}));
            g.fillRoundRect(RX + RW - 9, ty, 3, tb, 1.5f, C({1, 1, 1, 0.28f}));
        }
        buttonsRow(L"Back", I_BACK, mode == Mode::Uninstall ? L"Uninstall" : (ex.found ? L"Update" : L"Next"));
    }

    void pageWorking(double t) {
        const bool inst = mode == Mode::Install;
        eyebrow(inst ? L"SETTING THE SKY" : L"FOLDING THE SKY AWAY");
        double p = std::min(1.0, progShown); int pct = (int)std::floor(p * 100 + 0.5);
        wchar_t b[16]; swprintf(b, 16, L"%d", pct);
        T(F_SERIF, 116.f, RX - 4, 232, b, INK);
        float w = g.textWidth(F_SERIF, 116.f, b);
        T(F_SERIF_I, 38.f, RX + w + 4, 232, L"%", INK3);
        std::wstring text; EnterCriticalSection(&cs); for (auto& e : progLog) if (e.first <= p + 1e-6) text = e.second; LeaveCriticalSection(&cs);
        T(F_LIGHT, 15.f, RX, 282, g.ellipsize(F_LIGHT, 15.f, text, RW), INK2);
        const float by = 302, bh = 6;
        g.fillRoundRect(RX, by, RW, bh, 3, C({1, 1, 1, 0.08f}));
        float fw = std::max(bh, (float)(RW * p));
        g.fillRoundRect(RX, by, fw, bh, 3, C(gfx::lerp(GOLD2, GOLD, (float)p)));
        g.glow(RX + fw, by + bh / 2, 16, GOLD, 0.6f * alpha);
        static const wchar_t* tips[] = {
            L"Right-click Meridian's icon in the taskbar for Start with Windows and other options.",
            L"Themes are just folders. Drop one into the themes folder and Meridian notices.",
            L"The Moon in Meridian is drawn from its real phase and position in the sky, not from a picture.",
            L"Alarms and timers keep working while Meridian is minimised, and the taskbar button flashes when one goes off.",
            L"As a screen saver, any key, click or mouse move closes Meridian again.",
            L"The Halloween tab appears by itself from 1 October to 3 November, every year.",
            L"On the Galaxy theme, keep an eye on the evening sky — and your speakers. Press U if you do not feel like waiting.",
            L"Use the arrows beside the page dots — or press Tab — to glide between rotating panels and widget pages.",
        };
        if (inst) {
            int n = (int)(sizeof(tips) / sizeof(tips[0])); double ph = t / 6.5; int idx = (int)ph % n; float f = (float)(ph - std::floor(ph));
            float a = std::min(1.f, std::min(f * 5.f, (1 - f) * 5.f));
            float save = alpha; alpha = save * a;
            T(F_MONO, 10.f, RX, 372, L"DID YOU KNOW", INK3, Align::Left, 0.18f);
            paragraph(F_LIGHT, 14.5f, RX, 397, RW, 23, tips[idx], INK2, 3);
            alpha = save;
        }
    }

    std::vector<std::wstring> summary;
    void buildSummary() {
        summary.clear();
        if (mode == Mode::Install) {
            summary.push_back(L"Installed in " + g.ellipsize(F_LIGHT, 14.f, opt.dir, RW - 130));
            if (opt.startMenu && opt.desktop) summary.push_back(L"Shortcuts in the Start menu and on your desktop");
            else if (opt.startMenu) summary.push_back(L"A shortcut in the Start menu");
            else if (opt.desktop) summary.push_back(L"A shortcut on your desktop");
            if (opt.runAtLogon) summary.push_back(L"Opens by itself when you sign in");
            if (opt.saver) summary.push_back(L"Turns on by itself after " + minutesText(opt.saverMinutes) + L" of no use");
            if (opt.saver && opt.sleep) summary.push_back(L"Then the PC sleeps after another " + minutesText(opt.sleepMinutes) + L" (" + std::to_wstring(opt.saverMinutes + opt.sleepMinutes) + L" in all)");
            if (opt.gta && opt.halloween) summary.push_back(L"Extra tabs: GTA VI and Halloween");
            else if (opt.gta) summary.push_back(L"An extra tab: GTA VI");
            else if (opt.halloween) summary.push_back(L"An extra tab: Halloween");
            summary.push_back(L"Added to Settings, Apps, so Windows can remove it");
        } else {
            summary.push_back(L"Meridian's files, shortcuts and start-up entry are gone");
            if (ex.saver) summary.push_back(L"Your earlier screen saver is back");
            if (deleteSettings) summary.push_back(L"Your settings, places and notes were deleted");
            else summary.push_back(L"Your settings are kept in " + g.ellipsize(F_LIGHT, 14.f, inst::settingsFolder(false), RW - 190));
        }
    }
    void pageDone(float dt) {
        const bool inst = mode == Mode::Install;
        eyebrow(inst ? L"ALL SET" : L"GOODBYE");
        T(F_SERIF, 54.f, RX, 128, inst ? L"Meridian is" : L"Goodbye,", INK);
        T(F_SERIF_I, 54.f, RX, 190, inst ? L"ready." : L"for now.", GOLD);
        float y = 240;
        const float rowH = summary.size() > 6 ? 24.f : 27.f;
        for (auto& s : summary) { checkMark(RX + 7, y - 4.5f, GOLD); T(F_LIGHT, 14.f, RX + 26, y, s, INK2); y += rowH; }
        if (inst) {
            T(F_LIGHT, 12.5f, RX, 414, L"In Meridian, press the comma key for settings and ? for the list of keys.", INK3);
            toggleRow(I_T_LAUNCH, 424, L"Open Meridian now", opt.startMenu ? L"You can always find it in the Start menu." : L"Starts the clock straight away.", launchAfter, dt);
        }
        const float by = 476;
        button(I_PRIMARY, inst ? L"Finish" : L"Close", {RX + RW - 150, by, 150, 44}, true);
    }
    void pageError() {
        eyebrow(L"SOMETHING WENT WRONG");
        T(F_SERIF, 54.f, RX, 128, L"That did not", INK);
        T(F_SERIF_I, 54.f, RX, 190, L"work.", GOLD);
        std::wstring m; EnterCriticalSection(&cs); m = errMsg; LeaveCriticalSection(&cs);
        float y = paragraph(F_LIGHT, 15.5f, RX, 238, RW, 25, m, INK2, 7);
        paragraph(F_LIGHT, 13.f, RX, y + 10, RW, 20, L"Nothing else on your PC was changed beyond what is described above. You can try again.", INK3, 3);
        buttonsRow(L"Close", I_SECOND, L"Try again");
    }

    // ---------------------------------------------------------------- one frame
    float dtLast = 0.016f;
    void draw() {
        if (!g.begin()) return;
        const double t = nowSec() - t0;
        hits.clear();
        alpha = 1;
        g.vGradientRect(0, 0, W, H, BG0, BG1);
        g.glow(W - 100, 60, 280, rgb(0x4048B0), 0.10f);
        for (auto& s : stars) if (s.x >= LPW) star(s, t, 0.45f);
        if (page != Page::Working) {                                    // now and then a shooting star
            double u = std::fmod(t + 3.0, 13.0) / 1.0;
            if (u < 1.0) {
                float hx = (float)(W * 0.66 - u * 330), hy = (float)(34 + u * 188);
                for (int i = 0; i < 9; ++i) { float k = i / 9.f; float a = (1 - k) * (1 - k) * 0.7f * (float)std::sin(u * 3.14159);
                    g.line(hx + k * 52, hy - k * 36, hx + (k + 0.11f) * 52, hy - (k + 0.11f) * 36, gfx::withA(rgb(0xEAF0FF), a), 1.6f, true); }
            }
        }
        leftPanel(t, dtLast);
        float e = easeOut(pageT); alpha = e; g.setOffset(0, (1 - e) * 12.f);
        switch (page) {
            case Page::Welcome: pageWelcome(dtLast); break;
            case Page::Customise: pageCustomise(dtLast); break;
            case Page::Away: pageAway(dtLast); break;
            case Page::Notes: pageNotes(dtLast); break;
            case Page::Working: pageWorking(t); break;
            case Page::Done: pageDone(dtLast); break;
            case Page::Error: pageError(); break;
        }
        g.setOffset(0, 0); alpha = 1;
        songToast(dtLast);
        if (!g.end()) { g.invalidateDeviceResources(); RECT rc; GetClientRect(hwnd, &rc); g.createWindowTarget(hwnd, rc.right, rc.bottom); g.setDpi(dpi); g.freeBitmap(moonBmp); moonKey = -1; }
    }

    // ---------------------------------------------------------------- the work (on its own thread, so the window keeps moving)
    static DWORD WINAPI workerProc(LPVOID p) {
        App* a = (App*)p; bool running = false; std::wstring err;
        auto cb = [a](double f, const std::wstring& t) { EnterCriticalSection(&a->cs); a->progTarget = f; a->progLog.push_back({f, t}); LeaveCriticalSection(&a->cs); };
        if (a->mode == Mode::Install) err = inst::install(a->pl, a->opt, cb, &running);
        else err = inst::uninstall(a->pl, a->targetDir, a->deleteSettings, cb, &running);
        EnterCriticalSection(&a->cs); a->errMsg = err; a->errRunning = running; a->workDone = true; LeaveCriticalSection(&a->cs);
        return 0;
    }
    void startWork() {
        if (worker) { WaitForSingleObject(worker, INFINITE); CloseHandle(worker); worker = nullptr; }
        EnterCriticalSection(&cs); progTarget = 0; progShown = 0; progLog.clear(); progLog.push_back({0.0, L"Getting ready"}); workDone = false; errMsg.clear(); LeaveCriticalSection(&cs);
        doneAt = 0;
        workStart = nowSec(); paceSec = 8.0 + 2.0 * (double)(GetTickCount() % 1000) / 1000.0;     // 8 to 10 seconds, a different one each time
        song::start();
        go(Page::Working);
        worker = CreateThread(nullptr, 0, workerProc, this, 0, nullptr);
        if (!worker) {
            EnterCriticalSection(&cs);
            errMsg = L"Windows could not start the install task. Please try again.";
            workDone = true;
            LeaveCriticalSection(&cs);
        }
    }
    void tick() {
        double t = nowSec(); float dt = (float)std::min(0.1, t - lastT); lastT = t; dtLast = std::max(0.001f, dt);
        bool anim = page == Page::Working || pageT < 1.f || toastT > 0.f || t < activeUntil;
        for (int i = 0; i < I_COUNT; ++i) {
            float prev = hov[i]; animTo(hov[i], hover == i ? 1.f : 0.f, dt, 16.f);
            if (hov[i] != prev) anim = true;
        }
        pageT = std::min(1.f, pageT + dt / 0.40f);
        { float p1 = stepAnim, p2 = stepAnim2; animTo(stepAnim, opt.saver ? 1.f : 0.f, dt, 10.f); animTo(stepAnim2, (opt.saver && opt.sleep) ? 1.f : 0.f, dt, 10.f); if (stepAnim != p1 || stepAnim2 != p2) anim = true; }
        if (std::fabs(scrollTarget - scrollShown) > 0.1f) anim = true;
        if (songOpen ? songK < 0.998f : songK > 0.002f) anim = true;
        if (!moonBmp.bmp && moonsprite::ready()) anim = true;
        if (page != Page::Working && std::fmod(t - t0 + 3.0, 13.0) < 1.05) anim = true;
        if (page == Page::Working) {
            EnterCriticalSection(&cs); double target = progTarget; bool done = workDone; std::wstring err = errMsg; LeaveCriticalSection(&cs);
            // The work itself takes a moment; the bar does not. It fills over paceSec seconds (8 to 10) along a gently uneven curve, and never gets ahead of
            // what has really been done (so on a slow PC it simply waits for the work, exactly as before).
            const double u = std::min(1.0, (t - workStart) / paceSec), cap = paceCurve(u), want = std::min(target, cap);
            if (u >= 1.0 && target >= 1.0) progShown = 1.0;
            else { progShown += (want - progShown) * (1.0 - std::exp(-7.0 * dt)); if (std::fabs(want - progShown) < 0.0004) progShown = want; progShown = std::min(progShown, want); }
            if (done && !err.empty()) { if (worker) { WaitForSingleObject(worker, INFINITE); CloseHandle(worker); worker = nullptr; } failedOnce = true; go(Page::Error); }
            else if (done && progShown >= 0.999) {
                if (doneAt == 0) doneAt = t;
                if (t - doneAt > 0.6) { if (worker) { WaitForSingleObject(worker, INFINITE); CloseHandle(worker); worker = nullptr; } buildSummary(); progShown = 1; go(Page::Done); }
            }
        }
        if (isStepper(down)) {                                                         // hold to repeat
            anim = true;
            if (holdStart == 0) holdStart = t;
            if (t - holdStart > 0.45 && t - holdLast > 0.06) { holdLast = t; click(down); }
        } else holdStart = 0;
        if (!IsIconic(hwnd) && (anim || (t - lastPaintAt) >= 0.055)) {
            lastPaintAt = t;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    }

    // ---------------------------------------------------------------- input
    void setMinutes(int m) { opt.saverMinutes = std::max(inst::kAwayMin, std::min(inst::kAwayMax, m)); }
    void setSleep(int m) { opt.sleepMinutes = std::max(inst::kSleepMin, std::min(inst::kSleepMax, m)); }
    // one to nine go up by one, after that by five (the same steps as Meridian's own Settings, Away)
    static int awayUp(int v) { return v < 10 ? v + 1 : (v / 5 + 1) * 5; }
    static int awayDown(int v) { return v <= 10 ? v - 1 : ((v - 1) / 5) * 5; }
    static int sleepUp(int v) { return (v / 5 + 1) * 5; }
    static int sleepDown(int v) { return ((v - 1) / 5) * 5; }
    bool pickFolder() {
        IFileOpenDialog* d = nullptr; bool ok = false;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog, (void**)&d)) || !d) return false;
        DWORD o = 0; d->GetOptions(&o); d->SetOptions(o | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        d->SetTitle(L"Choose where to install Meridian");
        if (SUCCEEDED(d->Show(hwnd))) {
            IShellItem* it = nullptr;
            if (SUCCEEDED(d->GetResult(&it)) && it) {
                PWSTR p = nullptr;
                if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p) {
                    std::wstring dir = p; CoTaskMemFree(p);
                    while (dir.size() > 3 && dir.back() == L'\\') dir.pop_back();
                    bool nonEmpty = false; WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
                    if (h != INVALID_HANDLE_VALUE) { do { if (wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L"..")) nonEmpty = true; } while (!nonEmpty && FindNextFileW(h, &fd)); FindClose(h); }
                    std::wstring nm = inst::exeName(dir);
                    if (nonEmpty && _wcsicmp(nm.c_str(), L"Meridian") != 0 && !inst::fileExists(dir + L"\\Meridian.exe") && !inst::pathsEqual(dir, ex.dir)) dir += L"\\Meridian";   // never scatter files into a folder that is already in use
                    if (inst::validInstallDir(dir)) { opt.dir = dir; ok = true; }
                }
                it->Release();
            }
        }
        d->Release();
        return ok;
    }
    void finish() {
        if (mode == Mode::Install && launchAfter && page == Page::Done) ShellExecuteW(hwnd, L"open", (opt.dir + L"\\Meridian.exe").c_str(), nullptr, opt.dir.c_str(), SW_SHOWNORMAL);
        DestroyWindow(hwnd);
    }
    void click(int id) {
        activeUntil = nowSec() + 0.8;
        switch (id) {
            case I_PRIMARY:
                if (page == Page::Welcome && mode == Mode::Install && !ex.found) go(Page::Away);           // a first install asks about the away options on the way
                else if (page == Page::Notes && mode == Mode::Install && !ex.found) go(Page::Away);
                else if (page == Page::Customise) go(Page::Away);
                else if (page == Page::Welcome || page == Page::Away || page == Page::Notes || page == Page::Error) startWork();
                else if (page == Page::Done) finish();
                break;
            case I_SECOND:
                if (page == Page::Welcome && mode == Mode::Install) go(Page::Customise);
                else if (page == Page::Welcome && uninstallFromSetup) cancelUninstall();
                else if (page == Page::Welcome || page == Page::Error) DestroyWindow(hwnd);
                break;
            case I_UNINSTALL: if (page == Page::Welcome && mode == Mode::Install) beginUninstall(); break;
            case I_LINK_CUSTOM: if (page == Page::Welcome && mode == Mode::Install) go(Page::Customise); break;
            case I_SONG: songOpen = !songOpen; break;
            case I_MUTE: song::setMuted(!song::muted()); break;
            case I_BACK: if (page == Page::Customise) go(Page::Welcome); else if (page == Page::Away) go(prevPage == Page::Customise ? Page::Customise : Page::Welcome); else if (page == Page::Notes) go(prevPage == Page::Customise ? Page::Customise : Page::Welcome); break;
            case I_LINK_NOTES: go(Page::Notes); break;
            case I_CHANGE: pickFolder(); break;
            case I_T_START: opt.startMenu = !opt.startMenu; break;
            case I_T_DESK: opt.desktop = !opt.desktop; break;
            case I_T_RUN: opt.runAtLogon = !opt.runAtLogon; break;
            case I_T_SAVER: opt.saver = !opt.saver; break;
            case I_T_GTA: opt.gta = !opt.gta; break;
            case I_T_HAL: opt.halloween = !opt.halloween; break;
            case I_T_DELSET: deleteSettings = !deleteSettings; break;
            case I_T_LAUNCH: launchAfter = !launchAfter; break;
            case I_T_SLEEP: if (opt.saver) opt.sleep = !opt.sleep; break;
            case I_MINUS: setMinutes(awayDown(opt.saverMinutes)); break;
            case I_PLUS: setMinutes(awayUp(opt.saverMinutes)); break;
            case I_MINUS2: setSleep(sleepDown(opt.sleepMinutes)); break;
            case I_PLUS2: setSleep(sleepUp(opt.sleepMinutes)); break;
            case I_CHIP0: setMinutes(5); break; case I_CHIP1: setMinutes(10); break; case I_CHIP2: setMinutes(15); break; case I_CHIP3: setMinutes(30); break;
            case I_CHIP4: setSleep(30); break; case I_CHIP5: setSleep(60); break; case I_CHIP6: setSleep(90); break; case I_CHIP7: setSleep(120); break;
        }
    }
    void mouseMove(int px, int py) {
        mx = px * 96.f / dpi; my = py * 96.f / dpi;
        int h = 0;
        if (page != Page::Working) for (int i = (int)hits.size() - 1; i >= 0; --i) if (hits[i].r.has(mx, my)) { h = hits[i].id; break; }
        hover = h;
    }
    void keyDown(WPARAM vk) {
        if (vk == 'M' && !song::title().empty()) {            // mute / unmute the song, on any page
            song::setMuted(!song::muted()); toast = song::muted() ? L"SOUND OFF" : L"SOUND ON"; toastT = 1.8f; return;
        }
        if (page == Page::Working) return;
        bool shift = GetKeyState(VK_SHIFT) & 0x8000;
        if (vk == VK_TAB) {
            if (hits.empty()) return;
            int idx = -1; for (size_t i = 0; i < hits.size(); ++i) if (hits[i].id == focus) idx = (int)i;
            idx = shift ? (idx <= 0 ? (int)hits.size() - 1 : idx - 1) : (idx + 1) % (int)hits.size();
            focus = hits[idx].id;
        } else if (vk == VK_SPACE) { if (focus) click(focus); }
        else if (vk == VK_RETURN) { if (focus) click(focus); else if (page != Page::Welcome || true) click(I_PRIMARY); }
        else if (vk == VK_ESCAPE) {
            if (page == Page::Customise || page == Page::Away || page == Page::Notes) click(I_BACK);
            else if (page == Page::Welcome && uninstallFromSetup) cancelUninstall();
            else DestroyWindow(hwnd);
        } else if (page == Page::Notes) {
            float vh = notesView.h;
            if (vk == VK_DOWN) scrollTarget += 44; else if (vk == VK_UP) scrollTarget -= 44;
            else if (vk == VK_NEXT) scrollTarget += vh - 40; else if (vk == VK_PRIOR) scrollTarget -= vh - 40;
            else if (vk == VK_HOME) scrollTarget = 0; else if (vk == VK_END) scrollTarget = scrollMax;
        } else if (page == Page::Away && opt.saver && (vk == VK_UP || vk == VK_RIGHT || vk == VK_DOWN || vk == VK_LEFT)) {
            bool up = vk == VK_UP || vk == VK_RIGHT;
            if (inSleepGroup(focus) && opt.sleep) setSleep(up ? sleepUp(opt.sleepMinutes) : sleepDown(opt.sleepMinutes));
            else setMinutes(up ? awayUp(opt.saverMinutes) : awayDown(opt.saverMinutes));
        }
    }
    void wheel(int delta) {
        if (page == Page::Notes && notesView.has(mx, my)) scrollTarget -= delta / 120.f * 54.f;
        else if (page == Page::Away && opt.saver) {
            bool up = delta > 0;
            if (stepArea2.h > 0 && opt.sleep && (inSleepGroup(hover) || stepArea2.has(mx, my))) setSleep(up ? sleepUp(opt.sleepMinutes) : sleepDown(opt.sleepMinutes));
            else if (stepArea1.h > 0 && (hover == I_MINUS || hover == I_PLUS || (hover >= I_CHIP0 && hover <= I_CHIP3) || stepArea1.has(mx, my))) setMinutes(up ? awayUp(opt.saverMinutes) : awayDown(opt.saverMinutes));
        }
    }
};

// ====================================================================== window
static App* gApp = nullptr;
static const wchar_t* CLASS_NAME = L"MeridianSetupWnd";

static float dpiOf(HWND h) {
    typedef UINT (WINAPI *Fn)(HWND);
    static Fn fn = (Fn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (fn && h) { UINT d = fn(h); if (d) return (float)d; }
    HDC dc = GetDC(h); int d = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(h, dc);
    return d > 0 ? (float)d : 96.f;
}
static void fitWindow(HWND hwnd, float dpi, const RECT* suggested) {
    int cw = (int)std::lround(W * dpi / 96.f), ch = (int)std::lround(H * dpi / 96.f);
    RECT r{0, 0, cw, ch}; DWORD style = (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE), ex = (DWORD)GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    typedef BOOL (WINAPI *AdjFn)(LPRECT, DWORD, BOOL, DWORD, UINT);
    static AdjFn adj = (AdjFn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi");
    if (adj) adj(&r, style, FALSE, ex, (UINT)dpi); else AdjustWindowRectEx(&r, style, FALSE, ex);
    int ww = r.right - r.left, wh = r.bottom - r.top, x, y;
    if (suggested) { x = suggested->left; y = suggested->top; }
    else { RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0); x = wa.left + ((wa.right - wa.left) - ww) / 2; y = wa.top + ((wa.bottom - wa.top) - wh) / 2; if (y < wa.top) y = wa.top; }
    SetWindowPos(hwnd, nullptr, x, y, ww, wh, SWP_NOZORDER | SWP_NOACTIVATE);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    App* a = gApp;
    switch (m) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); if (a && !IsIconic(h)) a->draw(); EndPaint(h, &ps); return 0; }
        case WM_TIMER: if (a && (!IsIconic(h) || a->page == Page::Working)) a->tick(); return 0;
        case WM_MOUSEMOVE: { if (a) { a->activeUntil = nowSec() + 0.5; a->mouseMove((short)LOWORD(l), (short)HIWORD(l)); } TRACKMOUSEEVENT te{sizeof(te), TME_LEAVE, h, 0}; TrackMouseEvent(&te); return 0; }
        case WM_MOUSELEAVE: if (a) { a->activeUntil = nowSec() + 0.5; a->hover = 0; a->mx = a->my = -1; } return 0;
        case WM_LBUTTONDOWN: if (a) { a->activeUntil = nowSec() + 0.8; a->mouseMove((short)LOWORD(l), (short)HIWORD(l)); a->down = a->hover; a->focus = 0; SetCapture(h); if (isStepper(a->down)) a->click(a->down); } return 0;
        case WM_LBUTTONUP: if (a) { ReleaseCapture(); a->activeUntil = nowSec() + 0.8; a->mouseMove((short)LOWORD(l), (short)HIWORD(l)); int d = a->down; a->down = 0; if (d && d == a->hover && !isStepper(d)) a->click(d); } return 0;
        case WM_MOUSEWHEEL: if (a) { a->activeUntil = nowSec() + 0.8; a->wheel((short)HIWORD(w)); } return 0;
        case WM_KEYDOWN: if (a) { a->activeUntil = nowSec() + 0.8; a->keyDown(w); } return 0;
        case WM_SETCURSOR: if (LOWORD(l) == HTCLIENT && a && a->hover) { SetCursor(LoadCursorW(nullptr, IDC_HAND)); return TRUE; } break;
        case WM_DPICHANGED: if (a) {
            a->dpi = (float)HIWORD(w); fitWindow(h, a->dpi, (const RECT*)l);
            RECT rc; GetClientRect(h, &rc); a->g.createWindowTarget(h, rc.right, rc.bottom); a->g.setDpi(a->dpi); a->g.freeBitmap(a->moonBmp); a->moonKey = -1;
        } return 0;
        case WM_CLOSE: if (a && a->page == Page::Working) return 0; DestroyWindow(h); return 0;
        case WM_DESTROY: KillTimer(h, 1); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ====================================================================== command line
struct Args {
    bool silent = false, uninstall = false, fromTemp = false, deleteSettings = false;
    bool hasDir = false; std::wstring dir, targetDir, syncName;
    DWORD parentPid = 0;
    int saverMin = 0, sleepMin = 0; bool noSleep = false, run = false, noDesk = false, noStart = false, noGta = false, noHal = false;
};
static bool startsWithI(const std::wstring& s, const wchar_t* p) { size_t n = wcslen(p); return s.size() >= n && _wcsnicmp(s.c_str(), p, n) == 0; }
static Args parseArgs() {
    Args a;
    std::wstring raw = GetCommandLineW();
    int argc = 0; LPWSTR* argv = CommandLineToArgvW(raw.c_str(), &argc);
    for (int i = 1; argv && i < argc; ++i) {
        std::wstring s = argv[i];
        if (startsWithI(s, L"/D=")) {
            std::wstring v = s.substr(3);
            while (i + 1 < argc && argv[i + 1][0] != L'/') { v += L' '; v += argv[++i]; }
            while (!v.empty() && (v.back() == L' ' || v.back() == L'"')) v.pop_back();
            while (!v.empty() && v.front() == L'"') v.erase(0, 1);
            a.hasDir = !v.empty(); a.dir = v;
            continue;
        }
        if (!_wcsicmp(s.c_str(), L"/S")) a.silent = true;
        else if (!_wcsicmp(s.c_str(), L"/uninstall")) a.uninstall = true;
        else if (!_wcsicmp(s.c_str(), L"/_from")) a.fromTemp = true;
        else if (!_wcsicmp(s.c_str(), L"/DeleteSettings")) a.deleteSettings = true;
        else if (startsWithI(s, L"/_dir=")) a.targetDir = s.substr(6);
        else if (startsWithI(s, L"/_parent=")) a.parentPid = (DWORD)_wtoi(s.c_str() + 9);
        else if (startsWithI(s, L"/_sync=")) a.syncName = s.substr(7);
        else if (startsWithI(s, L"/ScreenSaver=")) { int m = _wtoi(s.c_str() + 13); if (m >= 1) a.saverMin = m; }
        else if (startsWithI(s, L"/SleepAfter=")) { int m = _wtoi(s.c_str() + 12); if (m >= 1) a.sleepMin = m; }
        else if (!_wcsicmp(s.c_str(), L"/NoSleep")) a.noSleep = true;
        else if (!_wcsicmp(s.c_str(), L"/StartWithWindows")) a.run = true;
        else if (!_wcsicmp(s.c_str(), L"/NoDesktop")) a.noDesk = true;
        else if (!_wcsicmp(s.c_str(), L"/NoStartMenu")) a.noStart = true;
        else if (!_wcsicmp(s.c_str(), L"/NoGTA")) a.noGta = true;
        else if (!_wcsicmp(s.c_str(), L"/NoHalloween")) a.noHal = true;
    }
    if (argv) LocalFree(argv);
    return a;
}
static std::wstring selfPath() { wchar_t b[MAX_PATH * 2]; DWORD n = GetModuleFileNameW(nullptr, b, MAX_PATH * 2); return n && n < MAX_PATH * 2 ? std::wstring(b, n) : L""; }
static void signalSilentSync(const std::wstring& syncName, DWORD rc) {
    if (syncName.empty()) return;
    if (HANDLE hm = OpenFileMappingW(FILE_MAP_WRITE, FALSE, (syncName + L"_rc").c_str())) {
        if (DWORD* p = (DWORD*)MapViewOfFile(hm, FILE_MAP_WRITE, 0, 0, sizeof(DWORD))) { *p = rc; UnmapViewOfFile(p); }
        CloseHandle(hm);
    }
    if (HANDLE he = OpenEventW(EVENT_MODIFY_STATE, FALSE, (syncName + L"_ev").c_str())) {
        SetEvent(he);
        CloseHandle(he);
    }
}
// Copies this program into %TEMP% and starts the copy as the uninstaller for `dir`.
// In silent mode (`silentRc != nullptr`), waits until the worker finishes removing the app and reports its real exit code (0/2/3),
// then exits so the worker can finish removing uninstall.exe and the empty folder right afterwards.
static bool startTempUninstaller(const std::wstring& self, const std::wstring& dir, bool silent, bool deleteSettings, int* silentRc) {
    if (self.empty()) return false;
    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
    wchar_t nm[64]; swprintf(nm, 64, L"meridian-uninstall-%08lx.exe", (unsigned long)GetTickCount());
    std::wstring copy = std::wstring(tmp) + nm;
    if (!CopyFileW(self.c_str(), copy.c_str(), FALSE)) return false;

    std::wstring syncName;
    HANDLE hMap = nullptr, hEv = nullptr;
    DWORD* rcPtr = nullptr;
    if (silent && silentRc) {
        syncName = L"Local\\MeridianUninst_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount());
        hMap = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(DWORD), (syncName + L"_rc").c_str());
        if (hMap) { rcPtr = (DWORD*)MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(DWORD)); if (rcPtr) *rcPtr = 2; }
        hEv = CreateEventW(nullptr, TRUE, FALSE, (syncName + L"_ev").c_str());
    }

    std::wstring cl = L"\"" + copy + L"\" /uninstall /_from \"/_dir=" + dir + L"\"" + (silent ? L" /S" : L"") + (deleteSettings ? L" /DeleteSettings" : L"");
    if (!syncName.empty() && hMap && rcPtr && hEv)
        cl += L" /_parent=" + std::to_wstring(GetCurrentProcessId()) + L" /_sync=" + syncName;
    std::vector<wchar_t> c(cl.begin(), cl.end()); c.push_back(0);
    STARTUPINFOW si{}; si.cb = sizeof(si); PROCESS_INFORMATION pi{};
    if (!CreateProcessW(copy.c_str(), c.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        if (rcPtr) UnmapViewOfFile(rcPtr);
        if (hMap) CloseHandle(hMap);
        if (hEv) CloseHandle(hEv);
        DeleteFileW(copy.c_str());
        return false;
    }
    if (silent && silentRc && hEv && rcPtr) {
        HANDLE hs[2] = {hEv, pi.hProcess};
        DWORD wr = WaitForMultipleObjects(2, hs, FALSE, 120000);
        if (wr == WAIT_OBJECT_0) *silentRc = (int)*rcPtr;
        else if (wr == WAIT_OBJECT_0 + 1) { DWORD ec = 2; GetExitCodeProcess(pi.hProcess, &ec); *silentRc = (int)ec; }
        else *silentRc = 2;
    }
    if (rcPtr) UnmapViewOfFile(rcPtr);
    if (hMap) CloseHandle(hMap);
    if (hEv) CloseHandle(hEv);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return true;
}
static void applyArgs(const Args& a, inst::Options& o) {
    if (a.hasDir) o.dir = a.dir;
    if (a.saverMin) { o.saver = true; o.saverMinutes = std::max(inst::kAwayMin, std::min(inst::kAwayMax, a.saverMin)); }
    if (a.sleepMin) { o.saver = true; o.sleep = true; o.sleepMinutes = std::max(inst::kSleepMin, std::min(inst::kSleepMax, a.sleepMin)); }       // sleep only happens after Meridian has turned on, so it needs the screen saver
    if (a.noSleep) o.sleep = false;
    if (a.run) o.runAtLogon = true;
    if (a.noDesk) o.desktop = false;
    if (a.noStart) o.startMenu = false;
    if (a.noGta) o.gta = false;
    if (a.noHal) o.halloween = false;
}
// the uninstaller runs from a copy in %TEMP%, so that the original uninstall.exe can be deleted with the rest of the folder
static void deleteSelfLater(const std::wstring& exe) {
    if (exe.empty()) return;
    for (wchar_t ch : exe) if (ch == L'"' || ch < 32) return;
    wchar_t sysDir[MAX_PATH];
    UINT n = GetSystemDirectoryW(sysDir, MAX_PATH);
    std::wstring cmdExe = (n > 0 && n < MAX_PATH) ? std::wstring(sysDir, n) + L"\\cmd.exe" : L"cmd.exe";
    SetEnvironmentVariableW(L"MERIDIAN_DEL", exe.c_str());
    std::wstring cmd = L"\"cmd.exe\" /D /C ping -n 3 127.0.0.1 >nul & del /F /Q \"%MERIDIAN_DEL%\"";
    STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE; PROCESS_INFORMATION pi{};
    std::vector<wchar_t> c(cmd.begin(), cmd.end()); c.push_back(0);
    if (CreateProcessW(cmdExe.c_str(), c.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Args args = parseArgs();
    const std::wstring self = selfPath();

    // ---- an uninstaller started from the install folder hands over to a temporary copy of itself
    if (args.uninstall && !args.fromTemp) {
        std::wstring dir = inst::parentDir(self);
        if (_wcsicmp(inst::exeName(self).c_str(), L"uninstall.exe") == 0 && inst::looksInstalled(dir)) {      // (also after an uninstall that left a file behind: Meridian.exe may be gone while Windows still lists it)
            int silentRc = 0;
            if (startTempUninstaller(self, dir, args.silent, args.deleteSettings, args.silent ? &silentRc : nullptr)) return silentRc;
            args.targetDir = dir;                           // could not make the copy: uninstall in place (the uninstaller itself may stay behind)
        }
    }

    // one setup at a time (the uninstaller's temporary copy is a different step, so it is not blocked by the program that started it)
    HANDLE once = CreateMutexW(nullptr, FALSE, args.fromTemp ? L"Local\\MeridianSetupUninstallStep" : L"Local\\MeridianSetupRunning");
    if (once && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (!args.silent) MessageBoxW(nullptr, L"Meridian Setup is already open.", L"Meridian Setup", MB_ICONINFORMATION);
        signalSilentSync(args.syncName, 4);
        return 4;
    }

    Payload pl;
    if (!pl.load(song::key())) {
        if (!args.silent) MessageBoxW(nullptr, L"This setup file is damaged. Please download it again.", L"Meridian Setup", MB_ICONERROR);
        signalSilentSync(args.syncName, 1);
        return 1;
    }

    App app; gApp = &app;
    app.pl = pl; app.fromTemp = args.fromTemp;
    app.ex = inst::detect();
    app.mode = args.uninstall ? Mode::Uninstall : Mode::Install;
    if (args.uninstall) {
        app.targetDir = !args.targetDir.empty() ? args.targetDir : (app.ex.found ? app.ex.dir : L"");
        if (app.targetDir.empty()) {
            if (!args.silent) MessageBoxW(nullptr, L"Meridian does not seem to be installed for this user.", L"Meridian Setup", MB_ICONINFORMATION);
            signalSilentSync(args.syncName, 1);
            return 1;
        }
        app.deleteSettings = args.deleteSettings;
        app.ex.saver = app.ex.found && app.ex.saver;
    }
    app.opt.dir = app.ex.found ? app.ex.dir : inst::defaultDir();
    if (app.ex.found) { app.opt.startMenu = app.ex.startMenu; app.opt.desktop = app.ex.desktop; app.opt.runAtLogon = app.ex.runAtLogon; app.opt.saver = app.ex.saver;
                        app.opt.saverMinutes = app.ex.saverMinutes; app.opt.gta = app.ex.gta; app.opt.halloween = app.ex.halloween;
                        app.opt.sleep = app.ex.sleep; app.opt.sleepMinutes = app.ex.sleepMinutes; }
    applyArgs(args, app.opt);

    // ---- silent: no window at all, the exit code says how it went
    if (args.silent) {
        int rc = 0; bool running = false;
        const bool deferSelf = args.uninstall && args.fromTemp && !args.syncName.empty();
        HANDLE hParent = (deferSelf && args.parentPid) ? OpenProcess(SYNCHRONIZE, FALSE, args.parentPid) : nullptr;
        inst::Progress none = [](double, const std::wstring&) {};
        std::wstring err = args.uninstall ? inst::uninstall(pl, app.targetDir, args.deleteSettings, none, &running, deferSelf) : inst::install(pl, app.opt, none, &running);
        if (!err.empty()) rc = running ? 3 : 2;
        if (deferSelf) {
            signalSilentSync(args.syncName, (DWORD)rc);      // wake the waiting caller with the real exit code
            if (hParent) { WaitForSingleObject(hParent, 10000); CloseHandle(hParent); }
            if (rc == 0) inst::finishDeferredUninstall(app.targetDir);
        } else if (hParent) CloseHandle(hParent);
        if (args.fromTemp) deleteSelfLater(self);
        return rc;
    }

    // ---- the wizard
    if (!app.g.init()) { MessageBoxW(nullptr, L"Setup could not start Direct2D / DirectWrite. Please make sure Windows is up to date.", L"Meridian Setup", MB_ICONERROR); return 1; }
    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = WndProc; wc.hInstance = hInst; wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APP)); wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hbrBackground = nullptr; wc.lpszClassName = CLASS_NAME;
    RegisterClassExW(&wc);
    const wchar_t* title = args.uninstall ? L"Uninstall Meridian" : L"Meridian Setup  ·  Sonnet Edition";
    song::start();                                         // the song starts with the setup window (it decodes on its own thread while the window opens)
    app.hwnd = CreateWindowExW(0, CLASS_NAME, title, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, 900, 620, nullptr, nullptr, hInst, nullptr);
    if (!app.hwnd) return 1;
    app.dpi = dpiOf(app.hwnd);
    fitWindow(app.hwnd, app.dpi, nullptr);
    { BOOL dark = TRUE; DwmSetWindowAttribute(app.hwnd, 20, &dark, sizeof(dark));
      COLORREF cap = RGB(0x08, 0x0B, 0x24), txt = RGB(0xB8, 0xC2, 0xE8), bor = RGB(0x1A, 0x1E, 0x4A);
      DwmSetWindowAttribute(app.hwnd, 35, &cap, sizeof(cap)); DwmSetWindowAttribute(app.hwnd, 36, &txt, sizeof(txt)); DwmSetWindowAttribute(app.hwnd, 34, &bor, sizeof(bor)); }
    RECT rc; GetClientRect(app.hwnd, &rc);
    if (!app.g.createWindowTarget(app.hwnd, rc.right, rc.bottom)) { MessageBoxW(nullptr, L"Setup could not create its window surface.", L"Meridian Setup", MB_ICONERROR); return 1; }
    app.g.setDpi(app.dpi);
    if (args.uninstall) app.opt.saver = false;
    app.init();
    SetTimer(app.hwnd, 1, 16, nullptr);
    ShowWindow(app.hwnd, SW_SHOW); UpdateWindow(app.hwnd); SetForegroundWindow(app.hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    song::shutdown();
    moonsprite::shutdown();
    if (app.worker) { WaitForSingleObject(app.worker, INFINITE); CloseHandle(app.worker); app.worker = nullptr; }
    app.g.freeBitmap(app.moonBmp);
    app.g.shutdown();
    if (once) CloseHandle(once);
    CoUninitialize();
    if (args.fromTemp) deleteSelfLater(self);
    return 0;
}
