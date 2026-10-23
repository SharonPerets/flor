#!/bin/sh
# Tests and pictures that run on a normal PC (Linux), no board needed.
#   alarms_test: timers, reminders, restarts and summer/winter time (prints what happens)
#   time_screen_preview: draws the Time screen into .ppm pictures (open with any image viewer)
# Needs g++. Downloads the ArduinoJson header once.
set -e
cd "$(dirname "$0")"
SRC=../../src
OUT=${OUT:-out}
mkdir -p "$OUT"
[ -f "$OUT/ArduinoJson.h" ] || curl -sSL -o "$OUT/ArduinoJson.h" \
  https://github.com/bblanchon/ArduinoJson/releases/download/v7.2.1/ArduinoJson-v7.2.1.h
g++ -std=c++17 -Istub -I"$OUT" -I$SRC alarms_test.cpp $SRC/alarms.cpp stub/fakeclock.cpp -o "$OUT/alarms_test"
g++ -O1 -std=c++17 -I$SRC time_screen_preview.cpp $SRC/time_screen.cpp $SRC/gfx.cpp $SRC/gfont_data.cpp -o "$OUT/preview"
cd "$OUT" && ./alarms_test a && ./alarms_test b && ./alarms_test c && ./preview && echo "pictures: $(pwd)/*.ppm"
