// src/main.cpp — TurboUSD Node firmware, ESP32-S3 side.
//
// Responsibilities on this chip: WiFi + provisioning, talking to Supabase,
// rendering the 5 screens with LVGL, reading the touch panel, and telling
// the RP2040 when to buzz. The RP2040 itself only ever receives short
// commands over UART (see rp2040_link.h) -- it never talks to the network.

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <functional>
#include "config.h"
#include "net_lock.h"
#include "disk_cache.h"
#include "storage.h"
#include "wifi_manager.h"
#include "api_client.h"
#include "rp2040_link.h"
#include "ota_updater.h"
#include "ui/ui_manager.h"
#include "screenshot_server.h"   // http://<ip>/shot.bmp — pixel-perfect captures

// The Arduino loop task defaults to an 8 KB stack, but almost every network
// fetch (treasury, price, debt history, OHLCV, mining) runs a blocking TLS
// handshake straight from loop() — and mbedTLS is very stack-hungry. 8 KB left
// almost no headroom, so a fetch triggered from deep in a large handler (e.g.
// reloading the US-debt chart from inside updateClockIfNeeded) could overflow
// the stack and reboot the device. 16 KB gives comfortable margin for all of
// them. (Global scope: overrides the core's weak getArduinoLoopTaskStackSize.)
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

SemaphoreHandle_t gNetLock = nullptr;   // see net_lock.h

WebLog Log;   // console tee → Serial + WiFi ring buffer (http://<ip>/logs)

Storage storage;
WifiManager wifiManager;
ApiClient apiClient;
Rp2040Link rp2040Link;
OtaUpdater otaUpdater;
UiManager uiManager;

// --- Timers (millis()-based, intentionally not blocking delay()s) ---
uint32_t lastHeartbeatAt        = 0;
uint32_t lastTreasuryRefreshAt  = 0;
uint32_t lastDebtRefreshAt      = 0;
uint32_t lastOhlcvRefreshAt     = 0;
uint32_t lastOtaCheckAt         = 0;
uint32_t lastNtpSyncAt          = 0;
uint32_t lastSensorPollAt       = 0;
uint32_t lastGeoSyncAt          = 0;
uint32_t bootMillis             = 0;

bool nodeRegistered         = false;
bool alarmCurrentlyFiring   = false;
bool bootValidMarked        = false;   // flipped once after first healthy network contact

// Pending OTA update metadata (populated by the nightly check, consumed when
// the user taps "Install" in the UI).
String pendingOtaVersion;
String pendingOtaUrl;
String pendingOtaSha256;

// ── Helpers ───────────────────────────────────────────────────────────────────

void syncTimeFromNtp() {
    // Apply the saved timezone offset (set by geo-IP autodetect, or 0/UTC until
    // the first geo sync) so localtime_r() returns the user's local wall-clock
    // time. daylightOffset is 0 because the geo offset already includes DST.
    configTime(storage.getTzOffsetSec(), 0, "pool.ntp.org", "time.nist.gov");
    Log.printf("NTP sync requested (tz offset %ld s).\n", (long)storage.getTzOffsetSec());
}

// Geo-IP → timezone + regional formatting defaults. Timezone always tracks the
// device's location; the formatting choices (units, date/time order, week start)
// are applied only until the user changes one (then locale is locked).
void autoConfigureLocaleFromGeo() {
    GeoLocale geo;
    if (!apiClient.fetchGeoLocale(geo)) {
        Log.println("Geo locale: lookup failed, keeping current settings.");
        return;
    }

    // Timezone (no manual TZ control exists on-device, so it always follows geo).
    storage.setTzOffsetSec(geo.utcOffsetSec);
    configTime(geo.utcOffsetSec, 0, "pool.ntp.org", "time.nist.gov");

    if (storage.getLocaleLocked()) return; // user owns the formatting choices now

    char tempUnit; String dateFmt, timeFmt; uint8_t weekStart;
    ApiClient::localeDefaultsForCountry(geo.countryCode, tempUnit, dateFmt, timeFmt, weekStart);
    storage.setTempUnit(tempUnit);
    storage.setDateFormat(dateFmt);
    storage.setTimeFormat(timeFmt);
    storage.setWeekStart(weekStart);
    Log.printf("Geo locale: %s offset=%ld → %c %s %s wk%u\n",
                  geo.countryCode, (long)geo.utcOffsetSec, tempUnit,
                  dateFmt.c_str(), timeFmt.c_str(), (unsigned)weekStart);
}

