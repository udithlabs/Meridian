// Meridian as a Windows screen saver: see saver.h.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <cwctype>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <atomic>
#include <string>
#include "saver.h"

namespace saver {

namespace {

const UINT_PTR kTimerId = 0x5AFE;          // the quarter-second input check
const ULONGLONG kGraceMs = 700;            // input in the first moments is ignored (the click or key that started a test run)
const ULONGLONG kBaselineMs = 1000;        // the "last input" time is noted this long after the start
const int kMoveSlop = 6;                   // pixels the pointer may wander before it counts as the user
const ULONGLONG kHardStopMs = 1300;        // after the keyboard or mouse is used the saver is gone within this time, whatever the picture or the graphics driver is doing

HWND g_clock = nullptr;
ULONGLONG g_start = 0;
std::atomic<LONG> g_pointerX{0}, g_pointerY{0};
bool g_haveBaseline = false;
DWORD g_lastInput = 0;
bool g_closing = false;

// sleep: how long the saver may stay on the screen before the PC goes to sleep (0 = never), and when it came on (the PC's own clock, which
// does not run while the PC sleeps, so a nap taken by Windows does not use up the time)
ULONGLONG g_sleepMs = 0, g_startUnbiased = 0;
bool g_sleepNow = false;

ULONGLONG unbiasedMs() {
    ULONGLONG t = 0;
    QueryUnbiasedInterruptTime(&t);                 // 100 ns units
    return t / 10000;
}

const wchar_t* const kDesktopKey = L"Control Panel\\Desktop";
const wchar_t* const kBackupKey = L"Software\\Meridian\\ScreenSaverBackup";
const wchar_t* const kSaverKey = L"Software\\Meridian\\Saver";

bool regGetStr(const wchar_t* key, const wchar_t* name, std::wstring& out, DWORD* typeOut = nullptr) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
    wchar_t buf[2048];
    DWORD sz = sizeof buf - 2, type = 0;
    const LONG r = RegQueryValueExW(k, name, nullptr, &type, (LPBYTE)buf, &sz);
    RegCloseKey(k);
    if (r != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return false;
    buf[sz / 2] = 0;
    out = buf;
    if (typeOut) *typeOut = type;                  // REG_SZ or REG_EXPAND_SZ; the text comes back as stored (a %SystemRoot% in it is not expanded)
    return true;
}
bool regGetDword(const wchar_t* key, const wchar_t* name, DWORD& out) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
    DWORD v = 0, sz = sizeof v, type = 0;
    const LONG r = RegQueryValueExW(k, name, nullptr, &type, (LPBYTE)&v, &sz);
    RegCloseKey(k);
    if (r != ERROR_SUCCESS || type != REG_DWORD) return false;
    out = v;
    return true;
}
void regSetStr(const wchar_t* key, const wchar_t* name, const std::wstring& v, DWORD type = REG_SZ) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    RegSetValueExW(k, name, 0, type == REG_EXPAND_SZ ? REG_EXPAND_SZ : REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
}
void regDelValue(const wchar_t* key, const wchar_t* name) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    RegDeleteValueW(k, name);
    RegCloseKey(k);
}

// the sleep time from the registry, or 0 for never (a value outside 20 to 120 is pulled into that range)
int sleepMinutesFromRegistry() {
    DWORD v = 0;
    if (!regGetDword(kSaverKey, L"SleepAfterMinutes", v) || v == 0) return 0;
    return (int)std::max<DWORD>(kSleepMinMinutes, std::min<DWORD>(kSleepMaxMinutes, v));
}

