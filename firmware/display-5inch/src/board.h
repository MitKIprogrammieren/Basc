// Hardware des Displays: Bildschirm, Touch, Hintergrundlicht.
// board.cpp = Waveshare ESP32-S3-Touch-LCD-5, board_sc01plus.cpp = WT32-SC01 Plus.
#pragma once

#include <stdint.h>

// Startet Display und Touch und meldet beides bei LVGL an.
// lv_init() muss vorher gelaufen sein. Gibt false zurück, wenn das Display nicht startet.
bool board_start();
void board_licht(bool an);

// Bildschirm aus/an (board_gemeinsam.cpp): Ist das Licht aus, weckt die erste Berührung nur –
// sie wird nicht als Tippen weitergegeben, bis der Finger wieder losgelassen ist.
bool board_licht_an();
uint32_t board_beruehrt_um();  // millis() der letzten Berührung, 0 = noch keine
// Aus touch_lesen(): false, wenn die Berührung nicht an LVGL weitergehen soll
bool board_beruehrung(bool gedrueckt);
void board_licht_merken(bool an);  // nur für board_licht()
