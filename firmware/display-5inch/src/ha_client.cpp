#include "ha_client.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

std::recursive_mutex g_http_riegel;

void HaClient::begin(const char* url, const char* token, const char* todo) {
    url_ = url;
    token_ = token;
    todo_ = todo;
    while (!url_.empty() && url_.back() == '/') url_.pop_back();
}

bool HaClient::post(const std::string& pfad, const std::string& body, std::string& antwort, std::string& fehler) {
    std::lock_guard<std::recursive_mutex> riegel(g_http_riegel);
    std::string ziel = url_ + pfad;
    int code = -1;
    // bis zu 3 Versuche, solange schon der Verbindungsaufbau scheitert (WLAN-Aussetzer, HA kurz beschäftigt).
    // Verbindung und Antwort bleiben innerhalb eines Durchlaufs – der WiFiClient darf nicht vor dem Lesen enden.
    for (int versuch = 0; versuch < 3; versuch++) {
        if (versuch) delay(800 * versuch);
        if (WiFi.status() != WL_CONNECTED) {
            fehler = "WLAN getrennt";
            continue;
        }
        HTTPClient http;
        WiFiClient plain;
        WiFiClientSecure tls;
        bool ok;
        if (ziel.rfind("https://", 0) == 0) {
            tls.setInsecure();  // lokales HA mit eigenem Zertifikat
            ok = http.begin(tls, ziel.c_str());
        } else {
            ok = http.begin(plain, ziel.c_str());
        }
        if (!ok) {
            fehler = "Adresse ungültig";
            return false;
        }
        http.setConnectTimeout(4000);
        http.setTimeout(10000);
        http.addHeader("Authorization", ("Bearer " + token_).c_str());
        http.addHeader("Content-Type", "application/json");
        code = http.POST((uint8_t*)body.data(), body.size());
        if (code <= 0) {
            fehler = std::string("Home Assistant nicht erreichbar (") + http.errorToString(code).c_str() + ")";
            http.end();
            if (!http_neu_versuchen(code)) return false;
            continue;
        }
        String s = http.getString();
        http.end();
        if (code == 401) {
            fehler = "Token abgelehnt (401)";
            return false;
        }
        if (code != 200 && code != 201) {
            fehler = "Home Assistant meldet " + std::to_string(code);
            return false;
        }
        antwort.assign(s.c_str(), s.length());
        return true;
    }
    return false;
}

bool HaClient::lager_laden(Lager& lager, std::string& fehler) {
    JsonDocument b;
    b["entity_id"] = todo_;
    std::string body, antwort;
    serializeJson(b, body);
    if (!post("/api/services/todo/get_items?return_response", body, antwort, fehler)) return false;
    if (!lager.lade_service_antwort(antwort.data(), antwort.size(), todo_.c_str())) {
        fehler = "Antwort nicht lesbar – gibt es " + todo_ + "?";
        return false;
    }
    return true;
}

bool HaClient::beschreibung_setzen(const std::string& item_uid, const std::string& beschreibung, std::string& fehler) {
    JsonDocument b;
    b["entity_id"] = todo_;
    b["item"] = item_uid;
    b["description"] = beschreibung;
    std::string body, antwort;
    serializeJson(b, body);
    return post("/api/services/todo/update_item", body, antwort, fehler);
}

bool HaClient::zustand_setzen(const std::string& entity, const std::string& body, std::string& fehler) {
    std::string antwort;
    return post("/api/states/" + entity, body, antwort, fehler);
}

bool HaClient::ereignis_senden(const std::string& name, const std::string& body, std::string& fehler) {
    std::string antwort;
    return post("/api/events/" + name, body, antwort, fehler);
}

bool HaClient::liste_laden(const std::string& entity, std::string& antwort, std::string& fehler) {
    JsonDocument b;
    b["entity_id"] = entity;
    std::string body;
    serializeJson(b, body);
    return post("/api/services/todo/get_items?return_response", body, antwort, fehler);
}

bool HaClient::vorlage(const std::string& tpl, std::string& antwort, std::string& fehler) {
    JsonDocument b;
    b["template"] = tpl;
    std::string body;
    serializeJson(b, body);
    return post("/api/template", body, antwort, fehler);
}
