// include/api_client.h — all outbound HTTP calls to our own backend
// (Supabase Edge Functions / REST) and to the external real-data sources
// (treasury.turbousd.com, US Treasury Fiscal Data API).
//
// Kept as one module so retry/timeout/JSON-parsing conventions are
// consistent everywhere instead of repeated ad-hoc per call site.
//
// IMPORTANT convention: every request whose body is parsed with
// deserializeJson(http.getStream()) MUST call http.useHTTP10(true) first.
// With HTTP/1.1, DexScreener / treasury.turbousd.com / GeckoTerminal /
// Supabase reply with "Transfer-Encoding: chunked", and Arduino-ESP32's
// HTTPClient does NOT de-chunk getStream() — the parser then sees the raw
// hex chunk-size lines and fails on EVERY response, which surfaced as
// "no data anywhere" (blank Turbo screen, empty ticker search, etc.).
// HTTP/1.0 forbids chunked, so the body arrives as plain bytes.
// (http.getString() de-chunks correctly, so callers using it are fine.)

#pragma once
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
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
    bool mined = false;         // false = this is the currently-pending block
    time_t createdAtUtc = 0;    // when the block was opened (drives the countdown ring)
    time_t minedAtUtc   = 0;    // when it was mined (countdown fallback: minedAt + 1 h)
};

// Parses "2026-07-05T12:34:56[.frac][+00:00|Z]" (UTC) → epoch seconds.
// Days-from-civil algorithm — mktime() would apply the local TZ offset.
static time_t parseIso8601Utc(const char* s) {
    int Y, M, D, h, m, sec;
    if (!s || sscanf(s, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &sec) != 6) return 0;
    int y = Y - (M <= 2);
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (M + (M > 2 ? -3 : 9)) + 2) / 5 + D - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = (long)era * 146097 + (long)doe - 719468;
    return (time_t)days * 86400 + h * 3600 + m * 60 + sec;
}

struct LeaderboardEntry {
    char   name[24]   = {};   // display name, or "#CODE" fallback
    double earned     = 0;
    int    uptimePct  = 0;
    bool   online     = false;
};

struct GeoLocale {
    bool    valid = false;
    char    countryCode[3] = {0, 0, 0};  // ISO 3166-1 alpha-2, uppercase
    int32_t utcOffsetSec   = 0;          // seconds east of UTC, incl. current DST
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

    // Geo-IP lookup for locale autodetect. Returns the device's country code and
    // current UTC offset (incl. DST) from its public IP. Best-effort: returns
    // false on any network/parse error and the caller keeps current settings.
    bool fetchGeoLocale(GeoLocale& out) {
        HTTPClient http;
        http.useHTTP10(true);   // body is parsed from getStream() — see header note
        http.begin(ENDPOINT_GEO_IP);
        http.setTimeout(8000);
        int code = http.GET();
        if (code != 200) { http.end(); return false; }

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, http.getStream());
        http.end();
        if (err) return false;

        const char* status = doc["status"] | "";
        if (strcmp(status, "success") != 0) return false;
        const char* cc = doc["countryCode"] | "";
        if (!cc[0] || !cc[1]) return false;

