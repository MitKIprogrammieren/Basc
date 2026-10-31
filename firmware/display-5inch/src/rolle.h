// Filamentrolle wie rolle() in pi/filamentwaage/web/app.js – dieselbe Zeichnung (SVG 44×44, um −16° gedreht),
// hier in Software gerastert, weil LVGL 8 kein SVG kann. Farbe, Besonderheiten (Seide, Matt, Metallic, CF, GF,
// Glitzer, Transparent) und Größe frei. Ergebnis wird zwischengespeichert. Reines C++/LVGL, läuft im PC-Test.
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#include "lvgl.h"

// Rolle in Pixelgröße groesse (quadratisch). Zeiger bleibt gültig, solange der Zwischenspeicher sie hält
// (die zuletzt benutzten ROLLE_CACHE Bilder).
const lv_img_dsc_t* rolle_bild(uint32_t rgb, const std::vector<std::string>& besonderheiten, int groesse,
                               bool leer = false);

// Rohfassung als RGBA (für Tests und den Abgleich mit dem Browser)
void rolle_rgba(uint32_t rgb, const std::vector<std::string>& besonderheiten, int groesse, std::vector<uint8_t>& rgba);

// Wie viele fertige Bilder höchstens im Speicher bleiben
#ifndef ROLLE_CACHE
#define ROLLE_CACHE 96
#endif
