#include "lv_speicher.h"

#include <esp_heap_caps.h>
#include <stdlib.h>

// Bis zu dieser Größe kommt ein Block in den internen RAM …
static const size_t KLEIN = 1024;
// … solange danach noch so viel interner RAM frei ist (WLAN, TLS und die Bildpuffer brauchen ihn)
static const size_t RESERVE = 72 * 1024;

static bool intern(size_t n) {
    return n <= KLEIN && heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) > RESERVE + n;
}

extern "C" void* lv_speicher_holen(size_t n) {
    void* p = nullptr;
    if (intern(n)) p = heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = malloc(n);
    return p;
}

extern "C" void* lv_speicher_neu(void* p, size_t n) {
    if (!p) return lv_speicher_holen(n);
    if (!n) {
        free(p);
        return nullptr;
    }
    // bleibt, wo er ist, wenn möglich; sonst neu holen (heap_caps_realloc wechselt nötigenfalls den Bereich)
    void* q = heap_caps_realloc(p, n, intern(n) ? (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) : (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!q) q = heap_caps_realloc(p, n, MALLOC_CAP_8BIT);
    return q;
}
