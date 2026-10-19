#!/usr/bin/env bash
# PC-Test für die Display-Firmware: Modelltest + gerenderte Bildschirme.
# Braucht g++, LVGL 8.3 und ArduinoJson 7 als Quellordner:
#   git clone --depth 1 -b release/v8.3 https://github.com/lvgl/lvgl /tmp/deps/lvgl
#   git clone --depth 1 -b v7.2.1 https://github.com/bblanchon/ArduinoJson /tmp/deps/ArduinoJson
# Aufruf: test/host/build.sh [deps-ordner] [ausgabeordner]
set -euo pipefail
HIER="$(cd "$(dirname "$0")" && pwd)"
FW="$(cd "$HIER/../.." && pwd)"
DEPS="${1:-/tmp/deps}"
AUS="${2:-$HIER/out}"
OBJ="$AUS/obj"
mkdir -p "$OBJ"

INC=(-I"$FW/include" -I"$DEPS" -I"$DEPS/lvgl" -I"$DEPS/ArduinoJson/src" -I"$FW/src"
     -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE)

echo "Modelltest …"
g++ -std=c++17 -Wall -Wextra -O1 "${INC[@]}" "$HIER/test_model.cpp" "$FW/src/model.cpp" -o "$OBJ/test_model"
"$OBJ/test_model"

echo "LVGL bauen …"
if [ ! -f "$OBJ/liblvgl.a" ]; then
  find "$DEPS/lvgl/src" -name '*.c' | {
    while read -r f; do
      o="$OBJ/lv_$(echo "$f" | md5sum | cut -c1-12).o"
      gcc -O1 -w "${INC[@]}" -c "$f" -o "$o" &
      while [ "$(jobs -r | wc -l)" -ge 8 ]; do sleep 0.05; done
    done
    wait
  }
  ar rcs "$OBJ/liblvgl.a" "$OBJ"/lv_*.o
fi
for f in "$FW"/src/fonts/*.c; do gcc -O1 -w "${INC[@]}" -c "$f" -o "$OBJ/$(basename "$f" .c).o"; done

echo "Oberfläche rendern …"
g++ -std=c++17 -Wall -Wextra -O1 "${INC[@]}" "$HIER/render.cpp" "$FW/src/ui.cpp" "$FW/src/model.cpp" \
    "$OBJ"/fl_*.o "$OBJ/liblvgl.a" -o "$OBJ/render"
"$OBJ/render" "$FW/../../testdaten/todo_filament.json" "$AUS"
for p in "$AUS"/*.ppm; do python3 -c "from PIL import Image; import sys; Image.open(sys.argv[1]).save(sys.argv[1][:-4]+'.png')" "$p" && rm "$p"; done
ls "$AUS"/*.png
