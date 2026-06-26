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
    // stored in PSRAM. Only populated if JPEG decode support is compiled in.
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

        _btn1x1 = _makeSizeBtn(_sizeBar, "1×1");
        _btn2x2 = _makeSizeBtn(_sizeBar, "2×2");
        _btn3x3 = _makeSizeBtn(_sizeBar, "3×3");

        // Carousel toggle
        lv_obj_t* carLbl = lv_label_create(_sizeBar);
        lv_label_set_text(carLbl, "Carousel");
        lv_obj_set_style_text_font(carLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(carLbl, lv_color_hex(NFT_CLR_MUTED), 0);
        lv_obj_set_style_pad_left(carLbl, 8, 0);

        _carouselSwitch = lv_switch_create(_sizeBar);
        lv_obj_set_style_bg_color(_carouselSwitch, lv_color_hex(NFT_CLR_GREEN), LV_PART_INDICATOR | LV_STATE_CHECKED);
        if (storage.getNftCarousel()) lv_obj_add_state(_carouselSwitch, LV_STATE_CHECKED);
        lv_obj_add_event_cb(_carouselSwitch, [](lv_event_t* e) {
            NftScreen* self = (NftScreen*)lv_event_get_user_data(e);
            bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
            storage.setNftCarousel(on);
            self->_applyCarouselSetting(on);
        }, LV_EVENT_VALUE_CHANGED, this);

        // ── Grid area ─────────────────────────────────────────────────────────
        _gridArea = lv_obj_create(_body);
        lv_obj_set_size(_gridArea, 480, NFT_GRID_H);
        lv_obj_align(_gridArea, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(_gridArea, lv_color_hex(NFT_CLR_BG), 0);
        lv_obj_set_style_border_width(_gridArea, 0, 0);
        lv_obj_set_style_pad_all(_gridArea, 4, 0);
        lv_obj_set_style_pad_gap(_gridArea, 4, 0);
        lv_obj_clear_flag(_gridArea, LV_OBJ_FLAG_SCROLLABLE);

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
            NftScreen* self = (NftScreen*)lv_timer_get_user_data(t);
            if (self) self->_onSlideshowTick();
        }, 1000, this);

        // ── Pending-result poll timer ─────────────────────────────────────────
        lv_timer_create([](lv_timer_t* t) {
            NftScreen* self = (NftScreen*)lv_timer_get_user_data(t);
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
            _openWalletDialog();
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
    lv_obj_t*  _loadingLabel   = nullptr;
    lv_obj_t*  _spinner        = nullptr;
    void*      _userData       = nullptr;

    int        _gridSize       = 9;    // 1, 4, or 9
    bool       _fetching       = false;

    // Cells (max 9 for 3×3 grid)
    struct CellWidgets {
        lv_obj_t* container = nullptr;
        lv_obj_t* img       = nullptr;   // lv_canvas or lv_img for decoded image
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
    TaskHandle_t _bgTask = nullptr;
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

        lv_label_set_text(_loadingLabel, "Loading pinlisted NFTs…");
        lv_obj_clear_flag(_spinner, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < _cellCount; i++) {
            if (_cells[i].container) lv_obj_add_flag(_cells[i].container, LV_OBJ_FLAG_HIDDEN);
        }

        if (_bgTask) { vTaskDelete(_bgTask); _bgTask = nullptr; }
        xTaskCreatePinnedToCore(_bgPinlistFetchFn, "nft_pin", 16384, nullptr, 1, &_bgTask, 0);
    }

    static void _bgPinlistFetchFn(void* /*pvArg*/) {
        NftScreen* self = s_instance;
        if (!self) { vTaskDelete(nullptr); return; }

        String pinlist = storage.getNftPinlist();
        if (pinlist.length() == 0) {
            snprintf(_pendingResult.error_msg, sizeof(_pendingResult.error_msg), "Pinlist is empty.");
            _pendingResult.error = true;
            _pendingResult.ready = true;
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
            http.begin(url);
            if (strlen(OPENSEA_API_KEY) > 0) http.addHeader("X-API-KEY", OPENSEA_API_KEY);
            http.addHeader("Accept", "application/json");
            http.setTimeout(10000);

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

        _pendingResult.ready = true;
        self->_bgTask = nullptr;
        vTaskDelete(nullptr);
    }

    void _startFetch() {
        if (_fetching) return;
        _fetching = true;
        _pendingResult.ready = false;
        _pendingResult.error = false;
        _pendingResult.count = 0;

        // Show spinner
        lv_label_set_text(_loadingLabel, "Fetching your NFTs…");
        lv_obj_clear_flag(_spinner, LV_OBJ_FLAG_HIDDEN);
        // Hide existing cells
        for (int i = 0; i < _cellCount; i++) {
            if (_cells[i].container) lv_obj_add_flag(_cells[i].container, LV_OBJ_FLAG_HIDDEN);
        }

        if (_bgTask) { vTaskDelete(_bgTask); _bgTask = nullptr; }
        xTaskCreatePinnedToCore(_bgFetchFn, "nft_fetch", 16384, nullptr, 1, &_bgTask, 0);
    }

    void _pollPending() {
        if (!_fetching) return;
        if (!_pendingResult.ready) return;

        _fetching = false;
        lv_obj_add_flag(_spinner, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(_loadingLabel, "");

        if (_pendingResult.error) {
            lv_label_set_text(_loadingLabel, _pendingResult.error_msg);
            return;
        }

        // Absorb results into cache
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

        // For a N-cell grid, group NFTs into N groups (one per cell).
        // Each group shows the top NFT for that "slot" (sorted by floor price).
        // If nftCache has fewer items than _gridSize, we still create _gridSize cells
        // and re-use items cyclically.
        int n = _gridSize;                   // 1, 4, or 9
        int side = (n == 1) ? 1 : (n == 4 ? 2 : 3);
        lv_coord_t cellW = (lv_coord_t)((480 - 4 - (side - 1) * 4) / side);
        lv_coord_t cellH = (lv_coord_t)((NFT_GRID_H - 4 - (side - 1) * 4) / side);

        int total = (int)_nftCache.size();
        // Distribute NFTs round-robin across cells so each cell has roughly equal share
        int perCell = (total + n - 1) / n;   // ceil division

        for (int ci = 0; ci < n; ci++) {
            _buildCell(ci, ci, side, cellW, cellH, ci * perCell, min(perCell, total - ci * perCell));
        }
        _cellCount = n;
        _slideshowSecs  = storage.getNftSlideshowSecs();
        _slideshowCount = _slideshowSecs;
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
            // Decoded image available: show as LVGL image descriptor
            // img_pixels must be LVGL-native RGB565 in PSRAM
            static lv_img_dsc_t imgDsc;  // static ok: only one cell shown at a time
            imgDsc.header.always_zero = 0;
            imgDsc.header.w           = item.img_w;
            imgDsc.header.h           = item.img_h;
            imgDsc.header.cf          = LV_IMG_CF_TRUE_COLOR;
            imgDsc.data_size          = item.img_w * item.img_h * 2;
            imgDsc.data               = item.img_pixels;

            lv_obj_t* imgObj = lv_img_create(cw.container);
            lv_img_set_src(imgObj, &imgDsc);
            lv_img_set_zoom(imgObj, 256);  // 1:1
            lv_obj_align(imgObj, LV_ALIGN_TOP_MID, 0, 0);
            lv_img_set_antialias(imgObj, false);
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
            if (item.floor_price_eth < 0.001f)
                snprintf(floorBuf, sizeof(floorBuf), "Ξ%.4f", item.floor_price_eth);
            else
                snprintf(floorBuf, sizeof(floorBuf), "Ξ%.3f", item.floor_price_eth);
            lv_label_set_text(cw.floorLbl, floorBuf);
            lv_obj_set_style_text_font(cw.floorLbl, &lv_font_montserrat_10, 0);
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
        lv_label_set_text(hint, "Enter your EVM wallet address to view your NFTs.");
        lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(hint, 280);
        lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(hint, lv_color_hex(NFT_CLR_MUTED), 0);

        lv_obj_t* ta = lv_textarea_create(card);
        lv_obj_set_size(ta, 280, 36);
        lv_textarea_set_placeholder_text(ta, "0x…");
        lv_textarea_set_one_line(ta, true);
        lv_obj_set_style_text_font(ta, &lv_font_montserrat_10, 0);
        lv_obj_set_style_bg_color(ta, lv_color_hex(0x141414), 0);
        lv_obj_set_style_border_color(ta, lv_color_hex(NFT_CLR_GREEN), LV_STATE_FOCUSED);
        lv_textarea_set_max_length(ta, 42);

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
            if (!w || strlen(w) < 42) return;
            storage.setNftWallet(String(w));
            closeModal(sCard);
            if (sSelf) sSelf->_startFetch();
        }, LV_EVENT_CLICKED, nullptr);

        lv_obj_add_event_cb(cancelBtn, [](lv_event_t*) {
            closeModal(sCard);
        }, LV_EVENT_CLICKED, nullptr);
    }

    // ── Background fetch task ─────────────────────────────────────────────────
    //   Runs on core 0. Writes into _pendingResult.
    //   LVGL must only be touched from core 1 via the poll timer.

    static void _bgFetchFn(void* pvArg) {
        NftScreen* self = s_instance;
        if (!self) { vTaskDelete(nullptr); return; }

        String wallet = storage.getNftWallet();
        if (wallet.length() < 42) {
            snprintf(_pendingResult.error_msg, sizeof(_pendingResult.error_msg), "No wallet configured.");
            _pendingResult.error = true;
            _pendingResult.ready = true;
            vTaskDelete(nullptr);
            return;
        }

        // ── Step 1: Fetch NFT list from OpenSea ─────────────────────────────
        // GET /api/v2/chain/{chain}/account/{address}/nfts?limit=50
        String nftsUrl = String(ENDPOINT_OPENSEA_BASE) +
                         "/chain/" + NFT_OPENSEA_CHAIN +
                         "/account/" + wallet +
                         "/nfts?limit=50";

        HTTPClient http;
        http.begin(nftsUrl);
        if (strlen(OPENSEA_API_KEY) > 0)
            http.addHeader("X-API-KEY", OPENSEA_API_KEY);
        http.addHeader("Accept", "application/json");
        http.setTimeout(10000);

        int code = http.GET();
        if (code != 200) {
            snprintf(_pendingResult.error_msg, sizeof(_pendingResult.error_msg),
                     "OpenSea HTTP %d", code);
            _pendingResult.error = true;
            _pendingResult.ready = true;
            http.end();
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
            vTaskDelete(nullptr);
            return;
        }

        JsonArray nfts = doc["nfts"].as<JsonArray>();

        // Collect unique slugs and raw items (limited to NFT_MAX_ITEMS)
        static RawNft rawNfts[NFT_MAX_ITEMS];
        static char slugList[NFT_MAX_COLLECTIONS][64];
        int rawCount  = 0;
        int slugCount = 0;

        for (JsonObject nft : nfts) {
            if (rawCount >= NFT_MAX_ITEMS) break;

            const char* slug     = nft["collection"] | "";
            const char* name     = nft["name"]        | "";
            const char* imgUrl   = nft["image_url"]   | nft["metadata_url"] | "";

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

        // Sort by floor price descending (bubble sort — small N)
        for (int i = 0; i < _pendingResult.count - 1; i++) {
            for (int j = 0; j < _pendingResult.count - 1 - i; j++) {
                if (_pendingResult.items[j].floor_price_eth < _pendingResult.items[j+1].floor_price_eth) {
                    NftItem tmp = _pendingResult.items[j];
                    _pendingResult.items[j]   = _pendingResult.items[j+1];
                    _pendingResult.items[j+1] = tmp;
                }
            }
        }

        _pendingResult.ready = true;

        self->_bgTask = nullptr;
        vTaskDelete(nullptr);
    }
};

// Static member definitions (allocated in BSS / PSRAM)
NftScreen* NftScreen::s_instance = nullptr;
NftPendingResult NftScreen::_pendingResult;