        out.countryCode[0] = (char)toupper((unsigned char)cc[0]);
        out.countryCode[1] = (char)toupper((unsigned char)cc[1]);
        out.countryCode[2] = 0;
        out.utcOffsetSec   = doc["offset"] | 0;
        out.valid = true;
        return true;
    }

    // Derive sensible display-locale defaults from an ISO 3166-1 country code.
    // These are common regional conventions (CLDR-style), not hard rules — the
    // user can override any of them, which then locks out further auto-config.
    static void localeDefaultsForCountry(const char* cc,
                                         char& tempUnit,       // 'C' or 'F'
                                         String& dateFormat,   // "DD/MM" or "MM/DD"
                                         String& timeFormat,   // "24H" or "AMPM"
                                         uint8_t& weekStart)   // 0 = Sun, 1 = Mon
    {
        // Fahrenheit: US + a handful of territories/countries.
        static const char* F[]   = {"US","BS","BZ","KY","PW","FM","MH","LR"};
        // MM/DD date order: essentially the US (+ a couple of Pacific territories).
        static const char* MDY[] = {"US","FM","MH","PW"};
        // 12-hour clock is the everyday norm here; most of the world writes 24h.
        static const char* H12[] = {"US","CA","AU","NZ","PH","IN","PK","BD","EG","SA","CO","MX"};
        // Week starts Sunday across the Americas, Japan, Korea, Israel, India, ZA…
        static const char* SUN[] = {"US","CA","MX","BR","AR","CO","PE","VE","CL",
                                    "JP","KR","IL","IN","ZA","PH","HK","TW"};

        tempUnit   = ccInList(cc, F,   sizeof(F)/sizeof(F[0]))   ? 'F' : 'C';
        dateFormat = ccInList(cc, MDY, sizeof(MDY)/sizeof(MDY[0])) ? "MM/DD" : "DD/MM";
        timeFormat = ccInList(cc, H12, sizeof(H12)/sizeof(H12[0])) ? "AMPM" : "24H";
        weekStart  = ccInList(cc, SUN, sizeof(SUN)/sizeof(SUN[0])) ? 0 : 1;
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
        doc["setup_token"] = storage.getSetupToken();   // owner-only web setup — see storage.h
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
        http.useHTTP10(true);   // response config is parsed from getStream()
        http.begin(ENDPOINT_HEARTBEAT);
        http.setTimeout(8000);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Authorization", String("Bearer ") + SUPABASE_ANON_KEY);

        JsonDocument reqDoc;
        reqDoc["mac_address"] = getMacAddress();
        reqDoc["uptime_seconds"] = uptimeSeconds;
        reqDoc["wifi_rssi"] = WiFi.RSSI();
        reqDoc["free_heap_bytes"] = ESP.getFreeHeap();
        // Keeps the server-side copy of the owner token fresh — this is how
        // EXISTING nodes (registered before tokens existed) get one stored.
        reqDoc["setup_token"] = storage.getSetupToken();

        // Alarm changed ON THE DEVICE since the last sync → push it up, so
        // the server copy (and therefore future config syncs) reflect it
        // instead of silently reverting the user's change.
        bool alarmWasDirty = storage.getAlarmDirty();
        if (alarmWasDirty) {
            reqDoc["alarm_hour"]    = storage.getAlarmHour();
            reqDoc["alarm_minute"]  = storage.getAlarmMinute();
            reqDoc["alarm_enabled"] = storage.getAlarmEnabled();
            reqDoc["alarm_days"]    = storage.getAlarmDays();
        }

        // NFT gallery edited ON THE DEVICE (gear mode / Data toggle) → push up.
        bool nftListsWereDirty = storage.getNftListsDirty();
        if (nftListsWereDirty) {
            reqDoc["nft_coll_order"]  = storage.getNftCollOrder();
            reqDoc["nft_coll_hidden"] = storage.getNftHidden();
            reqDoc["nft_show_data"]   = storage.getNftShowData();
        }
        // Detected collections list changed → report it (feeds the web board).
        bool collsWereDirty = storage.getNftCollsDirty();
        if (collsWereDirty) {
            String rep = storage.getNftCollsReport();
            if (rep.length()) {
                reqDoc["nft_collections"] = serialized(rep);
                Serial.printf("heartbeat: pushing NFT collections report (%u bytes)\n",
                              (unsigned)rep.length());
            }
        }
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
                applyServerConfig(cfg, /*skipAlarm=*/alarmWasDirty,
                                  /*skipNftLists=*/nftListsWereDirty);
            }
        }
        http.end();
        if (alarmWasDirty)    storage.clearAlarmDirty();       // pushed successfully
        if (nftListsWereDirty) storage.setNftListsDirty(false);
        if (collsWereDirty)    storage.setNftCollsDirty(false);
        return true;
    }

    // Real TurboUSD treasury/supply/price data -- see config.h for the URL.
    TreasuryData fetchTreasuryData() {
        TreasuryData result;
        // The treasury response is large (~40 KB). The old 8 s timeout often
        // expired mid-download, so SUPPLY / TOTAL BURNED stayed "--". Just give
        // it more time on the same plain client the other https calls use (an
        // explicit WiFiClientSecure here was memory-heavy and destabilised the
        // heap). http.begin(url) already negotiates TLS via the cert bundle.
        HTTPClient http;
        http.useHTTP10(true);   // treasury API replies chunked on HTTP/1.1 — see header note
        http.begin(ENDPOINT_TREASURY_DATA);
        http.setTimeout(20000);
        int statusCode = http.GET();
        if (statusCode != 200) {
            Serial.printf("fetchTreasuryData failed, HTTP %d\n", statusCode);
            http.end();
            return result;
        }

        // The response is ~30 KB (huge chartData/operations arrays). Parse ONLY
        // the four scalar fields we need with a filter, so ArduinoJson doesn't
        // run out of heap trying to build the whole document (which left the
        // whole screen blank).
        JsonDocument filter;
        filter["tusdSupplyNum"]  = true;
        filter["tusdBurnedNum"]  = true;
        filter["tusdPriceUsd"]   = true;
        filter["totalManagedUsd"] = true;

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, http.getStream(),
                                                   DeserializationOption::Filter(filter));
        http.end();
        if (err) {
            Serial.printf("fetchTreasuryData JSON parse error: %s\n", err.c_str());
            return result;
        }

        result.tusdSupplyNum    = doc["tusdSupplyNum"]  | 0.0;
        result.tusdBurnedNum    = doc["tusdBurnedNum"]  | 0.0;
        result.tusdPriceUsd     = doc["tusdPriceUsd"]   | 0.0;
        result.treasuryValueUsd = doc["totalManagedUsd"] | 0.0;  // field is totalManagedUsd
        result.valid = true;
        return result;
    }

    // Live TUSD price in USD from a DEX aggregator: DexScreener first, then
    // GeckoTerminal as a fallback. Cached for TUSD_PRICE_CACHE_MS to avoid rate
    // limits; on total failure returns the last good value (0 if never fetched).
    double fetchTusdPrice() {
        static double   cached = 0;
        static uint32_t lastAt = 0;
        if (cached > 0 && (millis() - lastAt) < TUSD_PRICE_CACHE_MS) return cached;
        double price = _fetchPriceDexScreener();
        if (price <= 0) price = _fetchPriceGecko();
        if (price > 0) { cached = price; lastAt = millis(); return price; }
        return cached;  // both sources failed → keep showing the last good price
    }

    // Real US national debt figure, from the Treasury's own Fiscal Data API.
    DebtData fetchUsDebt() {
        DebtData result;
        HTTPClient http;
        http.useHTTP10(true);   // see header note
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

    // Historical debt points for the chart's adjustable year-range selector.
    // Reads the Treasury "Historical Debt Outstanding" dataset DIRECTLY from
    // the public Fiscal Data API (annual, small, no auth) — so the chart works
    // out of the box without the Supabase debt-history function being deployed.
    // Returns points oldest-first, capped to `yearsBack` and `maxPoints`.
    int fetchDebtHistory(int yearsBack, DebtHistoryPoint* outPoints, int maxPoints) {
        HTTPClient http;
        http.useHTTP10(true);   // see header note
        http.begin("https://api.fiscaldata.treasury.gov/services/api/fiscal_service/v2/accounting/od/"
                   "debt_outstanding?fields=record_date,debt_outstanding_amt&sort=-record_date&page[size]=120");
        http.setTimeout(9000);
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

        // API returns newest-first. Keep those within yearsBack, then reverse.
        static DebtHistoryPoint tmp[120];
        int n = 0, newestYear = 0;
        for (JsonObject row : doc["data"].as<JsonArray>()) {
            if (n >= 120) break;
            const char* dateStr = row["record_date"];       // "YYYY-MM-DD"
            const char* amtStr  = row["debt_outstanding_amt"];
            if (!dateStr || !amtStr || strlen(dateStr) < 4) continue;
            int yr = atoi(String(dateStr).substring(0, 4).c_str());
            if (newestYear == 0) newestYear = yr;
            if (yr < newestYear - yearsBack) break;          // older than the range
            tmp[n].year = yr;
            tmp[n].totalDebtUsd = atof(amtStr);
            n++;
        }
        int count = 0;
        for (int i = n - 1; i >= 0 && count < maxPoints; i--) outPoints[count++] = tmp[i];
        return count;
    }

    // Real weekly OHLCV candles for the Turbo Stats screen's chart. Reads
    // from our own Supabase cache (synced daily from GeckoTerminal by
    // sync-ohlcv-history), NOT GeckoTerminal directly -- same rationale as
    // fetchDebtHistory(). See that table's comment for the free-tier
    // 6-month history limitation this inherits.
    // groupDays: 1 = daily, 7 = weekly (default; served by our Supabase
    // cache), 30 = monthly. Non-weekly goes straight to GeckoTerminal with
    // client-side aggregation — the cache only stores weekly candles.
    int fetchOhlcvHistory(OhlcvCandle* outCandles, int maxCandles, int groupDays = 7) {
        if (groupDays != 7) return _fetchOhlcvGecko(outCandles, maxCandles, groupDays);
        HTTPClient http;
        http.useHTTP10(true);   // see header note
        http.begin(ENDPOINT_OHLCV_HISTORY);
        http.setTimeout(8000);
        // Edge Functions are deployed with JWT verification by default — send
        // the anon key or the request never reaches the function.
        http.addHeader("Authorization", String("Bearer ") + SUPABASE_ANON_KEY);
        http.addHeader("apikey", SUPABASE_ANON_KEY);

        int statusCode = http.GET();
        int count = 0;
        if (statusCode == 200) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, http.getStream());
            if (!err) {
                for (JsonObject row : doc["candles"].as<JsonArray>()) {
                    if (count >= maxCandles) break;
                    outCandles[count].open = row["open_usd"] | 0.0;
                    outCandles[count].high = row["high_usd"] | 0.0;
                    outCandles[count].low = row["low_usd"] | 0.0;
                    outCandles[count].close = row["close_usd"] | 0.0;
                    count++;
                }
            }
        } else {
            Serial.printf("fetchOhlcvHistory failed, HTTP %d\n", statusCode);
        }
        http.end();
        if (count > 0) return count;

        // Fallback: our Supabase cache is unavailable (function not deployed,
        // sync never ran → HTTP 500, table empty…). Pull weekly candles for
        // the TUSD pool straight from GeckoTerminal so the chart still works.
        // Free tier returns ~6 months of history — same limit the cache has.
        return _fetchOhlcvGecko(outCandles, maxCandles, 7);
    }

    // GeckoTerminal weekly OHLCV for the TUSD pool. GT's OHLCV endpoint only
    // accepts day/hour/minute timeframes (NOT week — it returns HTTP 400,
    // which is why this fallback used to yield an empty chart), so we fetch
    // DAILY candles and aggregate 7 days per weekly candle here. ohlcv_list
    // rows are [ts, open, high, low, close, volume], NEWEST first.
    int _fetchOhlcvGecko(OhlcvCandle* outCandles, int maxCandles, int groupDays) {
        HTTPClient http;
        http.useHTTP10(true);   // see header note
        http.begin(String(ENDPOINT_GECKOTERMINAL_OHLCV) + TUSD_CHAIN_SLUG +
                   "/pools/" + TUSD_POOL_ADDR +
                   "/ohlcv/day?aggregate=1&limit=" + String(maxCandles * groupDays) + "&currency=usd&token=base");
        http.setTimeout(12000);
        http.addHeader("Accept", "application/json");
        if (http.GET() != 200) { http.end(); return 0; }

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, http.getStream());
        http.end();
        if (err) return 0;

        JsonArray list = doc["data"]["attributes"]["ohlcv_list"].as<JsonArray>();
        int totalDays = list.size();
        if (totalDays == 0) return 0;

        // Group newest-first days into candles of `groupDays`, emit oldest-first.
        int weeks = (totalDays + groupDays - 1) / groupDays;
        if (weeks > maxCandles) weeks = maxCandles;
        int count = 0;
        for (int wk = weeks - 1; wk >= 0; wk--) {       // oldest group first
            int from = wk * groupDays;                  // newest day of this group
            int to   = min(from + groupDays, totalDays); // exclusive
            if (from >= totalDays) continue;
            OhlcvCandle c;
            // Within the group, index `from` is the NEWEST day, `to-1` the oldest.
            c.open  = list[to - 1][1] | 0.0;            // oldest day's open
            c.close = list[from][4]   | 0.0;            // newest day's close
            c.high  = 0.0; c.low = 0.0;
            for (int d = from; d < to; d++) {
                double hi = list[d][2] | 0.0, lo = list[d][3] | 0.0;
                if (hi > c.high) c.high = hi;
                if (c.low == 0.0 || (lo > 0.0 && lo < c.low)) c.low = lo;
            }
            outCandles[count++] = c;
        }
        return count;
    }

    // Public node directory (name, earnings, uptime) for the on-device
    // leaderboard — mirrors the two-column board on the web's network page.
    int fetchNodeDirectory(LeaderboardEntry* out, int maxEntries) {
        HTTPClient http;
        http.useHTTP10(true);   // see header note
        http.begin(String(SUPABASE_REST_BASE_URL) +
                   "/public_node_directory?select=display_name,node_code,total_tusd_earned,uptime_pct,is_online&limit=24");
        http.setTimeout(8000);
        http.addHeader("Authorization", String("Bearer ") + SUPABASE_ANON_KEY);
        http.addHeader("apikey", SUPABASE_ANON_KEY);
        if (http.GET() != 200) { http.end(); return 0; }

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, http.getStream());
        http.end();
        if (err) return 0;

        int count = 0;
        for (JsonObject row : doc.as<JsonArray>()) {
            if (count >= maxEntries) break;
            LeaderboardEntry& e = out[count];
            const char* dn = row["display_name"] | "";
            if (dn[0]) snprintf(e.name, sizeof(e.name), "%s", dn);
            else       snprintf(e.name, sizeof(e.name), "#%s", row["node_code"] | "????");
            e.earned    = row["total_tusd_earned"] | 0.0;
            e.uptimePct = row["uptime_pct"] | 0;
            e.online    = row["is_online"] | false;
            count++;
        }
        return count;
    }

    // Last few mined blocks + the current pending one, for the "live mining
    // activity" animation on the Node & Network screen.
    int fetchMiningFeed(MiningFeedEntry* outEntries, int maxEntries) {
        HTTPClient http;
        http.useHTTP10(true);   // see header note
        http.begin(ENDPOINT_MINING_FEED);
        http.setTimeout(8000);
        http.addHeader("Authorization", String("Bearer ") + SUPABASE_ANON_KEY);
        http.addHeader("apikey", SUPABASE_ANON_KEY);

        int statusCode = http.GET();
        if (statusCode != 200) {
            Serial.printf("miningFeed: HTTP %d\n", statusCode);
            http.end();
            return 0;
        }

        JsonDocument doc;
        DeserializationError ferr = deserializeJson(doc, http.getStream());
        http.end();
        if (ferr) { Serial.printf("miningFeed: parse %s\n", ferr.c_str()); return 0; }

        int count = 0;
        for (JsonObject row : doc.as<JsonArray>()) {
            if (count >= maxEntries) break;
            outEntries[count].blockNumber = row["block_number"] | 0;
            outEntries[count].rewardTusd = row["reward_tusd"] | 0.0;
            // Winner: display name, else "#CODE", else empty.
            if (!row["winner_display_name"].isNull())
                outEntries[count].winnerDisplayName = row["winner_display_name"].as<String>();
            else if (!row["winner_node_code"].isNull())
                outEntries[count].winnerDisplayName = String("#") + row["winner_node_code"].as<String>();
            else
                outEntries[count].winnerDisplayName = "";
            outEntries[count].mined = !row["mined_at"].isNull();
            outEntries[count].createdAtUtc = parseIso8601Utc(row["created_at"] | "");
            outEntries[count].minedAtUtc   = parseIso8601Utc(row["mined_at"]   | "");
            count++;
        }
        // Decisive trace: if the Node screen is empty, this line tells us
        // whether the data arrived (parse side) or the fetch failed (above).
        Serial.printf("miningFeed: %d entries, first block #%ld created=%ld mined=%ld\n",
                      count,
                      count ? (long)outEntries[0].blockNumber : 0L,
                      count ? (long)outEntries[0].createdAtUtc : 0L,
                      count ? (long)outEntries[0].minedAtUtc : 0L);
        return count;
    }

