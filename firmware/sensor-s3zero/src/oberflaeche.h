// Komplette Waagen-Oberfläche (dieselbe wie auf dem Pi) direkt von der Sensorplatine.
// Webdateien liegen im Dateisystem (LittleFS), die API spricht wie pi/filamentwaage/app.py.
#pragma once

void oberflaeche_start();   // einmal, sobald das Heim-WLAN steht
void oberflaeche_weiter();  // aus loop()
