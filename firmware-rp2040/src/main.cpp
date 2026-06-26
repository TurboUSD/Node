// src/main.cpp — TurboUSD Node firmware, RP2040 side.
// Drives the buzzer on command and reports the Grove AHT20 temp/humidity
// sensor back to the ESP32-S3. See PROTOCOL.md for the UART format.

#include <Arduino.h>
#include <Wire.h>
#include "AHT20.h"
#include "board_pins.h"

enum class Command : uint8_t {
    PLAY_ALARM   = 0x01,
    STOP_ALARM   = 0x02,
    PLAY_CHIME   = 0x03,
    PING         = 0xF0,
    READ_TH      = 0x20,  // reply is a 7-byte sensor frame, not an ACK (see PROTOCOL.md)
    // Volume-aware alarm commands (backward-compatible — old ESP32 firmware
    // never sends these, so old builds keep using PLAY_ALARM at volume 2).
    PLAY_ALARM_V1 = 0x11,  // volume 1 — whisper
    PLAY_ALARM_V2 = 0x12,  // volume 2 — soft (default)
    PLAY_ALARM_V3 = 0x13,  // volume 3 — medium
    PLAY_ALARM_V4 = 0x14,  // volume 4 — loud
    PLAY_ALARM_V5 = 0x15,  // volume 5 — max
};

bool alarmActive = false;
uint32_t alarmStartedAt = 0;
const uint32_t ALARM_MAX_DURATION_MS = 5UL * 60UL * 1000UL; // auto-stop after 5 min even with no STOP_ALARM, so a dropped command can't buzz forever

// Simple two-tone alarm pattern, distinct from a single flat beep so it
// reads as "alarm" rather than "low battery warning" or similar. Frequencies
// chosen to be clearly audible through a small buzzer without being
// unpleasantly harsh.
const uint16_t ALARM_TONE_A_HZ = 1800;
const uint16_t ALARM_TONE_B_HZ = 1400;
const uint16_t TONE_DURATION_MS = 220;

uint32_t lastToneToggleAt = 0;
bool playingToneA = true;

// Volume control via PWM duty cycle on the passive buzzer.
// analogWrite() range is 0–255 on the RP2040 Arduino core.
// Beyond 50% duty cycle (~128) a passive buzzer gets no louder, so 128 is max.
// Values are tuned so level 2 ("soft default") is clearly audible without
// being startling, while level 5 is the full 50% square-wave maximum.
uint8_t currentVolume = 2; // default; overridden by PLAY_ALARM_Vn commands
const uint8_t VOLUME_DUTY[5] = { 8, 25, 60, 100, 128 };

void startTone(uint16_t freqHz) {
    analogWriteFreq(freqHz);
    analogWrite(BUZZER_PIN, VOLUME_DUTY[currentVolume - 1]);
}

void stopTone() {
    analogWrite(BUZZER_PIN, 0);
}

void handleAlarmPattern() {
    if (!alarmActive) return;

    if (millis() - alarmStartedAt > ALARM_MAX_DURATION_MS) {
        alarmActive = false;
        stopTone();
        return;
    }

    if (millis() - lastToneToggleAt > TONE_DURATION_MS) {
        lastToneToggleAt = millis();
        playingToneA = !playingToneA;
        startTone(playingToneA ? ALARM_TONE_A_HZ : ALARM_TONE_B_HZ);
    }
}

void playChimeBlocking() {
    // Short and blocking is fine here -- a UI-feedback chime is meant to be
    // near-instant, and nothing else needs this chip's attention mid-chime.
    analogWriteFreq(2200);
    analogWrite(BUZZER_PIN, VOLUME_DUTY[currentVolume - 1]);
    delay(80);
    analogWrite(BUZZER_PIN, 0);
    delay(20);
}

void startAlarm(uint8_t volume) {
    currentVolume = constrain(volume, 1, 5);
    alarmActive = true;
    alarmStartedAt = millis();
    lastToneToggleAt = 0;
}

void sendAck() {
    Serial1.write(0xAA);
}

// ── Ambient sensor (Grove AHT20 on the RP2040 I2C port) ─────────────────────
// Read on a slow cadence into a cache so answering READ_TH is instant and the
// ~80 ms AHT20 conversion never stalls the alarm-tone loop. The S3 polls this.
AHT20 aht;
const int16_t  TEMP_SENTINEL_NO_DATA = (int16_t)0x8000; // -32768 → "no valid reading"
int16_t  cachedTempCenti = TEMP_SENTINEL_NO_DATA;        // centi-°C, signed
uint16_t cachedHumCenti  = 0;                            // centi-% RH, unsigned
uint32_t lastSensorReadAt = 0;
const uint32_t SENSOR_READ_INTERVAL_MS = 2000;