std::wstring exeFolder() {
    wchar_t b[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(nullptr, b, (DWORD)(sizeof b / sizeof b[0]));
    if (n == 0 || n >= sizeof b / sizeof b[0]) return L"";
    std::wstring p(b, n);
    const size_t i = p.find_last_of(L"\\/");
    return i == std::wstring::npos ? L"" : p.substr(0, i);
}
std::wstring exeFile() {
    wchar_t b[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(nullptr, b, (DWORD)(sizeof b / sizeof b[0]));
    return n == 0 || n >= sizeof b / sizeof b[0] ? L"" : std::wstring(b, n);
}

// Windows reads SCRNSAVE.EXE again when it is told the user's settings changed (what Install-ScreenSaver.bat does with rundll32). Not waited for.
void refreshWindowsSaver() {
    wchar_t sysDir[MAX_PATH];
    const UINT sn = GetSystemDirectoryW(sysDir, MAX_PATH);
    if (sn == 0 || sn >= MAX_PATH) return;
    const std::wstring app = std::wstring(sysDir, sn) + L"\\rundll32.exe";
    wchar_t cmd[] = L"rundll32.exe user32.dll,UpdatePerUserSystemParameters 1, True";
    STARTUPINFOW si;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof pi);
    if (CreateProcessW(app.c_str(), cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
}

// Windows only starts files called .scr: make Meridian.scr (a copy of this exe) beside it, so that a running Meridian.exe is never locked by it
bool makeScr(const std::wstring& exe, const std::wstring& scr, std::wstring* problem) {
    const std::wstring tmp = scr + L".new";
    SetFileAttributesW(tmp.c_str(), FILE_ATTRIBUTE_NORMAL);
    bool ok = CopyFileW(exe.c_str(), tmp.c_str(), FALSE) != 0;
    DWORD err = ok ? 0 : GetLastError();
    if (ok) {
        ok = MoveFileExW(tmp.c_str(), scr.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
        if (!ok) err = GetLastError();
    }
    if (!ok) DeleteFileW(tmp.c_str());
    if (ok) return true;
    if (GetFileAttributesW(scr.c_str()) != INVALID_FILE_ATTRIBUTES) return true;       // could not refresh it (in use?), but an earlier copy is there
    if (problem) *problem = err == ERROR_ACCESS_DENIED ? L"Meridian could not write Meridian.scr next to Meridian.exe (access denied). Move the Meridian folder somewhere you can write to."
                                                       : L"Meridian could not write Meridian.scr next to Meridian.exe.";
    return false;
}

void goToSleep() {
#ifdef MERIDIAN_DEV
    if (const char* log = std::getenv("MERIDIAN_TEST_SLEEP_LOG")) {     // test builds: write a line instead of really sleeping
        if (FILE* f = std::fopen(log, "a")) { std::fprintf(f, "SLEEP\n"); std::fclose(f); }
        return;
    }
#endif
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {      // (normally already allowed for the user; harmless otherwise)
        TOKEN_PRIVILEGES tp;
        ZeroMemory(&tp, sizeof tp);
        if (LookupPrivilegeValueW(nullptr, L"SeShutdownPrivilege", &tp.Privileges[0].Luid)) {
            tp.PrivilegeCount = 1;
            tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            AdjustTokenPrivileges(token, FALSE, &tp, 0, nullptr, nullptr);
        }
        CloseHandle(token);
    }
    typedef BOOLEAN(WINAPI * SetSuspendStateFn)(BOOLEAN hibernate, BOOLEAN force, BOOLEAN disableWakeEvent);
    if (HMODULE m = LoadLibraryExW(L"powrprof.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        if (auto fn = (SetSuspendStateFn)(void*)GetProcAddress(m, "SetSuspendState")) fn(FALSE, FALSE, FALSE);   // FALSE: sleep, not hibernate; returns when the PC wakes up
        FreeLibrary(m);
    }
}

DWORD lastInputTick() {
    LASTINPUTINFO li;
    ZeroMemory(&li, sizeof li);
    li.cbSize = sizeof li;
    return GetLastInputInfo(&li) ? li.dwTime : 0;
}

// The last line of defence. It watches the keyboard and mouse itself (GetLastInputInfo, no window and no message involved), on a thread of its own,
// so it works even when the main thread is stuck: waiting for a graphics driver that is still waking up, a frame that never finishes, or a
// shutdown that hangs. Windows keeps the screen on a bare grey desktop until the saver has ended, so a saver that lingers is the grey wait a
// person sees. TerminateProcess skips every clean-up (nothing in the saver needs one) and cannot be held up by anything.
static bool pointerWithinSlop(POINT& prev) {
    POINT p;
    if (!GetCursorPos(&p)) return false;
    const bool moved = (p.x != prev.x || p.y != prev.y);
    prev = p;
    const int dx = p.x - (int)g_pointerX.load(std::memory_order_relaxed);
    const int dy = p.y - (int)g_pointerY.load(std::memory_order_relaxed);
    return moved && dx * dx + dy * dy <= kMoveSlop * kMoveSlop;
}

DWORD WINAPI watchdogProc(LPVOID) {
    DWORD base = 0;
    bool haveBase = false;
    ULONGLONG seen = 0;
    POINT prevP{g_pointerX.load(std::memory_order_relaxed), g_pointerY.load(std::memory_order_relaxed)};
    for (;;) {
        Sleep(30);
        const ULONGLONG now = GetTickCount64();
        if (now - g_start < kBaselineMs) { GetCursorPos(&prevP); continue; }
        const DWORD li = lastInputTick();
        if (!haveBase) { base = li; haveBase = true; GetCursorPos(&prevP); continue; }
        if (seen == 0) {
            if (li != base) {
                // ignore tiny sub-slop optical mouse tremor so it never triggers TerminateProcess
                if (pointerWithinSlop(prevP)) { base = li; continue; }
                seen = now;
            }
            continue;
        }
        if (now - seen >= kHardStopMs) TerminateProcess(GetCurrentProcess(), 0);
    }
    return 0;
}

// the same test for the clock window and for the black windows on the other monitors
bool inputMessage(UINT msg, WPARAM wp, LPARAM lp) {
    const bool grace = GetTickCount64() - g_start < kGraceMs;
    switch (msg) {
        case WM_KEYDOWN: case WM_SYSKEYDOWN:
            if (grace || (lp & (1 << 30))) return false;               // a key still held down from the one that started it
            return true;
        case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN: case WM_XBUTTONDOWN:
        case WM_LBUTTONDBLCLK: case WM_RBUTTONDBLCLK: case WM_MBUTTONDBLCLK:
        case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: case 0x0246 /* WM_POINTERDOWN */:
            return !grace;
        case WM_MOUSEMOVE: {
            POINT p;
            if (!GetCursorPos(&p)) return false;
            if (grace) {
                g_pointerX.store(p.x, std::memory_order_relaxed);
                g_pointerY.store(p.y, std::memory_order_relaxed);
                return false;                                          // follow the pointer until things have settled
            }
            const int dx = p.x - (int)g_pointerX.load(std::memory_order_relaxed);
            const int dy = p.y - (int)g_pointerY.load(std::memory_order_relaxed);
            return dx * dx + dy * dy > kMoveSlop * kMoveSlop;          // Windows sends a stray move when a window appears: that is not the user
        }
        case WM_TIMER: {
            if (wp != kTimerId) return false;
            if (GetTickCount64() - g_start < kBaselineMs) return false;
            static POINT timerPrevP = {0, 0};
            if (!g_haveBaseline) { g_lastInput = lastInputTick(); GetCursorPos(&timerPrevP); g_haveBaseline = true; return false; }
            const DWORD li = lastInputTick();
            if (li == g_lastInput) return false;
            if (pointerWithinSlop(timerPrevP)) { g_lastInput = li; return false; }
            return true;                                               // somebody used the keyboard or mouse (even if focus is elsewhere)
        }
        default: return false;
    }
}

LRESULT CALLBACK coverProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_SETCURSOR) { SetCursor(nullptr); return TRUE; }
    if (msg == WM_ERASEBKGND) { RECT r; GetClientRect(hwnd, &r); FillRect((HDC)wp, &r, (HBRUSH)GetStockObject(BLACK_BRUSH)); return 1; }
    if (msg == WM_SYSCOMMAND && (wp & 0xFFF0) == SC_SCREENSAVE) return 0;
    if (inputMessage(msg, wp, lp) && g_clock && !g_closing) { g_closing = true; PostMessageW(g_clock, kLeaveMessage, 0, 0); return 0; }      // the clock window fades out, then goes
    return DefWindowProcW(hwnd, msg, wp, lp);
}

struct Cover { HINSTANCE inst; HMONITOR main; };

BOOL CALLBACK coverMonitor(HMONITOR mon, HDC, LPRECT, LPARAM lp) {
    const Cover* c = (const Cover*)lp;
    if (mon == c->main) return TRUE;
    MONITORINFO mi;
    ZeroMemory(&mi, sizeof mi);
    mi.cbSize = sizeof mi;
    if (!GetMonitorInfoW(mon, &mi)) return TRUE;
    HWND w = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"MeridianSaverCover", L"", WS_POPUP,
                             mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                             nullptr, nullptr, c->inst, nullptr);
    if (w) ShowWindow(w, SW_SHOWNOACTIVATE);
    return TRUE;
}

// "/p:1234", "/p 1234", "-c", "/S" ...: the letter after the slash or dash, in lower case, and the number after it (if any)
bool readSwitch(const std::wstring& arg, wchar_t& letter, std::wstring& tail) {
    size_t i = 0;
    if (arg.empty() || (arg[0] != L'/' && arg[0] != L'-')) return false;
    while (i < arg.size() && (arg[i] == L'/' || arg[i] == L'-')) ++i;
    if (i >= arg.size()) return false;
    std::wstring word;
    while (i < arg.size() && arg[i] != L':') word.push_back((wchar_t)std::towlower(arg[i++]));
    if (i < arg.size() && arg[i] == L':') ++i;
    tail = arg.substr(i);
    if (word == L"screensaver" || word == L"saver") { letter = L's'; return true; }      // handy for a shortcut of your own
    if (word.size() != 1) return false;
    letter = word[0];
    return true;
}

} // namespace

Mode modeFromCommandLine(HWND* parent) {
    if (parent) *parent = nullptr;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return Mode::Normal;
    Mode mode = Mode::Normal;
    if (argc >= 2) {
        wchar_t letter = 0;
        std::wstring tail;
        if (readSwitch(argv[1], letter, tail)) {
            if (letter == L's') mode = Mode::Run;
            else if (letter == L'p') mode = Mode::Preview;
            else if (letter == L'c') {
                mode = Mode::Configure;
                if (tail.empty() && argc >= 3) tail = argv[2];
                if (!tail.empty() && parent) *parent = (HWND)(ULONG_PTR)_wcstoui64(tail.c_str(), nullptr, 10);
            } else if (letter == L'a') mode = Mode::Nothing;
        }
    }
    LocalFree(argv);
    if (parent && *parent && !IsWindow(*parent)) *parent = nullptr;
    return mode;
}

bool oneAtATime() {
    HANDLE h = CreateMutexW(nullptr, FALSE, L"Local\\Meridian.ScreenSaver");
    if (!h) return true;                                               // cannot tell: show it
    if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(h); return false; }
    return true;                                                       // the handle stays open until the process ends
}

