#include "sensor_link.h"

#include <ArduinoJson.h>

#include "config.h"

void SensorLink::begin(HardwareSerial& port, int rx, int tx, uint32_t baud) {
    port_ = &port;
    port_->setRxBufferSize(1024);
    port_->begin(baud, SERIAL_8N1, rx, tx);
}

void SensorLink::senden(const char* zeile) {
    port_->print(zeile);
    port_->print('\n');
    port_->flush();
    letzte_abfrage_ = millis();
}

void SensorLink::tara() { befehl_ = "{\"c\":\"tara\"}"; }

void SensorLink::kalibrieren(int gramm) {
    befehl_ = "{\"c\":\"kal\",\"g\":" + std::to_string(gramm) + "}";
}

void SensorLink::loop() {
    if (!port_) return;
    while (port_->available()) {
        char c = (char)port_->read();
        if (c == '\n' || c == '\r') {
            if (laenge_) {
                puffer_[laenge_] = 0;
                zeile_verarbeiten(puffer_);
            }
            laenge_ = 0;
        } else if (laenge_ < sizeof(puffer_) - 1) {
            puffer_[laenge_++] = c;
        } else {
            laenge_ = 0;  // Müll verwerfen
        }
    }
    uint32_t jetzt = millis();
    d_.verbunden = letzte_antwort_ && (jetzt - letzte_antwort_ < 1000);
    if (jetzt - letzte_abfrage_ >= SENSOR_ABFRAGE_MS) {
        if (!befehl_.empty()) {
            senden(befehl_.c_str());
            befehl_.clear();
        } else {
            senden("{\"c\":\"p\"}");
        }
    }
}

void SensorLink::zeile_verarbeiten(const char* z) {
    if (z[0] != '{') return;  // z. B. Startmeldungen des ROM-Bootloaders
    JsonDocument doc;
    if (deserializeJson(doc, z)) return;
    letzte_antwort_ = millis();
    const char* t = doc["t"] | "";
    if (!strcmp(t, "s")) {
        d_.gramm = doc["g"] | 0.0f;
        d_.stabil = (doc["st"] | 0) != 0;
        d_.kalibriert = (doc["cal"] | 0) != 0;
        d_.tag_l = doc["L"] | "";
        d_.tag_r = doc["R"] | "";
    } else if (!strcmp(t, "ok") || !strcmp(t, "err")) {
        bool ok = !strcmp(t, "ok");
        std::string c = doc["c"] | "";
        std::string text;
        if (!ok) text = std::string("Sensor: ") + (doc["msg"] | "Fehler");
        else if (c == "tara") text = "Tara gesetzt";
        else if (c == "kal") text = "Kalibriert";
        else text = "OK";
        if (rueckmeldung) rueckmeldung(ok, text);
    }
}
