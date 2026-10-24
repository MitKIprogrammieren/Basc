// FilamentLager-Waage – Sensorplatine (Waveshare ESP32-S3-Zero)
//
// Misst das Gewicht (NAU7802 über I²C) und liest NFC-Tags mit zwei PN532 im HSU-Modus.
// Antwortet auf Anfragen über USB (Raspberry Pi), RS485 (5"-Display) und WLAN (Pad 7).
// Zusätzlich die komplette Waagen-Oberfläche unter http://filamentwaage.local (oberflaeche.cpp).
// Protokoll: eine JSON-Zeile pro Nachricht, siehe docs/PROTOKOLL.md.

#include <Arduino.h>

#include "config.h"

// Zustands-LED: Waveshare S3-Zero (GPIO21) und Super Mini (GPIO48) – an beide senden, die jeweils andere ist unbelegt.
// Eigener RMT-Kanal je Pin: neopixelWrite() des Arduino-Kerns 2.x bedient nur den ersten Pin, den es je gesehen hat.
#include "esp32-hal-rmt.h"
static void led_farbe(uint8_t r, uint8_t g, uint8_t b) {
    static rmt_obj_t* kanal[2] = {nullptr, nullptr};
    static bool versucht = false;
    const int pins[2] = {LED_PIN, LED_PIN2};
    if (!versucht) {
        versucht = true;
        for (int i = 0; i < 2; i++) {
            kanal[i] = rmtInit(pins[i], RMT_TX_MODE, RMT_MEM_64);
            if (kanal[i]) rmtSetTick(kanal[i], 100);  // 100 ns je Takt
        }
    }
    rmt_data_t daten[24];
    const uint8_t farbe[3] = {g, r, b};  // WS2812: Grün, Rot, Blau
    int n = 0;
    for (int f = 0; f < 3; f++)
        for (int bit = 7; bit >= 0; bit--) {
            bool eins = farbe[f] & (1 << bit);
            daten[n].level0 = 1;
            daten[n].duration0 = eins ? 8 : 4;
            daten[n].level1 = 0;
            daten[n].duration1 = eins ? 4 : 8;
            n++;
        }
    for (int i = 0; i < 2; i++)
        if (kanal[i]) rmtWrite(kanal[i], daten, 24);
}
#include <ArduinoJson.h>
#include "nau7802.h"
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#if WLAN_AKTIV
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiManager.h>

#include "oberflaeche.h"
#endif

#include <algorithm>
#include <mutex>
#include <utility>

#include "pn532_hsu.h"
#include "sensor.h"
#include "drucker.h"

