// Phomemo-Etikettendrucker (M110/M120/M220/M20-Familie) per Bluetooth LE.
// Befehle wie in pi/filamentwaage/phomemo.py; Bluetooth startet erst beim ersten Suchen oder Drucken.

#include "drucker.h"

#include <NimBLEDevice.h>
#include <Preferences.h>
#include <mbedtls/base64.h>

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

#include "config.h"

void (*g_drucker_meldung)(const String& text, bool fehler) = nullptr;

namespace {

const char* const DIENST = "ff00";
const char* const SCHREIBEN = "ff02";
constexpr uint8_t PAPIER_LUECKE = 0x0A;

struct Werte {
    String adresse, name, format = "expanded";
    uint8_t adress_typ = 0;  // 0 = öffentlich, 1 = zufällig (aus der Suche)
    int dichte = 10;
} g_w;

struct Geraet {
    String name, adresse;
    uint8_t typ;
    bool phomemo;
};

struct Druck {
    int breite, hoehe, kopien;
    std::vector<uint8_t> daten;
};

std::mutex g_mx;
std::vector<Geraet> g_geraete;
bool g_suche_fertig = false, g_suche_laeuft = false, g_suche_wunsch = false;
String g_suche_fehler;
bool g_druckt = false;
QueueHandle_t g_druecke = nullptr;
Preferences g_prefs;
bool g_ble_an = false;

void meldung(const String& t, bool fehler = false) {
    if (g_drucker_meldung) g_drucker_meldung(t, fehler);
}

void laden() {
    g_prefs.begin("etikett", true);
    g_w.adresse = g_prefs.getString("adresse", "");
    g_w.name = g_prefs.getString("name", "");
    g_w.format = g_prefs.getString("format", "expanded");
    g_w.adress_typ = g_prefs.getUChar("typ", 0);
    g_w.dichte = g_prefs.getInt("dichte", 10);
    g_prefs.end();
}

void speichern() {  // g_mx gehalten
    g_prefs.begin("etikett", false);
    g_prefs.putString("adresse", g_w.adresse);
    g_prefs.putString("name", g_w.name);
    g_prefs.putString("format", g_w.format);
    g_prefs.putUChar("typ", g_w.adress_typ);
    g_prefs.putInt("dichte", g_w.dichte);
    g_prefs.end();
}

bool wahrscheinlich_phomemo(String n) {
    n.toLowerCase();
    if (n.indexOf("phomemo") >= 0) return true;
    // meldet sich meist nur mit dem Modell, z. B. „M110“, „M220“, „M20Pro“, „Q30“
    return n.length() >= 3 && strchr("mqd", n[0]) && isdigit((unsigned char)n[1]);
}

void ble_an() {
    if (g_ble_an) return;
    NimBLEDevice::init("filamentwaage");
    NimBLEDevice::setMTU(185);
    g_ble_an = true;
}

// ---------------------------------------------------------------- Druckbefehle
std::vector<uint8_t> auf_kopfbreite(int breite, int hoehe, const std::vector<uint8_t>& d, int kopf, int& neu_breite) {
    kopf -= kopf % 8;
    neu_breite = kopf;
    if (breite == kopf) return d;
    int alt = breite / 8, neu = kopf / 8;
    std::vector<uint8_t> out((size_t)neu * hoehe, 0);
    for (int y = 0; y < hoehe; y++) {
        const uint8_t* z = d.data() + (size_t)y * alt;
        uint8_t* o = out.data() + (size_t)y * neu;
        if (alt > neu) memcpy(o, z + (alt - neu) / 2, neu);
        else memcpy(o + (neu - alt) / 2, z, alt);
    }
    return out;
}

void befehle(std::vector<uint8_t>& out, int breite, int hoehe, const std::vector<uint8_t>& daten, int dichte) {
    int bpr = breite / 8;
    dichte = constrain(dichte, 1, 15);
    const uint8_t kopf[] = {0x1B, 0x4E, 0x0D, 5,             // Geschwindigkeit
                            0x1B, 0x4E, 0x04, (uint8_t)dichte,  // Schwärzung
                            0x1F, 0x11, PAPIER_LUECKE,          // Papierart
                            0x1D, 0x76, 0x30, 0x00, (uint8_t)(bpr & 0xFF), (uint8_t)(bpr >> 8),
                            (uint8_t)(hoehe & 0xFF), (uint8_t)(hoehe >> 8)};
    out.insert(out.end(), kopf, kopf + sizeof(kopf));
    out.insert(out.end(), daten.begin(), daten.end());
    const uint8_t ende[] = {0x1F, 0xF0, 0x05, 0x00, 0x1F, 0xF0, 0x03, 0x00};  // bis zur nächsten Lücke
    out.insert(out.end(), ende, ende + sizeof(ende));
}

// ---------------------------------------------------------------- Bluetooth
String senden(const std::vector<uint8_t>& auftrag, const String& adresse, uint8_t typ) {
    if (!adresse.length()) return "Kein Drucker eingestellt – unter Einstellungen → Etiketten suchen";
    ble_an();
    NimBLEClient* c = NimBLEDevice::createClient();
    c->setConnectTimeout(10);
    bool verbunden = c->connect(NimBLEAddress(adresse.c_str(), typ));
    if (!verbunden) verbunden = c->connect(NimBLEAddress(adresse.c_str(), typ ? 0 : 1));  // anderer Adresstyp
    if (!verbunden) {
        NimBLEDevice::deleteClient(c);
        return "Drucker nicht erreichbar – eingeschaltet? Phomemo-App am Handy trennen";
    }
    NimBLERemoteCharacteristic* ziel = nullptr;
    if (NimBLERemoteService* s = c->getService(DIENST)) ziel = s->getCharacteristic(SCHREIBEN);
    if (!ziel) {  // andere Firmware: erste Kennung, die ohne Antwort schreiben kann
        for (NimBLERemoteService* s : *c->getServices(true)) {
            for (NimBLERemoteCharacteristic* k : *s->getCharacteristics(true))
                if (k->canWriteNoResponse()) { ziel = k; break; }
            if (ziel) break;
        }
    }
    String fehler;
    if (!ziel) {
        fehler = "Drucker bietet keinen Schreibkanal an";
    } else {
        size_t stueck = constrain((int)c->getMTU() - 3, 20, 180);
        for (size_t i = 0; i < auftrag.size(); i += stueck) {
            size_t n = std::min(stueck, auftrag.size() - i);
            if (!ziel->writeValue(auftrag.data() + i, n, false)) {
                fehler = "Verbindung zum Drucker abgebrochen";
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(15));
        }
        if (!fehler.length()) vTaskDelay(pdMS_TO_TICKS(1000));  // Drucker erst fertig empfangen lassen
    }
    c->disconnect();
    NimBLEDevice::deleteClient(c);
    return fehler;
}

void suchen() {
    ble_an();
    NimBLEScan* s = NimBLEDevice::getScan();
    s->setActiveScan(true);
    s->setInterval(100);
    s->setWindow(99);
    NimBLEScanResults r = s->start(6, false);
    std::vector<Geraet> out;
    for (int i = 0; i < r.getCount(); i++) {
        NimBLEAdvertisedDevice d = r.getDevice(i);
        String name = d.getName().c_str();
        name.trim();
        if (!name.length()) continue;
        String adr = d.getAddress().toString().c_str();
        bool doppelt = false;
        for (const Geraet& g : out) doppelt |= g.adresse == adr;
        if (!doppelt) out.push_back({name, adr, d.getAddress().getType(), wahrscheinlich_phomemo(name)});
    }
    s->clearResults();
    std::sort(out.begin(), out.end(), [](const Geraet& a, const Geraet& b) {
        if (a.phomemo != b.phomemo) return a.phomemo;
        return a.name.compareTo(b.name) < 0;
    });
    std::lock_guard<std::mutex> l(g_mx);
    g_geraete = out;
    g_suche_fertig = true;
    g_suche_laeuft = false;
}

void arbeiter(void*) {
    for (;;) {
        Druck* d = nullptr;
        if (xQueueReceive(g_druecke, &d, pdMS_TO_TICKS(200)) != pdTRUE) {
            bool los;
            {
                std::lock_guard<std::mutex> l(g_mx);
                los = g_suche_wunsch;
                g_suche_wunsch = false;
            }
            if (los) suchen();
            continue;
        }
        Werte w;
        {
            std::lock_guard<std::mutex> l(g_mx);
            w = g_w;
            g_druckt = true;
        }
        int breite = 0;
        std::vector<uint8_t> raster = auf_kopfbreite(d->breite, d->hoehe, d->daten, ETIKETT_KOPF_PX, breite);
        std::vector<uint8_t> auftrag;
        for (int i = 0; i < d->kopien; i++) befehle(auftrag, breite, d->hoehe, raster, w.dichte);
        meldung("Drucke Etikett …");
        String fehler = senden(auftrag, w.adresse, w.adress_typ);
        if (fehler.length()) meldung(fehler, true);
        else meldung(d->kopien == 1 ? String("Etikett gedruckt") : String(d->kopien) + " Etiketten gedruckt");
        {
            std::lock_guard<std::mutex> l(g_mx);
            g_druckt = false;
        }
        delete d;
    }
}

}  // namespace

