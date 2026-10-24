#!/bin/sh
# Tests and pictures that run on a normal PC (Linux), no board needed.
#   alarms_test: timers, reminders, restarts and summer/winter time (prints what happens)
#   preview: draws the Time screen into .ppm pictures (open with any image viewer)
#   claude_preview: draws the Claude screen, the top-bar tabs, the call button and the pull-down panel
#   layout_preview: whole screens - top bar, the slim music line, the Action Center (4.3 design)
#   todo_preview: checks the to-do list (days, order, saving) and draws the To-do screen and keyboard
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
g++ $FLAGS "$HERE/layout_preview.cpp" "$SRC/action_center.cpp" "$SRC/weather.cpp" "$SRC/tabs.cpp" "$SRC/panel.cpp" "$SRC/time_screen.cpp" "$SRC/gfx.cpp" "$SRC/gfont_data.cpp" -o "$OUT/layout_preview"
g++ $FLAGS -I"$HERE/stub" -I"$OUT" "$HERE/todo_preview.cpp" "$SRC/todos.cpp" "$SRC/todo_screen.cpp" "$SRC/gfx.cpp" "$SRC/gfont_data.cpp" -o "$OUT/todo_preview"
cd "$OUT"
./alarms_test a && ./alarms_test b && ./alarms_test c
./preview
./todo_preview
./claude_preview
./layout_preview
echo "pictures: $OUT/*.ppm"
