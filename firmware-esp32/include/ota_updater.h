// include/ota_updater.h — OTA update logic for the TurboUSD Node ESP32-S3.
//
// SAFETY MODEL (dual-partition + rollback guard):
//   The partition table (default_ota.csv, set in platformio.ini) keeps two
//   equal app partitions — ota_0 and ota_1. OTA writes the new image into
//   whichever is *not* currently booted, then atomically switches the boot
//   pointer. If power is lost mid-download the active partition is never
//   touched. After a successful flash + restart, markBootValid() must be
//   called once the device proves it works (WiFi connected, backend reachable).
//   Until that call the ESP32 bootloader will auto-rollback on the next reset
//   — a hard guarantee that a bad update can't permanently break the device.
//
// USER-FACING FLOW (check-then-confirm, not silent-apply):
//   1. checkNewVersion() — runs silently at night; stores metadata if newer.
//   2. Caller shows a UI badge ("v0.x.x available").
//   3. User taps "Install" → caller invokes applyPendingUpdate() → restarts.

#pragma once
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <mbedtls/sha256.h>
#include <esp_ota_ops.h>
#include "config.h"

class OtaUpdater {
public:
    // ── Call once after WiFi + backend confirm the device is healthy ──────────
    // Cancels the bootloader's rollback timer for the currently running image.
    // Without this call, a reset after any OTA update would revert to the
    // previous firmware, so it must be called on every clean boot.
    static void markBootValid() {
        esp_ota_mark_app_valid_cancel_rollback();
        Serial.println("OTA: boot marked valid, rollback guard cleared.");
    }

    // ── Silent nightly check ──────────────────────────────────────────────────
    // Queries the backend for the latest release. Returns true and populates
    // outVersion / outUrl / outSha256 if a newer version exists. Returns false
    // if already up to date or the check fails. Does NOT download anything.
    bool checkNewVersion(String& outVersion, String& outUrl, String& outSha256) {
        JsonDocument release;
        if (!fetchLatestReleaseInfo(release)) return false;

        String latestVersion = release["version"].as<String>();
        if (latestVersion == FIRMWARE_VERSION || latestVersion.isEmpty()) {
            Serial.println("OTA: firmware is current.");
            return false;
        }

        outVersion = latestVersion;
        outUrl     = release["binary_url"].as<String>();
        outSha256  = release["sha256"].as<String>();
        Serial.printf("OTA: new version available %s -> %s\n",
                      FIRMWARE_VERSION, latestVersion.c_str());
        return true;
    }

    // ── User-confirmed download + flash ───────────────────────────────────────
    // Called after the user taps "Install" in the UI.
    // Returns true on success; caller should ESP.restart() immediately after.
    bool applyPendingUpdate(const String& binaryUrl, const String& expectedSha256) {
        return downloadAndFlash(binaryUrl, expectedSha256);
    }

    // Legacy: silent check + immediate apply in one call (kept for CI/testing).
    bool checkAndApply() {
        String ver, url, sha;
        if (!checkNewVersion(ver, url, sha)) return false;
        return applyPendingUpdate(url, sha);
    }

private:
    bool fetchLatestReleaseInfo(JsonDocument& outDoc) {
        HTTPClient http;
        http.begin(String(ENDPOINT_LATEST_FIRMWARE) + "?target=esp32s3");
        int statusCode = http.GET();
        if (statusCode != 200) {
            Serial.printf("OTA check failed, HTTP %d\n", statusCode);
            http.end();
            return false;
        }
        DeserializationError err = deserializeJson(outDoc, http.getStream());
        http.end();
        return !err;
    }

    bool downloadAndFlash(const String& url, const String& expectedSha256Hex) {
        HTTPClient http;
        http.begin(url);
        int statusCode = http.GET();
        if (statusCode != 200) {
            Serial.printf("OTA download failed, HTTP %d\n", statusCode);
            http.end();
            return false;
        }

        int contentLength = http.getSize();
        if (contentLength <= 0) {
            Serial.println("OTA download has no Content-Length, aborting.");
            http.end();
            return false;
        }

        if (!Update.begin(contentLength)) {
            Serial.println("Not enough free space for OTA update.");
            http.end();
            return false;
        }

        // Stream the download straight into flash while simultaneously
        // hashing it, so we verify integrity without buffering the whole
        // ~1-2MB image in RAM.
        mbedtls_sha256_context shaCtx;
        mbedtls_sha256_init(&shaCtx);
        mbedtls_sha256_starts(&shaCtx, 0);

        WiFiClient* stream = http.getStreamPtr();
        uint8_t buf[1024];
        int written = 0;
        while (http.connected() && written < contentLength) {
            size_t available = stream->available();
            if (!available) { delay(2); continue; }
            int toRead = min((size_t)sizeof(buf), available);
            int readBytes = stream->readBytes(buf, toRead);
            if (readBytes <= 0) break;

            size_t bytesWritten = Update.write(buf, readBytes);
            if ((int)bytesWritten != readBytes) {
                Serial.printf("OTA flash write error at byte %d: wrote %d of %d\n",
                              written, (int)bytesWritten, readBytes);
                Update.abort();
                mbedtls_sha256_free(&shaCtx);
                http.end();
                return false;
            }
            mbedtls_sha256_update(&shaCtx, buf, readBytes);
            written += readBytes;
        }
        http.end();

        uint8_t hash[32];
        mbedtls_sha256_finish(&shaCtx, hash);
        mbedtls_sha256_free(&shaCtx);

        char hashHex[65];
        for (int i = 0; i < 32; i++) sprintf(hashHex + i * 2, "%02x", hash[i]);
        hashHex[64] = '\0';

        if (expectedSha256Hex.length() > 0 && !expectedSha256Hex.equalsIgnoreCase(hashHex)) {
            Serial.println("OTA SHA256 mismatch -- refusing to apply a possibly-corrupt image.");
            Update.abort();
            return false;
        }

        if (written != contentLength || !Update.end(true)) {
            Serial.printf("OTA write incomplete or failed: %s\n", Update.errorString());
            return false;
        }

        Serial.println("OTA update applied successfully. Restarting...");
        return true; // caller restarts
    }
};

extern OtaUpdater otaUpdater;