void drucker_start() {
    laden();
    g_druecke = xQueueCreate(4, sizeof(Druck*));
    xTaskCreatePinnedToCore(arbeiter, "druck", 8192, nullptr, 1, nullptr, 1);
}

void drucker_einstellungen(JsonDocument& d) {
    std::lock_guard<std::mutex> l(g_mx);
    d["verbindung"] = "ble";
    d["adresse"] = g_w.adresse;
    d["name"] = g_w.name;
    d["format"] = g_w.format;
    d["dichte"] = g_w.dichte;
    d["kopf_px"] = ETIKETT_KOPF_PX;
    JsonArray f = d["formate"].to<JsonArray>();
    for (const char* x : {"classic", "expanded", "compact", "vertikal"}) f.add(x);
    d["druckt"] = g_druckt;
}

void drucker_setzen(JsonObjectConst o) {
    std::lock_guard<std::mutex> l(g_mx);
    String format = o["format"] | "";
    if (format == "classic" || format == "expanded" || format == "compact" || format == "vertikal") g_w.format = format;
    if (o["dichte"].is<int>()) g_w.dichte = constrain(o["dichte"].as<int>(), 1, 15);
    if (o["adresse"].is<const char*>()) {
        g_w.adresse = String(o["adresse"].as<const char*>()).substring(0, 40);
        g_w.name = String(o["name"] | "").substring(0, 40);
        g_w.adress_typ = 0;
        for (const Geraet& g : g_geraete)
            if (g.adresse == g_w.adresse) g_w.adress_typ = g.typ;
    }
    speichern();
}

