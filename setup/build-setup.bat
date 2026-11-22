@echo off
rem Build Meridian-Setup-2.9.9.exe on Windows (64-bit, one file).
rem Needs what build.bat needs (MinGW-w64: g++ and windres on PATH) plus Python 3 (the "python" command) to pack the files.
rem Build Meridian.exe first (..\build.bat): the setup carries the folder above this one (Meridian.exe, README.txt, themes\, licenses\).
rem   build-setup.bat [app-folder] [licenses-folder]
setlocal
cd /d "%~dp0"
set APP=%~1
if "%APP%"=="" set APP=..
set LIC=%~2
if "%LIC%"=="" set LIC=%APP%\licenses
set BUILD=build
set FLAGS=-std=c++17 -O2 -ffile-prefix-map=%CD%=. -Wall -Wextra -Wno-unused-parameter -Wno-delete-non-virtual-dtor -Wno-misleading-indentation -Wno-missing-field-initializers -Wno-cast-function-type -DUNICODE -D_UNICODE -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00
rem (the same warnings as build.sh. Set MERIDIAN_WERROR=1 first to make any warning an error.)
if defined MERIDIAN_WERROR set FLAGS=%FLAGS% -Werror
if not exist %BUILD% mkdir %BUILD%
echo PACK payload
python pack.py "%APP%" "%LIC%" ..\src\scene_settings.cpp %BUILD% || exit /b 1
set OBJS=
for %%f in (astro moonsprite gfx) do (
  echo CXX %%f.cpp
  g++ %FLAGS% -c ..\src\%%f.cpp -o %BUILD%\%%f.o || exit /b 1
  call set OBJS=%%OBJS%% %BUILD%\%%f.o
)
for %%f in (install song setup_main) do (
  echo CXX %%f.cpp
  g++ %FLAGS% -c %%f.cpp -o %BUILD%\%%f.o || exit /b 1
  call set OBJS=%%OBJS%% %BUILD%\%%f.o
)
echo RC setup.rc
copy /y %BUILD%\payload.bin . >nul
copy /y %BUILD%\notes.txt . >nul
windres -O coff -I . -I ..\src setup.rc %BUILD%\setup_rc.o || exit /b 1
del payload.bin notes.txt
echo LD Meridian-Setup-2.9.9.exe
g++ -static -mwindows -O2 -o Meridian-Setup-2.9.9.exe %OBJS% %BUILD%\setup_rc.o -ld2d1 -ldwrite -lwindowscodecs -ldwmapi -lole32 -loleaut32 -luuid -lshell32 -lgdi32 -luser32 -ladvapi32 -lwinmm || exit /b 1
strip --strip-all Meridian-Setup-2.9.9.exe
echo Done: Meridian-Setup-2.9.9.exe
