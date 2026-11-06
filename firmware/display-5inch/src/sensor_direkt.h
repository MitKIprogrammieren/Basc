// Messung direkt am Display, ohne Sensorplatine: NAU7802 (I²C) und zwei PN532 (HSU) hängen am SC01 Plus.
// Gleiche Schnittstelle wie SensorLink, damit main.cpp und die Oberfläche nichts davon merken.
// Messlogik wie auf der Sensorplatine (firmware/sensor-s3zero/src/main.cpp): Median, Glättung, Ruhe-Erkennung,
// Tara/Kalibrieren über 15 Proben, Nullpunkt-Nachführung, Neustart hängender NFC-Leser.
// Nur mit -DSENSOR_DIREKT=1 gebaut (env:sc01plus_direkt).
#pragma once

#include <Arduino.h>

#include <deque>
#include <mutex>
#include <string>

#include "sensor_link.h"  // SensorDaten

class SensorDirekt {
public:
    void begin();    // Pins aus config.h
    void starten();  // Mess- und NFC-Task auf Kern 0
    SensorDaten daten();
    bool ueber_wlan() const { return false; }
    void tara();
    void kalibrieren(int gramm);
    bool meldung_holen(bool& ok, std::string& text);
    // für die Diagnose über USB
    bool waage_da();
    bool nfc_bereit(int seite);  // 0 = links, 1 = rechts
    long roh();
    bool beschaeftigt();       // Tara oder Kalibrieren läuft
    bool waage_liefert();      // Messwert jünger als 1 s
    void kalibrierung_loeschen();

private:
    static void mess_task(void* selbst);
    static void nfc_task(void* selbst);
    void mess_schritt();
    void nfc_schritt();
    void melden(bool ok, const std::string& text);
};
