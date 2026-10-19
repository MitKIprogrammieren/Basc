// Verbindung zur Sensorplatine (ESP32-S3-Zero) über RS485.
// Das Display fragt alle 100 ms ab, die Sensorplatine antwortet nur auf Anfrage.
// So senden nie beide gleichzeitig auf der Zweidrahtleitung. Protokoll: docs/PROTOKOLL.md
#pragma once

#include <Arduino.h>

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
    void begin(HardwareSerial& port, int rx, int tx, uint32_t baud);
    void loop();  // oft aufrufen
    const SensorDaten& daten() const { return d_; }
    void tara();
    void kalibrieren(int gramm);
    // Rückmeldung zu Tara/Kalibrieren: (ok, Text)
    std::function<void(bool, const std::string&)> rueckmeldung;

private:
    void senden(const char* zeile);
    void zeile_verarbeiten(const char* z);
    HardwareSerial* port_ = nullptr;
    SensorDaten d_;
    char puffer_[256];
    size_t laenge_ = 0;
    uint32_t letzte_abfrage_ = 0, letzte_antwort_ = 0;
    std::string befehl_;  // wartet auf die nächste Abfrage-Lücke
};
