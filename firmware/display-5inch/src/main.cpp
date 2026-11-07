// FilamentLager-Waage – Display-Firmware für das Waveshare ESP32-S3-Touch-LCD-5 (env:ws5)
// und das WT32-SC01 Plus (env:sc01plus).
//
// Aufgaben:
//  - Oberfläche (LVGL) auf Kern 1 in loop()
//  - Sensorplatine über RS485 oder WLAN abfragen (Gewicht, NFC-Tags)
//  - Home Assistant (todo.filament) in einer eigenen Task auf Kern 0,
//    damit langsame HTTP-Anfragen die Oberfläche nicht einfrieren
//  - Netz-Task (Kern 0): AMS-Belegung aus Home Assistant, Etikettendrucker (Integration m20_druckbruecke),
//    Herstellerlogos (von der Sensorplatine bzw. aus Home Assistant) und Druckaufträge
//  - Einstellungen (WLAN, Home Assistant, Sensor-IP) im Flash; der Webflasher schickt sie per USB
//    ({"c":"wlan",…} und {"c":"einst",…}), secrets.h liefert nur noch Vorgaben für den ersten Start

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <mbedtls/base64.h>
#include <time.h>

#include <map>
#include <memory>
#include <set>

#include <deque>
#include <functional>
#include <mutex>

#include "ams.h"
#include <esp_heap_caps.h>

#include "board.h"
#include "config.h"
#include "etikett.h"
#include "logo.h"
#include "ha_client.h"
#include "lvgl.h"
#include "model.h"
#include "sensor_link.h"
#if SENSOR_DIREKT
#include <ESPmDNS.h>

#include "sensor_direkt.h"
#if DIREKT_WEB
#include "../../sensor-s3zero/src/drucker.h"
#include "../../sensor-s3zero/src/oberflaeche.h"
#endif
#include "../../sensor-s3zero/src/sensor.h"
#endif
#include "ui.h"

// secrets.h ist freiwillig: ohne sie kommen WLAN und Home Assistant über den Webflasher
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WLAN_SSID
#define WLAN_SSID ""
#define WLAN_PASS ""
#endif
#ifndef HA_URL
#define HA_URL ""
#define HA_TOKEN ""
#endif
#ifndef HA_TODO
#define HA_TODO "todo.filament"
#endif
#ifndef SENSOR_HOST
#define SENSOR_HOST ""
#endif

