// src/main.cpp — TurboUSD Node firmware, RP2040 side.
// Drives the buzzer on command and reports an optional Grove AHT20 temp/humidity
// sensor back to the ESP32-S3. NOTE: the base SenseCAP Indicator D1/D1L have no
// built-in environmental sensor, so temp/humidity read as "--" unless a Grove
// AHT20 is plugged in. See PROTOCOL.md for the UART format.

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

// IMPORTANT — buzzer drive method, settled against Seeed's OFFICIAL example
// (SenseCAP_Indicator_RP2040/examples/buzzer/buzzer.ino, verified 2026-07-05):
//
//     #define Buzzer 19
//     analogWrite(Buzzer, 127);   // no analogWriteFreq() call at all
//
// On the earlephilhower arduino-pico core (which this project uses) that
// means PWM at the DEFAULT 1 kHz with 50% duty — and that is the drive this
// hardware is known to sound with. So:
//   • We do NOT call analogWriteFreq() anywhere (field evidence from a
//     previous build: a boot beep using analogWriteFreq(2000) stayed silent
//     on the device, while Seeed's freq-less method is their own reference).
//   • The MLT-8530 is a passive magnetic buzzer: loudness peaks at 50% duty
//     (127/255) and falls off above it — 255 = continuous DC = totally
//     SILENT. The previous firmware mapped volume 5 → duty 255, so MAX
//     volume produced no sound at all. Duty is therefore capped at 127,
//     with volume 5 = exactly Seeed's reference value.
const uint16_t BEEP_ON_MS  = 250;   // buzzer-on time per beep
const uint16_t BEEP_OFF_MS = 180;   // silence between beeps

uint32_t lastToggleAt = 0;
bool     beepIsOn     = false;

// Volume via PWM duty, capped at 127 (= 50% = loudest for a passive buzzer).
uint8_t currentVolume = 2; // default; overridden by PLAY_ALARM_Vn commands
const uint8_t VOLUME_DUTY[5] = { 13, 26, 51, 89, 127 };

void buzzerOn()  { analogWrite(BUZZER_PIN, VOLUME_DUTY[currentVolume - 1]); }  // Seeed method: plain analogWrite, default 1 kHz PWM
void buzzerOff() { analogWrite(BUZZER_PIN, 0); }

void handleAlarmPattern() {
    if (!alarmActive) return;

    if (millis() - alarmStartedAt > ALARM_MAX_DURATION_MS) {
        alarmActive = false;
        buzzerOff();
        return;
    }

    uint32_t interval = beepIsOn ? BEEP_ON_MS : BEEP_OFF_MS;
    if (millis() - lastToggleAt > interval) {
        lastToggleAt = millis();
        beepIsOn = !beepIsOn;
        if (beepIsOn) buzzerOn(); else buzzerOff();
    }
}

void playChimeBlocking() {
    // Short and blocking is fine here -- a UI-feedback chime is meant to be
    // near-instant, and nothing else needs this chip's attention mid-chime.
    buzzerOn();
    delay(90);
    buzzerOff();
    delay(20);
}

void startAlarm(uint8_t volume) {
    currentVolume = constrain(volume, 1, 5);
    alarmActive = true;
    alarmStartedAt = millis();
    lastToggleAt = 0;
    beepIsOn = false;   // next toggle turns it on
}

void sendAck() {
    Serial1.write(0xAA);
}

// ── Optional Grove AHT20 temp/humidity sensor (RP2040 I2C port) ──────────────
// Read on a slow cadence into a cache so answering READ_TH is instant and the
// ~80 ms conversion never stalls the alarm-tone loop. The S3 polls this. On a
// base D1 with no sensor plugged in, getSensor() fails and we report "no data".
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
        long h = lroundf(humiFrac * 100.0f * 100.0f);    // fraction → % → centi-%
        cachedHumCenti  = (uint16_t)constrain(h, 0, 10000);
    } else {
        cachedTempCenti = TEMP_SENTINEL_NO_DATA;         // surfaces as "--" on the S3
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
            buzzerOff();
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

    // Grove-1 port power switch is active HIGH — must be driven on or an
    // external AHT20 on the Grove I2C bus gets no power and never responds.
    pinMode(GROVE_PWR_PIN, OUTPUT);
    digitalWrite(GROVE_PWR_PIN, HIGH);

    // Grove I2C bus for an optional AHT20 temp/humidity sensor.
    Wire.setSDA(GROVE_I2C_SDA);
    Wire.setSCL(GROVE_I2C_SCL);
    Wire.begin();
    aht.begin();

    // Boot self-test: two short beeps at max drive so it's immediately obvious
    // the buzzer + BUZZER_PIN are correct, INDEPENDENT of the UART/alarm path.
    //   • Beeps at power-on  → buzzer hardware/pin OK; any "alarm silent" issue
    //     is upstream (UART link or the ESP32 not sending the command).
    //   • No beep at power-on → BUZZER_PIN wrong or buzzer not driven.
    for (int i = 0; i < 2; i++) {
        analogWrite(BUZZER_PIN, 127);   // EXACTLY Seeed's reference drive (1 kHz default, 50% duty)
        delay(120);
        analogWrite(BUZZER_PIN, 0);
        delay(90);
    }
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
