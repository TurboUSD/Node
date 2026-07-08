// include/net_lock.h — ONE TLS connection at a time, firmware-wide.
//
// Why: every WiFiClientSecure handshake needs ~45 KB of INTERNAL RAM
// (mbedtls buffers can't live in PSRAM). The main loop (treasury/debt/
// heartbeat/mining), the ticker worker (batch/logos/charts) and the NFT
// worker all do HTTPS independently — whenever two overlapped, the second
// died with "SSL - Memory allocation failed" (-32512), which surfaced all
// over the app as random "connection refused" failures.
//
// Usage: the long-running WORKER TASKS hold the lock for their whole run
// (they're the heavyweights); the MAIN LOOP try-takes it with zero timeout
// and simply postpones its periodic fetches to the next loop iteration when
// a worker is active — the UI must never block.

#pragma once
#include <Arduino.h>
#include <esp_heap_caps.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

extern SemaphoreHandle_t gNetLock;

// ── TLS RAM viability ─────────────────────────────────────────────────────────
// A handshake needs ~45 KB of internal heap in total, with one ≥~17 KB
// contiguous block for mbedTLS's input buffer. Checking BEFORE connecting turns
// "silent -1 and a wasted attempt" into "wait a moment and succeed".
inline bool netTlsRamOk(size_t needFree = 56 * 1024, size_t needBlk = 20 * 1024) {
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) >= needFree &&
           heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) >= needBlk;
}

// Poll until netTlsRamOk() or timeout. IMPORTANT: call this BEFORE netLock(),
// never while holding it — waiting inside the lock stalls every other network
// worker behind this one, and the blocked workers' task stacks then pin the
// very RAM being waited for (the 0.2.1 boot jam: NFT waiting in-lock while
// tickers/logos/charts all queued up and nothing loaded anywhere).
inline bool netWaitTlsRam(uint32_t maxWaitMs) {
    uint32_t t0 = millis();
    while (!netTlsRamOk()) {
        if (millis() - t0 >= maxWaitMs) return false;
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    return true;
}

inline void netLockInit() {
    if (!gNetLock) gNetLock = xSemaphoreCreateMutex();
}

// Blocking take (workers). Always pair with netUnlock().
inline void netLock() {
    if (gNetLock) xSemaphoreTake(gNetLock, portMAX_DELAY);
}

// Non-blocking take (main loop). Returns false when a worker holds it.
inline bool netTryLock() {
    return gNetLock && xSemaphoreTake(gNetLock, 0) == pdTRUE;
}

inline void netUnlock() {
    if (gNetLock) xSemaphoreGive(gNetLock);
}
