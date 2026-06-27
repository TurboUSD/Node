// include/ui/ui_manager.h — top-level LVGL orchestration: display/touch
// init, owns one instance of each of the 5 screen classes, wires their
// shared header/footer taps (logo -> home, date -> calendar, QR -> config),
// and handles horizontal swipe navigation between them.
//
// Each screen's own visual structure lives in its dedicated header
// (screen_turbo.h, screen_debt.h, screen_game.h, screen_node.h) plus the
// Clock screen built inline below (kept as the original reference pattern).
//
// HARDWARE BRING-UP (initDisplayAndTouch):
//   Fully implemented based on Seeed's reference firmware:
//     github.com/Seeed-Solution/SenseCAP_Indicator_ESP32
//   The sequence is:
//     1. I2C bus init (SDA=39, SCL=40, shared by TCA9535 + FT6336U)
//     2. TCA9535 IO expander config (LCD_CS, LCD_RST, TP_RST as outputs)
//     3. ST7701S panel reset + 9-bit SPI init sequence (GPIO 41/48 bit-bang)
//     4. FT6336U touch reset (via expander) + LVGL input driver registration
//     5. RGB panel creation via esp_lcd_new_rgb_panel() (requires IDF 5.x /
//        Arduino-ESP32 3.x -- see platformio.ini's espressif32@^6.0.0)
//     6. Backlight enable (GPIO 45, active HIGH)

#pragma once
#include <Wire.h>
#include <lvgl.h>
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "board_pins.h"
#include "api_client.h"
#include "storage.h"
#include "ui/shared_components.h"
#include "ui/modal.h"
#include "ui/screen_turbo.h"
#include "ui/screen_debt.h"
#include "ui/screen_game.h"
#include "ui/screen_node.h"
#include "ui/screen_tickers.h"
#include "ui/screen_nft.h"

enum class ScreenId : uint8_t {
    CLOCK = 0,
    TURBO_STATS = 1,
    DEBT = 2,
    INFLATION_GAME = 3,
    NODE_NETWORK = 4,
    NFT = 5,      // penultimate — full-screen gallery
    TICKERS = 6,  // last — settings-like screener
    COUNT = 7
};

class UiManager {
public:
    void begin() {
        // lv_init() must come before initDisplayAndTouch() so the display
        // and input drivers can be registered inside that function.
        lv_init();
        initDisplayAndTouch();
        buildAllScreens();
        _loadScreenOrder();
        showScreen(ScreenId::CLOCK);
    }

    void loop() {
        lv_timer_handler();
        updateClockIfNeeded();
        _checkScreenTimeout();
    }

    // Set backlight brightness level 1–5 immediately via LEDC PWM.
    void setScreenBrightness(uint8_t level) {
        static const uint8_t DUTY[5] = { 25, 70, 130, 185, 255 };
        level = constrain(level, 1, 5);
        ledcWrite(0, DUTY[level - 1]);  // channel 0 = backlight (see ledcSetup in initDisplayAndTouch)
    }

    // Read brightness from NVS and apply it immediately.
    void applyStoredBrightness() {
        setScreenBrightness(storage.getScreenBrightness());
    }

    void showProvisioningScreen() {
        static bool shown = false;
        if (shown) return;
        shown = true;
        lv_obj_clean(lv_scr_act());
        lv_obj_t* label = lv_label_create(lv_scr_act());
        lv_label_set_text(label, "Connect your phone to:\nTurboUSD-Setup-XXXX\nthen open the page that appears.");
        lv_obj_set_style_text_color(label, lv_color_hex(0x3aff7a), 0);
        lv_obj_center(label);
    }

    bool isOnNodeScreen() { return currentScreen == ScreenId::NODE_NETWORK; }

