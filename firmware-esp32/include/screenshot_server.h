// include/screenshot_server.h — pixel-perfect screenshots over WiFi.
//
// The display runs double-buffered (direct_mode), so flush_cb hands us a pointer
// to the whole live frame via setLiveFrame(). On capture we snapshot that FB once
// into a stable SHADOW buffer (460 KB PSRAM) so the slow ~150 ms WiFi stream can't
// tear when LVGL swaps framebuffers mid-transfer. This tiny HTTP server streams it
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
#include <ESPmDNS.h>
#include <lvgl.h>
#include <esp_heap_caps.h>
#include "config.h"

namespace screenshot {

static WebServer* s_srv    = nullptr;
static uint16_t*  s_shadow = nullptr;   // 480×480 RGB565 stable snapshot buffer
static const uint16_t* s_live = nullptr; // last full frame LVGL handed to the panel

inline bool started() { return s_srv != nullptr; }

// Called once from the display bring-up.
inline void ensureShadow() {
    if (s_shadow) return;
    s_shadow = (uint16_t*)heap_caps_malloc((size_t)LCD_H_RES * LCD_V_RES * 2,
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_shadow) Log.println("Screenshot: shadow fb alloc FAILED (no captures)");
}

// Called from the LVGL flush_cb with the framebuffer just made active. With
// direct_mode + double buffering every swap hands us the full live frame, so we
// just remember which PSRAM framebuffer is live; _sendBmp snapshots it on demand.
inline void setLiveFrame(const void* fb) { s_live = (const uint16_t*)fb; }

inline void _u16(uint8_t* p, uint16_t v)  { p[0] = v & 0xFF; p[1] = v >> 8; }
inline void _u32(uint8_t* p, uint32_t v)  { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = v >> 24; }

inline void _sendBmp() {
    // Snapshot the live framebuffer into the stable shadow buffer ONCE so the slow
    // WiFi stream below isn't torn by LVGL swapping framebuffers mid-transfer.
    if (s_live && s_shadow)
        memcpy(s_shadow, s_live, (size_t)LCD_H_RES * LCD_V_RES * 2);
    const void* fb = s_shadow ? (const void*)s_shadow : (const void*)s_live;
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

    // Force a proper filename+extension on save. Without this some browsers
    // (notably iOS Safari, which ignores the <a download> attribute) saved the
    // file as "shot.download" and the user had to rename it to .bmp by hand.
    // `?dl=1` → attachment (Save button); plain /shot.bmp stays inline so the
    // <img> preview still renders.
    if (s_srv->hasArg("dl"))
        s_srv->sendHeader("Content-Disposition", "attachment; filename=\"node-screen.bmp\"");
    else
        s_srv->sendHeader("Content-Disposition", "inline; filename=\"node-screen.bmp\"");
    s_srv->setContentLength(66 + pxBytes);
    s_srv->send(200, "image/bmp", "");
    WiFiClient c = s_srv->client();
    // Write the header robustly too (short writes are possible under load).
    for (size_t ho = 0; ho < sizeof(hd) && c.connected(); ) {
        size_t wr = c.write(hd + ho, sizeof(hd) - ho);
        if (wr > 0) ho += wr; else delay(2);
    }
    const uint8_t* p = (const uint8_t*)fb;
    // Streaming ~460 KB over WiFi blocks the main loop for ~150 ms, during which
    // lv_timer_handler() never runs — the UI froze and then jumped, which read
    // as a flicker every time a screenshot was served. Service LVGL every ~20 ms
    // MID-STREAM so the screen keeps refreshing. We're not inside lv_timer_handler
    // here (the loop already returned from it before screenshot::poll), so this
    // is a plain sequential call, not re-entrancy. Worst case the capture tears
    // one frame — already an accepted tradeoff for this debug aid.
    uint32_t lastLv = millis();
    uint32_t stallStart = millis();
    for (uint32_t off = 0; off < pxBytes; ) {
        if (!c.connected()) break;               // client truly gone
        size_t n = min((uint32_t)8192, pxBytes - off);
        size_t wr = c.write(p + off, n);
        if (wr > 0) {
            off += wr;
            stallStart = millis();
        } else {
            // TCP send buffer momentarily full: retry instead of aborting. A
            // premature break sent FEWER bytes than Content-Length, so Chrome
            // never finalized the file and left it as ".crdownload". Only give
            // up after a real ~8 s stall (the client hung).
            if (millis() - stallStart > 8000) break;
            delay(2);
        }
        if (millis() - lastLv >= 20) { lv_timer_handler(); lastLv = millis(); }
        delay(0);                                // feed the watchdog
    }
    // Explicitly flush + close the socket. The ESP32 WebServer otherwise leaves
    // the connection keep-alive, so even after ALL Content-Length bytes arrive
    // the browser keeps waiting and never finalizes the file — it sat forever as
    // ".crdownload". Closing here signals end-of-response so it renames to .bmp.
    c.flush();
    c.stop();
    Log.println("Screenshot served");
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
        "<a href='/shot.bmp?dl=1' download='node-screen.bmp'>Save BMP</a>"
        "<a href='/logs'>Logs &rarr;</a>"
        "</div>"
        "<p style='color:#8a8f9a;font-size:12px'>Swipe the device to the screen you want, then Refresh.</p>"
        "</body></html>");
}

