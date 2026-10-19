// Hardware des Waveshare ESP32-S3-Touch-LCD-5: Display, Touch, Hintergrundlicht.
#pragma once

// Startet I2C, CH422G, Display und Touch und meldet beides bei LVGL an.
// lv_init() muss vorher gelaufen sein. Gibt false zurück, wenn das Display nicht startet.
bool board_start();
void board_licht(bool an);
