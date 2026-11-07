#include "nfc_schreiben.h"

#include <ArduinoJson.h>

#include <mutex>
#include <string>

#include "ha_client.h"

namespace nfc_schreiben {
namespace {
std::mutex g_mx;
String g_ntag[2];            // aufliegender NTAG je Seite (leer = keiner)
String g_auftrag_uid, g_auftrag_url;
int g_auftrag_seite = -1;
uint32_t g_auftrag_seit = 0;
bool g_ergebnis_offen = false;
bool g_ergebnis_ok = false;
String g_ergebnis_uid, g_ergebnis_fehler;
uint32_t g_gefragt = 0, g_pause_bis = 0;
String g_erledigt_uid;       // gerade beschrieben: nicht gleich noch einmal nachfragen
uint32_t g_erledigt_um = 0;

constexpr uint32_t ABFRAGE_MS = 2000;
constexpr uint32_t AUFTRAG_GILT_MS = 15000;
constexpr uint32_t OHNE_SERVER_MS = 300000;
}  // namespace

void tag_stand(int seite, const String& uid, bool ntag) {
    if (seite < 0 || seite > 1) return;
    std::lock_guard<std::mutex> l(g_mx);
    g_ntag[seite] = (uid.length() && ntag) ? uid : String();
}

bool auftrag_fuer(int seite, const String& uid, String& url) {
    std::lock_guard<std::mutex> l(g_mx);
    if (g_auftrag_seite != seite || !uid.equalsIgnoreCase(g_auftrag_uid)) return false;
    if (millis() - g_auftrag_seit > AUFTRAG_GILT_MS) {
        g_auftrag_seite = -1;
        return false;
    }
    url = g_auftrag_url;
    g_auftrag_seite = -1;  // genau einmal
    return true;
}

void ergebnis(const String& uid, bool ok, const String& fehler) {
    std::lock_guard<std::mutex> l(g_mx);
    g_ergebnis_offen = true;
    g_ergebnis_ok = ok;
    g_ergebnis_uid = uid;
    g_ergebnis_fehler = fehler;
    g_erledigt_uid = uid;
    g_erledigt_um = millis();
    Serial.printf("[nfc] Aufkleber %s %s %s\n", uid.c_str(), ok ? "beschrieben" : "NICHT beschrieben:", fehler.c_str());
}

bool etwas_zu_tun() {
    std::lock_guard<std::mutex> l(g_mx);
    return g_ergebnis_offen || g_ntag[0].length() || g_ntag[1].length();
}

void abfragen(HaClient& ha, const String& waage) {
    String pfad = "/api/nfc_auftrag/" + waage;
    pfad.replace('-', '_');
    std::string body, antwort, fehler;

    // 1. Ergebnis melden
    {
        JsonDocument j;
        {
            std::lock_guard<std::mutex> l(g_mx);
            if (g_ergebnis_offen) {
                j["uid"] = g_ergebnis_uid;
                j["ok"] = g_ergebnis_ok;
                j["fehler"] = g_ergebnis_fehler;
            }
        }
        if (j.size()) {
            serializeJson(j, body);
            if (ha.anfrage((pfad + "/ergebnis").c_str(), body, antwort, fehler)) {
                std::lock_guard<std::mutex> l(g_mx);
                g_ergebnis_offen = false;
            }
            return;
        }
    }

    // 2. Nach einem Auftrag fragen – nur wenn ein NTAG aufliegt
    String l_uid, r_uid;
    uint32_t jetzt = millis();
    {
        std::lock_guard<std::mutex> l(g_mx);
        l_uid = g_ntag[0];
        r_uid = g_ntag[1];
        bool frisch = jetzt - g_erledigt_um < 10000;
        if (frisch && l_uid.equalsIgnoreCase(g_erledigt_uid)) l_uid = "";
        if (frisch && r_uid.equalsIgnoreCase(g_erledigt_uid)) r_uid = "";
    }
    if (l_uid.isEmpty() && r_uid.isEmpty()) return;
    if (jetzt - g_gefragt < ABFRAGE_MS || (int32_t)(g_pause_bis - jetzt) > 0) return;
    g_gefragt = jetzt;

    JsonDocument j;
    j["l"] = l_uid;
    j["r"] = r_uid;
    serializeJson(j, body);
    if (!ha.anfrage(pfad.c_str(), body, antwort, fehler)) {
        // Home Assistant statt SchichtLohn eingetragen (kennt den Aufruf nicht) oder Server weg: selten fragen
        if (fehler.find("404") != std::string::npos || fehler.find("400") != std::string::npos) g_pause_bis = jetzt + OHNE_SERVER_MS;
        return;
    }
    JsonDocument a;
    if (deserializeJson(a, antwort)) return;
    const char* seite = a["seite"] | "";
    String uid = a["uid"] | "";
    String url = a["url"] | "";
    if (!uid.length() || !url.length() || (strcmp(seite, "l") && strcmp(seite, "r"))) return;
    std::lock_guard<std::mutex> l(g_mx);
    g_auftrag_seite = seite[0] == 'r' ? 1 : 0;
    g_auftrag_uid = uid;
    g_auftrag_url = url;
    g_auftrag_seit = millis();
}
}  // namespace nfc_schreiben