private:
    // DexScreener: GET /tokens/{contract} → pairs[]; prefer our exact pool.
    double _fetchPriceDexScreener() {
        HTTPClient http;
        http.useHTTP10(true);   // DexScreener replies chunked on HTTP/1.1 — see header note
        http.begin(String(ENDPOINT_DEXSCREENER_TOKENS) + TUSD_CONTRACT_ADDR);
        http.setTimeout(8000);
        if (http.GET() != 200) { http.end(); return 0; }
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, http.getStream());
        http.end();
        if (err) return 0;
        double best = 0;
        for (JsonObject pair : doc["pairs"].as<JsonArray>()) {
            double p = atof(pair["priceUsd"] | "0");
            const char* addr = pair["pairAddress"] | "";
            if (p > 0 && strcasecmp(addr, TUSD_POOL_ADDR) == 0) return p;  // exact pool
            if (p > best) best = p;
        }
        return best;  // no exact pool match → highest-priced pair for the token
    }

    // GeckoTerminal fallback: GET /networks/{net}/pools/{pool}.
    double _fetchPriceGecko() {
        HTTPClient http;
        http.useHTTP10(true);   // see header note
        http.begin(String(ENDPOINT_GECKOTERMINAL_OHLCV) + TUSD_CHAIN_SLUG + "/pools/" + TUSD_POOL_ADDR);
        http.setTimeout(8000);
        if (http.GET() != 200) { http.end(); return 0; }
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, http.getStream());
        http.end();
        if (err) return 0;
        const char* p = doc["data"]["attributes"]["base_token_price_usd"] | "0";
        double val = atof(p);
        if (val <= 0) { p = doc["data"]["attributes"]["quote_token_price_usd"] | "0"; val = atof(p); }
        return val;
    }

    // True if the 2-char country code `cc` is in `list` (n entries).
    static bool ccInList(const char* cc, const char* const* list, int n) {
        for (int i = 0; i < n; i++)
            if (cc[0] == list[i][0] && cc[1] == list[i][1]) return true;
        return false;
    }

    // Applies non-null fields from the heartbeat config payload to NVS.
    // Null JSON fields are skipped — they mean "not set yet, keep current value".
    // skipAlarm: true while a device-side alarm change is being pushed up —
    // the server copy is (at best) what we just sent, and applying it back
    // could race/revert the local value.
    void applyServerConfig(JsonObjectConst cfg, bool skipAlarm = false, bool skipNftLists = false) {
        // Node identity (Node & Network screen headline)
        if (!cfg["display_name"].isNull())      storage.setDisplayName(cfg["display_name"].as<String>());
        if (!cfg["is_verified"].isNull())       storage.setIsVerified(cfg["is_verified"].as<bool>());
        if (!cfg["total_tusd_earned"].isNull()) storage.setTotalEarned(cfg["total_tusd_earned"].as<float>());

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
        if (!cfg["week_start"].isNull()) {
            // Accept "mon"/"sun" or 1/0.
            const char* ws = cfg["week_start"];
            if (ws) storage.setWeekStart((ws[0] == 's' || ws[0] == 'S' || ws[0] == '0') ? 0 : 1);
            else    storage.setWeekStart(cfg["week_start"].as<int>() == 0 ? 0 : 1);
        }
        if (!cfg["tz_offset_sec"].isNull()) {
            storage.setTzOffsetSec(cfg["tz_offset_sec"].as<int32_t>());
        }
        // A server-pushed locale (from the node settings page) is the user's
        // explicit choice → lock out geo-IP so it can't later override it.
        if (!cfg["temp_unit"].isNull() || !cfg["date_format"].isNull() ||
            !cfg["time_format"].isNull() || !cfg["week_start"].isNull()) {
            storage.setLocaleLocked(true);
        }

        // Alarm settings — skipped while a device-side change is in flight
        // (see the skipAlarm parameter note above).
        if (!skipAlarm) {
            // Read current values first so we only write NVS when something actually changed
            uint8_t alarmHour    = storage.getAlarmHour();
            uint8_t alarmMinute  = storage.getAlarmMinute();
            bool    alarmEnabled = storage.getAlarmEnabled();
            bool    alarmChanged = false;
            if (!cfg["alarm_hour"].isNull())    { alarmHour    = cfg["alarm_hour"].as<uint8_t>();    alarmChanged = true; }
            if (!cfg["alarm_minute"].isNull())  { alarmMinute  = cfg["alarm_minute"].as<uint8_t>();  alarmChanged = true; }
            if (!cfg["alarm_enabled"].isNull()) { alarmEnabled = cfg["alarm_enabled"].as<bool>();     alarmChanged = true; }
            if (alarmChanged) {
                storage.setAlarm(alarmHour, alarmMinute, alarmEnabled);
                storage.clearAlarmDirty();   // server-originated — nothing to push back
            }
            if (!cfg["alarm_volume"].isNull()) {
                storage.setAlarmVolume(cfg["alarm_volume"].as<uint8_t>());
            }
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
        if (!cfg["ticker_cols"].isNull())       storage.setTickerCols(cfg["ticker_cols"].as<uint8_t>());
        if (!cfg["nft_carousel_enabled"].isNull()) storage.setNftCarousel(cfg["nft_carousel_enabled"].as<bool>());
        if (!cfg["nft_slideshow_secs"].isNull()) storage.setNftSlideshowSecs(cfg["nft_slideshow_secs"].as<uint8_t>());
        if (!cfg["nft_pinlist"].isNull())        storage.setNftPinlist(cfg["nft_pinlist"].as<String>());

        // Screen order
        if (!cfg["screen_order"].isNull())  storage.setScreenOrder(cfg["screen_order"].as<String>());
        if (!cfg["screen_hidden"].isNull()) storage.setScreenHidden(cfg["screen_hidden"].as<String>());
        if (!skipNftLists) {
            if (!cfg["nft_show_data"].isNull())   storage.setNftShowData(cfg["nft_show_data"].as<bool>());
            if (!cfg["nft_coll_order"].isNull())  storage.setNftCollOrder(cfg["nft_coll_order"].as<String>());
            if (!cfg["nft_coll_hidden"].isNull()) storage.setNftHidden(cfg["nft_coll_hidden"].as<String>());
        }
    }
};

extern ApiClient apiClient;
