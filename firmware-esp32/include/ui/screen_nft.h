// include/ui/screen_nft.h — NFT Gallery screen (screen 6 / index 6).
//
// Displays a grid of the user's NFTs sourced from OpenSea API v2.
// Grid size: 1×1, 2×2, or 3×3 — configurable at runtime and in NVS.
// Per-cell carousel: cycles through additional NFTs in the same collection.
// Auto-slideshow: optional timer advances cells every N seconds.
// Wallet address and settings are persisted in NVS and synced from the
//   web setup page (turbousd.com/setup/{nodeCode} — NFT Gallery section).
//
// Layout (480×480):
//   Header bar     38 px  (shared_components.h)
//   Grid+controls 404 px  — top 36px = grid-size buttons + carousel switch
//   Footer bar     38 px
//
// Data flow:
//   onShow() → _dispatchFetch() → FreeRTOS task on core 0:
//     1. OpenSea GET /chain/ethereum/account/{addr}/nfts?limit=50
//     2. Group by collection slug, collect unique slugs
//     3. For each collection: GET /collections/{slug}/stats (floor_price)
//        Rate-limit: 250 ms delay between collection-stats calls
//     4. Filter out spam (floor_price == 0), sort desc by floor_price
//     5. Write result into _pending, LVGL timer on core 1 applies it
//
// Image loading (optional, compile-time):
//   Requires LV_USE_SJPG=1 in lv_conf.h (LVGL streaming JPEG) or the
//   esp32-TJpgDec library. Without it, cells show colored placeholders
//   with collection name + floor price. All image bytes live in PSRAM.
//
// Caching:
//   NFT data cached in PSRAM for NFT_CACHE_TTL_MS (30 min). Images are
//   cached for the session (cleared when screen_nft is rebuilt).

#pragma once
#include <lvgl.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <vector>
#include <algorithm>
#include "config.h"
#include "storage.h"
#include "ui/shared_components.h"
#include "ui/modal.h"
#include "net_lock.h"
#include "img_decode.h"

// ── Constants ─────────────────────────────────────────────────────────────────
#define NFT_MAX_ITEMS          50   // practical cap; 81 (9×9) would need PSRAM placement for _pendingResult
#define NFT_MAX_COLLECTIONS    30
#define NFT_CACHE_TTL_MS       (30UL * 60UL * 1000UL)  // 30 minutes
#define NFT_RATELIMIT_DELAY_MS 300  // ms between OpenSea collection stats calls
#define NFT_OPENSEA_CHAIN      "ethereum"  // chain for OpenSea v2 NFT lookup
#define NFT_HEADER_H           36   // height of the grid-size selector strip
#define NFT_BODY_H             (480 - 38 - 38)           // 404 px
#define NFT_GRID_H             (NFT_BODY_H - NFT_HEADER_H) // 368 px

// Cell background colours (floor-price tiers: gold > 1 ETH, blue > 0.1 ETH, grey otherwise)
#define NFT_CLR_GOLD    0xe8b339
#define NFT_CLR_BLUE    0x5b8dee
#define NFT_CLR_GREY    0x1e1e22
#define NFT_CLR_BG      0x000000
#define NFT_CLR_CARD    0x0c0c0c
#define NFT_CLR_BORDER  0x1c1c1c
#define NFT_CLR_TEXT    0xe8e8e8
#define NFT_CLR_MUTED   0x6e7280
#define NFT_CLR_GREEN   0x43e397

// ── Data structures ───────────────────────────────────────────────────────────

struct NftItem {
    char name[64]          = {};
    char collection[64]    = {};  // human-readable collection name
    char slug[64]          = {};  // OpenSea collection slug
    char image_url[256]    = {};
    float floor_price_eth  = 0.0f;
    // Decoded image (nullptr until fetched). Pixels are LVGL-native RGB565
    // stored in PSRAM, produced by the nft_img bg task via img_decode.h.
    bool     img_tried     = false;
    uint8_t* img_pixels    = nullptr;
    uint32_t img_w         = 0;
    uint32_t img_h         = 0;
};

struct NftPendingResult {
    bool ready     = false;
    bool error     = false;
    char error_msg[128] = {};
    int  count     = 0;
    NftItem items[NFT_MAX_ITEMS];
};

// ── Ethereum "Ξ" glyph font ──────────────────────────────────────────────────
// LVGL's built-in Montserrat fonts only cover basic latin — no Greek Xi. This
// hand-made 1-glyph font provides Ξ (U+039E) and falls back to montserrat_10
// for everything else, so one label can render "0.005 Ξ". Built field-by-field
// at runtime (not aggregate init) so struct layout changes can't bite us.
static const uint8_t s_xiBitmap[] = {
    // 7×7 1bpp, rows packed MSB-first: ▬▬▬ / gap / ▬▬ inset / gap / ▬▬▬
    0xFE, 0x00, 0x03, 0xE0, 0x00, 0x3F, 0x80
};
static const lv_font_fmt_txt_glyph_dsc_t s_xiGlyphs[] = {
    {0, 0,   0, 0, 0, 0},   // glyph id 0 is reserved in LVGL fonts
    {0, 144, 7, 7, 1, 0},   // Ξ: adv 9px (144/16), 7×7 box on the baseline
};
static const lv_font_t* ethXiFont10() {
    static bool ready = false;
    static lv_font_fmt_txt_cmap_t        cmap;
    static lv_font_fmt_txt_dsc_t         dsc;
    static lv_font_fmt_txt_glyph_cache_t cache;
    static lv_font_t                     font;
    if (!ready) {
        memset(&cmap, 0, sizeof(cmap));
        cmap.range_start    = 0x039E;   // Ξ
        cmap.range_length   = 1;
        cmap.glyph_id_start = 1;
        cmap.type           = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY;

        memset(&dsc, 0, sizeof(dsc));
        dsc.glyph_bitmap = s_xiBitmap;
        dsc.glyph_dsc    = s_xiGlyphs;
        dsc.cmaps        = &cmap;
        dsc.cmap_num     = 1;
        dsc.bpp          = 1;
        dsc.cache        = &cache;

        memset(&font, 0, sizeof(font));
        font.get_glyph_dsc    = lv_font_get_glyph_dsc_fmt_txt;
        font.get_glyph_bitmap = lv_font_get_bitmap_fmt_txt;
        font.line_height      = lv_font_montserrat_10.line_height;
        font.base_line        = lv_font_montserrat_10.base_line;
        font.subpx            = LV_FONT_SUBPX_NONE;
        font.dsc              = &dsc;
        font.fallback         = &lv_font_montserrat_10;   // digits, dot, space
        ready = true;
    }
    return &font;
}

// ── NftScreen class ───────────────────────────────────────────────────────────

class NftScreen {
public:
    static NftScreen* s_instance;