namespace {

// ---------------------------------------------------------------- Aufträge an Home Assistant
struct Auftrag {
    enum Art { LADEN, REST, ZUORDNEN, SPULEN } art;
    std::string sorte, tag;
    int wert = 0;
};

struct Ergebnis {
    bool hat_lager = false;
    Lager lager;
    std::string meldung;
    bool fehler = false;
    bool ha_ok = false;
    bool ha_ok_gilt = true;  // false: Ergebnis der Netz-Task, ändert den HA-Zustand nicht
    // aus der Netz-Task
    bool hat_ams = false;
    AmsStand ams;
    bool hat_et = false;
    EtikettEinst et;
    bool hat_logo = false;
    std::string logo_name;
    std::unique_ptr<Logo> logo;  // nullptr = kein Logo zu finden
};

// Aufträge an die Netz-Task
struct NetzAuftrag {
    enum Art { AMS, DRUCKER, LOGO, DRUCK } art;
    std::string name;  // Hersteller (LOGO), Titel (DRUCK)
    std::string drucker;
    int breite = 0, hoehe = 0;
    std::vector<uint8_t> raster;
};
std::deque<NetzAuftrag> g_netz;
TaskHandle_t g_netz_task = nullptr;

std::mutex g_mx;
std::deque<Auftrag> g_auftraege;
std::deque<Ergebnis> g_ergebnisse;
TaskHandle_t g_ha_task = nullptr;

Lager g_lager;  // gehört der Oberfläche, wird nur in loop() geändert
HaClient g_ha;
#if SENSOR_DIREKT
SensorDirekt g_sensor;  // NAU7802 und PN532 direkt am Display
#else
SensorLink g_sensor;
HardwareSerial g_rs485(1);
#endif
bool g_ha_ok = false;
uint32_t g_letztes_laden = 0;
AmsStand g_ams;  // gehört der Oberfläche
int g_drucker_status = -1;  // für die Kopfzeile
std::map<std::string, std::unique_ptr<Logo>> g_logos;  // fertig geladen (nullptr = keins gefunden)
std::set<std::string> g_logo_angefragt;
bool g_logo_neu = false;
uint32_t g_letztes_logo = 0;  // wann zuletzt ein Logo ankam
uint32_t g_lager_summe = 0;

// FNV-1a über alles, was die Anzeige eines Eintrags bestimmt
uint32_t lager_pruefsumme(const Lager& l) {
    uint32_t h = 2166136261u;
    auto dazu = [&](const std::string& t) {
        for (unsigned char c : t) h = (h ^ c) * 16777619u;
        h = (h ^ 0xff) * 16777619u;
    };
    for (auto& f : l.sorten) {
        dazu(f.uid);
        dazu(f.summary);
        dazu(f.description);
    }
    return h;
}
uint32_t g_letzte_logo_meldung = 0, g_letztes_ams = 0, g_letzte_drucker = 0;

// ---------------------------------------------------------------- Einstellungen
struct Einstellungen {
    String ssid, pw, ha_url, ha_token, ha_todo, sensor;
} g_einst;
Preferences g_prefs;

void einst_laden() {
    g_prefs.begin("anzeige", false);  // schreibend öffnen legt den Bereich beim ersten Start an
    g_einst.ssid = g_prefs.getString("ssid", WLAN_SSID);
    g_einst.pw = g_prefs.getString("pw", WLAN_PASS);
    g_einst.ha_url = g_prefs.getString("ha_url", HA_URL);
    g_einst.ha_token = g_prefs.getString("ha_token", HA_TOKEN);
    g_einst.ha_todo = g_prefs.getString("ha_todo", HA_TODO);
    g_einst.sensor = g_prefs.getString("sensor", SENSOR_HOST);
    g_prefs.end();
    while (g_einst.ha_url.endsWith("/")) g_einst.ha_url.remove(g_einst.ha_url.length() - 1);
}

void einst_speichern(const char* schluessel, const String& wert) {
    g_prefs.begin("anzeige", false);
    g_prefs.putString(schluessel, wert);
    g_prefs.end();
}

// ---------------------------------------------------------------- USB-Befehle (Webflasher)
char g_usb_puffer[2048];
size_t g_usb_laenge = 0;
uint32_t g_neustart_um = 0;

void usb_antwort(JsonDocument& d) {
    serializeJson(d, Serial);
    Serial.print('\n');
}

void usb_befehl(const char* zeile) {
    JsonDocument doc;
    if (deserializeJson(doc, zeile)) return;
    std::string c = doc["c"] | "";
    JsonDocument a;
    if (c == "info") {
        bool wlan = WiFi.status() == WL_CONNECTED;
        a["t"] = "info";
        a["art"] = "anzeige";
        a["fw"] = FW_VERSION;
        a["geraet"] = GERAETENAME;
        a["wlan"] = wlan;
        a["ip"] = wlan ? WiFi.localIP().toString() : String("0.0.0.0");
        if (wlan) a["rssi"] = WiFi.RSSI();
        a["ssid"] = g_einst.ssid;
        a["ha"] = g_ha_ok;
        a["ha_url"] = g_einst.ha_url;
        a["sensor"] = g_sensor.daten().verbunden;
        a["sensor_host"] = g_einst.sensor;
#if SENSOR_DIREKT
        a["sensor_art"] = "direkt";
        a["waage_chip"] = g_sensor.waage_da() ? "NAU7802" : "keiner";
        a["nfc_l"] = g_sensor.nfc_bereit(0);
        a["nfc_r"] = g_sensor.nfc_bereit(1);
        a["roh"] = g_sensor.roh();
#endif
    } else if (c == "wlan") {
        String ssid = doc["ssid"] | "", pw = doc["pw"] | "";
        a["c"] = "wlan";
        if (ssid.isEmpty()) {
            a["t"] = "err";
            a["msg"] = "WLAN-Name fehlt";
        } else {
            g_einst.ssid = ssid;
            g_einst.pw = pw;
            einst_speichern("ssid", ssid);
            einst_speichern("pw", pw);
            WiFi.disconnect();
            WiFi.begin(ssid.c_str(), pw.c_str());
            a["t"] = "ok";
        }
    } else if (c == "einst") {
        // Home Assistant und Sensor-IP: nur übergebene Felder ändern, danach Neustart
        if (doc["ha_url"].is<const char*>()) einst_speichern("ha_url", doc["ha_url"].as<const char*>());
        if (doc["ha_token"].is<const char*>()) einst_speichern("ha_token", doc["ha_token"].as<const char*>());
        if (doc["ha_todo"].is<const char*>()) einst_speichern("ha_todo", doc["ha_todo"].as<const char*>());
        if (doc["sensor"].is<const char*>()) einst_speichern("sensor", doc["sensor"].as<const char*>());
        a["t"] = "ok";
        a["c"] = "einst";
        g_neustart_um = millis() + 800;
    } else {
        return;
    }
    usb_antwort(a);
}

void usb_lesen() {
    while (Serial.available()) {
        char ch = (char)Serial.read();
        if (ch == '\n' || ch == '\r') {
            if (g_usb_laenge) {
                g_usb_puffer[g_usb_laenge] = 0;
                if (g_usb_puffer[0] == '{') usb_befehl(g_usb_puffer);
            }
            g_usb_laenge = 0;
        } else if (g_usb_laenge < sizeof(g_usb_puffer) - 1) {
            g_usb_puffer[g_usb_laenge++] = ch;
        } else {
            g_usb_laenge = 0;
        }
    }
    if (g_neustart_um && (int32_t)(millis() - g_neustart_um) >= 0) ESP.restart();
}

void auftrag(Auftrag a) {
    {
        std::lock_guard<std::mutex> l(g_mx);
        // doppeltes Neuladen vermeiden
        if (a.art == Auftrag::LADEN)
            for (auto& x : g_auftraege)
                if (x.art == Auftrag::LADEN) return;
        g_auftraege.push_back(std::move(a));
    }
    if (g_ha_task) xTaskNotifyGive(g_ha_task);
}

void ergebnis(Ergebnis e) {
    std::lock_guard<std::mutex> l(g_mx);
    g_ergebnisse.push_back(std::move(e));
}

// Änderung an einer Sorte: frisch laden, ändern, schreiben, neu laden
bool aendern(Ergebnis& e, const std::string& sorte, std::function<Aenderung(const std::string&)> f) {
    Lager frisch;
    std::string fehler;
    if (!g_ha.lager_laden(frisch, fehler)) {
        e.meldung = fehler;
        e.fehler = true;
        return false;
    }
    int i = frisch.finde_uid(sorte);
    if (i < 0) {
        e.meldung = "Sorte nicht mehr im Lager";
        e.fehler = true;
        return false;
    }
    Aenderung a = f(frisch.sorten[i].description);
    e.meldung = a.meldung;
    if (a.geaendert && !g_ha.beschreibung_setzen(sorte, a.beschreibung, fehler)) {
        e.meldung = fehler;
        e.fehler = true;
        return false;
    }
    return true;
}

void ha_task(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
        for (;;) {
            Auftrag a;
            {
                std::lock_guard<std::mutex> l(g_mx);
                if (g_auftraege.empty()) break;
                a = g_auftraege.front();
                g_auftraege.pop_front();
            }
            Ergebnis e;
            std::string fehler;
            switch (a.art) {
                case Auftrag::LADEN: break;
                case Auftrag::REST:
                    aendern(e, a.sorte, [&](const std::string& d) { return rest_speichern(d, a.wert, a.tag); });
                    break;
                case Auftrag::SPULEN:
                    aendern(e, a.sorte, [&](const std::string& d) { return spulen_aendern(d, a.wert); });
                    break;
                case Auftrag::ZUORDNEN: {
                    // Tag zuerst bei anderen Sorten entfernen
                    Lager frisch;
                    if (g_ha.lager_laden(frisch, fehler)) {
                        for (auto& s : frisch.sorten) {
                            if (s.uid != a.sorte && s.hat_nfc(a.tag)) {
                                Aenderung x = tag_entfernen(s.description, a.tag);
                                if (x.geaendert) g_ha.beschreibung_setzen(s.uid, x.beschreibung, fehler);
                            }
                        }
                    }
                    aendern(e, a.sorte, [&](const std::string& d) { return tag_zuordnen(d, a.tag); });
                    break;
                }
            }
            // nach jeder Änderung den neuen Stand holen
            if (g_ha.lager_laden(e.lager, fehler)) {
                e.hat_lager = true;
                e.ha_ok = true;
            } else if (a.art == Auftrag::LADEN && WiFi.status() != WL_CONNECTED) {
                // WLAN gerade weg: steht schon in der Kopfzeile, kein zusätzlicher Hinweis bei jedem Neuladen
            } else if (e.meldung.empty() || a.art == Auftrag::LADEN) {
                e.meldung = fehler;
                e.fehler = true;
            }
            if (!e.ha_ok)
                Serial.printf("[ha] Lager laden fehlgeschlagen: %s (Speicher intern frei %u, größter Block %u)\n",
                              fehler.c_str(), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                              (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
            ergebnis(std::move(e));
        }
    }
}

// ---------------------------------------------------------------- Netz-Task
void netz(NetzAuftrag a) {
    {
        std::lock_guard<std::mutex> l(g_mx);
        if (a.art == NetzAuftrag::AMS || a.art == NetzAuftrag::DRUCKER)
            for (auto& x : g_netz)
                if (x.art == a.art) return;  // steht schon an
        g_netz.push_back(std::move(a));
    }
    if (g_netz_task) xTaskNotifyGive(g_netz_task);
}

// HTTP-Anfrage an eine volle Adresse. Gibt den HTTP-Code zurück (≤ 0 = keine Verbindung).
int http_anfrage(const std::string& url, const char* methode, const std::string& body, std::string& antwort,
                 uint32_t timeout_ms, bool mit_token) {
    std::lock_guard<std::recursive_mutex> riegel(g_http_riegel);
    int code = -1;
    for (int versuch = 0; versuch < 3; versuch++) {
        if (versuch) delay(800 * versuch);
        if (WiFi.status() != WL_CONNECTED) continue;
        HTTPClient http;
        WiFiClient plain;
        WiFiClientSecure tls;
        bool ok;
        if (url.rfind("https://", 0) == 0) {
            tls.setInsecure();
            ok = http.begin(tls, url.c_str());
        } else {
            ok = http.begin(plain, url.c_str());
        }
        if (!ok) return -2;
        http.setTimeout(timeout_ms);
        http.setConnectTimeout(4000);
        if (mit_token && g_einst.ha_token.length()) http.addHeader("Authorization", "Bearer " + g_einst.ha_token);
        if (!strcmp(methode, "POST")) {
            http.addHeader("Content-Type", "application/json");
            code = http.POST((uint8_t*)body.data(), body.size());
        } else {
            code = http.GET();
        }
        if (code > 0) {
            String s = http.getString();
            antwort.assign(s.c_str(), s.length());
        }
        http.end();
        if (!http_neu_versuchen(code)) break;
    }
    return code;
}

std::string url_kodieren(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
        else {
            char h[4];
            snprintf(h, sizeof h, "%%%02X", c);
            o += h;
        }
    }
    return o;
}

std::string json_text(const std::string& s) {
    JsonDocument d;
    d.set(s);
    std::string o;
    serializeJson(d, o);
    return o;
}

// Druck-Schnittstelle der Integration m20_druckbruecke in Home Assistant
std::string bruecke() { return std::string(g_einst.ha_url.c_str()) + "/api/m20_druckbruecke"; }

void netz_ams(Ergebnis& e) {
    std::string antwort, fehler;
    if (!g_ha.vorlage(AMS_VORLAGE, antwort, fehler)) return;  // ohne Home Assistant kein AMS – kein Fehlertext
    if (ams_aufbereiten(antwort.data(), antwort.size(), e.ams)) e.hat_ams = true;
}

void netz_drucker(Ergebnis& e) {
    e.hat_et = true;
    e.et.geladen = true;
    e.et.lager_url = std::string(g_einst.ha_url.c_str()) + "/todo?entity_id=" + g_einst.ha_todo.c_str();
    std::string antwort;
    int code = http_anfrage(bruecke() + "/drucker", "GET", "", antwort, 6000, true);
    if (code == 404) e.et.fehler = "Integration m20_druckbruecke fehlt in Home Assistant";
    else if (code <= 0) e.et.fehler = "Home Assistant nicht erreichbar";
    else if (code == 403) e.et.fehler = "Druck nur aus dem Heimnetz erlaubt";
    else {
        JsonDocument d;
        if (deserializeJson(d, antwort)) e.et.fehler = "Antwort der Druck-Integration unlesbar";
        for (JsonVariantConst x : d["drucker"].as<JsonArrayConst>()) {
            EtikettDrucker dr;
            dr.id = x["id"] | "";
            dr.name = x["name"] | dr.id.c_str();
            dr.erreichbar = x["erreichbar"] | true;
            if (!dr.id.empty()) e.et.drucker.push_back(dr);
        }
    }
    // eigene QR-Adresse (SchichtLohn) steht in den Etikett-Einstellungen der Sensorplatine
    if (g_einst.sensor.length()) {
        std::string a2;
        if (http_anfrage("http://" + std::string(g_einst.sensor.c_str()) + "/api/etikett/einstellungen", "GET", "", a2, 5000, false) == 200) {
            JsonDocument d;
            if (!deserializeJson(d, a2)) {
                e.et.qr_basis = d["qr"] | "";
                std::string lu = d["lager_url"] | "";
                if (!lu.empty()) e.et.lager_url = lu;
            }
        }
    }
}

void netz_logo(const std::string& name, Ergebnis& e) {
    e.hat_logo = true;
    e.logo_name = name;
    std::string url, png;
    if (g_einst.sensor.length())  // die Sensorplatine kennt alle mitgelieferten Logos und holt fehlende selbst nach
        url = "http://" + std::string(g_einst.sensor.c_str()) + "/api/hersteller_bild?name=" + url_kodieren(name);
    else
        url = std::string(g_einst.ha_url.c_str()) + "/local/hersteller/" + logo_kurz(name) + ".png";
    if (http_anfrage(url, "GET", "", png, 12000, false) != 200 || png.size() < 8) return;
    std::unique_ptr<Logo> l(new Logo);
    if (logo_laden((const uint8_t*)png.data(), png.size(), 0xf1f3f5, *l)) e.logo = std::move(l);
}

void netz_druck(const NetzAuftrag& a, Ergebnis& e) {
    size_t n = 0;
    mbedtls_base64_encode(nullptr, 0, &n, a.raster.data(), a.raster.size());
    std::string b64(n, '\0');
    mbedtls_base64_encode((unsigned char*)&b64[0], n, &n, a.raster.data(), a.raster.size());
    b64.resize(n);
    std::string body = "{\"breite\":" + std::to_string(a.breite) + ",\"hoehe\":" + std::to_string(a.hoehe) +
                       ",\"kopien\":1,\"titel\":" + json_text(a.name) + ",\"drucker\":" + json_text(a.drucker) +
                       ",\"daten\":\"" + b64 + "\"}";
    std::string antwort;
    int code = http_anfrage(bruecke() + "/drucken", "POST", body, antwort, 15000, true);
    JsonDocument d;
    deserializeJson(d, antwort);
    std::string text = d["text"] | "";
    if (code == 202 || code == 200) {
        e.meldung = text.empty() ? "Druckauftrag angenommen" : text;
    } else {
        e.fehler = true;
        e.meldung = !text.empty() ? text : code <= 0 ? "Home Assistant nicht erreichbar" : "Druck fehlgeschlagen (" + std::to_string(code) + ")";
    }
}

void netz_task(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
        for (;;) {
            NetzAuftrag a;
            {
                std::lock_guard<std::mutex> l(g_mx);
                if (g_netz.empty()) break;
                a = std::move(g_netz.front());
                g_netz.pop_front();
            }
            Ergebnis e;
            e.ha_ok_gilt = false;
            switch (a.art) {
                case NetzAuftrag::AMS: netz_ams(e); break;
                case NetzAuftrag::DRUCKER: netz_drucker(e); break;
                case NetzAuftrag::LOGO: netz_logo(a.name, e); break;
                case NetzAuftrag::DRUCK: netz_druck(a, e); break;
            }
            ergebnis(std::move(e));
        }
    }
}

