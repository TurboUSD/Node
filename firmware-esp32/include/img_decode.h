// include/img_decode.h — shared download + decode + downscale helper.
//
// Fetches an image over HTTP(S), decodes PNG (lodepng, bundled with LVGL)
// or baseline JPEG (tjpgd, ditto), nearest-neighbour scales it to the
// requested size and returns a ready-to-blit LV_IMG_CF_TRUE_COLOR (RGB565,
// 2 B/px, LV_COLOR_16_SWAP=0) bitmap in PSRAM. Same technique as the ticker
// logo pipeline in screen_tickers.h, generalised: decoding ONCE in a bg task
// means zero per-frame decode cost on the RGB panel.
//
// Disk cache: pass `cacheKey` (usually the canonical source URL) and the
// COMPRESSED download is persisted to the LittleFS partition — next boot the
// bytes come from flash instead of the network (decode is repeated, but
// that's tens of ms vs. a TLS handshake + download).
//
// Returns nullptr on any failure (HTTP error, unsupported format — e.g.
// webp/gif —, decode error, out of memory). Caller owns the buffer (free()).

#include "weblog.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <lvgl.h>
#include <esp_heap_caps.h>
#include "disk_cache.h"

// lodepng is compiled into LVGL when LV_USE_PNG=1; we only need this one call.
extern "C" unsigned lodepng_decode32(unsigned char** out, unsigned* w, unsigned* h,
                                     const unsigned char* in, size_t insize);
// tjpgd ships with LVGL's sjpg (LV_USE_SJPG=1); has its own extern "C" guards.
#include <src/extra/libs/sjpg/tjpgd.h>

