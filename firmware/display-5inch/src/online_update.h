#pragma once
// Update über das Netz: Die Waage holt die neue Firmware über den SchichtLohn-Server
// (/api/waagen_update/<zweig>/<datei>), der sie von GitHub durchreicht. So braucht die Waage kein HTTPS –
// dafür reicht der Speicher auf dem SC01 Plus nicht. Wird von Display und Sensorplatine benutzt (nur Kopfdatei).
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

namespace online_update {

// Fortschritt für die Anzeige: Text wie „Lade Firmware … 45 %“; fertig/fehler für den Abschluss
struct Stand {
    char text[96] = "";
    volatile bool laeuft = false;
    volatile bool fehler = false;
};

inline String url_fuer(const String& server, const String& token, const char* zweig, const String& datei) {
    String u = server;
    while (u.endsWith("/")) u.remove(u.length() - 1);
    u += "/api/waagen_update/";
    u += zweig;
    u += "/";
    u += datei;
    if (token.length() && token != "schichtlohn") u += "?token=" + token;
    return u;
}

// „1.11.10“ > „1.11.9“
inline bool neuer(const String& a, const String& b) {
    int ia = 0, ib = 0;
    while (ia < (int)a.length() || ib < (int)b.length()) {
        long x = 0, y = 0;
        while (ia < (int)a.length() && isDigit(a[ia])) x = x * 10 + (a[ia++] - '0');
        while (ib < (int)b.length() && isDigit(b[ib])) y = y * 10 + (b[ib++] - '0');
        if (x != y) return x > y;
        if (ia < (int)a.length()) ia++;
        if (ib < (int)b.length()) ib++;
    }
    return false;
}

// version.json lesen: {"fw":"1.11.5", "littlefs":"0x390000"}; leer bei Fehler (Grund in fehler)
inline String verfuegbar(const String& server, const String& token, const char* zweig, String& fehler,
                         uint32_t* fs_offset = nullptr) {
    if (WiFi.status() != WL_CONNECTED) { fehler = "kein WLAN"; return ""; }
    if (!server.length()) { fehler = "keine SchichtLohn-Adresse eingetragen"; return ""; }
    HTTPClient http;
    http.setTimeout(15000);
    if (!http.begin(url_fuer(server, token, zweig, "version.json"))) { fehler = "Adresse ungültig"; return ""; }
    int code = http.GET();
    String body = code == 200 ? http.getString() : "";
    http.end();
    if (code == 404) { fehler = "SchichtLohn kennt kein Waagen-Update – SchichtLohn aktualisieren"; return ""; }
    if (code != 200) { fehler = "SchichtLohn antwortet nicht (" + String(code) + ")"; return ""; }
    JsonDocument d;
    if (deserializeJson(d, body)) { fehler = "Antwort unlesbar"; return ""; }
    if (fs_offset) {
        String o = d["littlefs"] | "";
        *fs_offset = o.length() ? strtoul(o.c_str(), nullptr, 0) : 0;
    }
    String v = d["fw"] | "";
    if (!v.length()) fehler = "keine Version gefunden";
    return v;
}

// Eine Datei laden und mit Update schreiben (U_FLASH = Firmware, U_SPIFFS = Oberfläche/LittleFS)
inline bool laden(const String& url, int ziel, Stand& st, const char* was, String& fehler) {
    HTTPClient http;
    http.setTimeout(30000);
    if (!http.begin(url)) { fehler = "Adresse ungültig"; return false; }
    int code = http.GET();
    if (code != 200) { fehler = String(was) + ": SchichtLohn/GitHub antwortet " + code; http.end(); return false; }
    int laenge = http.getSize();
    if (laenge <= 0) { fehler = String(was) + ": Größe unbekannt"; http.end(); return false; }
    if (!Update.begin(laenge, ziel)) { fehler = String(was) + ": " + Update.errorString(); http.end(); return false; }
    WiFiClient* s = http.getStreamPtr();
    static uint8_t puffer[2048];
    int gelesen = 0, zuletzt = -1;
    uint32_t still = millis();
    while (gelesen < laenge) {
        int n = s->available();
        if (n <= 0) {
            if (!http.connected() || millis() - still > 20000) break;
            delay(2);
            continue;
        }
        n = s->readBytes(puffer, min(n, (int)sizeof puffer));
        if (Update.write(puffer, n) != (size_t)n) break;
        gelesen += n;
        still = millis();
        int p = (int)((int64_t)gelesen * 100 / laenge);
        if (p != zuletzt) {
            zuletzt = p;
            snprintf(st.text, sizeof st.text, "Lade %s … %d %%", was, p);
        }
    }
    http.end();
    if (gelesen != laenge) {
        Update.abort();
        fehler = String(was) + ": Verbindung abgebrochen";
        return false;
    }
    if (!Update.end(true)) { fehler = String(was) + ": " + Update.errorString(); return false; }
    return true;
}

// Alles einspielen. ordner: "" oder "n16r8/" bzw. "sc01plus_direkt/". mit_fs: auch die Oberfläche (LittleFS).
// Bei Erfolg startet die Waage neu (kehrt nicht zurück); bei Fehler steht der Grund in st.text.
inline void einspielen(const String& server, const String& token, const char* zweig, const String& ordner,
                       bool mit_fs, Stand& st, void (*vor_fs)() = nullptr) {
    st.laeuft = true;
    st.fehler = false;
    String fehler;
    snprintf(st.text, sizeof st.text, "Suche Update …");
    uint32_t fs_offset = 0;
    String v = verfuegbar(server, token, zweig, fehler, &fs_offset);
    if (!v.length()) goto fehlschlag;
    if (mit_fs) {
        const esp_partition_t* fs = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
        if (!fs || (fs_offset && fs->address != fs_offset)) {
            fehler = "Version " + v + " teilt den Speicher anders auf – einmal per USB flashen";
            goto fehlschlag;
        }
        if (vor_fs) vor_fs();
        if (!laden(url_fuer(server, token, zweig, ordner + "littlefs.bin"), U_SPIFFS, st, "Oberfläche", fehler)) goto fehlschlag;
    }
    if (!laden(url_fuer(server, token, zweig, ordner + "firmware.bin"), U_FLASH, st, "Firmware", fehler)) goto fehlschlag;
    snprintf(st.text, sizeof st.text, "Version %s eingespielt – starte neu …", v.c_str());
    delay(1500);
    ESP.restart();
    return;
fehlschlag:
    snprintf(st.text, sizeof st.text, "%s", fehler.c_str());
    st.fehler = true;
    st.laeuft = false;
}

}  // namespace online_update
