// include/disk_cache.h — tiny blob cache on the (previously unused) 1.5 MB
// "spiffs" flash partition, mounted as LittleFS.
//
// Purpose: downloaded/decoded artwork (ticker logos, NFT images) survives
// reboots and re-flashes, so the device doesn't hit the network for the same
// bytes on every boot. Files are keyed by a 32-bit FNV-1a hash of the source
// URL — same URL, same file; a changed URL simply creates a new entry.
//
// The partition sits at 0x670000–0x7FAFFF, BELOW the relocated NVS and above
// the app slots, so (like NVS) no bootloader/app/factory image ever overwrites
// it: the cache survives web re-flashes and OTA. Only a full-chip erase (or a
// failed mount → auto-format) clears it.
//
// All functions are safe to call from bg tasks (esp_littlefs serialises with
// an internal mutex). Buffers are allocated in PSRAM.

#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>

namespace diskcache {

static bool s_ok = false;

inline void init() {
    // Partition label "spiffs" (see partitions.csv); format on first use.
    s_ok = LittleFS.begin(true /*formatOnFail*/, "/lfs", 5, "spiffs");
    if (s_ok) {
        Serial.printf("DiskCache: mounted, %u/%u KB used\n",
                      (unsigned)(LittleFS.usedBytes() / 1024),
                      (unsigned)(LittleFS.totalBytes() / 1024));
    } else {
        Serial.println("DiskCache: mount FAILED — caching disabled");
    }
}

inline String _pathFor(const char* ns, const char* key) {
    uint32_t h = 2166136261u;                       // FNV-1a over the URL
    for (const char* p = key; *p; p++) { h ^= (uint8_t)*p; h *= 16777619u; }
    char buf[40];
    snprintf(buf, sizeof(buf), "/%s_%08x.bin", ns, (unsigned)h);
    return String(buf);
}

// Load a cached blob into a fresh PSRAM buffer. Returns nullptr on miss.
inline uint8_t* loadAlloc(const char* ns, const char* key, size_t* outLen) {
    *outLen = 0;
    if (!s_ok) return nullptr;
    String path = _pathFor(ns, key);
    File f = LittleFS.open(path, "r");
    if (!f) return nullptr;
    size_t len = f.size();
    if (len == 0) { f.close(); return nullptr; }
    uint8_t* buf = (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = (uint8_t*)malloc(len);
    if (!buf) { f.close(); return nullptr; }
    size_t got = f.read(buf, len);
    f.close();
    if (got != len) { free(buf); return nullptr; }
    *outLen = len;
    return buf;
}

inline bool save(const char* ns, const char* key, const uint8_t* data, size_t len) {
    if (!s_ok || !data || len == 0) return false;
    // Leave headroom: if the partition is nearly full just skip (no eviction
    // policy needed at our sizes — logos are ~5 KB, NFT source images ~25 KB).
    if (LittleFS.totalBytes() - LittleFS.usedBytes() < len + 16 * 1024) {
        Serial.printf("DiskCache: full, skipping %s\n", key);
        return false;
    }
    String path = _pathFor(ns, key);
    File f = LittleFS.open(path, "w");
    if (!f) return false;
    size_t wr = f.write(data, len);
    f.close();
    if (wr != len) { LittleFS.remove(path); return false; }
    return true;
}

inline bool has(const char* ns, const char* key) {
    if (!s_ok) return false;
    return LittleFS.exists(_pathFor(ns, key));
}

inline void remove(const char* ns, const char* key) {
    if (!s_ok) return;
    LittleFS.remove(_pathFor(ns, key));
}

} // namespace diskcache
