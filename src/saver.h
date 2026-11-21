// Meridian as a Windows screen saver.
//
// Windows starts a screen saver by running a ".scr" file with a switch: "/s" to show it, "/c" to configure it, "/p <window>" to
// draw the tiny preview in the Screen Saver Settings box. Meridian.scr is simply a copy of Meridian.exe (Install-ScreenSaver.bat
// makes it), so this file is the only place that knows about any of that. The normal app never gets here: with no switch, nothing
// in this file runs, and Meridian behaves exactly as it always did.
//
// In saver mode the clock is shown as you left it (same tab, theme, place), full screen on the main monitor, with the pointer
// hidden and any other monitor blacked out. Any key, click, wheel turn or real mouse movement closes it. It never writes your
// settings and it does not hold the screen awake, so Windows' own power-off timer still works.
#pragma once
#include <windows.h>

namespace saver {

enum class Mode {
    Normal,       // no switch: the ordinary Meridian window
    Run,          // /s : show the screen saver
    Preview,      // /p : the little preview box (left blank on purpose; nothing to leak if Windows forgets to close it)
    Configure,    // /c : "Settings..." button
    Nothing,      // /a (old "change password" switch)
};

// Reads the command line. For /c the owner window Windows passed (if any) is returned in *parent.
Mode modeFromCommandLine(HWND* parent);

// false when a Meridian screen saver is already on the screen (Windows asks for one at a time)
bool oneAtATime();

// Call once the clock window exists and covers the main monitor: remembers where the pointer is, blacks out the other monitors
// and starts a quarter-second timer on 'clock' (WM_TIMER) that notices keyboard or mouse use even when focus went elsewhere.
void begin(HINSTANCE inst, HWND clock);

// Is this message the user wanting the screen back? (Also true for the timer tick when Windows says there was input.)
bool isInput(UINT msg, WPARAM wp, LPARAM lp);

// Posted to the clock window by the black windows on the other monitors when they see the keyboard or mouse used: "start fading out" (the same as isInput
// turning true on the clock window itself).
constexpr UINT kLeaveMessage = 0x8000 + 0x5A;       // WM_APP + 0x5A

// "Settings..." in Screen Saver Settings: say where the settings live, then open Meridian itself.
void configure(HWND parent);

// ---- sleep: the screen saver can put the PC to sleep when nobody comes back
// If a sleep time is set (below), the saver counts the minutes it has been on the screen. Nobody can have used the PC in that time (any use
// closes the saver), so when the time is up it hides itself and the PC goes to sleep. The count does not run while the PC is asleep.
//   sleepTick():  call for every WM_TIMER the saver window gets; true when the time is up (the caller then closes the window as usual)
//   finish():     call once the saver window is gone, just before the process ends; this is where the PC actually goes to sleep
bool sleepTick(WPARAM timerId);
void finish();

// ---- the "Away" options, as the setup and Settings > Away set them
// "Turn Meridian on when the PC is not used" is Windows' own screen saver, set to Meridian.scr (a copy of Meridian.exe next to it) with a
// wait of 1 to 60 minutes; "then put the PC to sleep" is 20 to 120 more minutes. Windows keeps the first in its Control Panel\Desktop
// registry key; the second is kept beside it, in HKEY_CURRENT_USER\Software\Meridian\Saver (SleepAfterMinutes, 0 or missing = off).
const int kAwayMinMinutes = 1, kAwayMaxMinutes = 60;
const int kSleepMinMinutes = 20, kSleepMaxMinutes = 120;

struct Away {
    bool on = false;                // Windows' screen saver is this Meridian (Meridian.scr beside this Meridian.exe) and switched on
    int minutes = 10;               // how long Windows waits first (as it is set in Windows)
    int sleepMinutes = 0;           // how long Meridian has been on when the PC goes to sleep; 0 = never
};
Away readAway();
// Same steps as Install-ScreenSaver.bat / Remove-ScreenSaver.bat (the earlier screen saver is remembered and put back). On false, *problem says why.
bool setAway(bool on, int minutes, std::wstring* problem);
void setAwayMinutes(int minutes);   // only the wait: applies at once, no restart of anything
void setSleepAfter(int minutes);    // 0 = off

} // namespace saver
