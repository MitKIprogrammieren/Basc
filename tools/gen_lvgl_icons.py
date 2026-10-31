#!/usr/bin/env python3
"""Erzeugt die Symbole der Web-Oberfläche (pi/filamentwaage/web) als LVGL-8-Bilder (Alpha 8 Bit, einfärbbar).

    python3 tools/gen_lvgl_icons.py firmware/display-5inch/src/icons

Die SVG-Pfade sind aus index.html bzw. app.js übernommen – ändert sich dort ein Symbol, hier mitziehen.
Braucht cairosvg und Pillow.
"""
import io
import os
import sys

import cairosvg
from PIL import Image

# Name: (Inhalt des <svg> mit viewBox 0 0 24 24, Strichstärke)
SYMBOLE = {
    # Navigation (index.html)
    "waage": ('<path d="M4 20h16"/><path d="M6 20l2-9h8l2 9"/><path d="M12 11V5"/><path d="M8 5h8"/>', 2),
    "lager": ('<rect x="3" y="4" width="18" height="6" rx="1.5"/><rect x="3" y="14" width="18" height="6" rx="1.5"/><path d="M7 7h.01M7 17h.01"/>', 2),
    "einst": ('<circle cx="12" cy="12" r="3"/><path d="M12 2v3M12 19v3M4.2 4.2l2.1 2.1M17.7 17.7l2.1 2.1M2 12h3M19 12h3M4.2 19.8l2.1-2.1M17.7 6.3l2.1-2.1"/>', 2),
    # Kopfzeile, Wiegen, Lager
    "wlan": ('<path d="M2 8.8a15 15 0 0 1 20 0"/><path d="M5.5 12.5a10 10 0 0 1 13 0"/><path d="M9 16.2a5 5 0 0 1 6 0"/><circle cx="12" cy="19.5" r=".8"/>', 2),
    "drucker": ('<path d="M7 9V3h10v6"/><rect x="3" y="9" width="18" height="8" rx="2"/><path d="M7 14h10v7H7z"/>', 2),
    "lupe": ('<circle cx="11" cy="11" r="7"/><path d="m20 20-3.5-3.5"/>', 2),
    "runter": ('<path d="m6 9 6 6 6-6"/>', 2.2),
    "hoch": ('<path d="m6 15 6-6 6 6"/>', 2.2),
    "filter": ('<path d="M4 6h16M7 12h10M10 18h4"/>', 2.2),
    "zu": ('<path d="m7 15 5-5 5 5"/><path d="m7 20 5-5 5 5" opacity=".45"/>', 2.2),
    "auf": ('<path d="m7 9 5 5 5-5"/><path d="m7 4 5 5 5-5" opacity=".45"/>', 2.2),
    "x": ('<path d="M6 6l12 12M18 6L6 18"/>', 2.4),
    "haken": ('<path d="M5 12.5l4.5 4.5L19 7.5"/>', 2.4),
}
GROESSEN = [12, 13, 14, 16, 18, 20, 22]


def alpha(inhalt, staerke, px):
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{px}" height="{px}" viewBox="0 0 24 24" fill="none" '
           f'stroke="#000" stroke-width="{staerke}" stroke-linecap="round" stroke-linejoin="round">{inhalt}</svg>')
    png = cairosvg.svg2png(bytestring=svg.encode(), output_width=px, output_height=px)
    return Image.open(io.BytesIO(png)).convert("RGBA").getchannel("A").tobytes()


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(1)
    aus = sys.argv[1]
    os.makedirs(aus, exist_ok=True)
    c = ['// Erzeugt von tools/gen_lvgl_icons.py – nicht von Hand ändern.', '#include "icons.h"', ""]
    h = ['// Erzeugt von tools/gen_lvgl_icons.py – nicht von Hand ändern.', "#pragma once", '#include "lvgl.h"', "",
         "// Symbol in passender Größe (die nächstkleinere vorhandene), nullptr wenn unbekannt",
         "const lv_img_dsc_t* ikon_bild(const char* name, int px);", ""]
    tabelle = []
    for name, (inhalt, staerke) in SYMBOLE.items():
        for px in GROESSEN:
            daten = alpha(inhalt, staerke, px)
            v = f"ik_{name}_{px}"
            zeilen = [", ".join(f"0x{b:02x}" for b in daten[i:i + 24]) for i in range(0, len(daten), 24)]
            c.append(f"static const uint8_t {v}_px[] = {{\n  " + ",\n  ".join(zeilen) + "\n};")
            c.append(f"static const lv_img_dsc_t {v} = {{{{LV_IMG_CF_ALPHA_8BIT, 0, 0, {px}, {px}}}, {len(daten)}, {v}_px}};")
            tabelle.append(f'    {{"{name}", {px}, &{v}}},')
    c += ["", "static const struct { const char* name; int px; const lv_img_dsc_t* bild; } TABELLE[] = {"] + tabelle + ["};", "",
          "const lv_img_dsc_t* ikon_bild(const char* name, int px) {",
          "    const lv_img_dsc_t* best = nullptr;",
          "    int best_px = 0;",
          "    for (const auto& t : TABELLE) {",
          "        if (strcmp(t.name, name)) continue;",
          "        if (t.px <= px && t.px > best_px) { best = t.bild; best_px = t.px; }",
          "        if (!best && t.px > px) best = t.bild;",
          "    }",
          "    return best;",
          "}", ""]
    c.insert(2, "#include <string.h>")
    with open(os.path.join(aus, "icons.cpp"), "w", encoding="utf-8") as f:
        f.write("\n".join(c))
    with open(os.path.join(aus, "icons.h"), "w", encoding="utf-8") as f:
        f.write("\n".join(h))
    print(f"{len(SYMBOLE)} Symbole × {len(GROESSEN)} Größen → {aus}")


if __name__ == "__main__":
    main()
