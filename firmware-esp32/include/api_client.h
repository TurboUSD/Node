// include/api_client.h — all outbound HTTP calls to our own backend
// (Supabase Edge Functions / REST) and to the external real-data sources
// (treasury.turbousd.com, US Treasury Fiscal Data API).
//
// Kept as one module so retry/timeout/JSON-parsing conventions are
// consistent everywhere instead of repeated ad-hoc per call site.

#pragma once
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "storage.h"

struct TreasuryData {
    double tusdSupplyNum = 0;
    double tusdBurnedNum = 0;
    double tusdPriceUsd = 0;
    double treasuryValueUsd = 0;
    bool valid = false;
};

struct DebtData {
    double totalDebtUsd = 0;
    bool valid = false;
};

struct DebtHistoryPoint {
    int year = 0;
    double totalDebtUsd = 0;
};

struct OhlcvCandle {
    double open = 0;
    double high = 0;
    double low = 0;
    double close = 0;
};

struct MiningFeedEntry {
    long blockNumber = 0;
    double rewardTusd = 0;
    String winnerDisplayName = "";
    bool mined = false; // false = this is the currently-pending block
};

class ApiClient {
public:
    String getMacAddress() {
        uint8_t mac[6];
        WiFi.macAddress(mac);
        char buf[18];
        snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        return String(buf);
    }

    // Called once, the very first time the device comes online with no
    // node_code saved yet. Idempotent server-side if called again with the
    // same MAC (e.g. after a factory reset that wiped NVS but not the
    // backend record).
    bool registerNode(String& outNodeCode) {
        HTTPClient http;
        http.begin(ENDPOINT_REGISTER_NODE);
        http.setTimeout(8000);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Authorization", String("Bearer ") + SUPABASE_ANON_KEY);

        JsonDocument doc;
        doc["mac_address"] = getMacAddress();
        doc["firmware_version"] = FIRMWARE_VERSION;
        String payload;
        serializeJson(doc, payload);

        int statusCode = http.POST(payload);
        String responseBody = http.getString(); // read once — stream is consumed after this
        if (statusCode != 200 && statusCode != 201) {
            Serial.printf("registerNode failed, HTTP %d: %s\n", statusCode, responseBody.c_str());
            http.end();
            return false;
        }

        JsonDocument respDoc;
        deserializeJson(respDoc, responseBody);
        outNodeCode = respDoc["node"]["node_code"].as<String>();
        http.end();
        return outNodeCode.length() > 0;
    }

    // Sends a heartbeat and applies any config fields returned by the server to
    // NVS storage. Only non-null fields from the server overwrite local NVS so
    // on-device changes (e.g. alarm set directly on the screen) aren't erased
    // if the web setup page hasn't set that field yet.
    bool sendHeartbeat(uint32_t uptimeSeconds) {
        HTTPClient http;
        http.begin(ENDPOINT_HEARTBEAT);
        http.setTimeout(8000);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Authorization", String("Bearer ") + SUPABASE_ANON_KEY);

        JsonDocument reqDoc;
        reqDoc["mac_address"] = getMacAddress();
        reqDoc["uptime_seconds"] = uptimeSeconds;
        reqDoc["wifi_rssi"] = WiFi.RSSI();
        reqDoc["free_heap_bytes"] = ESP.getFreeHeap();
        String payload;
        serializeJson(reqDoc, payload);

        int statusCode = http.POST(payload);
        if (statusCode != 200) {
            Serial.printf("Heartbeat failed, HTTP %d\n", statusCode);
            http.end();
            return false;
        }

        // Apply config sync from response (null fields = no change).
        JsonDocument respDoc;
        if (deserializeJson(respDoc, http.getStream()) == DeserializationError::Ok) {
            JsonObjectConst cfg = respDoc["config"];
            if (!cfg.isNull()) {
                applyServerConfig(cfg);
            }
        }
        http.end();
        return true;
    }

    // Real TurboUSD treasury/supply/price data -- see config.h for the URL.
    TreasuryData fetchTreasuryData() {
        TreasuryData result;
        HTTPClient http;
        http.begin(ENDPOINT_TREASURY_DATA);
        http.setTimeout(8000);
        int statusCode = http.GET();
        if (statusCode != 200) {
            Serial.printf("fetchTreasuryData failed, HTTP %d\n", statusCode);
            http.end();
            return result;
        }

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, http.getStream());
        http.end();
        if (err) {
            Serial.printf("fetchTreasuryData JSON parse error: %s\n", err.c_str());
            return result;
        }

