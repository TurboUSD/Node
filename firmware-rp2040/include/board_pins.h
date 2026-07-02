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

// ── Grove I2C port (optional external AHT20 temp/humidity sensor) ────────────
// Pins from Seeed's official RP2040 Grove-IIC example (Wire.setSDA(20)/setSCL(21)).
// NOTE: the base SenseCAP Indicator D1/D1L have NO built-in environmental sensor
// (only the D1S/D1Pro do). So temp/humidity work only if a Grove AHT20 is plugged
// into this port; otherwise the firmware reports "no reading" and the ESP32
// shows "--". This is expected on a base D1.
#define GROVE_I2C_SDA     20
#define GROVE_I2C_SCL     21
