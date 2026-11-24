#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <dbghelp.h>          // types only: dbghelp.dll itself is loaded when a crash happens, so nothing is linked or kept in memory
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include "robust.h"

namespace robust {

// ---------------------------------------------------------------- files
bool atomicWrite(const std::wstring& path, const std::string& data) {
    const std::wstring tmp = path + L".tmp";
    bool ok = false;
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD put = 0;
        ok = (data.empty() || WriteFile(h, data.data(), (DWORD)data.size(), &put, nullptr)) && put == (DWORD)data.size() && FlushFileBuffers(h);
        CloseHandle(h);
        if (ok) ok = MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
        if (!ok) DeleteFileW(tmp.c_str());
    }
    if (ok) return true;
    // the folder does not allow a swap (some network shares do not): write in place, as Meridian always used to. Opening a file for writing
    // empties it, so keep what was in it, and put that back if this write does not go through (disk full, the file taken away ...).
    std::string old;
    bool existed = false, haveOld = false;
    {
        HANDLE r = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (r != INVALID_HANDLE_VALUE) {
            existed = true;
            LARGE_INTEGER sz;
            if (GetFileSizeEx(r, &sz) && sz.QuadPart >= 0 && sz.QuadPart <= (64LL << 20)) {
                old.assign((size_t)sz.QuadPart, '\0');
                DWORD got = 0;
                haveOld = sz.QuadPart == 0 || (ReadFile(r, &old[0], (DWORD)sz.QuadPart, &got, nullptr) && got == (DWORD)sz.QuadPart);
            }
            CloseHandle(r);
        }
    }
    h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;                       // (nothing was opened for writing, so nothing was emptied)
    DWORD put = 0;
    ok = (data.empty() || WriteFile(h, data.data(), (DWORD)data.size(), &put, nullptr)) && put == (DWORD)data.size() && FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok) {
        if (existed && haveOld) {
            HANDLE w = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (w != INVALID_HANDLE_VALUE) { DWORD back = 0; if (!old.empty()) WriteFile(w, old.data(), (DWORD)old.size(), &back, nullptr); CloseHandle(w); }
        } else if (!existed) DeleteFileW(path.c_str());                // a half-written file that did not exist before is not worth keeping
    }
    return ok;
}

void appendLog(const std::wstring& path, const std::string& text, unsigned maxBytes, unsigned keepBytes) {
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa) && fa.nFileSizeHigh == 0 && fa.nFileSizeLow > maxBytes) {
        HANDLE r = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (r != INVALID_HANDLE_VALUE) {
            std::string tail(keepBytes, '\0');
            DWORD got = 0;
            LARGE_INTEGER pos;
            pos.QuadPart = (LONGLONG)fa.nFileSizeLow - (LONGLONG)keepBytes;
            if (pos.QuadPart < 0) pos.QuadPart = 0;
            const bool read = SetFilePointerEx(r, pos, nullptr, FILE_BEGIN) && ReadFile(r, &tail[0], keepBytes, &got, nullptr);
            CloseHandle(r);
            if (read) {
                tail.resize(got);
                const size_t nl = tail.find('\n');
                if (nl != std::string::npos) tail.erase(0, nl + 1);        // start on a whole line
                atomicWrite(path, tail);
            }
        }
    }
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD put = 0;
    WriteFile(h, text.data(), (DWORD)text.size(), &put, nullptr);
    CloseHandle(h);
}

// ---------------------------------------------------------------- one copy at a time
static HANDLE g_instance = nullptr;

bool becomeSingleInstance(const wchar_t* windowClass, bool patient) {
    for (int i = 0;; ++i) {
        HANDLE h = CreateMutexW(nullptr, FALSE, L"Local\\Meridian.AlwaysOnClock");
        if (!h) return true;                                           // cannot tell: better two clocks than none
        if (GetLastError() != ERROR_ALREADY_EXISTS) { g_instance = h; return true; }
        CloseHandle(h);
        if (!patient || i >= 80) break;
        Sleep(100);                                                    // the crashed copy is still letting go
    }
    HWND other = FindWindowW(windowClass, nullptr);
    if (other) {
        if (IsIconic(other)) ShowWindow(other, SW_RESTORE);
        SetForegroundWindow(other);
    }
    return false;
}

