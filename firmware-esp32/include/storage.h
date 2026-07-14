// include/storage.h — thin wrapper around the ESP32 Preferences (NVS) API.
// Centralizing this means the rest of the codebase never touches NVS
// directly, which makes it much easier to change the storage backend later
// (or add encryption) without hunting through every module.

#pragma once
#include <Preferences.h>
#include "config.h"

class Storage {
public:
    void begin() {
        prefs.begin(NVS_NAMESPACE, /* readOnly= */ false);
    }

    // --- WiFi credentials ---
    bool hasWifiCredentials() {
        return prefs.isKey(NVS_KEY_WIFI_SSID) && prefs.isKey(NVS_KEY_WIFI_PASS);
    }
    String getWifiSsid() { return prefs.getString(NVS_KEY_WIFI_SSID, ""); }
    String getWifiPass() { return prefs.getString(NVS_KEY_WIFI_PASS, ""); }
    void setWifiCredentials(const String& ssid, const String& pass) {
        prefs.putString(NVS_KEY_WIFI_SSID, ssid);
        prefs.putString(NVS_KEY_WIFI_PASS, pass);
    }
    void clearWifiCredentials() {
        prefs.remove(NVS_KEY_WIFI_SSID);
        prefs.remove(NVS_KEY_WIFI_PASS);
    }

    // --- Node identity ---
    String getMacAddress() { return prefs.getString(NVS_KEY_MAC_ADDRESS, ""); }
    void setMacAddress(const String& mac) { prefs.putString(NVS_KEY_MAC_ADDRESS, mac); }

    bool hasNodeCode() { return prefs.isKey(NVS_KEY_NODE_CODE); }
    String getNodeCode() { return prefs.getString(NVS_KEY_NODE_CODE, ""); }
    void setNodeCode(const String& code) { prefs.putString(NVS_KEY_NODE_CODE, code); }

    // --- Cumulative uptime (across reboots) ---
    // RAM only: seeded from the server's total_uptime_seconds on each heartbeat
    // and ticked locally in between, so the Node screen shows LIFETIME uptime
    // instead of resetting to "1m" on every reboot. Until the first heartbeat
    // syncs it, we fall back to this session's since-boot time.
    void setTotalUptime(uint32_t secs) { _totalUptBase = secs; _totalUptBaseMs = millis(); }
    uint32_t getTotalUptimeSecs() {
        if (_totalUptBaseMs == 0) return millis() / 1000;
        return _totalUptBase + (millis() - _totalUptBaseMs) / 1000;
    }

    // --- Node identity (synced down from the backend on each heartbeat) ---
    String getDisplayName()            { return prefs.isKey("disp_name") ? prefs.getString("disp_name", "") : ""; }
    void   setDisplayName(const String& n) { prefs.putString("disp_name", n); }
    bool   getIsVerified()             { return prefs.getBool("verified", false); }
    void   setIsVerified(bool v)       { prefs.putBool("verified", v); }
    float  getTotalEarned()            { return prefs.getFloat("earned", 0.0f); }
    void   setTotalEarned(float v)     { prefs.putFloat("earned", v); }

    // --- Setup token (owner-only web setup) ---
    // Random per-device secret, generated ONCE on first use and persisted.
    // It is embedded in the Settings QR (/setup/CODE?t=TOKEN) and reported to
    // the backend via register/heartbeat; the web setup page must present it
    // to read or change this node's config. Whoever physically holds the
    // device can see the QR → they are the owner. 64 bits of esp_random()
    // (hardware RNG) is plenty for a low-value, rate-limited endpoint.
    String getSetupToken() {
        String t = prefs.getString(NVS_KEY_SETUP_TOKEN, "");
        if (t.length() == 0) {
            char buf[17];
            snprintf(buf, sizeof(buf), "%08lx%08lx",
                     (unsigned long)esp_random(), (unsigned long)esp_random());
            t = String(buf);
            prefs.putString(NVS_KEY_SETUP_TOKEN, t);
        }
        return t;
    }

