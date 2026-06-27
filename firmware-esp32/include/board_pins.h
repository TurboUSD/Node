// include/board_pins.h — ESP32-S3 pin mapping for the SenseCAP Indicator D1.
//
// SOURCE OF TRUTH: all constants below were extracted from Seeed's official
// reference firmware at:
//   https://github.com/Seeed-Solution/SenseCAP_Indicator_ESP32
// Specifically from:
//   components/bsp/src/boards/sensecap_indicator_board.c  (RGB + I2C pins)
//   components/bsp/src/boards/lcd_panel_config.c           (SPI bit-bang pins)
//   components/bsp/src/indev/indev_tp.c                    (touch I2C address)
//   components/bsp/Kconfig.projbuild                        (resolution, PCLK freq)
//
// DISPLAY ARCHITECTURE — NOT QSPI:
//   The D1 uses a 16-bit RGB parallel interface (ST7701S controller, GX panel
//   variant). Before the RGB stream starts, the ST7701S must receive ~100 init
//   commands over a bit-banged 9-bit SPI bus (GPIO 41/48, CS via IO expander).
//   After that, pixel data flows continuously over the 16 RGB data GPIOs.
//
// IO EXPANDER — TCA9535 at I2C 0x20:
//   Several control signals route through a TCA9535 IO expander rather than
//   directly to GPIO. See ui_manager.h's initDisplayAndTouch() for the I2C
//   protocol used to drive it.

#pragma once

// ── Shared I2C bus (touch controller + IO expander + sensors) ───────────────
#define I2C_PIN_SDA   39
#define I2C_PIN_SCL   40

// ── TCA9535 IO expander ──────────────────────────────────────────────────────
#define IO_EXPANDER_I2C_ADDR  0x20   // confirmed: TCA9535, not PCA9554

// Expander pin assignments (port 0, pins 0-7):
#define EXPANDER_PIN_LCD_CS    4     // ST7701S SPI chip-select (active low)
#define EXPANDER_PIN_LCD_RST   5     // ST7701S reset (active low pulse)
#define EXPANDER_PIN_TP_RST    7     // FT6336U touch reset (active low pulse)

// Expander pin assignments (port 1, pins 8-15):
#define EXPANDER_PIN_RP2040_RST  8   // RP2040 reset (normally kept high)
#define EXPANDER_PIN_BMP_PWR     10  // BMP sensor power enable

// ── ST7701S init: bit-banged 9-bit SPI ──────────────────────────────────────
// CS routes through IO expander (EXPANDER_PIN_LCD_CS above).
// CLK and MOSI are direct GPIOs bit-banged by initDisplayAndTouch().
#define LCD_SPI_CLK   41   // SCLK for ST7701S init commands
#define LCD_SPI_MOSI  48   // MOSI for ST7701S init commands (write-only)

// ── RGB parallel panel — 16-bit, 480×480, 18 MHz PCLK ───────────────────────
// These feed esp_lcd_new_rgb_panel() in initDisplayAndTouch().
#define LCD_PIN_PCLK   21
#define LCD_PIN_VSYNC  17
#define LCD_PIN_HSYNC  16
#define LCD_PIN_DE     18
#define LCD_PIN_BL     45   // backlight enable, active HIGH

// 16 data lines in B[4:0] G[5:0] R[4:0] order (RGB565-compatible layout).
// Listed DATA0..DATA15 as Seeed wires them; passed directly to data_gpio_nums[].
#define LCD_DATA0   15  // B0
#define LCD_DATA1   14  // B1
#define LCD_DATA2   13  // B2
#define LCD_DATA3   12  // B3
#define LCD_DATA4   11  // B4
#define LCD_DATA5   10  // G0
#define LCD_DATA6    9  // G1
#define LCD_DATA7    8  // G2
#define LCD_DATA8    7  // G3
#define LCD_DATA9    6  // G4
#define LCD_DATA10   5  // G5
#define LCD_DATA11   4  // R0
#define LCD_DATA12   3  // R1
#define LCD_DATA13   2  // R2
#define LCD_DATA14   1  // R3
#define LCD_DATA15   0  // R4

// RGB panel timing parameters (from Kconfig.projbuild, GX/D1 variant):
#define LCD_H_RES               480
#define LCD_V_RES               480
#define LCD_PCLK_HZ             (12 * 1000 * 1000)   // 12 MHz — matches Seeed's verified
                                                     // Arduino reference (gfx->begin(12000000L)).
                                                     // 18 MHz was too fast → on-screen garbage.
#define LCD_HSYNC_BACK_PORCH    50
#define LCD_HSYNC_FRONT_PORCH   10
#define LCD_HSYNC_PULSE_WIDTH    8
#define LCD_VSYNC_BACK_PORCH    20
#define LCD_VSYNC_FRONT_PORCH   10
#define LCD_VSYNC_PULSE_WIDTH    8

// ── FT6336U capacitive touch controller ──────────────────────────────────────
// Shares the I2C bus above. RESET is via IO expander (EXPANDER_PIN_TP_RST).
// No hardware INT pin is used in Seeed's reference firmware; polling is used.
#define TOUCH_I2C_ADDR  0x48   // FT6336U on the GX/D1 panel variant

// ── User button ──────────────────────────────────────────────────────────────
#define BTN_USER_GPIO  38   // active LOW
