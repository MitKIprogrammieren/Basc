#include "nau7802.h"

namespace {
constexpr uint8_t ADR = 0x2A;
constexpr uint8_t PU_CTRL = 0x00, CTRL1 = 0x01, CTRL2 = 0x02, ADCO_B2 = 0x12, ADC = 0x15, PGA_PWR = 0x1C;
// PU_CTRL-Bits
constexpr uint8_t RR = 0, PUD = 1, PUA = 2, PUR = 3, CS = 4, CR = 5, AVDDS = 7;
}  // namespace

bool Nau7802::schreiben(uint8_t reg, uint8_t wert) {
    Wire.beginTransmission(ADR);
    Wire.write(reg);
    Wire.write(wert);
    return Wire.endTransmission() == 0;
}

int Nau7802::lesen8(uint8_t reg) {
    Wire.beginTransmission(ADR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return -1;
    if (Wire.requestFrom(ADR, (uint8_t)1) != 1) return -1;
    return Wire.read();
}

bool Nau7802::bit_setzen(uint8_t reg, uint8_t nr, bool an) {
    int v = lesen8(reg);
    if (v < 0) return false;
    v = an ? (v | (1 << nr)) : (v & ~(1 << nr));
    return schreiben(reg, (uint8_t)v);
}

bool Nau7802::begin(int sda, int scl) {
    Wire.begin(sda, scl, 100000);
    Wire.beginTransmission(ADR);
    if (Wire.endTransmission() != 0) {
        Wire.end();
        return false;  // kein NAU7802 an diesen Pins
    }
    // Ablauf nach Datenblatt 9.1: Reset, Digital-Teil an, warten bis bereit
    if (!bit_setzen(PU_CTRL, RR, true)) return false;
    delay(1);
    bit_setzen(PU_CTRL, RR, false);
    bit_setzen(PU_CTRL, PUD, true);
    uint32_t t0 = millis();
    while (true) {
        int v = lesen8(PU_CTRL);
        if (v >= 0 && (v & (1 << PUR))) break;
        if (millis() - t0 > 300) return false;
        delay(2);
    }
    bit_setzen(PU_CTRL, PUA, true);
    schreiben(CTRL1, (0b100 << 3) | 0b111);  // interner LDO 3,3 V, Verstärkung 128
    bit_setzen(PU_CTRL, AVDDS, true);               // Analogteil aus dem internen LDO
    schreiben(CTRL2, 0b000 << 4);            // 10 Messungen je Sekunde – ruhiger Wert
    schreiben(ADC, 0x30);                    // Chopper-Takt aus (Empfehlung des Herstellers)
    bit_setzen(PGA_PWR, 7, true);                   // 330-pF-Kondensator an Kanal 2 – weniger Rauschen
    delay(10);
    bit_setzen(PU_CTRL, CS, true);                  // Messung starten
    // interne Kalibrierung des Verstärkers
    bit_setzen(CTRL2, 2, true);
    t0 = millis();
    while (millis() - t0 < 1000) {
        int v = lesen8(CTRL2);
        if (v >= 0 && !(v & (1 << 2))) break;
        delay(5);
    }
    da_ = true;
    return true;
}

bool Nau7802::bereit() {
    if (!da_) return false;
    int v = lesen8(PU_CTRL);
    return v >= 0 && (v & (1 << CR));
}

long Nau7802::lesen() {
    Wire.beginTransmission(ADR);
    Wire.write(ADCO_B2);
    if (Wire.endTransmission(false) != 0) return 0;
    if (Wire.requestFrom(ADR, (uint8_t)3) != 3) return 0;
    uint32_t w = ((uint32_t)Wire.read() << 16) | ((uint32_t)Wire.read() << 8) | Wire.read();
    if (w & 0x800000) w |= 0xFF000000;  // Vorzeichen erweitern
    return (int32_t)w;
}