    // --- Alarm ---
    // alarm_dirty: set whenever the alarm is changed ON THE DEVICE, cleared
    // once the heartbeat has pushed it to the backend. While dirty, the
    // heartbeat's config sync must NOT overwrite the local alarm — that was
    // the "the alarm I set on the device doesn't stick" bug (the next
    // heartbeat silently restored the server's older copy).
    bool getAlarmDirty()   { return prefs.getBool("alarm_dirty", false); }
    void clearAlarmDirty() { prefs.putBool("alarm_dirty", false); }

    uint8_t getAlarmHour() { return prefs.getUChar(NVS_KEY_ALARM_HOUR, 7); }
    uint8_t getAlarmMinute() { return prefs.getUChar(NVS_KEY_ALARM_MIN, 30); }
    bool getAlarmEnabled() { return prefs.getBool(NVS_KEY_ALARM_ON, true); }
    void setAlarm(uint8_t hour, uint8_t minute, bool enabled) {
        prefs.putBool("alarm_dirty", true);
        prefs.putUChar(NVS_KEY_ALARM_HOUR, hour);
        prefs.putUChar(NVS_KEY_ALARM_MIN, minute);
        prefs.putBool(NVS_KEY_ALARM_ON, enabled);
    }

    // Alarm volume: 1 (whisper) – 5 (max). Default 2 = soft, not startling.
    uint8_t getAlarmVolume()           { return prefs.getUChar("alarm_vol", 2); }
    void    setAlarmVolume(uint8_t v)  { prefs.putUChar("alarm_vol", constrain(v, 1, 5)); }

    // Screen brightness: 1 (dim) – 5 (full). Default 5 = maximum, matches
    // the factory firmware which drives GPIO 45 HIGH permanently.
    uint8_t getScreenBrightness()          { return prefs.getUChar("screen_brt", 5); }
    void    setScreenBrightness(uint8_t v) { prefs.putUChar("screen_brt", constrain(v, 1, 5)); }

    // Screen always-on: when true the backlight never turns off automatically.
    // Default true (matches factory behaviour).
    bool getScreenAlwaysOn()          { return prefs.getBool("scr_always", true); }
    void setScreenAlwaysOn(bool v)    { prefs.putBool("scr_always", v); }

    // Screen timeout in minutes: how long after the last touch before the
    // backlight is turned off. Only used when getScreenAlwaysOn() == false.
    // Options: 1, 5, 10, 30. Default 10.
    uint8_t getScreenTimeoutMins()          { return prefs.getUChar("scr_timeout", 10); }
    void    setScreenTimeoutMins(uint8_t v) { prefs.putUChar("scr_timeout", v); }

    // Auto screen carousel: when true the UI advances through every screen on a
    // timer, looping back to Home. Default OFF. Seconds-per-screen default 10.
    // A device-side change marks it dirty so the next heartbeat pushes it up
    // (bidirectional sync with the web, same pattern as the alarm settings).
    bool    getScreenCarousel()             { return prefs.getBool("scr_carou", false); }
    void    setScreenCarousel(bool v)       { prefs.putBool("scr_carou", v); prefs.putBool("scr_carou_d", true); }
    uint8_t getScreenCarouselSecs()         { return prefs.getUChar("scr_carou_s", 10); }
    void    setScreenCarouselSecs(uint8_t v){ prefs.putUChar("scr_carou_s", constrain(v, 3, 120)); prefs.putBool("scr_carou_d", true); }
    bool    getScreenCarouselDirty()        { return prefs.getBool("scr_carou_d", false); }
    void    clearScreenCarouselDirty()      { prefs.putBool("scr_carou_d", false); }

