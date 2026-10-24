// Schnittstelle zwischen Messung (main.cpp) und Oberfläche (oberflaeche.cpp).
#pragma once

#include <Arduino.h>

struct Messwert {
    float gramm = 0;      // brutto, inkl. Spule
    bool stabil = false;
    bool kalibriert = false;
    bool hx711_ok = false;  // in der letzten Sekunde kam ein Messwert
    String tag_l, tag_r;
};

Messwert messwert();
bool nfc_leser_bereit(int seite);  // 0 = links, 1 = rechts

// Tara (kal = false) oder Kalibrieren mit gramm. Rückgabe: Fehlertext oder nullptr.
// antwort_an = nullptr, wenn die Oberfläche den Vorgang startet.
const char* vorgang_starten(bool kal, int gramm, Stream* antwort_an);
// Wird aufgerufen, wenn Tara oder Kalibrieren fertig ist (aus loop()).
extern void (*g_vorgang_fertig)(bool ok, const String& text);

void wlan_setzen(const char* ssid, const char* pw);
void alles_zuruecksetzen();  // Kalibrierung und WLAN löschen (Werkseinstellungen)
bool pad_verbunden();        // gerade hängt ein Pad 7 / Pi an Port 4711
String start_bericht();  // Grund des letzten Neustarts, für /api/diagnose

extern volatile uint32_t g_loops;  // Durchläufe der Hauptschleife (main.cpp)