// ---------------------------------------------------------------- Aktionen der Oberfläche
// ---------------------------------------------------------------- Bildschirm aus
int g_auto_aus_min = 0;  // 0 = nie
uint32_t g_aktiv_um = 0;  // letzte Bedienung oder Bewegung auf der Waage

int auto_aus_laden() {
    Preferences p;
    p.begin("anzeige", false);
    int m = p.getInt("licht_aus", 0);
    p.end();
    return m;
}

// Weckt bei Bewegung auf der Waage (Gewicht ändert sich um 5 g oder ein Tag kommt/geht) und schaltet nach der
// eingestellten Zeit ohne Bedienung ab. Berührungen behandelt board_gemeinsam.cpp.
void licht_pruefen(const SensorDaten& d) {
    static float gramm = 0;
    static std::string tag_l, tag_r;
    static bool erstes = true;
    uint32_t jetzt = millis();
    bool bewegt = erstes || fabsf(d.gramm - gramm) >= 5 || d.tag_l != tag_l || d.tag_r != tag_r;
    erstes = false;
    if (bewegt) {
        gramm = d.gramm;
        tag_l = d.tag_l;
        tag_r = d.tag_r;
        g_aktiv_um = jetzt;
        if (!board_licht_an()) board_licht(true);
    }
    uint32_t t = board_beruehrt_um();
    if (t && (int32_t)(t - g_aktiv_um) > 0) g_aktiv_um = t;
    if (g_auto_aus_min > 0 && board_licht_an() && jetzt - g_aktiv_um > (uint32_t)g_auto_aus_min * 60000UL)
        board_licht(false);
}

