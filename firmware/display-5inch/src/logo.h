// Herstellerlogos: PNG dekodieren, für den dunklen Bildschirm aufbereiten (wie logoFaerben() in app.js)
// und als LVGL-Bild ablegen. Reines LVGL/C++, läuft auch im PC-Test.
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#include "lvgl.h"

// Größen, in denen die Oberfläche Logos zeigt (fertig vorgerechnet – LVGL-Zoom schneidet hier ab).
// Wie die Web-Oberfläche: .hlogo img { height: 16px; max-width: 90px } – breite Logos werden schmaler statt abgeschnitten.
enum LogoArt { LOGO_LISTE = 0, LOGO_GRUPPE = 1, LOGO_GROSS = 2, LOGO_ARTEN = 3 };
#if defined(UI_KOMPAKT) && UI_KOMPAKT
constexpr int LOGO_H[LOGO_ARTEN] = {12, 14, 15};
constexpr int LOGO_W[LOGO_ARTEN] = {62, 62, 110};
#else
constexpr int LOGO_H[LOGO_ARTEN] = {16, 20, 20};
constexpr int LOGO_W[LOGO_ARTEN] = {90, 90, 160};
#endif

struct Logo {
    lv_img_dsc_t bild[LOGO_ARTEN]{};       // je Art ein fertiges Bild (LV_IMG_CF_TRUE_COLOR_ALPHA)
    std::vector<uint8_t> px[LOGO_ARTEN];   // Pixel zu bild
    std::vector<uint8_t> png;              // Original, fürs Etikett
};

// Dateiname wie kurz() in etiketten.py / oberflaeche.cpp: „Bambu Lab“ → „bambu-lab“
std::string logo_kurz(const std::string& hersteller);

// PNG → Logo in allen Größen aus LOGO_H/LOGO_W.
// text_rgb: Farbe, in der dunkle, unbunte Logos auf dem dunklen Hintergrund erscheinen.
bool logo_laden(const uint8_t* png, size_t laenge, uint32_t text_rgb, Logo& aus);

// Schwarz-Weiß-Fassung fürs Etikett (wie fuerDruck() in etikett.js): 1 = schwarz.
// Passt das Logo in breite×hoehe ein, liefert die tatsächliche Größe zurück.
bool logo_druck(const std::vector<uint8_t>& png, int max_w, int max_h, std::vector<uint8_t>& schwarz, int& w, int& h);