    // Ticker Stats screen: which DEX pool the screen shows stats for. Chosen via
    // the footer picker on-device OR the web setting; the backend ticker-stats
    // function resolves it to display fields. Defaults to ₸USD (its own pool).
    // A device-side change marks it dirty so the next heartbeat pushes it up
    // (same bidirectional pattern as the carousel/alarm settings).
    String  getTickerStatsPool()            { return prefs.getString("ts_pool",   TUSD_POOL_ADDR); }
    String  getTickerStatsChain()           { return prefs.getString("ts_chain",  TUSD_CHAIN_SLUG); }
    String  getTickerStatsSymbol()          { return prefs.getString("ts_sym",    "TUSD"); }
    void    setTickerStatsPool(const String& v)   { prefs.putString("ts_pool",  v); prefs.putBool("ts_dirty", true); }
    void    setTickerStatsChain(const String& v)  { prefs.putString("ts_chain", v); prefs.putBool("ts_dirty", true); }
    void    setTickerStatsSymbol(const String& v) { prefs.putString("ts_sym",   v); prefs.putBool("ts_dirty", true); }
    // Server-originated apply (no dirty flag — nothing to push back).
    void    applyTickerStatsFromServer(const String& pool, const String& chain, const String& sym) {
        if (pool.length())  prefs.putString("ts_pool",  pool);
        if (chain.length()) prefs.putString("ts_chain", chain);
        if (sym.length())   prefs.putString("ts_sym",   sym);
    }
    bool    getTickerStatsDirty()           { return prefs.getBool("ts_dirty", false); }
    void    clearTickerStatsDirty()         { prefs.putBool("ts_dirty", false); }

    // Home (first screen) background image URL. Set from the web only; empty =
    // plain black background (default). The device downloads + paints it and
    // draws a shadow behind the clock/alarm box for legibility.
    String  getHomeBgUrl()                  { return prefs.getString("home_bg", ""); }
    void    setHomeBgUrl(const String& v)   { prefs.putString("home_bg", v); }

    // Bitmask of active alarm days: bit0=Mon, bit1=Tue, …, bit6=Sun (ISO order).
    // Default 0x7F = all seven days active.
    uint8_t getAlarmDays() { return prefs.getUChar(NVS_KEY_ALARM_DAYS, 0x7F); }
    void setAlarmDays(uint8_t mask) { prefs.putBool("alarm_dirty", true); prefs.putUChar(NVS_KEY_ALARM_DAYS, mask); }

    // Returns true if alarm is globally enabled AND today's weekday is active.
    // tmWday follows struct tm convention: 0 = Sunday, 1 = Monday, …, 6 = Saturday.
    bool isAlarmActiveToday(int tmWday) {
        if (!getAlarmEnabled()) return false;
        // Map tm_wday (0=Sun … 6=Sat) → our bitmask bit index (bit0=Mon … bit6=Sun)
        static const int wdayToBit[7] = { 6, 0, 1, 2, 3, 4, 5 };
        return (getAlarmDays() >> wdayToBit[tmWday % 7]) & 1;
    }

    // --- Display preferences ---
    char getTempUnit() { return prefs.getString(NVS_KEY_TEMP_UNIT, "C")[0]; }
    void setTempUnit(char unit) { prefs.putString(NVS_KEY_TEMP_UNIT, String(unit)); }

    String getDateFormat() { return prefs.getString(NVS_KEY_DATE_FMT, "DD/MM"); }
    void setDateFormat(const String& fmt) { prefs.putString(NVS_KEY_DATE_FMT, fmt); }

    String getTimeFormat() { return prefs.getString(NVS_KEY_TIME_FMT, "24H"); }
    void setTimeFormat(const String& fmt) { prefs.putString(NVS_KEY_TIME_FMT, fmt); }

    // Week start: 0 = Sunday, 1 = Monday. Used by the calendar popup and the
    // alarm day-of-week strip. Default Monday (ISO). Auto-set from geo-IP unless
    // the user has locked locale (see getLocaleLocked).
    uint8_t getWeekStart() { return prefs.getUChar(NVS_KEY_WEEK_START, 1); }
    void setWeekStart(uint8_t v) { prefs.putUChar(NVS_KEY_WEEK_START, v ? 1 : 0); }

    // Timezone offset in seconds east of UTC, including current DST. Applied via
    // configTime() so the clock + alarm run on local time. 0 = UTC (the value
    // until the first successful geo-IP sync).
    int32_t getTzOffsetSec() { return (int32_t)prefs.getInt(NVS_KEY_TZ_OFFSET, 0); }
    void setTzOffsetSec(int32_t s) { prefs.putInt(NVS_KEY_TZ_OFFSET, s); }

