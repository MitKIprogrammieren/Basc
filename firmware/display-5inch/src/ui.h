// Oberfläche der FilamentLager-Waage (LVGL 8, 800×480).
// Reines LVGL, keine Arduino-Abhängigkeit: läuft auch im PC-Test (test/host).
#pragma once

#include <string>

#include "model.h"

struct WaagenStatus {
    float brutto_g = 0;      // Gewicht auf dem Teller inkl. Spule
    bool stabil = false;     // Gewicht ruhig
    bool sensor_ok = false;  // Sensorplatine antwortet
    std::string tag_l, tag_r;  // NFC-UIDs der beiden Leser, leer = kein Tag
    bool wlan_ok = false;
    bool ha_ok = false;
    std::string uhrzeit;     // "21:27"
    std::string ip;
    std::string version;
    int leer_standard_g = 250;  // Leerspule, wenn die Sorte keinen eigenen Wert hat
};

class UiAktionen {
public:
    virtual ~UiAktionen() {}
    virtual void tara() = 0;
    virtual void kalibrieren(int gramm) = 0;
    virtual void rest_speichern(const std::string& sorte_uid, int netto_g, const std::string& tag) = 0;
    virtual void tag_zuordnen(const std::string& sorte_uid, const std::string& tag) = 0;
    virtual void spulen_aendern(const std::string& sorte_uid, int delta) = 0;
    virtual void neu_laden() = 0;
};

// Einmal nach lv_init() und dem Display-Treiber aufrufen.
void ui_start(UiAktionen* aktionen, const Lager* lager);
// Laufend mit dem aktuellen Stand aufrufen (z. B. 10× pro Sekunde).
void ui_status(const WaagenStatus& s);
// Nach dem Neuladen des Lagers aufrufen.
void ui_lager_neu();
// Kurze Meldung unten einblenden.
void ui_meldung(const std::string& text, bool fehler = false);

// Für Tests: Bildschirm direkt öffnen ("wiegen", "lager", "detail:<index>", "einst")
void ui_zeige(const std::string& name);
