#!/usr/bin/env bash
# Smoke test for the Windows-only part of Meridian, run under Wine on Linux (no Windows PC needed).
# It builds the test build (MERIDIAN_DEV=1, the one with the --shot switch), then draws every face, every theme, every settings page and the
# holiday screens off-screen, and checks that each picture really got drawn (right size, not blank).
# This catches a crash, a missing font or resource, and a face that draws nothing. It does NOT judge how things look, and Wine is not Windows:
# real Windows is still needed for the screen saver (Wine only checks that it ends), sleep, the real window, and the GPU.
#   Needs: wine (wine64), mingw-w64, ImageMagick (identify), python3.   Run from the source folder:  bash tests/smoke_wine.sh
set -euo pipefail
cd "$(dirname "$0")/.."
SRC=$PWD
WINE=${WINE:-$(command -v wine64 || command -v wine || true)}
[ -n "$WINE" ] || { echo "smoke_wine: wine is not installed, skipping"; exit 0; }
command -v identify >/dev/null || { echo "smoke_wine: ImageMagick (identify) is not installed"; exit 1; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
export WINEDEBUG=-all WINEPREFIX=${WINEPREFIX:-$T/prefix}
if [ -z "${DISPLAY:-}" ] && command -v xvfb-run >/dev/null; then exec xvfb-run -a bash "$0" "$@"; fi
echo "== building the test build"
cp -a "$SRC" "$T/m"; rm -rf "$T/m/build" "$T/m/build-dev" "$T/m/setup/build"
( cd "$T/m" && MERIDIAN_DEV=1 bash ./build.sh >/dev/null )
EXE="$T/m/build-dev/Meridian-dev.exe"
mkdir -p "$T/out"
win() { "$WINE" winepath -w "$1" 2>/dev/null | tr -d '\r'; }
fails=0
# shot NAME EXPECTED-WxH MIN-COLOURS args...
shot() {
  local name=$1 size=$2 minc=$3; shift 3
  local png="$T/out/$name.png"
  if ! timeout 120 "$WINE" "$EXE" --shot "$(win "$png")" --size "$size" "$@" >/dev/null 2>&1; then
    echo "FAIL $name: process exited with non-zero status"; fails=$((fails+1)); return
  fi
  if [ ! -s "$png" ]; then echo "FAIL $name: nothing was drawn"; fails=$((fails+1)); return; fi
  local got colours; got=$(identify -format '%wx%h' "$png"); colours=$(identify -format '%k' "$png")
  if [ "$got" != "$size" ]; then echo "FAIL $name: picture is $got, wanted $size"; fails=$((fails+1)); return; fi
  if [ "$colours" -lt "$minc" ]; then echo "FAIL $name: only $colours colours, looks blank"; fails=$((fails+1)); return; fi
  echo "ok   $name ($colours colours)"
}
echo "== faces x themes"
for face in 0 1 2 3 4 5; do for theme in 1 7; do shot "face$face-theme$theme" 1280x720 300 --face $face --theme $theme --time "2026-11-12 20:15:00" --tz 5:30; done; done
for theme in 0 2 3 4 5 6 8 9 10; do shot "sky-theme$theme" 1280x720 300 --face 0 --theme $theme --time "2026-11-12 20:15:00" --tz 5:30; done
echo "== settings pages"
for tab in 0 1 2 3 4 5 6 7 8 9 10 11; do shot "settings$tab" 1280x720 300 --settings --tab $tab --theme 7 --time "2026-11-12 20:15:00" --tz 5:30; done
echo "== holidays.txt"
printf '# test\n2028-01-12 duruthu\n2028-02-30 bak\nnonsense\n' > "$T/hol.txt"
shot "holidays-bad-lines" 1280x720 300 --settings --tab 9 --holidays "$(win "$T/hol.txt")" --theme 7
shot "calendar-month" 1280x720 300 --face 4 --calmonth 1 --holidays "$(win "$T/hol.txt")" --theme 1
echo "== screen saver exit (needs xdotool; the limits are loose because Wine is slower than Windows)"
if command -v xdotool >/dev/null && [ -n "${DISPLAY:-}" ]; then
  # saverexit NAME VAR=VALUE: the saver is started, the mouse is moved after 5 s, and the process must be gone 4 s later.
  # The test build has two switches for this: MERIDIAN_TEST_NOFADE=1 (no picture to fade) and MERIDIAN_TEST_STALL_MS=20000 (the main thread
  # is stuck for 20 s: only the watchdog in saver.cpp can end the process in time).
  saverexit() {
    local name=$1; shift
    env "$@" "$WINE" "$EXE" /s >/dev/null 2>&1 & local wp=$!
    sleep 5; xdotool mousemove 300 300; sleep 0.3; xdotool mousemove 700 500
    local i; for i in $(seq 1 40); do kill -0 $wp 2>/dev/null || break; sleep 0.1; done
    if kill -0 $wp 2>/dev/null; then echo "FAIL $name: the saver was still on the screen 4 s after the mouse moved"; kill $wp 2>/dev/null || true; fails=$((fails+1))
    else echo "ok   $name"; fi
    wait $wp 2>/dev/null || true
  }
  saverexit "saver-exit-fade" SAVER_TEST=1
  saverexit "saver-exit-no-picture" MERIDIAN_TEST_NOFADE=1
  saverexit "saver-exit-stuck-main-thread" MERIDIAN_TEST_STALL_MS=20000
else echo "skipped (xdotool or a display is missing)"; fi
if [ "$fails" -ne 0 ]; then echo "smoke_wine: $fails check(s) FAILED"; exit 1; fi
echo "smoke_wine: all checks passed"
