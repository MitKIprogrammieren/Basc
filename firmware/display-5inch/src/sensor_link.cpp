#include "sensor_link.h"

#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include "config.h"

void SensorLink::begin(HardwareSerial& port, int rx, int tx, uint32_t baud, int de) {
    seriell_ = &port;
    io_ = &port;
    port.setRxBufferSize(1024);
    port.begin(baud, SERIAL_8N1, rx, tx);
    if (de >= 0) {
        // Halbduplex: der UART schaltet den Sender über RTS selbst ein und nach dem letzten Byte wieder aus
        port.setPins(rx, tx, -1, de);
        port.setMode(UART_MODE_RS485_HALF_DUPLEX);
    }
}

void SensorLink::begin_wlan(const char* host, uint16_t tcp_port) {
    wlan_ = true;
    host_ = host;
    tcp_port_ = tcp_port;
}

void SensorLink::starten() { xTaskCreatePinnedToCore(task, "sensor", 6144, this, 2, nullptr, 0); }

void SensorLink::task(void* selbst) {
    SensorLink* s = (SensorLink*)selbst;
    for (;;) {
        s->schritt();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

SensorDaten SensorLink::daten() {
    std::lock_guard<std::mutex> l(mx_);
    return d_;
}

void SensorLink::tara() {
    std::lock_guard<std::mutex> l(mx_);
    befehl_ = "{\"c\":\"tara\"}";
}

void SensorLink::kalibrieren(int gramm) {
    std::lock_guard<std::mutex> l(mx_);
    befehl_ = "{\"c\":\"kal\",\"g\":" + std::to_string(gramm) + "}";
}

bool SensorLink::meldung_holen(bool& ok, std::string& text) {
    std::lock_guard<std::mutex> l(mx_);
    if (meldungen_.empty()) return false;
    ok = meldungen_.front().first;
    text = meldungen_.front().second;
    meldungen_.pop_front();
    return true;
}

// Verbindungsversuch höchstens alle 3 s (läuft in der Sensor-Task, nicht in der Oberfläche)
bool SensorLink::wlan_verbinden() {
    uint32_t jetzt = millis();
    if (WiFi.status() != WL_CONNECTED || (letzter_versuch_ && jetzt - letzter_versuch_ < 3000)) return false;
    letzter_versuch_ = jetzt;
    IPAddress ip;
    const std::string endung = ".local";
    if (host_.size() > endung.size() && host_.compare(host_.size() - endung.size(), endung.size(), endung) == 0) {
        static bool mdns_an = false;
        if (!mdns_an) mdns_an = MDNS.begin(GERAETENAME);
        ip = MDNS.queryHost(host_.substr(0, host_.size() - endung.size()).c_str(), 1500);
        if (ip == IPAddress()) return false;
    } else if (!ip.fromString(host_.c_str())) {
        if (!WiFi.hostByName(host_.c_str(), ip)) return false;
    }
    if (!client_.connect(ip, tcp_port_, 2000)) return false;
    client_.setNoDelay(true);
    client_.setTimeout(1);  // Senden/Empfangen höchstens 1 s blockieren
    io_ = &client_;
    laenge_ = 0;
    Serial.printf("[sensor] per WLAN verbunden mit %s\n", ip.toString().c_str());
    return true;
}

void SensorLink::senden(const char* zeile) {
    // in einem Stück schreiben, damit über WLAN ein einziges Paket rausgeht
    std::string z = std::string(zeile) + "\n";
    io_->write((const uint8_t*)z.data(), z.size());
    if (seriell_) seriell_->flush();
    letzte_abfrage_ = millis();
}

void SensorLink::schritt() {
    if (wlan_ && !client_.connected()) {
        io_ = nullptr;
        if (client_) client_.stop();
        {
            std::lock_guard<std::mutex> l(mx_);
            d_.verbunden = false;
        }
        if (!wlan_verbinden()) return;
    }
    if (!io_) return;
    while (io_->available()) {
        char c = (char)io_->read();
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
    std::string befehl;
    {
        std::lock_guard<std::mutex> l(mx_);
        d_.verbunden = letzte_antwort_ && (jetzt - letzte_antwort_ < 1000);
        if (jetzt - letzte_abfrage_ >= SENSOR_ABFRAGE_MS) befehl.swap(befehl_);
    }
    if (jetzt - letzte_abfrage_ >= SENSOR_ABFRAGE_MS) senden(befehl.empty() ? "{\"c\":\"p\"}" : befehl.c_str());
}

void SensorLink::zeile_verarbeiten(const char* z) {
    if (z[0] != '{') return;  // z. B. Startmeldungen des ROM-Bootloaders
    JsonDocument doc;
    if (deserializeJson(doc, z)) return;
    letzte_antwort_ = millis();
    const char* t = doc["t"] | "";
    std::lock_guard<std::mutex> l(mx_);
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
        if (meldungen_.size() < 8) meldungen_.push_back({ok, text});
    }
}
