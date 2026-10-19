// FilamentLager-Waage – Sensorplatine (Waveshare ESP32-S3-Zero)
//
// Misst das Gewicht (HX711) und liest NFC-Tags mit zwei PN532 im HSU-Modus.
// Antwortet auf Anfragen über USB (Raspberry Pi), RS485 (5"-Display) und WLAN (Pad 7).
// Protokoll: eine JSON-Zeile pro Nachricht, siehe docs/PROTOKOLL.md.

#include <Arduino.h>

#include "config.h"
#include <ArduinoJson.h>
#include <HX711.h>
#include <Preferences.h>
#if WLAN_AKTIV
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiManager.h>
#endif

#include <algorithm>
#include <mutex>
#include <utility>

#include "pn532_hsu.h"

namespace {

HX711 g_hx;
Preferences g_prefs;
Pn532Hsu g_nfc_r, g_nfc_l;

// ---------------------------------------------------------------- Kalibrierung
long g_offset = 0;
float g_faktor = 0;  // Rohwert pro Gramm, 0 = nicht kalibriert

void kal_laden() {
    g_prefs.begin("waage", true);
    g_offset = g_prefs.getLong("offset", 0);
    g_faktor = g_prefs.getFloat("faktor", 0);
    g_prefs.end();
}

void kal_speichern() {
    g_prefs.begin("waage", false);
    g_prefs.putLong("offset", g_offset);
    g_prefs.putFloat("faktor", g_faktor);
    g_prefs.end();
}

// ---------------------------------------------------------------- Messung
constexpr int MEDIAN_N = 5, STABIL_N = 10;
long g_roh[MEDIAN_N];
int g_roh_pos = 0, g_roh_anzahl = 0;
long g_roh_median = 0;
float g_gramm = 0;
bool g_gramm_gueltig = false;
float g_verlauf[STABIL_N];
int g_verlauf_pos = 0, g_verlauf_anzahl = 0;
bool g_stabil = false;

long median() {
    long t[MEDIAN_N];
    int n = g_roh_anzahl;
    for (int i = 0; i < n; i++) t[i] = g_roh[i];
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && t[j - 1] > t[j]; j--) std::swap(t[j - 1], t[j]);
    return t[n / 2];
}

void messen(long roh) {
    g_roh[g_roh_pos] = roh;
    g_roh_pos = (g_roh_pos + 1) % MEDIAN_N;
    if (g_roh_anzahl < MEDIAN_N) g_roh_anzahl++;
    g_roh_median = median();
    if (g_faktor == 0) {
        g_gramm = 0;
        g_stabil = false;
        return;
    }
    float neu = (g_roh_median - g_offset) / g_faktor;
    // große Sprünge (Spule aufgelegt) sofort übernehmen, sonst glätten
    if (!g_gramm_gueltig || fabsf(neu - g_gramm) > 30) g_gramm = neu;
    else g_gramm += GLAETTUNG * (neu - g_gramm);
    g_gramm_gueltig = true;
    g_verlauf[g_verlauf_pos] = g_gramm;
    g_verlauf_pos = (g_verlauf_pos + 1) % STABIL_N;
    if (g_verlauf_anzahl < STABIL_N) g_verlauf_anzahl++;
    float mn = 1e9, mx = -1e9;
    for (int i = 0; i < g_verlauf_anzahl; i++) {
        mn = std::min(mn, g_verlauf[i]);
        mx = std::max(mx, g_verlauf[i]);
    }
    g_stabil = g_verlauf_anzahl == STABIL_N && (mx - mn) < STABIL_BAND_G;
}

// Tara und Kalibrieren sammeln Rohwerte, ohne die Schleife zu blockieren
enum class Vorgang { KEINER, TARA, KAL } g_vorgang = Vorgang::KEINER;
int g_kal_gramm = 0;
int64_t g_summe = 0;
int g_proben = 0;
Stream* g_antwort_an = nullptr;
constexpr int PROBEN = 15;

// ---------------------------------------------------------------- NFC (eigene Task)
std::mutex g_nfc_mx;
String g_tag[2];  // 0 = links, 1 = rechts
uint32_t g_tag_zeit[2] = {0, 0};

void nfc_task(void*) {
    Pn532Hsu* leser[2] = {&g_nfc_l, &g_nfc_r};
    for (;;) {
        for (int i = 0; i < 2; i++) {
            if (!leser[i]->bereit()) continue;
            String uid = leser[i]->lese_uid();
            uint32_t jetzt = millis();
            std::lock_guard<std::mutex> l(g_nfc_mx);
            if (uid.length()) {
                g_tag[i] = uid;
                g_tag_zeit[i] = jetzt;
            } else if (g_tag[i].length() && jetzt - g_tag_zeit[i] > TAG_WEG_MS) {
                g_tag[i] = "";
            }
        }
        vTaskDelay(pdMS_TO_TICKS(40));
    }
}

