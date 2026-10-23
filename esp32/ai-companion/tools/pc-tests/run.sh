#!/bin/sh
# Tests and pictures that run on a normal PC (Linux), no board needed.
#   alarms_test: timers, reminders, restarts and summer/winter time (prints what happens)
#   preview: draws the Time screen into .ppm pictures (open with any image viewer)
#   claude_preview: draws the Claude screen, the top-bar tabs, the call button and the pull-down panel
#   sketch_preview: draws the Sketch screen and gallery, checks saving/loading a drawing
# Needs g++. Downloads the ArduinoJson header once.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SRC="$HERE/../../src"
OUT=${OUT:-$HERE/out}
mkdir -p "$OUT"
[ -f "$OUT/ArduinoJson.h" ] || curl -sSL -o "$OUT/ArduinoJson.h" \
  https://github.com/bblanchon/ArduinoJson/releases/download/v7.2.1/ArduinoJson-v7.2.1.h
FLAGS="-std=gnu++11 -O1 -I$SRC"
g++ $FLAGS -I"$HERE/stub" -I"$OUT" "$HERE/alarms_test.cpp" "$SRC/alarms.cpp" "$HERE/stub/fakeclock.cpp" -o "$OUT/alarms_test"
g++ $FLAGS "$HERE/time_screen_preview.cpp" "$SRC/time_screen.cpp" "$SRC/gfx.cpp" "$SRC/gfont_data.cpp" -o "$OUT/preview"
g++ $FLAGS "$HERE/claude_preview.cpp" "$SRC/claude_screen.cpp" "$SRC/tabs.cpp" "$SRC/panel.cpp" "$SRC/gfx.cpp" "$SRC/gfont_data.cpp" -o "$OUT/claude_preview"
g++ $FLAGS "$HERE/sketch_preview.cpp" "$SRC/sketch.cpp" "$SRC/gfx.cpp" "$SRC/gfont_data.cpp" -o "$OUT/sketch_preview"
cd "$OUT"
./alarms_test a && ./alarms_test b && ./alarms_test c
./preview
./sketch_preview
./claude_preview
echo "pictures: $OUT/*.ppm"
