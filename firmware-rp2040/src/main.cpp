// src/main.cpp — TurboUSD Node firmware, RP2040 side.
// Drives the buzzer on command and reports an optional Grove AHT20 temp/humidity
// sensor back to the ESP32-S3. NOTE: the base SenseCAP Indicator D1/D1L have no
// built-in environmental sensor, so temp/humidity read as "--" unless a Grove
// AHT20 is plugged in. See PROTOCOL.md for the UART format.

#include <Arduino.h>
#include <Wire.h>
#include "AHT20.h"
#include "board_pins.h"

// Grove I2C temp/humidity probe. DEFAULT OFF: the base D1/D1L have NO sensor, and
// probing a floating/empty Grove bus was still able to HANG setup() on some units
// (Wire never returning) → loop() never ran → NO UART heartbeat, NO command
// parsing, NO alarm, while the pre-I2C boot beep still played (exactly the
// "1 beep but ping FAILED / no heartbeat" symptom in the /logs capture). Off = the
// buzzer/alarm link is guaranteed alive. Set to 1 only if a Grove AHT20 is fitted.
#ifndef TRY_GROVE_SENSOR
#define TRY_GROVE_SENSOR 0
#endif

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
// Even the LOWEST setting must be clearly audible: the old {13,26,…} floor was
// so weak (5–10% duty) that an alarm at the default volume 2 looked "silent"
// next to the 127-duty boot beep. Minimum now 60 (~24%), max still the 127
// reference peak (going higher = DC = silent on a passive buzzer).
uint8_t currentVolume = 2; // default; overridden by PLAY_ALARM_Vn commands
const uint8_t VOLUME_DUTY[5] = { 60, 80, 100, 115, 127 };

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
bool sensorPresent = false;   // probed once in setup(); reads skipped when absent
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
    // ── Boot sequence mirrors Seeed's FACTORY firmware exactly ──────────────
    // (examples/indicator_rp2040/indicator_rp2040.ino, the one that audibly
    // beeps at power-on). Factory order: sensor_power_on() → ... →
    // beep_init() → delay(500) → beep_on(). The 500 ms settle time after
    // raising the GP18 power rail matters: beeping immediately after
    // power-on (as this firmware used to) can fall inside the rail's
    // stabilisation window and produce no sound.
    pinMode(GROVE_PWR_PIN, OUTPUT);       // factory sensor_power_on(): GP18 HIGH
    digitalWrite(GROVE_PWR_PIN, HIGH);

    pinMode(BUZZER_PIN, OUTPUT);          // factory beep_init()
    delay(500);                           // factory settle delay before first beep

    // Boot self-test: ONE short, SOFT beep (low duty, brief) — just enough to
    // confirm the buzzer works at power-on. The alarm is the only loud sound.
    analogWrite(BUZZER_PIN, 15);          // very soft (~half the previous soft beep)
    delay(70);
    analogWrite(BUZZER_PIN, 0);
    delay(200);

    Serial1.setRX(UART_FROM_S3_RX);
    Serial1.setTX(UART_TO_S3_TX);
    Serial1.begin(UART_BAUD);

    // Grove I2C bus for an optional AHT20 temp/humidity sensor.
    //
    // CRITICAL: with the Grove port EMPTY the I2C lines FLOAT (the pull-up
    // resistors live on the sensor module), and the RP2040 core's
    // Wire.endTransmission() can hang FOREVER on a floating bus. That froze
    // setup() right here on sensorless devices: the four boot beeps played
    // (they run before I2C) but loop() never started — so no UART hellos, no
    // command parsing, no alarm. Fix: weak internal pull-ups so the bus
    // idles high, then a single probe; if nothing ACKs at 0x38, skip the
    // sensor entirely.
#if TRY_GROVE_SENSOR
    pinMode(GROVE_I2C_SDA, INPUT_PULLUP);
    pinMode(GROVE_I2C_SCL, INPUT_PULLUP);
    delay(2);
    // Belt & braces: if either line still reads LOW with pull-ups on, the
    // bus is shorted/held — do NOT even initialise I2C (that's the hang).
    if (digitalRead(GROVE_I2C_SDA) == HIGH && digitalRead(GROVE_I2C_SCL) == HIGH) {
        Wire.setSDA(GROVE_I2C_SDA);
        Wire.setSCL(GROVE_I2C_SCL);
        Wire.begin();
        Wire.beginTransmission(0x38);      // AHT20 address probe
        sensorPresent = (Wire.endTransmission() == 0);
        if (sensorPresent) aht.begin();
    }
#else
    // Sensor probe skipped (see TRY_GROVE_SENSOR): guarantees setup() finishes
    // and loop() runs, so the buzzer/alarm link and heartbeat are always alive.
    sensorPresent = false;
#endif
}

void loop() {
    // Link beacon: for the first 2 minutes, tell the ESP32 we exist — a
    // [0x7E][0xEE][checksum] hello every 5 s. The ESP32 logs it ("RP2040
    // heartbeat RECEIVED"), which proves the RP→ESP wire and that THIS
    // firmware is running, without opening the RP2040's USB port.
    // PERMANENT (the first version stopped after 2 minutes — but the RP2040
    // doesn't reboot when the ESP32 is reflashed, so by the time anyone
    // watched the ESP32 serial the hellos were long gone). 3 bytes / 10 s.
    static uint32_t lastHelloAt = 0;
    static bool firstHello = true;
    if (firstHello || millis() - lastHelloAt > 10000) {
        firstHello = false;
        lastHelloAt = millis();
        uint8_t f[3] = { 0x7E, 0xEE, (uint8_t)(0x7E ^ 0xEE) };
        Serial1.write(f, sizeof(f));
    }

    // Frame parser: [0x7E][cmd][checksum]. Reads byte-by-byte rather than
    // blocking on Serial1.readBytes() so handleAlarmPattern() keeps running
    // (and the buzzer keeps alternating tones) even if bytes arrive slowly.
    static uint8_t frame[3];
    static uint8_t frameIndex = 0;

    while (Serial1.available()) {
        uint8_t b = Serial1.read();

        // (The old per-byte "link diagnostic" blip lived here — it made the
        // buzzer chirp every few seconds once the link was up. The link is
        // confirmed working now, so it's gone: the buzzer only sounds for the
        // boot beep and the alarm.)

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
    if (sensorPresent && !alarmActive && (millis() - lastSensorReadAt > SENSOR_READ_INTERVAL_MS)) {
        lastSensorReadAt = millis();
        readSensorInto();
    }
}
