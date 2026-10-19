#include "ha_client.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

void HaClient::begin(const char* url, const char* token, const char* todo) {
    url_ = url;
    token_ = token;
    todo_ = todo;
    while (!url_.empty() && url_.back() == '/') url_.pop_back();
}

bool HaClient::post(const std::string& pfad, const std::string& body, std::string& antwort, std::string& fehler) {
    if (WiFi.status() != WL_CONNECTED) {
        fehler = "WLAN getrennt";
        return false;
    }
    std::string ziel = url_ + pfad;
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
    http.setTimeout(10000);
    http.addHeader("Authorization", ("Bearer " + token_).c_str());
    http.addHeader("Content-Type", "application/json");
    int code = http.POST((uint8_t*)body.data(), body.size());
    if (code <= 0) {
        fehler = std::string("Home Assistant nicht erreichbar (") + http.errorToString(code).c_str() + ")";
        http.end();
        return false;
    }
    String s = http.getString();
    http.end();
    if (code == 401) {
        fehler = "Token abgelehnt (401)";
        return false;
    }
    if (code != 200) {
        fehler = "Home Assistant meldet " + std::to_string(code);
        return false;
    }
    antwort.assign(s.c_str(), s.length());
    return true;
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
