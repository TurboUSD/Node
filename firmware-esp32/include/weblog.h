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

namespace weblog {
  static const size_t CAP = 16000;      // ring-buffer capacity (bytes)
  inline char   _buf[CAP];
  inline size_t _head    = 0;           // next write position
  inline bool   _wrapped = false;       // has the ring overwritten older data?

  inline void append(const uint8_t* data, size_t n) {
    for (size_t i = 0; i < n; i++) {
      _buf[_head++] = (char)data[i];
      if (_head >= CAP) { _head = 0; _wrapped = true; }
    }
  }

  // Copy the buffer in chronological order into `out`; returns bytes written.
  inline size_t snapshot(char* out, size_t outCap) {
    size_t n = _wrapped ? CAP : _head;
    if (n > outCap) n = outCap;
    size_t start = _wrapped ? _head : 0;
    for (size_t i = 0; i < n; i++) out[i] = _buf[(start + i) % CAP];
    return n;
  }
}

class WebLog : public Print {
public:
  size_t write(uint8_t c) override {
    Serial.write(c);
    weblog::append(&c, 1);
    return 1;
  }
  size_t write(const uint8_t* b, size_t n) override {
    Serial.write(b, n);
    weblog::append(b, n);
    return n;
  }
};

extern WebLog Log;
