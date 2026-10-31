// Speicher für LVGL: klein → interner RAM, groß → PSRAM
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void* lv_speicher_holen(size_t n);
void* lv_speicher_neu(void* p, size_t n);
#ifdef __cplusplus
}
#endif
