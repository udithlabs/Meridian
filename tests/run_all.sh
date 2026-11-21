#!/usr/bin/env bash
# Builds and runs every test that needs no Windows (g++ on Linux, macOS or MSYS2). Run from the source folder:  bash tests/run_all.sh
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=${TMPDIR:-/tmp}/meridian-tests; mkdir -p "$OUT"
CXX=${CXX:-g++}
SAN="-fsanitize=address,undefined"
FLAGS="-std=c++17 -Wall -Wextra -Wno-unused-parameter -I src $SAN"
run() { name=$1; shift; echo "== $name"; $CXX $FLAGS "$@" -o "$OUT/$name" && "$OUT/$name"; }
run expr_test      tests/expr_test.cpp src/expr.cpp
run minijson_test  tests/minijson_test.cpp
"$OUT/minijson_test" tests/sample_*.json
run textesc_test   tests/textesc_test.cpp
run textedit_test  tests/textedit_test.cpp
run dates_test     tests/dates_test.cpp src/astro.cpp
run alarm_test     tests/alarm_test.cpp src/astro.cpp
run clockguard_test tests/clockguard_test.cpp
run locate_test    tests/locate_test.cpp src/locate.cpp       # (the parsers only; the part that talks to the network is Windows-only and is not built here)
run holidays_test  tests/holidays_test.cpp src/holidays.cpp src/astro.cpp
run holidays_regions_test tests/holidays_regions_test.cpp src/holidays.cpp src/astro.cpp
run reveal_test    tests/reveal_test.cpp                      # (the screen saver's graceful start: numbers only)
run daily_test     tests/daily_test.cpp
run ufo_audio_test tests/ufo_audio_test.cpp
run regression_292_test tests/regression_292_test.cpp src/astro.cpp src/expr.cpp src/holidays.cpp src/skyevents.cpp src/storms.cpp src/today.cpp src/weather.cpp
run regression_295_test tests/regression_295_test.cpp src/astro.cpp src/storms.cpp src/weather.cpp
run regression_297_test tests/regression_297_test.cpp src/astro.cpp src/holidays.cpp src/skyevents.cpp src/today.cpp src/weather.cpp
# the setup's install-folder check (header only, no Windows needed)
echo "== pathcheck_test"; $CXX $FLAGS -I setup tests/pathcheck_test.cpp -o "$OUT/pathcheck_test" && "$OUT/pathcheck_test"
echo "== song_test"; $CXX $FLAGS -I setup tests/song_test.cpp -o "$OUT/song_test" && "$OUT/song_test"          # (the setup's music: how it is kept, the decoding and the volume)
echo "all test programs passed"