// Pull a fresh reading from the AHT20 into the cache. AHT20::getSensor returns
// non-zero on success and reports humidity as a 0–1 fraction, temp in °C.
void readSensorInto() {
    float humiFrac, tempC;
    int ok = aht.getSensor(&humiFrac, &tempC);
    if (ok) {
        cachedTempCenti = (int16_t)lroundf(tempC * 100.0f);
        long h = lroundf(humiFrac * 100.0f * 100.0f); // fraction → % → centi-%
        cachedHumCenti  = (uint16_t)constrain(h, 0, 10000);
    } else {
        cachedTempCenti = TEMP_SENTINEL_NO_DATA;       // surfaces as "--" on the S3
        cachedHumCenti  = 0;
    }
}

// Reply to READ_TH: [0x7E][0x20][tHi][tLo][hHi][hLo][checksum]. See PROTOCOL.md.
void sendSensorFrame() {
    uint8_t f[7];
    f[0] = 0x7E;
    f[1] = static_cast<uint8_t>(Command::READ_TH);
    f[2] = (uint8_t)((uint16_t)cachedTempCenti >> 8);
    f[3] = (uint8_t)((uint16_t)cachedTempCenti & 0xFF);
    f[4] = (uint8_t)(cachedHumCenti >> 8);
    f[5] = (uint8_t)(cachedHumCenti & 0xFF);
    f[6] = f[0] ^ f[1] ^ f[2] ^ f[3] ^ f[4] ^ f[5];
    Serial1.write(f, sizeof(f));
}

void processCommand(uint8_t cmd) {
    switch (static_cast<Command>(cmd)) {
        case Command::PLAY_ALARM:
            startAlarm(2); // legacy command always plays at volume 2 (soft default)
            sendAck();
            break;
        case Command::PLAY_ALARM_V1: startAlarm(1); sendAck(); break;
        case Command::PLAY_ALARM_V2: startAlarm(2); sendAck(); break;
        case Command::PLAY_ALARM_V3: startAlarm(3); sendAck(); break;
        case Command::PLAY_ALARM_V4: startAlarm(4); sendAck(); break;
        case Command::PLAY_ALARM_V5: startAlarm(5); sendAck(); break;
        case Command::STOP_ALARM:
            alarmActive = false;
            stopTone();
            sendAck();
            break;
        case Command::PLAY_CHIME:
            playChimeBlocking();
            sendAck();
            break;
        case Command::PING:
            sendAck();
            break;
        case Command::READ_TH:
            // Answer from the cache with a data frame instead of an ACK.
            sendSensorFrame();
            break;
        default:
            // Unknown command byte -- ignore rather than ack, so the S3
            // side's ping()-style waits correctly time out instead of
            // mistaking garbage for a healthy link.
            break;
    }
}

void setup() {
    pinMode(BUZZER_PIN, OUTPUT);
    Serial1.setRX(UART_FROM_S3_RX);
    Serial1.setTX(UART_TO_S3_TX);
    Serial1.begin(UART_BAUD);

    // Grove I2C bus for the AHT20 temp/humidity sensor.
    Wire.setSDA(GROVE_I2C_SDA);
    Wire.setSCL(GROVE_I2C_SCL);
    Wire.begin();
    aht.begin();
}

void loop() {
    // Frame parser: [0x7E][cmd][checksum]. Reads byte-by-byte rather than
    // blocking on Serial1.readBytes() so handleAlarmPattern() keeps running
    // (and the buzzer keeps alternating tones) even if bytes arrive slowly.
    static uint8_t frame[3];
    static uint8_t frameIndex = 0;

    while (Serial1.available()) {
        uint8_t b = Serial1.read();
        if (frameIndex == 0 && b != 0x7E) continue; // resync: wait for a start byte
        frame[frameIndex++] = b;

        if (frameIndex == 3) {
            uint8_t expectedChecksum = frame[0] ^ frame[1];
            if (frame[2] == expectedChecksum) {
                processCommand(frame[1]);
            }
            frameIndex = 0;
        }
    }

    handleAlarmPattern();

    // Refresh the cached sensor reading on a slow cadence. Skip while the alarm
    // is sounding so the ~80 ms AHT20 conversion can't glitch the tone pattern;
    // a slightly stale temperature during a 5-minute alarm is harmless.
    if (!alarmActive && (millis() - lastSensorReadAt > SENSOR_READ_INTERVAL_MS)) {
        lastSensorReadAt = millis();
        readSensorInto();
    }
}
