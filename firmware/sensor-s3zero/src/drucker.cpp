// Phomemo-Etikettendrucker (M110/M120/M220/M20-Familie) per Bluetooth LE.
// Befehle wie in pi/filamentwaage/phomemo.py; Bluetooth startet erst beim ersten Suchen oder Drucken.

#include "drucker.h"

#include <NimBLEDevice.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_system.h>
#include <mbedtls/base64.h>

#include <algorithm>
#include <list>
#include <mutex>
#include <string>
#include <vector>

#include "config.h"

void (*g_drucker_meldung)(const String& text, bool fehler) = nullptr;

// Überlebt einen Absturz (nicht aber das Abschalten): Lief beim Absturz gerade Bluetooth,
// bleibt Bluetooth bis zum nächsten Einschalten aus – sonst startet die Waage in einer Schleife neu.
RTC_NOINIT_ATTR uint32_t g_ble_marke;
constexpr uint32_t BLE_LAEUFT = 0x424C4531;  // "BLE1"
bool g_ble_gesperrt = false;

namespace {

const char* const DIENST = "ff00";
const char* const SCHREIBEN = "ff02";
constexpr uint8_t PAPIER_LUECKE = 0x0A;

struct Werte {
    String adresse, name, format = "expanded";
    String bruecke;  // http://pi:8765 – Druckbrücke für Drucker, die nur klassisches Bluetooth können (M20 Pro)
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
    String titel;
};

std::mutex g_mx;
std::vector<Geraet> g_geraete;
bool g_suche_fertig = false, g_suche_laeuft = false, g_suche_wunsch = false;
String g_suche_fehler;
bool g_druckt = false;
bool g_trennen_wunsch = false;
String g_druck_bericht = "noch nichts gedruckt";
bool g_minitest_wunsch = false;
int g_letzter_druck = -1;  // -1 = noch keiner, 0 = fehlgeschlagen, 1 = geklappt
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
    g_w.bruecke = g_prefs.getString("bruecke", "");
    g_prefs.end();
}

