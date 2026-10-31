// Etikett für die Phomemo-Drucker (M110, M20 Pro …) – Format „Classic“ (40×30 mm) bzw. „Expanded“ (50×30 mm)
// wie quer() in pi/filamentwaage/web/etikett.js: Logo, schwarzer Balken mit Material und Hex-Wert, Farbe,
// Werte links unten, QR-Code rechts unten. Gezeichnet mit 203 dpi in Schwarz-Weiß.
// Reines LVGL (Canvas), läuft auch im PC-Test.
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#include "lvgl.h"
#include "model.h"

struct EtikettOpt {
    bool breit = false;           // Expanded 50×30 statt Classic 40×30
    int rest_g = 0;               // > 0: gewogener Rest kommt mit aufs Etikett
    std::string qr;               // Inhalt des QR-Codes, leer = keiner
    const std::vector<uint8_t>* logo_png = nullptr;  // Herstellerlogo, sonst wird der Name geschrieben
};

struct EtikettBild {
    int breite = 0, hoehe = 0;     // Punkte (Breite durch 8 teilbar)
    std::vector<uint8_t> raster;   // 1 Bit je Punkt, MSB zuerst, 1 = schwarz (wie Etikett.raster())
    std::vector<lv_color_t> px;    // dieselbe Zeichnung als Bild für die Vorschau
    lv_img_dsc_t bild{};           // zeigt auf px
};

// Verkleinerte Vorschau in Graustufen (LVGL-Zoom schneidet große Bilder ab)
struct EtikettVorschau {
    std::vector<lv_color_t> px;
    lv_img_dsc_t bild{};
};
void etikett_vorschau(const EtikettBild& e, int hoehe, EtikettVorschau& aus);

// Zeichnet das Etikett. Braucht ein laufendes LVGL (lv_init).
bool etikett_zeichnen(const Filament& f, const EtikettOpt& o, EtikettBild& aus);

// Inhalt des QR-Codes wie etQr() in app.js: eigene Adresse + ?rolle=<Code>, sonst die Lager-Liste
std::string etikett_qr(const Filament& f, const std::string& basis, const std::string& lager_url);

// Titel für den Druckauftrag, z. B. "PLA Blue Gray (Bambu Lab)"
std::string etikett_titel(const Filament& f);
