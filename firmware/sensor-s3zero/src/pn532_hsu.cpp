#include "pn532_hsu.h"
#include "config.h"
#include "mbedtls/md.h"

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
    rst_ = rst;
    port_->begin(115200, SERIAL_8N1, rx, tx);
    if (rst_ >= 0) {
        pinMode(rst_, OUTPUT);
        digitalWrite(rst_, HIGH);
    }
    return einrichten();
}

bool Pn532Hsu::neu_starten() {
    if (!port_) return false;
    neustarts_++;
    return einrichten();
}

bool Pn532Hsu::einrichten() {
    fehler_ = 0;
    if (rst_ >= 0) {
        // RSTPDN kurz auf LOW: harter Reset, holt den PN532 auch aus einem Hänger
        digitalWrite(rst_, LOW);
        delay(20);
        digitalWrite(rst_, HIGH);
        delay(80);
    }
    while (port_->available()) port_->read();
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
    if (!befehl(liste, sizeof liste, a, n, 80)) {
        // Kein Tag ist kein Fehler (dann antwortet der Leser mit NbTg = 0).
        // Hier hat er gar nicht oder kaputt geantwortet: nach mehreren Fehlern gilt er als hängend.
        if (++fehler_ >= NFC_MAX_FEHLER) bereit_ = false;
        return "";
    }
    fehler_ = 0;
    // a: NbTg, Tg, SENS_RES(2), SEL_RES, NFCIDLength, NFCID…
    if (n < 6 || a[0] == 0) return "";
    uint8_t uid_len = a[5];
    if (uid_len == 0 || 6 + uid_len > n) return "";
    sak_ = a[4];
    uid_len_ = uid_len > sizeof uid_ ? sizeof uid_ : uid_len;
    memcpy(uid_, a + 6, uid_len_);
    String s;
    char h[3];
    for (uint8_t i = 0; i < uid_len; i++) {
        snprintf(h, sizeof h, "%02X", a[6 + i]);
        s += h;
    }
    return s;
}

// ---------------------------------------------------------------- Bambu-Lab-Tags
namespace {
// Schlüssel A je Sektor: HKDF-SHA256(UID, Salz, „RFID-A\0“) → 16 × 6 Bytes
// (github.com/Bambu-Research-Group/RFID-Tag-Guide, deriveKeys.py)
const uint8_t BAMBU_SALZ[16] = {0x9a, 0x75, 0x9c, 0xf2, 0xc4, 0xf7, 0xca, 0xff,
                                0x22, 0x2c, 0xb9, 0x76, 0x9b, 0x41, 0xbc, 0x96};

void hkdf_sha256(const uint8_t* ikm, size_t ikm_len, uint8_t* aus, size_t aus_len) {
    const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    uint8_t prk[32], t[32];
    mbedtls_md_hmac(md, BAMBU_SALZ, sizeof BAMBU_SALZ, ikm, ikm_len, prk);
    const uint8_t info[7] = {'R', 'F', 'I', 'D', '-', 'A', 0};
    size_t fertig = 0, t_len = 0;
    for (uint8_t i = 1; fertig < aus_len; i++) {
        uint8_t ein[32 + sizeof info + 1];
        memcpy(ein, t, t_len);
        memcpy(ein + t_len, info, sizeof info);
        ein[t_len + sizeof info] = i;
        mbedtls_md_hmac(md, prk, sizeof prk, ein, t_len + sizeof info + 1, t);
        t_len = 32;
        size_t n = aus_len - fertig < 32 ? aus_len - fertig : 32;
        memcpy(aus + fertig, t, n);
        fertig += n;
    }
}

String text_aus(const uint8_t* d, size_t n) {
    String s;
    for (size_t i = 0; i < n && d[i]; i++)
        if (d[i] >= 0x20 && d[i] < 0x7f) s += (char)d[i];
    s.trim();
    return s;
}
}  // namespace

bool Pn532Hsu::mifare_auth(uint8_t block, const uint8_t* key) {
    uint8_t cmd[14] = {0x40, 0x01, 0x60, block};  // InDataExchange, Ziel 1, Auth mit Schlüssel A, Schlüssel, UID
    memcpy(cmd + 4, key, 6);
    memcpy(cmd + 10, uid_, 4);
    uint8_t a[4];
    uint8_t n = sizeof a;
    return befehl(cmd, sizeof cmd, a, n, 100) && n >= 1 && a[0] == 0x00;
}

bool Pn532Hsu::mifare_lesen(uint8_t block, uint8_t* daten16) {
    const uint8_t cmd[] = {0x40, 0x01, 0x30, block};
    uint8_t a[20];
    uint8_t n = sizeof a;
    if (!befehl(cmd, sizeof cmd, a, n, 100) || n < 17 || a[0] != 0x00) return false;
    memcpy(daten16, a + 1, 16);
    return true;
}

void Pn532Hsu::feld_neu() {
    // HF-Feld kurz aus und an: Der Tag startet frisch und wird bei der nächsten Suche wie gewohnt gefunden
    uint8_t a[4];
    uint8_t n = sizeof a;
    const uint8_t aus[] = {0x32, 0x01, 0x00};
    befehl(aus, sizeof aus, a, n, 50);
    delay(5);
    n = sizeof a;
    const uint8_t an[] = {0x32, 0x01, 0x01};
    befehl(an, sizeof an, a, n, 50);
}

bool Pn532Hsu::lese_bambu(BambuTag& t) {
    // Bambu Lab nutzt MIFARE Classic 1K (SAK 0x08) mit 4-Byte-UID
    if (!bereit_ || uid_len_ != 4 || (sak_ & 0x18) != 0x08) return false;
    uint8_t keys[96];
    hkdf_sha256(uid_, 4, keys, sizeof keys);
    uint8_t b1[16], b2[16], b4[16], b5[16];
    bool ok = mifare_auth(1, keys) && mifare_lesen(1, b1) && mifare_lesen(2, b2) &&
              mifare_auth(4, keys + 6) && mifare_lesen(4, b4) && mifare_lesen(5, b5);
    feld_neu();
    if (!ok) return false;
    t.variante = text_aus(b1, 8);
    t.material_id = text_aus(b1 + 8, 8);
    t.typ = text_aus(b2, 16);
    t.detail = text_aus(b4, 16);
    char h[9];
    snprintf(h, sizeof h, "%02X%02X%02X%02X", b5[0], b5[1], b5[2], b5[3]);
    t.farbe = h;
    t.gewicht = (uint16_t)(b5[4] | (b5[5] << 8));
    memcpy(&t.durchmesser, b5 + 8, 4);
    // Ohne Materialangabe ist es kein (gültiger) Bambu-Tag
    return t.material_id.length() > 0 || t.typ.length() > 0;
}