struct Aktionen : UiAktionen {
    void tara() override { g_sensor.tara(); }
    void bildschirm_aus() override { board_licht(false); }
    void auto_aus_setzen(int minuten) override {
        g_auto_aus_min = minuten;
        g_aktiv_um = millis();
        Preferences p;
        p.begin("anzeige", false);
        p.putInt("licht_aus", minuten);
        p.end();
        ui_meldung(minuten ? "Bildschirm geht nach " + std::to_string(minuten) + " min ohne Bedienung aus"
                           : "Bildschirm bleibt an");
    }
    void kalibrieren(int g) override {
        SensorDaten d = g_sensor.daten();
        if (d.gramm < 0 && !d.kalibriert) {
            ui_meldung("Erst Tara bei leerem Teller", true);
            return;
        }
        g_sensor.kalibrieren(g);
        ui_meldung("Kalibriere mit " + std::to_string(g) + " g …");
    }
    void rest_speichern(const std::string& s, int netto, const std::string& tag) override {
        if (!g_sensor.daten().stabil) {
            ui_meldung("Gewicht noch unruhig – kurz warten", true);
            return;
        }
        ui_meldung("Speichere …");
        auftrag({Auftrag::REST, s, tag, netto});
    }
    void tag_zuordnen(const std::string& s, const std::string& tag) override {
        ui_meldung("Ordne Tag zu …");
        auftrag({Auftrag::ZUORDNEN, s, tag, 0});
    }
    void spulen_aendern(const std::string& s, int delta) override { auftrag({Auftrag::SPULEN, s, "", delta}); }
    void neu_laden() override {
        ui_meldung("Lade Lager …");
        auftrag({Auftrag::LADEN, "", "", 0});
    }
    const Logo* logo(const std::string& h) override {
        auto it = g_logos.find(h);
        if (it != g_logos.end()) return it->second.get();
        // höchstens 48 Hersteller anfragen (Speicher), jeden nur einmal
        if (WiFi.status() == WL_CONNECTED && g_logo_angefragt.size() < 48 && g_logo_angefragt.insert(h).second) {
            NetzAuftrag a{NetzAuftrag::LOGO};
            a.name = h;
            netz(std::move(a));
        }
        return nullptr;
    }
    void etikett_drucken(const std::string& titel, const EtikettBild& b, const std::string& drucker) override {
        NetzAuftrag a{NetzAuftrag::DRUCK};
        a.name = titel;
        a.drucker = drucker;
        a.breite = b.breite;
        a.hoehe = b.hoehe;
        a.raster = b.raster;
        netz(std::move(a));
    }
    void drucker_laden() override {
        netz({NetzAuftrag::AMS});
        netz({NetzAuftrag::DRUCKER});
    }
} g_aktionen;