namespace imgdec {

struct JpegCtx {
    const uint8_t* in;
    size_t   inSize;
    size_t   inPos;
    uint8_t* rgb;       // RGB888 out, w*h*3
    uint16_t w, h;
};

static size_t _jin(JDEC* jd, uint8_t* buf, size_t len) {
    JpegCtx* c = (JpegCtx*)jd->device;
    if (len > c->inSize - c->inPos) len = c->inSize - c->inPos;
    if (buf) memcpy(buf, c->in + c->inPos, len);
    c->inPos += len;
    return len;
}

static int _jout(JDEC* jd, void* bitmap, JRECT* rect) {
    JpegCtx* c = (JpegCtx*)jd->device;
    const uint8_t* src = (const uint8_t*)bitmap;   // RGB888 (JD_FORMAT 0)
    for (int y = rect->top; y <= rect->bottom; y++)
        for (int x = rect->left; x <= rect->right; x++) {
            if (x < c->w && y < c->h)
                memcpy(&c->rgb[(y * c->w + x) * 3], src, 3);
            src += 3;
        }
    return 1;
}

// Allocate preferring PSRAM (these buffers are big), internal as fallback.
static inline uint8_t* _alloc(size_t n) {
    uint8_t* p = (uint8_t*)heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : (uint8_t*)malloc(n);
}

// Decode `body` (PNG/JPEG by magic) and scale to FIT INSIDE maxW×maxH
// preserving aspect ratio (contain — no cropping). Actual output dims are
// returned via outW/outH. Does NOT free `body`. NULL on failure.
static uint8_t* _decodeScale(const uint8_t* body, size_t len,
                             int maxW, int maxH, const char* tag,
                             uint16_t* outWp, uint16_t* outHp,
                             uint32_t bgColor) {
    unsigned char* rgba = nullptr;
    unsigned iw = 0, ih = 0;
    bool rgbaFromLvMem = false;

    if (body[0] == 0x89 && body[1] == 0x50) {              // PNG
        unsigned rc = lodepng_decode32(&rgba, &iw, &ih, body, len);
        if (rc != 0 || !rgba || iw == 0 || ih == 0) {
            Log.printf("img[%s] png decode rc=%u\n", tag, rc);
            if (rgba) lv_mem_free(rgba);
            return nullptr;
        }
        rgbaFromLvMem = true;                              // lodepng → lv_mem
    } else if (body[0] == 0xFF && body[1] == 0xD8) {       // baseline JPEG
        uint8_t* work = (uint8_t*)malloc(4096);            // tjpgd needs ~3.1 KB
        if (!work) return nullptr;
        JDEC jd;
        JpegCtx ctx{ body, len, 0, nullptr, 0, 0 };
        if (jd_prepare(&jd, _jin, work, 4096, &ctx) != JDR_OK) {
            Log.printf("img[%s] jd_prepare failed\n", tag);
            free(work); return nullptr;
        }
        ctx.w = jd.width; ctx.h = jd.height;
        if (ctx.w == 0 || ctx.h == 0 || (uint32_t)ctx.w * ctx.h > 800u * 800u) {
            Log.printf("img[%s] bad dims %ux%u\n", tag, ctx.w, ctx.h);
            free(work); return nullptr;
        }
        ctx.rgb = _alloc((uint32_t)ctx.w * ctx.h * 3);
        if (!ctx.rgb) { free(work); return nullptr; }
        JRESULT dr = jd_decomp(&jd, _jout, 0);
        free(work);
        if (dr != JDR_OK) {
            Log.printf("img[%s] tjpgd rc=%d (%ux%u)\n", tag, (int)dr, ctx.w, ctx.h);
            free(ctx.rgb); return nullptr;
        }
        iw = ctx.w; ih = ctx.h;
        rgba = _alloc((uint32_t)iw * ih * 4);
        if (!rgba) { free(ctx.rgb); return nullptr; }
        for (uint32_t i = 0; i < (uint32_t)iw * ih; i++) {
            rgba[i * 4 + 0] = ctx.rgb[i * 3 + 0];
            rgba[i * 4 + 1] = ctx.rgb[i * 3 + 1];
            rgba[i * 4 + 2] = ctx.rgb[i * 3 + 2];
            rgba[i * 4 + 3] = 255;
            // Breathe every 64k px: this loop hammers the PSRAM bus the RGB
            // panel also streams the framebuffer from — unthrottled it starves
            // the panel DMA and the screen visibly shimmers.
            if ((i & 0xFFFF) == 0xFFFF) delay(1);
        }
        free(ctx.rgb);
    } else {
        Log.printf("img[%s] unsupported format %02X%02X (webp/gif?)\n", tag, body[0], body[1]);
        return nullptr;
    }

    // Fit inside the box, preserving aspect (letterbox handled by the caller).
    int outW = maxW, outH = (int)((uint64_t)maxW * ih / iw);
    if (outH > maxH) { outH = maxH; outW = (int)((uint64_t)maxH * iw / ih); }
    if (outW < 1) outW = 1;
    if (outH < 1) outH = 1;

    // Nearest-neighbour scale → RGB565 little-endian ([lo, hi] per pixel).
    uint8_t* px = _alloc((size_t)outW * outH * 2);
    if (!px) { if (rgbaFromLvMem) lv_mem_free(rgba); else free(rgba); return nullptr; }
    for (int y = 0; y < outH; y++) {
        if ((y & 31) == 31) delay(1);   // PSRAM-bus breather (see above)
        unsigned sy = (unsigned)((uint64_t)y * ih / outH);
        for (int x = 0; x < outW; x++) {
            unsigned sx = (unsigned)((uint64_t)x * iw / outW);
            const unsigned char* sp = &rgba[(sy * iw + sx) * 4];
            // Composite transparency onto bgColor (per-image — e.g. a
            // NodeMonke's Background trait orange; default black for the dark
            // gallery). Ignoring alpha used to render transparent-background
            // on-chain PNGs on a glaring white sheet.
            unsigned a  = sp[3];
            unsigned ia = 255 - a;
            uint8_t br = (uint8_t)((bgColor >> 16) & 0xFF);
            uint8_t bgc = (uint8_t)((bgColor >> 8) & 0xFF);
            uint8_t bb = (uint8_t)(bgColor & 0xFF);
            lv_color_t c = lv_color_make((uint8_t)((sp[0] * a + br  * ia) / 255),
                                         (uint8_t)((sp[1] * a + bgc * ia) / 255),
                                         (uint8_t)((sp[2] * a + bb  * ia) / 255));
            px[(y * outW + x) * 2 + 0] = c.full & 0xFF;
            px[(y * outW + x) * 2 + 1] = c.full >> 8;
        }
    }
    if (rgbaFromLvMem) lv_mem_free(rgba); else free(rgba);
    if (outWp) *outWp = (uint16_t)outW;
    if (outHp) *outHp = (uint16_t)outH;
    Log.printf("img[%s] decoded %ux%u -> %dx%d OK\n", tag, iw, ih, outW, outH);
    return px;
}

// Download `url` (or hit the disk cache if `cacheKey` was seen before) and
// produce an outW×outH RGB565 bitmap. `tag` is only for serial logs.
static uint8_t* fetchRgb565(const char* url, int maxW, int maxH,
                            const char* tag, const char* cacheKey = nullptr,
                            uint16_t* outWp = nullptr, uint16_t* outHp = nullptr,
                            uint32_t bgColor = 0x000000) {
    uint8_t* body    = nullptr;
    size_t   len     = 0;
    bool     fromDisk = false;

    if (cacheKey) {
        body = diskcache::loadAlloc("img", cacheKey, &len);
        if (body) {
            fromDisk = true;
            Log.printf("img[%s] from disk cache (%u B)\n", tag, (unsigned)len);
        }
    }

    if (!body) {
        HTTPClient http;
        http.useHTTP10(true);
        http.begin(url);
        // No image/webp here: imgix-style CDNs (i.seadn.io) negotiate the
        // format from Accept, and there's no webp decoder on-device.
        http.addHeader("Accept", "image/jpeg,image/png");
        http.setTimeout(15000);
        int code = http.GET();
        if (code != 200) {
            Log.printf("img[%s] GET %d %s\n", tag, code, url);
            http.end();
            return nullptr;
        }
        const size_t CAP = 300 * 1024;
        int declared = http.getSize();
        if (declared > (int)CAP) { Log.printf("img[%s] too big (%d B)\n", tag, declared); http.end(); return nullptr; }
        body = _alloc(declared > 0 ? declared : CAP);
        if (!body) { http.end(); return nullptr; }
        WiFiClient* s = http.getStreamPtr();
        uint32_t lastData = millis();
        while ((http.connected() || s->available()) && millis() - lastData < 6000) {
            size_t avail = s->available();
            if (!avail) { delay(5); continue; }
            size_t cap = (declared > 0 ? (size_t)declared : CAP) - len;
            if (!cap) break;
            int got = s->readBytes(body + len, min(avail, cap));
            if (got > 0) { len += got; lastData = millis(); }
            if (declared > 0 && len >= (size_t)declared) break;
        }
        http.end();
        if (len < 8) {
            Log.printf("img[%s] body too short (%u B)\n", tag, (unsigned)len);
            free(body);
            return nullptr;
        }
        Log.printf("img[%s] %u bytes, magic %02X%02X\n", tag, (unsigned)len, body[0], body[1]);
    }

    uint8_t* px = _decodeScale(body, len, maxW, maxH, tag, outWp, outHp, bgColor);
    if (px && cacheKey && !fromDisk)  diskcache::save("img", cacheKey, body, len);
    if (!px && cacheKey && fromDisk)  diskcache::remove("img", cacheKey);   // corrupt/stale entry
    free(body);
    return px;
}

} // namespace imgdec