namespace {

Nau7802 g_nau;  // Wägezellen-Wandler an WAAGE_SDA/WAAGE_SCL
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

uint32_t g_letzte_messung = 0;

void messen(long roh) {
    g_letzte_messung = millis();
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
Stream* g_antwort_an = nullptr;  // nullptr = von der Oberfläche gestartet
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
    char puf[300];  // reicht auch für den WLAN-Befehl mit langem Passwort
    size_t n = 0;
};

Kanal g_usb{&Serial, -1, {}, 0};
Kanal g_bus{&Serial0, RS485_DE, {}, 0};
#if WLAN_AKTIV
WiFiServer g_server(WLAN_PORT);
WiFiClient g_client;  // immer nur eine Gegenstelle; eine neue löst die alte ab
Kanal g_wlan{&g_client, -1, {}, 0};
WiFiManager g_wm;
uint32_t g_setup_wlan_ab = 0;  // ab wann das Setup-WLAN öffnen darf (0 = nicht mehr)
bool g_oberflaeche_laeuft = false;
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
        const char* fehler = vorgang_starten(!strcmp(c, "kal"), doc["g"] | 0, s);
        if (fehler) antwort(s, false, c, fehler);
    } else if (!strcmp(c, "info")) {
        JsonDocument d;
        d["t"] = "info";
        d["fw"] = FW_VERSION;
        d["nfc_l"] = g_nfc_l.bereit();
        d["nfc_r"] = g_nfc_r.bereit();
        d["hx711"] = g_nau.da();  // Feldname bleibt für ältere Gegenstellen: „Wandler gefunden“
        d["waage_chip"] = g_nau.da() ? "NAU7802" : "keiner";
        d["roh"] = g_roh_median;
        d["offset"] = g_offset;
        d["faktor"] = g_faktor;
#if WLAN_AKTIV
        d["wlan"] = WiFi.status() == WL_CONNECTED;
        d["ssid"] = WiFi.SSID();
        d["ip"] = WiFi.localIP().toString();
#endif
        senden(s, d);
    } else if (!strcmp(c, "wlan")) {
        // vom Webflasher: {"c":"wlan","ssid":"…","pw":"…"} – speichert das Heim-WLAN, kein Setup-WLAN nötig
#if WLAN_AKTIV
        const char* ssid = doc["ssid"] | "";
        const char* pw = doc["pw"] | "";
        if (!*ssid || strlen(ssid) > 32 || strlen(pw) > 63) {
            antwort(s, false, c, "WLAN-Name oder Passwort ungültig");
            return;
        }
        wlan_setzen(ssid, pw);
        antwort(s, true, c);
#else
        antwort(s, false, c, "WLAN ist abgeschaltet");
#endif
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
        if (g_vorgang_fertig) g_vorgang_fertig(true, "Tara gesetzt");
    } else {
        float f = (float)(mittel - g_offset) / (float)g_kal_gramm;
        if (fabsf(f) < 1.0f) {
            antwort(g_antwort_an, false, "kal", "kein Gewicht erkannt – Tara gesetzt?");
            if (g_vorgang_fertig) g_vorgang_fertig(false, "Kein Gewicht erkannt – erst Tara bei leerem Teller, dann Gewicht auflegen");
        } else {
            g_faktor = f;
            g_gramm_gueltig = false;
            g_verlauf_anzahl = 0;
            kal_speichern();
            antwort(g_antwort_an, true, "kal");
            if (g_vorgang_fertig) g_vorgang_fertig(true, "Kalibriert mit " + String(g_kal_gramm) + " g");
        }
    }
    g_vorgang = Vorgang::KEINER;
}

// ---------------------------------------------------------------- WLAN
#if WLAN_AKTIV
void wlan_start() {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(WLAN_HOSTNAME);
    // Ohne Stromsparen antwortet das WLAN schneller – mit Bluetooth zusammen verlangt der ESP32 aber
    // den Modem-Schlaf, sonst bricht er beim WLAN-Start ab („Should enable WiFi modem sleep“).
    WiFi.setSleep(drucker_gesperrt() ? WIFI_PS_NONE : WIFI_PS_MIN_MODEM);
    g_wm.setConfigPortalBlocking(false);  // Waage misst weiter, während das Setup-WLAN offen ist
    g_wm.setConfigPortalTimeout(0);
    g_wm.setTitle("FilamentLager-Waage");
    g_wm.setShowInfoUpdate(false);
    // Nicht blockieren: Das Setup läuft weiter (USB-Befehle des Webflashers, Messen), während das WLAN verbindet.
    // Das Setup-WLAN öffnet erst, wenn nach 30 s noch keine Verbindung steht – oder sofort, wenn keins gespeichert ist.
    g_prefs.begin("waage", true);
    bool leise = g_prefs.getBool("wlan_leise", false);  // hat beim letzten Mal nur mit wenig Sendeleistung geklappt
    g_prefs.end();
    if (g_wm.getWiFiIsSaved()) {
        if (leise) WiFi.setTxPower(WIFI_POWER_8_5dBm);
        WiFi.begin();
        g_setup_wlan_ab = millis() + 30000;
    } else {
        g_setup_wlan_ab = millis();
    }
    g_server.begin();
    g_server.setNoDelay(true);
}