// ---------------------------------------------------------------- Kommunikation
struct Kanal {
    Stream* s;
    int de;  // RS485-Senderfreigabe, -1 = keine
    char puf[160];
    size_t n = 0;
};

Kanal g_usb{&Serial, -1, {}, 0};
Kanal g_bus{&Serial0, RS485_DE, {}, 0};
#if WLAN_AKTIV
WiFiServer g_server(WLAN_PORT);
WiFiClient g_client;  // immer nur eine Gegenstelle; eine neue löst die alte ab
Kanal g_wlan{&g_client, -1, {}, 0};
WiFiManager g_wm;
#endif

void senden(Stream* s, const JsonDocument& doc) {
    if (!s) return;
#if WLAN_AKTIV
    if (s == g_wlan.s) {
        if (!g_client.connected()) return;
        String z;
        serializeJson(doc, z);
        z += '\n';
        g_client.write((const uint8_t*)z.c_str(), z.length());  // in einem Paket
        return;
    }
#endif
    Kanal* k = (s == g_bus.s) ? &g_bus : &g_usb;
    if (k->de >= 0) digitalWrite(k->de, HIGH);
    serializeJson(doc, *s);
    s->print('\n');
    if (k->de >= 0) {
        s->flush();  // erst wenn alles raus ist, den Sender abschalten
        digitalWrite(k->de, LOW);
    }
}

void antwort(Stream* s, bool ok, const char* c, const char* msg = nullptr) {
    JsonDocument d;
    d["t"] = ok ? "ok" : "err";
    d["c"] = c;
    if (msg) d["msg"] = msg;
    senden(s, d);
}

void status_senden(Stream* s) {
    JsonDocument d;
    d["t"] = "s";
    d["g"] = roundf(g_gramm * 10) / 10;
    d["st"] = g_stabil ? 1 : 0;
    d["cal"] = g_faktor != 0 ? 1 : 0;
    {
        std::lock_guard<std::mutex> l(g_nfc_mx);
        d["L"] = g_tag[0];
        d["R"] = g_tag[1];
    }
    senden(s, d);
}

void befehl(Stream* s, const char* zeile) {
    JsonDocument doc;
    if (deserializeJson(doc, zeile)) return;
    const char* c = doc["c"] | "";
    if (!strcmp(c, "p")) {
        status_senden(s);
    } else if (!strcmp(c, "tara") || !strcmp(c, "kal")) {
        if (g_vorgang != Vorgang::KEINER) {
            antwort(s, false, c, "beschäftigt");
            return;
        }
        if (!strcmp(c, "kal")) {
            g_kal_gramm = doc["g"] | 0;
            if (g_kal_gramm < 50) {
                antwort(s, false, c, "Kalibriergewicht fehlt");
                return;
            }
            g_vorgang = Vorgang::KAL;
        } else {
            g_vorgang = Vorgang::TARA;
        }
        g_summe = 0;
        g_proben = 0;
        g_antwort_an = s;
    } else if (!strcmp(c, "info")) {
        JsonDocument d;
        d["t"] = "info";
        d["fw"] = FW_VERSION;
        d["nfc_l"] = g_nfc_l.bereit();
        d["nfc_r"] = g_nfc_r.bereit();
        d["hx711"] = g_hx.is_ready();
        d["roh"] = g_roh_median;
        d["offset"] = g_offset;
        d["faktor"] = g_faktor;
        senden(s, d);
    }
}

void kanal_lesen(Kanal& k) {
    while (k.s->available()) {
        char c = (char)k.s->read();
        if (c == '\n' || c == '\r') {
            if (k.n) {
                k.puf[k.n] = 0;
                if (k.puf[0] == '{') befehl(k.s, k.puf);
            }
            k.n = 0;
        } else if (k.n < sizeof(k.puf) - 1) {
            k.puf[k.n++] = c;
        } else {
            k.n = 0;
        }
    }
}

void vorgang_weiter(long roh) {
    if (g_vorgang == Vorgang::KEINER) return;
    g_summe += roh;
    if (++g_proben < PROBEN) return;
    long mittel = (long)(g_summe / g_proben);
    if (g_vorgang == Vorgang::TARA) {
        g_offset = mittel;
        g_gramm = 0;
        g_gramm_gueltig = false;
        g_verlauf_anzahl = 0;
        kal_speichern();
        antwort(g_antwort_an, true, "tara");
    } else {
        float f = (float)(mittel - g_offset) / (float)g_kal_gramm;
        if (fabsf(f) < 1.0f) {
            antwort(g_antwort_an, false, "kal", "kein Gewicht erkannt – Tara gesetzt?");
        } else {
            g_faktor = f;
            g_gramm_gueltig = false;
            g_verlauf_anzahl = 0;
            kal_speichern();
            antwort(g_antwort_an, true, "kal");
        }
    }
    g_vorgang = Vorgang::KEINER;
}

