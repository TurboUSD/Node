// include/psram_alloc.h — allocators that place big containers and JSON
// documents in PSRAM (8 MB, mostly idle) instead of the scarce internal heap
// that WiFi + mbedTLS live off. Every allocation falls back to the internal
// heap when PSRAM is unavailable, so the worst case equals the old behaviour.
//
// Used by: NftScreen::_nftCache (up to ~24 KB with a full wallet), and the
// large ArduinoJson parses (OpenSea account pages, GeckoTerminal OHLCV,
// ticker list) whose documents spiked 20-40 KB of internal heap per parse.

#pragma once
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

// STL allocator (std::vector etc.) → PSRAM.
template <typename T>
struct PsramAlloc {
    using value_type = T;
    PsramAlloc() = default;
    template <class U> PsramAlloc(const PsramAlloc<U>&) {}
    T* allocate(size_t n) {
        void* p = heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!p) p = malloc(n * sizeof(T));
        return (T*)p;
    }
    void deallocate(T* p, size_t) { free(p); }
    template <class U> bool operator==(const PsramAlloc<U>&) const { return true; }
    template <class U> bool operator!=(const PsramAlloc<U>&) const { return false; }
};

// ArduinoJson v7 allocator → PSRAM. Usage: JsonDocument doc(psramJsonAlloc());
struct PsramJsonAllocator : ArduinoJson::Allocator {
    void* allocate(size_t n) override {
        void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        return p ? p : malloc(n);
    }
    void deallocate(void* p) override { free(p); }
    void* reallocate(void* p, size_t n) override {
        void* np = heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        return np ? np : realloc(p, n);
    }
};
inline PsramJsonAllocator* psramJsonAlloc() {
    static PsramJsonAllocator a;
    return &a;
}
