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
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <time.h>

#include <algorithm>
#include <deque>
#include <functional>
#include <mutex>

#include "config.h"
#include "ha_client.h"
#include "model.h"
#include "sensor.h"

namespace {

AsyncWebServer g_server(WEB_PORT);
AsyncEventSource g_events("/api/ereignisse");
std::mutex g_mx;  // schützt alles darunter

Lager g_lager;
int g_lager_version = 0;
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
}

// ---------------------------------------------------------------- Home Assistant (eigener Task)
enum class Art { LADEN, REST, SPULEN, ZUORDNEN, TEST };
struct Auftrag {
    Art art;
    std::string sorte, tag;
    int zahl = 0;
    HaCfg test;
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

bool laden(std::string& fehler) {
    HaClient ha;
    if (!client_fuer(ha_cfg(), ha, fehler)) return false;
    Lager neu;
    if (!ha.lager_laden(neu, fehler)) return false;
    std::lock_guard<std::mutex> l(g_mx);
    g_lager = std::move(neu);
    g_lager_version++;
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
            case Art::TEST: {
                HaClient ha;
                Lager probe;
                ok = client_fuer(a->test, ha, fehler) && ha.lager_laden(probe, fehler);
                if (ok) text = "Verbindung steht – " + std::to_string(probe.sorten.size()) + " Sorten in " + a->test.todo.c_str();
                break;
            }
            case Art::LADEN:
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
    JsonArray c = o["codes"].to<JsonArray>();
    for (const auto& x : f.codes) c.add(x);
    JsonArray n = o["nfc"].to<JsonArray>();
    for (const auto& x : f.nfc) n.add(x);
    o["bestand_g"] = f.bestand_g();
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
void post(const char* pfad, std::function<void(AsyncWebServerRequest*, JsonObject)> fn) {
    auto* h = new AsyncCallbackJsonWebHandler(pfad, [fn](AsyncWebServerRequest* r, JsonVariant& j) {
        JsonObject o = j.is<JsonObject>() ? j.as<JsonObject>() : JsonObject();
        fn(r, o);
    });
    h->setMethod(HTTP_POST);
    h->setMaxContentLength(4096);
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
        if (g_fs_ok && (LittleFS.exists("/index.html.gz") || LittleFS.exists("/index.html")))
            r->send(LittleFS, "/index.html", "text/html; charset=utf-8");
        else
            r->send(200, "text/html; charset=utf-8", SEITE_OHNE_DATEIEN);
    });
    g_server.serveStatic("/static/", LittleFS, "/").setCacheControl("max-age=600");

    g_server.on("/api/zustand", HTTP_GET, [](AsyncWebServerRequest* r) {
        r->send(200, "application/json", zustand_text());
    });
    g_server.on("/api/lager", HTTP_GET, [](AsyncWebServerRequest* r) {
        JsonDocument d;
        {
            std::lock_guard<std::mutex> l(g_mx);
            d["version"] = g_lager_version;
            JsonArray a = d["sorten"].to<JsonArray>();
            for (const Filament& f : g_lager.sorten) sorte_json(a.add<JsonObject>(), f);
        }
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

    // Etiketten und AMS gibt es nur in der Pi-Variante – die Oberfläche bekommt leere Antworten
    g_server.on("/api/ams", HTTP_GET, [](AsyncWebServerRequest* r) {
        r->send(200, "application/json", "{\"drucker\":[],\"abbuchungen\":[]}");
    });
    auto etikett = [](AsyncWebServerRequest* r) {
        JsonDocument d;
        d["verbindung"] = "nur Pi-Variante";
        d["adresse"] = "";
        d["name"] = "";
        d["format"] = "expanded";
        d["dichte"] = 10;
        d["bilder"].to<JsonObject>();
        d["bilder_eigen"].to<JsonArray>();
        JsonArray h = d["hersteller"].to<JsonArray>();
        std::lock_guard<std::mutex> l(g_mx);
        std::vector<std::string> namen;
        for (const Filament& f : g_lager.sorten)
            if (!f.hersteller.empty() && std::find(namen.begin(), namen.end(), f.hersteller) == namen.end()) namen.push_back(f.hersteller);
        std::sort(namen.begin(), namen.end());
        for (const auto& n : namen) h.add(n);
        String url = g_ha.url;
        while (url.endsWith("/")) url.remove(url.length() - 1);
        d["lager_url"] = url + "/todo?entity_id=" + g_ha.todo;
        senden(r, 200, d);
    };
    g_server.on("/api/etikett/einstellungen", HTTP_GET, etikett);
    post("/api/etikett/einstellungen", [etikett](AsyncWebServerRequest* r, JsonObject) { etikett(r); });
    g_server.on("/api/etikett/suchen", HTTP_GET, [](AsyncWebServerRequest* r) {
        r->send(200, "application/json",
                "{\"ok\":false,\"text\":\"Etiketten druckt nur die Pi-Variante\",\"geraete\":[]}");
    });
    post("/api/etikett/drucken", [](AsyncWebServerRequest* r, JsonObject) {
        ok_text(r, false, "Etiketten druckt nur die Pi-Variante (Bluetooth zum Phomemo)");
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
            meldung("Updates kommen über den Webflasher (FilamentLager-Webflasher.html)");
        }
        angenommen(r);
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
            g_events.send(zustand_text().c_str(), "zustand", jetzt);
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