std::string uhrzeit() {
    struct tm t;
    if (!getLocalTime(&t, 0)) return "";
    char b[8];
    strftime(b, sizeof b, "%H:%M", &t);
    return b;
}

}  // namespace

// ---------------------------------------------------------------- WLAN
// Der ESP32 verbindet sich nach einem Abbruch nicht zuverlässig von selbst neu (je nach Grund – Router-Neustart,
// Mesh-Wechsel, Störung – bleibt er einfach getrennt). Darum: Stromsparen aus, Trennungen mitschreiben und
// alle 10 s selbst neu verbinden, bis es wieder steht.
static volatile uint8_t g_wlan_grund = 0;
static uint32_t g_wlan_weg_seit = 0, g_wlan_versuch = 0;
static int g_wlan_versuche = 0;

// Stromsparen im WLAN aus – außer Bluetooth läuft mit (Etikettendruck in der Variante ohne Sensorplatine):
// dann verlangt der ESP32 Modem-Sleep, sonst bricht er mit „Should enable WiFi modem sleep“ ab.
void wlan_schlaf_setzen() {
#if SENSOR_DIREKT && DIREKT_WEB
    WiFi.setSleep(drucker_gesperrt() ? WIFI_PS_NONE : WIFI_PS_MIN_MODEM);
#else
    WiFi.setSleep(false);
#endif
}

void wlan_start() {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
#if SENSOR_DIREKT
    WiFi.setHostname(waage_name().c_str());
#else
    WiFi.setHostname(GERAETENAME);
#endif
    wlan_schlaf_setzen();
    WiFi.setAutoReconnect(true);
    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) { g_wlan_grund = info.wifi_sta_disconnected.reason; },
                 ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    if (g_einst.ssid.length()) WiFi.begin(g_einst.ssid.c_str(), g_einst.pw.c_str());
}

void wlan_pruefen() {
    if (g_einst.ssid.isEmpty()) return;
    uint32_t jetzt = millis();
    if (WiFi.status() == WL_CONNECTED) {
        if (g_wlan_weg_seit) {
            Serial.printf("WLAN wieder da nach %lu s (%d Versuch(e)), RSSI %d dBm\n",
                          (unsigned long)((jetzt - g_wlan_weg_seit) / 1000), g_wlan_versuche, WiFi.RSSI());
            wlan_schlaf_setzen();
        }
        g_wlan_weg_seit = 0;
        g_wlan_versuche = 0;
        return;
    }
    if (!g_wlan_weg_seit) {
        g_wlan_weg_seit = jetzt;
        g_wlan_versuch = jetzt;
        Serial.printf("WLAN getrennt, Grund %u\n", (unsigned)g_wlan_grund);
        return;
    }
    if (jetzt - g_wlan_versuch < 10000) return;
    g_wlan_versuch = jetzt;
    g_wlan_versuche++;
    Serial.printf("WLAN: neuer Versuch %d (Status %d, Grund %u)\n", g_wlan_versuche, (int)WiFi.status(),
                  (unsigned)g_wlan_grund);
    WiFi.disconnect(false);
    WiFi.begin(g_einst.ssid.c_str(), g_einst.pw.c_str());
}

#if SENSOR_DIREKT
// ---------------------------------------------------------------- Web-Oberfläche wie auf der Sensorplatine
// oberflaeche.cpp aus firmware/sensor-s3zero läuft hier mit (http://<name>.local). Dafür die Schnittstelle aus sensor.h.
esp_reset_reason_t g_start_grund = ESP_RST_UNKNOWN;
volatile uint32_t g_loops = 0;
String g_name;
constexpr const char* NAME_STANDARD = "filamentwaage-2";  // die Sensorplatine heißt „filamentwaage“

bool name_gueltig(const String& n) {
    if (n.length() < 2 || n.length() > 24 || n[0] == '-' || n[n.length() - 1] == '-') return false;
    for (char ch : n)
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-')) return false;
    return true;
}

const String& waage_name() {
    if (g_name.isEmpty()) {
        Preferences p;
        p.begin("geraet", false);
        g_name = p.getString("name", NAME_STANDARD);
        p.end();
        if (!name_gueltig(g_name)) g_name = NAME_STANDARD;
    }
    return g_name;
}

bool waage_name_setzen(String n) {
    n.trim();
    n.toLowerCase();
    if (!name_gueltig(n)) return false;
    Preferences p;
    p.begin("geraet", false);
    p.putString("name", n);
    p.end();
    g_name = n;  // Hostname und mDNS gelten nach dem Neustart
    return true;
}

Messwert messwert() {
    SensorDaten d = g_sensor.daten();
    Messwert m;
    m.gramm = d.gramm;
    m.stabil = d.stabil;
    m.kalibriert = d.kalibriert;
    m.hx711_ok = g_sensor.waage_liefert();
    m.tag_l = d.tag_l.c_str();
    m.tag_r = d.tag_r.c_str();
    m.bambu_l = g_sensor.bambu(0);
    m.bambu_r = g_sensor.bambu(1);
    m.nfc_l_ok = g_sensor.nfc_bereit(0);
    m.nfc_r_ok = g_sensor.nfc_bereit(1);
    return m;
}

bool nfc_leser_bereit(int seite) { return g_sensor.nfc_bereit(seite); }

const char* vorgang_starten(bool kal, int gramm, Stream*) {
    if (g_sensor.beschaeftigt()) return "beschäftigt";
    if (kal) {
        if (gramm < 20) return "Kalibriergewicht fehlt (mindestens 20 g)";
        g_sensor.kalibrieren(gramm);
    } else {
        g_sensor.tara();
    }
    return nullptr;
}

