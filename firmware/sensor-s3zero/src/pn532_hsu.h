// Minimaler PN532-Treiber für den HSU-Modus (UART, 115200 Baud).
// Liest die UID von ISO14443A-Tags (NTAG/TigerTag, MIFARE Classic wie bei Bambu Lab) und bei Bambu-Lab-Tags
// zusätzlich Material, Farbe und Gewicht (Schlüssel aus der UID abgeleitet, wie im Bambu-Research-Group-Guide).
#pragma once

#include <Arduino.h>

// Inhalt eines Bambu-Lab-Tags (MIFARE Classic 1K, Blöcke 1, 2, 4, 5)
struct BambuTag {
    String variante;     // z. B. „A00-K0“ (Farbvariante)
    String material_id;  // z. B. „GFA00“ – wie filament_id im AMS
    String typ;          // z. B. „PLA“
    String detail;       // z. B. „PLA Basic“
    String farbe;        // „RRGGBBAA“ wie im AMS
    uint16_t gewicht = 0;     // Filament in g
    float durchmesser = 0;    // mm
    // Block 6: Trocknen, Bett und Düse (0 = nicht angegeben)
    uint16_t trocknen_c = 0, trocknen_h = 0, bett = 0, duese_max = 0, duese_min = 0;
};

class Pn532Hsu {
public:
    // rst = -1, wenn RSTPDN nicht verdrahtet ist
    bool begin(HardwareSerial& port, int rx, int tx, int rst);
    // Setzt den Leser zurück (wenn RSTPDN verdrahtet ist) und richtet ihn neu ein.
    bool neu_starten();
    // Sucht einen Tag. Gibt die UID als Hex-Text zurück, leer wenn keiner da ist.
    // Dauert ohne Tag etwa 20–40 ms.
    String lese_uid();
    // Nach lese_uid(): Liest einen Bambu-Lab-Tag aus. false, wenn es keiner ist oder das Lesen scheitert.
    bool lese_bambu(BambuTag& t);
    bool bereit() const { return bereit_; }
    uint32_t neustarts() const { return neustarts_; }

private:
    bool befehl(const uint8_t* daten, uint8_t laenge, uint8_t* antwort, uint8_t& antwort_laenge, uint32_t timeout_ms);
    bool warte_ack(uint32_t timeout_ms);
    int lies_byte(uint32_t bis);
    void aufwecken();
    bool einrichten();
    bool mifare_auth(uint8_t block, const uint8_t* key);
    bool mifare_lesen(uint8_t block, uint8_t* daten16);
    void feld_neu();
    HardwareSerial* port_ = nullptr;
    int rst_ = -1;
    bool bereit_ = false;
    uint8_t fehler_ = 0;      // Kommunikationsfehler hintereinander
    uint32_t neustarts_ = 0;
    uint8_t uid_[10] = {};    // UID und SAK der letzten Lesung
    uint8_t uid_len_ = 0, sak_ = 0;
};