    // Call once at startup. Builds the skeleton; data loaded on first onShow().
    void build(lv_obj_t* parentScreen, lv_event_cb_t onLogoTapped,
               lv_event_cb_t onDateTapped, lv_event_cb_t onQrTapped, void* userData) {
        _userData = userData;
        header = buildSharedHeader(parentScreen, onLogoTapped, onDateTapped, userData);
        footer = buildSharedFooter(parentScreen, onQrTapped, userData);

        // Root body
        _body = lv_obj_create(parentScreen);
        lv_obj_set_size(_body, 480, NFT_BODY_H);
        lv_obj_align(_body, LV_ALIGN_TOP_MID, 0, 38);
        lv_obj_set_style_bg_color(_body, lv_color_hex(NFT_CLR_BG), 0);
        lv_obj_set_style_border_width(_body, 0, 0);
        lv_obj_set_style_pad_all(_body, 0, 0);
        lv_obj_clear_flag(_body, LV_OBJ_FLAG_SCROLLABLE);

        // ── Grid-size selector strip ──────────────────────────────────────────
        _sizeBar = lv_obj_create(_body);
        lv_obj_set_size(_sizeBar, 480, NFT_HEADER_H);
        lv_obj_align(_sizeBar, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(_sizeBar, lv_color_hex(0x0a0a0a), 0);
        lv_obj_set_style_border_width(_sizeBar, 0, 0);
        lv_obj_set_style_pad_all(_sizeBar, 4, 0);
        lv_obj_set_flex_flow(_sizeBar, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(_sizeBar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(_sizeBar, 6, 0);
        lv_obj_clear_flag(_sizeBar, LV_OBJ_FLAG_SCROLLABLE);

        _btn1x1 = _makeSizeBtn(_sizeBar, "1x1");
        _btn2x2 = _makeSizeBtn(_sizeBar, "2x2");
        _btn3x3 = _makeSizeBtn(_sizeBar, "3x3");

        // Carousel toggle — a subtle tappable WORD, not a big switch (the
        // switch dominated the strip). Lit green when on, muted grey when off.
        _carouselSwitch = lv_label_create(_sizeBar);
        lv_label_set_text(_carouselSwitch, "Carousel");
        lv_obj_set_style_text_font(_carouselSwitch, &lv_font_montserrat_12, 0);
        lv_obj_set_style_pad_left(_carouselSwitch, 8, 0);
        lv_obj_add_flag(_carouselSwitch, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(_carouselSwitch, 10);
        _refreshCarouselLabel();
        lv_obj_add_event_cb(_carouselSwitch, [](lv_event_t* e) {
            NftScreen* self = (NftScreen*)lv_event_get_user_data(e);
            bool on = !storage.getNftCarousel();
            storage.setNftCarousel(on);
            self->_refreshCarouselLabel();
            self->_applyCarouselSetting(on);
        }, LV_EVENT_CLICKED, this);

        // ── "+ Wallet" button (floats at the right of the strip) ──────────────
        // Dedicated target for entering/changing the NFT wallet, so the grid
        // never has to be tappable (which used to fire on swipe-in). IGNORE_LAYOUT
        // keeps it out of the flex row so it can sit hard against the right edge.
        // Plain tappable green word — no button chrome (same treatment as the
        // Carousel toggle; the solid green pill dominated the strip).
        _addWalletBtn = lv_btn_create(_sizeBar);
        lv_obj_add_flag(_addWalletBtn, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_set_size(_addWalletBtn, LV_SIZE_CONTENT, 24);
        lv_obj_align(_addWalletBtn, LV_ALIGN_RIGHT_MID, -6, 0);
        lv_obj_set_style_bg_opa(_addWalletBtn, LV_OPA_0, 0);
        lv_obj_set_style_border_width(_addWalletBtn, 0, 0);
        lv_obj_set_style_shadow_width(_addWalletBtn, 0, 0);
        lv_obj_set_style_pad_hor(_addWalletBtn, 4, 0);
        lv_obj_set_ext_click_area(_addWalletBtn, 10);
        lv_obj_t* awLbl = lv_label_create(_addWalletBtn);
        lv_label_set_text(awLbl, "Wallet");
        lv_obj_set_style_text_font(awLbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(awLbl, lv_color_hex(NFT_CLR_GREEN), 0);
        lv_obj_center(awLbl);
        lv_obj_add_event_cb(_addWalletBtn, [](lv_event_t* e) {
            NftScreen* self = (NftScreen*)lv_event_get_user_data(e);
            if (self) self->_openWalletDialog();
        }, LV_EVENT_CLICKED, this);

        // ── Grid area ─────────────────────────────────────────────────────────
        _gridArea = lv_obj_create(_body);
        lv_obj_set_size(_gridArea, 480, NFT_GRID_H);
        lv_obj_align(_gridArea, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(_gridArea, lv_color_hex(NFT_CLR_BG), 0);
        lv_obj_set_style_border_width(_gridArea, 0, 0);
        lv_obj_set_style_pad_all(_gridArea, 4, 0);
        lv_obj_set_style_pad_gap(_gridArea, 4, 0);
        lv_obj_clear_flag(_gridArea, LV_OBJ_FLAG_SCROLLABLE);

        // NOTE: the grid itself is intentionally NOT clickable. Making the whole
        // grid a tap target meant a horizontal swipe-in (to change screens) also
        // registered as a tap and popped the wallet keyboard dialog. Adding the
        // NFT wallet now has a dedicated "+ Wallet" button (built below), mirroring
        // the tickers screen's "+ Add" button, so swiping never opens the dialog.

        // Loading label (shown while fetching)
        _loadingLabel = lv_label_create(_gridArea);
        lv_label_set_text(_loadingLabel, "");
        lv_obj_set_style_text_color(_loadingLabel, lv_color_hex(NFT_CLR_MUTED), 0);
        lv_obj_set_style_text_font(_loadingLabel, &lv_font_montserrat_12, 0);
        lv_obj_center(_loadingLabel);

        _spinner = lv_spinner_create(_gridArea, 1000, 60);
        lv_obj_set_size(_spinner, 40, 40);
        lv_obj_set_style_arc_color(_spinner, lv_color_hex(NFT_CLR_GREEN), LV_PART_INDICATOR);
        lv_obj_add_flag(_spinner, LV_OBJ_FLAG_HIDDEN);

        // Apply saved grid size
        _gridSize = storage.getNftGridSize();
        if (_gridSize != 1 && _gridSize != 4 && _gridSize != 9) _gridSize = 9;
        _updateSizeBtnStyles();

        // ── Slideshow timer (fires every second, counts down) ─────────────────
        lv_timer_create([](lv_timer_t* t) {
            NftScreen* self = (NftScreen*)t->user_data;
            if (self) self->_onSlideshowTick();
        }, 1000, this);

        // ── Pending-result poll timer ─────────────────────────────────────────
        lv_timer_create([](lv_timer_t* t) {
            NftScreen* self = (NftScreen*)t->user_data;
            if (self) self->_pollPending();
        }, 100, this);

        s_instance = this;
    }

    // Called from ui_manager when this screen becomes visible.
    void onShow() {
        if (storage.hasNftPinlist()) {
            // Manual pinlist takes priority over wallet-based fetch
            if (_cacheExpired()) _startPinlistFetch();
            else                 _rebuildGrid();
        } else if (!storage.hasNftWallet()) {
            // Don't pop a keyboard dialog automatically on swipe-in (that was the
            // stray keyboard). Show a hint instead; the wallet is set from the
            // node's web setup page, or by tapping the grid to open the dialog.
            _rebuildGrid();
            lv_label_set_text(_loadingLabel,
                "No NFT wallet set.\nTap the + WALLET button at the top\nto enter one, or add it on your\nnode's setup page.");
        } else if (_cacheExpired()) {
            _startFetch();
        } else {
            _rebuildGrid();  // use cached data
        }
    }

    SharedHeaderRefs header;
    SharedFooterRefs footer;

private:
    // ── Members ───────────────────────────────────────────────────────────────
    lv_obj_t*  _body           = nullptr;
    lv_obj_t*  _sizeBar        = nullptr;
    lv_obj_t*  _btn1x1         = nullptr;
    lv_obj_t*  _btn2x2         = nullptr;
    lv_obj_t*  _btn3x3         = nullptr;
    lv_obj_t*  _carouselSwitch = nullptr;
    lv_obj_t*  _gridArea       = nullptr;
    lv_obj_t*  _addWalletBtn   = nullptr;
    lv_obj_t*  _loadingLabel   = nullptr;
    lv_obj_t*  _spinner        = nullptr;
    void*      _userData       = nullptr;

    int        _gridSize       = 9;    // 1, 4, or 9
    bool       _fetching       = false;

    // Cells (max 9 for 3×3 grid)
    struct CellWidgets {
        lv_obj_t* container = nullptr;
        lv_obj_t* img       = nullptr;   // lv_canvas or lv_img for decoded image
        lv_img_dsc_t imgDsc = {};        // PER-CELL: a shared static dsc made every cell show the same image
        lv_obj_t* nameLbl   = nullptr;
        lv_obj_t* floorLbl  = nullptr;
        lv_obj_t* dotRow    = nullptr;   // carousel position dots
        int       nftStart  = 0;        // index of first NFT in this cell's "group"
        int       nftCount  = 0;        // how many NFTs are in this cell's group
        int       nftCurrent= 0;        // which one is displayed right now
    } _cells[9];
    int _cellCount = 0;

    // Cached NFT data (persists until expired or wallet changes)
    std::vector<NftItem> _nftCache;
    uint32_t _cacheTimestamp = 0;

    // Slideshow state
    uint8_t _slideshowSecs  = 10;
    uint8_t _slideshowCount = 0;   // counts seconds down

    // ── FreeRTOS ─────────────────────────────────────────────────────────────
    volatile TaskHandle_t _bgTask  = nullptr;   // written by workers on core 0, read on core 1
    volatile TaskHandle_t _imgTask = nullptr;   // image download/decode worker
    volatile bool         _imgDirty = false;    // set by img task → poll refreshes cells
    static NftPendingResult _pendingResult;

    // ── Helpers ───────────────────────────────────────────────────────────────

    bool _cacheExpired() {
        if (_nftCache.empty()) return true;
        return (millis() - _cacheTimestamp) > NFT_CACHE_TTL_MS;
    }

    // ── Pinlist fetch (manual picks mode) ────────────────────────────────────
    // Fetches each NFT individually from OpenSea using the stored pinlist.
    // Format of pinlist: "ethereum:0xcontract:tokenId,base:0xcontract:tokenId,…"

    void _startPinlistFetch() {
        if (_fetching) return;
        _fetching = true;
        _pendingResult.ready = false;
        _pendingResult.error = false;
        _pendingResult.count = 0;

        lv_label_set_text(_loadingLabel, "Loading pinlisted NFTs...");
        lv_obj_clear_flag(_spinner, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < _cellCount; i++) {
            if (_cells[i].container) lv_obj_add_flag(_cells[i].container, LV_OBJ_FLAG_HIDDEN);
        }

        if (_bgTask) { _fetching = false; return; }   // never vTaskDelete — see _startFetch
        xTaskCreatePinnedToCore(_bgPinlistFetchFn, "nft_pin", 16384, nullptr, 1, (TaskHandle_t*)&_bgTask, 0);
    }

    static void _bgPinlistFetchFn(void* /*pvArg*/) {
        NftScreen* self = s_instance;
        if (!self) { vTaskDelete(nullptr); return; }
        netLock();   // exclusive TLS ownership — see net_lock.h

        String pinlist = storage.getNftPinlist();
        if (pinlist.length() == 0) {
            snprintf(_pendingResult.error_msg, sizeof(_pendingResult.error_msg), "Pinlist is empty.");
            _pendingResult.error = true;
            _pendingResult.ready = true;
            netUnlock();
        if (s_instance) s_instance->_bgTask = nullptr;   // ALWAYS clear before self-delete
            vTaskDelete(nullptr);
            return;
        }

        // ── Parse "chain:contract:tokenId,…" ─────────────────────────────────
        struct PinEntry {
            char chain[16]    = {};
            char contract[43] = {};
            char tokenId[24]  = {};
        };

        static PinEntry entries[NFT_MAX_ITEMS];
        int entryCount = 0;

        String item;
        pinlist += ',';  // sentinel to flush last item
        for (int ci = 0; ci < (int)pinlist.length() && entryCount < NFT_MAX_ITEMS; ci++) {
            char ch = pinlist[ci];
            if (ch == ',') {
                // Split on first two colons: chain : contract : tokenId
                int first  = item.indexOf(':');
                int second = item.indexOf(':', first + 1);
                if (first > 0 && second > first) {
                    PinEntry& e = entries[entryCount++];
                    item.substring(0, first).toCharArray(e.chain,    sizeof(e.chain));
                    item.substring(first + 1, second).toCharArray(e.contract, sizeof(e.contract));
                    item.substring(second + 1).toCharArray(e.tokenId, sizeof(e.tokenId));
                }
                item = "";
            } else {
                item += ch;
            }
        }

        if (entryCount == 0) {
            snprintf(_pendingResult.error_msg, sizeof(_pendingResult.error_msg), "No valid pinlist entries.");
            _pendingResult.error = true;
            _pendingResult.ready = true;
            netUnlock();
        if (s_instance) s_instance->_bgTask = nullptr;   // ALWAYS clear before self-delete
            vTaskDelete(nullptr);
            return;
        }

        // ── Fetch each NFT individually from OpenSea ──────────────────────────
        _pendingResult.count = 0;

        for (int ei = 0; ei < entryCount && _pendingResult.count < NFT_MAX_ITEMS; ei++) {
            PinEntry& e = entries[ei];

            // GET /api/v2/chain/{chain}/contract/{contract}/nfts/{tokenId}
            String url = String(ENDPOINT_OPENSEA_BASE)
                       + "/chain/" + e.chain
                       + "/contract/" + e.contract
                       + "/nfts/" + e.tokenId;

            HTTPClient http;
            http.useHTTP10(true);   // body parsed from getStream() — avoid chunked encoding
            http.begin(url);
            if (strlen(OPENSEA_API_KEY) > 0) http.addHeader("X-API-KEY", OPENSEA_API_KEY);
            http.addHeader("Accept", "application/json");
            http.setTimeout(20000);

            int code = http.GET();
            if (code != 200) { http.end(); delay(NFT_RATELIMIT_DELAY_MS); continue; }

            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, http.getStream());
            http.end();
            if (err) { delay(NFT_RATELIMIT_DELAY_MS); continue; }

            JsonObject nft = doc["nft"].as<JsonObject>();
            const char* name      = nft["name"]              | e.tokenId;
            const char* image_url = nft["display_image_url"] | nft["image_url"] | "";
            const char* slug      = nft["collection"]        | "";

            // Fetch floor price from collection stats
            float fp = 0.0f;
            char  colName[64] = {};
            if (slug[0]) {
                String statsUrl = String(ENDPOINT_OPENSEA_BASE) + "/collections/" + slug + "/stats";
                HTTPClient hStats;
                hStats.useHTTP10(true);   // body parsed from getStream() — avoid chunked encoding
                hStats.begin(statsUrl);
                if (strlen(OPENSEA_API_KEY) > 0) hStats.addHeader("X-API-KEY", OPENSEA_API_KEY);
                hStats.addHeader("Accept", "application/json");
                hStats.setTimeout(6000);
                if (hStats.GET() == 200) {
                    JsonDocument sDoc;
                    if (deserializeJson(sDoc, hStats.getStream()) == DeserializationError::Ok) {
                        fp = sDoc["total"]["floor_price"] | 0.0f;
                        const char* cn = sDoc["name"] | slug;
                        strncpy(colName, cn, sizeof(colName) - 1);
                    }
                }
                hStats.end();
                delay(NFT_RATELIMIT_DELAY_MS);
            }

            NftItem& item2 = _pendingResult.items[_pendingResult.count++];
            strncpy(item2.name,       name,                        sizeof(item2.name)       - 1);
            strncpy(item2.slug,       slug,                        sizeof(item2.slug)       - 1);
            strncpy(item2.collection, colName[0] ? colName : slug, sizeof(item2.collection) - 1);
            strncpy(item2.image_url,  image_url,                   sizeof(item2.image_url)  - 1);
            item2.floor_price_eth = fp;

            delay(NFT_RATELIMIT_DELAY_MS);
        }

        _sortByFloor();   // most valuable collection first, same as wallet mode
        _pendingResult.ready = true;
        self->_bgTask = nullptr;
        netUnlock();
        if (s_instance) s_instance->_bgTask = nullptr;   // ALWAYS clear before self-delete
        vTaskDelete(nullptr);
    }

    // Floor price desc; ties broken by slug so collections stay contiguous.
    static void _sortByFloor() {
        for (int i = 0; i < _pendingResult.count - 1; i++) {
            for (int j = 0; j < _pendingResult.count - 1 - i; j++) {
                NftItem& a = _pendingResult.items[j];
                NftItem& b = _pendingResult.items[j+1];
                bool swap = a.floor_price_eth < b.floor_price_eth ||
                            (a.floor_price_eth == b.floor_price_eth && strcmp(a.slug, b.slug) > 0);
                if (swap) { NftItem tmp = a; a = b; b = tmp; }
            }
        }
    }

    void _startFetch() {
        if (_fetching) return;
        _fetching = true;
        _pendingResult.ready = false;
        _pendingResult.error = false;
        _pendingResult.count = 0;

        // Show spinner
        lv_label_set_text(_loadingLabel, "Fetching your NFTs...");
        lv_obj_clear_flag(_spinner, LV_OBJ_FLAG_HIDDEN);
        // Hide existing cells
        for (int i = 0; i < _cellCount; i++) {
            if (_cells[i].container) lv_obj_add_flag(_cells[i].container, LV_OBJ_FLAG_HIDDEN);
        }

        // NEVER vTaskDelete here: error exits inside _bgFetchFn used to leave
        // a STALE handle in _bgTask, and deleting a dead/recycled task handle
        // panics (StoreProhibited — this was the "crash swiping past the NFT
        // screen"). The worker clears _bgTask itself on every exit path; if
        // it's still set, a fetch is genuinely in flight → just skip.
        if (_bgTask) { _fetching = false; return; }
        xTaskCreatePinnedToCore(_bgFetchFn, "nft_fetch", 16384, nullptr, 1, (TaskHandle_t*)&_bgTask, 0);
    }

    void _pollPending() {
        // Fresh decoded images → repaint the cells showing them.
        if (_imgDirty) {
            _imgDirty = false;
            for (int i = 0; i < _cellCount; i++) _refreshCell(i);
        }

        if (!_fetching) return;
        if (!_pendingResult.ready) return;
        if (_imgTask) return;   // img worker still iterating the old cache — absorb next tick

        _fetching = false;
        lv_obj_add_flag(_spinner, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(_loadingLabel, "");

        if (_pendingResult.error) {
            lv_label_set_text(_loadingLabel, _pendingResult.error_msg);
            return;
        }

        // Absorb results into cache (freeing any previously decoded pixels)
        for (auto& it : _nftCache) if (it.img_pixels) { free(it.img_pixels); it.img_pixels = nullptr; }
        _nftCache.clear();
        for (int i = 0; i < _pendingResult.count; i++) {
            _nftCache.push_back(_pendingResult.items[i]);
        }
        _cacheTimestamp = millis();
        _rebuildGrid();
    }

    // ── Grid builder ──────────────────────────────────────────────────────────

    void _rebuildGrid() {
        // Delete old cells
        for (int i = 0; i < _cellCount; i++) {
            if (_cells[i].container) {
                lv_obj_del(_cells[i].container);
                _cells[i] = {};
            }
        }
        _cellCount = 0;

        if (_nftCache.empty()) {
            lv_label_set_text(_loadingLabel, "No NFTs found.\nEnter a wallet with NFTs.");
            return;
        }
        lv_label_set_text(_loadingLabel, "");

        // ONE COLLECTION PER CELL (2x2 and 3x3): the cache is sorted by floor
        // price desc with same-collection items contiguous, so cell 0 gets the
        // most valuable collection, cell 1 the next, etc. Each cell's carousel
        // cycles through that collection's NFTs. 1x1 = the whole wallet in one
        // carousel. Fewer collections than cells → fewer cells (no repeats).
        int n = _gridSize;                   // 1, 4, or 9
        int side = (n == 1) ? 1 : (n == 4 ? 2 : 3);
        lv_coord_t cellW = (lv_coord_t)((480 - 4 - (side - 1) * 4) / side);
        lv_coord_t cellH = (lv_coord_t)((NFT_GRID_H - 4 - (side - 1) * 4) / side);

        int total = (int)_nftCache.size();
        int gStart[9], gCount[9], groups = 0;
        if (n == 1) {
            gStart[0] = 0; gCount[0] = total; groups = 1;
        } else {
            for (int i = 0; i < total && groups < n; ) {
                int j = i;
                while (j < total && strcmp(_nftCache[j].slug, _nftCache[i].slug) == 0) j++;
                gStart[groups] = i;
                gCount[groups] = j - i;
                groups++;
                i = j;
            }
        }

        for (int ci = 0; ci < groups; ci++)
            _buildCell(ci, ci, side, cellW, cellH, gStart[ci], gCount[ci]);
        _cellCount = groups;
        _slideshowSecs  = storage.getNftSlideshowSecs();
        _slideshowCount = _slideshowSecs;

        _startImageFetch();   // download/decode artwork for what's now visible
    }

    void _buildCell(int idx, int pos, int side, lv_coord_t w, lv_coord_t h, int nftStart, int nftCount) {
        CellWidgets& cw = _cells[idx];
        cw.nftStart   = nftStart;
        cw.nftCount   = max(1, nftCount);
        cw.nftCurrent = 0;

        int col = pos % side;
        int row = pos / side;
        lv_coord_t x = (lv_coord_t)(4 + col * (w + 4));
        lv_coord_t y = (lv_coord_t)(4 + row * (h + 4));

        cw.container = lv_obj_create(_gridArea);
        lv_obj_set_size(cw.container, w, h);
        lv_obj_set_pos(cw.container, x, y);
        lv_obj_set_style_border_color(cw.container, lv_color_hex(NFT_CLR_BORDER), 0);
        lv_obj_set_style_border_width(cw.container, 1, 0);
        lv_obj_set_style_radius(cw.container, 6, 0);
        lv_obj_set_style_pad_all(cw.container, 4, 0);
        lv_obj_set_style_clip_corner(cw.container, true, 0);
        lv_obj_clear_flag(cw.container, LV_OBJ_FLAG_SCROLLABLE);

        _refreshCell(idx);

        // Tap: advance carousel manually
        lv_obj_add_flag(cw.container, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(cw.container, (void*)(intptr_t)idx);
        lv_obj_add_event_cb(cw.container, [](lv_event_t* e) {
            NftScreen* self = NftScreen::s_instance;
            int ci = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(e));
            if (self && ci >= 0 && ci < 9) self->_advanceCell(ci);
        }, LV_EVENT_CLICKED, nullptr);
    }

    void _refreshCell(int idx) {
        CellWidgets& cw = _cells[idx];
        if (!cw.container) return;

        // Clean and rebuild cell contents
        lv_obj_clean(cw.container);
        cw.nameLbl  = nullptr;
        cw.floorLbl = nullptr;
        cw.dotRow   = nullptr;

        int nftIdx = cw.nftStart + (cw.nftCurrent % cw.nftCount);
        if (nftIdx >= (int)_nftCache.size()) nftIdx = (int)_nftCache.size() - 1;
        if (nftIdx < 0) return;

        const NftItem& item = _nftCache[nftIdx];

        // Background colour by floor price tier
        uint32_t bgColor;
        if (item.floor_price_eth >= 1.0f)      bgColor = 0x1a1400;  // dark gold tint
        else if (item.floor_price_eth >= 0.1f)  bgColor = 0x0a0f1a;  // dark blue tint
        else                                     bgColor = NFT_CLR_GREY;
        lv_obj_set_style_bg_color(cw.container, lv_color_hex(bgColor), 0);
        lv_obj_set_style_bg_opa(cw.container, LV_OPA_COVER, 0);

        lv_coord_t cw_w, cw_h;
        cw_w = lv_obj_get_width(cw.container)  - 8;  // minus padding
        cw_h = lv_obj_get_height(cw.container) - 8;

        // Image placeholder (full-cell canvas colored by tier)
        // If img_pixels is non-null (JPEG decoded), display as lv_img instead.
        if (item.img_pixels && item.img_w > 0) {
            // Decoded image available (RGB565 in PSRAM). One dsc PER CELL —
            // a shared static one made every cell display the same image.
            cw.imgDsc.header.always_zero = 0;
            cw.imgDsc.header.w           = item.img_w;
            cw.imgDsc.header.h           = item.img_h;
            cw.imgDsc.header.cf          = LV_IMG_CF_TRUE_COLOR;
            cw.imgDsc.data_size          = item.img_w * item.img_h * 2;
            cw.imgDsc.data               = item.img_pixels;

            // Square viewport that leaves room for the name/floor strip; zoom
            // (pivot 0,0) scales the fixed 224px decode to whatever this grid
            // size needs, and the box clips the rest.
            lv_coord_t sideBox = min(cw_w, (lv_coord_t)(cw_h - 18));
            lv_obj_t* imgObj = lv_img_create(cw.container);
            lv_img_set_src(imgObj, &cw.imgDsc);
            lv_img_set_pivot(imgObj, 0, 0);
            lv_img_set_zoom(imgObj, (uint16_t)max(1L, 256L * sideBox / (long)item.img_w));
            lv_obj_set_size(imgObj, sideBox, sideBox);
            lv_obj_align(imgObj, LV_ALIGN_TOP_MID, 0, 0);
            lv_img_set_antialias(imgObj, false);
            lv_obj_clear_flag(imgObj, LV_OBJ_FLAG_CLICKABLE);   // taps advance the carousel
        } else {
            // No decoded image — show a coloured tile with a subtle grid icon
            lv_obj_t* placeholder = lv_obj_create(cw.container);
            lv_obj_set_size(placeholder, cw_w, cw_h > 60 ? cw_h - 40 : cw_h / 2);
            lv_obj_align(placeholder, LV_ALIGN_TOP_MID, 0, 0);
            lv_obj_set_style_bg_color(placeholder, lv_color_hex(bgColor == NFT_CLR_GREY ? 0x2a2a2e : bgColor), 0);
            lv_obj_set_style_border_width(placeholder, 0, 0);
            lv_obj_set_style_radius(placeholder, 4, 0);
            lv_obj_clear_flag(placeholder, LV_OBJ_FLAG_SCROLLABLE);

            // Show image URL hint (tiny text, so owner knows it loaded but img decode needed)
            if (item.image_url[0]) {
                lv_obj_t* hint = lv_label_create(placeholder);
                lv_label_set_text(hint, LV_SYMBOL_IMAGE);
                lv_obj_set_style_text_color(hint, lv_color_hex(0x444448), 0);
                lv_obj_center(hint);
            }
        }

        // Collection name
        if (cw_h > 30) {
            cw.nameLbl = lv_label_create(cw.container);
            lv_label_set_text(cw.nameLbl, item.name[0] ? item.name : item.collection);
            lv_label_set_long_mode(cw.nameLbl, LV_LABEL_LONG_DOT);
            lv_obj_set_width(cw.nameLbl, cw_w);
            lv_obj_set_style_text_font(cw.nameLbl, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(cw.nameLbl, lv_color_hex(NFT_CLR_TEXT), 0);
            lv_obj_align(cw.nameLbl, LV_ALIGN_BOTTOM_LEFT, 0, cw_h > 50 ? -16 : 0);
        }

        // Floor price
        if (cw_h > 50 && item.floor_price_eth > 0) {
            cw.floorLbl = lv_label_create(cw.container);
            char floorBuf[24];
            // "0.005 \u039E" — the Xi comes from the embedded 1-glyph font.
            if (item.floor_price_eth < 0.001f)
                snprintf(floorBuf, sizeof(floorBuf), "%.4f \xCE\x9E", item.floor_price_eth);
            else
                snprintf(floorBuf, sizeof(floorBuf), "%.3f \xCE\x9E", item.floor_price_eth);
            lv_label_set_text(cw.floorLbl, floorBuf);
            lv_obj_set_style_text_font(cw.floorLbl, ethXiFont10(), 0);
            uint32_t priceColor = item.floor_price_eth >= 1.0f ? NFT_CLR_GOLD :
                                  item.floor_price_eth >= 0.1f ? NFT_CLR_BLUE  : NFT_CLR_GREEN;
            lv_obj_set_style_text_color(cw.floorLbl, lv_color_hex(priceColor), 0);
            lv_obj_align(cw.floorLbl, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
        }

        // Carousel dots (only if there are multiple NFTs in this cell)
        if (cw.nftCount > 1 && cw_h > 40) {
            cw.dotRow = lv_obj_create(cw.container);
            int dots = min(cw.nftCount, 5);
            lv_coord_t dotRowW = (lv_coord_t)(dots * 10);
            lv_obj_set_size(cw.dotRow, dotRowW, 8);
            lv_obj_align(cw.dotRow, LV_ALIGN_BOTTOM_MID, 0, 0);
            lv_obj_set_style_bg_opa(cw.dotRow, LV_OPA_0, 0);
            lv_obj_set_style_border_width(cw.dotRow, 0, 0);
            lv_obj_set_style_pad_all(cw.dotRow, 0, 0);

            for (int d = 0; d < dots; d++) {
                lv_obj_t* dot = lv_obj_create(cw.dotRow);
                lv_obj_set_size(dot, 6, 6);
                lv_obj_set_pos(dot, (lv_coord_t)(d * 10), 1);
                lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
                lv_obj_set_style_border_width(dot, 0, 0);
                bool active = (d == cw.nftCurrent % dots);
                lv_obj_set_style_bg_color(dot, lv_color_hex(active ? NFT_CLR_GREEN : 0x444448), 0);
            }
        }
    }

    void _advanceCell(int idx) {
        CellWidgets& cw = _cells[idx];
        if (cw.nftCount <= 1) return;
        cw.nftCurrent = (cw.nftCurrent + 1) % cw.nftCount;
        _refreshCell(idx);
        // The first NFT_IMG_MAX_FETCH images load eagerly; anything beyond
        // that gets fetched the moment the carousel reaches it.
        int nftIdx = cw.nftStart + (cw.nftCurrent % cw.nftCount);
        if (nftIdx < (int)_nftCache.size() && !_nftCache[nftIdx].img_pixels
            && !_nftCache[nftIdx].img_tried)
            _startImageFetch();
    }

    // ── Slideshow tick ────────────────────────────────────────────────────────

    void _onSlideshowTick() {
        uint8_t slideSecs = storage.getNftSlideshowSecs();
        if (slideSecs == 0 || !storage.getNftCarousel()) return;
        if (_cellCount == 0) return;

        if (_slideshowCount > 0) { _slideshowCount--; return; }
        _slideshowCount = slideSecs;

        // Advance all cells together (like a global slide advance)
        for (int i = 0; i < _cellCount; i++) {
            if (_cells[i].nftCount > 1) {
                _cells[i].nftCurrent = (_cells[i].nftCurrent + 1) % _cells[i].nftCount;
                _refreshCell(i);
            }
        }
    }

    // ── Carousel setting ──────────────────────────────────────────────────────

    // Green + full opacity when on; muted grey when off.
    void _refreshCarouselLabel() {
        if (!_carouselSwitch) return;
        bool on = storage.getNftCarousel();
        lv_obj_set_style_text_color(_carouselSwitch,
            lv_color_hex(on ? NFT_CLR_GREEN : NFT_CLR_MUTED), 0);
        lv_obj_set_style_text_opa(_carouselSwitch, on ? LV_OPA_COVER : LV_OPA_70, 0);
    }

    void _applyCarouselSetting(bool on) {
        // Rebuilding the grid respects getNftCarousel() and getNftSlideshowSecs()
        if (!_nftCache.empty()) _rebuildGrid();
    }

    // ── Size selector buttons ─────────────────────────────────────────────────

    lv_obj_t* _makeSizeBtn(lv_obj_t* parent, const char* label) {
        lv_obj_t* btn = lv_btn_create(parent);
        lv_obj_set_size(btn, 50, 26);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x141414), 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(NFT_CLR_BORDER), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_pad_all(btn, 2, 0);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, label);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(NFT_CLR_MUTED), 0);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, _onSizeBtnTapped, LV_EVENT_CLICKED, this);
        return btn;
    }

    static void _onSizeBtnTapped(lv_event_t* e) {
        NftScreen* self = (NftScreen*)lv_event_get_user_data(e);
        lv_obj_t* btn = lv_event_get_current_target(e);
        int newSize = 9;
        if (btn == self->_btn1x1) newSize = 1;
        else if (btn == self->_btn2x2) newSize = 4;
        else if (btn == self->_btn3x3) newSize = 9;
        if (newSize == self->_gridSize) return;
        self->_gridSize = newSize;
        storage.setNftGridSize((uint8_t)newSize);
        self->_updateSizeBtnStyles();
        self->_rebuildGrid();
    }

    void _updateSizeBtnStyles() {
        _applyBtnStyle(_btn1x1, _gridSize == 1);
        _applyBtnStyle(_btn2x2, _gridSize == 4);
        _applyBtnStyle(_btn3x3, _gridSize == 9);
    }

    void _applyBtnStyle(lv_obj_t* btn, bool active) {
        if (!btn) return;
        lv_obj_set_style_bg_color(btn, lv_color_hex(active ? 0x1a2a1a : 0x141414), 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(active ? NFT_CLR_GREEN : NFT_CLR_BORDER), 0);
        lv_obj_t* lbl = lv_obj_get_child(btn, 0);
        if (lbl) lv_obj_set_style_text_color(lbl, lv_color_hex(active ? NFT_CLR_GREEN : NFT_CLR_MUTED), 0);
    }

    // ── Wallet entry dialog ───────────────────────────────────────────────────

    void _openWalletDialog() {
        lv_obj_t* card = openModal(lv_scr_act());

        lv_obj_t* title = lv_label_create(card);
        lv_label_set_text(title, "NFT WALLET");
        lv_obj_set_style_text_color(title, lv_color_hex(NFT_CLR_MUTED), 0);

        lv_obj_t* hint = lv_label_create(card);
        lv_label_set_text(hint, "Enter your EVM wallet address (0x...)\nor ENS name (yourname.eth) to view your NFTs.");
        lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(hint, 280);
        lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(hint, lv_color_hex(NFT_CLR_MUTED), 0);

        lv_obj_t* ta = lv_textarea_create(card);
        lv_obj_set_size(ta, 280, 36);
        lv_textarea_set_placeholder_text(ta, "0x...");
        lv_textarea_set_one_line(ta, true);
        lv_obj_set_style_text_font(ta, &lv_font_montserrat_10, 0);
        lv_obj_set_style_bg_color(ta, lv_color_hex(0x141414), 0);
        lv_obj_set_style_border_color(ta, lv_color_hex(NFT_CLR_GREEN), LV_STATE_FOCUSED);
        lv_textarea_set_max_length(ta, 64);   // fits an ENS name, not just 0x addresses

        // Pre-fill saved wallet if any
        String saved = storage.getNftWallet();
        if (saved.length() > 0) lv_textarea_set_text(ta, saved.c_str());

        lv_obj_t* kb = lv_keyboard_create(card);
        lv_obj_set_size(kb, 300, 130);
        lv_obj_set_style_bg_color(kb, lv_color_hex(0x0a0a0a), 0);
        lv_keyboard_set_textarea(kb, ta);

        lv_obj_t* btnRow = lv_obj_create(card);
        lv_obj_set_size(btnRow, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_bg_opa(btnRow, LV_OPA_0, 0);
        lv_obj_set_style_border_width(btnRow, 0, 0);

        lv_obj_t* cancelBtn = addModalButton(btnRow, "CANCEL", false);
        lv_obj_t* saveBtn   = addModalButton(btnRow, "LOAD NFTs", true);

        static lv_obj_t* sTa; sTa = ta;
        static lv_obj_t* sCard; sCard = card;
        static NftScreen* sSelf; sSelf = this;

        lv_obj_add_event_cb(saveBtn, [](lv_event_t*) {
            const char* w = lv_textarea_get_text(sTa);
            if (!w) return;
            size_t len = strlen(w);
            // Accept a full 0x address (42 chars) OR an ENS name ("x.eth",
            // min 5 chars) — the fetch task resolves ENS to the address.
            bool isAddr = (len == 42 && strncmp(w, "0x", 2) == 0);
            bool isEns  = (len >= 5 && strcasecmp(w + len - 4, ".eth") == 0);
            if (!isAddr && !isEns) return;
            storage.setNftWallet(String(w));
            closeModal(sCard);
            if (sSelf) sSelf->_startFetch();
        }, LV_EVENT_CLICKED, nullptr);

        lv_obj_add_event_cb(cancelBtn, [](lv_event_t*) {
            closeModal(sCard);
        }, LV_EVENT_CLICKED, nullptr);
    }

    // ── Image download/decode worker ─────────────────────────────────────────
    // Fetches artwork for the NFTs currently on screen (first item of each
    // cell's collection group first, then the rest, capped) and decodes to
    // 224×224 RGB565 in PSRAM via img_decode.h. Cells repaint via _imgDirty.
    #define NFT_IMG_SIDE     224   // decode size; cells zoom-fit whatever grid is active
    #define NFT_IMG_MAX_FETCH 12   // per run — carousel taps re-trigger for the rest

    void _startImageFetch() {
        if (_imgTask || _nftCache.empty()) return;
        xTaskCreatePinnedToCore(_bgImgFetchFn, "nft_img", 12288, nullptr, 1, (TaskHandle_t*)&_imgTask, 0);
    }

    static void _bgImgFetchFn(void* /*pvArg*/) {
        NftScreen* self = s_instance;
        if (!self) { vTaskDelete(nullptr); return; }
        netLock();   // exclusive TLS ownership — see net_lock.h

        // Priority order: the visible NFT of each cell, then everything else.
        int order[NFT_MAX_ITEMS];
        int on = 0;
        int total = (int)self->_nftCache.size();
        for (int c = 0; c < self->_cellCount && on < NFT_IMG_MAX_FETCH; c++) {
            CellWidgets& cw = self->_cells[c];
            int i = cw.nftStart + (cw.nftCount > 0 ? (cw.nftCurrent % cw.nftCount) : 0);
            if (i >= 0 && i < total) order[on++] = i;
        }
        for (int i = 0; i < total && on < NFT_IMG_MAX_FETCH; i++) {
            bool dup = false;
            for (int k = 0; k < on; k++) if (order[k] == i) { dup = true; break; }
            if (!dup) order[on++] = i;
        }

        int fetched = 0;
        for (int k = 0; k < on; k++) {
            NftItem& it = self->_nftCache[order[k]];
            if (it.img_pixels || it.img_tried || !it.image_url[0]) continue;
            it.img_tried = true;

            // seadn.io is imgix-backed: ask for a small variant. Fall back to
            // the raw URL if the sized variant fails.
            String base = it.image_url;
            int q = base.indexOf('?');
            if (q >= 0) base = base.substring(0, q);
            uint8_t* px = imgdec::fetchRgb565((base + "?w=256&auto=format").c_str(),
                                              NFT_IMG_SIDE, NFT_IMG_SIDE, it.name);
            if (!px) px = imgdec::fetchRgb565(it.image_url, NFT_IMG_SIDE, NFT_IMG_SIDE, it.name);
            if (px) {
                it.img_pixels = px;
                it.img_w = it.img_h = NFT_IMG_SIDE;
                self->_imgDirty = true;   // poll timer repaints on core 1
                fetched++;
            }
            delay(NFT_RATELIMIT_DELAY_MS);
        }
        Serial.printf("NFT img worker: %d/%d images decoded\n", fetched, on);

        netUnlock();
        if (s_instance) s_instance->_imgTask = nullptr;   // ALWAYS clear before self-delete
        vTaskDelete(nullptr);
    }

    // ── Background fetch task ─────────────────────────────────────────────────
    //   Runs on core 0. Writes into _pendingResult.
    //   LVGL must only be touched from core 1 via the poll timer.

    static void _bgFetchFn(void* pvArg) {
        NftScreen* self = s_instance;
        if (!self) { vTaskDelete(nullptr); return; }
        netLock();   // exclusive TLS ownership — see net_lock.h

        String wallet = storage.getNftWallet();

        // ENS support: "tonysoprano.eth" → resolve to the 0x address via the
        // public ensideas resolver. The ENS name stays in NVS (it's what the
        // user typed and recognises); resolution happens per fetch, which the
        // daily NFT cache already rate-limits.
        if (wallet.length() >= 5 && wallet.endsWith(".eth")) {
            HTTPClient ens;
            ens.useHTTP10(true);   // body parsed from getStream()
            String lower = wallet; lower.toLowerCase();
            ens.begin("https://api.ensideas.com/ens/resolve/" + lower);
            ens.setTimeout(8000);
            String resolved;
            if (ens.GET() == 200) {
                JsonDocument doc;
                if (deserializeJson(doc, ens.getStream()) == DeserializationError::Ok)
                    resolved = doc["address"] | "";
            }
            ens.end();
            if (resolved.length() == 42 && resolved.startsWith("0x")) {
                wallet = resolved;
            } else {
                snprintf(_pendingResult.error_msg, sizeof(_pendingResult.error_msg),
                         "Could not resolve %s", wallet.c_str());
                _pendingResult.error = true;
                _pendingResult.ready = true;
                netUnlock();
        if (s_instance) s_instance->_bgTask = nullptr;   // ALWAYS clear before self-delete
                vTaskDelete(nullptr);
                return;
            }
        }

        if (wallet.length() < 42) {
            snprintf(_pendingResult.error_msg, sizeof(_pendingResult.error_msg), "No wallet configured.");
            _pendingResult.error = true;
            _pendingResult.ready = true;
            netUnlock();
        if (s_instance) s_instance->_bgTask = nullptr;   // ALWAYS clear before self-delete
            vTaskDelete(nullptr);
            return;
        }

        // ── Step 1: Fetch NFT list from OpenSea ─────────────────────────────
        // GET /api/v2/chain/{chain}/account/{address}/nfts?limit=50
        String nftsUrl = String(ENDPOINT_OPENSEA_BASE) +
                         "/chain/" + NFT_OPENSEA_CHAIN +
                         "/account/" + wallet +
                         "/nfts?limit=24";

        // Up to 3 attempts, 2.5 s apart: the first try often lands while the
        // ticker worker still holds a TLS connection and internal RAM is too
        // low for another handshake ("SSL - Memory allocation failed" →
        // HTTP -1). A couple of seconds later the heap has recovered.
        HTTPClient http;
        int code = -1;
        for (int attempt = 0; attempt < 3; attempt++) {
            if (attempt > 0) {
                Serial.printf("NFT fetch retry %d (prev code %d)\n", attempt, code);
                vTaskDelay(pdMS_TO_TICKS(2500));
            }
            http.useHTTP10(true);   // body parsed from getStream() — avoid chunked encoding
            http.begin(nftsUrl);
            if (strlen(OPENSEA_API_KEY) > 0)
                http.addHeader("X-API-KEY", OPENSEA_API_KEY);
            http.addHeader("Accept", "application/json");
            http.setTimeout(20000);
            code = http.GET();
            if (code == 200) break;
            http.end();
        }
        if (code != 200) {
            if (code == 401 || code == 403) {
                // OpenSea now requires an API key for the account-NFTs
                // endpoint. The key is baked in at build time (see the
                // OPENSEA_API_KEY secret in the release workflow).
                snprintf(_pendingResult.error_msg, sizeof(_pendingResult.error_msg),
                         "OpenSea requires an API key (HTTP %d).\nRebuild with OPENSEA_API_KEY set.", code);
            } else {
                snprintf(_pendingResult.error_msg, sizeof(_pendingResult.error_msg),
                         "OpenSea HTTP %d", code);
            }
            _pendingResult.error = true;
            _pendingResult.ready = true;
            http.end();
            netUnlock();
        if (s_instance) s_instance->_bgTask = nullptr;   // ALWAYS clear before self-delete
            vTaskDelete(nullptr);
            return;
        }

        // Parse NFTs — collect slugs and basic info
        struct RawNft {
            char name[64]       = {};
            char slug[64]       = {};
            char image_url[256] = {};
        };

        // We parse in two passes to keep heap low on the ESP32.
        // First pass: collect slugs and item count per slug.
        // JsonDocument needs to fit the response (~50 NFTs * ~300 bytes = ~15 KB).
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, http.getStream());
        http.end();

        if (err) {
            snprintf(_pendingResult.error_msg, sizeof(_pendingResult.error_msg),
                     "JSON parse error: %s", err.c_str());
            _pendingResult.error = true;
            _pendingResult.ready = true;
            netUnlock();
        if (s_instance) s_instance->_bgTask = nullptr;   // ALWAYS clear before self-delete
            vTaskDelete(nullptr);
            return;
        }

        JsonArray nfts = doc["nfts"].as<JsonArray>();
        Serial.printf("NFT: OpenSea returned %d NFTs for %s\n", (int)nfts.size(), wallet.c_str());

        // Collect unique slugs and raw items (limited to NFT_MAX_ITEMS)
        static RawNft rawNfts[NFT_MAX_ITEMS];
        static char slugList[NFT_MAX_COLLECTIONS][64];
        int rawCount  = 0;
        int slugCount = 0;

        for (JsonObject nft : nfts) {
            if (rawCount >= NFT_MAX_ITEMS) break;

            const char* slug     = nft["collection"] | "";
            const char* name     = nft["name"]        | "";
            // display_image_url is OpenSea's pre-resized variant (~500px) —
            // ideal for on-device decode. NEVER metadata_url (that's JSON).
            const char* imgUrl   = nft["display_image_url"] | nft["image_url"] | "";

            if (!slug[0]) continue;  // skip NFTs with no collection slug
            if (!imgUrl[0]) continue; // skip NFTs with no image

            // Add slug to unique list
            bool knownSlug = false;
            for (int si = 0; si < slugCount; si++) {
                if (strcmp(slugList[si], slug) == 0) { knownSlug = true; break; }
            }
            if (!knownSlug && slugCount < NFT_MAX_COLLECTIONS) {
                strncpy(slugList[slugCount++], slug, 63);
            }

            strncpy(rawNfts[rawCount].name,      name,   sizeof(rawNfts[0].name)-1);
            strncpy(rawNfts[rawCount].slug,      slug,   sizeof(rawNfts[0].slug)-1);
            strncpy(rawNfts[rawCount].image_url, imgUrl, sizeof(rawNfts[0].image_url)-1);
            rawCount++;
        }

        // ── Step 2: Fetch floor price for each unique slug ───────────────────
        static float floorPrices[NFT_MAX_COLLECTIONS];
        static char  collectionNames[NFT_MAX_COLLECTIONS][64];
        memset(floorPrices, 0, sizeof(floorPrices));
        memset(collectionNames, 0, sizeof(collectionNames));

        for (int si = 0; si < slugCount; si++) {
            String statsUrl = String(ENDPOINT_OPENSEA_BASE) +
                              "/collections/" + slugList[si] + "/stats";
            HTTPClient hStats;
            hStats.useHTTP10(true);   // body parsed from getStream() — avoid chunked encoding
            hStats.begin(statsUrl);
            if (strlen(OPENSEA_API_KEY) > 0)
                hStats.addHeader("X-API-KEY", OPENSEA_API_KEY);
            hStats.addHeader("Accept", "application/json");
            hStats.setTimeout(6000);

            int sc = hStats.GET();
            if (sc == 200) {
                JsonDocument sDoc;
                if (deserializeJson(sDoc, hStats.getStream()) == DeserializationError::Ok) {
                    floorPrices[si] = sDoc["total"]["floor_price"] | 0.0f;
                    const char* cn = sDoc["name"] | slugList[si];
                    strncpy(collectionNames[si], cn, 63);
                }
            }
            hStats.end();
            Serial.printf("NFT stats[%s] HTTP %d floor=%.4f\n", slugList[si], sc, floorPrices[si]);

            // Rate-limit: don't hammer OpenSea's free tier
            delay(NFT_RATELIMIT_DELAY_MS);
        }

        // ── Step 3: Assemble NftItem list — filter spam, sort by floor price ─
        _pendingResult.count = 0;
        for (int ri = 0; ri < rawCount && _pendingResult.count < NFT_MAX_ITEMS; ri++) {
            // Find this item's collection floor price
            float fp = 0.0f;
            char  colName[64] = {};
            for (int si = 0; si < slugCount; si++) {
                if (strcmp(slugList[si], rawNfts[ri].slug) == 0) {
                    fp = floorPrices[si];
                    strncpy(colName, collectionNames[si], 63);
                    break;
                }
            }

            // Spam filter: only include if floor price > 0 (real collection)
            if (fp <= 0.0f) continue;

            NftItem& item = _pendingResult.items[_pendingResult.count++];
            strncpy(item.name,       rawNfts[ri].name,      sizeof(item.name)-1);
            strncpy(item.slug,       rawNfts[ri].slug,      sizeof(item.slug)-1);
            strncpy(item.collection, colName[0] ? colName : rawNfts[ri].slug, sizeof(item.collection)-1);
            strncpy(item.image_url,  rawNfts[ri].image_url, sizeof(item.image_url)-1);
            item.floor_price_eth = fp;
        }

        // Sort by floor price descending, tie-broken by slug so items of the
        // SAME collection stay contiguous — the grid walks these as groups
        // (one collection per cell). Bubble sort — small N.
        _sortByFloor();
        Serial.printf("NFT: %d items after spam filter (sorted by floor desc)\n", _pendingResult.count);

        _pendingResult.ready = true;

        self->_bgTask = nullptr;
        netUnlock();
        if (s_instance) s_instance->_bgTask = nullptr;   // ALWAYS clear before self-delete
        vTaskDelete(nullptr);
    }
};

// Static member definitions (allocated in BSS / PSRAM)
NftScreen* NftScreen::s_instance = nullptr;
NftPendingResult NftScreen::_pendingResult;
