#include "pn532_hsu.h"

namespace {
constexpr uint8_t TFI_HOST = 0xD4;
constexpr uint8_t TFI_PN532 = 0xD5;
}

int Pn532Hsu::lies_byte(uint32_t bis) {
    while ((int32_t)(bis - millis()) > 0) {
        if (port_->available()) return port_->read();
        delayMicroseconds(100);
    }
    return -1;
}

void Pn532Hsu::aufwecken() {
    // im HSU-Modus schläft der PN532 nach dem Einschalten: 0x55 0x55 + Nullen weckt ihn
    const uint8_t weck[] = {0x55, 0x55, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    port_->write(weck, sizeof weck);
    port_->flush();
    delay(2);
}

bool Pn532Hsu::warte_ack(uint32_t timeout_ms) {
    const uint8_t ack[6] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
    uint32_t bis = millis() + timeout_ms;
    uint8_t pos = 0;
    while (pos < 6) {
        int b = lies_byte(bis);
        if (b < 0) return false;
        if (b == ack[pos]) pos++;
        else pos = (b == 0x00) ? 1 : 0;
    }
    return true;
}

bool Pn532Hsu::befehl(const uint8_t* daten, uint8_t laenge, uint8_t* antwort, uint8_t& antwort_laenge,
                      uint32_t timeout_ms) {
    while (port_->available()) port_->read();

    uint8_t len = laenge + 1;
    uint8_t summe = TFI_HOST;
    uint8_t kopf[] = {0x00, 0x00, 0xFF, len, (uint8_t)(0x100 - len), TFI_HOST};
    for (uint8_t i = 0; i < laenge; i++) summe += daten[i];
    uint8_t fuss[] = {(uint8_t)(0x100 - summe), 0x00};
    port_->write(kopf, sizeof kopf);
    port_->write(daten, laenge);
    port_->write(fuss, sizeof fuss);
    port_->flush();

    if (!warte_ack(30)) return false;

    // Antwort: 00 00 FF LEN LCS D5 CMD+1 DATEN… DCS 00
    uint32_t bis = millis() + timeout_ms;
    uint8_t pos = 0;
    while (pos < 3) {
        int b = lies_byte(bis);
        if (b < 0) return false;
        const uint8_t start[3] = {0x00, 0x00, 0xFF};
        if (b == start[pos]) pos++;
        else pos = (b == 0x00) ? 1 : 0;
    }
    int l = lies_byte(bis), lcs = lies_byte(bis);
    if (l < 2 || lcs < 0 || (uint8_t)(l + lcs) != 0) return false;
    int tfi = lies_byte(bis), cmd = lies_byte(bis);
    if (tfi != TFI_PN532 || cmd != daten[0] + 1) return false;
    uint8_t n = (uint8_t)l - 2;
    summe = TFI_PN532 + (uint8_t)cmd;
    for (uint8_t i = 0; i < n; i++) {
        int b = lies_byte(bis);
        if (b < 0) return false;
        if (i < antwort_laenge) antwort[i] = (uint8_t)b;
        summe += (uint8_t)b;
    }
    int dcs = lies_byte(bis);
    lies_byte(bis);  // Postambel
    if (dcs < 0 || (uint8_t)(summe + dcs) != 0) return false;
    antwort_laenge = n < antwort_laenge ? n : antwort_laenge;
    return true;
}

bool Pn532Hsu::begin(HardwareSerial& port, int rx, int tx, int rst) {
    port_ = &port;
    port_->begin(115200, SERIAL_8N1, rx, tx);
    if (rst >= 0) {
        pinMode(rst, OUTPUT);
        digitalWrite(rst, LOW);
        delay(20);
        digitalWrite(rst, HIGH);
        delay(80);
    }
    aufwecken();
    uint8_t a[8];
    uint8_t n = sizeof a;

    const uint8_t version[] = {0x02};
    if (!befehl(version, sizeof version, a, n, 100)) {
        aufwecken();
        n = sizeof a;
        if (!befehl(version, sizeof version, a, n, 100)) return bereit_ = false;
    }
    // SAM normal, kein IRQ
    const uint8_t sam[] = {0x14, 0x01, 0x14, 0x01};
    n = sizeof a;
    if (!befehl(sam, sizeof sam, a, n, 100)) return bereit_ = false;
    // Nur 2 Versuche pro Suche, damit InListPassiveTarget ohne Tag schnell zurückkommt
    const uint8_t retries[] = {0x32, 0x05, 0xFF, 0x01, 0x02};
    n = sizeof a;
    befehl(retries, sizeof retries, a, n, 100);
    return bereit_ = true;
}

String Pn532Hsu::lese_uid() {
    if (!bereit_) return "";
    const uint8_t liste[] = {0x4A, 0x01, 0x00};  // 1 Ziel, 106 kbps Typ A
    uint8_t a[24];
    uint8_t n = sizeof a;
    if (!befehl(liste, sizeof liste, a, n, 80)) return "";
    // a: NbTg, Tg, SENS_RES(2), SEL_RES, NFCIDLength, NFCID…
    if (n < 6 || a[0] == 0) return "";
    uint8_t uid_len = a[5];
    if (uid_len == 0 || 6 + uid_len > n) return "";
    String s;
    char h[3];
    for (uint8_t i = 0; i < uid_len; i++) {
        snprintf(h, sizeof h, "%02X", a[6 + i]);
        s += h;
    }
    return s;
}