void wlan_weiter() {
    g_wm.process();
    if (g_setup_wlan_ab && WiFi.status() != WL_CONNECTED && (int32_t)(millis() - g_setup_wlan_ab) >= 0 &&
        !g_wm.getConfigPortalActive()) {
        g_setup_wlan_ab = 0;
        Serial.println("WLAN nicht erreichbar – Setup-WLAN „" WLAN_SETUP_NAME "“ geöffnet");
        g_wm.startConfigPortal(WLAN_SETUP_NAME);
    }
    if (WiFi.status() == WL_CONNECTED) g_setup_wlan_ab = 0;
    // Ein fehlgeschlagener Verbindungsversuch wird vom ESP32 nicht von selbst wiederholt (früher tat das der
    // WiFiManager). Alle 10 s neu versuchen; ab dem 3. Versuch mit weniger Sendeleistung – kleine Boards wie der
    // ESP32-S3 Super Mini verbinden sich mit voller Leistung oft nicht (Antenne zu nah am Spannungsregler).
    static uint32_t letzter_versuch = millis();
    static int versuche = 0;
    if (WiFi.status() == WL_CONNECTED) {
        if (versuche) Serial.printf("WLAN verbunden nach %d Wiederholung(en)\n", versuche);
        if (versuche >= 2) {  // erst mit weniger Sendeleistung geklappt: beim nächsten Start gleich so
            g_prefs.begin("waage", false);
            g_prefs.putBool("wlan_leise", true);
            g_prefs.end();
        }
        versuche = 0;
        letzter_versuch = millis();
    } else if (millis() - letzter_versuch > 10000 && g_wm.getWiFiIsSaved()) {
        letzter_versuch = millis();
        versuche++;
        Serial.printf("WLAN: Versuch %d, Status %d\n", versuche + 1, (int)WiFi.status());
        if (versuche == 2) WiFi.setTxPower(WIFI_POWER_8_5dBm);
        WiFi.disconnect(false);
        WiFi.begin();
    }
    static bool mdns = false;
    if (WiFi.status() == WL_CONNECTED && !mdns && MDNS.begin(WLAN_HOSTNAME)) {
        MDNS.addService("filamentwaage", "tcp", WLAN_PORT);
        MDNS.addService("http", "tcp", WEB_PORT);
        mdns = true;
    }
    // Hat der Start-Versuch nicht geklappt, öffnet der WiFiManager sein Setup-WLAN – das blieb bisher offen, auch wenn
    // das Heim-WLAN kurz darauf doch verbunden war, und die Oberfläche startete nie. Jetzt: verbunden → Setup schließen.
    static uint32_t verbunden_seit = 0;
    if (g_wm.getConfigPortalActive() && WiFi.status() == WL_CONNECTED) {
        if (!verbunden_seit) verbunden_seit = millis();
        else if (millis() - verbunden_seit > 15000) {
            Serial.println("WLAN verbunden – Setup-WLAN wird geschlossen, Oberfläche startet");
            g_wm.stopConfigPortal();
            verbunden_seit = 0;
        }
    } else {
        verbunden_seit = 0;
    }
    // Oberfläche erst starten, wenn das Setup-WLAN zu ist (WiFiManager belegt dort selbst Port 80)
    if (!g_oberflaeche_laeuft && WiFi.status() == WL_CONNECTED && !g_wm.getConfigPortalActive()) {
        oberflaeche_start();
        g_oberflaeche_laeuft = true;
    }
    if (g_oberflaeche_laeuft) oberflaeche_weiter();
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
            led_farbe(14, 0, 14);
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
    if (!wlan_verbunden()) led_farbe(blink ? 14 : 0, blink ? 6 : 0, 0);  // orange blinkend: kein WLAN
    else if (g_faktor == 0) led_farbe(12, 0, 0);  // rot: nicht kalibriert
    else if (tag) led_farbe(0, 14, 0);            // grün: Tag erkannt
    else led_farbe(0, 0, 6);                      // blau: bereit
}

}  // namespace

