// Gemeinsam für beide Displays: Bildschirm aus/an und Wecken per Berührung (siehe board.h).
#include <Arduino.h>

#include "board.h"

namespace {
bool g_an = true;
bool g_schlucken = false;  // Finger, der gerade geweckt hat – erst nach dem Loslassen wieder durchlassen
uint32_t g_beruehrt = 0;
}  // namespace

void board_licht_merken(bool an) {
    if (an != g_an) Serial.printf("[licht] %s\n", an ? "an" : "aus");
    g_an = an;
}
bool board_licht_an() { return g_an; }
uint32_t board_beruehrt_um() { return g_beruehrt; }

bool board_beruehrung(bool gedrueckt) {
    if (!gedrueckt) {
        g_schlucken = false;
        return false;
    }
    g_beruehrt = millis();
    if (!g_an) {
        Serial.println("[licht] Berührung weckt");
        board_licht(true);
        g_schlucken = true;
    }
    return !g_schlucken;
}