        result.tusdSupplyNum = doc["tusdSupplyNum"] | 0.0;
        result.tusdBurnedNum = doc["tusdBurnedNum"] | 0.0;
        result.tusdPriceUsd = doc["tusdPriceUsd"] | 0.0;
        result.treasuryValueUsd = doc["treasuryValueUsd"] | 0.0; // confirm exact field name against the live API before relying on this
        result.valid = true;
        return result;
    }

    // Real US national debt figure, from the Treasury's own Fiscal Data API.
    DebtData fetchUsDebt() {
        DebtData result;
        HTTPClient http;
        http.begin(ENDPOINT_US_DEBT);
        http.setTimeout(8000);
        int statusCode = http.GET();
        if (statusCode != 200) {
            Serial.printf("fetchUsDebt failed, HTTP %d\n", statusCode);
            http.end();
            return result;
        }

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, http.getStream());
        http.end();
        if (err) return result;

        // Fiscal Data API returns { data: [ { tot_pub_debt_out_amt: "..." } ] }
        const char* amountStr = doc["data"][0]["tot_pub_debt_out_amt"];
        if (amountStr) {
            result.totalDebtUsd = atof(amountStr);
            result.valid = true;
        }
        return result;
    }

    // Historical debt points for the chart's adjustable year-range
    // selector. Reads from our own Supabase cache (synced daily from
    // Treasury by sync-debt-history), NOT a direct Treasury call -- avoids
    // every device hammering a public government API every time someone
    // taps the range picker. See backend/functions/debt-history.
    int fetchDebtHistory(int yearsBack, DebtHistoryPoint* outPoints, int maxPoints) {
        HTTPClient http;
        String url = String(ENDPOINT_DEBT_HISTORY) + "?years=" + String(yearsBack);
        http.begin(url);
        http.addHeader("Authorization", String("Bearer ") + SUPABASE_ANON_KEY);
        http.addHeader("apikey", SUPABASE_ANON_KEY);
        http.setTimeout(8000);

        int statusCode = http.GET();
        if (statusCode != 200) {
            Serial.printf("fetchDebtHistory failed, HTTP %d\n", statusCode);
            http.end();
            return 0;
        }

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, http.getStream());
        http.end();
        if (err) return 0;

        int count = 0;
        for (JsonObject row : doc["points"].as<JsonArray>()) {
            if (count >= maxPoints) break;
            const char* dateStr = row["record_date"]; // "YYYY-MM-DD"
            if (dateStr && strlen(dateStr) >= 4) {
                outPoints[count].year = atoi(String(dateStr).substring(0, 4).c_str());
            }
            outPoints[count].totalDebtUsd = row["total_debt_usd"] | 0.0;
            count++;
        }
        return count;
    }

    // Real weekly OHLCV candles for the Turbo Stats screen's chart. Reads
    // from our own Supabase cache (synced daily from GeckoTerminal by
    // sync-ohlcv-history), NOT GeckoTerminal directly -- same rationale as
    // fetchDebtHistory(). See that table's comment for the free-tier
    // 6-month history limitation this inherits.
    int fetchOhlcvHistory(OhlcvCandle* outCandles, int maxCandles) {
        HTTPClient http;
        http.begin(ENDPOINT_OHLCV_HISTORY);
        http.setTimeout(8000);

        int statusCode = http.GET();
        if (statusCode != 200) {
            Serial.printf("fetchOhlcvHistory failed, HTTP %d\n", statusCode);
            http.end();
            return 0;
        }

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, http.getStream());
        http.end();
        if (err) return 0;

        int count = 0;
        for (JsonObject row : doc["candles"].as<JsonArray>()) {
            if (count >= maxCandles) break;
            outCandles[count].open = row["open_usd"] | 0.0;
            outCandles[count].high = row["high_usd"] | 0.0;
            outCandles[count].low = row["low_usd"] | 0.0;
            outCandles[count].close = row["close_usd"] | 0.0;
            count++;
        }
        return count;
    }

    // Last few mined blocks + the current pending one, for the "live mining
    // activity" animation on the Node & Network screen.
    int fetchMiningFeed(MiningFeedEntry* outEntries, int maxEntries) {
        HTTPClient http;
        http.begin(ENDPOINT_MINING_FEED);
        http.setTimeout(8000);
        http.addHeader("Authorization", String("Bearer ") + SUPABASE_ANON_KEY);
        http.addHeader("apikey", SUPABASE_ANON_KEY);

        int statusCode = http.GET();
        if (statusCode != 200) {
            http.end();
            return 0;
        }

        JsonDocument doc;
        deserializeJson(doc, http.getStream());
        http.end();

        int count = 0;
        for (JsonObject row : doc.as<JsonArray>()) {
            if (count >= maxEntries) break;
            outEntries[count].blockNumber = row["block_number"] | 0;
            outEntries[count].rewardTusd = row["reward_tusd"] | 0.0;
            outEntries[count].winnerDisplayName = row["winner_display_name"].as<String>();
            outEntries[count].mined = !row["mined_at"].isNull();
            count++;
        }
        return count;
    }