void ensureNodeIsRegistered() {
    if (storage.hasNodeCode()) {
        nodeRegistered = true;
        return;
    }
    String nodeCode;
    if (apiClient.registerNode(nodeCode)) {
        storage.setNodeCode(nodeCode);
        nodeRegistered = true;
        Log.printf("Registered as node %s\n", nodeCode.c_str());
    } else {
        Log.println("Node registration failed, will retry next loop.");
    }
}

// Mark the running image as valid once the device has proven it can reach
// the network and backend. Until this is called after an OTA flash, the
// ESP32 bootloader will revert to the previous image on the next reset --
// that's intentional rollback protection, but we clear it here once we
// know the new firmware is healthy.
void markBootValidIfNeeded() {
    if (bootValidMarked) return;
    if (!nodeRegistered) return;          // wait until we've actually talked to the backend
    OtaUpdater::markBootValid();
    bootValidMarked = true;
}

// Top user button (GPIO 38, active LOW). Short press toggles the screen; a 3 s
// long press puts the device to sleep. By design there is NO factory-reset
// action, so the firmware can't be wiped by holding the button.
void handleUserButton() {
    static bool     prevDown   = false;
    static uint32_t pressStart = 0;
    static bool     longFired  = false;
    // Double-press detection (NFT photo-frame toggle). Only armed while the NFT
    // screen is showing, so single presses stay INSTANT everywhere else.
    static uint32_t lastShortReleaseAt = 0;
    static bool     pendingSingle      = false;
    static uint32_t pendingSingleAt    = 0;
    const  uint32_t DOUBLE_MS = 350;

    bool     down = (digitalRead(BTN_USER_GPIO) == LOW);
    uint32_t now  = millis();

    if (down && !prevDown) { pressStart = now; longFired = false; }   // press begins
    if (down && !longFired && (now - pressStart >= 3000)) {           // long press
        longFired = true;
        pendingSingle = false;                     // don't also fire a deferred toggle
        uiManager.enterSleep();   // returns here once the button wakes it
    }
    if (!down && prevDown) {                                          // released
        uint32_t held = now - pressStart;
        if (!longFired && held >= 40 && held < 3000) {                // short press
            // While the alarm is ringing, the top button STOPS it (same as
            // tapping the on-screen STOP) — much more natural half-asleep
            // than aiming at a touch target.
            if (uiManager.isAlarmOverlayActive()) {
                uiManager.stopRingingAlarm();      // stops the buzzer + closes overlay
            } else if (uiManager.nftDoublePressActive()) {
                // On the NFT screen (or already fullscreen): a SECOND short press
                // within DOUBLE_MS toggles fullscreen; otherwise defer the normal
                // screen toggle so the second press has time to arrive.
                if (now - lastShortReleaseAt < DOUBLE_MS) {
                    lastShortReleaseAt = 0;
                    pendingSingle = false;
                    uiManager.toggleNftFullscreen();
                } else {
                    lastShortReleaseAt = now;
                    pendingSingle = true;
                    pendingSingleAt = now;
                }
            } else {
                uiManager.toggleScreen();           // instant everywhere else
            }
        }
    }
    // Fire a deferred single press once the double-press window has passed.
    if (pendingSingle && now - pendingSingleAt >= DOUBLE_MS) {
        pendingSingle = false;
        uiManager.toggleScreen();
    }
    prevDown = down;
}