const char* drucker_drucken(JsonObjectConst o) {
    int breite = o["breite"] | 0, hoehe = o["hoehe"] | 0;
    const char* b64 = o["daten"] | "";
    int kopien = constrain((int)(o["kopien"] | 1), 1, 10);
    if (breite <= 0 || breite % 8 || hoehe <= 0 || hoehe > 4000) return "Etikett unvollständig";
    size_t soll = (size_t)breite / 8 * hoehe, laenge = 0;
    auto* d = new Druck{breite, hoehe, kopien, std::vector<uint8_t>(soll + 4)};
    if (mbedtls_base64_decode(d->daten.data(), d->daten.size(), &laenge, (const unsigned char*)b64, strlen(b64)) ||
        laenge != soll) {
        delete d;
        return "Etikett unvollständig";
    }
    d->daten.resize(soll);
    if (xQueueSend(g_druecke, &d, 0) != pdTRUE) {
        delete d;
        return "Drucker ist noch beschäftigt";
    }
    return nullptr;
}

bool drucker_suche(JsonDocument& d) {
    std::lock_guard<std::mutex> l(g_mx);
    if (g_suche_fertig) {
        g_suche_fertig = false;
        d["ok"] = true;
        JsonArray a = d["geraete"].to<JsonArray>();
        for (const Geraet& g : g_geraete) {
            JsonObject o = a.add<JsonObject>();
            o["name"] = g.name;
            o["adresse"] = g.adresse;
            o["phomemo"] = g.phomemo;
        }
        return true;
    }
    if (!g_suche_laeuft) {
        g_suche_laeuft = true;
        g_suche_wunsch = true;
    }
    d["laeuft"] = true;
    return false;
}
