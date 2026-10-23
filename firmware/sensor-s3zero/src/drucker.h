// Etiketten auf einem Phomemo-Etikettendrucker per Bluetooth LE – wie pi/filamentwaage/phomemo.py.
// Das Etikett zeichnet die Oberfläche (etikett.js) und schickt es als 1-Bit-Raster.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// Ganz am Anfang von setup(): Bluetooth einschalten (oder nach Absturz sperren)
void drucker_frueh(bool erlaubt);
bool drucker_gesperrt();
String drucker_bruecke();  // Adresse der Druckbrücke oder leer
String drucker_bericht();  // was beim letzten Druck passiert ist (für /api/diagnose)
void drucker_start();
// Einstellungen für /api/etikett/einstellungen (ohne Hersteller und Lager-Adresse)
void drucker_einstellungen(JsonDocument& d);
void drucker_setzen(JsonObjectConst o);
// {breite, hoehe, daten (Base64), kopien} in die Warteschlange; Rückgabe: Fehlertext oder nullptr
const char* drucker_drucken(JsonObjectConst o);
// Bluetooth-Suche: startet sie bei Bedarf. true = fertig, Ergebnis in d ({ok, text, geraete})
bool drucker_suche(JsonDocument& d);
// alle Bluetooth-Verbindungen lösen (läuft im Drucker-Task)
void drucker_trennen();
// kleinster TSPL-Auftrag (zwei Balken) zum Testen
void drucker_minitest();
// für die Kopfleiste: 0 = kein Drucker, 1 = eingerichtet, 2 = letzter Druck fehlgeschlagen
int drucker_zustand();
// Stand der Druckbrücke (jede Minute abgefragt): 1 = Drucker verbunden, 0 = Brücke da, Drucker nicht, -1 = Brücke nicht erreichbar
void drucker_bruecke_melden(int stand);

extern void (*g_drucker_meldung)(const String& text, bool fehler);
