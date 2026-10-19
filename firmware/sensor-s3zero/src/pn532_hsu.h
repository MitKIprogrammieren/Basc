// Minimaler PN532-Treiber für den HSU-Modus (UART, 115200 Baud).
// Liest nur die UID von ISO14443A-Tags (NTAG/TigerTag, MIFARE Classic wie bei Bambu Lab).
// Bambu-Lab-Tags sind verschlüsselt; die UID reicht aber, um eine Spule wiederzuerkennen.
#pragma once

#include <Arduino.h>

class Pn532Hsu {
public:
    // rst = -1, wenn RSTO nicht verdrahtet ist
    bool begin(HardwareSerial& port, int rx, int tx, int rst);
    // Sucht einen Tag. Gibt die UID als Hex-Text zurück, leer wenn keiner da ist.
    // Dauert ohne Tag etwa 20–40 ms.
    String lese_uid();
    bool bereit() const { return bereit_; }

private:
    bool befehl(const uint8_t* daten, uint8_t laenge, uint8_t* antwort, uint8_t& antwort_laenge, uint32_t timeout_ms);
    bool warte_ack(uint32_t timeout_ms);
    int lies_byte(uint32_t bis);
    void aufwecken();
    HardwareSerial* port_ = nullptr;
    bool bereit_ = false;
};
