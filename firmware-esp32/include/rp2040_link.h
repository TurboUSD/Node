// include/rp2040_link.h — UART protocol between the ESP32-S3 (which knows
// what time it is and when the alarm should fire) and the RP2040 (which
// physically drives the buzzer). See firmware-rp2040/PROTOCOL.md for the
// byte-level spec this implements; keep both sides in sync if you change
// the framing here.

#pragma once
#include <HardwareSerial.h>
#include "config.h"

enum class Rp2040Command : uint8_t {
    PLAY_ALARM    = 0x01,  // legacy: plays at volume 2 (soft default)
    STOP_ALARM    = 0x02,
    PLAY_CHIME    = 0x03,  // short single beep, e.g. for UI feedback / button taps
    PING          = 0xF0,
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
        // Using HardwareSerial1, separate from the USB/debug Serial (Serial0).
        link.begin(RP2040_UART_BAUD, SERIAL_8N1, RP2040_UART_RX_PIN, RP2040_UART_TX_PIN);
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
            if (link.available() && link.read() == 0xAA) return true; // 0xAA = ack byte
        }
        return false;
    }

private:
    HardwareSerial link{1};

    void sendCommand(Rp2040Command cmd) {
        // Frame: [0x7E][cmd][checksum]. Checksum is a trivial XOR -- this
        // link is a few centimeters of trace on the same PCB, not a noisy
        // long-range channel, so we're guarding against framing bugs more
        // than line noise.
        uint8_t cmdByte = static_cast<uint8_t>(cmd);
        uint8_t checksum = 0x7E ^ cmdByte;
        link.write(0x7E);
        link.write(cmdByte);
        link.write(checksum);
    }
};

extern Rp2040Link rp2040Link;
