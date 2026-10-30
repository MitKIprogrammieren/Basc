// Verbindung zur Sensorplatine (ESP32-S3-Zero/Super Mini/N16R8).
// Über RS485 oder über WLAN (TCP-Port 4711, wie beim BTT Pad 7).
// Das Display fragt alle 100 ms ab, die Sensorplatine antwortet nur auf Anfrage.
// So senden auf RS485 nie beide gleichzeitig. Protokoll: docs/PROTOKOLL.md
//
// Läuft in einer eigenen Task auf Kern 0: Hakt das WLAN, wartet nur diese Task –
// die Oberfläche (loop() auf Kern 1) holt sich nur eine Kopie der Messwerte.
#pragma once

#include <Arduino.h>
#include <WiFiClient.h>

#include <deque>
#include <mutex>
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
    // Startet die Task (nach begin/begin_wlan aufrufen)
    void starten();
    SensorDaten daten();  // Kopie, aus jeder Task aufrufbar
    bool ueber_wlan() const { return wlan_; }
    void tara();
    void kalibrieren(int gramm);
    // Rückmeldung zu Tara/Kalibrieren abholen (in loop()): true, wenn eine da war
    bool meldung_holen(bool& ok, std::string& text);

private:
    static void task(void* selbst);
    void schritt();
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
    std::mutex mx_;
    SensorDaten d_;  // geschützt durch mx_
    std::string befehl_;  // wartet auf die nächste Abfrage-Lücke, geschützt durch mx_
    std::deque<std::pair<bool, std::string>> meldungen_;  // geschützt durch mx_
    char puffer_[256];
    size_t laenge_ = 0;
    uint32_t letzte_abfrage_ = 0, letzte_antwort_ = 0;
};
