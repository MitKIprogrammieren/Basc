#!/usr/bin/env python3
"""Erzeugt LVGL-8-Schriften (4 bpp, unkomprimiert) aus TTF-Dateien.

Ersatz für lv_font_conv, braucht nur Pillow:
    python3 tools/gen_lvgl_fonts.py <ordner-mit-montserrat-ttf> firmware/display-5inch/src/fonts

Die eingebauten LVGL-Montserrat-Schriften enthalten nur ASCII, also keine
Umlaute. Diese hier decken Latin-1 ab (ä ö ü ß ° · …) und fallen für
LV_SYMBOL_* auf die eingebaute Montserrat zurück.
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

LATIN = list(range(0x20, 0x7F)) + list(range(0xA0, 0x100)) + [0x2013, 0x2026, 0x2192, 0x2212, 0x2022]
DIGITS = [ord(c) for c in " %+,-.0123456789g"] + [0x2212]

FONTS = [
    # name,     ttf,                       px, zeichen, fallback (eingebaut, für Symbole)
    ("fl_12", "Montserrat-Medium.ttf", 12, LATIN, "lv_font_montserrat_12"),  # 3,5" (SC01 Plus)
    ("fl_14", "Montserrat-Medium.ttf", 14, LATIN, "lv_font_montserrat_14"),
    ("fl_16", "Montserrat-SemiBold.ttf", 16, LATIN, "lv_font_montserrat_16"),
    ("fl_20", "Montserrat-Bold.ttf", 20, LATIN, "lv_font_montserrat_20"),
    ("fl_28", "Montserrat-ExtraBold.ttf", 28, LATIN, "lv_font_montserrat_20"),
    ("fl_52", "Montserrat-ExtraBold.ttf", 52, DIGITS, "NULL"),  # 3,5" (SC01 Plus)
    ("fl_96", "Montserrat-ExtraBold.ttf", 96, DIGITS, "NULL"),
]


def glyph(font, cp):
    ch = chr(cp)
    adv = font.getlength(ch)
    x0b, y0b, x1b, y1b = font.getbbox(ch, anchor="ls")
    ox, oy, w, h = x0b, y0b, max(0, x1b - x0b), max(0, y1b - y0b)
    px = []
    if w and h:
        img = Image.new("L", (w, h), 0)
        ImageDraw.Draw(img).text((-x0b, -y0b), ch, font=font, fill=255, anchor="ls")
        px = list(img.tobytes())
    # leere Ränder abschneiden
    if px:
        rows = [any(px[y * w:(y + 1) * w]) for y in range(h)]
        cols = [any(px[y * w + x] for y in range(h)) for x in range(w)]
        if not any(rows):
            w = h = 0
            px = []
        else:
            y0, y1 = rows.index(True), h - rows[::-1].index(True)
            x0, x1 = cols.index(True), w - cols[::-1].index(True)
            px = [px[y * w + x] for y in range(y0, y1) for x in range(x0, x1)]
            ox, oy = ox + x0, oy + y0
            w, h = x1 - x0, y1 - y0
    # 4 bpp, fortlaufend gepackt
    data = bytearray()
    nib = [v >> 4 for v in px]
    for i in range(0, len(nib), 2):
        a = nib[i]
        b = nib[i + 1] if i + 1 < len(nib) else 0
        data.append((a << 4) | b)
    ofs_y = -(oy + h) if h else 0
    return {
        "cp": cp, "adv": int(round(adv * 16)), "w": w, "h": h,
        "ofs_x": ox if w else 0, "ofs_y": ofs_y, "data": bytes(data),
    }


def cmaps(cps):
    """Aufeinanderfolgende Bereiche als FORMAT0_TINY, Rest als SPARSE_TINY."""
    out, i = [], 0
    while i < len(cps):
        j = i
        while j + 1 < len(cps) and cps[j + 1] == cps[j] + 1:
            j += 1
        out.append((cps[i], cps[i:j + 1]))
        i = j + 1
    # Reihenfolge beibehalten, damit die Glyph-IDs je Abschnitt fortlaufend sind
    result, sparse = [], []
    for start, run in out:
        if len(run) >= 8:
            if sparse:
                result.append(("sparse", sparse[0], sparse))
                sparse = []
            result.append(("range", start, run))
        else:
            sparse.extend(run)
    if sparse:
        result.append(("sparse", sparse[0], sparse))
    return result


def emit(name, ttf, size, chars, fallback, outdir):
    font = ImageFont.truetype(ttf, size)
    ascent, descent = font.getmetrics()
    cps = sorted(set(chars))
    glyphs = [glyph(font, cp) for cp in cps]
    lines = []
    w = lines.append
    w("/* Automatisch erzeugt von tools/gen_lvgl_fonts.py – nicht von Hand ändern.")
    w(f" * Schrift: {os.path.basename(ttf)} {size} px, 4 bpp, {len(cps)} Zeichen.")
    w(" * Montserrat: SIL Open Font License 1.1, siehe fonts/OFL.txt */")
    w('#include "lvgl.h"')
    w("")
    w("static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {")
    idx, offsets = 0, []
    for g in glyphs:
        offsets.append(idx)
        label = chr(g["cp"]).replace("\\", "\\\\").replace("*/", "* /")
        w(f"    /* U+{g['cp']:04X} '{label}' */")
        for k in range(0, len(g["data"]), 16):
            w("    " + ", ".join(f"0x{b:02x}" for b in g["data"][k:k + 16]) + ",")
        idx += len(g["data"])
    if idx == 0:
        w("    0x00,")
    w("};")
    w("")
    w("static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {")
    w("    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},")
    for g, off in zip(glyphs, offsets):
        w(f"    {{.bitmap_index = {off}, .adv_w = {g['adv']}, .box_w = {g['w']}, .box_h = {g['h']}, "
          f".ofs_x = {g['ofs_x']}, .ofs_y = {g['ofs_y']}}},")
    w("};")
    w("")
    maps = cmaps(cps)
    gid = {cp: i + 1 for i, cp in enumerate(cps)}
    for n, (kind, start, run) in enumerate(maps):
        if kind == "sparse":
            w(f"static const uint16_t unicode_list_{n}[] = {{")
            w("    " + ", ".join(f"0x{cp - start:x}" for cp in run))
            w("};")
    w("")
    w("static const lv_font_fmt_txt_cmap_t cmaps[] = {")
    for n, (kind, start, run) in enumerate(maps):
        if kind == "range":
            w(f"    {{.range_start = {start}, .range_length = {len(run)}, .glyph_id_start = {gid[run[0]]}, "
              ".unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, "
              ".type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY},")
        else:
            w(f"    {{.range_start = {start}, .range_length = {run[-1] - start + 1}, .glyph_id_start = {gid[run[0]]}, "
              f".unicode_list = unicode_list_{n}, .glyph_id_ofs_list = NULL, .list_length = {len(run)}, "
              ".type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY},")
    w("};")
    w("")
    w("#if LVGL_VERSION_MAJOR == 8")
    w("static lv_font_fmt_txt_glyph_cache_t cache;")
    w("#endif")
    w("")
    w("static const lv_font_fmt_txt_dsc_t font_dsc = {")
    w("    .glyph_bitmap = glyph_bitmap, .glyph_dsc = glyph_dsc, .cmaps = cmaps,")
    w(f"    .kern_dsc = NULL, .kern_scale = 0, .cmap_num = {len(maps)}, .bpp = 4, .kern_classes = 0, .bitmap_format = 0,")
    w("#if LVGL_VERSION_MAJOR == 8")
    w("    .cache = &cache")
    w("#endif")
    w("};")
    w("")
    if fallback != "NULL":
        w(f"LV_FONT_DECLARE({fallback});")
    w(f"const lv_font_t {name} = {{")
    w("    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt, .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,")
    w(f"    .line_height = {ascent + descent}, .base_line = {descent}, .subpx = LV_FONT_SUBPX_NONE,")
    w(f"    .underline_position = {-max(1, size // 10)}, .underline_thickness = {max(1, size // 14)},")
    w(f"    .dsc = &font_dsc, .fallback = {'&' + fallback if fallback != 'NULL' else 'NULL'}, .user_data = NULL")
    w("};")
    path = os.path.join(outdir, f"{name}.c")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print(f"{path}: {len(cps)} Zeichen, {idx} Bytes Bitmap")


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    for name, ttf, size, chars, fb in FONTS:
        emit(name, os.path.join(src, ttf), size, chars, fb, out)
    with open(os.path.join(out, "fonts.h"), "w", encoding="utf-8") as f:
        f.write("#pragma once\n#include \"lvgl.h\"\n")
        for name, *_ in FONTS:
            f.write(f"LV_FONT_DECLARE({name});\n")


if __name__ == "__main__":
    main()
