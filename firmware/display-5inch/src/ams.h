// AMS-Belegung der Bambu-Lab-Drucker aus Home Assistant (Bambu-Lab-Integration).
// Gleiche Abfrage und Aufbereitung wie pi/filamentwaage/pi_system.py (AMS_VORLAGE, ams_aufbereiten).
// Wird vom Display und von der Sensorplatine (/api/ams) benutzt. Reines C++ mit ArduinoJson.
#pragma once

#include <ArduinoJson.h>

#include <string>
#include <vector>

#include "model.h"

struct AmsSlot {
    std::string slot;  // "1".."4", bei AMS HT und externer Spule leer oder Nummer
    bool slot_zahl = false;  // slot als Zahl ausgeben (wie in Python)
    bool leer = true;
    std::string hex = "#888888";
    uint32_t rgb = 0x888888;
    std::string name;
    int rest = -1;  // Prozent, -1 = unbekannt
    bool aktiv = false;
    std::string tag;          // tag_uid aus Home Assistant, leer wenn keiner
    std::string filament_id;  // z. B. GFA01
    int fach = 0;             // nur zum Sortieren
};

struct AmsEinheit {
    std::string name, kurz;  // "AMS A", "A"
    int feuchte = -1;        // %, -1 = unbekannt
    int nr = 0;
    std::vector<AmsSlot> slots;
};

struct AmsDrucker {
    std::string id, name;
    bool online = false;
    std::vector<AmsEinheit> ams;
};

struct AmsStand {
    std::vector<AmsDrucker> drucker;
    bool abbuchung_aktiv = false;
    bool geladen = false;
};

// Template für POST /api/template – liefert JSON
extern const char* const AMS_VORLAGE;

// Antwort des Templates → AmsStand. false bei ungültigem JSON.
bool ams_aufbereiten(const char* json, size_t laenge, AmsStand& aus);

// Gleiche Form wie die Pi-Variante: {"drucker":[…]} (für die Web-Oberfläche)
void ams_als_json(const AmsStand& s, JsonObject ziel);

struct AmsTreffer {
    const AmsDrucker* drucker = nullptr;
    const AmsEinheit* ams = nullptr;
    const AmsSlot* slot = nullptr;
    explicit operator bool() const { return slot != nullptr; }
};

// Slot, in dem die Spule mit diesem NFC-Tag zuletzt steckte (Vergleich der ersten 8 Hex-Zeichen).
// nur_drucker: Auswahl aus den Einstellungen, leer = alle Drucker.
AmsTreffer ams_slot_fuer_tag(const AmsStand& s, const std::string& tag, const std::vector<std::string>& nur_drucker = {});

// Sorte mit gleicher Bambu-ID und ähnlicher Farbe, -1 wenn keine passt
int ams_vorschlag(const Lager& lager, const AmsSlot& slot);

// "A2", "HT1", "Ext" – kurze Slot-Bezeichnung für Chips
std::string ams_slot_kurz(const AmsEinheit& a, const AmsSlot& s);