void wlan_setzen(const char* ssid, const char* pw) {
    g_einst.ssid = ssid;
    g_einst.pw = pw;
    einst_speichern("ssid", g_einst.ssid);
    einst_speichern("pw", g_einst.pw);
    WiFi.disconnect(false);
    WiFi.begin(ssid, pw);
}

bool pad_verbunden() { return false; }

void alles_zuruecksetzen() {
    g_sensor.kalibrierung_loeschen();
    Preferences p;
    p.begin("geraet", false);
    p.clear();
    p.end();
    einst_speichern("ssid", "");
    einst_speichern("pw", "");
}

String start_bericht() {
    switch (g_start_grund) {
        case ESP_RST_POWERON: return "grund=eingeschaltet";
        case ESP_RST_SW: return "grund=neu gestartet";
        case ESP_RST_PANIC: return "grund=Absturz";
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT: return "grund=Watchdog";
        case ESP_RST_BROWNOUT: return "grund=Unterspannung";
        default: return "grund=anderer Grund";
    }
}

// Startet die Web-Oberfläche, sobald das WLAN zum ersten Mal steht. Home Assistant übernimmt sie beim ersten Mal
// aus den Display-Einstellungen (Webflasher), danach hat sie ihre eigenen (Einstellungen auf der Webseite).
// Bambu-Lab-Tag als BambuInfo für die Oberfläche – JSON vom Leser nur neu lesen, wenn es sich geändert hat
BambuInfo bambu_info(int seite) {
    static String alt[2];
    static BambuInfo info[2];
    String j = g_sensor.bambu(seite);
    if (j == alt[seite]) return info[seite];
    alt[seite] = j;
    BambuInfo b;
    JsonDocument d;
    if (j.length() && !deserializeJson(d, j)) {
        b.uid = d["uid"] | "";
        b.material_id = d["material_id"] | "";
        b.variante = d["variante"] | "";
        b.typ = d["typ"] | "";
        b.detail = d["detail"] | "";
        b.gewicht = d["gewicht"] | 0;
        String f = d["farbe"] | "808080";  // RRGGBBAA
        b.rgb = strtoul(f.substring(0, 6).c_str(), nullptr, 16);
    }
    info[seite] = b;
    return b;
}

// ---------------------------------------------------------------- Meldungen an SchichtLohn (ohne Web-Oberfläche)
// Wie die Sensorplatine (oberflaeche.cpp): sensor.<name>_laufzeit, _gewicht und _nfc an die eingetragene Adresse
// (SchichtLohn-Server oder Home Assistant). Daran erkennt SchichtLohn die Waage, zeigt das Gewicht im Lager und die
// „Spule erkannt“-Karte. Eigene Task auf Kern 0, damit langsame Antworten den Bildschirm nicht bremsen.
String melde_entity(const char* teil) {
    String n = waage_name();
    n.replace('-', '_');
    return "sensor." + n + "_" + teil;
}

String melde_anzeige(const char* teil) {
    String n = waage_name();  // „filamentwaage-2“ → „Filamentwaage 2“
    n.replace('-', ' ');
    if (n.length()) n.setCharAt(0, toupper((unsigned char)n[0]));
    return n + " " + teil;
}

void melden_task(void*) {
    HaClient ha;
    String url, token;
    float gewicht_gemeldet = -1000;
    uint32_t gewicht_um = 0, laufzeit_um = 0, nfc_um = 0;
    std::string nfc_alt;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(250));
        if (WiFi.status() != WL_CONNECTED || g_einst.ha_url.isEmpty()) continue;
        if (url != g_einst.ha_url || token != g_einst.ha_token) {
            url = g_einst.ha_url;
            token = g_einst.ha_token;
            ha.begin(url.c_str(), token.length() ? token.c_str() : "schichtlohn", g_einst.ha_todo.c_str());
        }
        uint32_t jetzt = millis();
        std::string fehler;
        SensorDaten d = g_sensor.daten();

        // Gewicht (brutto): ruhig und ≥ 1 g anders höchstens 1×/s, große Sprünge sofort, sonst alle 30 s
        if (d.verbunden && d.kalibriert) {
            float g = d.gramm < 0 ? 0.0f : d.gramm;
            float diff = fabsf(g - gewicht_gemeldet);
            uint32_t seit = jetzt - gewicht_um;
            if ((d.stabil && diff >= 1.0f && seit >= 1000) || (diff >= 50.0f && seit >= 1000) || seit >= 30000) {
                JsonDocument j;
                j["state"] = roundf(g * 10) / 10;
                j["attributes"]["friendly_name"] = melde_anzeige("Gewicht");
                j["attributes"]["unit_of_measurement"] = "g";
                j["attributes"]["state_class"] = "measurement";
                j["attributes"]["device_class"] = "weight";
                j["attributes"]["icon"] = "mdi:scale";
                std::string body;
                serializeJson(j, body);
                if (ha.zustand_setzen(melde_entity("gewicht").c_str(), body, fehler)) gewicht_gemeldet = g;
                gewicht_um = jetzt;
            }
        }

        // NFC: sofort bei jeder Änderung (Tag oder Bambu-Daten), sonst alle 60 s
        String bl = g_sensor.bambu(0), br = g_sensor.bambu(1);
        std::string nfc_neu = d.tag_l + "|" + d.tag_r + "|" + bl.c_str() + "|" + br.c_str();
        if (nfc_neu != nfc_alt || jetzt - nfc_um >= 60000) {
            JsonDocument j;
            j["state"] = !d.tag_r.empty() ? d.tag_r : d.tag_l;
            JsonObject a = j["attributes"].to<JsonObject>();
            a["friendly_name"] = melde_anzeige("NFC");
            a["icon"] = "mdi:nfc-variant";
            a["tag_l"] = d.tag_l;
            a["tag_r"] = d.tag_r;
            const String* bambu[2] = {&bl, &br};
            const char* namen[2] = {"bambu_l", "bambu_r"};
            for (int i = 0; i < 2; i++) {
                JsonDocument b;
                if (bambu[i]->length() && !deserializeJson(b, *bambu[i])) {
                    b.remove("uid");
                    a[namen[i]] = b;
                } else {
                    a[namen[i]] = nullptr;
                }
            }
            a["leer_bambu_g"] = 250;  // Bambu-Mehrwegspule
            a["leer_standard_g"] = 250;
            std::string body;
            serializeJson(j, body);
            if (ha.zustand_setzen(melde_entity("nfc").c_str(), body, fehler)) nfc_alt = nfc_neu;
            nfc_um = jetzt;
        }

        // Laufzeit mit Version, IP und Name – daran erkennt SchichtLohn die Waage; dazu WLAN-Signal
        if (!laufzeit_um || jetzt - laufzeit_um >= 60000) {
            laufzeit_um = jetzt;
            JsonDocument j;
            j["state"] = (long)(jetzt / 3600000UL);
            j["attributes"]["friendly_name"] = melde_anzeige("läuft seit");
            j["attributes"]["unit_of_measurement"] = "h";
            j["attributes"]["icon"] = "mdi:timer-outline";
            j["attributes"]["version"] = FW_VERSION;
            j["attributes"]["ip"] = WiFi.localIP().toString();
            j["attributes"]["waage"] = waage_name();
            std::string body;
            serializeJson(j, body);
            if (!ha.zustand_setzen(melde_entity("laufzeit").c_str(), body, fehler))
                Serial.printf("[melden] %s nicht erreichbar: %s\n", url.c_str(), fehler.c_str());
            JsonDocument w;
            w["state"] = WiFi.RSSI();
            w["attributes"]["friendly_name"] = melde_anzeige("WLAN");
            w["attributes"]["unit_of_measurement"] = "dBm";
            w["attributes"]["state_class"] = "measurement";
            w["attributes"]["device_class"] = "signal_strength";
            body.clear();
            serializeJson(w, body);
            ha.zustand_setzen(melde_entity("wlan").c_str(), body, fehler);
        }
    }
}