void begin(HINSTANCE inst, HWND clock) {
    g_clock = clock;
    g_start = GetTickCount64();
    POINT p0{0, 0};
    if (GetCursorPos(&p0)) {
        g_pointerX.store(p0.x, std::memory_order_relaxed);
        g_pointerY.store(p0.y, std::memory_order_relaxed);
    }
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = coverProc;
    wc.hInstance = inst;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"MeridianSaverCover";
    RegisterClassExW(&wc);
    Cover c{inst, MonitorFromWindow(clock, MONITOR_DEFAULTTOPRIMARY)};
    EnumDisplayMonitors(nullptr, nullptr, coverMonitor, (LPARAM)&c);
    SetTimer(clock, kTimerId, 250, nullptr);
    if (HANDLE t = CreateThread(nullptr, 0, watchdogProc, nullptr, 0, nullptr)) CloseHandle(t);
    g_startUnbiased = unbiasedMs();
    g_sleepMs = (ULONGLONG)sleepMinutesFromRegistry() * 60000ull;
#ifdef MERIDIAN_DEV
    if (const char* t = std::getenv("MERIDIAN_TEST_SLEEP_SECONDS")) g_sleepMs = (ULONGLONG)std::max(0, std::atoi(t)) * 1000ull;     // tests: seconds instead of minutes
#endif
}

