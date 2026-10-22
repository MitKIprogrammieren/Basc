// Etiketten auf einem Phomemo-Etikettendrucker per Bluetooth LE – wie pi/filamentwaage/phomemo.py.
// Das Etikett zeichnet die Oberfläche (etikett.js) und schickt es als 1-Bit-Raster.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// Ganz am Anfang von setup(): Bluetooth einschalten (oder nach Absturz sperren)
void drucker_frueh();
bool drucker_gesperrt();
void drucker_start();
// Einstellungen für /api/etikett/einstellungen (ohne Hersteller und Lager-Adresse)
void drucker_einstellungen(JsonDocument& d);
void drucker_setzen(JsonObjectConst o);
// {breite, hoehe, daten (Base64), kopien} in die Warteschlange; Rückgabe: Fehlertext oder nullptr
const char* drucker_drucken(JsonObjectConst o);
// Bluetooth-Suche: startet sie bei Bedarf. true = fertig, Ergebnis in d ({ok, text, geraete})
bool drucker_suche(JsonDocument& d);

extern void (*g_drucker_meldung)(const String& text, bool fehler);
