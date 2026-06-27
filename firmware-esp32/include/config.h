// include/config.h — central configuration for the TurboUSD Node firmware.
// Keeping all of this in one file makes it easy to see (and change) every
// externally-relevant setting without hunting through the rest of the code.

#pragma once

// ---------------------------------------------------------------------
// Backend endpoints (Supabase Edge Functions)
// ---------------------------------------------------------------------
#define SUPABASE_FUNCTIONS_BASE_URL "https://YOUR-PROJECT-REF.functions.supabase.co"
#define SUPABASE_REST_BASE_URL      "https://YOUR-PROJECT-REF.supabase.co/rest/v1"
#define SUPABASE_ANON_KEY           "YOUR-SUPABASE-ANON-KEY"

#define ENDPOINT_REGISTER_NODE      SUPABASE_FUNCTIONS_BASE_URL "/register-node"
#define ENDPOINT_HEARTBEAT          SUPABASE_FUNCTIONS_BASE_URL "/heartbeat"
#define ENDPOINT_LATEST_FIRMWARE    SUPABASE_FUNCTIONS_BASE_URL "/latest-firmware"
#define ENDPOINT_MINING_FEED        SUPABASE_REST_BASE_URL "/public_mining_feed?select=*&limit=4"
#define ENDPOINT_NODE_DIRECTORY     SUPABASE_REST_BASE_URL "/public_node_directory"
#define ENDPOINT_DEBT_HISTORY       SUPABASE_FUNCTIONS_BASE_URL "/debt-history"
#define ENDPOINT_OHLCV_HISTORY      SUPABASE_FUNCTIONS_BASE_URL "/ohlcv-history"
#define ENDPOINT_TICKER_CONFIG      SUPABASE_REST_BASE_URL "/node_ticker_config?select=pool_address,chain_id,base_symbol,base_name,quote_symbol,display_order&node_code=eq."
#define ENDPOINT_SEARCH_TOKENS      SUPABASE_FUNCTIONS_BASE_URL "/search-tokens"
#define ENDPOINT_ADD_TICKER         SUPABASE_FUNCTIONS_BASE_URL "/add-node-ticker"
#define ENDPOINT_REMOVE_TICKER      SUPABASE_FUNCTIONS_BASE_URL "/remove-node-ticker"

// External price sources (public, CORS-free, no key required)
#define ENDPOINT_DEXSCREENER_PAIRS  "https://api.dexscreener.com/latest/dex/pairs/"
#define ENDPOINT_DEXSCREENER_TOKENS "https://api.dexscreener.com/latest/dex/tokens/"  // lookup by contract addr
#define ENDPOINT_GECKOTERMINAL_OHLCV "https://api.geckoterminal.com/api/v2/networks/"

// NFT Gallery (screen_nft.h)
// OpenSea API v2 — free tier, no API key required for basic account NFT listing.
// Chain options: ethereum, base, polygon, arbitrum, optimism, bsc
// GET {BASE}/chain/{chain}/account/{address}/nfts?limit=50
// GET {BASE}/collections/{slug}/stats  (floor price)
#define ENDPOINT_OPENSEA_BASE       "https://api.opensea.io/api/v2"
#define OPENSEA_API_KEY             ""   // fill in after registering at opensea.io/get-api-key
// Alchemy NFT API fallback (requires API key)
#define ENDPOINT_ALCHEMY_NFTS_BASE  "https://eth-mainnet.g.alchemy.com/nft/v3"
#define ALCHEMY_API_KEY             ""   // fill in from alchemy.com dashboard

// External data sources (real, public, no key required)
#define ENDPOINT_TREASURY_DATA      "https://treasury.turbousd.com/api/treasury-data"
#define ENDPOINT_US_DEBT            "https://api.fiscaldata.treasury.gov/services/api/fiscal_service/v2/accounting/od/debt_to_penny?sort=-record_date&page[size]=1"

// Geo-IP locale autodetect. Free, no API key, HTTP (no TLS needed). Returns the
// device's country + current UTC offset (incl. DST) based on its public IP, used
// to auto-pick timezone, temp unit, date/time format and week-start on first
// connect. `offset` is seconds east of UTC for the device's IP *right now*.
#define ENDPOINT_GEO_IP            "http://ip-api.com/json/?fields=status,countryCode,offset"