void speichern() {  // g_mx gehalten
    g_prefs.begin("etikett", false);
    g_prefs.putString("adresse", g_w.adresse);
    g_prefs.putString("name", g_w.name);
    g_prefs.putString("format", g_w.format);
    g_prefs.putUChar("typ", g_w.adress_typ);
    g_prefs.putInt("dichte", g_w.dichte);
    g_prefs.putString("bruecke", g_w.bruecke);
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

const char* const GESPERRT = "Die Waage ist bei der letzten Bluetooth-Aktion abgestürzt – Bluetooth bleibt bis zum "
                             "nächsten Aus- und Einschalten aus. Bitte /api/diagnose an den Entwickler schicken.";

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

// ---------------------------------------------------------------- TSPL (M20 Pro und andere 300-dpi-Modelle)
// Aufgezeichnet aus der Phomemo-App: Textbefehle, Bild als BITMAP (1 = weiß, 0 = schwarz).
// Das Raster der Oberfläche hat 8 Punkte/mm (203 dpi) und wird auf 300 dpi hochgerechnet.
bool ist_tspl(String n) {
    n.toUpperCase();
    n.replace(" ", "");
    n.replace("-", "");
    return n.startsWith("M20PRO");
}

void tspl_auftrag(std::vector<uint8_t>& out, int breite, int hoehe, const std::vector<uint8_t>& daten, int dichte, int kopien) {
    const float F = 300.0f / 203.2f;
    int b_mm = (breite + 4) / 8, h_mm = (hoehe + 4) / 8;
    int nb = (int)(breite * F + 0.5f), nh = (int)(hoehe * F + 0.5f);
    int nbpr = (nb + 7) / 8, bpr = breite / 8;
    int tdichte = constrain((dichte * 7 + 5) / 10, 1, 15);  // 10 („Normal“) → 7 wie in der Phomemo-App
    char kopf[200];
    int n = snprintf(kopf, sizeof(kopf),
                     "SPEED 1\r\nSIZE %d mm,%d mm\r\nREFERENCE 0,0\r\nGAP 4.6 mm, 0 mm\r\nOFFSET 0 mm\r\n"
                     "DIRECTION 0,0\r\nDENSITY %d\r\nCLS\r\n",
                     b_mm, h_mm, tdichte);
    out.insert(out.end(), kopf, kopf + n);
    // Bild in 300 dpi (1 = weiß, 0 = schwarz)
    std::vector<uint8_t> bild((size_t)nbpr * nh, 0xFF);
    for (int y = 0; y < nh; y++) {
        int sy = std::min(hoehe - 1, (int)(y / F));
        const uint8_t* z = daten.data() + (size_t)sy * bpr;
        uint8_t* o = bild.data() + (size_t)y * nbpr;
        for (int x = 0; x < nb; x++) {
            int sx = std::min(breite - 1, (int)(x / F));
            if (z[sx >> 3] & (0x80 >> (sx & 7))) o[x >> 3] &= ~(0x80 >> (x & 7));
        }
    }
    // In Streifen schicken: der M20 Pro nimmt ein ganzes unkomprimiertes Bild nicht in einem Stück an
    // (sein Puffer ist nach 9–13 KB voll). Ganz weiße Streifen fallen weg.
    const int STREIFEN = 24;
    for (int y0 = 0; y0 < nh; y0 += STREIFEN) {
        int zeilen = std::min(STREIFEN, nh - y0);
        const uint8_t* a = bild.data() + (size_t)y0 * nbpr;
        size_t len = (size_t)zeilen * nbpr;
        bool leer = true;
        for (size_t k = 0; k < len && leer; k++) leer = a[k] == 0xFF;
        if (leer) continue;
        n = snprintf(kopf, sizeof(kopf), "BITMAP 0,%d,%d,%d,0,", y0, nbpr, zeilen);
        out.insert(out.end(), kopf, kopf + n);
        out.insert(out.end(), a, a + len);
        out.push_back('\r');
        out.push_back('\n');
    }
    n = snprintf(kopf, sizeof(kopf), "PRINT 1,%d\r\n", kopien);
    out.insert(out.end(), kopf, kopf + n);
}

// ---------------------------------------------------------------- Druckbrücke (HTTP)
// Schickt das Raster an die Druckbrücke auf dem Pi (tools/druckbruecke), die per klassischem Bluetooth druckt.
String an_bruecke(const String& basis, const char* pfad, const Druck* d, int dichte) {
    if (WiFi.status() != WL_CONNECTED) return "WLAN getrennt";
    String url = basis;
    while (url.endsWith("/")) url.remove(url.length() - 1);
    url += pfad;
    String body;
    if (d) {
        size_t b64len = 0;
        mbedtls_base64_encode(nullptr, 0, &b64len, d->daten.data(), d->daten.size());
        std::string b64(b64len, '\0');
        mbedtls_base64_encode((unsigned char*)&b64[0], b64len, &b64len, d->daten.data(), d->daten.size());
        b64.resize(b64len);
        body.reserve(b64len + 100);
        JsonDocument t;
        t.set(d->titel);
        String titel;
        serializeJson(t, titel);  // als JSON-String mit Anführungszeichen und Escapes
        body = "{\"breite\":" + String(d->breite) + ",\"hoehe\":" + String(d->hoehe) + ",\"kopien\":" +
               String(d->kopien) + ",\"dichte\":" + String(dichte) + ",\"titel\":" + titel + ",\"daten\":\"";
        body += b64.c_str();
        body += "\"}";
    }
    HTTPClient http;
    WiFiClient netz;
    if (!http.begin(netz, url)) return "Adresse der Druckbrücke ungültig";
    http.setTimeout(10000);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(body);
    String antwort = code > 0 ? http.getString() : String("");
    http.end();
    {
        std::lock_guard<std::mutex> l(g_mx);
        g_druck_bericht = "Druckbrücke " + url + ": " + String(code) + " " + antwort.substring(0, 200);
    }
    if (code <= 0) return "Druckbrücke nicht erreichbar (" + url + ") – läuft der Dienst auf dem Pi?";
    if (code >= 300) {
        JsonDocument j;
        if (!deserializeJson(j, antwort) && j["text"].is<const char*>()) return String("Druckbrücke: ") + j["text"].as<const char*>();
        return "Druckbrücke meldet " + String(code);
    }
    return "";
}

// ---------------------------------------------------------------- Bluetooth
// Bericht des letzten Drucks: welche Dienste der Drucker anbietet, was die Waage benutzt und was er
// geantwortet hat. Steht in /api/diagnose und geht als #-Zeile über USB.

String g_antworten;  // Benachrichtigungen des Druckers als Hex

void antwort_merken(NimBLERemoteCharacteristic* k, uint8_t* d, size_t n, bool) {
    if (g_antworten.length() > 600) return;
    g_antworten += String(k->getUUID().toString().c_str()) + ":";
    for (size_t i = 0; i < n && i < 32; i++) {
        char h[4];
        snprintf(h, sizeof(h), "%02x", d[i]);
        g_antworten += h;
    }
    g_antworten += " ";
}

String eigenschaften(NimBLERemoteCharacteristic* k) {
    String e;
    if (k->canRead()) e += "r";
    if (k->canWrite()) e += "w";
    if (k->canWriteNoResponse()) e += "W";
    if (k->canNotify()) e += "n";
    if (k->canIndicate()) e += "i";
    return e;
}

String senden(const std::vector<uint8_t>& auftrag, const String& adresse, uint8_t typ, const String& name) {
    if (!adresse.length()) return "Kein Drucker eingestellt – unter Einstellungen → Etiketten suchen";
    if (g_ble_gesperrt) return GESPERRT;
    uint32_t t0 = millis();
    auto stand = [&](const String& was) {  // jeder Schritt sofort sichtbar, falls es hängt
        String t = String((millis() - t0) / 1000.0f, 1) + " s: " + was;
        {
            std::lock_guard<std::mutex> l(g_mx);
            g_druck_bericht = t;
        }
        Serial.printf("# Druck: %s\n", t.c_str());
    };
    // Viele Phomemo (z. B. M20 Pro) wechseln ihre Bluetooth-Adresse – deshalb vor jedem Druck kurz suchen
    // und den Drucker am Namen wiedererkennen; die gemerkte Adresse ist nur der Notnagel.
    stand("suche " + (name.length() ? name : adresse));
    NimBLEAddress ziel_adresse(adresse.c_str(), typ);
    bool gefunden = false;
    String gesehen;
    {
        // bis zu 10 s suchen, aufhören sobald er da ist
        struct Suche : public NimBLEAdvertisedDeviceCallbacks {
            String name, adresse, gesehen;
            NimBLEAddress treffer;
            volatile bool fertig = false;
            void onResult(NimBLEAdvertisedDevice* d) override {
                String n = d->getName().c_str();
                n.trim();
                String a = d->getAddress().toString().c_str();
                if (n.length() && gesehen.length() < 300 && gesehen.indexOf(n) < 0) gesehen += (gesehen.length() ? ", " : "") + n;
                bool name_passt = name.length() && n.length() && (n.equalsIgnoreCase(name) || n.startsWith(name.substring(0, 8)));
                if (!fertig && (name_passt || a.equalsIgnoreCase(adresse))) {
                    treffer = d->getAddress();
                    fertig = true;
                    NimBLEDevice::getScan()->stop();
                }
            }
        } suche;
        suche.name = name;
        suche.adresse = adresse;
        NimBLEScan* scan = NimBLEDevice::getScan();
        scan->setAdvertisedDeviceCallbacks(&suche, false);
        scan->setActiveScan(true);
        scan->setInterval(100);
        scan->setWindow(99);
        scan->start(10, false);
        scan->setAdvertisedDeviceCallbacks(nullptr);
        scan->clearResults();
        gefunden = suche.fertig;
        if (gefunden) ziel_adresse = suche.treffer;
        gesehen = suche.gesehen;
    }
    if (gefunden && !String(ziel_adresse.toString().c_str()).equalsIgnoreCase(adresse)) {
        // neue Adresse merken
        std::lock_guard<std::mutex> l(g_mx);
        g_w.adresse = ziel_adresse.toString().c_str();
        g_w.adress_typ = ziel_adresse.getType();
        speichern();
    }
    stand(String(gefunden ? "gefunden, " : "nicht gefunden, versuche trotzdem ") + "verbinde mit " + ziel_adresse.toString().c_str());
    NimBLEClient* c = NimBLEDevice::createClient();
    c->setConnectTimeout(10);
    bool verbunden = c->connect(ziel_adresse);
    if (!verbunden && !gefunden) verbunden = c->connect(NimBLEAddress(adresse.c_str(), typ ? 0 : 1));  // anderer Adresstyp
    if (!verbunden) {
        NimBLEDevice::deleteClient(c);
        stand(String(gefunden ? "gefunden, aber Verbindung abgelehnt" : "Drucker nicht gefunden") + "; gesehen: " +
              (gesehen.length() ? gesehen : String("nichts mit Namen")));
        return gefunden ? "Drucker gefunden, lässt aber keine Verbindung zu – Phomemo-App am Handy schließen"
                        : "Drucker nicht gefunden – ist er noch mit dem Handy oder Mac verbunden? Dort trennen, Drucker aus und ein";
    }
    stand("verbunden, lese Dienste");
    NimBLERemoteCharacteristic* ziel = nullptr;
    String bericht = "MTU " + String(c->getMTU()) + "; Dienste:";
    g_antworten = "";
    for (NimBLERemoteService* s : *c->getServices(true)) {
        bericht += " [" + String(s->getUUID().toString().c_str()) + ":";
        for (NimBLERemoteCharacteristic* k : *s->getCharacteristics(true)) {
            bericht += " " + String(k->getUUID().toString().c_str()) + "(" + eigenschaften(k) + ")";
            if (k->canNotify() && bericht.length() < 900) k->subscribe(true, antwort_merken);  // Antworten mitlesen
        }
        bericht += "]";
    }
    if (NimBLERemoteService* s = c->getService(DIENST)) ziel = s->getCharacteristic(SCHREIBEN);
    if (!ziel) {  // andere Firmware: erste Kennung, die ohne Antwort schreiben kann
        for (NimBLERemoteService* s : *c->getServices(false)) {
            for (NimBLERemoteCharacteristic* k : *s->getCharacteristics(false))
                if (k->canWriteNoResponse()) { ziel = k; break; }
            if (ziel) break;
        }
    }
    bericht += "; benutzt " + (ziel ? String(ziel->getUUID().toString().c_str()) : String("keinen"));
    String fehler;
    size_t gesendet = 0;
    if (!ziel) {
        fehler = "Drucker bietet keinen Schreibkanal an";
    } else {
        static const char FRAGE[] = "SSSGETTYPE\r\n";
        ziel->writeValue((const uint8_t*)FRAGE, sizeof(FRAGE) - 1, false);
        vTaskDelay(pdMS_TO_TICKS(1500));
        stand(bericht + "; sende");
        size_t stueck = constrain((int)c->getMTU() - 3, 20, 180);
        // Mit Bestätigung schreiben, wenn der Drucker das kann: dann wartet die Waage nach jedem Paket,
        // bis er es angenommen hat – sonst läuft sein Empfangspuffer über (M20 Pro trennt dann).
        bool bestaetigt = ziel->canWrite();
        bericht += bestaetigt ? "; mit Bestätigung" : "; ohne Bestätigung";
        for (size_t i = 0; i < auftrag.size(); i += stueck) {
            size_t n = std::min(stueck, auftrag.size() - i);
            if (!ziel->writeValue(auftrag.data() + i, n, bestaetigt)) {
                fehler = "Verbindung zum Drucker abgebrochen";
                break;
            }
            gesendet += n;
            if ((gesendet / stueck) % 10 == 0) stand(bericht + "; gesendet " + String(gesendet) + "/" + String(auftrag.size()));
            // alle 2 KB kurz Luft lassen, damit der Drucker das Bild verarbeiten kann
            vTaskDelay(pdMS_TO_TICKS((gesendet % 2048) < stueck ? 120 : 20));
        }
        if (!fehler.length()) vTaskDelay(pdMS_TO_TICKS(1500));  // Drucker erst fertig empfangen lassen
    }
    stand(bericht + "; gesendet " + String(gesendet) + "/" + String(auftrag.size()) + " Bytes; Antworten: " +
          (g_antworten.length() ? g_antworten : String("keine")) + (fehler.length() ? "; Fehler: " + fehler : String("")));
    c->disconnect();
    NimBLEDevice::deleteClient(c);
    return fehler;
}

void suchen() {
    if (g_ble_gesperrt) {
        std::lock_guard<std::mutex> l(g_mx);
        g_suche_fehler = GESPERRT;
        g_suche_fertig = true;
        g_suche_laeuft = false;
        return;
    }
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
            bool los, trennen, minitest;
            {
                std::lock_guard<std::mutex> l(g_mx);
                los = g_suche_wunsch;
                g_suche_wunsch = false;
                minitest = g_minitest_wunsch;
                g_minitest_wunsch = false;
                trennen = g_trennen_wunsch;
                g_trennen_wunsch = false;
            }
            if (minitest) {
                // Kleinster TSPL-Auftrag ohne Bild: zwei Balken. Zeigt, ob der Drucker über Bluetooth LE druckt.
                static const char JOB[] =
                    "SIZE 40 mm,30 mm\r\nGAP 4.6 mm, 0 mm\r\nDIRECTION 0,0\r\nDENSITY 7\r\nCLS\r\n"
                    "BAR 40,40,392,60\r\nBAR 40,150,200,40\r\nPRINT 1,1\r\n";
                Werte w;
                {
                    std::lock_guard<std::mutex> l(g_mx);
                    w = g_w;
                }
                std::vector<uint8_t> auftrag(JOB, JOB + sizeof(JOB) - 1);
                meldung("Mini-Test: schicke zwei Balken …");
                String f;
                if (w.bruecke.length()) {
                    f = an_bruecke(w.bruecke, "/test", nullptr, 0);
                } else {
                    g_ble_marke = BLE_LAEUFT;
                    f = senden(auftrag, w.adresse, w.adress_typ, w.name);
                    g_ble_marke = 0;
                }
                meldung(f.length() ? f : String("Mini-Test gesendet – kommen zwei schwarze Balken?"), f.length() > 0);
            }
            if (trennen && g_ble_an) {
                // alle Bluetooth-Verbindungen der Waage lösen – auch hängende von früheren Druckversuchen
                int n = 0;
                std::list<NimBLEClient*> liste = *NimBLEDevice::getClientList();
                for (NimBLEClient* c : liste) {
                    if (c->isConnected()) {
                        c->disconnect();
                        n++;
                    }
                    NimBLEDevice::deleteClient(c);
                }
                NimBLEDevice::getScan()->stop();
                if (n) meldung("Offene Bluetooth-Verbindung zum Drucker gelöst");
            }
            if (los) {
                g_ble_marke = BLE_LAEUFT;
                suchen();
                g_ble_marke = 0;
            }
            continue;
        }
        Werte w;
        {
            std::lock_guard<std::mutex> l(g_mx);
            w = g_w;
            g_druckt = true;
        }
        String fehler;
        if (w.bruecke.length()) {
            meldung("Schicke Etikett an die Druckbrücke …");
            fehler = an_bruecke(w.bruecke, "/drucken", d, w.dichte);
        } else {
            std::vector<uint8_t> auftrag;
            if (ist_tspl(w.name)) {
                tspl_auftrag(auftrag, d->breite, d->hoehe, d->daten, w.dichte, d->kopien);
            } else {
                int breite = 0;
                std::vector<uint8_t> raster = auf_kopfbreite(d->breite, d->hoehe, d->daten, ETIKETT_KOPF_PX, breite);
                for (int i = 0; i < d->kopien; i++) befehle(auftrag, breite, d->hoehe, raster, w.dichte);
            }
            meldung("Drucke Etikett …");
            g_ble_marke = BLE_LAEUFT;
            fehler = senden(auftrag, w.adresse, w.adress_typ, w.name);
            g_ble_marke = 0;
        }
        {
            std::lock_guard<std::mutex> l(g_mx);
            g_letzter_druck = fehler.length() ? 0 : 1;
        }
        if (fehler.length()) meldung(fehler, true);
        else if (w.bruecke.length()) meldung("Druckbrücke hat das Etikett angenommen");
        else meldung(d->kopien == 1 ? String("Etikett gedruckt") : String(d->kopien) + " Etiketten gedruckt");
        {
            std::lock_guard<std::mutex> l(g_mx);
            g_druckt = false;
        }
        delete d;
    }
}

}  // namespace

