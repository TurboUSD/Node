// include/lv_psram_mem.h — LVGL heap allocators backed by PSRAM.
//
// WHY: plain malloc() only spills to PSRAM for allocations above the
// CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL threshold (~16 KB), so the THOUSANDS
// of small widget/style/draw allocations LVGL makes for seven full screens
// all landed in INTERNAL RAM — filling and fragmenting it until no ~45 KB
// contiguous block remained for a TLS handshake. That is the root cause of
// the recurring "SSL - Memory allocation failed" (-32512) plague (NFTs,
// mining feed, random fetch failures).
//
// These allocators put LVGL memory in PSRAM first (8 MB of it, and the
// ESP32-S3's cache makes it plenty fast for UI structures), falling back to
// internal RAM only if PSRAM ever runs dry. free() works for both heaps.
//
// This header is included from lv_conf.h (LV_MEM_CUSTOM_INCLUDE) and gets
// compiled into LVGL's C sources — keep it C-compatible.

#ifndef LV_PSRAM_MEM_H
#define LV_PSRAM_MEM_H

#include <stdlib.h>
#include "esp_heap_caps.h"

static inline void* lv_psram_alloc(size_t size) {
    void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(size);
}

static inline void* lv_psram_realloc(void* ptr, size_t size) {
    void* p = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : realloc(ptr, size);
}

static inline void lv_psram_free(void* ptr) {
    free(ptr);   // valid for both internal and PSRAM allocations
}

#endif
