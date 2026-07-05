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
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

extern SemaphoreHandle_t gNetLock;

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