// ---------------------------------------------------------------- crash note and restart
static wchar_t g_crashLog[MAX_PATH * 2];
static wchar_t g_exePath[MAX_PATH * 2];
static const char* g_version = "";
static int g_restartCount = 0;
static bool g_allowRestart = true;
static ULONGLONG g_startTick = 0;
static wchar_t g_dumpDir[MAX_PATH * 2];

// ---- crash dump. MiniDumpWriteDump runs on a thread of its own: when the crash was a stack overflow the crashed thread has no stack left to do it on.
struct DumpJob { EXCEPTION_POINTERS* ep; DWORD thread; const wchar_t* path; bool ok; };

static DWORD WINAPI dumpThread(LPVOID p) {
    DumpJob* j = (DumpJob*)p;
    typedef BOOL (WINAPI *WriteFn)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
    HMODULE dh = LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    WriteFn write = dh ? (WriteFn)(void*)GetProcAddress(dh, "MiniDumpWriteDump") : nullptr;
    if (write) {
        HANDLE f = CreateFileW(j->path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mei;
            mei.ThreadId = j->thread; mei.ExceptionPointers = j->ep; mei.ClientPointers = FALSE;
            j->ok = write(GetCurrentProcess(), GetCurrentProcessId(), f, MiniDumpNormal, j->ep ? &mei : nullptr, nullptr, nullptr) != FALSE;
            CloseHandle(f);
            if (!j->ok) DeleteFileW(j->path);
        }
    }
    if (dh) FreeLibrary(dh);
    return 0;
}

// Keep the newest 'keep' dumps in the folder (their names start with the date, so the newest sort last). Called at start-up, never from the crash handler.
static void pruneDumps(const wchar_t* dir, size_t keep) {
    std::vector<std::wstring> names;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((std::wstring(dir) + L"\\Meridian-*.dmp").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) names.push_back(fd.cFileName); } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(names.begin(), names.end());
    for (size_t i = 0; i + keep < names.size(); ++i) DeleteFileW((std::wstring(dir) + L"\\" + names[i]).c_str());
}

