// Oberfläche der FilamentLager-Waage (LVGL 8, 800×480).
// Reines LVGL, keine Arduino-Abhängigkeit: läuft auch im PC-Test (test/host).
#pragma once

#include <string>
#include <vector>

#include "ams.h"
#include "etikett.h"
#include "logo.h"
#include "model.h"

struct WaagenStatus {
    float brutto_g = 0;      // Gewicht auf dem Teller inkl. Spule
    bool stabil = false;     // Gewicht ruhig
    bool kalibriert = false; // Waage ist kalibriert (Kopfzeile „Waage“: grün)
    bool sensor_ok = false;  // Sensorplatine antwortet
    std::string tag_l, tag_r;  // NFC-UIDs der beiden Leser, leer = kein Tag
    bool wlan_ok = false;
    int drucker = -1;        // Etikettendrucker: -1 keiner bekannt, 0 grau, 1 bereit, 2 Fehler
    bool ha_ok = false;
    std::string uhrzeit;     // "21:27"
    std::string ip;
    std::string version;
    int leer_standard_g = 250;  // Leerspule, wenn die Sorte keinen eigenen Wert hat
};

// Etikettendrucker aus der Home-Assistant-Integration (m20_druckbruecke)
struct EtikettDrucker {
    std::string id, name;
    bool erreichbar = true;
};

struct EtikettEinst {
    std::vector<EtikettDrucker> drucker;
    bool geladen = false;      // Liste wurde schon abgefragt
    std::string fehler;        // z. B. „Integration nicht gefunden“
    std::string qr_basis;      // eigene QR-Adresse (SchichtLohn), sonst Lager-Liste
    std::string lager_url;     // Lager-Liste in Home Assistant
    bool breit = false;        // Expanded 50×30 statt Classic 40×30
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
    // Herstellerlogo; nullptr = (noch) keins – wird im Hintergrund geladen, danach ui_logos_neu()
    virtual const Logo* logo(const std::string& /*hersteller*/) { return nullptr; }
    // fertig gezeichnetes Etikett drucken
    virtual void etikett_drucken(const std::string& /*titel*/, const EtikettBild& /*bild*/, const std::string& /*drucker*/) {}
    // AMS-Belegung und Etikettendrucker neu abfragen
    virtual void drucker_laden() {}
};

// Einmal nach lv_init() und dem Display-Treiber aufrufen.
void ui_start(UiAktionen* aktionen, const Lager* lager);
// Laufend mit dem aktuellen Stand aufrufen (z. B. 10× pro Sekunde).
void ui_status(const WaagenStatus& s);
// Nach dem Neuladen des Lagers aufrufen.
void ui_lager_neu();
// AMS-Belegung (Zeiger bleibt gültig, bis zum nächsten Aufruf)
void ui_ams(const AmsStand* ams);
// Etikettendrucker und QR-Inhalt
void ui_etikett(const EtikettEinst& e);
// Ein Logo ist fertig geladen
void ui_logos_neu();
// Kurze Meldung unten einblenden.
void ui_meldung(const std::string& text, bool fehler = false);

// Für Tests: Bildschirm direkt öffnen ("wiegen", "lager", "detail:<index>", "einst", "drucker", "etikett:<index>")
void ui_zeige(const std::string& name);