private:
    // Applies non-null fields from the heartbeat config payload to NVS.
    // Null JSON fields are skipped — they mean "not set yet, keep current value".
    void applyServerConfig(JsonObjectConst cfg) {
        // Display preferences
        if (!cfg["temp_unit"].isNull()) {
            const char* tu = cfg["temp_unit"];
            if (tu && (tu[0] == 'C' || tu[0] == 'F')) storage.setTempUnit(tu[0]);
        }
        if (!cfg["date_format"].isNull()) {
            const char* df = cfg["date_format"];
            if (df) storage.setDateFormat(String(df));
        }
        if (!cfg["time_format"].isNull()) {
            const char* tf = cfg["time_format"];
            if (tf) storage.setTimeFormat(String(tf));
        }

        // Alarm settings
        // Read current values first so we only write NVS when something actually changed
        uint8_t alarmHour    = storage.getAlarmHour();
        uint8_t alarmMinute  = storage.getAlarmMinute();
        bool    alarmEnabled = storage.getAlarmEnabled();
        bool    alarmChanged = false;
        if (!cfg["alarm_hour"].isNull())    { alarmHour    = cfg["alarm_hour"].as<uint8_t>();    alarmChanged = true; }
        if (!cfg["alarm_minute"].isNull())  { alarmMinute  = cfg["alarm_minute"].as<uint8_t>();  alarmChanged = true; }
        if (!cfg["alarm_enabled"].isNull()) { alarmEnabled = cfg["alarm_enabled"].as<bool>();     alarmChanged = true; }
        if (alarmChanged) storage.setAlarm(alarmHour, alarmMinute, alarmEnabled);
        if (!cfg["alarm_volume"].isNull()) {
            storage.setAlarmVolume(cfg["alarm_volume"].as<uint8_t>());
        }

        // Screen brightness (stored in NVS; main.cpp calls uiManager.applyStoredBrightness()
        // after sendHeartbeat() so the new value takes effect immediately, not just on next boot)
        if (!cfg["screen_brightness"].isNull()) {
            storage.setScreenBrightness(cfg["screen_brightness"].as<uint8_t>());
        }

        // Screen timeout settings (always-on toggle + inactivity timeout in minutes)
        if (!cfg["screen_always_on"].isNull()) {
            storage.setScreenAlwaysOn(cfg["screen_always_on"].as<bool>());
        }
        if (!cfg["screen_timeout_mins"].isNull()) {
            storage.setScreenTimeoutMins(cfg["screen_timeout_mins"].as<uint8_t>());
        }

        // NFT Gallery settings
        if (!cfg["nft_wallet_address"].isNull()) storage.setNftWallet(cfg["nft_wallet_address"].as<String>());
        if (!cfg["nft_grid_size"].isNull())      storage.setNftGridSize(cfg["nft_grid_size"].as<uint8_t>());
        if (!cfg["nft_carousel_enabled"].isNull()) storage.setNftCarousel(cfg["nft_carousel_enabled"].as<bool>());
        if (!cfg["nft_slideshow_secs"].isNull()) storage.setNftSlideshowSecs(cfg["nft_slideshow_secs"].as<uint8_t>());
        if (!cfg["nft_pinlist"].isNull())        storage.setNftPinlist(cfg["nft_pinlist"].as<String>());

        // Screen order
        if (!cfg["screen_order"].isNull()) storage.setScreenOrder(cfg["screen_order"].as<String>());
    }
};

extern ApiClient apiClient;
