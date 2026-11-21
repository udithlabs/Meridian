// What the setup does to the computer: copy the files, make the shortcuts, write the registry entries, and undo all of it again.
// No window code in here, so the same functions serve the wizard, the silent mode (/S) and the uninstaller.
#pragma once
#include <windows.h>
#include <functional>
#include <string>
#include "payload.h"

namespace inst {

struct Options {
    std::wstring dir;
    bool startMenu = true, desktop = true, runAtLogon = false, saver = false, gta = true, halloween = true;
    int saverMinutes = 10;          // Meridian turns on after this many minutes of no use (1-60); needs `saver`
    bool sleep = false;             // then put the PC to sleep ...
    int sleepMinutes = 60;          // ... after Meridian has been on this many more minutes (20-120); needs `saver`
};

constexpr int kAwayMin = 1, kAwayMax = 60, kSleepMin = 20, kSleepMax = 120;

// What is on the computer already (an older Meridian, from this setup or from the 2.6.x one).
struct Existing {
    bool found = false;
    std::wstring dir, version;
    bool startMenu = false, desktop = false, runAtLogon = false, saver = false, gta = false, halloween = false;
    int saverMinutes = 10;
    bool sleep = false;
    int sleepMinutes = 60;
};

using Progress = std::function<void(double fraction, const std::wstring& text)>;

std::wstring defaultDir();                                  // %LOCALAPPDATA%\Programs\Meridian
Existing detect();
// Is this folder a Meridian install, even a half-removed one (Meridian.exe gone, but Meridian.scr or uninstall.exe left and Windows still listing it)?
// The uninstaller uses it so that "try again" works after an uninstall that could not remove everything.
bool looksInstalled(const std::wstring& dir);
// An empty string means success; otherwise one plain sentence about what went wrong. `stillRunning` is set when the only problem is that
// Meridian would not close (the wizard then offers Retry).
std::wstring install(const Payload& pl, const Options& o, const Progress& pr, bool* stillRunning = nullptr);
// When deferSelfUninstall is true, leaves <dir>\uninstall.exe in place so the parent silent uninstaller can receive the real exit code first;
// call finishDeferredUninstall(dir) after the parent process exits.
std::wstring uninstall(const Payload& pl, const std::wstring& dir, bool deleteSettings, const Progress& pr, bool* stillRunning = nullptr, bool deferSelfUninstall = false);
void finishDeferredUninstall(const std::wstring& dir);
// Transactional file swap with backup and full rollback on failure (used by install() and unit-tested directly).
std::wstring commitStagedFiles(const std::wstring& dir, const std::wstring& stageDir, const std::vector<std::wstring>& relFiles);

std::wstring settingsFolder(bool local);                    // %APPDATA%\Meridian or %LOCALAPPDATA%\Meridian
std::wstring widen(const std::string& utf8);
std::wstring exeName(const std::wstring& path);
bool pathsEqual(const std::wstring& a, const std::wstring& b);
bool dirExists(const std::wstring& p);
bool fileExists(const std::wstring& p);
std::wstring parentDir(const std::wstring& p);
bool isMeridianRunning();

} // namespace inst