static LONG WINAPI crashFilter(EXCEPTION_POINTERS* ep) {
    static volatile LONG busy = 0;
    if (InterlockedExchange(&busy, 1)) return EXCEPTION_CONTINUE_SEARCH;
    // keep this small and allocation-free: the process is already in trouble
    const DWORD code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
    const ULONG_PTR addr = ep && ep->ExceptionRecord ? (ULONG_PTR)ep->ExceptionRecord->ExceptionAddress : 0;
    const ULONG_PTR base = (ULONG_PTR)GetModuleHandleW(nullptr);
    SYSTEMTIME t;
    GetLocalTime(&t);
    // 1. the note (before anything that could take long or fail), 2. the crash dump, 3. a second line saying the dump was saved, 4. the restart
    auto note = [](const char* text, int len) {
        if (!g_crashLog[0] || len <= 0) return;
        HANDLE h = CreateFileW(g_crashLog, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) { DWORD put = 0; WriteFile(h, text, (DWORD)len, &put, nullptr); CloseHandle(h); }
    };
    char line[420];
    const unsigned long long off = (addr >= base) ? (unsigned long long)(addr - base) : (unsigned long long)addr;
    const int n = std::snprintf(line, sizeof line, "%04d-%02d-%02d %02d:%02d:%02d  crash %08lX at Meridian.exe+0x%llX  (version %s, up %lu s, restart %d)\r\n",
                                (int)t.wYear, (int)t.wMonth, (int)t.wDay, (int)t.wHour, (int)t.wMinute, (int)t.wSecond,
                                (unsigned long)code, off, g_version,
                                (unsigned long)((GetTickCount64() - g_startTick) / 1000ULL), g_restartCount);
    note(line, n > 0 ? std::min(n, (int)sizeof line - 1) : 0);
    if (g_dumpDir[0]) {
        // static storage: if dumpThread outlives the 15s wait, it never reads a destroyed stack frame
        static wchar_t path[MAX_PATH * 2 + 48];
        static DumpJob job;
        _snwprintf(path, sizeof(path) / sizeof(path[0]) - 1, L"%ls\\Meridian-%04d%02d%02d-%02d%02d%02d.dmp", g_dumpDir, (int)t.wYear, (int)t.wMonth, (int)t.wDay, (int)t.wHour, (int)t.wMinute, (int)t.wSecond);
        path[sizeof(path) / sizeof(path[0]) - 1] = L'\0';
        CreateDirectoryW(g_dumpDir, nullptr);
        job = {ep, GetCurrentThreadId(), path, false};
        HANDLE th = CreateThread(nullptr, 1 << 20, dumpThread, &job, 0, nullptr);
        if (th) {
            // a dump takes well under a second; if it has not finished in 15 seconds, leave it and carry on with the restart
            const bool done = WaitForSingleObject(th, 15000) == WAIT_OBJECT_0;
            CloseHandle(th);
            if (done && job.ok) {
                const int m = std::snprintf(line, sizeof line, "%04d-%02d-%02d %02d:%02d:%02d  saved crash dump Meridian-%04d%02d%02d-%02d%02d%02d.dmp\r\n",
                                            (int)t.wYear, (int)t.wMonth, (int)t.wDay, (int)t.wHour, (int)t.wMinute, (int)t.wSecond,
                                            (int)t.wYear, (int)t.wMonth, (int)t.wDay, (int)t.wHour, (int)t.wMinute, (int)t.wSecond);
                note(line, m > 0 ? std::min(m, (int)sizeof line - 1) : 0);
            }
        }
    }
    if (g_allowRestart && g_restartCount < 3 && g_exePath[0]) {
        wchar_t cmd[MAX_PATH * 2 + 40];
        _snwprintf(cmd, sizeof(cmd) / sizeof(cmd[0]) - 1, L"\"%ls\" --restarted %d", g_exePath, g_restartCount + 1);
        cmd[sizeof(cmd) / sizeof(cmd[0]) - 1] = L'\0';
        STARTUPINFOW si;
        ZeroMemory(&si, sizeof si);
        si.cb = sizeof si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&pi, sizeof pi);
        if (CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
    }
    return EXCEPTION_EXECUTE_HANDLER;                                  // end this copy quietly, without Windows' crash window
}

int restartCountFromCommandLine() {
    int argc = 0, n = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 0;
    for (int i = 1; i + 1 < argc; ++i) if (lstrcmpW(argv[i], L"--restarted") == 0) n = _wtoi(argv[i + 1]);
    LocalFree(argv);
    return n < 0 ? 0 : (n > 9 ? 9 : n);
}

void installCrashHandler(const std::wstring& logPath, const char* version, int restartCount, const std::wstring& dumpDir, bool allowRestart) {
    lstrcpynW(g_crashLog, logPath.c_str(), (int)(sizeof g_crashLog / sizeof g_crashLog[0]));
    lstrcpynW(g_dumpDir, dumpDir.c_str(), (int)(sizeof g_dumpDir / sizeof g_dumpDir[0]));
    if (g_dumpDir[0]) pruneDumps(g_dumpDir, 3);
    GetModuleFileNameW(nullptr, g_exePath, (DWORD)(sizeof g_exePath / sizeof g_exePath[0]));
    g_version = version ? version : "";
    g_restartCount = restartCount;
    g_allowRestart = allowRestart;
    g_startTick = GetTickCount64();
    SetUnhandledExceptionFilter(crashFilter);
}

