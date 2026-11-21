#!/usr/bin/env bash
# Cross-compile Meridian.exe on Linux with mingw-w64 (static, single file).
set -euo pipefail
cd "$(dirname "$0")"
CXX=${CXX:-x86_64-w64-mingw32-g++-posix}
RC=${RC:-x86_64-w64-mingw32-windres}
FLAGS="-std=c++17 -O2 -ffile-prefix-map=$(pwd)=. -Wall -Wextra -Wno-unused-parameter -Wno-delete-non-virtual-dtor -Wno-misleading-indentation -Wno-missing-field-initializers -DUNICODE -D_UNICODE -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00"
# MERIDIAN_WERROR=1 ./build.sh makes any compiler warning an error (the automatic checks in .github/workflows/build.yml do this).
if [ -n "${MERIDIAN_WERROR:-}" ]; then FLAGS="$FLAGS -Werror"; fi
# Test build (screenshot / benchmark switches, test hooks):  MERIDIAN_DEV=1 ./build.sh   Writes build-dev/Meridian-dev.exe so release Meridian.exe is never overwritten.
B=build
OUT=Meridian.exe
if [ -n "${MERIDIAN_DEV:-}" ]; then FLAGS="$FLAGS -DMERIDIAN_DEV"; B=build-dev; OUT=build-dev/Meridian-dev.exe; fi
mkdir -p "$B"
# object files made with other flags, or another compiler, are never reused
FP="$CXX $FLAGS"
if [ "$(cat "$B/flags.txt" 2>/dev/null || true)" != "$FP" ]; then rm -f "$B"/*.o; printf '%s' "$FP" > "$B/flags.txt"; fi
OBJS=()
for f in astro skyevents holidays today weather storms locate robust saver moonsprite gfx app scene_common scene_events scene_widgets scene_settings scene_panel scene_quiet scene_space ufo_audio expr themes scene_theme scene scene_faces main; do
  if [ ! -f $B/$f.o ] || [ src/$f.cpp -nt $B/$f.o ] || [ -n "$(find src -name '*.h' -newer $B/$f.o -print -quit)" ]; then
    echo "  CXX $f.cpp"; $CXX $FLAGS -c src/$f.cpp -o $B/$f.o
  fi
  OBJS+=($B/$f.o)
done
if [ ! -f $B/app_rc.o ] || [ -n "$(find assets src/app.rc src/resource.h -newer $B/app_rc.o -type f -print -quit)" ]; then
  echo "  RC  app.rc"; (cd src && $RC -O coff -I . app.rc ../$B/app_rc.o)
fi
echo "  LD  $OUT"
$CXX -static -mwindows -O2 -o "$OUT" "${OBJS[@]}" $B/app_rc.o \
  -ld2d1 -ldwrite -lwindowscodecs -lwinhttp -ldwmapi -lole32 -loleaut32 -luuid -lwinmm -lshell32 -lgdi32 -luser32 -ladvapi32
x86_64-w64-mingw32-strip --strip-all "$OUT" 2>/dev/null || true
ls -la "$OUT"
