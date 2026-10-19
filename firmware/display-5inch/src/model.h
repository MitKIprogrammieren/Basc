// Datenmodell für das Filamentlager „Ernsthaft gelagert“ (todo.filament).
// Reines C++ ohne Arduino-Abhängigkeit, damit es auch auf dem PC getestet werden kann.
//
// Jeder Eintrag der Todo-Liste ist eine Filamentsorte. Die Daten stehen als JSON
// in der Beschreibung (v2), z. B.
//   {"v":2,"hersteller":"Bambu Lab","material":"PLA","farbe":"Blue Gray","hex":"#5b6579",
//    "besonderheiten":["Basic"],"gewicht":1000,"spulen":5,"refills":0,"reste":[560],...}
//
// Die Waage ergänzt zwei eigene Felder und lässt alle anderen unverändert:
//   "nfc":   ["04A1B2C3D4E5F6", ...]   NFC-UIDs, die zu dieser Sorte gehören
//   "waage": {"04A1B2C3D4E5F6": 562}   zuletzt gewogener Rest je Spule (UID)
//   "leer_g": 250                      optional: Gewicht der leeren Spule
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

struct Filament {
    std::string uid;          // UID des Todo-Eintrags in Home Assistant
    std::string summary;      // Titel, z. B. "Bambu Lab PLA Blue Gray (Basic)"
    std::string description;  // Original-JSON, wird beim Speichern weiterverwendet

    std::string hersteller, material, farbe;
    uint32_t rgb = 0x888888;
    std::vector<std::string> besonderheiten;
    int gewicht = 1000;  // Nettogewicht einer vollen Spule in g
    int spulen = 1;      // Anzahl Spulen inkl. angebrochener
    int refills = 0;
    std::vector<int> reste;  // Restgewichte angebrochener Spulen in g
    int duese_min = -1, duese_max = -1, bett_min = -1, bett_max = -1;
    int trocknen_c = -1, trocknen_h = -1;
    float durchmesser = 1.75f;
    int leer_g = -1;  // -1 = Standardwert aus der Konfiguration
    std::string fcode, bambu_id;
    std::vector<std::string> codes, nfc;

    // Name ohne Hersteller, z. B. "PLA Blue Gray"
    std::string kurzname() const;
    // Bestand in Gramm: volle Spulen × Gewicht + Summe der Reste
    int bestand_g() const;
    bool hat_nfc(const std::string& tag) const;
};

class Lager {
public:
    std::vector<Filament> sorten;

    // Antwort von POST /api/services/todo/get_items?return_response einlesen.
    // entity z. B. "todo.filament". Gibt false bei Fehler zurück.
    bool lade_service_antwort(const char* json, size_t len, const char* entity);
    // Einen einzelnen Eintrag (summary, uid, description) übernehmen.
    void eintrag(const std::string& uid, const std::string& summary, const std::string& description);

    int finde_nfc(const std::string& tag) const;   // Index oder -1
    int finde_uid(const std::string& uid) const;   // Index oder -1
};

// Ergebnis einer Änderung: neue Beschreibung für todo.update_item
struct Aenderung {
    bool geaendert = false;
    std::string beschreibung;
    std::string meldung;  // kurzer Text für die Anzeige
};

// Gemessenen Rest (netto, ohne Leerspule) speichern.
// tag: NFC-UID der Spule, leer wenn unbekannt.
Aenderung rest_speichern(const std::string& beschreibung, int netto_g, const std::string& tag);
// NFC-UID einer Sorte zuordnen (und von anderen Sorten entfernen, siehe lager_ohne_tag).
Aenderung tag_zuordnen(const std::string& beschreibung, const std::string& tag);
Aenderung tag_entfernen(const std::string& beschreibung, const std::string& tag);
// Spulenzahl um delta ändern (+1 / -1).
Aenderung spulen_aendern(const std::string& beschreibung, int delta);

uint32_t hex_zu_rgb(const std::string& hex, uint32_t standard = 0x888888);