void checkAlarmTrigger() {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t); // LOCAL time — the alarm must fire at the user's wall-clock
                           // time, not UTC. Offset is set by syncTimeFromNtp()/geo.

    // Guard: before the first NTP sync `time()` returns a 1970 value, so
    // tm_hour/tm_min are meaningless — never fire (or spam logs) then.
    bool timeValid = (t.tm_year + 1900) >= 2024;

    const bool    enabled     = storage.getAlarmEnabled();
    const bool    activeToday = storage.isAlarmActiveToday(t.tm_wday);
    const uint8_t alarmH      = storage.getAlarmHour();
    const uint8_t alarmM      = storage.getAlarmMinute();
    const uint8_t alarmVol    = storage.getAlarmVolume();
    const bool    timeMatch   = timeValid && t.tm_hour == alarmH && t.tm_min == alarmM;

    // Fire ONCE per target-minute occurrence, but accept the WHOLE minute — not
    // just the first 5 seconds. The old 5-second window (`t.tm_sec < 5`) was the
    // root cause of the silent alarm: a single loop pass that happened to land
    // on a blocking TLS fetch right at HH:MM:00–04 skipped the only window, and
    // the alarm stayed silent for the entire day. Keying off a per-minute id
    // means the alarm fires as long as we run the check AT ALL during that
    // minute (guaranteed — the check runs every loop pass).
    static long lastFiredMinuteId = -1;
    const long minuteId = timeValid
        ? ((long)(t.tm_year) * 366L + t.tm_yday) * 1440L + t.tm_hour * 60L + t.tm_min
        : -1;

    const bool shouldFire = enabled && activeToday && timeMatch && (minuteId != lastFiredMinuteId);

    // Diagnostic: log the alarm decision only when its STATE CHANGES while we're
    // inside the target minute (or the minute right before it), so the log still
    // shows EXACTLY why the alarm did/didn't fire — without the per-second flood
    // that used to spam ~120 identical lines across those two minutes. The FIRING
    // line and the overlay re-send line below cover the actual fire event.
    static int lastLoggedState = -1;
    const bool nearAlarm = timeValid && enabled &&
        (t.tm_hour == alarmH) && (t.tm_min == alarmM || (t.tm_min + 1) % 60 == alarmM);
    const int alarmStateKey = (enabled ? 1 : 0) | (activeToday ? 2 : 0) | (timeMatch ? 4 : 0)
        | ((minuteId == lastFiredMinuteId) ? 8 : 0) | (uiManager.isAlarmOverlayActive() ? 16 : 0);
    if (nearAlarm && alarmStateKey != lastLoggedState) {
        lastLoggedState = alarmStateKey;
        Log.printf("ALARM chk %02d:%02d:%02d | enabled=%d activeToday=%d set=%02u:%02u vol=%u "
                      "timeMatch=%d firedThisMin=%d overlay=%d\n",
                      t.tm_hour, t.tm_min, t.tm_sec, enabled, activeToday, alarmH, alarmM, alarmVol,
                      timeMatch, (minuteId == lastFiredMinuteId), uiManager.isAlarmOverlayActive());
    } else if (!nearAlarm) {
        lastLoggedState = -1;   // re-arm so the next approach logs its first state
    }

    static uint32_t alarmFiredAt = 0;
    if (shouldFire) {
        lastFiredMinuteId = minuteId;
        alarmCurrentlyFiring = true;
        alarmFiredAt = millis();
        Log.printf("ALARM: FIRING now (%02d:%02d:%02d) vol=%u — sending PLAY_ALARM to RP2040\n",
                      t.tm_hour, t.tm_min, t.tm_sec, alarmVol);
        rp2040Link.playAlarm(alarmVol);
        uiManager.showAlarmFiringOverlay();
    } else if (!timeMatch) {
        alarmCurrentlyFiring = false;
    }

    // While the overlay is up (user hasn't tapped STOP), re-send PLAY_ALARM
    // every 10 s. The UART link has no retransmission, so if the single
    // original frame was lost/corrupted the alarm would otherwise stay
    // silent. Re-sending restarts the RP2040's 5-min auto-stop window, so we
    // stop re-sending after 5 min ourselves — an unattended alarm then still
    // goes quiet instead of buzzing forever.
    static uint32_t lastAlarmResendAt = 0;
    if (uiManager.isAlarmOverlayActive()
        && millis() - alarmFiredAt < 5UL * 60UL * 1000UL
        && millis() - lastAlarmResendAt > 10000) {
        lastAlarmResendAt = millis();
        Log.println("ALARM: overlay still up — re-sending PLAY_ALARM to RP2040");
        rp2040Link.playAlarm(alarmVol);
    }
}

// Returns true if the current LOCAL time is in the overnight OTA check window
// (02:00–04:00). We avoid daytime checks so a download doesn't compete with
// the heartbeat / data-refresh traffic during normal use.
bool isOtaCheckWindow() {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    return (t.tm_hour >= 2 && t.tm_hour < 4);
}