void noteRunning() {
    if (g_restartCount && GetTickCount64() - g_startTick > 600000ULL) g_restartCount = 0;
}

// ---------------------------------------------------------------- monitors, battery, menus
bool rectReachable(int x, int y, int w, int h) {
    RECT want = {x, y, x + w, y + h};
    HMONITOR mon = MonitorFromRect(&want, MONITOR_DEFAULTTONULL);      // the monitor most of the window is on; none: it is off every screen
    if (!mon) return false;
    MONITORINFO mi;
    ZeroMemory(&mi, sizeof mi);
    mi.cbSize = sizeof mi;
    if (!GetMonitorInfoW(mon, &mi)) return false;
    const RECT& wk = mi.rcWork;
    return x > wk.left - w + 80 && x < wk.right - 80 && y > wk.top - 20 && y < wk.bottom - 80;     // enough of the title bar to grab it
}

bool onBattery() {
    SYSTEM_POWER_STATUS ps;
    return GetSystemPowerStatus(&ps) && ps.ACLineStatus == 0;          // 1 = plugged in, 255 = unknown: only 0 counts
}

static const wchar_t* const kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

static std::wstring quotedExePath() {
    wchar_t exe[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(nullptr, exe, (DWORD)(sizeof exe / sizeof exe[0]));
    return L"\"" + std::wstring(exe, n) + L"\"";
}

bool startWithWindows() {
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
    wchar_t buf[MAX_PATH * 2 + 4];
    DWORD type = 0, size = (DWORD)sizeof buf - sizeof(wchar_t);
    const bool got = RegQueryValueExW(k, L"Meridian", nullptr, &type, (LPBYTE)buf, &size) == ERROR_SUCCESS && type == REG_SZ;
    RegCloseKey(k);
    if (!got) return false;
    buf[size / sizeof(wchar_t)] = 0;
    return lstrcmpiW(buf, quotedExePath().c_str()) == 0;
}

bool setStartWithWindows(bool on) {
    HKEY k = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
    bool ok;
    if (on) {
        const std::wstring cmd = quotedExePath();
        ok = RegSetValueExW(k, L"Meridian", 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)((cmd.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    } else {
        const LONG r = RegDeleteValueW(k, L"Meridian");
        ok = r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND;
    }
    RegCloseKey(k);
    return ok;
}

void setMenuDark(bool dark) {
    // uxtheme.dll ordinals 135 (SetPreferredAppMode) and 136 (FlushMenuThemes). They are not documented, but dark-mode apps
    // have used them since Windows 10 1903. If anything is missing, nothing happens and the menu simply stays light.
    typedef LONG (WINAPI * RtlGetVersionFn)(OSVERSIONINFOW*);
    typedef int (WINAPI * SetModeFn)(int);
    typedef void (WINAPI * FlushFn)();
    static bool inited = false, usable = false;
    static SetModeFn setMode = nullptr;
    static FlushFn flush = nullptr;
    if (!inited) {
        inited = true;
        HMODULE nt = GetModuleHandleW(L"ntdll.dll");
        RtlGetVersionFn rtl = nt ? (RtlGetVersionFn)(void*)GetProcAddress(nt, "RtlGetVersion") : nullptr;
        OSVERSIONINFOW vi;
        ZeroMemory(&vi, sizeof vi);
        vi.dwOSVersionInfoSize = sizeof vi;
        const bool new1903 = rtl && rtl(&vi) == 0 && vi.dwBuildNumber >= 18362;
        HMODULE ux = new1903 ? LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32) : nullptr;
        if (ux) {
            setMode = (SetModeFn)(void*)GetProcAddress(ux, MAKEINTRESOURCEA(135));
            flush = (FlushFn)(void*)GetProcAddress(ux, MAKEINTRESOURCEA(136));
            usable = setMode != nullptr;
        }
    }
    if (!usable) return;
    setMode(dark ? 2 : 3);                                             // 2 = always dark, 3 = always light
    if (flush) flush();
}

} // namespace robust