// Raw log ring buffer as plain text (newest at the bottom). Independent of the
// USB serial console — this is how you read runtime logs when the RP2040 link
// on GPIO43/44 has taken over the UART console pins.
inline void _sendLogTxt() {
    char* buf = (char*)malloc(weblog::CAP + 1);
    if (!buf) { s_srv->send(500, "text/plain", "oom"); return; }
    size_t n = weblog::snapshot(buf, weblog::CAP);
    buf[n] = 0;
    // ?dl=1 → download the whole ring buffer as a file (the viewer's Download
    // button). Plain /log.txt stays inline (the live viewer fetches it).
    if (s_srv->hasArg("dl"))
        s_srv->sendHeader("Content-Disposition", "attachment; filename=\"turbousd-log.txt\"");
    s_srv->send(200, "text/plain; charset=utf-8", buf);
    free(buf);
}

// Auto-refreshing HTML log viewer with Pause / Jump-to-bottom / Screenshot
// controls. The live refresh used to wipe any text you were selecting the
// instant it ticked (every 1.5 s), so copying with the mouse was impossible.
// Now: (1) a Pause button freezes the refresh, and (2) the refresh auto-skips
// whenever there's an active text selection — so just selecting text holds the
// log still until you release, and the copy sticks.
inline void _sendLogPage() {
    s_srv->send(200, "text/html",
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>TurboUSD Node logs</title>"
        "<style>html,body{margin:0;background:#000;color:#d8f0d8;"
        "font-family:ui-monospace,Menlo,Consolas,monospace}"
        "#bar{position:sticky;top:0;background:#0a0a0a;border-bottom:1px solid #262626;"
        "padding:8px 10px;font-size:13px;color:#9096a1;display:flex;align-items:center;gap:8px;flex-wrap:wrap}"
        "#bar .t{font-weight:600}#bar .sp{flex:1}"
        "#bar button,#bar a{font:inherit;font-size:12px;font-weight:600;cursor:pointer;"
        "background:#161a17;color:#d8f0d8;border:1px solid #2a2f2b;border-radius:16px;"
        "padding:5px 12px;text-decoration:none;line-height:1}"
        "#bar button:hover,#bar a:hover{background:#20261f}"
        "#bar #shot{background:#3aff7a;color:#000;border-color:#3aff7a}"
        "#l{white-space:pre-wrap;word-break:break-word;font-size:12px;line-height:1.45;"
        "padding:10px 12px 60px}b{color:#3aff7a}</style></head><body>"
        "<div id='bar'><span class='t'>TurboUSD logs</span>"
        "<span style='color:#6a6a6e;font-size:11px'>ESP32 v" FIRMWARE_VERSION
        " &middot; RP2040 v" RP2040_FIRMWARE_VERSION "</span>"
        "<b id='st'>live</b>"
        "<span id='clk' style='color:#7fd0a0;font-size:12px;font-weight:600'></span>"
        "<span class='sp'></span>"
        "<button id='pz'>Pause</button>"
        "<button id='bt'>Bottom &darr;</button>"
        "<a href='/log.txt?dl=1' download='turbousd-log.txt'>&#11015; Download</a>"
        "<a id='shot' href='/'>&#128247; Screenshot</a></div>"
        "<div id='l'>loading…</div>"
        "<script>let follow=true,paused=false;"
        "const e=document.getElementById('l'),st=document.getElementById('st'),"
        "pz=document.getElementById('pz');"
        "addEventListener('scroll',()=>{follow=(innerHeight+scrollY)>=(document.body.scrollHeight-60)});"
        "function hasSel(){return window.getSelection&&String(window.getSelection()).length>0}"
        "async function u(){if(paused||hasSel())return;try{let r=await fetch('/log.txt',{cache:'no-store'});"
        "e.textContent=await r.text();if(follow)scrollTo(0,document.body.scrollHeight)}catch(x){}}"
        "pz.onclick=()=>{paused=!paused;pz.textContent=paused?'Resume':'Pause';"
        "st.textContent=paused?'paused':'live';st.style.color=paused?'#ffcf72':'#3aff7a'};"
        "document.getElementById('bt').onclick=()=>{follow=true;scrollTo(0,document.body.scrollHeight)};"
        "const clk=document.getElementById('clk');"
        "function tk(){clk.textContent=new Date().toLocaleTimeString()}tk();setInterval(tk,1000);"
        "u();setInterval(u,1500);</script></body></html>");
}

