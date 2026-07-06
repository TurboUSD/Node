// include/screenshot_server.h — pixel-perfect screenshots over WiFi.
//
// The Arduino core's IDF predates esp_lcd_rgb_panel_get_frame_buffer(), so
// we can't read the panel's own PSRAM framebuffer. Instead the display
// flush_cb mirrors every blitted region into a SHADOW framebuffer owned
// here (460 KB PSRAM, allocated at display init) — at any moment it holds
// the exact composited frame on the glass. This tiny HTTP server streams it
// as a 16-bit BMP:
//
//   http://<device-ip>/          — preview page with a Save button
//   http://<device-ip>/shot.bmp  — the raw 480×480 screenshot
//
// The device prints the URL on serial after WiFi connects, and it only runs
// in STA mode (the provisioning portal owns port 80 in AP mode, and the
// device restarts between the two states, so they never coexist).
//
// Worst case a capture is mid-frame (LVGL blitting while we read) — a partial
// tear on one region. Reload and it's gone; no locking needed for a debug aid.

#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include <lvgl.h>
#include <esp_heap_caps.h>
#include "config.h"

namespace screenshot {

static WebServer* s_srv    = nullptr;
static uint16_t*  s_shadow = nullptr;   // 480×480 RGB565 mirror of the panel

inline bool started() { return s_srv != nullptr; }

// Called once from the display bring-up.
inline void ensureShadow() {
    if (s_shadow) return;
    s_shadow = (uint16_t*)heap_caps_malloc((size_t)LCD_H_RES * LCD_V_RES * 2,
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_shadow) Serial.println("Screenshot: shadow fb alloc FAILED (no captures)");
}

// Called from the LVGL flush_cb with every region blitted to the panel.
inline void mirror(const lv_area_t* area, const lv_color_t* px) {
    if (!s_shadow) return;
    int w = area->x2 - area->x1 + 1;
    for (int y = area->y1; y <= area->y2; y++)
        memcpy(&s_shadow[(size_t)y * LCD_H_RES + area->x1],
               (const uint16_t*)px + (size_t)(y - area->y1) * w,
               (size_t)w * 2);
}

inline void _u16(uint8_t* p, uint16_t v)  { p[0] = v & 0xFF; p[1] = v >> 8; }
inline void _u32(uint8_t* p, uint32_t v)  { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = v >> 24; }

inline void _sendBmp() {
    const void* fb = s_shadow;
    if (!fb) {
        s_srv->send(500, "text/plain", "framebuffer unavailable");
        return;
    }
    const uint32_t W = LCD_H_RES, H = LCD_V_RES;
    const uint32_t pxBytes = W * H * 2;          // RGB565, row stride 960 (already /4)

    // BMP: 14-byte file header + 40-byte BITMAPINFOHEADER + 3 channel masks.
    // 16 bpp BI_BITFIELDS with the RGB565 masks; NEGATIVE height = top-down,
    // which matches the framebuffer layout so we can stream it verbatim.
    uint8_t hd[66] = {};
    hd[0] = 'B'; hd[1] = 'M';
    _u32(hd + 2,  66 + pxBytes);                 // file size
    _u32(hd + 10, 66);                           // pixel data offset
    _u32(hd + 14, 40);                           // info header size
    _u32(hd + 18, W);
    _u32(hd + 22, (uint32_t)(-(int32_t)H));      // top-down
    _u16(hd + 26, 1);                            // planes
    _u16(hd + 28, 16);                           // bpp
    _u32(hd + 30, 3);                            // BI_BITFIELDS
    _u32(hd + 34, pxBytes);
    _u32(hd + 38, 2835); _u32(hd + 42, 2835);    // 72 dpi
    _u32(hd + 54, 0xF800);                       // R mask
    _u32(hd + 58, 0x07E0);                       // G mask
    _u32(hd + 62, 0x001F);                       // B mask

    s_srv->setContentLength(66 + pxBytes);
    s_srv->send(200, "image/bmp", "");
    WiFiClient c = s_srv->client();
    c.write(hd, sizeof(hd));
    const uint8_t* p = (const uint8_t*)fb;
    for (uint32_t off = 0; off < pxBytes; ) {
        size_t n = min((uint32_t)8192, pxBytes - off);
        size_t wr = c.write(p + off, n);
        if (wr == 0) break;                      // client gone
        off += wr;
        delay(0);                                // feed the watchdog
    }
    Serial.println("Screenshot served");
}

inline void _sendIndex() {
    s_srv->send(200, "text/html",
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>TurboUSD Node screenshot</title>"
        "<style>body{background:#000;color:#e8e8ea;font-family:system-ui,sans-serif;"
        "text-align:center;padding:18px}img{width:min(480px,96vw);border-radius:14px;"
        "border:1px solid #262626}a,button{display:inline-block;margin:12px 6px;padding:10px 20px;"
        "background:#3aff7a;color:#000;border:none;border-radius:20px;font-weight:700;"
        "font-size:14px;text-decoration:none;cursor:pointer}</style></head><body>"
        "<h3>Live screen</h3>"
        "<img id='s' src='/shot.bmp'>"
        "<div>"
        "<button onclick=\"document.getElementById('s').src='/shot.bmp?'+Date.now()\">Refresh</button>"
        "<a href='/shot.bmp' download='node-screen.bmp'>Save BMP</a>"
        "</div>"
        "<p style='color:#6e7280;font-size:12px'>Swipe the device to the screen you want, then Refresh.</p>"
        "</body></html>");
}

// Call once WiFi is connected (STA mode).
inline void init() {
    if (s_srv) return;
    s_srv = new WebServer(80);
    s_srv->on("/",         HTTP_GET, []() { _sendIndex(); });
    s_srv->on("/shot.bmp", HTTP_GET, []() { _sendBmp();   });
    s_srv->begin();
    Serial.printf("Screenshot server: http://%s/  (raw: /shot.bmp)\n",
                  WiFi.localIP().toString().c_str());
}

// Call every loop() pass while connected.
inline void poll() { if (s_srv) s_srv->handleClient(); }

} // namespace screenshot
