// Hardware des Displays: Bildschirm, Touch, Hintergrundlicht.
// board.cpp = Waveshare ESP32-S3-Touch-LCD-5, board_sc01plus.cpp = WT32-SC01 Plus.
#pragma once

// Startet Display und Touch und meldet beides bei LVGL an.
// lv_init() muss vorher gelaufen sein. Gibt false zurück, wenn das Display nicht startet.
bool board_start();
void board_licht(bool an);