void applyPendingOtaUpdate() {
    Log.printf("OTA: user confirmed install of %s\n", pendingOtaVersion.c_str());
    if (otaUpdater.applyPendingUpdate(pendingOtaUrl, pendingOtaSha256)) {
        delay(500);
        ESP.restart();
    } else {
        Log.println("OTA: apply failed. Device continues on current firmware.");
        // Clear pending so the badge disappears; next nightly check will re-detect.
        pendingOtaVersion = "";
        pendingOtaUrl = "";
        pendingOtaSha256 = "";
        uiManager.clearOtaBadge();
    }
}

// ── Arduino entry points ──────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    Log.println("\nTurboUSD Node booting, firmware " FIRMWARE_VERSION);

    // Log WHY we booted. If the device is mysteriously restarting (e.g. "it
    // reboots when I open screen X"), this line on the serial monitor tells us
    // whether it was a panic (4), an interrupt/task watchdog (5/6), brownout
    // (9), etc. — invaluable for diagnosing in the field.
    esp_reset_reason_t rr = esp_reset_reason();
    static const char* RR_NAMES[] = {"UNKNOWN","POWERON","EXT","SW","PANIC","INT_WDT",
                                     "TASK_WDT","WDT","DEEPSLEEP","BROWNOUT","SDIO"};
    Log.printf("Reset reason: %d (%s)\n", (int)rr,
                  (rr >= 0 && rr <= 10) ? RR_NAMES[rr] : "?");

    pinMode(BTN_USER_GPIO, INPUT_PULLUP);  // top user button, active LOW

    netLockInit();
    diskcache::init();   // mount the LittleFS art cache (logos/NFTs survive reboots)   // single-TLS-at-a-time lock — see net_lock.h
    storage.begin();
    // Alarm config snapshot at boot — so the serial log always shows what the
    // device thinks the alarm is, independent of the web UI.
    Log.printf("ALARM cfg @boot: enabled=%d %02u:%02u vol=%u days=0x%02X dirty=%d\n",
                  storage.getAlarmEnabled(), storage.getAlarmHour(), storage.getAlarmMinute(),
                  storage.getAlarmVolume(), storage.getAlarmDays(), storage.getAlarmDirty());
    rp2040Link.begin();
    uiManager.begin(); // lv_init + hardware bring-up + build all screens

    uiManager.onAlarmDismissed = [](){ rp2040Link.stopAlarm(); };

    // When the user taps "Install" on the OTA notification, apply it.
    uiManager.onOtaInstallConfirmed = [](){ applyPendingOtaUpdate(); };

    wifiManager.begin(); // connects with saved creds or opens provisioning AP

    bootMillis = millis();
}

