// include/rp2040_link.h — UART protocol between the ESP32-S3 (which knows
// what time it is and when the alarm should fire) and the RP2040 (which
// physically drives the buzzer and reads the ambient temp/humidity sensor).
// See firmware-rp2040/PROTOCOL.md for the byte-level spec this implements;
// keep both sides in sync if you change the framing here.

#pragma once
#include <Arduino.h>
#include "config.h"
#include "driver/uart.h"               // raw IDF driver — see begin()
#include "driver/gpio.h"
#include "soc/io_mux_reg.h"            // PIN_INPUT_ENABLE — pad level read-back diagnostic

enum class Rp2040Command : uint8_t {
    PLAY_ALARM    = 0x01,  // legacy: plays at volume 2 (soft default)
    STOP_ALARM    = 0x02,
    PLAY_CHIME    = 0x03,  // short single beep, e.g. for UI feedback / button taps
    PING          = 0xF0,
    READ_TH       = 0x20,  // request temp/humidity; reply is a 7-byte frame, not an ACK
    // Volume-aware alarm commands — RP2040 firmware 1.2+
    PLAY_ALARM_V1 = 0x11,
    PLAY_ALARM_V2 = 0x12,
    PLAY_ALARM_V3 = 0x13,
    PLAY_ALARM_V4 = 0x14,
    PLAY_ALARM_V5 = 0x15,
};

