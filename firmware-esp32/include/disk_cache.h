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

// One mutex serialises ALL cache ops. Without it, a load's exists()→open()
// sequence (core 0/1 workers) could be interrupted by another op's eviction
// deleting that very file in between → open() fails with the VFS "does not
// exist, no permits for creation" ERROR line, and each of those blocks ~40 ms
// and visibly stutters/flickers the UI. Serialising the whole op removes the
// race (and thus the flicker) on a near-full, actively-evicting partition.
static SemaphoreHandle_t s_mtx = nullptr;
// TRY-lock with a timeout, never a forever wait. If a cache op ever holds the
// mutex too long (a big eviction on a near-full partition), a forever wait on
// the LVGL/loopTask — which the Task Watchdog watches — would starve it past the
// ~5 s TWDT and PANIC-REBOOT the device (the NFT-decode crash loop). With a
// bounded wait the blocked caller just treats it as a cache miss/skip instead.
static const uint32_t CACHE_LOCK_MS = 2000;
struct _Guard {
    bool locked = false;
    _Guard() {
        if (!s_mtx) { locked = true; return; }
        locked = (xSemaphoreTake(s_mtx, pdMS_TO_TICKS(CACHE_LOCK_MS)) == pdTRUE);
    }
    ~_Guard() { if (locked && s_mtx) xSemaphoreGive(s_mtx); }
};

inline void _purgeOrphans(const char* const* deadPrefixes, int nPrefixes);   // defined below

inline void init() {
    if (!s_mtx) s_mtx = xSemaphoreCreateMutex();
    // Partition label "spiffs" (see partitions.csv); format on first use.
    s_ok = LittleFS.begin(true /*formatOnFail*/, "/lfs", 5, "spiffs");
    if (s_ok) {
        Log.printf("DiskCache: mounted, %u/%u KB used\n",
                      (unsigned)(LittleFS.usedBytes() / 1024),
                      (unsigned)(LittleFS.totalBytes() / 1024));
        // Reclaim space from superseded decoded-cover formats so the partition
        // isn't permanently full of dead blobs (which caused constant eviction
        // of live art + re-downloads + the decode-time watchdog reboot).
        static const char* kDead[] = { "dec_", "dec2_" };
        _purgeOrphans(kDead, 2);
        Log.printf("DiskCache: %u/%u KB used after purge\n",
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
    _Guard _g;   // block eviction from deleting this file mid-open (see s_mtx)
    if (!_g.locked) return nullptr;   // busy → treat as a miss, decode fresh
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
    if (_freeBytes() >= need) return;
    // ONE directory scan (the expensive part), then in-memory oldest-first
    // deletes. The old code re-scanned the whole directory for EVERY single file
    // it deleted (O(files²)); on a near-full partition that held the cache mutex
    // for seconds, starving the watchdog-watched loopTask into a reboot.
    struct Ent { char path[40]; time_t t; };
    const int MAXF = 320;
    Ent* ents = (Ent*)heap_caps_malloc(sizeof(Ent) * MAXF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ents) return;   // no PSRAM: skip eviction rather than risk a long scan loop
    int cnt = 0;
    File dir = LittleFS.open("/");
    if (dir) {
        for (File f = dir.openNextFile(); f && cnt < MAXF; f = dir.openNextFile()) {
            if (!f.isDirectory()) {
                strncpy(ents[cnt].path, f.path(), sizeof(ents[cnt].path) - 1);
                ents[cnt].path[sizeof(ents[cnt].path) - 1] = 0;
                ents[cnt].t = f.getLastWrite();
                cnt++;
            }
            f.close();
        }
        dir.close();
    }
    int evicted = 0;
    while (_freeBytes() < need) {
        int oldest = -1;
        for (int i = 0; i < cnt; i++) {
            if (ents[i].path[0] == 0) continue;
            if (oldest < 0 || ents[i].t < ents[oldest].t) oldest = i;
        }
        if (oldest < 0) break;   // nothing left to evict
        LittleFS.remove(String(ents[oldest].path));
        ents[oldest].path[0] = 0;
        // BREATHE between deletes: each remove() is a flash erase (tens of ms)
        // and this loop runs with the cache mutex held. On an 83%-full
        // partition a burst of evictions monopolised the CPU long enough to
        // starve IDLE0 and trip the Task WDT ("Reset reason: 6" reboots).
        vTaskDelay(pdMS_TO_TICKS(5));
        // Hard cap per pass: better to fail ONE cache write (the caller just
        // skips persisting that blob) than to block until free space wins.
        if (++evicted >= 24) break;
    }
    free(ents);
}

// One-time cleanup of ORPHANED namespaces: when a decoded-cover format changes
// we bump its namespace ("dec" → "dec2" → "dec3"), but the old blobs linger and
// fill the partition, so eviction then keeps deleting LIVE logos/images and the
// device re-downloads everything each boot. Delete any file whose name starts
// with a dead prefix so live entries persist as intended.
inline void _purgeOrphans(const char* const* deadPrefixes, int nPrefixes) {
    if (!s_ok) return;
    _Guard _g;
    if (!_g.locked) return;
    File dir = LittleFS.open("/");
    if (!dir) return;
    String toDelete[64];
    int nDel = 0;
    for (File f = dir.openNextFile(); f && nDel < 64; f = dir.openNextFile()) {
        if (!f.isDirectory()) {
            String p = f.path();                 // e.g. "/dec2_1a2b3c4d.bin"
            const char* name = p.c_str();
            if (name[0] == '/') name++;
            for (int k = 0; k < nPrefixes; k++) {
                if (strncmp(name, deadPrefixes[k], strlen(deadPrefixes[k])) == 0) {
                    toDelete[nDel++] = p; break;
                }
            }
        }
        f.close();
    }
    dir.close();
    for (int i = 0; i < nDel; i++) LittleFS.remove(toDelete[i]);
    if (nDel) Log.printf("DiskCache: purged %d orphaned entries\n", nDel);
}

inline bool save(const char* ns, const char* key, const uint8_t* data, size_t len) {
    if (!s_ok || !data || len == 0) return false;
    _Guard _g;   // evict + write atomically vs. concurrent loads (see s_mtx)
    if (!_g.locked) return false;   // busy → skip caching this blob (harmless)
    // Make room by evicting oldest entries rather than refusing forever. Free a
    // BIG chunk in one go (not just this blob) so a burst of NFT-cover saves
    // doesn't trigger a fresh eviction scan on every single one.
    const size_t HEADROOM = 192 * 1024;
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
    _Guard _g;
    if (!_g.locked) return false;
    return LittleFS.exists(_pathFor(ns, key));
}

inline void remove(const char* ns, const char* key) {
    if (!s_ok) return;
    _Guard _g;
    if (!_g.locked) return;
    LittleFS.remove(_pathFor(ns, key));
}

} // namespace diskcache