// ---------------------------------------------------------------- Startprotokoll
// Überlebt Neustarts (nicht das Abschalten): welcher Startschritt zuletzt lief und wie oft die Waage
// kurz hintereinander neu gestartet ist. Ab dem zweiten schnellen Neustart startet sie ohne Bluetooth.
RTC_NOINIT_ATTR uint32_t g_rtc_marke, g_rtc_schritt, g_rtc_neustarts;
constexpr uint32_t RTC_GUELTIG = 0x57414147;  // "WAAG"
const char* const SCHRITTE[] = {"–", "Start", "Bluetooth", "Speicher", "USB/RS485", "Waage", "NFC", "WLAN", "läuft"};
uint32_t g_vorher_schritt = 0, g_neustarts = 0;
bool g_sicherer_start = false;
String g_start_grund;

void schritt(uint32_t n) {
    g_rtc_schritt = n;
    if (n >= 4) Serial.printf("# Start: %s\n", SCHRITTE[n]);
}

const char* grund_text(esp_reset_reason_t g) {
    switch (g) {
        case ESP_RST_POWERON: return "eingeschaltet";
        case ESP_RST_SW: return "neu gestartet";
        case ESP_RST_PANIC: return "Absturz";
        case ESP_RST_INT_WDT: return "Watchdog (Interrupt)";
        case ESP_RST_TASK_WDT: return "Watchdog (Task)";
        case ESP_RST_WDT: return "Watchdog";
        case ESP_RST_BROWNOUT: return "Unterspannung";
        default: return "anderer Grund";
    }
}

String start_bericht() {
    return "grund=" + g_start_grund + ", letzter Schritt davor=" + SCHRITTE[g_vorher_schritt < 9 ? g_vorher_schritt : 0] +
           ", schnelle Neustarts=" + String(g_neustarts) + (g_sicherer_start ? ", ohne Bluetooth gestartet" : "");
}

// ---------------------------------------------------------------- für die Oberfläche (sensor.h)
void (*g_vorgang_fertig)(bool ok, const String& text) = nullptr;

Messwert messwert() {
    Messwert m;
    m.gramm = roundf(g_gramm * 10) / 10;
    m.stabil = g_stabil;
    m.kalibriert = g_faktor != 0;
    m.hx711_ok = g_letzte_messung && millis() - g_letzte_messung < 1000;
    std::lock_guard<std::mutex> l(g_nfc_mx);
    m.tag_l = g_tag[0];
    m.tag_r = g_tag[1];
    return m;
}

bool nfc_leser_bereit(int seite) { return seite == 0 ? g_nfc_l.bereit() : g_nfc_r.bereit(); }

const char* vorgang_starten(bool kal, int gramm, Stream* s) {
    if (g_vorgang != Vorgang::KEINER) return "beschäftigt";
    if (kal) {
        if (gramm < 50) return "Kalibriergewicht fehlt";
        g_kal_gramm = gramm;
        g_vorgang = Vorgang::KAL;
    } else {
        g_vorgang = Vorgang::TARA;
    }
    g_summe = 0;
    g_proben = 0;
    g_antwort_an = s;
    return nullptr;
}

#if WLAN_AKTIV
void wlan_setzen(const char* ssid, const char* pw) {
    if (g_wm.getConfigPortalActive()) g_wm.stopConfigPortal();
    WiFi.persistent(true);  // bleibt nach dem Neustart gespeichert, WiFiManager nutzt es beim nächsten Start
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    WiFi.begin(ssid, pw);
    g_setup_wlan_ab = millis() + 30000;  // Setup-WLAN nur, wenn auch diese Daten nicht klappen
}

bool pad_verbunden() { return (bool)g_client && g_client.connected(); }

void alles_zuruecksetzen() {
    g_offset = 0;
    g_faktor = 0;
    kal_speichern();
    g_wm.resetSettings();
}
#else
void wlan_setzen(const char*, const char*) {}
bool pad_verbunden() { return false; }
void alles_zuruecksetzen() {
    g_offset = 0;
    g_faktor = 0;
    kal_speichern();
}
#endif