class Rp2040Link {
public:
    void begin() {
        // The link kept coming up dead through Arduino's HardwareSerial (the
        // RP2040's byte-activity diagnostic showed ZERO bytes ever arriving),
        // so this now mirrors Seeed's own esp32_rp2040_comm example 1:1 at
        // the IDF level — uart_driver_install + uart_param_config +
        // uart_set_pin on UART2, TX 19 / RX 20 — and LOGS every step, so if
        // anything rejects the pins (19/20 double as the S3's USB pads) it
        // shows up in the serial monitor as "RP-link: ..." lines.
        // NOTE: the link now lives on GPIO 43/44 (the S3's U0TXD/U0RXD pads,
        // hard-wired to the RP2040 on this board). All the USB-PHY-detach
        // gymnastics from the 19/20 era are gone — 19/20 ARE the USB pads and
        // carry the console/flasher; they were never the RP2040 link.
        gpio_hold_dis((gpio_num_t)RP2040_UART_TX_PIN);    // release any sleep/hold latch
        gpio_hold_dis((gpio_num_t)RP2040_UART_RX_PIN);
        gpio_reset_pin((gpio_num_t)RP2040_UART_TX_PIN);   // detach any previous owner
        gpio_reset_pin((gpio_num_t)RP2040_UART_RX_PIN);   // (UART0 console mux, etc.)

        uart_config_t cfg = {};
        cfg.baud_rate  = RP2040_UART_BAUD;
        cfg.data_bits  = UART_DATA_8_BITS;
        cfg.parity     = UART_PARITY_DISABLE;
        cfg.stop_bits  = UART_STOP_BITS_1;
        cfg.flow_ctrl  = UART_HW_FLOWCTRL_DISABLE;
        cfg.source_clk = UART_SCLK_APB;   // IDF 4.4 (Arduino 2.0.x) has no UART_SCLK_DEFAULT

        _e1 = uart_driver_install(LINK_UART, 512, 512, 0, nullptr, 0);
        _e2 = uart_param_config(LINK_UART, &cfg);
        _e3 = uart_set_pin(LINK_UART, RP2040_UART_TX_PIN, RP2040_UART_RX_PIN,
                           UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        // Enable the INPUT BUFFER on both pads (doesn't disturb the matrix
        // routing) so printStatus() can read back the physical line levels.
        // UART idles HIGH: tx_pad=1 → our pad is really driving; rx_pad=1 →
        // the RP2040's TX is really reaching us. Any 0 = that line is dead
        // at the electrical level and no firmware can fix it.
        PIN_INPUT_ENABLE(GPIO_PIN_MUX_REG[RP2040_UART_TX_PIN]);
        PIN_INPUT_ENABLE(GPIO_PIN_MUX_REG[RP2040_UART_RX_PIN]);
        printStatus();
        _ok = (_e1 == ESP_OK && _e2 == ESP_OK && _e3 == ESP_OK);
        if (!_ok) Log.println("RP-link: INIT FAILED — alarm commands cannot reach the RP2040");
    }

    // Re-printable at any time — serial monitors usually attach AFTER boot,
    // so the one-shot init line kept getting missed. Includes heap health:
    // `heap` = free internal RAM, `maxblk` = largest contiguous block (a TLS
    // handshake needs ~45 KB contiguous — if maxblk sits below that, that's
    // the whole "-32512 SSL memory" story in one number).
    void printStatus() {
        uint32_t baud = 0;
        uart_get_baudrate(LINK_UART, &baud);
        Log.printf("RP-link: install=%d config=%d set_pin=%d (TX=%d RX=%d, UART%d, %lu baud) | "
                      "tx_pad=%d rx_pad=%d | heap=%u maxblk=%u\n",
                      (int)_e1, (int)_e2, (int)_e3, RP2040_UART_TX_PIN, RP2040_UART_RX_PIN, (int)LINK_UART,
                      (unsigned long)baud,
                      gpio_get_level((gpio_num_t)RP2040_UART_TX_PIN),
                      gpio_get_level((gpio_num_t)RP2040_UART_RX_PIN),
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }

    // volume: 1 (whisper) – 5 (max). Default 2 matches the soft-default in
    // legacy PLAY_ALARM (0x01). Values outside 1–5 are clamped to the range.
    void playAlarm(uint8_t volume = 2) {
        volume = constrain(volume, 1, 5);
        static const Rp2040Command VOL_CMD[5] = {
            Rp2040Command::PLAY_ALARM_V1,
            Rp2040Command::PLAY_ALARM_V2,
            Rp2040Command::PLAY_ALARM_V3,
            Rp2040Command::PLAY_ALARM_V4,
            Rp2040Command::PLAY_ALARM_V5,
        };
        Log.printf("RP-link: playAlarm(vol=%u) → cmd=0x%02X (+legacy 0x01) uart_ok=%d\n",
                      volume, (unsigned)VOL_CMD[volume - 1], (int)_ok);
        // Send the LEGACY PLAY_ALARM (0x01) FIRST, then the volume-aware command.
        // An OLD RP2040 firmware (no 0x11–0x15 support, no heartbeat/ACK — exactly
        // what the /logs "ping FAILED / no heartbeat" symptom shows) only knows
        // 0x01, so this makes it buzz anyway. New firmware runs 0x01 (vol 2) then
        // the Vn command overrides it, so the requested volume still wins.
        sendCommand(Rp2040Command::PLAY_ALARM);
        sendCommand(VOL_CMD[volume - 1]);
    }
    void stopAlarm()  { sendCommand(Rp2040Command::STOP_ALARM); }
    void playChime()  { sendCommand(Rp2040Command::PLAY_CHIME); }

    // RP2040 firmware version reported over the link via its version frame.
    // Empty string until the first frame arrives (the caller can fall back to
    // the paired build constant in config.h).
    String rpVersion() const { return String(_rpVersion); }

    // Call every main-loop pass. Drains and HEX-logs anything the RP2040
    // sends spontaneously (its new firmware emits a 0x7E 0xEE hello every 5 s
    // for the first 2 minutes). This splits the dead-link mystery in half
    // from the ESP32's serial log alone:
    //   "RP2040 heartbeat RECEIVED"  → RP→ESP wire OK + new RP firmware ✓
    //   garbage bytes logged         → wire OK but baud/framing mismatch
    //   nothing ever                 → RP→ESP wire dead OR old RP firmware
    void pollRx() {
        if (!_ok) return;
        uint8_t buf[16];
        int n = uart_read_bytes(LINK_UART, buf, sizeof(buf), 0);
        if (n <= 0) return;
        bool hello = false;
        for (int i = 0; i + 1 < n; i++) {
            if (buf[i] == 0x7E && buf[i + 1] == 0xEE) hello = true;
            // Version frame [0x7E][0xEF][maj][min][pat]: record the RP2040's real
            // firmware version so the UI can show it (a version frame also proves
            // the link is alive, same as the plain hello).
            if (buf[i] == 0x7E && buf[i + 1] == 0xEF && i + 4 < n) {
                snprintf(_rpVersion, sizeof(_rpVersion), "%u.%u.%u",
                         buf[i + 2], buf[i + 3], buf[i + 4]);
                hello = true;
            }
        }
        static uint32_t lastLogAt = 0;
        if (hello) {
            // Log the "link is alive" line ONCE, not on every 5 s hello (the RP2040
            // emits one for the first 2 minutes → the log used to be flooded).
            // "firmware confirmed" here only ever meant "the RP is running the new
            // firmware that emits heartbeats", not that an update was found.
            if (!_helloLogged) {
                _helloLogged = true;
                if (_rpVersion[0])
                    Log.printf("RP-link: RP2040 link alive (firmware v%s)\n", _rpVersion);
                else
                    Log.println("RP-link: RP2040 link alive");
            }
            return;
        }
        if (millis() - lastLogAt > 2000) {
            lastLogAt = millis();
            char hex[3 * sizeof(buf) + 1] = {};
            for (int i = 0; i < n; i++) snprintf(hex + i * 3, 4, "%02X ", buf[i]);
            Log.printf("RP-link: RX %d bytes: %s\n", n, hex);
        }
    }

    // Call periodically (e.g. once a minute) so a firmware update or crash
    // on the RP2040 side gets noticed instead of silently never buzzing.
    bool ping(uint32_t timeoutMs = 200) {
        sendCommand(Rp2040Command::PING);
        uint32_t start = millis();
        while (millis() - start < timeoutMs) {
            int b = _readByte();
            if (b == 0xAA) return true; // 0xAA = ack byte
        }
        return false;
    }

    // Request the latest ambient reading from the RP2040. Returns true and
    // fills tempC + humidityPct on success; false on timeout, bad checksum, or
    // the RP2040's "no sensor" sentinel (e.g. no AHT20 plugged in). The caller
    // should keep showing the previous value (or "--") when this returns false.
    // See PROTOCOL.md → "Sensor response" for the 7-byte frame layout.
    bool readTempHumidity(float& tempC, int& humidityPct, uint32_t timeoutMs = 250) {
        // NOTE: we deliberately do NOT uart_flush_input() here anymore — that
        // ran every ~10 s and could swallow the RP2040's 0x7E 0xEE heartbeat
        // before pollRx() logged it, masking a live link. The frame parser
        // below already resyncs on the 0x7E start byte, so stale bytes are
        // skipped safely without a flush.
        sendCommand(Rp2040Command::READ_TH);

        uint8_t frame[7];
        uint8_t idx = 0;
        uint32_t start = millis();
        while (millis() - start < timeoutMs) {
            int rb = _readByte();
            if (rb < 0) continue;
            uint8_t b = (uint8_t)rb;
            if (idx == 0 && b != 0x7E) continue;            // wait for start byte
            frame[idx++] = b;
            if (idx < sizeof(frame)) continue;

            // Full frame received.
            if (frame[1] != static_cast<uint8_t>(Rp2040Command::READ_TH)) return false;
            uint8_t cs = frame[0] ^ frame[1] ^ frame[2] ^ frame[3] ^ frame[4] ^ frame[5];
            if (frame[6] != cs) return false;               // corrupted frame

            int16_t  tCenti = (int16_t)(((uint16_t)frame[2] << 8) | frame[3]);
            uint16_t hCenti = (uint16_t)(((uint16_t)frame[4] << 8) | frame[5]);
            if (tCenti == (int16_t)0x8000) return false;    // RP2040 has no valid reading

            tempC = tCenti / 100.0f;
            humidityPct = (int)((hCenti + 50) / 100);       // centi-% → % (rounded)
            return true;
        }
        return false; // timed out
    }

private:
    // UART port 2 — matches Seeed's own esp32_rp2040_comm example exactly
    // (ESP32_COMM_PORT_NUM = 2, TXD 19, RXD 20).
    static const uart_port_t LINK_UART = UART_NUM_2;
    bool _ok = false;
    esp_err_t _e1 = ESP_FAIL, _e2 = ESP_FAIL, _e3 = ESP_FAIL;   // init results, reprintable
    char _rpVersion[16] = {};   // RP2040 firmware version from its version frame ("" until received)
    bool _helloLogged = false;  // "link alive" logged once (the 5 s hellos used to spam)

    // Non-blocking single-byte read; -1 when nothing is waiting.
    int _readByte() {
        if (!_ok) return -1;
        uint8_t b;
        int n = uart_read_bytes(LINK_UART, &b, 1, 0);
        return n == 1 ? (int)b : -1;
    }

    void sendCommand(Rp2040Command cmd) {
        // Frame: [0x7E][cmd][checksum]. Checksum is a trivial XOR -- this
        // link is a few centimeters of trace on the same PCB, not a noisy
        // long-range channel, so we're guarding against framing bugs more
        // than line noise.
        if (!_ok) { Log.println("RP-link: send skipped (uart init failed)"); return; }
        uint8_t f[3] = { 0x7E, static_cast<uint8_t>(cmd),
                         (uint8_t)(0x7E ^ static_cast<uint8_t>(cmd)) };
        uart_write_bytes(LINK_UART, (const char*)f, sizeof(f));
        uart_wait_tx_done(LINK_UART, pdMS_TO_TICKS(50));   // actually push it onto the wire
    }
};

extern Rp2040Link rp2040Link;
