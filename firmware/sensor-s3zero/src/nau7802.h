// Minimaler Treiber für den NAU7802 (24-Bit-Wandler für Wägezellen, I²C-Adresse 0x2A).
// Die Sensorplatine sucht ihn beim Start und danach alle 5 s an WAAGE_SDA/WAAGE_SCL.
#pragma once
#include <Arduino.h>
#include <Wire.h>

class Nau7802 {
public:
    // true, wenn ein NAU7802 antwortet und eingerichtet werden konnte
    // bus: anderer I²C-Bus, z. B. Wire1, wenn Wire schon belegt ist (Touch des SC01 Plus)
    bool begin(int sda, int scl, TwoWire& bus = Wire);
    bool bereit();   // neuer Messwert liegt vor
    long lesen();    // 24-Bit-Rohwert mit Vorzeichen
    bool da() const { return da_; }

private:
    bool schreiben(uint8_t reg, uint8_t wert);
    int lesen8(uint8_t reg);
    bool bit_setzen(uint8_t reg, uint8_t nr, bool an);
    bool da_ = false;
    TwoWire* bus_ = &Wire;
};