void drucker_frueh(bool erlaubt) {
    // Nach einem Absturz mitten in einer Bluetooth-Aktion Bluetooth sperren
    if (esp_reset_reason() != ESP_RST_POWERON && g_ble_marke == BLE_LAEUFT) g_ble_gesperrt = true;
    if (!erlaubt) g_ble_gesperrt = true;
    g_ble_marke = 0;
    // Bluetooth gleich beim Start einschalten, solange der interne Speicher noch frei ist und bevor
    // große Speicherblöcke ins PSRAM umgeleitet werden – der Bluetooth-Teil braucht internen Speicher.
    if (!g_ble_gesperrt) {
        g_ble_marke = BLE_LAEUFT;
        ble_an();
        g_ble_marke = 0;
    }
}

bool drucker_gesperrt() { return g_ble_gesperrt; }
String drucker_bruecke() {
    std::lock_guard<std::mutex> l(g_mx);
    return g_w.bruecke;
}
String drucker_bericht() {
    std::lock_guard<std::mutex> l(g_mx);
    return g_druck_bericht;
}

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
    bool verbunden = false;
    if (g_ble_an)
        for (NimBLEClient* c : *NimBLEDevice::getClientList()) verbunden |= c->isConnected();
    d["verbunden"] = verbunden;
    d["bruecke"] = g_w.bruecke;
}

