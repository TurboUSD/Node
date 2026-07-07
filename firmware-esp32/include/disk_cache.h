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
#include "weblog.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>

namespace diskcache {

static bool s_ok = false;

inline void init() {
    // Partition label "spiffs" (see partitions.csv); format on first use.
    s_ok = LittleFS.begin(true /*formatOnFail*/, "/lfs", 5, "spiffs");
    if (s_ok) {
        Log.printf("DiskCache: mounted, %u/%u KB used\n",
                      (unsigned)(LittleFS.usedBytes() / 1024),
                      (unsigned)(LittleFS.totalBytes() / 1024));
    } else {
        Log.println("DiskCache: mount FAILED — caching disabled");
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
    // exists() first: opening a missing file makes the Arduino VFS layer log
    // an ERROR line over serial ("no permits for creation") — besides the
    // noise, each line blocks ~40 ms and visibly stuttered UI animations.
    if (!LittleFS.exists(path)) return nullptr;
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

// Free space (bytes) currently available on the cache partition.
inline size_t _freeBytes() { return LittleFS.totalBytes() - LittleFS.usedBytes(); }

// Evict the OLDEST cache entries (by last-write time) until at least `need`
// bytes are free. Before this, a full partition simply refused every new write,
// so once it filled (a 40-NFT wallet does), nothing new ever cached and every
// image re-downloaded + re-decoded on each visit/reboot. LRU-by-write eviction
// keeps the cache useful. Deleting a still-displayed blob is harmless — its
// pixels already live in PSRAM; only the disk copy goes.
inline void _evictUntil(size_t need) {
    if (!s_ok) return;
    int guard = 0;
    while (_freeBytes() < need && guard++ < 256) {
        File dir = LittleFS.open("/");
        if (!dir) return;
        String oldestPath; time_t oldestT = 0; bool first = true;
        for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
            if (!f.isDirectory()) {
                time_t t = f.getLastWrite();
                String p = f.path();
                if (first || t < oldestT) { oldestT = t; oldestPath = p; first = false; }
            }
            f.close();
        }
        dir.close();
        if (oldestPath.length() == 0) break;   // nothing left to evict
        LittleFS.remove(oldestPath);
    }
}

inline bool save(const char* ns, const char* key, const uint8_t* data, size_t len) {
    if (!s_ok || !data || len == 0) return false;
    // Make room by evicting oldest entries rather than refusing forever.
    const size_t HEADROOM = 16 * 1024;
    if (_freeBytes() < len + HEADROOM) _evictUntil(len + HEADROOM);
    if (_freeBytes() < len + 2 * 1024) {   // couldn't free enough (blob too big)
        Log.printf("DiskCache: cannot fit %s (%u B) even after eviction\n", key, (unsigned)len);
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
