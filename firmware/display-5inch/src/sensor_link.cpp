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

// Kurzer Verbindungsversuch, höchstens alle 3 s – blockiert die Oberfläche nur kurz
bool SensorLink::wlan_verbinden() {
    uint32_t jetzt = millis();
    if (WiFi.status() != WL_CONNECTED || (letzter_versuch_ && jetzt - letzter_versuch_ < 3000)) return false;
    letzter_versuch_ = jetzt;
    IPAddress ip;
    const std::string endung = ".local";
    if (host_.size() > endung.size() && host_.compare(host_.size() - endung.size(), endung.size(), endung) == 0) {
        static bool mdns_an = false;
        if (!mdns_an) mdns_an = MDNS.begin(GERAETENAME);
        ip = MDNS.queryHost(host_.substr(0, host_.size() - endung.size()).c_str(), 800);
        if (ip == IPAddress()) return false;
    } else if (!ip.fromString(host_.c_str())) {
        if (!WiFi.hostByName(host_.c_str(), ip)) return false;
    }
    if (!client_.connect(ip, tcp_port_, 800)) return false;
    client_.setNoDelay(true);
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

void SensorLink::tara() { befehl_ = "{\"c\":\"tara\"}"; }

void SensorLink::kalibrieren(int gramm) {
    befehl_ = "{\"c\":\"kal\",\"g\":" + std::to_string(gramm) + "}";
}

void SensorLink::loop() {
    if (wlan_ && !client_.connected()) {
        io_ = nullptr;
        if (client_) client_.stop();
        d_.verbunden = false;
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
