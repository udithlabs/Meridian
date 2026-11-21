// Small Windows helpers that make Meridian safe to leave running for months: files that survive a power cut, one copy at a
// time, a crash note and a restart, a saved window that comes back on the right monitor, battery check, dark menus.
// Everything here is plain Win32 with no other dependencies, so it is easy to read on its own.
#pragma once
#include <string>

namespace robust {

// Write 'data' to 'path' so that a crash or power cut leaves either the old file or the new one, never half of one:
// it is written beside the target and swapped in. Falls back to writing in place where a swap is not allowed.
bool atomicWrite(const std::wstring& path, const std::string& data);

// Append 'text' to a log. Past 'maxBytes' the log is cut down to its newest 'keepBytes' (from a line start) first.
void appendLog(const std::wstring& path, const std::string& text, unsigned maxBytes, unsigned keepBytes);

// true when this is the only Meridian running. If another one is, it is brought to the front and false is returned.
// 'patient': this copy was started by the crash restart, so give the dying copy a few seconds to let go first.
bool becomeSingleInstance(const wchar_t* windowClass, bool patient);

// On a crash: write one line to 'logPath' (what, where, which version), save a small crash dump (a .dmp file with where every thread was,
// openable in Visual Studio or WinDbg) into 'dumpDir' when one is given, and start Meridian again, at most three times in a row (the count is
// passed on the command line as --restarted N, and goes back to zero after ten minutes of running). Only the newest three dumps are kept.
void installCrashHandler(const std::wstring& logPath, const char* version, int restartCount, const std::wstring& dumpDir = L"", bool allowRestart = true);
void noteRunning();            // call every frame: lets the restart counter go back to zero after ten calm minutes
int  restartCountFromCommandLine();

// Is a window at this saved position and size still reachable on one of the connected monitors?
bool rectReachable(int x, int y, int w, int h);

// Running on the battery right now (false on a desktop PC, or when Windows cannot say)?
bool onBattery();

// "Start with Windows": a value in the current user's Run list (no admin rights needed). It only counts as on while it points at
// THIS exe, so after the folder is moved the item shows as off and switching it on again fixes the path.
bool startWithWindows();
bool setStartWithWindows(bool on);

// Make right-click menus dark or light to match the theme (Windows 10 1903 and newer; does nothing on older ones).
void setMenuDark(bool dark);

} // namespace robust
