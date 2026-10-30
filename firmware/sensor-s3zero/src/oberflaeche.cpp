// Komplette Waagen-Oberfläche auf der Sensorplatine.
//
// Die Webdateien sind dieselben wie auf dem Pi (pi/filamentwaage/web), sie liegen gzip-komprimiert
// im Dateisystem (LittleFS, siehe tools/web_daten.py). Die API verhält sich wie pi/filamentwaage/app.py:
//   GET  /api/zustand, /api/lager, /api/ereignisse (Server-Sent Events), /api/einstellungen, /api/wlan/netze
//   POST /api/tara, /api/kalibrieren, /api/rest, /api/zuordnen, /api/spulen, /api/neu_laden,
//        /api/einstellungen, /api/wlan, /api/ha, /api/ha/test, /api/system
// Home Assistant wird in einem eigenen Task angesprochen, damit der Webserver nie wartet.

#include "oberflaeche.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <Update.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <time.h>

#include <algorithm>
#include <deque>
#include <functional>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>

#include "config.h"
#include "drucker.h"
#include "ha_client.h"
#include "model.h"
#include "sensor.h"

namespace {

AsyncWebServer g_server(WEB_PORT);
AsyncEventSource g_events("/api/ereignisse");
std::mutex g_mx;  // schützt alles darunter

Lager g_lager;
int g_lager_version = 0;
// Fertiges JSON für /api/lager – einmal nach jedem Laden gebaut, liegt im PSRAM und wird
// stückweise ausgeliefert. 236 Sorten sind gut 150 KB, das passt nicht in den internen Speicher.
std::shared_ptr<const std::string> g_lager_json = std::make_shared<const std::string>("{\"version\":0,\"sorten\":[]}");
bool g_ha_ok = false;
bool g_fs_ok = false;

struct Meldung {
    String text;
    bool fehler;
};
std::deque<Meldung> g_meldungen;

struct HaCfg {
    String url, token, todo;
};
HaCfg g_ha;
JsonDocument g_einst;  // Einstellungen der Oberfläche (Design, Waage, Bambu)
Preferences g_prefs;

String g_wlan_ssid, g_wlan_pw;
uint32_t g_wlan_um = 0, g_neustart_um = 0;

void meldung(const String& text, bool fehler = false) {
    std::lock_guard<std::mutex> l(g_mx);
    g_meldungen.push_back({text, fehler});
    while (g_meldungen.size() > 20) g_meldungen.pop_front();
}

// ---------------------------------------------------------------- Einstellungen
void einst_standard(JsonDocument& e) {
    if (!e["akzent"].is<const char*>()) e["akzent"] = "#4caf50";
    if (!e["helligkeit"].is<int>()) e["helligkeit"] = 100;
    if (!e["display_aus_min"].is<int>()) e["display_aus_min"] = 0;
    if (!e["kompakt"].is<bool>()) e["kompakt"] = false;
    if (!e["leerspule_g"].is<int>()) e["leerspule_g"] = 250;
    if (!e["nfc_stufe"].is<int>()) e["nfc_stufe"] = 3;
    if (!e["drucker"].is<JsonArray>()) e["drucker"].to<JsonArray>();
    if (!e["ams_suche"].is<bool>()) e["ams_suche"] = false;
    e["abbuchung_aktiv"] = false;
}

void einst_laden() {
    g_prefs.begin("ui", true);
    String j = g_prefs.getString("einst", "{}");
    g_ha.url = g_prefs.getString("ha_url", "http://homeassistant.local:8123");
    g_ha.token = g_prefs.getString("ha_token", "");
    g_ha.todo = g_prefs.getString("ha_todo", "todo.filament");
    g_prefs.end();
    if (deserializeJson(g_einst, j) || !g_einst.is<JsonObject>()) g_einst.to<JsonObject>();
    einst_standard(g_einst);
}

void einst_speichern() {  // g_mx gehalten
    String j;
    serializeJson(g_einst, j);
    g_prefs.begin("ui", false);
    g_prefs.putString("einst", j);
    g_prefs.putString("ha_url", g_ha.url);
    g_prefs.putString("ha_token", g_ha.token);
    g_prefs.putString("ha_todo", g_ha.todo);
    g_prefs.end();
}

int signal_prozent(int rssi) { return constrain(2 * (rssi + 100), 0, 100); }

String ip_text() { return WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String(""); }

void einst_json(JsonDocument& d) {
    std::lock_guard<std::mutex> l(g_mx);
    d.set(g_einst);
    JsonObject w = d["wlan"].to<JsonObject>();
    if (WiFi.status() == WL_CONNECTED) {
        w["ssid"] = WiFi.SSID();
        w["ip"] = WiFi.localIP().toString();
        w["signal"] = signal_prozent(WiFi.RSSI());
    }
    JsonObject ha = d["ha"].to<JsonObject>();
    ha["url"] = g_ha.url;
    ha["todo"] = g_ha.todo;
    ha["token_gesetzt"] = g_ha.token.length() > 0;
    ha["token_ende"] = g_ha.token.length() > 4 ? g_ha.token.substring(g_ha.token.length() - 4) : String("");
    d["einrichten_url"] = "http://" + ip_text() + "/";
    d["update"] = true;  // Oberfläche zeigt „Update einspielen“
}

// ---------------------------------------------------------------- Home Assistant (eigener Task)
enum class Art { LADEN, REST, SPULEN, ZUORDNEN, TEST, FELD };
struct Auftrag {
    Art art;
    std::string sorte, tag;
    int zahl = 0;
    HaCfg test;
    std::string wert;  // FELD: neuer Inhalt, tag = Feldname
};
QueueHandle_t g_auftraege = nullptr;

void auftrag(Auftrag* a) {
    if (!g_auftraege || xQueueSend(g_auftraege, &a, 0) != pdTRUE) {
        delete a;
        meldung("Zu viele Aufträge – kurz warten", true);
    }
}
void auftrag(Art art, const std::string& sorte = "", const std::string& tag = "", int zahl = 0) {
    auftrag(new Auftrag{art, sorte, tag, zahl, {}});
}

// ESP32 löst "homeassistant.local" nicht von selbst auf – per mDNS nachschlagen
String url_aufloesen(const String& url) {
    int a = url.indexOf("://");
    if (a < 0) return url;
    a += 3;
    int b = a;
    while (b < (int)url.length() && url[b] != ':' && url[b] != '/') b++;
    String host = url.substring(a, b);
    if (!host.endsWith(".local")) return url;
    static String gemerkt_host, gemerkt_ip;
    static uint32_t gemerkt_um = 0;
    if (host != gemerkt_host || millis() - gemerkt_um > 300000 || !gemerkt_ip.length()) {
        IPAddress ip = MDNS.queryHost(host.substring(0, host.length() - 6), 2000);
        gemerkt_host = host;
        gemerkt_ip = ip == IPAddress() ? String("") : ip.toString();
        gemerkt_um = millis();
    }
    if (!gemerkt_ip.length()) return url;
    return url.substring(0, a) + gemerkt_ip + url.substring(b);
}

bool client_fuer(const HaCfg& c, HaClient& ha, std::string& fehler) {
    if (!c.token.length()) {
        fehler = "Kein Zugriffstoken – Einstellungen → Home Assistant";
        return false;
    }
    ha.begin(url_aufloesen(c.url).c_str(), c.token.c_str(), c.todo.c_str());
    return true;
}

HaCfg ha_cfg() {
    std::lock_guard<std::mutex> l(g_mx);
    return g_ha;
}

void sorte_json(JsonObject o, const Filament& f);

std::shared_ptr<const std::string> lager_als_json(const Lager& lager, int version) {
    auto s = std::make_shared<std::string>();
    s->reserve(lager.sorten.size() * 700 + 64);
    *s += "{\"version\":" + std::to_string(version) + ",\"sorten\":[";
    bool erstes = true;
    for (const Filament& f : lager.sorten) {  // Sorte für Sorte, damit nie das ganze Lager als JsonDocument im Speicher liegt
        JsonDocument d;
        sorte_json(d.to<JsonObject>(), f);
        if (!erstes) *s += ',';
        erstes = false;
        std::string teil;  // serializeJson überschreibt das Ziel – deshalb erst einzeln, dann anhängen
        serializeJson(d, teil);
        *s += teil;
    }
    *s += "]}";
    return s;
}

bool laden(std::string& fehler) {
    HaClient ha;
    if (!client_fuer(ha_cfg(), ha, fehler)) return false;
    Lager neu;
    if (!ha.lager_laden(neu, fehler)) return false;
    int version;
    {
        std::lock_guard<std::mutex> l(g_mx);
        version = g_lager_version + 1;
    }
    auto json = lager_als_json(neu, version);
    std::lock_guard<std::mutex> l(g_mx);
    g_lager = std::move(neu);
    g_lager_json = json;
    g_lager_version = version;
    g_ha_ok = true;
    return true;
}

// Wie Anwendung._aendern auf dem Pi: frisch laden, ändern, zurückschreiben
bool aendern(const std::string& uid, const std::function<Aenderung(const std::string&)>& fn, std::string& text,
             std::string& fehler) {
    HaClient ha;
    if (!client_fuer(ha_cfg(), ha, fehler)) return false;
    Lager frisch;
    if (!ha.lager_laden(frisch, fehler)) return false;
    int i = frisch.finde_uid(uid);
    if (i < 0) {
        fehler = "Sorte nicht mehr im Lager";
        return false;
    }
    Aenderung a = fn(frisch.sorten[i].description);
    if (a.geaendert && !ha.beschreibung_setzen(uid, a.beschreibung, fehler)) return false;
    text = a.meldung;
    return true;
}

// Holt den Status der Druckbrücke und meldet ihn als Sensoren an Home Assistant (ohne YAML).
// Die Sensoren entstehen über die REST-API; nach einem HA-Neustart sind sie nach spätestens einer Minute wieder da.
// Systemwerte der Waage selbst (ESP32) als Sensoren in Home Assistant
void waage_nach_ha() {
    if (WiFi.status() != WL_CONNECTED) return;
    HaClient ha;
    std::string fehler;
    if (!client_fuer(ha_cfg(), ha, fehler)) return;
    auto wert = [&](const char* entity, const char* name, float w, const char* einheit, const char* klasse, const char* icon) {
        JsonDocument d;
        d["state"] = roundf(w * 10) / 10;
        d["attributes"]["friendly_name"] = name;
        d["attributes"]["unit_of_measurement"] = einheit;
        d["attributes"]["state_class"] = "measurement";
        if (klasse) d["attributes"]["device_class"] = klasse;
        if (icon) d["attributes"]["icon"] = icon;
        std::string body;
        serializeJson(d, body);
        ha.zustand_setzen(entity, body, fehler);
    };
    // einmal nach jedem Start: Ereignis mit dem Grund – Home Assistant meldet Abstürze aufs Handy
    static bool start_gemeldet = false;
    if (!start_gemeldet) {
        static const char* const GRUND[] = {"unbekannt", "eingeschaltet", "extern", "neu gestartet", "Absturz",
                                            "Watchdog (Interrupt)", "Watchdog (Task)", "Watchdog", "Tiefschlaf",
                                            "Unterspannung", "SDIO"};
        int g = (int)esp_reset_reason();
        JsonDocument e;
        e["grund"] = g >= 0 && g < 11 ? GRUND[g] : "unbekannt";
        e["absturz"] = g == ESP_RST_PANIC || g == ESP_RST_INT_WDT || g == ESP_RST_TASK_WDT || g == ESP_RST_WDT ||
                       g == ESP_RST_BROWNOUT;
        e["version"] = FW_VERSION;
        std::string body;
        serializeJson(e, body);
        start_gemeldet = ha.ereignis_senden("filamentwaage_neustart", body, fehler);
    }
    // Trocknen fällig: angebrochene Spulen aus feuchteempfindlichem Material, nie oder vor über 30 Tagen getrocknet
    {
        time_t jetzt = time(nullptr);
        if (jetzt > 1700000000) {
            JsonDocument d;
            JsonArray liste = d["attributes"]["sorten"].to<JsonArray>();
            int anzahl = 0;
            {
                std::lock_guard<std::mutex> l(g_mx);
                for (const Filament& f : g_lager.sorten) {
                    if (f.reste.empty()) continue;
                    String m = String(f.material.c_str());
                    m.toUpperCase();
                    bool empfindlich = m.indexOf("PETG") >= 0 || m.indexOf("TPU") >= 0 || m.indexOf("TPE") >= 0 ||
                                       m.indexOf("PA") >= 0 || m.indexOf("NYLON") >= 0 || m.indexOf("PC") >= 0 ||
                                       m.indexOf("PVA") >= 0 || m.indexOf("BVOH") >= 0;
                    if (!empfindlich) continue;
                    int tage = -1;
                    struct tm t = {};
                    if (f.getrocknet.size() == 10 && sscanf(f.getrocknet.c_str(), "%d-%d-%d", &t.tm_year, &t.tm_mon, &t.tm_mday) == 3) {
                        t.tm_year -= 1900;
                        t.tm_mon -= 1;
                        t.tm_hour = 12;
                        tage = (int)((jetzt - mktime(&t)) / 86400);
                    }
                    if (tage >= 0 && tage < TROCKNEN_NACH_TAGEN) continue;
                    anzahl++;
                    if (liste.size() < 30)
                        liste.add(String((f.hersteller + " " + f.kurzname()).c_str()) +
                                  (tage < 0 ? String(" – nie getrocknet") : " – vor " + String(tage) + " Tagen"));
                }
            }
            d["state"] = anzahl;
            d["attributes"]["friendly_name"] = "Filament: Trocknen fällig";
            d["attributes"]["unit_of_measurement"] = "Sorten";
            d["attributes"]["icon"] = "mdi:water-off";
            d["attributes"]["nach_tagen"] = TROCKNEN_NACH_TAGEN;
            std::string body;
            serializeJson(d, body);
            ha.zustand_setzen("sensor.filamentwaage_trocknen_faellig", body, fehler);
        }
    }
    {  // Gewicht auf der Waage (brutto, mit Spule) – Schichtlohn zeigt es im Lager an
        Messwert m = messwert();
        if (m.hx711_ok && m.kalibriert)
            wert("sensor.filamentwaage_gewicht", "Waage Gewicht", m.gramm < 0 ? 0.0f : m.gramm, "g", "weight", "mdi:scale");
    }
    wert("sensor.filamentwaage_temperatur", "Waage Temperatur", temperatureRead(), "°C", "temperature", nullptr);
    wert("sensor.filamentwaage_wlan", "Waage WLAN", WiFi.RSSI(), "dBm", "signal_strength", nullptr);
    uint32_t ig = heap_caps_get_total_size(MALLOC_CAP_INTERNAL), ifr = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (ig) wert("sensor.filamentwaage_arbeitsspeicher", "Waage Arbeitsspeicher belegt", 100.0f - ifr * 100.0f / ig, "%", nullptr, "mdi:memory");
    if (ESP.getPsramSize())
        wert("sensor.filamentwaage_psram", "Waage PSRAM belegt", 100.0f - ESP.getFreePsram() * 100.0f / ESP.getPsramSize(), "%", nullptr, "mdi:memory");
    {
        JsonDocument d;
        d["state"] = (long)(millis() / 3600000UL);
        d["attributes"]["friendly_name"] = "Waage läuft seit";
        d["attributes"]["unit_of_measurement"] = "h";
        d["attributes"]["icon"] = "mdi:timer-outline";
        d["attributes"]["version"] = FW_VERSION;
        d["attributes"]["ip"] = WiFi.localIP().toString();
        std::string body;
        serializeJson(d, body);
        ha.zustand_setzen("sensor.filamentwaage_laufzeit", body, fehler);
    }
}

void bruecke_nach_ha() {
    String basis = drucker_bruecke();
    if (!basis.length() || WiFi.status() != WL_CONNECTED) return;
    while (basis.endsWith("/")) basis.remove(basis.length() - 1);
    HTTPClient http;
    WiFiClient netz;
    if (!http.begin(netz, basis + "/status")) return;
    http.setTimeout(4000);
    int code = http.GET();
    String text = code == 200 ? http.getString() : String("");
    http.end();
    JsonDocument z;
    bool erreichbar = code == 200 && !deserializeJson(z, text);
    drucker_bruecke_melden(!erreichbar ? -1 : (z["verbunden"] | false) ? 1 : 0);
    HaClient ha;
    std::string fehler;
    if (!client_fuer(ha_cfg(), ha, fehler)) return;
    auto setzen = [&](const char* entity, JsonDocument& d) {
        std::string body;
        serializeJson(d, body);
        ha.zustand_setzen(entity, body, fehler);
    };
    {
        JsonDocument d;
        d["state"] = erreichbar && (z["verbunden"] | false) ? "on" : "off";
        d["attributes"]["friendly_name"] = "M20 Pro verbunden";
        d["attributes"]["device_class"] = "connectivity";
        d["attributes"]["druckbruecke"] = erreichbar ? "erreichbar" : "nicht erreichbar";
        d["attributes"]["verbunden_seit"] = z["verbunden_seit"] | "";
        d["attributes"]["abbrueche_heute"] = z["drucke"]["abbrueche_heute"] | 0;
        setzen("binary_sensor.m20_pro_verbunden", d);
    }
    if (!erreichbar) return;
    if (!z["rolle"]["rest"].isNull()) {
        JsonDocument d;
        d["state"] = z["rolle"]["rest"];
        d["attributes"]["friendly_name"] = "M20 Pro Etiketten übrig";
        d["attributes"]["unit_of_measurement"] = "Etiketten";
        d["attributes"]["state_class"] = "measurement";
        d["attributes"]["icon"] = "mdi:label-multiple-outline";
        d["attributes"]["gesamt"] = z["rolle"]["gesamt"];
        d["attributes"]["rfid"] = z["rolle"]["rfid"];
        setzen("sensor.m20_pro_etiketten", d);
    }
    const char* akku = z["akku"]["wert"] | "";
    if (*akku) {
        JsonDocument d;
        d["state"] = akku;
        d["attributes"]["friendly_name"] = "M20 Pro Akku";
        bool prozent = atoi(akku) > 0 && atoi(akku) <= 100 && strspn(akku, "0123456789") == strlen(akku);
        if (prozent) {
            d["attributes"]["unit_of_measurement"] = "%";
            d["attributes"]["device_class"] = "battery";
            d["attributes"]["state_class"] = "measurement";
        }
        setzen("sensor.m20_pro_akku", d);
    }
    if (!z["signal"]["rssi"].isNull()) {
        JsonDocument d;
        d["state"] = z["signal"]["rssi"];
        d["attributes"]["friendly_name"] = "M20 Pro Funksignal";
        d["attributes"]["unit_of_measurement"] = "dB";
        d["attributes"]["state_class"] = "measurement";
        d["attributes"]["icon"] = "mdi:bluetooth";
        d["attributes"]["bewertung"] = z["signal"]["text"];
        setzen("sensor.m20_pro_funksignal", d);
    }
    {
        JsonDocument d;
        d["state"] = z["drucke"]["heute"] | 0;
        d["attributes"]["friendly_name"] = "M20 Pro Drucke heute";
        d["attributes"]["unit_of_measurement"] = "Etiketten";
        d["attributes"]["icon"] = "mdi:printer-pos";
        d["attributes"]["gesamt"] = z["drucke"]["gesamt"];
        d["attributes"]["fehler_heute"] = z["drucke"]["fehler_heute"];
        JsonVariantConst l = z["drucke"]["letzte"][0];
        if (!l.isNull()) {
            d["attributes"]["letzter"] = String(l["zeit"] | "") + " · " + String(l["titel"] | "") + " · " + String(l["text"] | "");
        }
        setzen("sensor.m20_pro_drucke_heute", d);
    }
    // Pi, auf dem die Brücke läuft (Pi-hole): Temperatur, Auslastung, Speicher, SD-Karte
    JsonObject sy = z["system"];
    if (erreichbar && !sy.isNull()) {
        auto wert = [&](const char* entity, const char* name, float w, const char* einheit, const char* klasse,
                        const char* icon) {
            JsonDocument d;
            d["state"] = roundf(w * 10) / 10;
            d["attributes"]["friendly_name"] = name;
            d["attributes"]["unit_of_measurement"] = einheit;
            d["attributes"]["state_class"] = "measurement";
            if (klasse) d["attributes"]["device_class"] = klasse;
            if (icon) d["attributes"]["icon"] = icon;
            d["attributes"]["geraet"] = sy["geraet"] | "";
            setzen(entity, d);
        };
        if (sy["temperatur_c"].is<float>())
            wert("sensor.druckbruecke_temperatur", "Druckbrücke Temperatur", sy["temperatur_c"].as<float>(), "°C", "temperature", nullptr);
        if (sy["auslastung"].is<int>())
            wert("sensor.druckbruecke_auslastung", "Druckbrücke Auslastung", sy["auslastung"].as<int>(), "%", nullptr, "mdi:cpu-64-bit");
        double mg = sy["speicher_gesamt"] | 0.0, mf = sy["speicher_frei"] | 0.0;
        if (mg > 0) wert("sensor.druckbruecke_speicher", "Druckbrücke Speicher belegt", 100.0 - mf * 100.0 / mg, "%", nullptr, "mdi:memory");
        double sg = sy["sd_gesamt"] | 0.0, sf = sy["sd_frei"] | 0.0;
        if (sg > 0) wert("sensor.druckbruecke_sd", "Druckbrücke SD-Karte belegt", 100.0 - sf * 100.0 / sg, "%", nullptr, "mdi:sd");
        if (sy["laufzeit_s"].is<long>()) {
            JsonDocument d;
            d["state"] = (long)(sy["laufzeit_s"].as<long>() / 3600);
            d["attributes"]["friendly_name"] = "Druckbrücke läuft seit";
            d["attributes"]["unit_of_measurement"] = "h";
            d["attributes"]["icon"] = "mdi:timer-outline";
            setzen("sensor.druckbruecke_laufzeit", d);
        }
    }
}

void arbeiter(void*) {
    uint32_t naechstes_laden = 0;
    for (;;) {
        Auftrag* a = nullptr;
        uint32_t jetzt = millis();
        TickType_t warte = naechstes_laden > jetzt ? pdMS_TO_TICKS(naechstes_laden - jetzt) : 0;
        if (xQueueReceive(g_auftraege, &a, warte) != pdTRUE) {
            a = new Auftrag{Art::LADEN, "", "", 1, {}};  // zahl = 1: regelmäßiges Laden
        }
        std::string text, fehler;
        bool ok = true;
        switch (a->art) {
            case Art::REST:
                ok = aendern(a->sorte, [&](const std::string& b) { return rest_speichern(b, a->zahl, a->tag); }, text, fehler);
                break;
            case Art::SPULEN:
                ok = aendern(a->sorte, [&](const std::string& b) { return spulen_aendern(b, a->zahl); }, text, fehler);
                break;
            case Art::ZUORDNEN: {
                std::vector<std::string> andere;
                {
                    std::lock_guard<std::mutex> l(g_mx);
                    for (const Filament& f : g_lager.sorten)
                        if (f.hat_nfc(a->tag) && f.uid != a->sorte) andere.push_back(f.uid);
                }
                std::string egal;
                for (const std::string& uid : andere) {
                    if (!(ok = aendern(uid, [&](const std::string& b) { return tag_entfernen(b, a->tag); }, egal, fehler))) break;
                }
                if (ok) ok = aendern(a->sorte, [&](const std::string& b) { return tag_zuordnen(b, a->tag); }, text, fehler);
                break;
            }
            case Art::FELD:
                ok = aendern(a->sorte, [&](const std::string& b) { return feld_setzen(b, a->tag, a->wert); }, text, fehler);
                break;
            case Art::TEST: {
                HaClient ha;
                Lager probe;
                ok = client_fuer(a->test, ha, fehler) && ha.lager_laden(probe, fehler);
                if (ok) text = "Verbindung steht – " + std::to_string(probe.sorten.size()) + " Sorten in " + a->test.todo.c_str();
                break;
            }
            case Art::LADEN:
                if (a->zahl == 1) {  // jede Minute: Werte der Waage und der Druckbrücke als Sensoren in HA
                    waage_nach_ha();
                    bruecke_nach_ha();
                }
                break;
        }
        if (a->art != Art::TEST) {
            std::string lade_fehler;
            bool hatte_token = ha_cfg().token.length() > 0;
            bool war_ok;
            {
                std::lock_guard<std::mutex> l(g_mx);
                war_ok = g_ha_ok;
            }
            if (!laden(lade_fehler)) {
                {
                    std::lock_guard<std::mutex> l(g_mx);
                    g_ha_ok = false;
                }
                // beim regelmäßigen Laden nur den ersten Ausfall melden, ohne Token gar nicht
                bool still = a->art == Art::LADEN && a->zahl == 1 && (!war_ok || !hatte_token);
                if (ok && !still) {
                    ok = false;
                    fehler = lade_fehler;
                }
            }
            naechstes_laden = millis() + LAGER_NEU_LADEN_S * 1000UL;
        }
        if (!ok) meldung(fehler.c_str(), true);
        else if (!text.empty()) meldung(text.c_str());
        delete a;
    }
}

// ---------------------------------------------------------------- Herstellerbilder
// Dateiname wie kurz() in etiketten.py: „Bambu Lab“ → „bambu-lab“. Die Bilder kommen aus pi/hersteller.
String kurz(const std::string& name) {
    std::string n;
    for (size_t i = 0; i < name.size(); i++) {
        unsigned char c = name[i];
        if (c == 0xC3 && i + 1 < name.size()) {  // ä ö ü ß Ä Ö Ü in UTF-8
            unsigned char d = name[++i];
            if (d == 0xA4 || d == 0x84) n += "ae";
            else if (d == 0xB6 || d == 0x96) n += "oe";
            else if (d == 0xBC || d == 0x9C) n += "ue";
            else if (d == 0x9F) n += "ss";
            else n += '-';
            continue;
        }
        if (isalnum(c) && c < 0x80) n += (char)tolower(c);
        else n += '-';
    }
    std::string out;
    for (char c : n)
        if (c != '-' || (!out.empty() && out.back() != '-')) out += c;
    while (!out.empty() && out.back() == '-') out.pop_back();
    return String(out.substr(0, 60).c_str());
}

String bild_pfad(const std::string& hersteller) {
    String k = kurz(hersteller);
    if (!k.length() || !g_fs_ok) return "";
    for (const char* e : {".png", ".jpg", ".webp"}) {
        String p = "/hersteller/" + k + e;
        if (LittleFS.exists(p)) return p;
    }
    return "";
}

// ---------------------------------------------------------------- Herstellerbilder von außen
// Logos liegen z. B. in Home Assistant unter /config/www/hersteller/ (→ http://ha:8123/local/hersteller/).
// Die Waage holt ein Logo erst, wenn die Oberfläche es anfragt, und behält es im PSRAM.
struct ExternesBild {
    std::atomic<int> zustand{0};  // 0 = wird geladen, 1 = da, 2 = gibt es nicht
    std::string daten;
    const char* typ = "image/png";
    uint32_t seit = millis();
};
std::map<String, std::shared_ptr<ExternesBild>> g_bilder;  // unter g_mx
QueueHandle_t g_bild_auftraege = nullptr;
constexpr size_t BILD_MAX_BYTES = 200 * 1024, BILDER_SPEICHER_MAX = 1024 * 1024;

String bild_quelle() {  // g_mx gehalten
    String q = g_einst["bild_quelle"] | "";
    if (!q.length()) {
        q = g_ha.url;
        while (q.endsWith("/")) q.remove(q.length() - 1);
        q += "/local/hersteller/";
    }
    if (!q.endsWith("/")) q += "/";
    return q;
}

void bild_holen(const String& k, std::shared_ptr<ExternesBild> b) {
    String basis;
    {
        std::lock_guard<std::mutex> l(g_mx);
        basis = bild_quelle();
    }
    basis = url_aufloesen(basis);
    static const struct { const char* endung; const char* typ; } ARTEN[] = {
        {".png", "image/png"}, {".jpg", "image/jpeg"}, {".webp", "image/webp"}};
    for (const auto& a : ARTEN) {
        if (WiFi.status() != WL_CONNECTED) break;
        HTTPClient http;
        WiFiClient plain;
        WiFiClientSecure tls;
        String url = basis + k + a.endung;
        bool ok = url.startsWith("https://") ? (tls.setInsecure(), http.begin(tls, url)) : http.begin(plain, url);
        if (!ok) break;
        http.setTimeout(5000);
        int code = http.GET();
        if (code == 200) {
            int laenge = http.getSize();
            if (laenge > 0 && (size_t)laenge > BILD_MAX_BYTES) { http.end(); break; }
            String inhalt = http.getString();
            http.end();
            if (inhalt.length() == 0 || inhalt.length() > BILD_MAX_BYTES) break;
            b->daten.assign(inhalt.c_str(), inhalt.length());
            b->typ = a.typ;
            b->zustand = 1;
            return;
        }
        http.end();
        if (code > 0 && code != 404) break;  // Server antwortet, aber nicht mit dem Bild – nicht weiter raten
    }
    b->zustand = 2;
}

void bild_task(void*) {
    for (;;) {
        String* k = nullptr;
        if (xQueueReceive(g_bild_auftraege, &k, portMAX_DELAY) != pdTRUE) continue;
        std::shared_ptr<ExternesBild> b;
        {
            std::lock_guard<std::mutex> l(g_mx);
            auto it = g_bilder.find(*k);
            if (it != g_bilder.end()) b = it->second;
        }
        if (b && b->zustand == 0) bild_holen(*k, b);
        // Speicher begrenzen: älteste Bilder fallen raus
        {
            std::lock_guard<std::mutex> l(g_mx);
            size_t summe = 0;
            for (auto& [n, x] : g_bilder) summe += x->daten.size();
            while (summe > BILDER_SPEICHER_MAX) {
                auto alt = g_bilder.end();
                for (auto it = g_bilder.begin(); it != g_bilder.end(); ++it)
                    if (it->second->zustand == 1 && (alt == g_bilder.end() || it->second->seit < alt->second->seit)) alt = it;
                if (alt == g_bilder.end()) break;
                summe -= alt->second->daten.size();
                g_bilder.erase(alt);
            }
        }
        delete k;
    }
}

// Zustand eines externen Logos: 0 = noch nie angefragt oder lädt, 1 = da, 2 = gibt es nicht
int bild_zustand(const String& k) {  // g_mx gehalten
    auto it = g_bilder.find(k);
    return it == g_bilder.end() ? 0 : it->second->zustand.load();
}

void bild_vergessen() {  // nach Änderung der Bildquelle
    std::lock_guard<std::mutex> l(g_mx);
    g_bilder.clear();
}

// ---------------------------------------------------------------- JSON für die Oberfläche
void sorte_json(JsonObject o, const Filament& f) {
    char hex[8];
    snprintf(hex, sizeof(hex), "#%06lx", (unsigned long)(f.rgb & 0xffffff));
    o["uid"] = f.uid;
    o["summary"] = f.summary;
    o["hersteller"] = f.hersteller;
    o["material"] = f.material;
    o["farbe"] = f.farbe;
    o["hex"] = hex;
    o["kurzname"] = f.kurzname();
    JsonArray b = o["besonderheiten"].to<JsonArray>();
    for (const auto& x : f.besonderheiten) b.add(x);
    o["gewicht"] = f.gewicht;
    o["spulen"] = f.spulen;
    o["refills"] = f.refills;
    JsonArray r = o["reste"].to<JsonArray>();
    for (int g : f.reste) r.add(g);
    o["duese_min"] = f.duese_min;
    o["duese_max"] = f.duese_max;
    o["bett_min"] = f.bett_min;
    o["bett_max"] = f.bett_max;
    o["trocknen_c"] = f.trocknen_c;
    o["trocknen_h"] = f.trocknen_h;
    o["durchmesser"] = f.durchmesser;
    o["leer_g"] = f.leer_g;
    o["fcode"] = f.fcode;
    o["bambu_id"] = f.bambu_id;
    o["ort"] = f.ort;
    o["getrocknet"] = f.getrocknet;
    JsonArray c = o["codes"].to<JsonArray>();
    for (const auto& x : f.codes) c.add(x);
    JsonArray n = o["nfc"].to<JsonArray>();
    for (const auto& x : f.nfc) n.add(x);
    o["bestand_g"] = f.bestand_g();
    // Werte für die Etikett-Zeilen (Flow, TD, K-Wert … aus SchichtLohn) unverändert aus der Beschreibung
    JsonDocument d;
    if (!f.description.empty() && !deserializeJson(d, f.description)) {
        for (const char* k : {"flow", "td", "mvs", "k", "kammer", "schwund", "buegeln_tempo", "buegeln_flow", "ams", "ams_modelle", "sku", "sl_id"})
            if (!d[k].isNull()) o[k] = d[k];
    }
}

String zustand_text() {
    Messwert m = messwert();
    JsonDocument d;
    d["brutto_g"] = m.gramm;
    d["stabil"] = m.stabil;
    d["kalibriert"] = m.kalibriert;
    d["tag_l"] = m.tag_l;
    d["tag_r"] = m.tag_r;
    d["sensor_ok"] = m.hx711_ok;
    {
        std::lock_guard<std::mutex> l(g_mx);
        d["ha_ok"] = g_ha_ok;
        d["lager_version"] = g_lager_version;
        d["leer_standard_g"] = g_einst["leerspule_g"] | 250;
    }
    struct tm t;
    char uhr[6] = "";
    if (getLocalTime(&t, 0) && t.tm_year > 120) strftime(uhr, sizeof(uhr), "%H:%M", &t);
    d["uhrzeit"] = uhr;
    d["version"] = FW_VERSION;
    d["ip"] = ip_text();
    d["drucker"] = drucker_zustand();
    String s;
    serializeJson(d, s);
    return s;
}

void senden(AsyncWebServerRequest* r, int code, const JsonDocument& d) {
    AsyncResponseStream* res = r->beginResponseStream("application/json; charset=utf-8");
    res->setCode(code);
    res->addHeader("Cache-Control", "no-store");
    serializeJson(d, *res);
    r->send(res);
}

void ok_text(AsyncWebServerRequest* r, bool ok, const String& text, int code = 200) {
    JsonDocument d;
    d["ok"] = ok;
    d["text"] = text;
    senden(r, code, d);
}

void angenommen(AsyncWebServerRequest* r) {
    JsonDocument d;
    d["ok"] = true;
    senden(r, 202, d);
}

// POST mit JSON-Körper
void post(const char* pfad, std::function<void(AsyncWebServerRequest*, JsonObject)> fn, size_t max = 4096) {
    auto* h = new AsyncCallbackJsonWebHandler(pfad, [fn](AsyncWebServerRequest* r, JsonVariant& j) {
        JsonObject o = j.is<JsonObject>() ? j.as<JsonObject>() : JsonObject();
        fn(r, o);
    });
    h->setMethod(HTTP_POST);
    h->setMaxContentLength(max);
    g_server.addHandler(h);
}

const char SEITE_OHNE_DATEIEN[] =
    "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'>"
    "<title>FilamentLager-Waage</title><body style='font-family:system-ui;background:#0d0f12;color:#f1f3f5;padding:24px'>"
    "<h2>Die Oberfläche fehlt noch</h2><p>Die Firmware läuft, aber das Dateisystem mit der Oberfläche ist leer.</p>"
    "<p>Am einfachsten mit dem Webflasher (FilamentLager-Webflasher.html) neu flashen – oder per Kommandozeile "
    "<code>pio run -t uploadfs</code> im Ordner firmware/sensor-s3zero.</p>";

void routen() {
    g_server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
        if (g_fs_ok && (LittleFS.exists("/index.html.gz") || LittleFS.exists("/index.html"))) {
            // Startseite nie zwischenspeichern – sie verweist mit Prüfsummen auf die aktuellen JS/CSS-Dateien
            AsyncWebServerResponse* res = r->beginResponse(LittleFS, "/index.html", "text/html; charset=utf-8");
            res->addHeader("Cache-Control", "no-cache");
            r->send(res);
        }
        else
            r->send(200, "text/html; charset=utf-8", SEITE_OHNE_DATEIEN);
    });
    g_server.serveStatic("/static/", LittleFS, "/").setCacheControl("max-age=600");

    g_server.on("/api/zustand", HTTP_GET, [](AsyncWebServerRequest* r) {
        r->send(200, "application/json", zustand_text());
    });
    g_server.on("/api/lager", HTTP_GET, [](AsyncWebServerRequest* r) {
        std::shared_ptr<const std::string> json;
        {
            std::lock_guard<std::mutex> l(g_mx);
            json = g_lager_json;
        }
        AsyncWebServerResponse* res = r->beginResponse(
            "application/json; charset=utf-8", json->size(), [json](uint8_t* puf, size_t max, size_t index) -> size_t {
                size_t n = std::min(max, json->size() - index);
                memcpy(puf, json->data() + index, n);
                return n;
            });
        res->addHeader("Cache-Control", "no-store");
        r->send(res);
    });
    // Zum Nachsehen, wenn etwas klemmt: Speicher und Lager
    g_server.on("/api/diagnose", HTTP_GET, [](AsyncWebServerRequest* r) {
        JsonDocument d;
        d["version"] = FW_VERSION;
        d["laufzeit_s"] = millis() / 1000;
        d["intern_frei"] = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        d["intern_groesster_block"] = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        d["psram_gesamt"] = ESP.getPsramSize();
        d["psram_frei"] = ESP.getFreePsram();
        {
            std::lock_guard<std::mutex> l(g_mx);
            d["sorten"] = g_lager.sorten.size();
            d["lager_version"] = g_lager_version;
            d["lager_json_bytes"] = g_lager_json->size();
            d["ha_ok"] = g_ha_ok;
            d["ha_url"] = g_ha.url;
        }
        d["wlan_rssi"] = WiFi.RSSI();
        static const char* const GRUND[] = {"unbekannt", "eingeschaltet", "extern", "neu gestartet", "Absturz",
                                            "Watchdog (Interrupt)", "Watchdog (Task)", "Watchdog", "Tiefschlaf",
                                            "Unterspannung", "SDIO"};
        int g = (int)esp_reset_reason();
        d["neustart_grund"] = g >= 0 && g < 11 ? GRUND[g] : "unbekannt";
        d["bluetooth_gesperrt"] = drucker_gesperrt();
        d["start"] = start_bericht();
        d["druck"] = drucker_bericht();
        senden(r, 200, d);
    });
    // Einstellungen → System: Temperatur, Leistung, Speicher (gleiche Form wie auf dem Pi)
    g_server.on("/api/system/info", HTTP_GET, [](AsyncWebServerRequest* r) {
        static uint32_t letzte_zeit = 0, letzte_loops = 0;
        uint32_t jetzt = millis(), loops = g_loops;
        float rate = letzte_zeit && jetzt > letzte_zeit ? (loops - letzte_loops) * 1000.0f / (jetzt - letzte_zeit) : -1;
        letzte_zeit = jetzt;
        letzte_loops = loops;
        JsonDocument d;
        d["geraet"] = "Sensorplatine";
        d["temperatur_c"] = roundf(temperatureRead() * 10) / 10;
        d["cpu_mhz"] = getCpuFrequencyMhz();
        d["kerne"] = ESP.getChipCores();
        if (rate >= 0) d["schleife_pro_s"] = (int)rate;
        d["aufgaben"] = uxTaskGetNumberOfTasks();
        if (WiFi.status() == WL_CONNECTED) {
            d["wlan_rssi"] = WiFi.RSSI();
            d["sendeleistung_dbm"] = WiFi.getTxPower() / 4.0f;
        }
        JsonArray sp = d["speicher"].to<JsonArray>();
        auto eintrag = [&](const char* name, uint32_t belegt, uint32_t gesamt, const String& text) {
            JsonObject o = sp.add<JsonObject>();
            o["name"] = name;
            o["belegt"] = belegt;
            o["gesamt"] = gesamt;
            o["text"] = text;
        };
        uint32_t ig = heap_caps_get_total_size(MALLOC_CAP_INTERNAL), ifr = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        eintrag("Arbeitsspeicher", ig - ifr, ig,
                "größter freier Block " + String(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024) + " KB");
        if (ESP.getPsramSize()) {
            size_t sorten, bytes;
            {
                std::lock_guard<std::mutex> l(g_mx);
                sorten = g_lager.sorten.size();
                bytes = g_lager_json->size();
            }
            eintrag("Zusatzspeicher (PSRAM)", ESP.getPsramSize() - ESP.getFreePsram(), ESP.getPsramSize(),
                    "Lager " + String(sorten) + " Sorten, " + String(bytes / 1024) + " KB");
        }
        const esp_partition_t* app = esp_ota_get_running_partition();
        if (app) eintrag("Firmware", ESP.getSketchSize(), app->size, "Platz für Updates über WLAN");
        if (g_fs_ok) {
            int logos = 0;
            File dir = LittleFS.open("/hersteller");
            if (dir && dir.isDirectory())
                for (File f = dir.openNextFile(); f; f = dir.openNextFile()) logos++;
            eintrag("Oberfläche (Flash)", LittleFS.usedBytes(), LittleFS.totalBytes(), String(logos) + " Herstellerlogos");
        }
        d["laufzeit_s"] = millis() / 1000;
        static const char* const GRUND[] = {"unbekannt", "eingeschaltet", "extern", "neu gestartet", "Absturz",
                                            "Watchdog (Interrupt)", "Watchdog (Task)", "Watchdog", "Tiefschlaf",
                                            "Unterspannung", "SDIO"};
        int g = (int)esp_reset_reason();
        d["neustart_grund"] = g >= 0 && g < 11 ? GRUND[g] : "unbekannt";
        d["board"] = String(ESP.getChipModel()) + " · " + String(ESP.getFlashChipSize() / 1048576) + " MB Flash" +
                     (ESP.getPsramSize() ? " · " + String((ESP.getPsramSize() + 524288) / 1048576) + " MB PSRAM" : String(""));
        senden(r, 200, d);
    });
    g_server.on("/api/einstellungen", HTTP_GET, [](AsyncWebServerRequest* r) {
        JsonDocument d;
        einst_json(d);
        senden(r, 200, d);
    });
    g_server.on("/api/wlan/netze", HTTP_GET, [](AsyncWebServerRequest* r) {
        int n = WiFi.scanComplete();
        if (n < 0) {  // läuft noch oder noch nicht gestartet – die Oberfläche fragt gleich nochmal
            if (n == WIFI_SCAN_FAILED) WiFi.scanNetworks(true);
            JsonDocument d;
            d["laeuft"] = true;
            return senden(r, 202, d);
        }
        JsonDocument d;
        JsonArray a = d.to<JsonArray>();
        for (int i = 0; i < n; i++) {
            String ssid = WiFi.SSID(i);
            if (!ssid.length()) continue;
            bool doppelt = false;
            for (JsonObject x : a) doppelt |= x["ssid"] == ssid;
            if (doppelt) continue;
            JsonObject o = a.add<JsonObject>();
            o["ssid"] = ssid;
            o["signal"] = signal_prozent(WiFi.RSSI(i));
            o["gesichert"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
        }
        WiFi.scanDelete();
        senden(r, 200, d);
    });

    // AMS gibt es nur in der Pi-Variante – die Oberfläche bekommt eine leere Antwort
    g_server.on("/api/ams", HTTP_GET, [](AsyncWebServerRequest* r) {
        r->send(200, "application/json", "{\"drucker\":[],\"abbuchungen\":[]}");
    });
    // Etiketten: Phomemo per Bluetooth (drucker.cpp)
    auto etikett = [](AsyncWebServerRequest* r) {
        JsonDocument d;
        drucker_einstellungen(d);
        JsonObject bilder = d["bilder"].to<JsonObject>();
        d["bilder_eigen"].to<JsonArray>();  // hochladen geht nur über das Repository (pi/hersteller)
        JsonArray h = d["hersteller"].to<JsonArray>();
        std::lock_guard<std::mutex> l(g_mx);
        std::vector<std::string> namen;
        for (const Filament& f : g_lager.sorten)
            if (!f.hersteller.empty() && std::find(namen.begin(), namen.end(), f.hersteller) == namen.end()) namen.push_back(f.hersteller);
        std::sort(namen.begin(), namen.end());
        JsonObject status = d["bild_status"].to<JsonObject>();
        for (const auto& n : namen) {
            h.add(n);
            if (bild_pfad(n).length()) {  // mitgeliefert (pi/hersteller → LittleFS)
                bilder[n] = 1;
                status[n] = "auf der Waage";
                continue;
            }
            int z = bild_zustand(kurz(n));
            if (z != 2) bilder[n] = 2;  // Version 2: die Oberfläche fragt an, die Waage lädt nach
            status[n] = z == 1 ? "geladen" : z == 2 ? "kein Bild gefunden" : "lädt bei Bedarf";
        }
        d["bild_quelle"] = g_einst["bild_quelle"] | "";
        d["bild_quelle_standard"] = [] { String q = g_ha.url; while (q.endsWith("/")) q.remove(q.length() - 1); return q + "/local/hersteller/"; }();
        d["bild_hochladen"] = false;
        String url = g_ha.url;
        while (url.endsWith("/")) url.remove(url.length() - 1);
        d["lager_url"] = url + "/todo?entity_id=" + g_ha.todo;
        senden(r, 200, d);
    };
    g_server.on("/api/etikett/einstellungen", HTTP_GET, etikett);
    post("/api/etikett/einstellungen", [etikett](AsyncWebServerRequest* r, JsonObject o) {
        drucker_setzen(o);
        if (o["bild_quelle"].is<const char*>()) {
            String q = o["bild_quelle"].as<const char*>();
            q.trim();
            if (q.length() && !q.startsWith("http://") && !q.startsWith("https://"))
                return ok_text(r, false, "Bildquelle muss mit http:// oder https:// beginnen");
            {
                std::lock_guard<std::mutex> l(g_mx);
                g_einst["bild_quelle"] = q;
                einst_speichern();
            }
            bild_vergessen();
        }
        etikett(r);
    });
    g_server.on("/api/etikett/suchen", HTTP_GET, [](AsyncWebServerRequest* r) {
        JsonDocument d;
        bool fertig = drucker_suche(d);
        senden(r, fertig ? 200 : 202, d);
    });
    post("/api/etikett/drucken", [](AsyncWebServerRequest* r, JsonObject o) {
        if (const char* f = drucker_drucken(o)) return ok_text(r, false, f, 400);
        angenommen(r);
    }, 160 * 1024);
    // Zum Testen im Browser aufrufbar: http://filamentwaage.local/api/etikett/minitest
    g_server.on("/api/etikett/minitest", HTTP_GET, [](AsyncWebServerRequest* r) {
        drucker_minitest();
        r->send(200, "text/plain; charset=utf-8",
                "Mini-Test gestartet: zwei schwarze Balken.\nIn ca. 15 s /api/diagnose ansehen (Zeile druck).");
    });
    post("/api/etikett/trennen", [](AsyncWebServerRequest* r, JsonObject) {
        drucker_trennen();
        ok_text(r, true, "Drucker getrennt und entfernt – über „Drucker suchen“ neu einrichten");
    });
    post("/api/hersteller_bild/loeschen", [](AsyncWebServerRequest* r, JsonObject) { angenommen(r); });
    g_server.on("/api/hersteller_bild", HTTP_GET, [](AsyncWebServerRequest* r) {
        std::string name = r->hasParam("name") ? r->getParam("name")->value().c_str() : "";
        String p = bild_pfad(name);
        if (p.length()) {  // mitgeliefert
            const char* typ = p.endsWith(".png") ? "image/png" : p.endsWith(".jpg") ? "image/jpeg" : "image/webp";
            AsyncWebServerResponse* res = r->beginResponse(LittleFS, p, typ);
            res->addHeader("Cache-Control", "max-age=3600");
            return r->send(res);
        }
        String k = kurz(name);
        if (!k.length()) return r->send(404, "text/plain", "kein Bild");
        std::shared_ptr<ExternesBild> b;
        bool neu = false;
        {
            std::lock_guard<std::mutex> l(g_mx);
            auto it = g_bilder.find(k);
            if (it == g_bilder.end()) {
                b = std::make_shared<ExternesBild>();
                g_bilder[k] = b;
                neu = true;
            } else {
                b = it->second;
            }
        }
        if (neu) {
            auto* auftrag = new String(k);
            if (xQueueSend(g_bild_auftraege, &auftrag, 0) != pdTRUE) {
                delete auftrag;
                b->zustand = 2;
            }
        }
        if (b->zustand == 2) return r->send(404, "text/plain", "kein Bild");
        if (b->zustand == 1) {
            // über einen Filler, der das Bild festhält – so darf es währenddessen aus dem Speicher fallen
            AsyncWebServerResponse* res = r->beginResponse(b->typ, b->daten.size(), [b](uint8_t* puf, size_t max, size_t index) -> size_t {
                size_t n = std::min(max, b->daten.size() - index);
                memcpy(puf, b->daten.data() + index, n);
                return n;
            });
            res->addHeader("Cache-Control", "max-age=3600");
            return r->send(res);
        }
        // Noch nicht da: Antwort offen halten, bis die Waage das Logo geholt hat (höchstens 10 s).
        // Ein leerer Körper heißt für die Oberfläche „kein Bild“ – sie druckt dann den Namen.
        uint32_t start = millis();
        r->send(r->beginChunkedResponse("image/png", [b, start](uint8_t* puf, size_t max, size_t index) -> size_t {
            int z = b->zustand;
            if (z == 0) return millis() - start < 10000 ? RESPONSE_TRY_AGAIN : 0;
            if (z != 1 || index >= b->daten.size()) return 0;
            size_t n = std::min(max, b->daten.size() - index);
            memcpy(puf, b->daten.data() + index, n);
            return n;
        }));
    });
    g_server.on("/api/hersteller_bild", HTTP_POST, [](AsyncWebServerRequest* r) {
        ok_text(r, false, "Auf der Sensorplatine: Bild im Repository unter pi/hersteller ablegen, dann neu flashen");
    });

    post("/api/tara", [](AsyncWebServerRequest* r, JsonObject) {
        if (const char* f = vorgang_starten(false, 0, nullptr)) meldung(f, true);
        angenommen(r);
    });
    post("/api/kalibrieren", [](AsyncWebServerRequest* r, JsonObject o) {
        int g = o["g"] | 1000;
        if (const char* f = vorgang_starten(true, g, nullptr)) meldung(f, true);
        else meldung("Kalibriere mit " + String(g) + " g …");
        angenommen(r);
    });
    post("/api/rest", [](AsyncWebServerRequest* r, JsonObject o) {
        if (!messwert().stabil) {
            meldung("Gewicht noch unruhig – kurz warten", true);
            JsonDocument d;
            d["ok"] = false;
            return senden(r, 200, d);
        }
        meldung("Speichere …");
        auftrag(Art::REST, o["sorte"] | "", o["tag"] | "", o["netto"] | 0);
        angenommen(r);
    });
    post("/api/zuordnen", [](AsyncWebServerRequest* r, JsonObject o) {
        meldung("Ordne Tag zu …");
        auftrag(Art::ZUORDNEN, o["sorte"] | "", o["tag"] | "");
        angenommen(r);
    });
    post("/api/spulen", [](AsyncWebServerRequest* r, JsonObject o) {
        auftrag(Art::SPULEN, o["sorte"] | "", "", o["delta"] | 0);
        angenommen(r);
    });
    post("/api/feld", [](AsyncWebServerRequest* r, JsonObject o) {
        auto* a = new Auftrag{Art::FELD, o["sorte"] | "", o["feld"] | "", 0, {}};
        a->wert = (const char*)(o["wert"] | "");
        auftrag(a);
        angenommen(r);
    });
    post("/api/neu_laden", [](AsyncWebServerRequest* r, JsonObject) {
        meldung("Lade Lager …");
        auftrag(Art::LADEN);
        angenommen(r);
    });
    post("/api/einstellungen", [](AsyncWebServerRequest* r, JsonObject o) {
        {
            std::lock_guard<std::mutex> l(g_mx);
            static const char* const ERLAUBT[] = {"akzent", "helligkeit", "display_aus_min", "kompakt",
                                                   "leerspule_g", "nfc_stufe", "drucker", "ams_suche"};
            for (const char* k : ERLAUBT)
                if (!o[k].isNull()) g_einst[k] = o[k];
            einst_standard(g_einst);
            einst_speichern();
        }
        JsonDocument d;
        einst_json(d);
        senden(r, 200, d);
    });
    post("/api/wlan", [](AsyncWebServerRequest* r, JsonObject o) {
        String ssid = o["ssid"] | "", pw = o["passwort"] | "";
        if (!ssid.length() || ssid.length() > 32 || pw.length() > 63) return ok_text(r, false, "WLAN-Name oder Passwort ungültig");
        g_wlan_ssid = ssid;
        g_wlan_pw = pw;
        g_wlan_um = millis() + 500;  // erst antworten, dann umschalten
        ok_text(r, true, "Verbinde mit " + ssid + " – die Waage ist danach unter ihrer neuen Adresse erreichbar");
    });
    auto ha_aus = [](JsonObject o) {
        HaCfg c = ha_cfg();
        if (o["url"].is<const char*>() && strlen(o["url"]) > 0) c.url = o["url"].as<const char*>();
        if (o["todo"].is<const char*>() && strlen(o["todo"]) > 0) c.todo = o["todo"].as<const char*>();
        if (o["token"].is<const char*>() && strlen(o["token"]) > 0) c.token = o["token"].as<const char*>();
        c.url.trim();
        c.token.trim();
        while (c.url.endsWith("/")) c.url.remove(c.url.length() - 1);
        return c;
    };
    post("/api/ha/test", [ha_aus](AsyncWebServerRequest* r, JsonObject o) {
        HaCfg c = ha_aus(o);
        if (!c.url.startsWith("http://") && !c.url.startsWith("https://")) return ok_text(r, false, "Adresse muss mit http:// oder https:// beginnen");
        auto* a = new Auftrag{Art::TEST, "", "", 0, c};
        auftrag(a);
        ok_text(r, true, "Teste Verbindung …");
    });
    post("/api/ha", [ha_aus](AsyncWebServerRequest* r, JsonObject o) {
        HaCfg c = ha_aus(o);
        if (!c.url.startsWith("http://") && !c.url.startsWith("https://")) return ok_text(r, false, "Adresse muss mit http:// oder https:// beginnen");
        {
            std::lock_guard<std::mutex> l(g_mx);
            g_ha = c;
            einst_speichern();
        }
        auftrag(Art::LADEN);
        ok_text(r, true, c.token.length() ? "Gespeichert – lade Lager …" : "Gespeichert – es fehlt noch das Token");
    });
    post("/api/system", [](AsyncWebServerRequest* r, JsonObject o) {
        String aktion = o["aktion"] | "";
        if (aktion == "neustart") {
            meldung("Waage startet neu …");
            g_neustart_um = millis() + 800;
        } else if (aktion == "werk") {
            meldung("Setze alles zurück und starte neu …");
            {
                std::lock_guard<std::mutex> l(g_mx);
                g_prefs.begin("ui", false);
                g_prefs.clear();
                g_prefs.end();
            }
            alles_zuruecksetzen();
            g_neustart_um = millis() + 800;
        } else if (aktion == "update") {
            meldung("Neue Version: FilamentLager-Webflasher.html unter „Update einspielen“ wählen");
        }
        angenommen(r);
    });

    // ------------------------------------------------ Update über WLAN
    // Die Oberfläche nimmt Firmware und Oberfläche aus FilamentLager-Webflasher.html und lädt sie hier hoch:
    // erst ?ziel=oberflaeche (LittleFS), dann ?ziel=firmware (freier OTA-Platz), danach Neustart.
    g_server.on("/api/update/info", HTTP_GET, [](AsyncWebServerRequest* r) {
        JsonDocument d;
        const esp_partition_t* fs = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
        const esp_partition_t* frei = esp_ota_get_next_update_partition(nullptr);
        d["version"] = FW_VERSION;
        d["fs_offset"] = fs ? fs->address : 0;
        d["fs_groesse"] = fs ? fs->size : 0;
        d["app_max"] = frei ? frei->size : 0;
        d["ota"] = frei != nullptr && frei != esp_ota_get_running_partition();
        senden(r, 200, d);
    });
    static bool s_update_ok = false;
    static String s_update_fehler;
    g_server.on(
        "/api/update", HTTP_POST,
        [](AsyncWebServerRequest* r) {
            bool fs = r->hasParam("ziel") && r->getParam("ziel")->value() == "oberflaeche";
            if (!s_update_ok) {
                if (fs) g_fs_ok = LittleFS.begin(false);
                return ok_text(r, false, s_update_fehler.length() ? s_update_fehler : String("Update fehlgeschlagen"), 400);
            }
            if (fs) {
                g_fs_ok = LittleFS.begin(false);
                return ok_text(r, true, "Oberfläche aktualisiert");
            }
            meldung("Firmware aktualisiert – Waage startet neu …");
            g_neustart_um = millis() + 1500;
            ok_text(r, true, "Firmware aktualisiert – Waage startet neu");
        },
        [](AsyncWebServerRequest* r, const String&, size_t index, uint8_t* daten, size_t laenge, bool fertig) {
            bool fs = r->hasParam("ziel") && r->getParam("ziel")->value() == "oberflaeche";
            if (index == 0) {
                s_update_fehler = "";
                if (fs) {
                    LittleFS.end();
                    g_fs_ok = false;
                }
                // Ein abgebrochener früherer Versuch hält das Update sonst „belegt“ – dann meldet
                // Update.begin() nur „No Error“, bis die Waage neu startet.
                if (Update.isRunning()) Update.abort();
                s_update_ok = Update.begin(UPDATE_SIZE_UNKNOWN, fs ? U_SPIFFS : U_FLASH);
                if (!s_update_ok) s_update_fehler = String("Update lässt sich nicht starten: ") + Update.errorString();
            }
            if (s_update_ok && laenge && Update.write(daten, laenge) != laenge) {
                s_update_ok = false;
                s_update_fehler = String("Schreiben fehlgeschlagen: ") + Update.errorString();
                Update.abort();
            }
            if (fertig && s_update_ok && !Update.end(true)) {
                s_update_ok = false;
                s_update_fehler = String("Update unvollständig: ") + Update.errorString();
            }
        });

    g_server.onNotFound([](AsyncWebServerRequest* r) { r->send(404, "text/plain", "nicht gefunden"); });

    g_events.onConnect([](AsyncEventSourceClient* c) { c->send(zustand_text().c_str(), "zustand", millis(), 2000); });
    g_server.addHandler(&g_events);
}

}  // namespace