void loop() {
    // Poll the user button first so screen-toggle / sleep work in every state
    // (including before WiFi is connected or while the provisioning portal is up).
    handleUserButton();
    rp2040Link.pollRx();   // raw RX monitor — see rp2040_link.h

    // Service LVGL on EVERY iteration, before any network early-return. Otherwise,
    // while provisioning or before WiFi connects, lv_timer_handler() never runs and
    // the RGB panel just shows uninitialized framebuffer garbage — which looks like
    // a display/pin problem but is really "the UI was never drawn".
    uiManager.loop();

    // Link status check ~6/12/20 s after boot. SILENT now (no chime): the link
    // is verified in /logs via the RP2040 heartbeat + ping-OK lines, so there's
    // no need to make the buzzer beep at boot. Only the alarm sounds.
    static int linkChecks = 0;
    static const uint32_t CHECK_AT[3] = { 6000, 12000, 20000 };
    if (linkChecks < 3 && millis() > CHECK_AT[linkChecks]) {
        linkChecks++;
        rp2040Link.printStatus();
        Log.printf("RP-link: status check %d\n", linkChecks);
    }

    // Round-trip test every 30 s, FOREVER (the old one-shot at t=30s was
    // usually missed: serial monitors attach late and the two chips don't
    // reboot together). Every line is decisive:
    //   ping OK        → link alive both ways
    //   heartbeat RECEIVED (from pollRx) but ping FAILED → ESP→RP dead
    //   neither, ever  → RP→ESP dead too (electrical / pad level)
    static uint32_t lastPingAt = 0;
    static int lastPingOk = -1;   // -1 = unknown, 0 = fail, 1 = ok
    if (millis() - lastPingAt > 30000) {
        lastPingAt = millis();
        bool ok = rp2040Link.ping(300);
        // Only log on a STATE CHANGE (first result, or ok<->fail), not every 30 s
        // forever — a steadily-alive link was flooding the log with "ping OK".
        if ((int)ok != lastPingOk) {
            lastPingOk = ok;
            Log.printf("RP-link: ping %s\n", ok ? "OK — link is ALIVE both ways" : "FAILED — no ACK from RP2040");
        }
    }

    // While the provisioning portal is up, prioritize serving it.
    if (wifiManager.isPortalActive()) {
        wifiManager.loopPortal();
        uiManager.showProvisioningScreen();
        return;
    }

    wifiManager.checkConnection(); // reconnect if dropped post-boot

    if (!wifiManager.isConnected()) {
        delay(500);
        return;
    }

    // Screenshot server: lazy-started on the first connected pass (the
    // provisioning portal owns port 80 in AP mode, never simultaneously).
    if (!screenshot::started()) screenshot::init();
    screenshot::poll();

    uint32_t now = millis();

    // ── Network section: runs only when no worker task holds the TLS lock ──
    // Every HTTPS handshake needs ~45 KB of INTERNAL RAM; two at once → 
    // "SSL - Memory allocation failed" everywhere. Workers (tickers/NFT)
    // hold the lock for their whole run; we just postpone to the next pass.
    if (netTryLock()) {

    if (!nodeRegistered) ensureNodeIsRegistered();

    // Mark boot valid once we're healthy — must happen before any OTA check.
    markBootValidIfNeeded();

    if (lastNtpSyncAt == 0 || now - lastNtpSyncAt > NTP_RESYNC_INTERVAL_MS) {
        syncTimeFromNtp();
        lastNtpSyncAt = now;
    }

    // Geo-IP locale autodetect: once on first connect, then twice a day (to
    // follow travel and DST changes). Sets the timezone always, and the regional
    // formatting defaults only until the user overrides them.
    if (lastGeoSyncAt == 0 || now - lastGeoSyncAt > GEO_LOCALE_SYNC_INTERVAL_MS) {
        autoConfigureLocaleFromGeo();
        lastGeoSyncAt = now;
    }

    // First heartbeat fires immediately (lastHeartbeatAt == 0), not 3 minutes
    // in — it's what pulls the web-configured settings (alarm, brightness,
    // tickers order…) down to the device, so a freshly booted node should sync
    // right away.
    if (nodeRegistered && (lastHeartbeatAt == 0 || now - lastHeartbeatAt > HEARTBEAT_INTERVAL_MS)) {
        uint32_t uptimeSeconds = (now - bootMillis) / 1000;
        apiClient.sendHeartbeat(uptimeSeconds);
        // Heartbeat response may have updated screen_brightness in NVS via
        // applyServerConfig(); apply it now so the change is immediate.
        uiManager.applyStoredBrightness();
        uiManager.reloadScreenOrder();   // screen order/visibility too
        lastHeartbeatAt = now;
    }

    if (lastTreasuryRefreshAt == 0 || now - lastTreasuryRefreshAt > TREASURY_DATA_REFRESH_MS) {
        TreasuryData data = apiClient.fetchTreasuryData();
        if (data.valid) uiManager.updateTreasuryData(data);
        // Live price from DexScreener/GeckoTerminal — works even if the treasury
        // service is down, and overrides its (possibly stale) price.
        double price = apiClient.fetchTusdPrice();
        if (price > 0) uiManager.updateTusdPrice(price);
        lastTreasuryRefreshAt = now;
    }

    // Until the first SUCCESSFUL debt fetch, retry every minute — a failed
    // boot-time fetch used to leave "--" on screen for a whole hour.
    static bool debtLive = false;
    if (lastDebtRefreshAt == 0 ||
        now - lastDebtRefreshAt > (debtLive ? US_DEBT_REFRESH_MS : 60000UL)) {
        DebtData data = apiClient.fetchUsDebt();
        if (data.valid) { uiManager.updateDebtData(data); debtLive = true; }
        else Log.println("fetchUsDebt: no data, retrying in 60 s");
        lastDebtRefreshAt = now;
    }

    // Debt-history self-heal: the chart loads once via updateDebtData, but if
    // that first fetch hit a transient failure (TLS memory, DNS, treasury API
    // hiccup) the chart stayed empty forever. Retry once a minute until real
    // data lands.
    static uint32_t lastDebtHistRetryAt = 0;
    if (!uiManager.debtHistLoaded() && now - lastDebtHistRetryAt > 60000) {
        lastDebtHistRetryAt = now;
        uiManager.retryDebtHistory();
    }

    if (lastOhlcvRefreshAt == 0 || now - lastOhlcvRefreshAt > OHLCV_CHART_REFRESH_MS
        || uiManager.turboTfConsumeDirty()) {   // 1D/1W/1M dropdown changed
        OhlcvCandle candles[26];
        int count = apiClient.fetchOhlcvHistory(candles, uiManager.turboBars(),
                                                uiManager.turboGroupDays());
        if (count > 0) uiManager.loadOhlcvChart(candles, count);
        lastOhlcvRefreshAt = now;
    }

    if (uiManager.isOnNodeScreen() && uiManager.miningFeedNeedsRefresh()) {
        MiningFeedEntry entries[4];
        int count = apiClient.fetchMiningFeed(entries, 4);
        if (count > 0) uiManager.updateMiningFeed(entries, count);
        else           uiManager.retryMiningFeedSoon();   // keep old data, retry in ~3 s
    }

    // Leaderboard (₸ rewards | uptime) under the mining track — refreshed
    // every 60 s while the Node screen is visible.
    static uint32_t lastLeaderboardAt = 0;
    if ((lastLeaderboardAt == 0) ||                     // once at boot → footer node count
        (uiManager.isOnNodeScreen() && now - lastLeaderboardAt > 60000)) {
        lastLeaderboardAt = now;
        LeaderboardEntry lb[24];
        int n = apiClient.fetchNodeDirectory(lb, 24);
        if (n > 0) uiManager.updateLeaderboard(lb, n);
    }

    // OTA: check silently during the overnight window, once per OTA_CHECK_INTERVAL_MS.
    // Never auto-apply — store the metadata and let the user confirm via the UI badge.
    if (bootValidMarked && pendingOtaVersion.isEmpty()
        && isOtaCheckWindow()
        && (lastOtaCheckAt == 0 || now - lastOtaCheckAt > OTA_CHECK_INTERVAL_MS))
    {
        lastOtaCheckAt = now;
        String ver, url, sha;
        if (otaUpdater.checkNewVersion(ver, url, sha)) {
            pendingOtaVersion = ver;
            pendingOtaUrl     = url;
            pendingOtaSha256  = sha;
            // Show a persistent badge on all screens; user can dismiss or install.
            uiManager.showOtaBadge(ver.c_str());
        }
    }

    // Manual "Check for updates" from the device settings popup. Runs on demand,
    // any time (not just the overnight window). Shows the install badge if a
    // newer image exists, or an "up to date" bar otherwise.
    if (uiManager.otaCheckRequested) {
        uiManager.otaCheckRequested = false;
        String ver, url, sha;
        if (otaUpdater.checkNewVersion(ver, url, sha)) {
            pendingOtaVersion = ver;
            pendingOtaUrl     = url;
            pendingOtaSha256  = sha;
            uiManager.showOtaBadge(ver.c_str());
        } else {
            uiManager.showOtaInfo("Firmware is up to date");
        }
    }

    netUnlock();
    }   // end network section

    // Ambient temp/humidity comes from the AHT20 on the RP2040 (the S3 has no
    // sensor of its own). Poll it on a slow cadence; the UI keeps the last good
    // value when a read fails (e.g. no sensor plugged in → header shows "--").
    if (lastSensorPollAt == 0 || now - lastSensorPollAt > SENSOR_POLL_INTERVAL_MS) {
        lastSensorPollAt = now;
        float tempC; int humidityPct;
        if (rp2040Link.readTempHumidity(tempC, humidityPct)) {
            uiManager.updateAmbient(tempC, humidityPct);
        } else {
            uiManager.markAmbientUnavailable();
        }
    }

    checkAlarmTrigger();
    uiManager.loop(); // LVGL tick, touch events, screen redraws
}
