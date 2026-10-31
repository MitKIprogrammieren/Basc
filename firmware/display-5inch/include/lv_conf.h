/* LVGL 8.3 Konfiguration für das Waveshare ESP32-S3-Touch-LCD-5 (800×480) und WT32-SC01 Plus (480×320).
 * Nicht gesetzte Werte nimmt LVGL aus lv_conf_internal.h. */
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

/* Speicher: auf dem ESP32 aus dem PSRAM, damit der interne RAM für WLAN frei bleibt */
#define LV_MEM_CUSTOM 1
#ifdef ARDUINO
#define LV_MEM_CUSTOM_INCLUDE <esp32-hal-psram.h>
#define LV_MEM_CUSTOM_ALLOC ps_malloc
#define LV_MEM_CUSTOM_FREE free
#define LV_MEM_CUSTOM_REALLOC ps_realloc
#else
#define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC malloc
#define LV_MEM_CUSTOM_FREE free
#define LV_MEM_CUSTOM_REALLOC realloc
#endif

#ifdef ARDUINO
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())
#endif

#define LV_DISP_DEF_REFR_PERIOD 16
#define LV_INDEV_DEF_READ_PERIOD 20
#define LV_DPI_DEF 187

#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0

/* Eingebaute Schriften nur noch für die Symbole (LV_SYMBOL_*) */
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(fl_12) LV_FONT_DECLARE(fl_14) LV_FONT_DECLARE(fl_16)
#define LV_FONT_DEFAULT &fl_16

#define LV_TXT_ENC LV_TXT_ENC_UTF8
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1
#define LV_THEME_DEFAULT_GROW 0

#define LV_USE_ARC 1
#define LV_USE_BAR 1
#define LV_USE_BTN 1
#define LV_USE_LABEL 1
#define LV_USE_TEXTAREA 1
#define LV_USE_KEYBOARD 1
#define LV_USE_BTNMATRIX 1
#define LV_USE_CANVAS 1
#define LV_USE_IMG 1

/* Herstellerlogos (lodepng) und QR-Code aufs Etikett */
#define LV_USE_PNG 1
#define LV_USE_QRCODE 1

#endif /* LV_CONF_H */
