// Verbindung zur Sensorplatine (ESP32-S3-Zero/Super Mini/N16R8).
// Über RS485 oder über WLAN (TCP-Port 4711, wie beim BTT Pad 7).
// Das Display fragt alle 100 ms ab, die Sensorplatine antwortet nur auf Anfrage.
// So senden auf RS485 nie beide gleichzeitig. Protokoll: docs/PROTOKOLL.md
#pragma once

#include <Arduino.h>
#include <WiFiClient.h>

#include <functional>
#include <string>

struct SensorDaten {
    float gramm = 0;
    bool stabil = false;
    bool kalibriert = false;
    std::string tag_l, tag_r;
    bool verbunden = false;  // letzte Antwort jünger als 1 s
};

class SensorLink {
public:
    // RS485: de = Pin der Senderfreigabe, -1 bei Transceivern mit automatischer Umschaltung
    void begin(HardwareSerial& port, int rx, int tx, uint32_t baud, int de = -1);
    // WLAN: IP oder Name der Sensorplatine (z. B. "192.168.1.96" oder "filamentwaage.local")
    void begin_wlan(const char* host, uint16_t tcp_port);
    void loop();  // oft aufrufen
    const SensorDaten& daten() const { return d_; }
    bool ueber_wlan() const { return wlan_; }
    void tara();
    void kalibrieren(int gramm);
    // Rückmeldung zu Tara/Kalibrieren: (ok, Text)
    std::function<void(bool, const std::string&)> rueckmeldung;

private:
    void senden(const char* zeile);
    void zeile_verarbeiten(const char* z);
    bool wlan_verbinden();
    Stream* io_ = nullptr;
    HardwareSerial* seriell_ = nullptr;
    bool wlan_ = false;
    WiFiClient client_;
    std::string host_;
    uint16_t tcp_port_ = 0;
    uint32_t letzter_versuch_ = 0;
    SensorDaten d_;
    char puffer_[256];
    size_t laenge_ = 0;
    uint32_t letzte_abfrage_ = 0, letzte_antwort_ = 0;
    std::string befehl_;  // wartet auf die nächste Abfrage-Lücke
};