// NAU7802 suchen – auch mit vertauschtem SDA/SCL. Beim Start mehrfach, danach regelmäßig aus loop(),
// damit ein später angestecktes Modul ohne Neustart erkannt wird.
void waage_suchen(bool beim_start) {
    static bool gemeldet = false;
    for (int v = 0; v < (beim_start ? 3 : 1); v++) {
        if (v) delay(100);
        if (g_nau.begin(WAAGE_SDA, WAAGE_SCL)) {
            Serial.println("# Waage: NAU7802 gefunden (SDA=GPIO" + String(WAAGE_SDA) + ", SCL=GPIO" + String(WAAGE_SCL) + ")");
            return;
        }
        if (g_nau.begin(WAAGE_SCL, WAAGE_SDA)) {
            Serial.println("# Waage: NAU7802 gefunden – SDA und SCL sind vertauscht angeschlossen, geht trotzdem");
            return;
        }
    }
    if (!gemeldet) {
        gemeldet = true;
        Serial.println("# Waage: kein NAU7802 an GPIO" + String(WAAGE_SDA) + "/" + String(WAAGE_SCL) +
                       " – Verkabelung prüfen (VIN 3V3, GND, SDA, SCL); suche alle 5 s weiter");
    }
}

void setup() {
    esp_reset_reason_t grund = esp_reset_reason();
    g_start_grund = grund_text(grund);
    if (g_rtc_marke != RTC_GUELTIG || grund == ESP_RST_POWERON) {
        g_rtc_marke = RTC_GUELTIG;
        g_rtc_neustarts = 0;
        g_rtc_schritt = 0;
    } else if (g_rtc_schritt != 8) {
        g_rtc_neustarts++;  // die letzte Runde kam nie bis „läuft“
    }
    g_vorher_schritt = g_rtc_schritt;
    g_neustarts = g_rtc_neustarts;
    g_sicherer_start = g_neustarts >= 2;

    Serial.setTxTimeoutMs(0);  // ohne angeschlossenen Rechner nicht warten
    Serial.begin(115200);
    schritt(1);
    Serial.printf("# FilamentLager-Waage %s: %s\n", FW_VERSION, start_bericht().c_str());
    // Bluetooth zuerst, solange alles im internen Speicher landet – aber nicht, wenn die Waage gerade
    // mehrmals hintereinander beim Start neu gestartet ist
    schritt(2);
    drucker_frueh(!g_sicherer_start);
    delay(300);  // Strom nach dem Bluetooth-Start erst beruhigen lassen, dann WLAN
    // Danach alles ab 256 Byte ins PSRAM: Das Lager (Hunderte Sorten) würde sonst den internen
    // Speicher füllen, den WLAN und der Webserver brauchen.
    schritt(3);
    if (psramFound()) heap_caps_malloc_extmem_enable(256);
    schritt(4);
    Serial0.setRxBufferSize(512);
    Serial0.begin(RS485_BAUD, SERIAL_8N1, RS485_RX, RS485_TX);
    if (RS485_DE >= 0) {
        pinMode(RS485_DE, OUTPUT);
        digitalWrite(RS485_DE, LOW);
    }

    schritt(5);
    kal_laden();
    waage_suchen(true);
    schritt(6);

    bool r = g_nfc_r.begin(Serial1, NFC_R_RX, NFC_R_TX, NFC_R_RST);
    bool l = g_nfc_l.begin(Serial2, NFC_L_RX, NFC_L_TX, NFC_L_RST);
    xTaskCreatePinnedToCore(nfc_task, "nfc", 4096, nullptr, 1, nullptr, 0);
#if WLAN_AKTIV
    pinMode(BOOT_TASTE, INPUT_PULLUP);
    schritt(7);
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
    // 15 s ohne Neustart: gilt als gut gestartet
    if (g_rtc_schritt != 8 && millis() > 15000) {
        g_rtc_schritt = 8;
        g_rtc_neustarts = 0;
        Serial.println("# Start: läuft");
    }
    static uint32_t nau_frage = 0, nau_suche = 0;  // NAU7802 alle 10 ms fragen; fehlt er, alle 5 s neu suchen
    bool neu = false;
    if (g_nau.da()) {
        if (millis() - nau_frage >= 10) { nau_frage = millis(); neu = g_nau.bereit(); }
    } else if (millis() - nau_suche >= 5000) {
        nau_suche = millis();
        waage_suchen(false);
    }
    if (neu) {
        long roh = g_nau.lesen();
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
