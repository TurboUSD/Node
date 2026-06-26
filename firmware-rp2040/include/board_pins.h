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
