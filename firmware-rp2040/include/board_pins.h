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

// ── Grove I2C port (ambient temp/humidity sensor: AHT20) ────────────────────
// Pins confirmed from Seeed's official RP2040 Grove-IIC example, which does
//   Wire.setSDA(20); Wire.setSCL(21);
// (https://wiki.seeedstudio.com/SenseCAP_Indicator_RP2040_Grove_IIC/).
// The AHT20 is an external Grove module plugged into this port; if it's absent
// the firmware just reports "no reading" and the ESP32 shows "--".
#define GROVE_I2C_SDA     20
#define GROVE_I2C_SCL     21
