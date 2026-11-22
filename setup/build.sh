#!/usr/bin/env bash
# Builds Meridian-Setup-<version>.exe (64-bit, one file) on Linux with mingw-w64.
#   ./build.sh [app-folder] [licenses-folder]
# app-folder: where Meridian.exe, README.txt and themes/ are (default: the folder above this one).  licenses-folder: default <app-folder>/licenses.
set -euo pipefail
cd "$(dirname "$0")"
APP=${1:-..}; LIC=${2:-$APP/licenses}
CXX=${CXX:-x86_64-w64-mingw32-g++-posix}
RC=${RC:-x86_64-w64-mingw32-windres}
FLAGS="-std=c++17 -O2 -ffile-prefix-map=$(pwd)=. -Wall -Wextra -Wno-unused-parameter -Wno-delete-non-virtual-dtor -Wno-misleading-indentation -Wno-missing-field-initializers -Wno-cast-function-type -DUNICODE -D_UNICODE -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00"
if [ -n "${MERIDIAN_WERROR:-}" ]; then FLAGS="$FLAGS -Werror"; fi          # MERIDIAN_WERROR=1: any compiler warning is an error
B=build; mkdir -p "$B"
FP="$CXX $FLAGS"
if [ "$(cat "$B/flags.txt" 2>/dev/null || true)" != "$FP" ]; then rm -f "$B"/*.o; printf '%s' "$FP" > "$B/flags.txt"; fi
echo "  PACK payload"; python3 -I pack.py "$APP" "$LIC" ../src/scene_settings.cpp "$B"
obj() { # name source
  if [ ! -f $B/$1.o ] || [ $2 -nt $B/$1.o ] || [ -n "$(find . ../src -name '*.h' -newer $B/$1.o -print -quit)" ]; then echo "  CXX $2"; $CXX $FLAGS -c $2 -o $B/$1.o; fi
}
obj astro ../src/astro.cpp; obj moonsprite ../src/moonsprite.cpp; obj gfx ../src/gfx.cpp
obj install install.cpp; obj song song.cpp; obj setup_main setup_main.cpp
echo "  RC  setup.rc"; cp "$B/payload.bin" "$B/notes.txt" . ; $RC -O coff -I . -I ../src setup.rc $B/setup_rc.o ; rm -f payload.bin notes.txt
OUT=Meridian-Setup-2.9.9.exe
echo "  LD  $OUT"
$CXX -static -mwindows -O2 -o "$OUT" $B/astro.o $B/moonsprite.o $B/gfx.o $B/install.o $B/song.o $B/setup_main.o $B/setup_rc.o \
  -ld2d1 -ldwrite -lwindowscodecs -ldwmapi -lole32 -loleaut32 -luuid -lshell32 -lgdi32 -luser32 -ladvapi32 -lwinmm
x86_64-w64-mingw32-strip --strip-all "$OUT" 2>/dev/null || true
ls -la "$OUT"