bool sleepTick(WPARAM timerId) {
    if (timerId != kTimerId || g_sleepMs == 0 || g_closing) return false;
    if (unbiasedMs() - g_startUnbiased < g_sleepMs) return false;
    g_closing = true;
    g_sleepNow = true;
    return true;
}

void finish() {
    if (!g_sleepNow) return;
    g_sleepNow = false;
    goToSleep();
}

bool isInput(UINT msg, WPARAM wp, LPARAM lp) {
    if (g_closing) return false;
    if (!inputMessage(msg, wp, lp)) return false;
    g_closing = true;
    return true;
}

void configure(HWND parent) {
    const int pick = MessageBoxW(parent,
        L"The Meridian screen saver shows Meridian just as you left it: the same tab, theme, place and weather.\n\n"
        L"To change any of that, open Meridian, change it there and close it again. How long Windows waits before starting the screen "
        L"saver is set in Windows (Settings > Personalization > Lock screen > Screen saver settings).\n\n"
        L"Open Meridian now?",
        L"Meridian screen saver", MB_OKCANCEL | MB_ICONINFORMATION);
    if (pick != IDOK) return;
    std::wstring target = exeFolder() + L"\\Meridian.exe";
    if (GetFileAttributesW(target.c_str()) == INVALID_FILE_ATTRIBUTES) {
        wchar_t exe[MAX_PATH * 2];
        const DWORD n = GetModuleFileNameW(nullptr, exe, (DWORD)(sizeof exe / sizeof exe[0]));
        if (n == 0 || n >= sizeof exe / sizeof exe[0]) return;
        target.assign(exe, n);
    }
    std::wstring cmd = L"\"" + target + L"\"";                         // launch Meridian.exe (not Meridian.scr) so Start-with-Windows and Away work cleanly
    STARTUPINFOW si;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof pi);
    if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
}

