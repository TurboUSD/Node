// include/weblog.h — tee for the serial console.
//
// The ESP32-S3 USB CDC console (esp-web-tools "Logs & Console") often shows only
// the ROM/boot lines and then goes quiet on this board, so runtime logs (image
// downloads, alarm, NFT resolve…) were unreadable. `Log` writes every line to
// BOTH the real Serial AND a circular RAM buffer that the on-device HTTP server
// exposes at http://<device-ip>/logs — no USB needed, works over WiFi like the
// screenshot page.
//
// Usage: the whole firmware calls Log.printf/println/print instead of Serial.*
// (Serial.begin() stays as-is). `Log` forwards to the real `Serial`, so nothing
// is lost on USB either.

#pragma once
#include <Arduino.h>
#include <time.h>
#include <esp_heap_caps.h>   // PSRAM ring buffer

namespace weblog {
  static const size_t CAP = 16000;      // ring-buffer capacity (bytes)
  // Ring lives in PSRAM (lazy alloc on first log line — the PSRAM heap is
  // registered before setup() runs). It used to be a 16 KB internal-BSS
  // array: internal RAM the TLS handshakes badly needed. NOTE: never call
  // Log from an ISR — writing PSRAM from an IRAM ISR during a
  // flash-cache-off window would crash (no ISR logs today).
  inline char*  _buf     = nullptr;
  inline size_t _head    = 0;           // next write position
  inline bool   _wrapped = false;       // has the ring overwritten older data?

  inline bool _ensureBuf() {
    if (_buf) return true;
    _buf = (char*)heap_caps_malloc(CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!_buf) _buf = (char*)malloc(CAP);   // fallback: internal (same as the old BSS)
    return _buf != nullptr;
  }

  inline void append(const uint8_t* data, size_t n) {
    if (!_ensureBuf()) return;            // OOM → serial-only logging
    for (size_t i = 0; i < n; i++) {
      _buf[_head++] = (char)data[i];
      if (_head >= CAP) { _head = 0; _wrapped = true; }
    }
  }

  // Copy the buffer in chronological order into `out`; returns bytes written.
  inline size_t snapshot(char* out, size_t outCap) {
    if (!_buf) return 0;
    size_t n = _wrapped ? CAP : _head;
    if (n > outCap) n = outCap;
    size_t start = _wrapped ? _head : 0;
    for (size_t i = 0; i < n; i++) out[i] = _buf[(start + i) % CAP];
    return n;
  }
}

class WebLog : public Print {
  bool _atLineStart = true;   // next byte begins a new line → prepend a timestamp

  // Emit "[HH:MM:SS] " (local wall-clock once NTP has synced) or "[+<secs>s] "
  // uptime before the first sync, so every log line is timestamped to the second
  // for diagnosing when things happened.
  void _emitPrefix() {
    char ts[20];
    time_t now = time(nullptr);
    struct tm t;
    if (now > 1600000000 && localtime_r(&now, &t)) {
      snprintf(ts, sizeof(ts), "[%02d:%02d:%02d] ", t.tm_hour, t.tm_min, t.tm_sec);
    } else {
      snprintf(ts, sizeof(ts), "[+%lus] ", (unsigned long)(millis() / 1000));
    }
    size_t tl = strlen(ts);
    Serial.write((const uint8_t*)ts, tl);
    weblog::append((const uint8_t*)ts, tl);
  }

  inline void _put(uint8_t c) {
    if (_atLineStart && c != '\n' && c != '\r') _emitPrefix();
    _atLineStart = (c == '\n');
    Serial.write(c);
    weblog::append(&c, 1);
  }

public:
  size_t write(uint8_t c) override { _put(c); return 1; }
  size_t write(const uint8_t* b, size_t n) override {
    for (size_t i = 0; i < n; i++) _put(b[i]);
    return n;
  }
};

extern WebLog Log;
