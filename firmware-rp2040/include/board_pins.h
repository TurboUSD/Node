// include/board_pins.h — RP2040 side pin mapping.
//
// IMPORTANT: these pin numbers are placeholders. Confirm the actual buzzer
// and UART pins against the SenseCAP Indicator D1 schematic (Seeed's own
// reference firmware repo, linked earlier in this project, is the
// authoritative source) before wiring/flashing for real.

#pragma once

#define BUZZER_PIN        2   // MLT-8530, driven via PWM for tone generation
#define UART_FROM_S3_RX   0
#define UART_TO_S3_TX     1
#define UART_BAUD         115200

// ── Built-in environmental sensors (I2C0) ───────────────────────────────────
// The SenseCAP Indicator D1S/D1Pro have on-board SHT41 (temp/humidity), SCD41
// (CO2) and SGP40 (tVOC) on the RP2040's first I2C bus. Confirmed from Seeed's
// official examples (https://wiki.seeedstudio.com/SenseCAP_Indicator_RP2040_CO2/):
//   Wire.setSDA(20); Wire.setSCL(21);
// IMPORTANT: the built-in sensors are unpowered until GPIO 18 is driven HIGH —
// without that they never respond and temp/humidity read as "no data".
// (The base D1 / D1L have NO built-in sensors; on those this simply reports none.)
#define SENSOR_I2C_SDA    20
#define SENSOR_I2C_SCL    21
#define SENSOR_PWR_PIN    18   // drive HIGH to power the on-board sensors