void drucker_setzen(JsonObjectConst o) {
    std::lock_guard<std::mutex> l(g_mx);
    String format = o["format"] | "";
    if (format == "classic" || format == "expanded" || format == "compact" || format == "vertikal") g_w.format = format;
    if (o["dichte"].is<int>()) g_w.dichte = constrain(o["dichte"].as<int>(), 1, 15);
    if (o["bruecke"].is<const char*>()) {
        String b = o["bruecke"].as<const char*>();
        b.trim();
        if (b.length() && !b.startsWith("http://")) b = "http://" + b;
        g_w.bruecke = b.substring(0, 80);
    }
    if (o["adresse"].is<const char*>()) {
        g_w.adresse = String(o["adresse"].as<const char*>()).substring(0, 40);
        g_w.name = String(o["name"] | "").substring(0, 40);
        g_w.adress_typ = 0;
        for (const Geraet& g : g_geraete)
            if (g.adresse == g_w.adresse) g_w.adress_typ = g.typ;
    }
    speichern();
}

// Nur an Modelle senden, deren Druckbefehle bekannt sind (203 dpi, ESC/POS wie phomemo.py).
// Der M20 Pro (300 dpi, RFID) spricht anders – falsche Befehle haben ihn hängen lassen.
bool modell_bekannt(String n) {
    n.toUpperCase();
    n.replace(" ", "");
    if (ist_tspl(n)) return true;  // M20 Pro: TSPL
    for (const char* m : {"M110", "M120", "M200", "M220", "M221", "M250", "M260", "M02", "T02", "M03"})
        if (n.startsWith(m)) return true;
    return false;
}

