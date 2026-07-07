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
#include "weblog.h"
#include <WiFi.h>          // WiFi.localIP() for the on-screen logs/screenshot URL
#include <Wire.h>
#include <lvgl.h>
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "esp_sleep.h"          // light sleep for the user-button "power off"
#include "board_pins.h"
#include "api_client.h"
#include "rp2040_link.h"   // confirmation chime on alarm save (link self-test)
#include "storage.h"
#include "screenshot_server.h"   // flush_cb mirrors frames into its shadow fb

// Large clock font (Montserrat 72px, digits + ":" + AM/PM) — assets/montserrat_clock.c
LV_FONT_DECLARE(montserrat_clock);
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
        showScreen(ScreenId::CLOCK, false);  // instant first load — no animation
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

    // Read brightness from NVS and apply it immediately. No-op while the
    // screen is off: the post-heartbeat call used to RELIGHT a screen the
    // user had just turned off (short press / timeout), every ~3 minutes.
    void applyStoredBrightness() {
        if (!_screenOn) return;
        setScreenBrightness(storage.getScreenBrightness());
    }

    // ── User button actions (top button, GPIO 38) ──────────────────────────
    // Short press: toggle the screen (backlight off/on) while the node keeps
    // running and mining in the background.
    void toggleScreen() {
        _screenOn = !_screenOn;
        if (_screenOn) applyStoredBrightness();
        else           ledcWrite(0, 0);   // backlight off (channel 0)
    }

    // Long press (3 s): "power off" — backlight off and enter light sleep. RAM
    // and the LVGL framebuffer are retained, so a press resumes instantly where
    // it left off. We deliberately do NOT wire a factory-reset action to the
    // button, so the firmware can't be wiped by accident. GPIO 38 isn't an RTC
    // pin, so this uses light sleep (any-GPIO wake), not deep sleep.
    void enterSleep() {
        // With a USB host attached (web/serial console open), light sleep
        // drops the native USB-CDC connection — the console shows "device has
        // been lost" and reconnects find nothing until a wake. Degrade to a
        // plain screen-off while plugged into a computer, so logs keep
        // flowing; on a wall charger real light sleep still happens.
        if (Serial) {
            _screenOn = false;
            ledcWrite(0, 0);
            Log.println("sleep: USB host attached — screen off only (no light sleep)");
            return;
        }
        ledcWrite(0, 0);                         // backlight off
        gpio_wakeup_enable((gpio_num_t)BTN_USER_GPIO, GPIO_INTR_LOW_LEVEL);
        esp_sleep_enable_gpio_wakeup();
        // The main loop is FROZEN during light sleep — without a timer wake
        // the alarm could never fire while "powered off". Arm a wake ~2 s
        // before the next scheduled alarm so checkAlarmTrigger() catches its
        // firing window right after resume.
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
        uint64_t usToAlarm = _usUntilNextAlarm();
        if (usToAlarm > 0) esp_sleep_enable_timer_wakeup(usToAlarm);
        esp_light_sleep_start();                 // blocks until button press or alarm-time wake
        // Woken up (button OR alarm timer): restore the screen.
        _screenOn = true;
        applyStoredBrightness();
    }

    // Microseconds until ~2 s before the next enabled alarm occurrence
    // (respecting the per-day bitmask), or 0 when no alarm is scheduled /
    // the clock isn't synced yet.
    uint64_t _usUntilNextAlarm() {
        if (!storage.getAlarmEnabled()) return 0;
        time_t now = time(nullptr);
        if (now < 1600000000) return 0;          // NTP not synced — can't schedule
        struct tm t;
        localtime_r(&now, &t);
        for (int d = 0; d < 8; d++) {
            struct tm c = t;
            c.tm_mday += d;
            c.tm_hour  = storage.getAlarmHour();
            c.tm_min   = storage.getAlarmMinute();
            c.tm_sec   = 0;
            time_t ct = mktime(&c);              // normalizes day rollover
            if (ct <= now + 3) continue;         // already passed (or this very moment)
            struct tm cd;
            localtime_r(&ct, &cd);
            if (!storage.isAlarmActiveToday(cd.tm_wday)) continue;
            uint64_t secs = (uint64_t)(ct - now);
            return (secs > 2 ? secs - 2 : 1) * 1000000ULL;
        }
        return 0;
    }

    void showProvisioningScreen() {
        static bool shown = false;
        if (shown) return;
        shown = true;
        // Build a dedicated screen and load it instantly. We do NOT clean the
        // currently-active screen (cleaning a screen that may still be mid-load
        // animation corrupts LVGL and crashes → reset loop).
        lv_obj_t* scr = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
        lv_obj_t* label = lv_label_create(scr);
        lv_label_set_text(label, "Connect your phone to:\nTurboUSD-Setup-XXXX\nthen open the page that appears.");
        lv_obj_set_style_text_color(label, lv_color_hex(0x3aff7a), 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(label);
        lv_scr_load(scr);
    }

    bool isOnNodeScreen() { return currentScreen == ScreenId::NODE_NETWORK; }

    // RGB panel handle — the screenshot server reads its PSRAM framebuffer.
    esp_lcd_panel_handle_t lcdPanel() const { return _lcdPanel; }

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
        snprintf(text, sizeof(text), "\xEF\x81\xB7 Firmware %s available — tap to install", version);
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

    // Set by the config popup's "Check for updates" button; the main loop polls
    // it, runs the OTA check on the network thread, and then either shows the
    // install badge (update found) or a transient "up to date" bar.
    volatile bool otaCheckRequested = false;

    // Transient bottom bar for a one-off OTA message (auto-dismisses ~4 s). Not
    // shown if an install badge is already up.
    void showOtaInfo(const char* msg) {
        if (_otaBadge) return;
        lv_obj_t* bar = lv_obj_create(lv_layer_top());
        lv_obj_set_size(bar, LV_PCT(100), 40);
        lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0x141414), 0);
        lv_obj_set_style_border_color(bar, lv_color_hex(0x3a3a3a), 0);
        lv_obj_set_style_border_width(bar, 1, 0);
        lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, 0);
        lv_obj_t* l = lv_label_create(bar);
        lv_label_set_text(l, msg);
        lv_obj_set_style_text_color(l, lv_color_hex(0xd8d8dc), 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
        lv_obj_center(l);
        lv_timer_t* t = lv_timer_create([](lv_timer_t* tm) {
            lv_obj_t* b = (lv_obj_t*)tm->user_data;
            if (b) lv_obj_del(b);
            lv_timer_del(tm);
        }, 4000, bar);
        lv_timer_set_repeat_count(t, 1);
    }

    // Set this before begin(). Called when user confirms "Install".
    std::function<void()> onOtaInstallConfirmed;

    // ── NFT fullscreen ("digital photo frame") ────────────────────────────────
    // A double-press of the top button toggles it — but only while the NFT
    // screen is showing (or to exit once you're in it). main.cpp only routes a
    // double-press to us when nftDoublePressActive() is true.
    bool _nftFullscreen = false;
    bool nftDoublePressActive() const { return currentScreen == ScreenId::NFT || _nftFullscreen; }
    void toggleNftFullscreen() {
        if (currentScreen != ScreenId::NFT && !_nftFullscreen) return;
        _nftFullscreen = !_nftFullscreen;
        nftScreen.setFullscreen(_nftFullscreen);
    }

    bool miningFeedNeedsRefresh() {
        uint32_t now = millis();
        if (now - lastMiningFeedFetch < MINING_FEED_REFRESH_MS) return false;
        lastMiningFeedFetch = now;
        return true;
    }

    // A failed feed fetch shouldn't cost the whole refresh window — pull the
    // next attempt in to ~3 s so a transient TLS/memory hiccup self-heals fast.
    void retryMiningFeedSoon() {
        lastMiningFeedFetch = millis() - MINING_FEED_REFRESH_MS + 3000;
    }

    void updateTreasuryData(const TreasuryData& data) {
        latestTreasury = data;
        turboScreen.updateData(data);
    }

    void loadOhlcvChart(OhlcvCandle* candles, int count) {
        turboScreen.loadRealCandles(candles, count, turboGroupDays());
    }

    // Turbo chart timeframe (1D/1W/1M dropdown) → candle grouping for
    // fetchOhlcvHistory(). 12 monthly bars keep the GT daily fetch at 360 rows.
    int turboGroupDays() { return turboScreen.timeframeSel == 0 ? 1 : (turboScreen.timeframeSel == 1 ? 7 : 30); }
    int turboBars()      { return turboScreen.timeframeSel == 2 ? 12 : 26; }
    bool turboTfConsumeDirty() {
        if (!turboScreen.tfDirty) return false;
        turboScreen.tfDirty = false;
        return true;
    }

    // Retry hook for main.cpp: true once the debt chart has real data.
    bool debtHistLoaded() const { return _debtHistLoaded; }
    void retryDebtHistory() {
        static const int yearValues[] = {5, 10, 20, 30, 50, 75};
        reloadDebtHistory(yearValues[debtYearsRangeIndex % 6]);
    }

    // Live TUSD price from DexScreener/GeckoTerminal (independent of the
    // treasury service, which may be down / not deployed).
    void updateTusdPrice(double priceUsd) {
        turboScreen.updatePrice(priceUsd);
    }

    void updateDebtData(const DebtData& data) {
        latestDebt = data;
        debtScreen.updateLiveTotal(data.totalDebtUsd);
        // Load the historical chart + SINCE/RATE once, on the first live total.
        if (!_debtHistLoaded) {
            static const int yearValues[] = {5, 10, 20, 30, 50, 75};
            reloadDebtHistory(yearValues[debtYearsRangeIndex % 6]);
        }
    }

    // Re-apply screen order/visibility after a heartbeat config sync (the
    // web's eye toggles otherwise only took effect after a reboot).
    void reloadScreenOrder() { _loadScreenOrder(); }

    void updateLeaderboard(LeaderboardEntry* entries, int count) {
        nodeScreen.updateLeaderboard(entries, count);
        // The footers' "| N NODES" counter feeds off the same directory data
        // (it used to sit at 0 forever — nothing ever set it).
        int online = 0;
        for (int i = 0; i < count; i++) if (entries[i].online) online++;
        _onlineNodeCount = online;
    }

    void updateMiningFeed(MiningFeedEntry* entries, int count) {
        nodeScreen.updateMiningFeed(entries, count);
        // Remember when the countdown started, for the per-second ring/strip:
        // preferred = the pending block's created_at; fallback (same rule the
        // web uses, and what keeps this working if the DB view lacks
        // created_at) = the newest mined block's mined_at.
        time_t newestMined = 0;
        for (int i = 0; i < count; i++) {
            if (entries[i].mined && entries[i].minedAtUtc > newestMined)
                newestMined = entries[i].minedAtUtc;
            if (!entries[i].mined) {
                if (entries[i].createdAtUtc > 0) _pendingBlockCreatedAt = entries[i].createdAtUtc;
                _pendingBlockReward = entries[i].rewardTusd > 0 ? entries[i].rewardTusd : 100.0;
            }
        }
        if (_pendingBlockCreatedAt == 0 && newestMined > 0)
            _pendingBlockCreatedAt = newestMined;
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
        // The alarm must ALWAYS light the screen — wake it from the
        // button-off state and from the idle-timeout state, and reset the
        // inactivity timer so it doesn't blank again mid-ring.
        _screenOn    = true;
        _screenIsOn  = true;
        _lastTouchMs = millis();
        applyStoredBrightness();

        lv_obj_t* overlay = lv_obj_create(lv_scr_act());
        lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(overlay, lv_color_hex(ALARM_GREEN), 0);
        lv_obj_set_style_border_width(overlay, 0, 0);
        lv_obj_set_style_pad_all(overlay, 0, 0);
        lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_center(overlay);

        // Big "TURBO ALARM!" filling the background, repeated, flashing.
        lv_obj_t* bgText = lv_label_create(overlay);
        lv_label_set_text(bgText,
            "TURBO ALARM!\nTURBO ALARM!\nTURBO ALARM!\nTURBO ALARM!\nTURBO ALARM!\nTURBO ALARM!");
        lv_obj_set_style_text_font(bgText, &lv_font_montserrat_32, 0);
        lv_obj_set_style_text_align(bgText, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_line_space(bgText, 16, 0);
        lv_obj_set_style_text_color(bgText, lv_color_hex(0xffffff), 0);
        lv_obj_center(bgText);

        lv_obj_t* stopBtn = lv_btn_create(overlay);
        lv_obj_set_size(stopBtn, 190, 74);
        lv_obj_set_style_radius(stopBtn, 37, 0);
        lv_obj_set_style_bg_color(stopBtn, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_border_width(stopBtn, 3, 0);
        lv_obj_set_style_border_color(stopBtn, lv_color_hex(0x000000), 0);
        lv_obj_set_style_shadow_width(stopBtn, 24, 0);
        lv_obj_set_style_shadow_color(stopBtn, lv_color_hex(0x000000), 0);
        lv_obj_set_style_shadow_opa(stopBtn, LV_OPA_40, 0);
        lv_obj_center(stopBtn);
        lv_obj_add_event_cb(stopBtn, [](lv_event_t* e) {
            UiManager* self = (UiManager*)lv_event_get_user_data(e);
            self->dismissAlarmOverlay();
        }, LV_EVENT_CLICKED, this);
        lv_obj_t* stopLabel = lv_label_create(stopBtn);
        lv_label_set_text(stopLabel, "STOP");
        lv_obj_set_style_text_font(stopLabel, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(stopLabel, lv_color_hex(0x000000), 0);
        lv_obj_center(stopLabel);

        alarmOverlay      = overlay;
        _alarmBgText      = bgText;
        _alarmStopBtn     = stopBtn;
        _alarmStopLbl     = stopLabel;
        _alarmBlinkPhase  = 0;
        // Blink green ↔ full white ~2x/s so it's unmissable in a dark room.
        _alarmBlinkTimer  = lv_timer_create(_alarmBlinkCb, 430, this);
    }

    // Alarm-overlay blink: alternate green ↔ white, flipping the big text and the
    // STOP button so they stay legible on either background.
    static void _alarmBlinkCb(lv_timer_t* t) {
        UiManager* self = (UiManager*)t->user_data;
        if (!self || !self->alarmOverlay) return;
        self->_alarmBlinkPhase ^= 1;
        bool green = (self->_alarmBlinkPhase == 0);
        uint32_t bg    = green ? ALARM_GREEN : 0xffffff;
        uint32_t big   = green ? 0xffffff    : ALARM_GREEN;
        uint32_t btnBg = green ? 0xffffff    : ALARM_GREEN;
        uint32_t btnFg = green ? 0x000000    : 0xffffff;
        lv_obj_set_style_bg_color(self->alarmOverlay, lv_color_hex(bg), 0);
        if (self->_alarmBgText)  lv_obj_set_style_text_color(self->_alarmBgText, lv_color_hex(big), 0);
        if (self->_alarmStopBtn) {
            lv_obj_set_style_bg_color(self->_alarmStopBtn, lv_color_hex(btnBg), 0);
            lv_obj_set_style_border_color(self->_alarmStopBtn, lv_color_hex(btnFg), 0);
        }
        if (self->_alarmStopLbl) lv_obj_set_style_text_color(self->_alarmStopLbl, lv_color_hex(btnFg), 0);
    }

    std::function<void()> onAlarmDismissed;

    // True while the yellow "alarm firing" overlay is up (i.e. the user hasn't
    // tapped STOP yet). main.cpp uses this to periodically re-send PLAY_ALARM
    // to the RP2040, so one lost UART frame can't result in a silent alarm.
    bool isAlarmOverlayActive() const { return alarmOverlay != nullptr; }

    // Public entry point for the physical top button: stop the ringing alarm
    // exactly like tapping the on-screen STOP.
    void stopRingingAlarm() { dismissAlarmOverlay(); }

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
    // Alarm overlay blink (green ↔ white) + big "TURBO ALARM!" background.
    static constexpr uint32_t ALARM_GREEN = 0x43e397;   // matches the clock digits
    lv_obj_t*    _alarmBgText     = nullptr;
    lv_obj_t*    _alarmStopBtn    = nullptr;
    lv_obj_t*    _alarmStopLbl    = nullptr;
    lv_timer_t*  _alarmBlinkTimer = nullptr;
    uint8_t      _alarmBlinkPhase = 0;

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
    int     _swipeCount = (int)ScreenId::COUNT;   // visible screens (hidden ones filtered out)
    int     _currentSwipePos = 0;

    // Swipe-vs-tap disambiguation. The touch read_cb records where a press
    // started and, if the finger travels past TAP_SLOP_PX before release, marks
    // the whole gesture as a swipe. Popup-opening handlers (alarm, calendar,
    // gear, …) consult _touchWasSwipe and bail, so a swipe that HAPPENS to start
    // on an icon no longer also fires that icon's click — which used to open the
    // popup on the screen you just swiped to. LVGL doesn't cancel CLICKED on
    // movement by itself (the icons aren't in a scrollable container), so we
    // gate it explicitly here.
    static constexpr lv_coord_t TAP_SLOP_PX = 22;
    bool       _pressActive  = false;
    bool       _touchWasSwipe = false;
    lv_coord_t _pressStartX  = 0;
    lv_coord_t _pressStartY  = 0;

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
    int gameYearsIndex = 0;            // 0 = REAL TIME (default), then 1Y…100Y
    bool   _gameRtActive   = false;    // real-time game mode currently driving the chart
    double _gameRtBaseline = 0.0;      // dollar value mapped to chart unit 0
    int    _gameRtCount    = 0;        // points drawn so far (fill phase: left → right)
    double _debtPerSecond = 0.0;   // US-debt-clock rate, derived from history
    bool   _debtHistLoaded = false;
    bool   _debtRangeDirty = false; // range picker changed → refetch history (debounced)
    time_t _pendingBlockCreatedAt = 0;   // pending mining block opened at (UTC)
    double _pendingBlockReward    = 100.0;
    double _annualDebasementRate = 0.08;  // yearly $ debasement for the inflation game,
                                          // derived from real debt growth (fallback 8%)

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

    bool  _screenOn     = true;   // user-button screen toggle state
    int   _onlineNodeCount = 0;   // total online nodes in the network (footer "| N NODES")

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
        _applyHiddenScreens();
    }

    // Drop hidden screens (web setup "eye" toggles / screen_hidden CSV) from
    // the rotation. Home/CLOCK can never be hidden.
    void _applyHiddenScreens() {
        String hid = storage.getScreenHidden();
        uint8_t filtered[(int)ScreenId::COUNT];
        int fc = 0;
        for (int i = 0; i < (int)ScreenId::COUNT; i++) {
            uint8_t id = _swipeOrder[i];
            bool hidden = false;
            if (id != (uint8_t)ScreenId::CLOCK && hid.length()) {
                int idx2 = 0, len = hid.length();
                while (idx2 < len) {
                    int sep = hid.indexOf(',', idx2);
                    if (sep < 0) sep = len;
                    if (hid.substring(idx2, sep).toInt() == (int)id) { hidden = true; break; }
                    idx2 = sep + 1;
                }
            }
            if (!hidden) filtered[fc++] = id;
        }
        if (fc < 1) { filtered[0] = (uint8_t)ScreenId::CLOCK; fc = 1; }
        memcpy(_swipeOrder, filtered, fc);
        _swipeCount = fc;
        if (_currentSwipePos >= _swipeCount) _currentSwipePos = 0;
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
        panelCfg.timings.flags.pclk_active_neg = 0;  // matches Seeed's verified Arduino
                                                     // reference (Arduino_GFX uses the
                                                     // default 0 for this panel).
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
            // Mirror into the screenshot shadow framebuffer (http://<ip>/shot.bmp).
            screenshot::mirror(area, colorP);
            lv_disp_flush_ready(drv);
        };
        lv_disp_drv_register(&dispDrv);
        screenshot::ensureShadow();

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
                    // The FT6336U is mounted rotated 180° vs the panel: touches
                    // landed at the diagonally-opposite point (tapping the top-left
                    // date opened the bottom-right gear's popup, etc.). Mirror BOTH
                    // X and Y so taps hit their targets and swipes go the right way.
                    lv_coord_t rawX = (lv_coord_t)((xh << 8) | xl);
                    lv_coord_t rawY = (lv_coord_t)((yh << 8) | yl);
                    data->point.x = (LCD_H_RES - 1) - rawX;
                    data->point.y = (LCD_V_RES - 1) - rawY;
                    data->state   = LV_INDEV_STATE_PRESSED;
                    // Swipe-vs-tap tracking: remember where the press began; once
                    // the finger travels past TAP_SLOP_PX, flag the gesture as a
                    // swipe so popup handlers ignore the trailing click.
                    UiManager* ui = static_cast<UiManager*>(drv->user_data);
                    if (!ui->_pressActive) {
                        ui->_pressActive   = true;
                        ui->_touchWasSwipe = false;
                        ui->_pressStartX   = data->point.x;
                        ui->_pressStartY   = data->point.y;
                    } else {
                        lv_coord_t dx = data->point.x - ui->_pressStartX;
                        lv_coord_t dy = data->point.y - ui->_pressStartY;
                        if (dx*dx + dy*dy > TAP_SLOP_PX*TAP_SLOP_PX) ui->_touchWasSwipe = true;
                    }
                    // Any touch resets the inactivity timer (and wakes screen if off)
                    ui->_onTouchActivity();
                } else {
                    // Release: keep _touchWasSwipe intact so the CLICKED handler
                    // that fires right after can read it; clear the active flag so
                    // the next press starts a fresh gesture.
                    static_cast<UiManager*>(drv->user_data)->_pressActive = false;
                    data->state = LV_INDEV_STATE_RELEASED;
                }
            } else {
                static_cast<UiManager*>(drv->user_data)->_pressActive = false;
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
        if (_alarmBlinkTimer) { lv_timer_del(_alarmBlinkTimer); _alarmBlinkTimer = nullptr; }
        if (alarmOverlay) { lv_obj_del(alarmOverlay); alarmOverlay = nullptr; }
        _alarmBgText = _alarmStopBtn = _alarmStopLbl = nullptr;
        if (onAlarmDismissed) onAlarmDismissed();
    }

    void buildAllScreens() {
        screens[(int)ScreenId::CLOCK] = buildClockScreen();

        screens[(int)ScreenId::TURBO_STATS] = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(screens[(int)ScreenId::TURBO_STATS], lv_color_black(), 0);
        turboScreen.build(screens[(int)ScreenId::TURBO_STATS], onLogoTapped, onDateTapped, onQrTapped, this);
        _wireAlarmIcon(turboScreen.header);
        _wireFooterNav(turboScreen.footer);

        screens[(int)ScreenId::DEBT] = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(screens[(int)ScreenId::DEBT], lv_color_black(), 0);
        debtScreen.build(screens[(int)ScreenId::DEBT], onLogoTapped, onDateTapped, onQrTapped,
                          onDebtRangeTapped, onSinceBtnTapped, onRateBtnTapped, this);
        _wireAlarmIcon(debtScreen.header);
        _wireFooterNav(debtScreen.footer);

        screens[(int)ScreenId::INFLATION_GAME] = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(screens[(int)ScreenId::INFLATION_GAME], lv_color_black(), 0);
        gameScreen.build(screens[(int)ScreenId::INFLATION_GAME], onLogoTapped, onDateTapped, onQrTapped, onGameYearsTapped, this);
        _wireAlarmIcon(gameScreen.header);
        _wireFooterNav(gameScreen.footer);

        screens[(int)ScreenId::NODE_NETWORK] = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(screens[(int)ScreenId::NODE_NETWORK], lv_color_black(), 0);
        nodeScreen.build(screens[(int)ScreenId::NODE_NETWORK], onLogoTapped, onDateTapped, onQrTapped, onVerifyBadgeTapped, this);
        _wireAlarmIcon(nodeScreen.header);
        _wireFooterNav(nodeScreen.footer);

        screens[(int)ScreenId::NFT] = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(screens[(int)ScreenId::NFT], lv_color_black(), 0);
        nftScreen.build(screens[(int)ScreenId::NFT], onLogoTapped, onDateTapped, onQrTapped, this);
        _wireAlarmIcon(nftScreen.header);
        _wireFooterNav(nftScreen.footer);

        screens[(int)ScreenId::TICKERS] = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(screens[(int)ScreenId::TICKERS], lv_color_black(), 0);
        tickerScreen.build(screens[(int)ScreenId::TICKERS], onLogoTapped, onDateTapped, onQrTapped, this);
        _wireAlarmIcon(tickerScreen.header);
        _wireFooterNav(tickerScreen.footer);
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

    // Footer navigation: tapping the node name/ID, the separator or the
    // "N NODES" counter jumps straight to the Network screen.
    void _wireFooterNav(SharedFooterRefs& ftr) {
        lv_obj_t* targets[3] = { ftr.nodeNameLabel, ftr.nodeSepLabel, ftr.nodeCountLabel };
        for (int i = 0; i < 3; i++) {
            if (!targets[i]) continue;
            lv_obj_add_flag(targets[i], LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_ext_click_area(targets[i], 8);   // small text → forgiving target
            lv_obj_add_event_cb(targets[i], [](lv_event_t* e) {
                UiManager* self = (UiManager*)lv_event_get_user_data(e);
                if (self && self->currentScreen != ScreenId::NODE_NETWORK)
                    self->showScreen(ScreenId::NODE_NETWORK);
            }, LV_EVENT_CLICKED, this);
        }
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
        lv_img_set_zoom(logo, 170);   // ~32px
        lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 10);

        // Date — larger, letter-spaced; tap opens the calendar.
        clockDateLabel = lv_label_create(scr);
        lv_obj_set_style_text_color(clockDateLabel, lv_color_hex(0xcfcfd4), 0);
        lv_obj_set_style_text_font(clockDateLabel, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_letter_space(clockDateLabel, 3, 0);
        lv_obj_align(clockDateLabel, LV_ALIGN_CENTER, 0, -84);
        lv_obj_add_flag(clockDateLabel, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(clockDateLabel, 12);
        lv_obj_add_event_cb(clockDateLabel, onDateTapped, LV_EVENT_CLICKED, this);

        // Time — big custom Montserrat font.
        clockTimeLabel = lv_label_create(scr);
        lv_obj_set_style_text_color(clockTimeLabel, lv_color_hex(0x3aff7a), 0);
        lv_obj_set_style_text_font(clockTimeLabel, &montserrat_clock, 0);
        lv_obj_align(clockTimeLabel, LV_ALIGN_CENTER, 0, -6);

        // Alarm — rounded pill; colour (border + text) follows the alarm state,
        // set each second in updateClockIfNeeded (yellow = on, grey = off).
        clockAlarmLabel = lv_label_create(scr);
        lv_obj_set_style_text_font(clockAlarmLabel, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(clockAlarmLabel, lv_color_hex(0x6e7280), 0);
        lv_obj_set_style_radius(clockAlarmLabel, 16, 0);
        lv_obj_set_style_border_width(clockAlarmLabel, 2, 0);
        lv_obj_set_style_border_color(clockAlarmLabel, lv_color_hex(0x6e7280), 0);
        lv_obj_set_style_pad_hor(clockAlarmLabel, 16, 0);
        lv_obj_set_style_pad_ver(clockAlarmLabel, 7, 0);
        lv_obj_align(clockAlarmLabel, LV_ALIGN_CENTER, 0, 78);
        lv_obj_add_flag(clockAlarmLabel, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(clockAlarmLabel, 10);
        lv_obj_add_event_cb(clockAlarmLabel, onAlarmLabelTapped, LV_EVENT_CLICKED, this);

        // Ambient temp/humidity line, e.g. "23° · 48%" (from the RP2040 AHT20).
        clockWeatherLabel = lv_label_create(scr);
        lv_obj_set_style_text_color(clockWeatherLabel, lv_color_hex(0x9a9a9e), 0);
        lv_obj_set_style_text_font(clockWeatherLabel, &lv_font_montserrat_16, 0);
        lv_obj_align(clockWeatherLabel, LV_ALIGN_CENTER, 0, 64);
        lv_label_set_text(clockWeatherLabel, "--\xC2\xB0 \xE2\x80\xA2 --%");
        lv_obj_add_flag(clockWeatherLabel, LV_OBJ_FLAG_HIDDEN);  // hidden per design; code kept

        clockFooterRefs = buildSharedFooter(scr, onQrTapped, this);
        _wireFooterNav(clockFooterRefs);

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

        // Refresh every footer (node name/code + "| N NODES"). Prefer the
        // DISPLAY NAME (synced from the backend on each heartbeat); fall back
        // to the node code, then to a placeholder for unregistered devices.
        String footerName = storage.getDisplayName();
        if (footerName.length() == 0) footerName = storage.getNodeCode();
        if (footerName.length() == 0) footerName = "NEW NODE";
        refreshSharedFooter(clockFooterRefs,     footerName, _onlineNodeCount);
        refreshSharedFooter(turboScreen.footer,  footerName, _onlineNodeCount);
        refreshSharedFooter(debtScreen.footer,   footerName, _onlineNodeCount);
        refreshSharedFooter(gameScreen.footer,   footerName, _onlineNodeCount);
        refreshSharedFooter(nodeScreen.footer,   footerName, _onlineNodeCount);
        refreshSharedFooter(tickerScreen.footer, footerName, _onlineNodeCount);
        refreshSharedFooter(nftScreen.footer,    footerName, _onlineNodeCount);

        // Tick the SINCE/RATE figures once per second: SINCE keeps growing
        // with elapsed time (visibly for NODE ON, whose value is shown in $k
        // with two decimals precisely so the climb is watchable live).
        if (_debtHistLoaded) _computeDebtDerived();

        // Debounced debt-range refetch (set by the range picker's live-apply).
        if (_debtRangeDirty) {
            _debtRangeDirty = false;
            static const int yearValues[] = {5, 10, 20, 30, 50, 75};
            reloadDebtHistory(yearValues[debtYearsRangeIndex % 6]);
        }

        // Real-time inflation game: tick once per second while visible.
        if (currentScreen == ScreenId::INFLATION_GAME && _gameRtActive)
            _tickGameRealtime();

        // Real mining countdown (pending block opened_at + 1 h), once per tick.
        if (_pendingBlockCreatedAt > 0) {
            time_t nowUtc = time(nullptr);
            if (nowUtc > 1600000000) {   // only once NTP has synced
                long secsLeft = (long)(_pendingBlockCreatedAt + 3600 - nowUtc);
                nodeScreen.updateCountdown(secsLeft, _pendingBlockReward);
            }
        }

        // Node & Network headline (name, badge, rewards hint, uptime) — the
        // identity fields sync down from the backend on each heartbeat (see
        // applyServerConfig); uptime counts from boot. Refresh every 30 s.
        static uint32_t lastNodeStatusAt = 0;
        if (lastNodeStatusAt == 0 || millis() - lastNodeStatusAt > 30000) {
            lastNodeStatusAt = millis();
            String nm = storage.getDisplayName();
            if (nm.length() == 0) nm = "NODE " + storage.getNodeCode();
            nodeScreen.setNodeName(nm);
            bool ver = storage.getIsVerified();
            nodeScreen.setVerified(ver);
            nodeScreen.setRewards(ver, storage.getTotalEarned());
            // Lifetime uptime (across reboots), synced from the server each
            // heartbeat and ticked locally — no longer resets to "1m" on reboot.
            uint32_t secs = storage.getTotalUptimeSecs();
            char ub[20];
            if      (secs < 3600)  snprintf(ub, sizeof(ub), "%lum", (unsigned long)(secs / 60));
            else if (secs < 86400) snprintf(ub, sizeof(ub), "%luh %lum",
                                            (unsigned long)(secs / 3600), (unsigned long)((secs % 3600) / 60));
            else                   snprintf(ub, sizeof(ub), "%lud %luh",
                                            (unsigned long)(secs / 86400), (unsigned long)((secs % 86400) / 3600));
            nodeScreen.setUptime(ub);
        }

        // Clock/Home screen text only below
        if (currentScreen != ScreenId::CLOCK) return;

        // Home ambient line, e.g. "23° · 48%" (or "--° · --%" with no sensor).
        if (clockWeatherLabel) {
            char wBuf[24];
            if (_sensorValid) {
                float dT = (tempUnit == 'F') ? (_tempC * 9.0f / 5.0f + 32.0f) : _tempC;
                snprintf(wBuf, sizeof(wBuf), "%d\xC2\xB0 \xE2\x80\xA2 %d%%", (int)roundf(dT), _humidityPct);
            } else {
                snprintf(wBuf, sizeof(wBuf), "--\xC2\xB0 \xE2\x80\xA2 --%%");
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

        // Alarm pill: ALWAYS show the configured (or default) alarm time so the
        // user can see it at a glance. Yellow (border + icon + text) when the
        // alarm is enabled, grey when it's off — the time stays visible either way.
        char alarmBuf[28];
        lv_color_t alarmCol = alarmOn ? lv_color_hex(0xe8b339)   // yellow = on
                                      : lv_color_hex(0x6e7280);  // grey   = off
        snprintf(alarmBuf, sizeof(alarmBuf), "\xEF\x83\xB3 %02d:%02d",
                 storage.getAlarmHour(), storage.getAlarmMinute());
        lv_label_set_text(clockAlarmLabel, alarmBuf);
        lv_obj_set_style_text_color(clockAlarmLabel, alarmCol, 0);
        lv_obj_set_style_border_color(clockAlarmLabel, alarmCol, 0);
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
        lv_obj_set_style_pad_column(row, 8, 0);
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
    static void onDebtRangeTapped(lv_event_t* e) {
        UiManager* self = (UiManager*)lv_event_get_user_data(e);
        self->debtYearsRangeIndex = (int)lv_dropdown_get_selected(lv_event_get_current_target(e));
        self->_debtRangeDirty = true;   // deferred 1-2 s refetch (updateClockIfNeeded)
    }
    static void onSinceBtnTapped(lv_event_t* e) {
        UiManager* self = (UiManager*)lv_event_get_user_data(e);
        self->sincePeriodIndex = (int)lv_dropdown_get_selected(lv_event_get_current_target(e));
        self->_computeDebtDerived();
    }
    static void onRateBtnTapped(lv_event_t* e) {
        UiManager* self = (UiManager*)lv_event_get_user_data(e);
        self->rateUnitIndex = (int)lv_dropdown_get_selected(lv_event_get_current_target(e));
        self->_computeDebtDerived();
    }
    static void onGameYearsTapped(lv_event_t* e) {
        UiManager* self = (UiManager*)lv_event_get_user_data(e);
        self->gameYearsIndex = (int)lv_dropdown_get_selected(lv_event_get_current_target(e));
        self->_updateGameProjection();
    }
    static void onVerifyBadgeTapped(lv_event_t* e) { ((UiManager*)lv_event_get_user_data(e))->openVerifyTooltip(); }

    void openAlarmPicker() {
        if (_touchWasSwipe) return;   // a swipe that started on the bell — not a tap
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
        lv_obj_clear_flag(daysRow, LV_OBJ_FLAG_SCROLLABLE);   // taps only — no stray scrolling
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
        // Honour the 12h/24h setting: in AM/PM mode the picker shows 1–12 plus an
        // AM/PM wheel; the hour is decoded back to 0–23 on save.
        bool alarm24h = (storage.getTimeFormat() == "24H");
        TimePickerRefs refs = addTimePicker(card, storage.getAlarmHour(), storage.getAlarmMinute(), alarm24h);

        // ── Alarm on/off toggle ───────────────────────────────────────────────
        static bool sAlarmEnabled;
        sAlarmEnabled = storage.getAlarmEnabled();

        // Keep "ALARM ON" and its switch close together and centred (roughly the
        // same footprint as the two time rollers above), instead of pinned to the
        // far edges of a full-width row.
        lv_obj_t* toggleRow = lv_obj_create(card);
        lv_obj_set_size(toggleRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(toggleRow, LV_OPA_0, 0);
        lv_obj_set_style_border_width(toggleRow, 0, 0);
        lv_obj_set_style_pad_all(toggleRow, 0, 0);
        lv_obj_set_style_pad_column(toggleRow, 12, 0);
        lv_obj_set_flex_flow(toggleRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(toggleRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

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
        lv_obj_set_style_pad_column(btnRow, 8, 0);
        lv_obj_set_style_bg_opa(btnRow, LV_OPA_0, 0);
        lv_obj_set_style_border_width(btnRow, 0, 0);

        lv_obj_t* cancelBtn = addModalButton(btnRow, "CANCEL", false);
        lv_obj_t* saveBtn   = addModalButton(btnRow, "SAVE",   true);

        static TimePickerRefs sRefs; static lv_obj_t* sCard;
        sRefs = refs; sCard = card;

        lv_obj_add_event_cb(saveBtn, [](lv_event_t*) {
            uint8_t h = decodePickerHour(sRefs);              // 0–23 regardless of 12/24h mode
            uint8_t m = lv_roller_get_selected(sRefs.minuteRoller);
            storage.setAlarm(h, m, sAlarmEnabled);
            storage.setAlarmDays(sDayMask);
            // Short confirmation beep — doubles as an end-to-end test of the
            // ESP32→RP2040 buzzer link: if saving the alarm chirps, the alarm
            // itself will sound; if it doesn't, the UART link is the problem
            // (not the alarm logic or the buzzer).
            rp2040Link.playChime();
            closeModal(sCard);
        }, LV_EVENT_CLICKED, nullptr);

        lv_obj_add_event_cb(cancelBtn, [](lv_event_t*) {
            closeModal(sCard);
        }, LV_EVENT_CLICKED, nullptr);
    }

    void openCalendarPopup() {
        if (_touchWasSwipe) return;   // a swipe that started on the date — not a tap
        lv_obj_t* card = openModal(lv_scr_act());

        // Custom month grid. LVGL 8's lv_calendar always lays the week out
        // Sunday-first with no option to change it, so we render our own grid
        // that honours the WEEK STARTS setting (Mon or Sun).
        time_t now = time(nullptr);
        struct tm t; localtime_r(&now, &t);
        int year = t.tm_year + 1900;
        int month = t.tm_mon;         // 0..11
        int today = t.tm_mday;

        struct tm first = {};
        first.tm_year = t.tm_year; first.tm_mon = month; first.tm_mday = 1;
        first.tm_hour = 12; first.tm_isdst = -1;
        mktime(&first);
        int firstWday = first.tm_wday;   // 0=Sun..6=Sat

        static const int dimTbl[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
        int daysInMonth = dimTbl[month];
        if (month == 1 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) daysInMonth = 29;

        bool sunFirst = (storage.getWeekStart() == 0);

        static const char* monthNames[12] = {"JANUARY","FEBRUARY","MARCH","APRIL","MAY","JUNE",
                                             "JULY","AUGUST","SEPTEMBER","OCTOBER","NOVEMBER","DECEMBER"};
        lv_obj_t* title = lv_label_create(card);
        char tbuf[24]; snprintf(tbuf, sizeof(tbuf), "%s %d", monthNames[month], year);
        lv_label_set_text(title, tbuf);
        lv_obj_set_style_text_color(title, lv_color_hex(0xcfcfd4), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);

        lv_obj_t* grid = lv_obj_create(card);
        lv_obj_set_size(grid, 7 * 36, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(grid, LV_OPA_0, 0);
        lv_obj_set_style_border_width(grid, 0, 0);
        lv_obj_set_style_pad_all(grid, 0, 0);
        lv_obj_set_style_pad_row(grid, 3, 0);
        lv_obj_set_style_pad_column(grid, 0, 0);   // CRITICAL: the theme's default column
                                                   // gap made 7×36px cells overflow 252px and
                                                   // wrap to 6/row, dropping Sat/Sun. Zero it.
        lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

        auto makeCell = [&]() -> lv_obj_t* {
            lv_obj_t* c = lv_obj_create(grid);
            lv_obj_set_size(c, 36, 26);
            lv_obj_set_style_bg_opa(c, LV_OPA_0, 0);
            lv_obj_set_style_border_width(c, 0, 0);
            lv_obj_set_style_pad_all(c, 0, 0);
            lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
            return c;
        };

        static const char* hdrSun[7] = {"S","M","T","W","T","F","S"};
        static const char* hdrMon[7] = {"M","T","W","T","F","S","S"};
        const char** hdr = sunFirst ? hdrSun : hdrMon;
        for (int i = 0; i < 7; i++) {
            lv_obj_t* h = makeCell();
            lv_obj_t* l = lv_label_create(h);
            lv_label_set_text(l, hdr[i]);
            lv_obj_set_style_text_color(l, lv_color_hex(0x6e7280), 0);
            lv_obj_set_style_text_font(l, &lv_font_montserrat_10, 0);
            lv_obj_center(l);
        }

        int offset = sunFirst ? firstWday : (firstWday + 6) % 7;
        for (int i = 0; i < offset; i++) makeCell();   // leading blanks

        for (int d = 1; d <= daysInMonth; d++) {
            lv_obj_t* c = makeCell();
            bool isToday = (d == today);
            if (isToday) {
                lv_obj_set_style_bg_color(c, lv_color_hex(0x2eaa50), 0);
                lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
                lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
            }
            lv_obj_t* l = lv_label_create(c);
            char db[4]; snprintf(db, sizeof(db), "%d", d);
            lv_label_set_text(l, db);
            lv_obj_set_style_text_color(l, isToday ? lv_color_hex(0x06150a) : lv_color_hex(0xcfcfd4), 0);
            lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
            lv_obj_center(l);
        }

        lv_obj_t* closeBtn = addModalButton(card, "CLOSE", false);
        static lv_obj_t* sCard; sCard = card;
        lv_obj_add_event_cb(closeBtn, [](lv_event_t*) { closeModal(sCard); }, LV_EVENT_CLICKED, nullptr);
    }

    void openConfigPopup() {
        if (_touchWasSwipe) return;   // a swipe that started on the gear — not a tap
        lv_obj_t* card = openModal(lv_scr_act());
        lv_obj_t* title = lv_label_create(card);
        lv_label_set_text(title, "DEVICE SETUP");
        lv_obj_set_style_text_color(title, lv_color_hex(0x9a9a9e), 0);

        // The DEVICE's config QR is the OWNER's entry point: whoever physically
        // holds the device (and sees this screen) is treated as the owner, so it
        // points at the private /setup/ page WITH the per-device secret token
        // (?t=...) — without it the web page refuses to show or save anything,
        // so merely knowing the public 4-char code is no longer enough to edit
        // someone else's node. The PUBLIC profile (/node/<code>) is what the
        // network map / ranking links use instead — see web/app/node/.
        String setupUrl = "https://network.turbousd.com/setup/" + storage.getNodeCode()
                        + "?t=" + storage.getSetupToken();
        addQrCode(card, setupUrl.c_str(), 120);

        // Full URL (incl. token) under the QR, so it can also be typed by hand.
        lv_obj_t* urlHint = lv_label_create(card);
        lv_label_set_text(urlHint, setupUrl.c_str());
        lv_obj_set_style_text_color(urlHint, lv_color_hex(0x9a9a9e), 0);
        lv_obj_set_style_text_font(urlHint, &lv_font_montserrat_10, 0);
        lv_label_set_long_mode(urlHint, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(urlHint, LV_PCT(100));
        lv_obj_set_style_text_align(urlHint, LV_TEXT_ALIGN_CENTER, 0);

        lv_obj_t* prefsTitle = lv_label_create(card);
        lv_label_set_text(prefsTitle, "DISPLAY PREFERENCES");
        lv_obj_set_style_text_color(prefsTitle, lv_color_hex(0x9a9a9e), 0);

        // TEMPERATURE toggle intentionally removed: the base D1 has no environmental
        // sensor and we no longer surface temp/humidity anywhere, so a °C/°F choice
        // would be a dead setting. (setTempUnit still exists for geo-locale use.)

        // Any manual change here locks locale, so the geo-IP autodetect can
        // never later overwrite the user's explicit choice (setLocaleLocked).
        addPrefToggleRow(card, "DATE FORMAT", "DD/MM", "MM/DD",
                          storage.getDateFormat() == "DD/MM",
                          [](bool leftActive){ storage.setDateFormat(leftActive ? "DD/MM" : "MM/DD"); storage.setLocaleLocked(true); });

        addPrefToggleRow(card, "TIME FORMAT", "24H", "AM/PM",
                          storage.getTimeFormat() == "24H",
                          [](bool leftActive){ storage.setTimeFormat(leftActive ? "24H" : "AMPM"); storage.setLocaleLocked(true); });

        addPrefToggleRow(card, "WEEK STARTS", "MON", "SUN",
                          storage.getWeekStart() == 1,
                          [](bool leftActive){ storage.setWeekStart(leftActive ? 1 : 0); storage.setLocaleLocked(true); });

        // Button reference (informational) — bottom of the preferences card.
        lv_obj_t* btnInfo = lv_label_create(card);
        lv_label_set_text(btnInfo,
            "Top button: tap to toggle the screen, or to silence a ringing alarm. "
            "On the NFT gallery, double-tap for fullscreen (double-tap again to exit).");
        lv_obj_set_style_text_color(btnInfo, lv_color_hex(0x6e7280), 0);
        lv_obj_set_style_text_font(btnInfo, &lv_font_montserrat_10, 0);

        // Diagnostics over WiFi (screen mirror + live logs) — at the very bottom.
        // Works even when the USB serial console is unavailable. Shows the
        // mDNS name and the raw IP (Android can't resolve .local → use the IP).
        lv_obj_t* diagInfo = lv_label_create(card);
        {
            String ip = WiFi.localIP().toString();
            String s = String("Logs & screen (same WiFi):\n")
                     + "http://turbousd.local/logs\n"
                     + "http://" + ip + "/logs";
            lv_label_set_text(diagInfo, s.c_str());
        }
        // Same muted tone as the URL under the QR (was bright green, which drew
        // the eye more than the setup URL above it).
        lv_obj_set_style_text_color(diagInfo, lv_color_hex(0x9a9a9e), 0);
        lv_obj_set_style_text_font(diagInfo, &lv_font_montserrat_10, 0);
        lv_label_set_long_mode(diagInfo, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(diagInfo, LV_PCT(100));
        lv_obj_set_style_text_align(diagInfo, LV_TEXT_ALIGN_CENTER, 0);

        // Firmware versions at the very bottom (ESP32 = the OTA-managed image;
        // RP2040 = the paired co-processor build, see config.h).
        lv_obj_t* verInfo = lv_label_create(card);
        {
            // Real RP2040 version from its version frame; fall back to the paired
            // build constant until the first frame arrives.
            String rpv = rp2040Link.rpVersion();
            char verBuf[80];
            snprintf(verBuf, sizeof(verBuf), "Firmware  ESP32 v%s   RP2040 v%s",
                     FIRMWARE_VERSION, rpv.length() ? rpv.c_str() : RP2040_FIRMWARE_VERSION);
            lv_label_set_text(verInfo, verBuf);
        }
        lv_obj_set_style_text_color(verInfo, lv_color_hex(0x6a6a6e), 0);
        lv_obj_set_style_text_font(verInfo, &lv_font_montserrat_10, 0);
        lv_label_set_long_mode(verInfo, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(verInfo, LV_PCT(100));
        lv_obj_set_style_text_align(verInfo, LV_TEXT_ALIGN_CENTER, 0);

        // "Check for updates" → requests an OTA check on the network thread; the
        // main loop shows the install badge if a newer ESP32 image exists, or an
        // "up to date" bar otherwise. The install itself is the existing badge flow.
        lv_obj_t* otaBtn = addModalButton(card, "CHECK FOR UPDATES", false);
        static lv_obj_t* sOtaCard; sOtaCard = card;
        lv_obj_add_event_cb(otaBtn, [](lv_event_t* e) {
            UiManager* self = (UiManager*)lv_event_get_user_data(e);
            if (self) { self->otaCheckRequested = true; self->showOtaInfo("Checking for updates\xE2\x80\xA6"); }
            closeModal(sOtaCard);
        }, LV_EVENT_CLICKED, this);

        lv_obj_t* closeBtn = addModalButton(card, "CLOSE", false);
        static lv_obj_t* sCard; sCard = card;
        lv_obj_add_event_cb(closeBtn, [](lv_event_t*) { closeModal(sCard); }, LV_EVENT_CLICKED, nullptr);
    }

    void openVerifyTooltip() {
        if (_touchWasSwipe) return;   // a swipe that started on the badge — not a tap
        // No bottom button: the modal's corner X closes it, and the freed
        // space goes to the instructions text.
        lv_obj_t* card = openModal(lv_scr_act());
        lv_obj_t* label = lv_label_create(card);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(label, 290);
        lv_label_set_text(label,
            "Verification pending.\n\n"
            "To get verified:\n"
            "1. Post a video on X showing this node running, tagging @turbousd\n"
            "2. Write your node name on paper, show it matches your screen\n"
            "3. Include the wallet holding your TUSD\n"
            "4. We manually review and whitelist your node");
        lv_obj_set_style_text_color(label, lv_color_hex(0x9a9a9e), 0);
    }


    void reloadDebtHistory(int years) {
        DebtHistoryPoint points[80];
        int count = apiClient.fetchDebtHistory(years, points, 80);
        Log.printf("debtHistory: %d points (years=%d)\n", count, years);
        if (count == 0) {
            Log.println("reloadDebtHistory: no data returned, leaving chart as-is.");
            return;
        }
        // Find the data's min/max so we can (a) fit the Y axis to it and (b) fill
        // the axis legend.
        double minUsd = points[0].totalDebtUsd, maxUsd = points[0].totalDebtUsd;
        for (int i = 1; i < count; i++) {
            if (points[i].totalDebtUsd < minUsd) minUsd = points[i].totalDebtUsd;
            if (points[i].totalDebtUsd > maxUsd) maxUsd = points[i].totalDebtUsd;
        }

        lv_chart_set_point_count(debtScreen.getChart(), count);
        // Fit the Y axis to the data. lv_chart's DEFAULT range is 0..100, but our
        // scaled values are ~200..365 (tenths of a trillion), so without this the
        // whole series was clipped to a flat line pinned at the top of the chart.
        lv_coord_t loScaled = (lv_coord_t)(minUsd / 1e11);
        lv_coord_t hiScaled = (lv_coord_t)(maxUsd / 1e11);
        if (hiScaled <= loScaled) hiScaled = loScaled + 1;
        lv_coord_t pad = (hiScaled - loScaled) / 12 + 1;   // small headroom so it isn't glued to the edges
        lv_coord_t lo  = loScaled - pad;
        if (lo < 0) lo = 0;   // debt can't be negative — the bottom tick used to read "$-3T"
        lv_chart_set_range(debtScreen.getChart(), LV_CHART_AXIS_PRIMARY_Y, lo, hiScaled + pad);

        for (int i = 0; i < count; i++) {
            // Scale to an integer-friendly range for lv_chart (raw USD values
            // are far too large for lv_coord_t); express in tenths of a
            // trillion so the chart's y-axis still reads meaningfully.
            lv_coord_t scaled = (lv_coord_t)(points[i].totalDebtUsd / 1e11);
            lv_chart_set_next_value(debtScreen.getChart(), debtScreen.getSeries(), scaled);
        }
        debtScreen.updateAxisLegend(points[0].year, points[count - 1].year, minUsd, maxUsd);

        // Derive a real annual $ debasement rate from the debt's growth over the
        // loaded span, for the inflation game. (Compound annual growth rate.)
        if (count >= 2 && points[count - 1].year > points[0].year && points[0].totalDebtUsd > 0) {
            double growth = points[count - 1].totalDebtUsd / points[0].totalDebtUsd;
            int span = points[count - 1].year - points[0].year;
            double r = pow(growth, 1.0 / span) - 1.0;
            if (r > 0.005 && r < 0.6) _annualDebasementRate = r;
        }
        _updateGameProjection();
        // Derive the debt-clock rate ($/sec) from the most recent year-over-year
        // change, then refresh the SINCE / RATE widgets.
        if (count >= 2) {
            double dDebt  = points[count - 1].totalDebtUsd - points[count - 2].totalDebtUsd;
            int    dYears = points[count - 1].year - points[count - 2].year;
            if (dYears < 1) dYears = 1;
            _debtPerSecond = dDebt / (dYears * 365.25 * 86400.0);
            _computeDebtDerived();
        }
        _debtHistLoaded = true;
    }

    // Fill the SINCE / RATE value labels + button labels from the computed rate.
    void _computeDebtDerived() {
        static const long unitSec[]   = {1, 60, 3600, 86400};                 // SEC MIN HOUR DAY
        static const long periodSec[] = {3600, 86400, 604800, 2592000};       // 1H 24H 7D 30D
        debtScreen.updateRateValue(_debtPerSecond * unitSec[rateUnitIndex % 4]);
        long secs = (sincePeriodIndex < 4) ? periodSec[sincePeriodIndex]
                                            : (long)(millis() / 1000);          // "NODE ON" ≈ uptime
        // Full comma-separated figure; recomputed every second so it climbs live.
        debtScreen.updateSinceValue(_debtPerSecond * secs);

        // (Dropdowns render their own selected option — no label plumbing.)
    }

    // NOTE on both pickers below: the selection applies LIVE as the roller
    // moves (VALUE_CHANGED), not only on SAVE — users closed the modal with
    // the X expecting their pick to stick, and it silently reverted.


    // Recompute the inflation-game projection ($10,000 eroded by the real annual
    // debasement rate over the selected horizon) and redraw its chart + labels.
    void _updateGameProjection() {
        static const int yearOpts[10] = {0, 1, 3, 5, 10, 20, 30, 50, 75, 100};
        int years = yearOpts[gameYearsIndex % 10];   // 0 = REAL TIME
        double rate = _annualDebasementRate;

        lv_obj_t* c = gameScreen.getChart();
        lv_chart_series_t* s = gameScreen.getSeries();

        if (years == 0) {
            // ── REAL TIME ── rolling 2-minute window, one point per second,
            // fed by _tickGameRealtime(). Chart units are 0.0001 $ offsets
            // from a baseline so the ~$0.000024/s fall is visible on an
            // integer axis. Re-entering resets the window so the line draws
            // itself from scratch.
            if (!_gameRtActive) {
                _gameRtActive   = true;
                _gameRtBaseline = 0.0;   // set on the first tick
                _gameRtCount    = 0;     // draw from the LEFT edge
                lv_chart_set_point_count(c, GAME_RT_POINTS);
                lv_chart_set_update_mode(c, LV_CHART_UPDATE_MODE_SHIFT);
                lv_chart_set_all_value(c, s, LV_CHART_POINT_NONE);
            }
            _tickGameRealtime();         // draw/refresh immediately
            return;
        }

        // ── Projection mode (1Y…100Y) ──
        if (_gameRtActive) {
            _gameRtActive = false;
            lv_chart_set_update_mode(c, LV_CHART_UPDATE_MODE_CIRCULAR);
            gameScreen.setRealtimeAxis(false, 0.0);
        }
        const int N = 24;
        lv_chart_set_point_count(c, N);

        double endV = 10000.0 * pow(1.0 / (1.0 + rate), (double)years);
        // Fit the Y axis to the curve (default 0..100 would clip $10,000 flat —
        // that's why the line/number never appeared).
        lv_coord_t lo = (lv_coord_t)(endV * 0.9);
        lv_chart_set_range(c, LV_CHART_AXIS_PRIMARY_Y, lo, 10000);
        for (int i = 0; i < N; i++) {
            double t = years * (double)i / (N - 1);
            double v = 10000.0 * pow(1.0 / (1.0 + rate), t);
            lv_chart_set_next_value(c, s, (lv_coord_t)v);
        }

        int dayCount = (int)(millis() / 86400000UL);
        gameScreen.updateProjection(dayCount, endV, years);
        gameScreen.setHorizonLabel(years);
    }


    // ── Real-time inflation tick (1/s while the game screen shows REAL TIME) ──
    // $10,000 eroded continuously at the derived annual debasement rate since
    // the node went online; the 4th decimal falls every few seconds.
    static const int GAME_RT_POINTS = 120;   // 2-minute rolling window
    void _tickGameRealtime() {
        lv_obj_t* c = gameScreen.getChart();
        lv_chart_series_t* s = gameScreen.getSeries();
        double rate     = _annualDebasementRate;
        double secsYear = 365.25 * 86400.0;
        double elapsed  = (double)millis() / 1000.0;
        double v        = 10000.0 * pow(1.0 / (1.0 + rate), elapsed / secsYear);
        double perSec   = v * log(1.0 + rate) / secsYear;   // $ lost per second

        if (_gameRtBaseline == 0.0) {
            _gameRtBaseline = v;
            gameScreen.setRealtimeAxis(true, _gameRtBaseline, false);
        }
        // Chart unit = 0.0001 $ offset from baseline (negative, falling).
        long scaledL = lround((v - _gameRtBaseline) * 10000.0);
        if (scaledL < -30000) {   // int16 lv_coord_t headroom — re-baseline
            _gameRtBaseline = v;
            _gameRtCount    = 0;
            gameScreen.setRealtimeAxis(true, _gameRtBaseline, false);
            lv_chart_set_all_value(c, s, LV_CHART_POINT_NONE);
            scaledL = 0;
        }
        lv_coord_t scaled = (lv_coord_t)scaledL;

        // Time flows left → right: FILL the window from the left edge first
        // (set_value_by_id), and only once it's full let the chart scroll
        // (set_next_value + SHIFT). The X labels flip from absolute
        // (0s → +2 min) to relative (-2 min → now) at that moment.
        if (_gameRtCount < GAME_RT_POINTS) {
            lv_chart_set_value_by_id(c, s, (uint16_t)_gameRtCount, scaled);
            _gameRtCount++;
            if (_gameRtCount == GAME_RT_POINTS)
                gameScreen.setRealtimeAxis(true, _gameRtBaseline, true);
        } else {
            lv_chart_set_next_value(c, s, scaled);
        }

        // Adaptive Y window: top = where this window's line STARTS (the
        // first point during the fill phase; the oldest visible point once
        // scrolling), bottom = current value MINUS 25% headroom. The line
        // always has fresh room to keep falling into — if the value plunged
        // while ticks were missed (screen off, blocking fetches), the span
        // simply grows and the whole curve stays in frame.
        lv_coord_t fallWin = (lv_coord_t)(perSec * 10000.0 * GAME_RT_POINTS) + 6;
        lv_coord_t hi = (_gameRtCount < GAME_RT_POINTS)
                        ? (lv_coord_t)3                    // fill phase: top = first point
                        : (lv_coord_t)(scaled + fallWin);  // scroll: oldest visible point
        lv_coord_t span = hi - scaled;
        if (span < 12) span = 12;
        lv_coord_t lo = scaled - (span / 4 + 2);           // 25% of the span free below
        lv_chart_set_range(c, LV_CHART_AXIS_PRIMARY_Y, lo, hi);

        int dayCount = (int)(millis() / 86400000UL);
        gameScreen.updateRealtime(dayCount, v, perSec);
    }

    void showScreen(ScreenId id, bool animate = true,
                    lv_scr_load_anim_t anim = LV_SCR_LOAD_ANIM_MOVE_LEFT) {
        // Leaving the NFT screen always drops fullscreen (safety — normally you
        // can't navigate away while it's on, since swipe is locked).
        if (_nftFullscreen && id != ScreenId::NFT) { _nftFullscreen = false; nftScreen.setFullscreen(false); }
        currentScreen = id;
        // Update swipe position to stay in sync with direct navigation (e.g. logo tap → home)
        for (int i = 0; i < _swipeCount; i++) {
            if (_swipeOrder[i] == (uint8_t)id) { _currentSwipePos = i; break; }
        }
        // Instant (non-animated) load for the very first screen at boot: a screen
        // animation that's still running when the provisioning screen loads can
        // corrupt LVGL and crash. Navigation taps/swipes still animate.
        //
        // Same protection for fast swiping: starting a second lv_scr_load_anim
        // while the previous 300 ms one is still running is a known LVGL 8
        // crash/corruption source (two screens animating, one gets unloaded
        // mid-anim). If the user swipes again before the animation finished,
        // load the next screen instantly instead of animating.
        static uint32_t lastAnimStartAt = 0;
        if (animate && millis() - lastAnimStartAt < 350) animate = false;
        if (animate) {
            lastAnimStartAt = millis();
            lv_scr_load_anim(screens[(int)id], anim, 300, 0, false);
        } else {
            lv_scr_load(screens[(int)id]);
        }
        // Trigger data load when ticker screen becomes visible
        if (id == ScreenId::TICKERS) {
            tickerScreen.onShow(storage.getNodeCode().c_str());
        }
        // Trigger NFT load (wallet prompt or cache refresh) when NFT screen shown
        if (id == ScreenId::NFT) {
            nftScreen.onShow();
        }
        // Refresh the inflation-game projection each time it's shown (uses the
        // latest debasement rate + selected horizon).
        if (id == ScreenId::INFLATION_GAME) {
            _updateGameProjection();
        }
    }

    void attachSwipeGesture(lv_obj_t* screen) {
        lv_obj_set_user_data(screen, this);
        // A scrollable screen swallows horizontal drags as (dead-end) scrolling,
        // which made only one swipe direction register. The content fits 480×480,
        // so the screen itself never needs to scroll — clearing the flag lets both
        // left and right swipes reliably fire the gesture.
        lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(screen, [](lv_event_t* e) {
            lv_obj_t* target = lv_event_get_current_target(e);
            UiManager* self = (UiManager*)lv_obj_get_user_data(target);
            if (!self) return;
            if (self->_nftFullscreen) return;   // fullscreen photo-frame locks swipe navigation
            lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
            int count = self->_swipeCount;   // hidden screens are out of rotation
            // Swipe LEFT → next screen (slides in from the right);
            // Swipe RIGHT → previous screen (slides in from the left).
            // (Touch X is mirror-corrected in the read_cb, so the gesture
            // direction is now the natural one.)
            if (dir == LV_DIR_LEFT) {
                int newPos = (self->_currentSwipePos + 1) % count;
                self->showScreen((ScreenId)self->_swipeOrder[newPos], true, LV_SCR_LOAD_ANIM_MOVE_LEFT);
            } else if (dir == LV_DIR_RIGHT) {
                int newPos = (self->_currentSwipePos - 1 + count) % count;
                self->showScreen((ScreenId)self->_swipeOrder[newPos], true, LV_SCR_LOAD_ANIM_MOVE_RIGHT);
            }
        }, LV_EVENT_GESTURE, nullptr);
    }
};