Away readAway() {
    Away a;
    const std::wstring scr = exeFolder() + L"\\Meridian.scr";
    std::wstring cur, act, wait;
    regGetStr(kDesktopKey, L"SCRNSAVE.EXE", cur);
    regGetStr(kDesktopKey, L"ScreenSaveActive", act);
    regGetStr(kDesktopKey, L"ScreenSaveTimeOut", wait);
    a.on = !cur.empty() && _wcsicmp(cur.c_str(), scr.c_str()) == 0 && act != L"0";
    const int m = _wtoi(wait.c_str()) / 60;
    if (m >= 1) a.minutes = m;
    a.sleepMinutes = sleepMinutesFromRegistry();
    return a;
}

void setAwayMinutes(int minutes) {
    minutes = std::max(kAwayMinMinutes, std::min(kAwayMaxMinutes, minutes));
    if (!SystemParametersInfoW(SPI_SETSCREENSAVETIMEOUT, (UINT)(minutes * 60), nullptr, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE))
        regSetStr(kDesktopKey, L"ScreenSaveTimeOut", std::to_wstring(minutes * 60));
}

void setSleepAfter(int minutes) {
    HKEY k;
    if (minutes <= 0) { regDelValue(kSaverKey, L"SleepAfterMinutes"); return; }
    minutes = std::max(kSleepMinMinutes, std::min(kSleepMaxMinutes, minutes));
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kSaverKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    const DWORD v = (DWORD)minutes;
    RegSetValueExW(k, L"SleepAfterMinutes", 0, REG_DWORD, (const BYTE*)&v, sizeof v);
    RegCloseKey(k);
}

bool setAway(bool on, int minutes, std::wstring* problem) {
    const std::wstring dir = exeFolder(), scr = dir + L"\\Meridian.scr";
    if (dir.empty()) { if (problem) *problem = L"Meridian could not find its own folder."; return false; }
    std::wstring cur;
    DWORD curType = REG_SZ;
    regGetStr(kDesktopKey, L"SCRNSAVE.EXE", cur, &curType);
    if (on) {
        if (!makeScr(exeFile(), scr, problem)) return false;
        const size_t slash = cur.find_last_of(L"\\/");
        const std::wstring curName = slash == std::wstring::npos ? cur : cur.substr(slash + 1);
        if (_wcsicmp(curName.c_str(), L"Meridian.scr") != 0) {            // what was there before is remembered (an earlier Meridian setting is not "before")
            std::wstring act, wait;
            regGetStr(kDesktopKey, L"ScreenSaveActive", act);
            regGetStr(kDesktopKey, L"ScreenSaveTimeOut", wait);
            RegDeleteTreeW(HKEY_CURRENT_USER, kBackupKey);
            regSetStr(kBackupKey, L"Saved", L"1");
            if (!cur.empty()) { regSetStr(kBackupKey, L"Scr", cur); regSetStr(kBackupKey, L"ScrType", curType == REG_EXPAND_SZ ? L"REG_EXPAND_SZ" : L"REG_SZ"); }   // (a saver stored with %SystemRoot% goes back the same way)
            if (!act.empty()) regSetStr(kBackupKey, L"Active", act);
            if (!wait.empty()) regSetStr(kBackupKey, L"Wait", wait);
        }
        regSetStr(kDesktopKey, L"SCRNSAVE.EXE", scr);
        regSetStr(kDesktopKey, L"ScreenSaveActive", L"1");
        setAwayMinutes(minutes);
        refreshWindowsSaver();
        return true;
    }
    if (_wcsicmp(cur.c_str(), scr.c_str()) == 0) {                          // only while Windows still points at this copy of Meridian.scr
        std::wstring saved, was, wasType, act, wait;
        regGetStr(kBackupKey, L"Saved", saved);
        if (saved == L"1") {
            regGetStr(kBackupKey, L"Scr", was);
            regGetStr(kBackupKey, L"ScrType", wasType);
            regGetStr(kBackupKey, L"Active", act);
            regGetStr(kBackupKey, L"Wait", wait);
            if (!was.empty()) regSetStr(kDesktopKey, L"SCRNSAVE.EXE", was, wasType == L"REG_EXPAND_SZ" ? REG_EXPAND_SZ : REG_SZ); else regDelValue(kDesktopKey, L"SCRNSAVE.EXE");
            regSetStr(kDesktopKey, L"ScreenSaveActive", act.empty() ? L"0" : act);
            if (!wait.empty()) regSetStr(kDesktopKey, L"ScreenSaveTimeOut", wait);
        } else {
            regDelValue(kDesktopKey, L"SCRNSAVE.EXE");
            regSetStr(kDesktopKey, L"ScreenSaveActive", L"0");
        }
        refreshWindowsSaver();
        RegDeleteTreeW(HKEY_CURRENT_USER, kBackupKey);
    }
    return true;
}

} // namespace saver