const char* drucker_drucken(JsonObjectConst o) {
    {
        std::lock_guard<std::mutex> l(g_mx);
        if (!g_w.bruecke.length() && ist_tspl(g_w.name)) {
            g_druck_bericht = "nicht gesendet: M20 Pro druckt nicht über Bluetooth LE – Druckbrücke eintragen";
            return "Der M20 Pro braucht die Druckbrücke auf dem Pi – unter Etiketten → Druckbrücke eintragen";
        }
        if (!g_w.bruecke.length() && g_w.name.length() && !modell_bekannt(g_w.name)) {
            g_druck_bericht = "nicht gesendet: Druckbefehle für " + g_w.name + " noch unbekannt";
            return "Für diesen Drucker kennt die Waage die Druckbefehle noch nicht – zum Schutz wird nichts gesendet";
        }
    }
    int breite = o["breite"] | 0, hoehe = o["hoehe"] | 0;
    const char* b64 = o["daten"] | "";
    int kopien = constrain((int)(o["kopien"] | 1), 1, 10);
    if (breite <= 0 || breite % 8 || hoehe <= 0 || hoehe > 4000) return "Etikett unvollständig";
    size_t soll = (size_t)breite / 8 * hoehe, laenge = 0;
    auto* d = new Druck{breite, hoehe, kopien, std::vector<uint8_t>(soll + 4), String(o["titel"] | "Etikett").substring(0, 80)};
    if (mbedtls_base64_decode(d->daten.data(), d->daten.size(), &laenge, (const unsigned char*)b64, strlen(b64)) ||
        laenge != soll) {
        delete d;
        return "Etikett unvollständig";
    }
    d->daten.resize(soll);
    if (xQueueSend(g_druecke, &d, 0) != pdTRUE) {
        delete d;
        return "Drucker ist noch beschäftigt – der letzte Auftrag hängt, Waage neu starten";
    }
    std::lock_guard<std::mutex> l(g_mx);
    g_druck_bericht = "Auftrag angenommen: " + String(breite) + "×" + String(hoehe) + " Punkte, wartet auf den Drucker";
    return nullptr;
}