    // Set true the moment the user changes any locale setting (temp unit, date/
    // time format, week start) on the device. Once locked, geo-IP auto-config
    // stops touching those so it can never stomp the user's explicit choice.
    bool getLocaleLocked() { return prefs.getBool(NVS_KEY_LOCALE_LOCKED, false); }
    void setLocaleLocked(bool v) { prefs.putBool(NVS_KEY_LOCALE_LOCKED, v); }

    // --- Per-section screen layout variant (vertical-swipe alternates) ---
    int getScreenVariant(const String& sectionKey) {
        String key = String(NVS_KEY_SCREEN_VARIANT_PREFIX) + sectionKey;
        return prefs.getInt(key.c_str(), 0);
    }
    void setScreenVariant(const String& sectionKey, int variantIndex) {
        String key = String(NVS_KEY_SCREEN_VARIANT_PREFIX) + sectionKey;
        prefs.putInt(key.c_str(), variantIndex);
    }

    // --- NFT Gallery settings ---
    // Wallet address used to query OpenSea (can differ from reward wallet_address).
    String getNftWallet()               { return prefs.isKey(NVS_KEY_NFT_WALLET) ? prefs.getString(NVS_KEY_NFT_WALLET, "") : ""; }
    bool   hasNftWallet()               { return prefs.isKey(NVS_KEY_NFT_WALLET); }
    void   setNftWallet(const String& w){ prefs.putString(NVS_KEY_NFT_WALLET, w); }

    // Grid size: 1 (1×1), 4 (2×2), or 9 (3×3).
    uint8_t getNftGridSize()            { return prefs.getUChar(NVS_KEY_NFT_GRID, 9); }
    void    setNftGridSize(uint8_t sz)  { prefs.putUChar(NVS_KEY_NFT_GRID, sz); }

    // Carousel: cycle multiple NFTs per cell automatically.
    bool   getNftCarousel()             { return prefs.getBool(NVS_KEY_NFT_CAROUSEL, true); }
    void   setNftCarousel(bool on)      { prefs.putBool(NVS_KEY_NFT_CAROUSEL, on); }

    // Ticker screen layout: 1 or 2 columns of cards.
    uint8_t getTickerCols()             { uint8_t c = prefs.getUChar("tick_cols", 1); return c == 2 ? 2 : 1; }
    void    setTickerCols(uint8_t c)    { prefs.putUChar("tick_cols", c == 2 ? 2 : 1); }

    // "Data" caption toggle: show collection name + floor under each artwork.
    bool   getNftShowData()             { return prefs.getBool("nft_showdata", true); }
    void   setNftShowData(bool on)      { prefs.putBool("nft_showdata", on); }

    // Manual collection order (comma-joined slugs; empty = pure floor order).
    // Now stored SPARSELY: only the collections the user explicitly reordered,
    // as a front-overlay on the (USD) floor sort. Empty means pure floor order.
    String getNftCollOrder()            { return prefs.isKey("nft_order") ? prefs.getString("nft_order", "") : ""; }
    void   setNftCollOrder(const String& v) { prefs.putString("nft_order", v); }

    // One-time heal: older builds (and the web board's checkbox toggle) used to
    // persist the FULL collection list as the "manual order", freezing a stale
    // raw-ETH-floor ranking that then overrode the device's USD floor sort (BTC
    // Ordinals stranded last, visible only in 3x3 where all cells fit). Clear it
    // once so pure USD floor order returns; genuine sparse reorders survive.
    bool   getNftOrderHealed()          { return prefs.getBool("nft_ord_heal", false); }
    void   setNftOrderHealed(bool v)    { prefs.putBool("nft_ord_heal", v); }

    // Deleted/hidden collections (comma-joined slugs)
    String getNftHidden()               { return prefs.isKey("nft_hidden") ? prefs.getString("nft_hidden", "") : ""; }
    void   setNftHidden(const String& v){ prefs.putString("nft_hidden", v); }

    // Slideshow delay in seconds per cell (0 = manual/off, default 10 s).
    uint8_t getNftSlideshowSecs()       { return prefs.getUChar(NVS_KEY_NFT_SLIDE, 10); }
    void    setNftSlideshowSecs(uint8_t s){ prefs.putUChar(NVS_KEY_NFT_SLIDE, s); }