// ---------------------------------------------------------------- WLAN
#if WLAN_AKTIV
void wlan_start() {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(WLAN_HOSTNAME);
    WiFi.setSleep(false);  // sonst kommen die Antworten mit Verzögerung
    g_wm.setConfigPortalBlocking(false);  // Waage misst weiter, während das Setup-WLAN offen ist
    g_wm.setConfigPortalTimeout(0);
    g_wm.setTitle("FilamentLager-Waage");
    g_wm.setShowInfoUpdate(false);
    g_wm.autoConnect(WLAN_SETUP_NAME);
    g_server.begin();
    g_server.setNoDelay(true);
}

void wlan_weiter() {
    g_wm.process();
    static bool mdns = false;
    if (WiFi.status() == WL_CONNECTED && !mdns && MDNS.begin(WLAN_HOSTNAME)) {
        MDNS.addService("filamentwaage", "tcp", WLAN_PORT);
        mdns = true;
    }
    WiFiClient neu = g_server.available();
    if (neu) {
        if (g_client) g_client.stop();
        g_client = neu;
        g_client.setNoDelay(true);
        g_wlan.n = 0;
        JsonDocument d;
        d["t"] = "hallo";
        d["fw"] = FW_VERSION;
        d["nfc_l"] = g_nfc_l.bereit();
        d["nfc_r"] = g_nfc_r.bereit();
        d["cal"] = g_faktor != 0 ? 1 : 0;
        senden(&g_client, d);
    }
    if (g_client && !g_client.connected()) g_client.stop();
    if (g_client) kanal_lesen(g_wlan);

    // BOOT-Taste 5 s gedrückt: WLAN-Daten löschen und neu starten
    static uint32_t gedrueckt_seit = 0;
    if (digitalRead(BOOT_TASTE) == LOW) {
        if (!gedrueckt_seit) gedrueckt_seit = millis();
        else if (millis() - gedrueckt_seit > 5000) {
            neopixelWrite(LED_PIN, 14, 0, 14);
            g_wm.resetSettings();
            delay(300);
            ESP.restart();
        }
    } else {
        gedrueckt_seit = 0;
    }
}

bool wlan_verbunden() { return WiFi.status() == WL_CONNECTED; }
#else
bool wlan_verbunden() { return true; }
#endif

void led() {
    static uint32_t zuletzt = 0;
    if (millis() - zuletzt < 200) return;
    zuletzt = millis();
    bool tag;
    {
        std::lock_guard<std::mutex> l(g_nfc_mx);
        tag = g_tag[0].length() || g_tag[1].length();
    }
    static bool blink = false;
    blink = !blink;
    if (!wlan_verbunden()) neopixelWrite(LED_PIN, blink ? 14 : 0, blink ? 6 : 0, 0);  // orange blinkend: kein WLAN
    else if (g_faktor == 0) neopixelWrite(LED_PIN, 12, 0, 0);  // rot: nicht kalibriert
    else if (tag) neopixelWrite(LED_PIN, 0, 14, 0);            // grün: Tag erkannt
    else neopixelWrite(LED_PIN, 0, 0, 6);                      // blau: bereit
}

}  // namespace

void setup() {
    Serial.begin(115200);
    Serial0.setRxBufferSize(512);
    Serial0.begin(RS485_BAUD, SERIAL_8N1, RS485_RX, RS485_TX);
    if (RS485_DE >= 0) {
        pinMode(RS485_DE, OUTPUT);
        digitalWrite(RS485_DE, LOW);
    }

    kal_laden();
    g_hx.begin(HX711_DOUT, HX711_SCK);

    bool r = g_nfc_r.begin(Serial1, NFC_R_RX, NFC_R_TX, NFC_R_RST);
    bool l = g_nfc_l.begin(Serial2, NFC_L_RX, NFC_L_TX, NFC_L_RST);
    xTaskCreatePinnedToCore(nfc_task, "nfc", 4096, nullptr, 1, nullptr, 0);
#if WLAN_AKTIV
    pinMode(BOOT_TASTE, INPUT_PULLUP);
    wlan_start();
#endif

    JsonDocument d;
    d["t"] = "hallo";
    d["fw"] = FW_VERSION;
    d["nfc_l"] = l;
    d["nfc_r"] = r;
    d["cal"] = g_faktor != 0 ? 1 : 0;
    if (Serial) senden(&Serial, d);  // nur wenn ein Rechner per USB verbunden ist
}

void loop() {
    if (g_hx.is_ready()) {
        long roh = g_hx.read();
        messen(roh);
        vorgang_weiter(roh);
    }
    kanal_lesen(g_usb);
    kanal_lesen(g_bus);
#if WLAN_AKTIV
    wlan_weiter();
#endif
    led();
    delay(2);
}