int drucker_zustand() {
    std::lock_guard<std::mutex> l(g_mx);
    if (g_w.bruecke.length()) return g_letzter_druck == 0 ? 2 : 1;
    if (g_ble_gesperrt && g_w.adresse.length()) return 2;
    if (!g_w.adresse.length()) return 0;
    return g_letzter_druck == 0 ? 2 : 1;
}

void drucker_minitest() {
    std::lock_guard<std::mutex> l(g_mx);
    g_minitest_wunsch = true;
}

void drucker_trennen() {
    std::lock_guard<std::mutex> l(g_mx);
    g_letzter_druck = -1;
    g_trennen_wunsch = true;
    // Drucker auch vergessen – danach frisch über „Drucker suchen“ einrichten
    g_w.adresse = "";
    g_w.name = "";
    g_w.adress_typ = 0;
    speichern();
    g_druck_bericht = "getrennt und entfernt";
}

bool drucker_suche(JsonDocument& d) {
    std::lock_guard<std::mutex> l(g_mx);
    if (g_suche_fertig) {
        g_suche_fertig = false;
        d["ok"] = !g_suche_fehler.length();
        if (g_suche_fehler.length()) d["text"] = g_suche_fehler;
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
        g_suche_fehler = "";
    }
    d["laeuft"] = true;
    return false;
}