// ---------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------
#define HEARTBEAT_INTERVAL_MS        (3UL * 60UL * 1000UL)      // every 3 minutes
#define TREASURY_DATA_REFRESH_MS     (5UL * 60UL * 1000UL)      // every 5 minutes
#define OHLCV_CHART_REFRESH_MS       (60UL * 60UL * 1000UL)     // once an hour -- the underlying cache is only synced once a day anyway, no need to poll more often than that, but an hourly check keeps a freshly-booted device from waiting a full day to see the chart
#define US_DEBT_REFRESH_MS           (60UL * 60UL * 1000UL)     // every hour (slow-moving figure)
#define MINING_FEED_REFRESH_MS       (15UL * 1000UL)            // every 15s while on the Node screen
#define SENSOR_POLL_INTERVAL_MS      (10UL * 1000UL)            // poll RP2040 for temp/humidity every 10s
#define GEO_LOCALE_SYNC_INTERVAL_MS  (12UL * 60UL * 60UL * 1000UL) // re-check geo-IP twice a day (catches travel + DST)
#define OTA_CHECK_INTERVAL_MS        (24UL * 60UL * 60UL * 1000UL) // once a day
#define NTP_RESYNC_INTERVAL_MS       (6UL * 60UL * 60UL * 1000UL)  // every 6 hours

// ---------------------------------------------------------------------
// Display / UI
// ---------------------------------------------------------------------
#define SCREEN_WIDTH   480
#define SCREEN_HEIGHT  480
// NUM_SCREENS is now defined further down after NFT screen constants

// ---------------------------------------------------------------------
// Inter-chip link (ESP32-S3 <-> RP2040, for the buzzer/alarm)
// See firmware-rp2040/PROTOCOL.md for the wire format.
// ---------------------------------------------------------------------
#define RP2040_UART_BAUD   115200
#define RP2040_UART_TX_PIN 17   // verify against actual board silkscreen/schematic before flashing
#define RP2040_UART_RX_PIN 18

// ---------------------------------------------------------------------
// NVS (flash) storage keys — what survives reboots/power loss
// ---------------------------------------------------------------------
#define NVS_NAMESPACE        "turbousd"
#define NVS_KEY_WIFI_SSID     "wifi_ssid"
#define NVS_KEY_WIFI_PASS     "wifi_pass"
#define NVS_KEY_MAC_ADDRESS   "mac_addr"
#define NVS_KEY_NODE_CODE     "node_code"
#define NVS_KEY_ALARM_HOUR    "alarm_h"
#define NVS_KEY_ALARM_MIN     "alarm_m"
#define NVS_KEY_ALARM_ON      "alarm_on"
#define NVS_KEY_ALARM_DAYS    "alarm_days"  // uint8_t bitmask: bit0=Mon … bit6=Sun (ISO); 0x7F = all days
#define NVS_KEY_TEMP_UNIT     "temp_unit"      // 'C' or 'F'
#define NVS_KEY_DATE_FMT      "date_fmt"       // "DD/MM" or "MM/DD"
#define NVS_KEY_TIME_FMT      "time_fmt"       // "24H" or "AMPM"
#define NVS_KEY_WEEK_START    "week_start"     // uint8_t: 0 = Sunday, 1 = Monday
#define NVS_KEY_TZ_OFFSET     "tz_offset"      // int32_t: seconds east of UTC (incl. current DST)
#define NVS_KEY_LOCALE_LOCKED "loc_locked"     // bool: user changed a locale setting → stop geo auto-config
#define NVS_KEY_SCREEN_VARIANT_PREFIX "scr_var_" // + section key, for per-section layout variants

// NFT gallery settings
#define NVS_KEY_NFT_WALLET   "nft_wallet"    // EVM address string (separate from reward wallet)
#define NVS_KEY_NFT_GRID     "nft_grid"      // uint8_t: 1, 4, or 9 cells
#define NVS_KEY_NFT_CAROUSEL "nft_carousel"  // bool: cycle multiple NFTs per cell
#define NVS_KEY_NFT_SLIDE    "nft_slide"     // uint8_t: slideshow seconds per cell (0 = off)

#define NUM_SCREENS    7   // clock, turbo stats, debt, inflation game, node & network, tickers, nft
