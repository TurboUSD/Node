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
#include "soc/usb_serial_jtag_reg.h"   // USB pad release — see begin()

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
        REG_CLR_BIT(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
        gpio_reset_pin((gpio_num_t)RP2040_UART_TX_PIN);   // detach any previous owner
        gpio_reset_pin((gpio_num_t)RP2040_UART_RX_PIN);   // (USB PHY, other muxes)

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
        printStatus();
        _ok = (_e1 == ESP_OK && _e2 == ESP_OK && _e3 == ESP_OK);
        if (!_ok) Serial.println("RP-link: INIT FAILED — alarm commands cannot reach the RP2040");
    }

    // Re-printable at any time — serial monitors usually attach AFTER boot,
    // so the one-shot init line kept getting missed. Includes heap health:
    // `heap` = free internal RAM, `maxblk` = largest contiguous block (a TLS
    // handshake needs ~45 KB contiguous — if maxblk sits below that, that's
    // the whole "-32512 SSL memory" story in one number).
    void printStatus() {
        Serial.printf("RP-link: install=%d config=%d set_pin=%d (TX=%d RX=%d, UART%d) | heap=%u maxblk=%u\n",
                      (int)_e1, (int)_e2, (int)_e3, RP2040_UART_TX_PIN, RP2040_UART_RX_PIN, (int)LINK_UART,
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
        sendCommand(VOL_CMD[volume - 1]);
    }
    void stopAlarm()  { sendCommand(Rp2040Command::STOP_ALARM); }
    void playChime()  { sendCommand(Rp2040Command::PLAY_CHIME); }

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
        if (_ok) uart_flush_input(LINK_UART);   // drain stale bytes so we don't desync
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
        if (!_ok) { Serial.println("RP-link: send skipped (uart init failed)"); return; }
        uint8_t f[3] = { 0x7E, static_cast<uint8_t>(cmd),
                         (uint8_t)(0x7E ^ static_cast<uint8_t>(cmd)) };
        uart_write_bytes(LINK_UART, (const char*)f, sizeof(f));
        uart_wait_tx_done(LINK_UART, pdMS_TO_TICKS(50));   // actually push it onto the wire
    }
};

extern Rp2040Link rp2040Link;
