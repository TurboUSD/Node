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
#include "storage.h"
#include "wifi_manager.h"
#include "api_client.h"
#include "rp2040_link.h"
#include "ota_updater.h"
#include "ui/ui_manager.h"

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
    // UTC; the on-device clock screen formats per the user's saved
    // date/time preferences, it doesn't need a timezone-aware NTP server.
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    Serial.println("NTP sync requested.");
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
        Serial.printf("Registered as node %s\n", nodeCode.c_str());
    } else {
        Serial.println("Node registration failed, will retry next loop.");
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

void checkAlarmTrigger() {
    time_t now = time(nullptr);
    struct tm t;
    gmtime_r(&now, &t); // UTC throughout; see syncTimeFromNtp()

    bool shouldFire = storage.getAlarmEnabled()
        && storage.isAlarmActiveToday(t.tm_wday)   // respects per-day bitmask
        && t.tm_hour == storage.getAlarmHour()
        && t.tm_min  == storage.getAlarmMinute()
        && t.tm_sec  < 5; // 5-second window so we fire exactly once per minute

    if (shouldFire && !alarmCurrentlyFiring) {
        alarmCurrentlyFiring = true;
        rp2040Link.playAlarm(storage.getAlarmVolume());
        uiManager.showAlarmFiringOverlay();
    } else if (!shouldFire) {
        alarmCurrentlyFiring = false;
    }
}

// Returns true if the current UTC time is in the overnight OTA check window
// (02:00–04:00). We avoid daytime checks so a download doesn't compete with
// the heartbeat / data-refresh traffic during normal use.
bool isOtaCheckWindow() {
    time_t now = time(nullptr);
    struct tm t;
    gmtime_r(&now, &t);
    return (t.tm_hour >= 2 && t.tm_hour < 4);
}

void applyPendingOtaUpdate() {
    Serial.printf("OTA: user confirmed install of %s\n", pendingOtaVersion.c_str());
    if (otaUpdater.applyPendingUpdate(pendingOtaUrl, pendingOtaSha256)) {
        delay(500);
        ESP.restart();
    } else {
        Serial.println("OTA: apply failed. Device continues on current firmware.");
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
    Serial.println("\nTurboUSD Node booting, firmware " FIRMWARE_VERSION);

    storage.begin();
    rp2040Link.begin();
    uiManager.begin(); // lv_init + hardware bring-up + build all screens

    uiManager.onAlarmDismissed = [](){ rp2040Link.stopAlarm(); };

    // When the user taps "Install" on the OTA notification, apply it.
    uiManager.onOtaInstallConfirmed = [](){ applyPendingOtaUpdate(); };

    wifiManager.begin(); // connects with saved creds or opens provisioning AP

    bootMillis = millis();
}

void loop() {
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

    uint32_t now = millis();

    if (!nodeRegistered) ensureNodeIsRegistered();

    // Mark boot valid once we're healthy — must happen before any OTA check.
    markBootValidIfNeeded();

    if (lastNtpSyncAt == 0 || now - lastNtpSyncAt > NTP_RESYNC_INTERVAL_MS) {
        syncTimeFromNtp();
        lastNtpSyncAt = now;
    }

    if (nodeRegistered && (now - lastHeartbeatAt > HEARTBEAT_INTERVAL_MS)) {
        uint32_t uptimeSeconds = (now - bootMillis) / 1000;
        apiClient.sendHeartbeat(uptimeSeconds);
        // Heartbeat response may have updated screen_brightness in NVS via
        // applyServerConfig(); apply it now so the change is immediate.
        uiManager.applyStoredBrightness();
        lastHeartbeatAt = now;
    }

    if (lastTreasuryRefreshAt == 0 || now - lastTreasuryRefreshAt > TREASURY_DATA_REFRESH_MS) {
        TreasuryData data = apiClient.fetchTreasuryData();
        if (data.valid) uiManager.updateTreasuryData(data);
        lastTreasuryRefreshAt = now;
    }

    if (lastDebtRefreshAt == 0 || now - lastDebtRefreshAt > US_DEBT_REFRESH_MS) {
        DebtData data = apiClient.fetchUsDebt();
        if (data.valid) uiManager.updateDebtData(data);
        lastDebtRefreshAt = now;
    }

    if (lastOhlcvRefreshAt == 0 || now - lastOhlcvRefreshAt > OHLCV_CHART_REFRESH_MS) {
        OhlcvCandle candles[26];
        int count = apiClient.fetchOhlcvHistory(candles, 26);
        if (count > 0) uiManager.loadOhlcvChart(candles, count);
        lastOhlcvRefreshAt = now;
    }

    if (uiManager.isOnNodeScreen() && uiManager.miningFeedNeedsRefresh()) {
        MiningFeedEntry entries[4];
        int count = apiClient.fetchMiningFeed(entries, 4);
        uiManager.updateMiningFeed(entries, count);
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
