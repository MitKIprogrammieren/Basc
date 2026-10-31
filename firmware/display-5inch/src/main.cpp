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
#include "board.h"
#include "config.h"
#include "etikett.h"
#include "logo.h"
#include "ha_client.h"
#include "lvgl.h"
#include "model.h"
#include "sensor_link.h"
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
SensorLink g_sensor;
HardwareSerial g_rs485(1);
bool g_ha_ok = false;
uint32_t g_letztes_laden = 0;
AmsStand g_ams;  // gehört der Oberfläche
std::map<std::string, std::unique_ptr<Logo>> g_logos;  // fertig geladen (nullptr = keins gefunden)
std::set<std::string> g_logo_angefragt;
bool g_logo_neu = false;
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
        a["ssid"] = g_einst.ssid;
        a["ha"] = g_ha_ok;
        a["ha_url"] = g_einst.ha_url;
        a["sensor"] = g_sensor.daten().verbunden;
        a["sensor_host"] = g_einst.sensor;
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
            } else if (e.meldung.empty() || a.art == Auftrag::LADEN) {
                e.meldung = fehler;
                e.fehler = true;
            }
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
    if (WiFi.status() != WL_CONNECTED) return -1;
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
    http.setConnectTimeout(3000);
    if (mit_token && g_einst.ha_token.length()) http.addHeader("Authorization", "Bearer " + g_einst.ha_token);
    int code;
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
struct Aktionen : UiAktionen {
    void tara() override { g_sensor.tara(); }
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

void setup() {
    // Standard sind 256 Bytes – die Einstellungen vom Webflasher (mit HA-Token) sind länger
    Serial.setRxBufferSize(4096);
    Serial.begin(115200);
    // Nicht warten, wenn am USB niemand mitliest (sonst bremst jede Ausgabezeile die Oberfläche)
    Serial.setTxTimeoutMs(0);
    delay(200);
    Serial.println("FilamentLager-Waage " FW_VERSION);

    lv_init();
    if (!board_start()) {
        Serial.println("Display-Fehler – Pins und Timing in config.h prüfen");
    }
    ui_start(&g_aktionen, &g_lager);

    einst_laden();
    if (g_einst.sensor.length()) {
        g_sensor.begin_wlan(g_einst.sensor.c_str(), SENSOR_PORT);
        Serial.printf("Sensorplatine per WLAN: %s\n", g_einst.sensor.c_str());
    } else {
        g_sensor.begin(g_rs485, RS485_RX, RS485_TX, RS485_BAUD, RS485_DE);
    }
    g_sensor.starten();

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(GERAETENAME);
    if (g_einst.ssid.length()) WiFi.begin(g_einst.ssid.c_str(), g_einst.pw.c_str());
    configTzTime(ZEITZONE, "pool.ntp.org", "time.google.com");

    g_ha.begin(g_einst.ha_url.c_str(), g_einst.ha_token.c_str(), g_einst.ha_todo.c_str());
    xTaskCreatePinnedToCore(ha_task, "ha", 12288, nullptr, 1, &g_ha_task, 0);
    xTaskCreatePinnedToCore(netz_task, "netz", 12288, nullptr, 1, &g_netz_task, 0);
    if (g_einst.ssid.isEmpty()) ui_meldung("Kein WLAN eingerichtet – Webflasher benutzen", true);
    else if (g_einst.ha_url.isEmpty() || g_einst.ha_token.isEmpty()) ui_meldung("Home Assistant fehlt – Webflasher benutzen", true);
}

void loop() {
    lv_timer_handler();
    {
        bool ok;
        std::string t;
        while (g_sensor.meldung_holen(ok, t)) ui_meldung(t, !ok);
    }
    usb_lesen();

    // Ergebnisse der HA-Task übernehmen
    std::deque<Ergebnis> fertig;
    {
        std::lock_guard<std::mutex> l(g_mx);
        fertig.swap(g_ergebnisse);
    }
    for (auto& e : fertig) {
        if (e.hat_lager) {
            g_lager.sorten.swap(e.lager.sorten);
            ui_lager_neu();
        }
        if (e.ha_ok_gilt) g_ha_ok = e.ha_ok;
        if (e.hat_ams) {
            g_ams = std::move(e.ams);
            ui_ams(&g_ams);
        }
        if (e.hat_et) ui_etikett(e.et);
        if (e.hat_logo) {
            g_logos[e.logo_name] = std::move(e.logo);
            if (g_logos[e.logo_name]) g_logo_neu = true;
        }
        if (!e.meldung.empty()) ui_meldung(e.meldung, e.fehler);
    }
    // neue Logos gesammelt melden, damit die Lagerliste nicht bei jedem einzeln neu aufgebaut wird
    if (g_logo_neu && millis() - g_letzte_logo_meldung > 1000) {
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
        WaagenStatus s;
        s.brutto_g = d.gramm;
        s.stabil = d.stabil;
        s.sensor_ok = d.verbunden;
        s.tag_l = d.tag_l;
        s.tag_r = d.tag_r;
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