// Wächter auf Kern 0: meldet, wenn die Bildschirm-Schleife (Kern 1) hängt, und an welcher Stelle
volatile const char* g_schritt = "Start";
void waechter(void*) {
    uint32_t alt = 0, gleich_seit = millis();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        uint32_t n = g_loops;
        if (n != alt) {
            alt = n;
            gleich_seit = millis();
        } else if (millis() - gleich_seit >= 5000) {
            Serial.printf("[system] Bildschirm-Schleife hängt seit %lu s bei „%s“ (Speicher intern frei %u)\n",
                          (unsigned long)((millis() - gleich_seit) / 1000), (const char*)g_schritt,
                          (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
            gleich_seit = millis();
        }
    }
}

void web_weiter() {
    g_loops++;
#if DIREKT_WEB
    static bool laeuft = false;
    if (!laeuft) {
        if (WiFi.status() != WL_CONNECTED) return;
        laeuft = true;
        Preferences p;
        p.begin("ui", false);
        if (p.getString("ha_token", "").isEmpty() && g_einst.ha_token.length()) {
            p.putString("ha_url", g_einst.ha_url);
            p.putString("ha_token", g_einst.ha_token);
            p.putString("ha_todo", g_einst.ha_todo);
        }
        p.end();
        if (MDNS.begin(waage_name().c_str())) MDNS.addService("http", "tcp", WEB_PORT);
        g_schritt = "Web-Oberfläche starten";
        oberflaeche_start();
        Serial.printf("Web-Oberfläche: http://%s.local  (http://%s)\n", waage_name().c_str(),
                      WiFi.localIP().toString().c_str());
        return;
    }
    g_schritt = "Web-Oberfläche";
    oberflaeche_weiter();
#endif
}
#endif

void setup() {
    // Standard sind 256 Bytes – die Einstellungen vom Webflasher (mit HA-Token) sind länger
    Serial.setRxBufferSize(4096);
    Serial.begin(115200);
    // Nicht warten, wenn am USB niemand mitliest (sonst bremst jede Ausgabezeile die Oberfläche)
    Serial.setTxTimeoutMs(0);
    delay(200);
    Serial.println("FilamentLager-Waage " FW_VERSION);
#if BOARD_HAS_PSRAM
    // Schon Speicherstücke ab 256 Byte aus dem PSRAM nehmen (Standard: erst ab 4 KB). Das Lager vom Server sind
    // Hunderte kleine Texte – im internen Speicher (gut 75 KB frei) blieb beim Neuladen nichts mehr übrig
    // (Protokoll: „intern frei 508“), dann hängen WLAN und Display. Intern bleibt für WLAN, Puffer und Stacks.
    heap_caps_malloc_extmem_enable(256);
#endif
#if SENSOR_DIREKT
    g_start_grund = esp_reset_reason();
#if DIREKT_WEB
    // Bluetooth (direkter Etikettendruck der Web-Oberfläche) bleibt aus: zusammen mit Display, WLAN und Webserver
    // reicht der interne Speicher des SC01 Plus nicht (AsyncTCP startet sonst nicht). Gedruckt wird über Home Assistant.
    drucker_frueh(false);
#endif
#endif

    lv_init();
    if (!board_start()) {
        Serial.println("Display-Fehler – Pins und Timing in config.h prüfen");
    }
    ui_start(&g_aktionen, &g_lager);
    g_auto_aus_min = auto_aus_laden();
    ui_auto_aus(g_auto_aus_min);
    g_aktiv_um = millis();

    einst_laden();
#if SENSOR_DIREKT
    g_sensor.begin();  // Sensor-Adresse aus dem Webflasher wird hier nicht gebraucht
#else
    if (g_einst.sensor.length()) {
        g_sensor.begin_wlan(g_einst.sensor.c_str(), SENSOR_PORT);
        Serial.printf("Sensorplatine per WLAN: %s\n", g_einst.sensor.c_str());
    } else {
        g_sensor.begin(g_rs485, RS485_RX, RS485_TX, RS485_BAUD, RS485_DE);
    }
#endif
    g_sensor.starten();

    wlan_start();
    configTzTime(ZEITZONE, "pool.ntp.org", "time.google.com");

    g_ha.begin(g_einst.ha_url.c_str(), g_einst.ha_token.c_str(), g_einst.ha_todo.c_str());
    xTaskCreatePinnedToCore(ha_task, "ha", 12288, nullptr, 1, &g_ha_task, 0);
    xTaskCreatePinnedToCore(netz_task, "netz", 12288, nullptr, 1, &g_netz_task, 0);
    if (g_einst.ssid.isEmpty()) ui_meldung("Kein WLAN eingerichtet – Webflasher benutzen", true);
    else if (g_einst.ha_url.isEmpty() || g_einst.ha_token.isEmpty()) ui_meldung("Home Assistant fehlt – Webflasher benutzen", true);
#if SENSOR_DIREKT
    xTaskCreatePinnedToCore(waechter, "waechter", 3072, nullptr, 1, nullptr, 0);
    xTaskCreatePinnedToCore(melden_task, "melden", 8192, nullptr, 1, nullptr, 0);
#endif
}

void loop() {
#if SENSOR_DIREKT
    g_schritt = "Bildschirm zeichnen";
#endif
    lv_timer_handler();
#if SENSOR_DIREKT
    g_schritt = "WLAN";
#endif
    wlan_pruefen();
    {
        bool ok;
        std::string t;
        while (g_sensor.meldung_holen(ok, t)) ui_meldung(t, !ok);
    }
#if SENSOR_DIREKT
    g_schritt = "USB";
#endif
    usb_lesen();
#if SENSOR_DIREKT
    web_weiter();
    g_schritt = "Ergebnisse übernehmen";
#endif

    // Ergebnisse der HA-Task übernehmen
    std::deque<Ergebnis> fertig;
    {
        std::lock_guard<std::mutex> l(g_mx);
        fertig.swap(g_ergebnisse);
    }
    for (auto& e : fertig) {
        if (e.hat_lager) {
            // Das Lager wird jede Minute geholt – die Liste nur neu aufbauen, wenn sich etwas geändert hat
            uint32_t h = lager_pruefsumme(e.lager);
            if (h != g_lager_summe || e.lager.sorten.size() != g_lager.sorten.size()) {
                g_lager_summe = h;
                g_lager.sorten.swap(e.lager.sorten);
                ui_lager_neu();
            }
        }
        if (e.ha_ok_gilt) g_ha_ok = e.ha_ok;
        if (e.hat_ams) {
            g_ams = std::move(e.ams);
            ui_ams(&g_ams);
        }
        if (e.hat_et) {
            ui_etikett(e.et);
            // Kopfzeile „Drucker“: grün, wenn einer erreichbar ist, rot, wenn alle aus sind
            bool einer = false;
            for (auto& d : e.et.drucker) einer |= d.erreichbar;
            g_drucker_status = e.et.drucker.empty() ? -1 : einer ? 1 : 2;
        }
        if (e.hat_logo) {
            g_logos[e.logo_name] = std::move(e.logo);
            if (g_logos[e.logo_name]) {
                g_logo_neu = true;
                g_letztes_logo = millis();
            }
        }
        if (!e.meldung.empty()) ui_meldung(e.meldung, e.fehler);
    }
    // neue Logos gesammelt melden, damit die Lagerliste nicht bei jedem einzeln neu aufgebaut wird
    // erst, wenn 1,5 s kein weiteres kam (oder spätestens alle 6 s), damit die Liste beim ersten Laden
    // nicht im Sekundentakt neu aufgebaut wird
    if (g_logo_neu && (millis() - g_letztes_logo > 1500 || millis() - g_letzte_logo_meldung > 6000)) {
        g_logo_neu = false;
        g_letzte_logo_meldung = millis();
        ui_logos_neu();
    }

    uint32_t jetzt = millis();
    bool wlan = WiFi.status() == WL_CONNECTED;
    bool ha_da = g_einst.ha_url.length() && g_einst.ha_token.length();
    if (wlan && ha_da && (g_letztes_laden == 0 || jetzt - g_letztes_laden > LAGER_NEU_LADEN_MS)) {
        g_letztes_laden = jetzt;
        auftrag({Auftrag::LADEN, "", "", 0});
    }
    // AMS jede Minute, Etikettendrucker alle 5 Minuten
    if (wlan && ha_da && (g_letztes_ams == 0 || jetzt - g_letztes_ams > 60000)) {
        g_letztes_ams = jetzt;
        netz({NetzAuftrag::AMS});
    }
    if (wlan && ha_da && (g_letzte_drucker == 0 || jetzt - g_letzte_drucker > 300000)) {
        g_letzte_drucker = jetzt;
        netz({NetzAuftrag::DRUCKER});
    }

    static uint32_t letzte_anzeige = 0;
    if (jetzt - letzte_anzeige >= 100) {
        letzte_anzeige = jetzt;
        SensorDaten d = g_sensor.daten();
#if SENSOR_DIREKT
        g_schritt = "Anzeige aktualisieren";
#endif
        licht_pruefen(d);
        static uint32_t letzter_bericht = 0;
        if (jetzt - letzter_bericht >= 30000) {
            letzter_bericht = jetzt;
            Serial.printf("[system] Speicher intern frei %u (größter Block %u), PSRAM frei %u, WLAN %s, Licht %s\n",
                          (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                          (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), wlan ? "verbunden" : "getrennt",
                          board_licht_an() ? "an" : "aus");
        }
        WaagenStatus s;
        s.brutto_g = d.gramm;
        s.stabil = d.stabil;
        s.kalibriert = d.kalibriert;
        s.drucker = g_drucker_status;
        s.sensor_ok = d.verbunden;
        s.tag_l = d.tag_l;
        s.tag_r = d.tag_r;
#if SENSOR_DIREKT
        s.bambu_l = bambu_info(0);
        s.bambu_r = bambu_info(1);
#endif
        s.wlan_ok = wlan;
        s.ha_ok = g_ha_ok;
        s.uhrzeit = uhrzeit();
        s.ip = wlan ? WiFi.localIP().toString().c_str() : "";
        s.version = FW_VERSION;
        s.leer_standard_g = LEERSPULE_STANDARD_G;
        ui_status(s);
    }
    delay(5);
}