    // Called by main.cpp when the nightly OTA check finds a newer version.
    // Creates a small persistent badge at the bottom of the screen. Tapping
    // it opens a confirm dialog; on confirm, onOtaInstallConfirmed is called.
    void showOtaBadge(const char* version) {
        clearOtaBadge(); // remove any existing one first

        _otaBadge = lv_obj_create(lv_layer_top()); // layer_top() floats over all screens
        lv_obj_set_size(_otaBadge, LV_PCT(100), 42);
        lv_obj_align(_otaBadge, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(_otaBadge, lv_color_hex(0x1c2c1c), 0);
        lv_obj_set_style_border_color(_otaBadge, lv_color_hex(0x3aff7a), 0);
        lv_obj_set_style_border_width(_otaBadge, 1, 0);
        lv_obj_set_style_border_side(_otaBadge, LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_pad_all(_otaBadge, 0, 0);
        lv_obj_add_flag(_otaBadge, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* label = lv_label_create(_otaBadge);
        char text[48];
        snprintf(text, sizeof(text), "\xE2\x86\x91 Firmware %s available — tap to install", version);
        lv_label_set_text(label, text);
        lv_obj_set_style_text_color(label, lv_color_hex(0x3aff7a), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
        lv_obj_center(label);

        // Store version string in badge's user_data so the callback can show it
        static char versionBuf[24];
        strncpy(versionBuf, version, sizeof(versionBuf) - 1);
        lv_obj_set_user_data(_otaBadge, versionBuf);
        lv_obj_add_event_cb(_otaBadge, onOtaBadgeTapped, LV_EVENT_CLICKED, this);
    }

    void clearOtaBadge() {
        if (_otaBadge) { lv_obj_del(_otaBadge); _otaBadge = nullptr; }
    }

    // Set this before begin(). Called when user confirms "Install".
    std::function<void()> onOtaInstallConfirmed;

    bool miningFeedNeedsRefresh() {
        uint32_t now = millis();
        if (now - lastMiningFeedFetch < MINING_FEED_REFRESH_MS) return false;
        lastMiningFeedFetch = now;
        return true;
    }

    void updateTreasuryData(const TreasuryData& data) {
        latestTreasury = data;
        turboScreen.updateData(data);
    }

    void loadOhlcvChart(OhlcvCandle* candles, int count) {
        turboScreen.loadRealCandles(candles, count);
    }

    void updateDebtData(const DebtData& data) {
        latestDebt = data;
        debtScreen.updateLiveTotal(data.totalDebtUsd);
    }

    void updateMiningFeed(MiningFeedEntry* entries, int count) {
        nodeScreen.updateMiningFeed(entries, count);
    }

    // Called from the main loop after a successful RP2040 sensor poll. The new
    // values appear on the next per-second header/Home refresh, so there's no
    // need to redraw here.
    void updateAmbient(float tempC, int humidityPct) {
        _tempC = tempC;
        _humidityPct = humidityPct;
        _sensorValid = true;
    }

    // Called when a sensor poll fails (link down, or no AHT20 plugged in).
    // Drops back to showing "--" rather than a stale reading.
    void markAmbientUnavailable() {
        _sensorValid = false;
    }

    void showAlarmFiringOverlay() {
        lv_obj_t* overlay = lv_obj_create(lv_scr_act());
        lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(overlay, lv_color_hex(0xe8b339), 0);
        lv_obj_center(overlay);

        lv_obj_t* stopBtn = lv_btn_create(overlay);
        lv_obj_set_size(stopBtn, 160, 60);
        lv_obj_center(stopBtn);
        lv_obj_add_event_cb(stopBtn, [](lv_event_t* e) {
            UiManager* self = (UiManager*)lv_event_get_user_data(e);
            self->dismissAlarmOverlay();
        }, LV_EVENT_CLICKED, this);
        lv_obj_t* stopLabel = lv_label_create(stopBtn);
        lv_label_set_text(stopLabel, "STOP");
        lv_obj_center(stopLabel);

        alarmOverlay = overlay;
    }

    std::function<void()> onAlarmDismissed;

    // Called from the touch input driver whenever the screen is physically touched.
    // Resets the inactivity timer and wakes the backlight if it was off.
    void _onTouchActivity() {
        _lastTouchMs  = millis();
        if (!_screenIsOn) {
            _screenIsOn = true;
            applyStoredBrightness();
        }
    }

    // Checked every loop() tick. Turns off the backlight after the configured
    // idle period when always-on mode is disabled.
    void _checkScreenTimeout() {
        if (storage.getScreenAlwaysOn()) return;
        uint32_t timeoutMs = (uint32_t)storage.getScreenTimeoutMins() * 60UL * 1000UL;
        if (_screenIsOn && (millis() - _lastTouchMs > timeoutMs)) {
            _screenIsOn = false;
            ledcWrite(0, 0);  // channel 0 = backlight
        }
    }

private:
    ScreenId currentScreen = ScreenId::CLOCK;
    lv_obj_t* screens[(int)ScreenId::COUNT] = { nullptr };
    lv_obj_t* alarmOverlay = nullptr;

    // Screen timeout state
    uint32_t _lastTouchMs = 0;     // millis() of the last touch event
    bool     _screenIsOn  = true;  // false while backlight is off due to timeout

    // Swipe order: _swipeOrder[swipe_pos] = ScreenId value.
    // Default (matches the intended UX order):
    //   pos 0 → CLOCK(0)        Home
    //   pos 1 → TURBO_STATS(1)  Price/chart
    //   pos 2 → TICKERS(6)      Token screener  ← screen 3 (user-facing)
    //   pos 3 → DEBT(2)         US debt
    //   pos 4 → INFLATION_GAME(3) Inflation calculator
    //   pos 5 → NFT(5)          NFT gallery     ← penultimate
    //   pos 6 → NODE_NETWORK(4) Node status     ← last
    // Can be overridden per-device from NVS (set via web setup page). Position 0 is always CLOCK.
    uint8_t _swipeOrder[(int)ScreenId::COUNT] = { 0, 1, 6, 2, 3, 5, 4 };
    int     _currentSwipePos = 0;

    TurboScreen  turboScreen;
    DebtScreen   debtScreen;
    GameScreen   gameScreen;
    NodeScreen   nodeScreen;
    TickerScreen tickerScreen;
    NftScreen    nftScreen;

    TreasuryData latestTreasury;
    DebtData latestDebt;
    uint32_t lastMiningFeedFetch = 0;

    esp_lcd_panel_handle_t _lcdPanel = nullptr;
    lv_obj_t* _otaBadge = nullptr;

    // TCA9535 IO expander output shadow registers (one byte per 8-pin port).
    // We must track current output state to do single-pin set/clear, since
    // the TCA9535 write register takes the full port byte each time.
    uint8_t _expanderPort0 = 0xFF;  // all pins high by default
    uint8_t _expanderPort1 = 0xFF;

    int debtYearsRangeIndex = 4;
    int sincePeriodIndex = 4;
    int rateUnitIndex = 0;
    int gameYearsIndex = 1;

    lv_obj_t* clockTimeLabel = nullptr;
    lv_obj_t* clockDateLabel = nullptr;
    lv_obj_t* clockAlarmLabel = nullptr;
    lv_obj_t* clockWeatherLabel = nullptr;  // "23° · 48%" line on the Home screen
    uint32_t lastClockRedrawSecond = 255;
    SharedFooterRefs clockFooterRefs;

    // Ambient readings from the AHT20 on the RP2040 (polled over UART in the
    // main loop). _sensorValid is false until the first good read, or whenever
    // a read fails — the UI then shows "--" instead of a stale/zero value.
    float _tempC        = 0.0f;
    int   _humidityPct  = 0;
    bool  _sensorValid  = false;

    // ── Screen swipe order ───────────────────────────────────────────────────

    // Load the user-defined swipe order from NVS.
    // Format: comma-separated ScreenId integers, e.g. "0,4,1,2,3,5,6".
    // Position 0 must always be 0 (CLOCK/Home) — enforced here.
    // Falls back to the default 0,1,2,3,4,5,6 if NVS entry is absent/malformed.
    void _loadScreenOrder() {
        String stored = storage.getScreenOrder();
        if (stored.length() == 0) return;   // use default array initialiser values

        uint8_t tmp[(int)ScreenId::COUNT];
        int pos = 0;
        int idx = 0;
        int len = stored.length();

        while (idx < len && pos < (int)ScreenId::COUNT) {
            int sep = stored.indexOf(',', idx);
            if (sep < 0) sep = len;
            int val = stored.substring(idx, sep).toInt();
            if (val >= 0 && val < (int)ScreenId::COUNT) {
                tmp[pos++] = (uint8_t)val;
            }
            idx = sep + 1;
        }

        // Only apply if we got exactly COUNT entries and position 0 is CLOCK
        if (pos == (int)ScreenId::COUNT && tmp[0] == (uint8_t)ScreenId::CLOCK) {
            memcpy(_swipeOrder, tmp, sizeof(_swipeOrder));
        }
    }

    // ── TCA9535 helpers ──────────────────────────────────────────────────────

    // Write one byte to a TCA9535 register over I2C.
    void expanderWriteReg(uint8_t reg, uint8_t val) {
        Wire.beginTransmission(IO_EXPANDER_I2C_ADDR);
        Wire.write(reg);
        Wire.write(val);
        Wire.endTransmission();
    }

    // Set or clear a single IO expander pin, preserving all other pin states.
    // Pins 0-7 → port 0; pins 8-15 → port 1.
    void expanderSetPin(uint8_t pin, uint8_t level) {
        if (pin < 8) {
            if (level) _expanderPort0 |=  (1u << pin);
            else       _expanderPort0 &= ~(1u << pin);
            expanderWriteReg(0x02, _expanderPort0);  // output port 0
        } else {
            uint8_t bit = pin - 8;
            if (level) _expanderPort1 |=  (1u << bit);
            else       _expanderPort1 &= ~(1u << bit);
            expanderWriteReg(0x03, _expanderPort1);  // output port 1
        }
    }

    // ── ST7701S bit-bang SPI ─────────────────────────────────────────────────
    // Protocol: 9-bit frames, MSB first (bit 8 = D/CX: 0=cmd, 1=data).
    // CS is driven through the IO expander; CLK and MOSI are direct GPIOs.
    // Faithfully ported from Seeed's SPI_SendData / SPI_WriteComm / SPI_WriteData
    // in components/bsp/src/boards/lcd_panel_config.c.

    void st7701sSpiSend9(uint16_t bits9) {
        for (int n = 0; n < 9; n++) {
            digitalWrite(LCD_SPI_MOSI, (bits9 & 0x0100) ? HIGH : LOW);
            bits9 <<= 1;
            digitalWrite(LCD_SPI_CLK, HIGH);
            delayMicroseconds(10);
            digitalWrite(LCD_SPI_CLK, LOW);
            delayMicroseconds(10);
        }
    }

    // Send a command byte (D/CX = 0). Matches Seeed's SPI_WriteComm() exactly:
    // two CS-framed 9-bit transactions — the first carries the high byte of a
    // 16-bit command address (always 0 for the ST7701S's single-byte commands),
    // the second carries the actual command byte with D/CX = 0.
    void st7701sCmd(uint8_t cmd) {
        expanderSetPin(EXPANDER_PIN_LCD_CS, 0);
        delayMicroseconds(10);
        digitalWrite(LCD_SPI_CLK, LOW);
        delayMicroseconds(10);
        st7701sSpiSend9(((uint16_t)(cmd >> 8) & 0xFF) | 0x2000u);
        digitalWrite(LCD_SPI_CLK, HIGH);
        delayMicroseconds(10);
        digitalWrite(LCD_SPI_CLK, LOW);
        expanderSetPin(EXPANDER_PIN_LCD_CS, 1);
        delayMicroseconds(10);
        expanderSetPin(EXPANDER_PIN_LCD_CS, 0);
        delayMicroseconds(10);
        st7701sSpiSend9((uint16_t)(cmd & 0xFF));  // D/CX = bit8 = 0 (command)
        expanderSetPin(EXPANDER_PIN_LCD_CS, 1);
        delayMicroseconds(10);
    }

    // Send a data byte (D/CX = 1). Matches Seeed's SPI_WriteData().
    void st7701sData(uint8_t data) {
        uint16_t bits9 = ((uint16_t)(data & 0xFF)) | 0x0100u;  // D/CX = 1
        expanderSetPin(EXPANDER_PIN_LCD_CS, 0);
        delayMicroseconds(10);
        digitalWrite(LCD_SPI_CLK, LOW);
        delayMicroseconds(10);
        st7701sSpiSend9(bits9);
        digitalWrite(LCD_SPI_CLK, HIGH);
        delayMicroseconds(10);
        digitalWrite(LCD_SPI_CLK, LOW);
        delayMicroseconds(10);
        expanderSetPin(EXPANDER_PIN_LCD_CS, 1);
        delayMicroseconds(10);
    }

    // Full ST7701S initialisation sequence for the D1's GX 4.0-inch panel.
    // Copied verbatim from Seeed's lcd_panel_st7701s_init() in lcd_panel_config.c,
    // translated to st7701sCmd() / st7701sData() calls.
    void st7701sInit() {
        // Page 0 (manufacturer command enable)
        st7701sCmd(0xFF); st7701sData(0x77); st7701sData(0x01);
        st7701sData(0x00); st7701sData(0x00); st7701sData(0x10);

        st7701sCmd(0xC0); st7701sData(0x3B); st7701sData(0x00);  // 480×480
        st7701sCmd(0xC1); st7701sData(0x0D); st7701sData(0x02);
        st7701sCmd(0xC2); st7701sData(0x31); st7701sData(0x05);
        st7701sCmd(0xC7); st7701sData(0x04);
        st7701sCmd(0xCD); st7701sData(0x08);

        // Positive gamma
        st7701sCmd(0xB0);
        st7701sData(0x00); st7701sData(0x11); st7701sData(0x18); st7701sData(0x0E);
        st7701sData(0x11); st7701sData(0x06); st7701sData(0x07); st7701sData(0x08);
        st7701sData(0x07); st7701sData(0x22); st7701sData(0x04); st7701sData(0x12);
        st7701sData(0x0F); st7701sData(0xAA); st7701sData(0x31); st7701sData(0x18);

        // Negative gamma
        st7701sCmd(0xB1);
        st7701sData(0x00); st7701sData(0x11); st7701sData(0x19); st7701sData(0x0E);
        st7701sData(0x12); st7701sData(0x07); st7701sData(0x08); st7701sData(0x08);
        st7701sData(0x08); st7701sData(0x22); st7701sData(0x04); st7701sData(0x11);
        st7701sData(0x11); st7701sData(0xA9); st7701sData(0x32); st7701sData(0x18);

        // Page 1
        st7701sCmd(0xFF); st7701sData(0x77); st7701sData(0x01);
        st7701sData(0x00); st7701sData(0x00); st7701sData(0x11);

        st7701sCmd(0xB0); st7701sData(0x60);
        st7701sCmd(0xB1); st7701sData(0x32);
        st7701sCmd(0xB2); st7701sData(0x07);
        st7701sCmd(0xB3); st7701sData(0x80);
        st7701sCmd(0xB5); st7701sData(0x49);
        st7701sCmd(0xB7); st7701sData(0x85);
        st7701sCmd(0xB8); st7701sData(0x21);
        st7701sCmd(0xC1); st7701sData(0x78);
        st7701sCmd(0xC2); st7701sData(0x78);

        delay(20);

        st7701sCmd(0xE0); st7701sData(0x00); st7701sData(0x1B); st7701sData(0x02);

        st7701sCmd(0xE1);
        st7701sData(0x08); st7701sData(0xA0); st7701sData(0x00); st7701sData(0x00);
        st7701sData(0x07); st7701sData(0xA0); st7701sData(0x00); st7701sData(0x00);
        st7701sData(0x00); st7701sData(0x44); st7701sData(0x44);

        st7701sCmd(0xE2);
        st7701sData(0x11); st7701sData(0x11); st7701sData(0x44); st7701sData(0x44);
        st7701sData(0xED); st7701sData(0xA0); st7701sData(0x00); st7701sData(0x00);
        st7701sData(0xEC); st7701sData(0xA0); st7701sData(0x00); st7701sData(0x00);

        st7701sCmd(0xE3);
        st7701sData(0x00); st7701sData(0x00); st7701sData(0x11); st7701sData(0x11);

        st7701sCmd(0xE4); st7701sData(0x44); st7701sData(0x44);

        st7701sCmd(0xE5);
        st7701sData(0x0A); st7701sData(0xE9); st7701sData(0xD8); st7701sData(0xA0);
        st7701sData(0x0C); st7701sData(0xEB); st7701sData(0xD8); st7701sData(0xA0);
        st7701sData(0x0E); st7701sData(0xED); st7701sData(0xD8); st7701sData(0xA0);
        st7701sData(0x10); st7701sData(0xEF); st7701sData(0xD8); st7701sData(0xA0);

        st7701sCmd(0xE6);
        st7701sData(0x00); st7701sData(0x00); st7701sData(0x11); st7701sData(0x11);

        st7701sCmd(0xE7); st7701sData(0x44); st7701sData(0x44);

        st7701sCmd(0xE8);
        st7701sData(0x09); st7701sData(0xE8); st7701sData(0xD8); st7701sData(0xA0);
        st7701sData(0x0B); st7701sData(0xEA); st7701sData(0xD8); st7701sData(0xA0);
        st7701sData(0x0D); st7701sData(0xEC); st7701sData(0xD8); st7701sData(0xA0);
        st7701sData(0x0F); st7701sData(0xEE); st7701sData(0xD8); st7701sData(0xA0);

        st7701sCmd(0xEB);
        st7701sData(0x02); st7701sData(0x00); st7701sData(0xE4); st7701sData(0xE4);
        st7701sData(0x88); st7701sData(0x00); st7701sData(0x40);

        st7701sCmd(0xEC); st7701sData(0x3C); st7701sData(0x00);

        st7701sCmd(0xED);
        st7701sData(0xAB); st7701sData(0x89); st7701sData(0x76); st7701sData(0x54);
        st7701sData(0x02); st7701sData(0xFF); st7701sData(0xFF); st7701sData(0xFF);
        st7701sData(0xFF); st7701sData(0xFF); st7701sData(0xFF); st7701sData(0x20);
        st7701sData(0x45); st7701sData(0x67); st7701sData(0x98); st7701sData(0xBA);

        st7701sCmd(0x36); st7701sData(0x10);

        // Page 3 (internal oscillator fine-tune)
        st7701sCmd(0xFF); st7701sData(0x77); st7701sData(0x01);
        st7701sData(0x00); st7701sData(0x00); st7701sData(0x13);
        st7701sCmd(0xE5); st7701sData(0xE4);

        // Back to page 0 for display control
        st7701sCmd(0xFF); st7701sData(0x77); st7701sData(0x01);
        st7701sData(0x00); st7701sData(0x00); st7701sData(0x00);

        st7701sCmd(0x3A); st7701sData(0x60);  // pixel format: RGB666 (matches 16-data-line layout)
        st7701sCmd(0x21);                      // display inversion on (required for this panel)
        st7701sCmd(0x11);                      // sleep out
        delay(120);
        st7701sCmd(0x29);                      // display on
        delay(20);

        // Deassert CS / idle state
        expanderSetPin(EXPANDER_PIN_LCD_CS, 1);
        digitalWrite(LCD_SPI_CLK, HIGH);
        digitalWrite(LCD_SPI_MOSI, HIGH);
    }

    // ── Main hardware bring-up ────────────────────────────────────────────────

    void initDisplayAndTouch() {
        // 1. I2C bus — shared by TCA9535 IO expander and FT6336U touch.
        Wire.begin(I2C_PIN_SDA, I2C_PIN_SCL, 400000UL);

        // 2. TCA9535 IO expander — configure port 0 pins 4, 5, 7 as outputs;
        //    port 1 pins 8 (bit 0) and 10 (bit 2) as outputs; rest as inputs.
        //    TCA9535 config register: 0 = output, 1 = input (default all 1).
        //    Port 0: clear bits 4, 5, 7 → config = 0b01001111 = 0x4F
        //    Port 1: clear bits 0, 2   → config = 0b11111010 = 0xFA
        expanderWriteReg(0x06, 0x4F);  // port 0 direction
        expanderWriteReg(0x07, 0xFA);  // port 1 direction
        // Drive all outputs high initially
        expanderWriteReg(0x02, _expanderPort0);
        expanderWriteReg(0x03, _expanderPort1);

        // 3. Power on RP2040 and BMP sensor via expander (keep high = enabled).
        expanderSetPin(EXPANDER_PIN_RP2040_RST, 1);
        expanderSetPin(EXPANDER_PIN_BMP_PWR,    1);

        // 4. ST7701S panel reset pulse (active low, 10 ms).
        expanderSetPin(EXPANDER_PIN_LCD_RST, 0);
        delay(10);
        expanderSetPin(EXPANDER_PIN_LCD_RST, 1);
        delay(50);  // wait for controller to come out of reset

        // 5. FT6336U touch reset pulse (active low, 5 ms).
        expanderSetPin(EXPANDER_PIN_TP_RST, 0);
        delay(5);
        expanderSetPin(EXPANDER_PIN_TP_RST, 1);
        delay(50);

        // 6. Set up GPIO pins for bit-bang SPI (ST7701S init commands).
        pinMode(LCD_SPI_CLK,  OUTPUT);
        pinMode(LCD_SPI_MOSI, OUTPUT);
        digitalWrite(LCD_SPI_CLK,  HIGH);
        digitalWrite(LCD_SPI_MOSI, HIGH);

        // 7. Send the ~100 ST7701S init commands over bit-bang SPI.
        st7701sInit();

        // 8. Create RGB panel via ESP-IDF 5.x esp_lcd component.
        //    Frame buffer lives in PSRAM (480×480×2 = ~450 KB).
        esp_lcd_rgb_panel_config_t panelCfg = {};
        panelCfg.clk_src                    = LCD_CLK_SRC_XTAL;  // DEFAULT renamed in IDF 5.1+
        panelCfg.data_width                 = 16;
        // bits_per_pixel and num_fbs removed from struct in newer IDF — data_width suffices
        panelCfg.psram_trans_align          = 64;
        panelCfg.hsync_gpio_num             = LCD_PIN_HSYNC;
        panelCfg.vsync_gpio_num             = LCD_PIN_VSYNC;
        panelCfg.de_gpio_num                = LCD_PIN_DE;
        panelCfg.pclk_gpio_num             = LCD_PIN_PCLK;
        panelCfg.disp_gpio_num             = GPIO_NUM_NC;
        panelCfg.data_gpio_nums[0]  = LCD_DATA0;
        panelCfg.data_gpio_nums[1]  = LCD_DATA1;
        panelCfg.data_gpio_nums[2]  = LCD_DATA2;
        panelCfg.data_gpio_nums[3]  = LCD_DATA3;
        panelCfg.data_gpio_nums[4]  = LCD_DATA4;
        panelCfg.data_gpio_nums[5]  = LCD_DATA5;
        panelCfg.data_gpio_nums[6]  = LCD_DATA6;
        panelCfg.data_gpio_nums[7]  = LCD_DATA7;
        panelCfg.data_gpio_nums[8]  = LCD_DATA8;
        panelCfg.data_gpio_nums[9]  = LCD_DATA9;
        panelCfg.data_gpio_nums[10] = LCD_DATA10;
        panelCfg.data_gpio_nums[11] = LCD_DATA11;
        panelCfg.data_gpio_nums[12] = LCD_DATA12;
        panelCfg.data_gpio_nums[13] = LCD_DATA13;
        panelCfg.data_gpio_nums[14] = LCD_DATA14;
        panelCfg.data_gpio_nums[15] = LCD_DATA15;
        panelCfg.timings.pclk_hz          = LCD_PCLK_HZ;
        panelCfg.timings.h_res            = LCD_H_RES;
        panelCfg.timings.v_res            = LCD_V_RES;
        panelCfg.timings.hsync_pulse_width = LCD_HSYNC_PULSE_WIDTH;
        panelCfg.timings.hsync_back_porch  = LCD_HSYNC_BACK_PORCH;
        panelCfg.timings.hsync_front_porch = LCD_HSYNC_FRONT_PORCH;
        panelCfg.timings.vsync_pulse_width = LCD_VSYNC_PULSE_WIDTH;
        panelCfg.timings.vsync_back_porch  = LCD_VSYNC_BACK_PORCH;
        panelCfg.timings.vsync_front_porch = LCD_VSYNC_FRONT_PORCH;
        panelCfg.timings.flags.pclk_active_neg = 0;
        panelCfg.flags.fb_in_psram = 1;

        ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panelCfg, &_lcdPanel));
        ESP_ERROR_CHECK(esp_lcd_panel_reset(_lcdPanel));
        ESP_ERROR_CHECK(esp_lcd_panel_init(_lcdPanel));

        // 9. Register LVGL display driver.
        //    Draw buffer: 480×20 lines in internal SRAM (fast for partial
        //    rendering). LVGL calls flush_cb for each dirty region; we blit
        //    it to the RGB framebuffer via esp_lcd_panel_draw_bitmap().
        static lv_disp_draw_buf_t drawBuf;
        static lv_color_t         lvBuf[LCD_H_RES * 20];
        lv_disp_draw_buf_init(&drawBuf, lvBuf, nullptr, LCD_H_RES * 20);

        static lv_disp_drv_t dispDrv;
        lv_disp_drv_init(&dispDrv);
        dispDrv.hor_res   = LCD_H_RES;
        dispDrv.ver_res   = LCD_V_RES;
        dispDrv.draw_buf  = &drawBuf;
        dispDrv.user_data = _lcdPanel;
        dispDrv.flush_cb  = [](lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* colorP) {
            esp_lcd_panel_handle_t panel = static_cast<esp_lcd_panel_handle_t>(drv->user_data);
            esp_lcd_panel_draw_bitmap(panel,
                area->x1, area->y1,
                area->x2 + 1, area->y2 + 1,
                colorP);
            lv_disp_flush_ready(drv);
        };
        lv_disp_drv_register(&dispDrv);

        // 10. Register LVGL touch input driver (FT6336U, polled — no INT pin).
        //     Reads 5 bytes from I2C 0x48: [touch_count, xH, xL, yH, yL].
        //     D/CX bits in xH[7:6] and yH[7:4] are masked out.
        //     If colors look mirrored, swap x/y or invert here to match the
        //     physical orientation (portrait, connector at bottom).
        static lv_indev_drv_t indevDrv;
        lv_indev_drv_init(&indevDrv);
        indevDrv.type      = LV_INDEV_TYPE_POINTER;
        indevDrv.user_data = this;   // so the lambda can reach _onTouchActivity()
        indevDrv.read_cb   = [](lv_indev_drv_t* drv, lv_indev_data_t* data) {
            Wire.beginTransmission(TOUCH_I2C_ADDR);
            Wire.write(0x02);  // TD_STATUS register: number of touch points
            Wire.endTransmission(false);
            Wire.requestFrom((uint8_t)TOUCH_I2C_ADDR, (uint8_t)5);
            if (Wire.available() >= 5) {
                uint8_t pts = Wire.read() & 0x0F;
                uint8_t xh  = Wire.read() & 0x0F;
                uint8_t xl  = Wire.read();
                uint8_t yh  = Wire.read() & 0x0F;
                uint8_t yl  = Wire.read();
                if (pts > 0) {
                    data->point.x = (lv_coord_t)((xh << 8) | xl);
                    data->point.y = (lv_coord_t)((yh << 8) | yl);
                    data->state   = LV_INDEV_STATE_PRESSED;
                    // Any touch resets the inactivity timer (and wakes screen if off)
                    static_cast<UiManager*>(drv->user_data)->_onTouchActivity();
                } else {
                    data->state = LV_INDEV_STATE_RELEASED;
                }
            } else {
                data->state = LV_INDEV_STATE_RELEASED;
            }
        };
        lv_indev_drv_register(&indevDrv);

        // 11. Enable backlight via LEDC PWM (GPIO 45, active HIGH).
        //     5 kHz / 8-bit resolution gives smooth dimming with no audible whine.
        //     Arduino-ESP32 2.x API: ledcSetup(ch,freq,bits) + ledcAttachPin(pin,ch).
        //     Channel 0 is reserved for the backlight (LCD_BL_LEDC_CH = 0).
        ledcSetup(0, 5000, 8);
        ledcAttachPin(LCD_PIN_BL, 0);
        applyStoredBrightness();
    }

    void dismissAlarmOverlay() {
        if (alarmOverlay) { lv_obj_del(alarmOverlay); alarmOverlay = nullptr; }
        if (onAlarmDismissed) onAlarmDismissed();
    }

    void buildAllScreens() {
        screens[(int)ScreenId::CLOCK] = buildClockScreen();

        screens[(int)ScreenId::TURBO_STATS] = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(screens[(int)ScreenId::TURBO_STATS], lv_color_black(), 0);
        turboScreen.build(screens[(int)ScreenId::TURBO_STATS], onLogoTapped, onDateTapped, onQrTapped, this);
        _wireAlarmIcon(turboScreen.header);

        screens[(int)ScreenId::DEBT] = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(screens[(int)ScreenId::DEBT], lv_color_black(), 0);
        debtScreen.build(screens[(int)ScreenId::DEBT], onLogoTapped, onDateTapped, onQrTapped,
                          onDebtRangeTapped, onSinceBtnTapped, onRateBtnTapped, this);
        _wireAlarmIcon(debtScreen.header);

        screens[(int)ScreenId::INFLATION_GAME] = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(screens[(int)ScreenId::INFLATION_GAME], lv_color_black(), 0);
        gameScreen.build(screens[(int)ScreenId::INFLATION_GAME], onLogoTapped, onDateTapped, onQrTapped, onGameYearsTapped, this);
        _wireAlarmIcon(gameScreen.header);

        screens[(int)ScreenId::NODE_NETWORK] = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(screens[(int)ScreenId::NODE_NETWORK], lv_color_black(), 0);
        nodeScreen.build(screens[(int)ScreenId::NODE_NETWORK], onLogoTapped, onDateTapped, onQrTapped, onVerifyBadgeTapped, this);
        _wireAlarmIcon(nodeScreen.header);

        screens[(int)ScreenId::NFT] = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(screens[(int)ScreenId::NFT], lv_color_black(), 0);
        nftScreen.build(screens[(int)ScreenId::NFT], onLogoTapped, onDateTapped, onQrTapped, this);
        _wireAlarmIcon(nftScreen.header);

        screens[(int)ScreenId::TICKERS] = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(screens[(int)ScreenId::TICKERS], lv_color_black(), 0);
        tickerScreen.build(screens[(int)ScreenId::TICKERS], onLogoTapped, onDateTapped, onQrTapped, this);
        _wireAlarmIcon(tickerScreen.header);
        TickerScreen::s_instance = &tickerScreen;
        lv_timer_create(TickerScreen::pollPending, 100, &tickerScreen);

        attachSwipeGesture(screens[(int)ScreenId::CLOCK]);
        attachSwipeGesture(screens[(int)ScreenId::TURBO_STATS]);
        attachSwipeGesture(screens[(int)ScreenId::DEBT]);
        attachSwipeGesture(screens[(int)ScreenId::INFLATION_GAME]);
        attachSwipeGesture(screens[(int)ScreenId::NODE_NETWORK]);
        attachSwipeGesture(screens[(int)ScreenId::NFT]);
        attachSwipeGesture(screens[(int)ScreenId::TICKERS]);
    }

    // Wire the alarm bell icon on a non-clock header after build().
    void _wireAlarmIcon(SharedHeaderRefs& hdr) {
        if (!hdr.alarmIcon) return;
        lv_obj_add_flag(hdr.alarmIcon, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(hdr.alarmIcon, 6);
        lv_obj_add_event_cb(hdr.alarmIcon, onAlarmIconTapped, LV_EVENT_CLICKED, this);
    }

    lv_obj_t* buildClockScreen() {
        lv_obj_t* scr = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

        LV_IMG_DECLARE(turbousd_logo);
        lv_obj_t* logo = lv_img_create(scr);
        lv_img_set_src(logo, &turbousd_logo);
        lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 12);

        clockDateLabel = lv_label_create(scr);
        lv_obj_set_style_text_color(clockDateLabel, lv_color_hex(0x9a9a9e), 0);
        lv_obj_align(clockDateLabel, LV_ALIGN_CENTER, 0, -70);

        clockTimeLabel = lv_label_create(scr);
        lv_obj_set_style_text_color(clockTimeLabel, lv_color_hex(0x3aff7a), 0);
        lv_obj_set_style_text_font(clockTimeLabel, &lv_font_montserrat_48, 0);
        lv_obj_align(clockTimeLabel, LV_ALIGN_CENTER, 0, -20);

        clockAlarmLabel = lv_label_create(scr);
        lv_obj_set_style_text_color(clockAlarmLabel, lv_color_hex(0xe8b339), 0);
        lv_obj_align(clockAlarmLabel, LV_ALIGN_CENTER, 0, 30);
        lv_obj_add_flag(clockAlarmLabel, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(clockAlarmLabel, onAlarmLabelTapped, LV_EVENT_CLICKED, this);

        // Ambient temp/humidity line, e.g. "23° · 48%" (from the RP2040 AHT20).
        clockWeatherLabel = lv_label_create(scr);
        lv_obj_set_style_text_color(clockWeatherLabel, lv_color_hex(0x9a9a9e), 0);
        lv_obj_set_style_text_font(clockWeatherLabel, &lv_font_montserrat_16, 0);
        lv_obj_align(clockWeatherLabel, LV_ALIGN_CENTER, 0, 64);
        lv_label_set_text(clockWeatherLabel, "--\xC2\xB0 \xC2\xB7 --%");

        clockFooterRefs = buildSharedFooter(scr, onQrTapped, this);

        return scr;
    }

    void updateClockIfNeeded() {
        time_t now = time(nullptr);
        struct tm t;
        localtime_r(&now, &t);  // local wall-clock (tz offset set via configTime/geo)
        if ((uint32_t)t.tm_sec == lastClockRedrawSecond) return;
        lastClockRedrawSecond = t.tm_sec;

        // Refresh alarm-bell icons on every non-clock header once per second.
        bool alarmOn    = storage.getAlarmEnabled();
        bool todayOn    = storage.isAlarmActiveToday(t.tm_wday);
        refreshSharedAlarmIcon(turboScreen.header,  alarmOn, todayOn);
        refreshSharedAlarmIcon(debtScreen.header,   alarmOn, todayOn);
        refreshSharedAlarmIcon(gameScreen.header,   alarmOn, todayOn);
        refreshSharedAlarmIcon(nodeScreen.header,   alarmOn, todayOn);
        refreshSharedAlarmIcon(tickerScreen.header, alarmOn, todayOn);
        refreshSharedAlarmIcon(nftScreen.header,    alarmOn, todayOn);

        // Refresh the date/time/temp/humidity text in every non-clock header.
        // (Previously nothing called this, so those headers showed no clock and
        // the temp/humidity labels stayed blank.) Temp/humidity come from the
        // RP2040 AHT20 poll; when there's no valid reading they render as "--".
        bool is24h      = storage.getTimeFormat() == "24H";
        char tempUnit   = storage.getTempUnit();
        refreshSharedHeader(turboScreen.header,  t, _tempC, _humidityPct, is24h, tempUnit, _sensorValid);
        refreshSharedHeader(debtScreen.header,   t, _tempC, _humidityPct, is24h, tempUnit, _sensorValid);
        refreshSharedHeader(gameScreen.header,   t, _tempC, _humidityPct, is24h, tempUnit, _sensorValid);
        refreshSharedHeader(nodeScreen.header,   t, _tempC, _humidityPct, is24h, tempUnit, _sensorValid);
        refreshSharedHeader(tickerScreen.header, t, _tempC, _humidityPct, is24h, tempUnit, _sensorValid);
        refreshSharedHeader(nftScreen.header,    t, _tempC, _humidityPct, is24h, tempUnit, _sensorValid);

        // Clock/Home screen text only below
        if (currentScreen != ScreenId::CLOCK) return;

        // Home ambient line, e.g. "23° · 48%" (or "--° · --%" with no sensor).
        if (clockWeatherLabel) {
            char wBuf[24];
            if (_sensorValid) {
                float dT = (tempUnit == 'F') ? (_tempC * 9.0f / 5.0f + 32.0f) : _tempC;
                snprintf(wBuf, sizeof(wBuf), "%d\xC2\xB0 \xC2\xB7 %d%%", (int)roundf(dT), _humidityPct);
            } else {
                snprintf(wBuf, sizeof(wBuf), "--\xC2\xB0 \xC2\xB7 --%%");
            }
            lv_label_set_text(clockWeatherLabel, wBuf);
        }

        char timeBuf[12];
        if (is24h) snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", t.tm_hour, t.tm_min);
        else {
            int hour12 = t.tm_hour % 12; if (hour12 == 0) hour12 = 12;
            snprintf(timeBuf, sizeof(timeBuf), "%d:%02d %s", hour12, t.tm_min, t.tm_hour < 12 ? "AM" : "PM");
        }
        lv_label_set_text(clockTimeLabel, timeBuf);

        char dateBuf[24];
        const char* days[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
        if (storage.getDateFormat() == "DD/MM")
            snprintf(dateBuf, sizeof(dateBuf), "%s %02d.%02d.%04d", days[t.tm_wday], t.tm_mday, t.tm_mon+1, t.tm_year+1900);
        else
            snprintf(dateBuf, sizeof(dateBuf), "%s %02d.%02d.%04d", days[t.tm_wday], t.tm_mon+1, t.tm_mday, t.tm_year+1900);
        lv_label_set_text(clockDateLabel, dateBuf);

        // Alarm label on clock screen: colour and text reflect today's status
        char alarmBuf[28];
        if (!alarmOn) {
            snprintf(alarmBuf, sizeof(alarmBuf), "\xE2\x8F\xB0 off");
            lv_obj_set_style_text_color(clockAlarmLabel, lv_color_hex(0x6e7280), 0);
        } else if (!todayOn) {
            snprintf(alarmBuf, sizeof(alarmBuf), "\xE2\x8F\xB0 %02d:%02d (not today)",
                     storage.getAlarmHour(), storage.getAlarmMinute());
            lv_obj_set_style_text_color(clockAlarmLabel, lv_color_hex(0x9a9a9e), 0);
        } else {
            snprintf(alarmBuf, sizeof(alarmBuf), "\xE2\x8F\xB0 %02d:%02d",
                     storage.getAlarmHour(), storage.getAlarmMinute());
            lv_obj_set_style_text_color(clockAlarmLabel, lv_color_hex(0xe8b339), 0);
        }
        lv_label_set_text(clockAlarmLabel, alarmBuf);
    }

    static void onOtaBadgeTapped(lv_event_t* e) {
        UiManager* self = (UiManager*)lv_event_get_user_data(e);
        const char* version = (const char*)lv_obj_get_user_data(lv_event_get_current_target(e));

        lv_obj_t* card = openModal(lv_layer_top());
        lv_obj_t* title = lv_label_create(card);
        char titleText[32]; snprintf(titleText, sizeof(titleText), "UPDATE TO %s", version ? version : "NEW VERSION");
        lv_label_set_text(title, titleText);
        lv_obj_set_style_text_color(title, lv_color_hex(0x3aff7a), 0);

        lv_obj_t* body = lv_label_create(card);
        lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(body, 280);
        lv_label_set_text(body,
            "The new firmware will download and install automatically. "
            "Your device will restart once — takes about 30 seconds total. "
            "All your settings are preserved.");
        lv_obj_set_style_text_color(body, lv_color_hex(0x9a9a9e), 0);

        lv_obj_t* row = lv_obj_create(card);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_bg_opa(row, LV_OPA_0, 0);
        lv_obj_set_style_border_width(row, 0, 0);

        lv_obj_t* cancelBtn = addModalButton(row, "LATER", false);
        lv_obj_t* installBtn = addModalButton(row, "INSTALL NOW", true);

        static lv_obj_t* sCard; sCard = card;
        static UiManager* sSelf; sSelf = self;

        lv_obj_add_event_cb(cancelBtn, [](lv_event_t*) {
            closeModal(sCard);
        }, LV_EVENT_CLICKED, nullptr);

        lv_obj_add_event_cb(installBtn, [](lv_event_t*) {
            closeModal(sCard);
            // Show a "Downloading..." label before handing off to main.cpp
            lv_obj_t* splash = lv_obj_create(lv_layer_top());
            lv_obj_set_size(splash, LV_PCT(100), LV_PCT(100));
            lv_obj_set_style_bg_color(splash, lv_color_hex(0x000000), 0);
            lv_obj_center(splash);
            lv_obj_t* dlLabel = lv_label_create(splash);
            lv_label_set_text(dlLabel, "Downloading update...\nDo not turn off.");
            lv_obj_set_style_text_color(dlLabel, lv_color_hex(0x3aff7a), 0);
            lv_obj_center(dlLabel);
            lv_timer_handler(); // force a redraw so the splash is visible
            if (sSelf->onOtaInstallConfirmed) sSelf->onOtaInstallConfirmed();
        }, LV_EVENT_CLICKED, nullptr);
    }

    static void onAlarmLabelTapped(lv_event_t* e) { ((UiManager*)lv_event_get_user_data(e))->openAlarmPicker(); }
    static void onAlarmIconTapped(lv_event_t* e)  { ((UiManager*)lv_event_get_user_data(e))->openAlarmPicker(); }
    static void onLogoTapped(lv_event_t* e) { ((UiManager*)lv_event_get_user_data(e))->showScreen(ScreenId::CLOCK); }
    static void onDateTapped(lv_event_t* e) { ((UiManager*)lv_event_get_user_data(e))->openCalendarPopup(); }
    static void onQrTapped(lv_event_t* e) { ((UiManager*)lv_event_get_user_data(e))->openConfigPopup(); }
    static void onDebtRangeTapped(lv_event_t* e) { ((UiManager*)lv_event_get_user_data(e))->openDebtRangePicker(); }
    static void onSinceBtnTapped(lv_event_t* e) { ((UiManager*)lv_event_get_user_data(e))->openSincePeriodPicker(); }
    static void onRateBtnTapped(lv_event_t* e) { ((UiManager*)lv_event_get_user_data(e))->openRateUnitPicker(); }
    static void onGameYearsTapped(lv_event_t* e) { ((UiManager*)lv_event_get_user_data(e))->openGameYearsPicker(); }
    static void onVerifyBadgeTapped(lv_event_t* e) { ((UiManager*)lv_event_get_user_data(e))->openVerifyTooltip(); }

    void openAlarmPicker() {
        lv_obj_t* card = openModal(lv_scr_act());

        lv_obj_t* title = lv_label_create(card);
        lv_label_set_text(title, "SET ALARM");
        lv_obj_set_style_text_color(title, lv_color_hex(0x9a9a9e), 0);

        // ── Day-of-week selector strip ────────────────────────────────────────
        // Layout: 7 squares (32×32) with 4px gap, centred.
        // Display order follows the week-start setting (auto-set from geo-IP,
        // user-overridable): 0 = Sunday first, 1 = Monday first.
        // Storage bitmask: bit0=Mon, bit1=Tue, …, bit6=Sun (ISO).
        // dayOrder[display_pos] = bitmask_bit_index
        bool sunFirst = (storage.getWeekStart() == 0);

        // Display order of bitmask-bit indices
        static const uint8_t dayOrderMon[7] = { 0, 1, 2, 3, 4, 5, 6 }; // Mon…Sun
        static const uint8_t dayOrderSun[7] = { 6, 0, 1, 2, 3, 4, 5 }; // Sun Mon…Sat
        const uint8_t* dayOrder = sunFirst ? dayOrderSun : dayOrderMon;

        // Day labels for each display position
        static const char* dayLabelsMon[7] = { "M","T","W","T","F","S","S" };
        static const char* dayLabelsSun[7] = { "S","M","T","W","T","F","S" };
        const char** dayLabels = sunFirst ? dayLabelsSun : dayLabelsMon;

        // Working copy of the day mask — modified as user taps, saved on SAVE
        static uint8_t sDayMask;
        sDayMask = storage.getAlarmDays();

        lv_obj_t* daysRow = lv_obj_create(card);
        lv_obj_set_size(daysRow, LV_PCT(100), 36);
        lv_obj_set_style_bg_opa(daysRow, LV_OPA_0, 0);
        lv_obj_set_style_border_width(daysRow, 0, 0);
        lv_obj_set_style_pad_all(daysRow, 0, 0);
        lv_obj_set_style_pad_column(daysRow, 4, 0);
        lv_obj_set_flex_flow(daysRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(daysRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        for (int pos = 0; pos < 7; pos++) {
            uint8_t bitIdx = dayOrder[pos];

            lv_obj_t* tile = lv_btn_create(daysRow);
            lv_obj_set_size(tile, 32, 32);
            lv_obj_set_style_radius(tile, 6, 0);
            lv_obj_set_style_pad_all(tile, 0, 0);

            bool active = (sDayMask >> bitIdx) & 1;
            lv_obj_set_style_bg_color(tile, lv_color_hex(active ? 0xe8b339 : 0x1a1a1a), 0);
            lv_obj_set_style_border_color(tile, lv_color_hex(active ? 0xe8b339 : 0x3a3a3a), 0);
            lv_obj_set_style_border_width(tile, 1, 0);

            lv_obj_t* lbl = lv_label_create(tile);
            lv_label_set_text(lbl, dayLabels[pos]);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(active ? 0x000000 : 0x6e7280), 0);
            lv_obj_center(lbl);

            // Store bit index in user data; tile's child label is lbl
            lv_obj_set_user_data(tile, (void*)(intptr_t)bitIdx);
            lv_obj_add_event_cb(tile, [](lv_event_t* ev) {
                lv_obj_t* t = lv_event_get_current_target(ev);
                uint8_t bit = (uint8_t)(intptr_t)lv_obj_get_user_data(t);
                // Toggle bit
                sDayMask ^= (1u << bit);
                bool nowActive = (sDayMask >> bit) & 1;
                lv_obj_set_style_bg_color(t, lv_color_hex(nowActive ? 0xe8b339 : 0x1a1a1a), 0);
                lv_obj_set_style_border_color(t, lv_color_hex(nowActive ? 0xe8b339 : 0x3a3a3a), 0);
                lv_obj_t* childLbl = lv_obj_get_child(t, 0);
                if (childLbl) {
                    lv_obj_set_style_text_color(childLbl,
                        lv_color_hex(nowActive ? 0x000000 : 0x6e7280), 0);
                }
            }, LV_EVENT_CLICKED, nullptr);
        }

        // ── Time picker ───────────────────────────────────────────────────────
        TimePickerRefs refs = addTimePicker(card, storage.getAlarmHour(), storage.getAlarmMinute());

        // ── Alarm on/off toggle ───────────────────────────────────────────────
        static bool sAlarmEnabled;
        sAlarmEnabled = storage.getAlarmEnabled();

        lv_obj_t* toggleRow = lv_obj_create(card);
        lv_obj_set_size(toggleRow, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(toggleRow, LV_OPA_0, 0);
        lv_obj_set_style_border_width(toggleRow, 0, 0);
        lv_obj_set_style_pad_all(toggleRow, 0, 0);
        lv_obj_set_flex_flow(toggleRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(toggleRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* toggleLbl = lv_label_create(toggleRow);
        lv_label_set_text(toggleLbl, "ALARM ON");
        lv_obj_set_style_text_font(toggleLbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(toggleLbl, lv_color_hex(0x9a9a9e), 0);

        lv_obj_t* sw = lv_switch_create(toggleRow);
        lv_obj_set_style_bg_color(sw, lv_color_hex(0xe8b339), LV_PART_INDICATOR | LV_STATE_CHECKED);
        if (sAlarmEnabled) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, [](lv_event_t* ev) {
            sAlarmEnabled = lv_obj_has_state(lv_event_get_target(ev), LV_STATE_CHECKED);
        }, LV_EVENT_VALUE_CHANGED, nullptr);

        // ── Save / Cancel buttons ─────────────────────────────────────────────
        lv_obj_t* btnRow = lv_obj_create(card);
        lv_obj_set_size(btnRow, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_bg_opa(btnRow, LV_OPA_0, 0);
        lv_obj_set_style_border_width(btnRow, 0, 0);

        lv_obj_t* cancelBtn = addModalButton(btnRow, "CANCEL", false);
        lv_obj_t* saveBtn   = addModalButton(btnRow, "SAVE",   true);

        static lv_obj_t* sHourRoller; static lv_obj_t* sMinRoller; static lv_obj_t* sCard;
        sHourRoller = refs.hourRoller; sMinRoller = refs.minuteRoller; sCard = card;

        lv_obj_add_event_cb(saveBtn, [](lv_event_t*) {
            uint8_t h = lv_roller_get_selected(sHourRoller);
            uint8_t m = lv_roller_get_selected(sMinRoller);
            storage.setAlarm(h, m, sAlarmEnabled);
            storage.setAlarmDays(sDayMask);
            closeModal(sCard);
        }, LV_EVENT_CLICKED, nullptr);

        lv_obj_add_event_cb(cancelBtn, [](lv_event_t*) {
            closeModal(sCard);
        }, LV_EVENT_CLICKED, nullptr);
    }

    void openCalendarPopup() {
        lv_obj_t* card = openModal(lv_scr_act());
        lv_calendar_create(card);
        lv_obj_t* closeBtn = addModalButton(card, "CLOSE", false);
        static lv_obj_t* sCard; sCard = card;
        lv_obj_add_event_cb(closeBtn, [](lv_event_t*) { closeModal(sCard); }, LV_EVENT_CLICKED, nullptr);
    }

    void openConfigPopup() {
        lv_obj_t* card = openModal(lv_scr_act());
        lv_obj_t* title = lv_label_create(card);
        lv_label_set_text(title, "DEVICE SETUP");
        lv_obj_set_style_text_color(title, lv_color_hex(0x9a9a9e), 0);

        String setupUrl = "https://turbousd.com/setup/" + storage.getNodeCode();
        addQrCode(card, setupUrl.c_str(), 120);

        lv_obj_t* urlHint = lv_label_create(card);
        lv_label_set_text(urlHint, setupUrl.c_str());
        lv_obj_set_style_text_color(urlHint, lv_color_hex(0x9a9a9e), 0);
        lv_obj_set_style_text_font(urlHint, &lv_font_montserrat_10, 0);

        lv_obj_t* prefsTitle = lv_label_create(card);
        lv_label_set_text(prefsTitle, "DISPLAY PREFERENCES");
        lv_obj_set_style_text_color(prefsTitle, lv_color_hex(0x9a9a9e), 0);

        // Any manual change here locks locale, so the geo-IP autodetect can
        // never later overwrite the user's explicit choice (setLocaleLocked).
        addPrefToggleRow(card, "TEMPERATURE", "\xC2\xB0" "C", "\xC2\xB0" "F",
                          storage.getTempUnit() == 'C',
                          [](bool leftActive){ storage.setTempUnit(leftActive ? 'C' : 'F'); storage.setLocaleLocked(true); });

        addPrefToggleRow(card, "DATE FORMAT", "DD/MM", "MM/DD",
                          storage.getDateFormat() == "DD/MM",
                          [](bool leftActive){ storage.setDateFormat(leftActive ? "DD/MM" : "MM/DD"); storage.setLocaleLocked(true); });

        addPrefToggleRow(card, "TIME FORMAT", "24H", "AM/PM",
                          storage.getTimeFormat() == "24H",
                          [](bool leftActive){ storage.setTimeFormat(leftActive ? "24H" : "AMPM"); storage.setLocaleLocked(true); });

        addPrefToggleRow(card, "WEEK STARTS", "MON", "SUN",
                          storage.getWeekStart() == 1,
                          [](bool leftActive){ storage.setWeekStart(leftActive ? 1 : 0); storage.setLocaleLocked(true); });

        lv_obj_t* closeBtn = addModalButton(card, "CLOSE", false);
        static lv_obj_t* sCard; sCard = card;
        lv_obj_add_event_cb(closeBtn, [](lv_event_t*) { closeModal(sCard); }, LV_EVENT_CLICKED, nullptr);
    }

    void openVerifyTooltip() {
        lv_obj_t* card = openModal(lv_scr_act());
        lv_obj_t* label = lv_label_create(card);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(label, 280);
        lv_label_set_text(label,
            "Verification pending.\n\n"
            "To get verified:\n"
            "1. Post a video on X tagging @turbousd\n"
            "2. Write your node name on paper, show it matches your screen\n"
            "3. Include the wallet holding your TUSD\n"
            "4. We manually review and whitelist your node");
        lv_obj_set_style_text_color(label, lv_color_hex(0x9a9a9e), 0);
        lv_obj_t* closeBtn = addModalButton(card, "GOT IT", false);
        static lv_obj_t* sCard; sCard = card;
        lv_obj_add_event_cb(closeBtn, [](lv_event_t*) { closeModal(sCard); }, LV_EVENT_CLICKED, nullptr);
    }

    void openDebtRangePicker() {
        static const char* options = "5Y\n10Y\n20Y\n30Y\n50Y\n75Y";
        static const int yearValues[] = {5, 10, 20, 30, 50, 75};
        lv_obj_t* card = openModal(lv_scr_act());
        lv_obj_t* roller = addOptionPicker(card, options, debtYearsRangeIndex);
        lv_obj_t* saveBtn = addModalButton(card, "SAVE", true);
        static lv_obj_t* sCard; sCard = card;
        static lv_obj_t* sRoller; sRoller = roller;
        static UiManager* sSelf; sSelf = this;
        lv_obj_add_event_cb(saveBtn, [](lv_event_t*) {
            sSelf->debtYearsRangeIndex = lv_roller_get_selected(sRoller);
            int years = yearValues[sSelf->debtYearsRangeIndex];
            char btnLabel[12]; snprintf(btnLabel, sizeof(btnLabel), "LAST %dY \xE2\x96\xBE", years);
            sSelf->debtScreen.setRangeButtonLabel(btnLabel);
            sSelf->reloadDebtHistory(years);
            closeModal(sCard);
        }, LV_EVENT_CLICKED, nullptr);
    }

    void reloadDebtHistory(int years) {
        DebtHistoryPoint points[40];
        int count = apiClient.fetchDebtHistory(years, points, 40);
        if (count == 0) {
            Serial.println("reloadDebtHistory: no data returned, leaving chart as-is.");
            return;
        }
        lv_chart_set_point_count(debtScreen.getChart(), count);
        for (int i = 0; i < count; i++) {
            // Scale to an integer-friendly range for lv_chart (raw USD values
            // are far too large for lv_coord_t); express in tenths of a
            // trillion so the chart's y-axis still reads meaningfully.
            lv_coord_t scaled = (lv_coord_t)(points[i].totalDebtUsd / 1e11);
            lv_chart_set_next_value(debtScreen.getChart(), debtScreen.getSeries(), scaled);
        }
    }

    void openSincePeriodPicker() {
        static const char* options = "1H\n24H\n7D\n30D\nNODE ON";
        lv_obj_t* card = openModal(lv_scr_act());
        addOptionPicker(card, options, sincePeriodIndex);
        lv_obj_t* saveBtn = addModalButton(card, "SAVE", true);
        static lv_obj_t* sCard; sCard = card;
        lv_obj_add_event_cb(saveBtn, [](lv_event_t*) { closeModal(sCard); }, LV_EVENT_CLICKED, nullptr);
    }

    void openRateUnitPicker() {
        static const char* options = "SEC\nMIN\nHOUR\nDAY";
        lv_obj_t* card = openModal(lv_scr_act());
        addOptionPicker(card, options, rateUnitIndex);
        lv_obj_t* saveBtn = addModalButton(card, "SAVE", true);
        static lv_obj_t* sCard; sCard = card;
        lv_obj_add_event_cb(saveBtn, [](lv_event_t*) { closeModal(sCard); }, LV_EVENT_CLICKED, nullptr);
    }

    void openGameYearsPicker() {
        static const char* options = "1Y\n3Y\n5Y\n10Y\n20Y\n30Y\n50Y\n75Y\n100Y";
        lv_obj_t* card = openModal(lv_scr_act());
        addOptionPicker(card, options, gameYearsIndex);
        lv_obj_t* saveBtn = addModalButton(card, "SAVE", true);
        static lv_obj_t* sCard; sCard = card;
        lv_obj_add_event_cb(saveBtn, [](lv_event_t*) { closeModal(sCard); }, LV_EVENT_CLICKED, nullptr);
    }

    void showScreen(ScreenId id) {
        currentScreen = id;
        // Update swipe position to stay in sync with direct navigation (e.g. logo tap → home)
        for (int i = 0; i < (int)ScreenId::COUNT; i++) {
            if (_swipeOrder[i] == (uint8_t)id) { _currentSwipePos = i; break; }
        }
        lv_scr_load_anim(screens[(int)id], LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
        // Trigger data load when ticker screen becomes visible
        if (id == ScreenId::TICKERS) {
            tickerScreen.onShow(storage.getNodeCode().c_str());
        }
        // Trigger NFT load (wallet prompt or cache refresh) when NFT screen shown
        if (id == ScreenId::NFT) {
            nftScreen.onShow();
        }
    }

    void attachSwipeGesture(lv_obj_t* screen) {
        lv_obj_set_user_data(screen, this);
        lv_obj_add_event_cb(screen, [](lv_event_t* e) {
            lv_obj_t* target = lv_event_get_current_target(e);
            UiManager* self = (UiManager*)lv_obj_get_user_data(target);
            if (!self) return;
            lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
            int count = (int)ScreenId::COUNT;
            if (dir == LV_DIR_LEFT) {
                int newPos = (self->_currentSwipePos + 1) % count;
                self->showScreen((ScreenId)self->_swipeOrder[newPos]);
            } else if (dir == LV_DIR_RIGHT) {
                int newPos = (self->_currentSwipePos - 1 + count) % count;
                self->showScreen((ScreenId)self->_swipeOrder[newPos]);
            }
        }, LV_EVENT_GESTURE, nullptr);
    }
};
