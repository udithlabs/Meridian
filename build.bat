@echo off
rem Build Meridian.exe on Windows with MinGW-w64 (g++ and windres must be on PATH,
rem e.g. open the "MSYS2 MINGW64" shell, or install winlibs.com and add its bin folder to PATH).
setlocal
set BUILD=build
set OUT=Meridian.exe
rem The same warnings as build.sh, so a problem shows up whichever way you build. Set MERIDIAN_WERROR=1 first to make any warning an error.
set FLAGS=-std=c++17 -O2 -ffile-prefix-map=%CD%=. -Wall -Wextra -Wno-unused-parameter -Wno-delete-non-virtual-dtor -Wno-misleading-indentation -Wno-missing-field-initializers -DUNICODE -D_UNICODE -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00
if defined MERIDIAN_WERROR set FLAGS=%FLAGS% -Werror
rem Test build (screenshot / benchmark switches, test hooks): set MERIDIAN_DEV=1 before running this file. Writes build-dev\Meridian-dev.exe so release Meridian.exe is never overwritten.
if defined MERIDIAN_DEV set FLAGS=%FLAGS% -DMERIDIAN_DEV& set BUILD=build-dev& set OUT=build-dev\Meridian-dev.exe
if not exist %BUILD% mkdir %BUILD%
rem (every file is compiled again on each run, so objects made with other flags are never reused; the test build has its own folder anyway)
set OBJS=
for %%f in (astro skyevents holidays today weather storms spacewx locate robust saver moonsprite gfx app scene_common scene_events scene_widgets scene_settings scene_panel scene_quiet scene_space ufo_audio expr themes scene_theme scene scene_faces main) do (
  echo CXX %%f.cpp
  g++ %FLAGS% -c src\%%f.cpp -o %BUILD%\%%f.o || exit /b 1
  call set OBJS=%%OBJS%% %BUILD%\%%f.o
)
echo RC app.rc
pushd src
windres -O coff -I . app.rc ..\%BUILD%\app_rc.o || exit /b 1
popd
echo LD %OUT%
g++ -static -mwindows -O2 -o %OUT% %OBJS% %BUILD%\app_rc.o -ld2d1 -ldwrite -lwindowscodecs -lwinhttp -ldwmapi -lole32 -loleaut32 -luuid -lwinmm -lshell32 -lgdi32 -luser32 -ladvapi32 || exit /b 1
strip --strip-all %OUT%
echo Done: %OUT%