// Call once WiFi is connected (STA mode).
inline void init() {
    if (s_srv) return;
    s_srv = new WebServer(80);
    s_srv->on("/",         HTTP_GET, []() { _sendIndex();  });
    s_srv->on("/shot.bmp", HTTP_GET, []() { _sendBmp();    });
    s_srv->on("/logs",     HTTP_GET, []() { _sendLogPage(); });
    s_srv->on("/log.txt",  HTTP_GET, []() { _sendLogTxt();  });
    s_srv->begin();

    // mDNS: reach the device by name instead of hunting for its IP —
    // http://turbousd.local/logs works on the same WiFi (most phones/laptops
    // resolve .local; Android is the usual exception → use the IP there).
    if (MDNS.begin("turbousd")) {
        MDNS.addService("http", "tcp", 80);
        Log.println("mDNS: http://turbousd.local/ (logs: /logs)");
    }
    Log.printf("Screenshot server: http://%s/  (screen: /shot.bmp, logs: /logs)\n",
                  WiFi.localIP().toString().c_str());
}

// Call every loop() pass while connected.
inline void poll() { if (s_srv) s_srv->handleClient(); }

// Re-announce mDNS. The responder is bound to the network interface, so after a
// WiFi disconnect + reconnect it silently stops answering (turbousd.local dies)
// until it's begun again. The loop calls this on every fresh (re)connection so the
// fixed name ALWAYS resolves — this was the "worked on boot, dead after a WiFi blip"
// bug: mDNS was only started once, never re-registered on reconnect.
inline void restartMdns() {
    MDNS.end();
    if (MDNS.begin("turbousd")) {
        MDNS.addService("http", "tcp", 80);
        Log.println("mDNS: re-announced http://turbousd.local/ (logs: /logs)");
    }
}

// Re-bind BOTH the HTTP listener and mDNS after a WiFi (re)connection. The
// WebServer's listen socket is bound to the old netif exactly like the mDNS
// responder — after a drop/reconnect the stale pcb never accepts again and
// handleClient() polls a dead socket forever: the "/logs dead until reboot"
// failure. Called from the loop on every fresh connection AND whenever the
// GOT_IP event fired (events catch blips short enough to happen entirely
// inside one blocked loop pass, which the once-per-pass isConnected() poll
// can't see).
inline void restartNet() {
    if (s_srv) {
        s_srv->stop();
        s_srv->begin();
    }
    restartMdns();
    Log.printf("HTTP server re-bound: http://%s/ (logs: /logs)\n",
                  WiFi.localIP().toString().c_str());
}

} // namespace screenshot