    // --- NFT manual pinlist (takes priority over wallet-based fetch) ---
    // Format: "chain:contract:tokenId,chain:contract:tokenId,..."  (max 20 items).
    // e.g. "ethereum:0xbd3531da5cf5857e7cfaa92426877b022e612cf8:3968,base:0x...:1"
    // Empty string means pinlist is not active; device falls back to NFT wallet.
    String getNftPinlist()                   { return prefs.isKey("nft_pinlist") ? prefs.getString("nft_pinlist", "") : ""; }
    void   setNftPinlist(const String& p)    { prefs.putString("nft_pinlist", p); }
    bool   hasNftPinlist()                   { String p = getNftPinlist(); return p.length() > 0; }

    // --- Screen swipe order ---
    // Stored as a comma-separated string of ScreenId enum values, e.g. "0,1,2,3,4,5,6".
    // Position 0 is always 0 (CLOCK/Home). The web setup page can reorder positions 1-6.
    String getScreenOrder()                 { return prefs.isKey("screen_order") ? prefs.getString("screen_order", "") : ""; }
    void   setScreenOrder(const String& o)  { prefs.putString("screen_order", o); }

    // Screens hidden from the swipe rotation (comma-joined ScreenId ints).
    String getScreenHidden()                { return prefs.isKey("scr_hidden") ? prefs.getString("scr_hidden", "") : ""; }
    void   setScreenHidden(const String& v) { prefs.putString("scr_hidden", v); }

    // Device-detected NFT collections (JSON array string) reported to the
    // backend so the web setup page can render the collections board.
    String getNftCollsReport()              { return prefs.isKey("nft_colls") ? prefs.getString("nft_colls", "") : ""; }
    void   setNftCollsReport(const String& v){ prefs.putString("nft_colls", v); }
    bool   getNftCollsDirty()               { return prefs.getBool("nftco_dirty", false); }
    void   setNftCollsDirty(bool d)         { prefs.putBool("nftco_dirty", d); }

    // Gallery lists (order/hidden/show_data) edited ON THE DEVICE → push up.
    bool   getNftListsDirty()               { return prefs.getBool("nftls_dirty", false); }
    void   setNftListsDirty(bool d)         { prefs.putBool("nftls_dirty", d); }

    // --- Ticker market-cap alerts ---
    // CSV of "poolAddress:dir:usdValue" — dir is 'g' (fires when mcap rises to or
    // above usdValue) or 'l' (falls to or below). usdValue is a plain USD number
    // (may carry decimals from the k/M/B editor, e.g. 40500000 = $40.50M).
    // One-shot: an alert is removed the moment it fires. Synced both ways with
    // the web via heartbeat CFG + the dirty flag (same pattern as the NFT lists).
    String getTickerAlerts()                 { return prefs.isKey("tkr_alerts") ? prefs.getString("tkr_alerts", "") : ""; }
    void   setTickerAlerts(const String& a)  { prefs.putString("tkr_alerts", a); }
    bool   getTickerAlertsDirty()            { return prefs.getBool("tkral_dirty", false); }
    void   setTickerAlertsDirty(bool d)      { prefs.putBool("tkral_dirty", d); }

    // --- NFT collection floor alerts ---
    // CSV "slug:dir:value" — value in the collection's floor currency (ETH, or
    // BTC for Ordinals), dir 'g'/'l'. Same one-shot + sync semantics as above.
    String getNftAlerts()                    { return prefs.isKey("nft_alerts") ? prefs.getString("nft_alerts", "") : ""; }
    void   setNftAlerts(const String& a)     { prefs.putString("nft_alerts", a); }
    bool   getNftAlertsDirty()               { return prefs.getBool("nftal_dirty", false); }
    void   setNftAlertsDirty(bool d)         { prefs.putBool("nftal_dirty", d); }

private:
    Preferences prefs;
    uint32_t _totalUptBase   = 0;   // cumulative uptime at last heartbeat (s), RAM only
    uint32_t _totalUptBaseMs = 0;   // millis() when _totalUptBase was set (0 = never)
};

extern Storage storage;