void oberflaeche_start() {
    einst_laden();
    g_fs_ok = LittleFS.begin(false);  // nicht formatieren: sonst wäre die Oberfläche weg
    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "fritz.box", "pool.ntp.org");
    g_vorgang_fertig = [](bool ok, const String& text) { meldung(text, !ok); };
    g_drucker_meldung = [](const String& text, bool fehler) { meldung(text, fehler); };
    drucker_start();
    g_bild_auftraege = xQueueCreate(32, sizeof(String*));
    xTaskCreatePinnedToCore(bild_task, "bilder", 8192, nullptr, 1, nullptr, 1);
    g_auftraege = xQueueCreate(16, sizeof(Auftrag*));
    xTaskCreatePinnedToCore(arbeiter, "ha", 16384, nullptr, 1, nullptr, 1);
    routen();
    g_server.begin();
}

void oberflaeche_weiter() {
    static uint32_t zuletzt = 0;
    uint32_t jetzt = millis();
    if (jetzt - zuletzt >= 150) {
        zuletzt = jetzt;
        if (g_events.count()) {
            // nur senden, wenn sich etwas geändert hat (die Uhrzeit zählt mit), sonst alle 2 s als Lebenszeichen
            static String zuletzt_text;
            static uint32_t zuletzt_gesendet = 0;
            String z = zustand_text();
            if (z != zuletzt_text || jetzt - zuletzt_gesendet > 2000) {
                g_events.send(z.c_str(), "zustand", jetzt);
                zuletzt_text = z;
                zuletzt_gesendet = jetzt;
            }
            std::deque<Meldung> neu;
            {
                std::lock_guard<std::mutex> l(g_mx);
                neu.swap(g_meldungen);
            }
            for (const Meldung& m : neu) {
                JsonDocument d;
                d["text"] = m.text;
                d["fehler"] = m.fehler;
                String s;
                serializeJson(d, s);
                g_events.send(s.c_str(), "meldung", jetzt);
            }
        } else {
            std::lock_guard<std::mutex> l(g_mx);
            g_meldungen.clear();  // niemand schaut zu – alte Meldungen nicht nachliefern
        }
    }
    if (g_wlan_um && (int32_t)(jetzt - g_wlan_um) >= 0) {
        g_wlan_um = 0;
        wlan_setzen(g_wlan_ssid.c_str(), g_wlan_pw.c_str());
    }
    if (g_neustart_um && (int32_t)(jetzt - g_neustart_um) >= 0) ESP.restart();
}
