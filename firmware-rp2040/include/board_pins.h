// include/board_pins.h — RP2040 side pin mapping.
//
// IMPORTANT: these pin numbers are placeholders. Confirm the actual buzzer
// and UART pins against the SenseCAP Indicator D1 schematic (Seeed's own
// reference firmware repo, linked earlier in this project, is the
// authoritative source) before wiring/flashing for real.

#pragma once

// Verified against Seeed SenseCAP Indicator pinout (ESPHome device reference):
//   Buzzer PWM = GP19; UART from ESP32-S3 = GP17 (RX, from ESP GP19/TX),
//   GP16 (TX, to ESP GP20/RX). These are the real board pins, not placeholders.
#define BUZZER_PIN        19  // MLT-8530 passive buzzer, driven via PWM for tone generation
#define UART_FROM_S3_RX   17
#define UART_TO_S3_TX     16
#define UART_BAUD         115200
#define GROVE_PWR_PIN     18  // Grove-1 power switch, active HIGH (must be on to power an external AHT20)

// ── Grove I2C port (optional external AHT20 temp/humidity sensor) ────────────
// Pins from Seeed's official RP2040 Grove-IIC example (Wire.setSDA(20)/setSCL(21)).
// NOTE: the base SenseCAP Indicator D1/D1L have NO built-in environmental sensor
// (only the D1S/D1Pro do). So temp/humidity work only if a Grove AHT20 is plugged
// into this port; otherwise the firmware reports "no reading" and the ESP32
// shows "--". This is expected on a base D1.
#define GROVE_I2C_SDA     20
#define GROVE_I2C_SCL     21
