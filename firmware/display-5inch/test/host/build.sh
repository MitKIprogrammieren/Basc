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

echo "AMS-Test (gegen die Python-Version) …"
g++ -std=c++17 -Wall -Wextra -O1 "${INC[@]}" "$HIER/test_ams.cpp" "$FW/src/ams.cpp" "$FW/src/model.cpp" -o "$OBJ/test_ams"
"$OBJ/test_ams" "$FW/../../testdaten/ams_vorlage.json" "$FW/../../testdaten/todo_filament.json" > "$OBJ/ams_cpp.json"
PYTHONPATH="$FW/../../pi" python3 - "$FW/../../testdaten/ams_vorlage.json" "$OBJ/ams_cpp.json" <<'PY'
import json, sys
from filamentwaage.pi_system import ams_aufbereiten
py = ams_aufbereiten(json.load(open(sys.argv[1])))
assert py == json.load(open(sys.argv[2])), "AMS: C++ und Python unterscheiden sich"
print("AMS: gleich wie Python")
PY

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

echo "Oberfläche rendern (5\", 800×480) …"
g++ -std=c++17 -Wall -Wextra -O1 "${INC[@]}" "$HIER/render.cpp" "$FW/src/ui.cpp" "$FW/src/model.cpp" \
    "$OBJ"/fl_*.o "$OBJ/liblvgl.a" -o "$OBJ/render"
"$OBJ/render" "$FW/../../testdaten/todo_filament.json" "$AUS"

echo "Oberfläche rendern (SC01 Plus, 480×320) …"
mkdir -p "$AUS/sc01plus"
g++ -std=c++17 -Wall -Wextra -O1 -DUI_KOMPAKT=1 "${INC[@]}" "$HIER/render.cpp" "$FW/src/ui.cpp" "$FW/src/model.cpp" \
    "$OBJ"/fl_*.o "$OBJ/liblvgl.a" -o "$OBJ/render_kompakt"
"$OBJ/render_kompakt" "$FW/../../testdaten/todo_filament.json" "$AUS/sc01plus"

for p in "$AUS"/*.ppm "$AUS"/sc01plus/*.ppm; do python3 -c "from PIL import Image; import sys; Image.open(sys.argv[1]).save(sys.argv[1][:-4]+'.png')" "$p" && rm "$p"; done
ls "$AUS"/*.png "$AUS"/sc01plus/*.png
