// Herstellerlogos: PNG dekodieren, für den dunklen Bildschirm aufbereiten (wie logoFaerben() in app.js)
// und als LVGL-Bild ablegen. Reines LVGL/C++, läuft auch im PC-Test.
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#include "lvgl.h"

// Höhen, in denen die Oberfläche Logos zeigt (fertig vorgerechnet – LVGL-Zoom schneidet hier ab)
#if defined(UI_KOMPAKT) && UI_KOMPAKT
#define LOGO_H_KLEIN 12
#define LOGO_H_GROSS 15
#else
#define LOGO_H_KLEIN 15
#define LOGO_H_GROSS 20
#endif

struct Logo {
    lv_img_dsc_t bild[2]{};       // [0] klein (Listen), [1] groß (Karte, Detail) – LV_IMG_CF_TRUE_COLOR_ALPHA
    std::vector<uint8_t> px[2];   // Pixel zu bild
    std::vector<uint8_t> png;     // Original, fürs Etikett
};

// Dateiname wie kurz() in etiketten.py / oberflaeche.cpp: „Bambu Lab“ → „bambu-lab“
std::string logo_kurz(const std::string& hersteller);

// PNG → Logo in beiden Größen (Höhe LOGO_H_KLEIN/LOGO_H_GROSS, Breite höchstens das 8-Fache).
// text_rgb: Farbe, in der dunkle, unbunte Logos auf dem dunklen Hintergrund erscheinen.
bool logo_laden(const uint8_t* png, size_t laenge, uint32_t text_rgb, Logo& aus);

// Schwarz-Weiß-Fassung fürs Etikett (wie fuerDruck() in etikett.js): 1 = schwarz.
// Passt das Logo in breite×hoehe ein, liefert die tatsächliche Größe zurück.
bool logo_druck(const std::vector<uint8_t>& png, int max_w, int max_h, std::vector<uint8_t>& schwarz, int& w, int& h);
