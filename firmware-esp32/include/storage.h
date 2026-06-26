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

    // --- Alarm ---
    uint8_t getAlarmHour() { return prefs.getUChar(NVS_KEY_ALARM_HOUR, 7); }
    uint8_t getAlarmMinute() { return prefs.getUChar(NVS_KEY_ALARM_MIN, 30); }
    bool getAlarmEnabled() { return prefs.getBool(NVS_KEY_ALARM_ON, true); }
    void setAlarm(uint8_t hour, uint8_t minute, bool enabled) {
        prefs.putUChar(NVS_KEY_ALARM_HOUR, hour);
        prefs.putUChar(NVS_KEY_ALARM_MIN, minute);
        prefs.putBool(NVS_KEY_ALARM_ON, enabled);
    }

    // Alarm volume: 1 (whisper) – 5 (max). Default 2 = soft, not startling.
    uint8_t getAlarmVolume()           { return prefs.getUChar("alarm_vol", 2); }
    void    setAlarmVolume(uint8_t v)  { prefs.putUChar("alarm_vol", constrain(v, 1, 5)); }

    // Bitmask of active alarm days: bit0=Mon, bit1=Tue, …, bit6=Sun (ISO order).
    // Default 0x7F = all seven days active.
    uint8_t getAlarmDays() { return prefs.getUChar(NVS_KEY_ALARM_DAYS, 0x7F); }
    void setAlarmDays(uint8_t mask) { prefs.putUChar(NVS_KEY_ALARM_DAYS, mask); }

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
    String getNftWallet()               { return prefs.getString(NVS_KEY_NFT_WALLET, ""); }
    bool   hasNftWallet()               { return prefs.isKey(NVS_KEY_NFT_WALLET); }
    void   setNftWallet(const String& w){ prefs.putString(NVS_KEY_NFT_WALLET, w); }

    // Grid size: 1 (1×1), 4 (2×2), or 9 (3×3).
    uint8_t getNftGridSize()            { return prefs.getUChar(NVS_KEY_NFT_GRID, 9); }
    void    setNftGridSize(uint8_t sz)  { prefs.putUChar(NVS_KEY_NFT_GRID, sz); }

    // Carousel: cycle multiple NFTs per cell automatically.
    bool   getNftCarousel()             { return prefs.getBool(NVS_KEY_NFT_CAROUSEL, true); }
    void   setNftCarousel(bool on)      { prefs.putBool(NVS_KEY_NFT_CAROUSEL, on); }

    // Slideshow delay in seconds per cell (0 = manual/off, default 10 s).
    uint8_t getNftSlideshowSecs()       { return prefs.getUChar(NVS_KEY_NFT_SLIDE, 10); }
    void    setNftSlideshowSecs(uint8_t s){ prefs.putUChar(NVS_KEY_NFT_SLIDE, s); }

    // --- NFT manual pinlist (takes priority over wallet-based fetch) ---
    // Format: "chain:contract:tokenId,chain:contract:tokenId,..."  (max 20 items).
    // e.g. "ethereum:0xbd3531da5cf5857e7cfaa92426877b022e612cf8:3968,base:0x...:1"
    // Empty string means pinlist is not active; device falls back to NFT wallet.
    String getNftPinlist()                   { return prefs.getString("nft_pinlist", ""); }
    void   setNftPinlist(const String& p)    { prefs.putString("nft_pinlist", p); }
    bool   hasNftPinlist()                   { String p = getNftPinlist(); return p.length() > 0; }

    // --- Screen swipe order ---
    // Stored as a comma-separated string of ScreenId enum values, e.g. "0,1,2,3,4,5,6".
    // Position 0 is always 0 (CLOCK/Home). The web setup page can reorder positions 1-6.
    String getScreenOrder()                 { return prefs.getString("screen_order", ""); }
    void   setScreenOrder(const String& o)  { prefs.putString("screen_order", o); }

private:
    Preferences prefs;
};

extern Storage storage;
