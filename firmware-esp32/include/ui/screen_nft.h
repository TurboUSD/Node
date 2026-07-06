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
#define NFT_HEADER_H           26   // height of the grid-size selector strip (slimmed for grid space)
#define NFT_CAPTION_H          16   // black caption band under each artwork (name + floor)
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
#define NFT_CLR_ACTIVE  0xd8d8dc   // active-control tone (bright grey — green stole the show)

// ── Data structures ───────────────────────────────────────────────────────────

struct NftItem {
    char name[64]          = {};
    char collection[64]    = {};  // human-readable collection name
    char slug[64]          = {};  // OpenSea collection slug
    char image_url[256]    = {};
    float floor_price_eth  = 0.0f;
    bool  pinned           = false;   // manual pick — ALWAYS earns a grid cell
    bool  floor_btc        = false;   // floor denominated in BTC (Ordinals) instead of ETH
    uint32_t bg_color      = 0;       // transparency composite colour (0 = black)
    // Decoded artwork, ONE SLOT PER GRID CLASS (0=1x1, 1=2x2, 2=3x3) so
    // switching grid sizes can show something instantly. RGB565 in PSRAM,
    // produced by the nft_img bg task via img_decode.h. Retention: the
    // current grid's slot for every item, plus ALL THREE slots for the cell
    // "covers" (first NFT of the top collections) — see _entitledSlot().
    uint8_t* px[3]    = {};
    uint16_t pw[3]    = {};
    uint16_t ph[3]    = {};
    bool     tried[3] = {};

    void freeSlot(int c) { if (px[c]) { free(px[c]); px[c] = nullptr; } tried[c] = false; }
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
    // Ξ — 7×7 1bpp, rows packed MSB-first: ▬▬▬ / gap / ▬▬ inset / gap / ▬▬▬
    0xFE, 0x00, 0x03, 0xE0, 0x00, 0x3F, 0x80,
    // ₿ — 7×9 1bpp: a "B" with the two strokes poking out top and bottom
    0x51, 0xF2, 0x14, 0x2F, 0x90, 0xA1, 0x7C, 0x50,
};
static const lv_font_fmt_txt_glyph_dsc_t s_xiGlyphs[] = {
    {0, 0,   0, 0, 0, 0},    // glyph id 0 is reserved in LVGL fonts
    {0, 144, 7, 7, 1, 0},    // Ξ: adv 9px (144/16), 7×7 box on the baseline
    {7, 144, 7, 9, 1, -1},   // ₿: bitmap offset 7, 7×9 box, dips 1px below baseline
};
static const lv_font_t* ethXiFont10() {
    static bool ready = false;
    static lv_font_fmt_txt_cmap_t        cmaps[2];
    static lv_font_fmt_txt_dsc_t         dsc;
    static lv_font_fmt_txt_glyph_cache_t cache;
    static lv_font_t                     font;
    if (!ready) {
        memset(cmaps, 0, sizeof(cmaps));
        cmaps[0].range_start    = 0x039E;   // Ξ (ETH floors)
        cmaps[0].range_length   = 1;
        cmaps[0].glyph_id_start = 1;
        cmaps[0].type           = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY;
        cmaps[1].range_start    = 0x20BF;   // ₿ (Ordinals floors)
        cmaps[1].range_length   = 1;
        cmaps[1].glyph_id_start = 2;
        cmaps[1].type           = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY;

        memset(&dsc, 0, sizeof(dsc));
        dsc.glyph_bitmap = s_xiBitmap;
        dsc.glyph_dsc    = s_xiGlyphs;
        dsc.cmaps        = cmaps;
        dsc.cmap_num     = 2;
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
        lv_obj_set_style_pad_all(_sizeBar, 3, 0);
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
        lv_label_set_text(_carouselSwitch, LV_SYMBOL_LOOP);   // carousel = cycle icon
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

        // "Data" toggle — same treatment as Carousel: green word = captions
        // (collection name + floor) shown, grey = hidden. Default ON.
        _dataSwitch = lv_label_create(_sizeBar);
        lv_label_set_text(_dataSwitch, LV_SYMBOL_LIST);       // data captions = list icon
        lv_obj_set_style_text_font(_dataSwitch, &lv_font_montserrat_12, 0);
        lv_obj_set_style_pad_left(_dataSwitch, 8, 0);
        lv_obj_add_flag(_dataSwitch, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(_dataSwitch, 10);
        _refreshDataLabel();
        lv_obj_add_event_cb(_dataSwitch, [](lv_event_t* e) {
            NftScreen* self = (NftScreen*)lv_event_get_user_data(e);
            storage.setNftShowData(!storage.getNftShowData());
            storage.setNftListsDirty(true);   // sync to the web on next heartbeat
            self->_refreshDataLabel();
            for (int i = 0; i < self->_cellCount; i++) self->_refreshCell(i);
        }, LV_EVENT_CLICKED, this);

        // Gear — edit mode (reorder arrows + delete on each cell), mirrors the
        // tickers screen's gear. Sits at the far right, to the RIGHT of Wallet.
        _editBtn = lv_label_create(_sizeBar);
        lv_obj_add_flag(_editBtn, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_label_set_text(_editBtn, LV_SYMBOL_SETTINGS);
        lv_obj_set_style_text_font(_editBtn, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(_editBtn, lv_color_hex(NFT_CLR_MUTED), 0);
        lv_obj_align(_editBtn, LV_ALIGN_RIGHT_MID, -8, 0);   // far right — to the RIGHT of Wallet
        lv_obj_add_flag(_editBtn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(_editBtn, 10);
        lv_obj_add_event_cb(_editBtn, [](lv_event_t* e) {
            NftScreen* self = (NftScreen*)lv_event_get_user_data(e);
            self->_editMode = !self->_editMode;
            lv_obj_set_style_text_color(self->_editBtn,
                lv_color_hex(self->_editMode ? NFT_CLR_GREEN : NFT_CLR_MUTED), 0);
            self->_rebuildReq = true;   // cells gain/lose their edit overlays
        }, LV_EVENT_CLICKED, this);

        // ── "+ Wallet" button (floats at the right of the strip) ──────────────
        // Dedicated target for entering/changing the NFT wallet, so the grid
        // never has to be tappable (which used to fire on swipe-in). IGNORE_LAYOUT
        // keeps it out of the flex row so it can sit hard against the right edge.
        // Plain tappable green word — no button chrome (same treatment as the
        // Carousel toggle; the solid green pill dominated the strip).
        _addWalletBtn = lv_btn_create(_sizeBar);
        lv_obj_add_flag(_addWalletBtn, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_set_size(_addWalletBtn, LV_SIZE_CONTENT, 20);
        lv_obj_align(_addWalletBtn, LV_ALIGN_RIGHT_MID, -30, 0);   // left of the gear
        lv_obj_set_style_bg_opa(_addWalletBtn, LV_OPA_0, 0);
        lv_obj_set_style_border_width(_addWalletBtn, 0, 0);
        lv_obj_set_style_shadow_width(_addWalletBtn, 0, 0);
        lv_obj_set_style_pad_hor(_addWalletBtn, 4, 0);
        lv_obj_set_ext_click_area(_addWalletBtn, 10);
        lv_obj_t* awLbl = lv_label_create(_addWalletBtn);
        lv_label_set_text(awLbl, "Wallet");
        lv_obj_set_style_text_font(awLbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(awLbl, lv_color_hex(NFT_CLR_ACTIVE), 0);
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
        // Instant display from the disk snapshot on the first show after boot
        // (images come from the LittleFS art cache); a live refresh follows.
        if (_nftCache.empty() && (storage.hasNftPinlist() || storage.hasNftWallet())) {
            if (_loadSnapshot()) _rebuildGrid();
        }

        if (storage.hasNftPinlist() && !storage.hasNftWallet()) {
            // Picks only (no wallet configured)
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
    bool       _editMode       = false;   // gear: reorder/delete overlays on cells
    volatile bool _rebuildReq  = false;   // deferred rebuild (set inside event cbs)
    lv_obj_t*  _dataSwitch     = nullptr; // "Data" caption toggle word
    lv_obj_t*  _editBtn        = nullptr; // gear button
    // UI-thread copies of the NVS order/hidden lists (the img worker reads
    // these from core 0 — plain char buffers, no String tearing).
    char _ordBuf[384] = {};
    char _hidBuf[384] = {};

    // Cells (max 9 for 3×3 grid)
    struct CellWidgets {
        lv_obj_t* container = nullptr;
        lv_obj_t* img       = nullptr;   // lv_canvas or lv_img for decoded image
        lv_img_dsc_t imgDsc = {};        // PER-CELL: a shared static dsc made every cell show the same image
        uint32_t dotsAt = 0;             // when the carousel dots were last revealed (tap)
        const uint8_t* shownPx = nullptr; // bitmap currently displayed (repaint only on change)
        lv_coord_t cellW = 0, cellH = 0;  // set at build — lv_obj_get_width() reads 0 pre-layout
        lv_obj_t* nameLbl   = nullptr;
        lv_obj_t* floorLbl  = nullptr;
        lv_obj_t* dotRow    = nullptr;   // carousel position dots
        int       nftStart  = 0;        // index of first NFT in this cell's "group"
        int       nftCount  = 0;        // how many NFTs are in this cell's group
        int       nftCurrent= 0;        // which one is displayed right now
    } _cells[9];
    int _cellCount = 0;

    // One collection group in the (floor-sorted) cache: contiguous run.
    struct NftGrp {
        int  start;
        int  count;
        char slug[64];
    };

    // Cached NFT data (persists until expired or wallet changes)
    std::vector<NftItem> _nftCache;
    uint32_t _cacheTimestamp = 0;
    bool     _snapshotOnly   = false;   // cache came from disk → still needs a live refresh
    String   _lastListSig;              // change detector: skip rebuilds on identical refreshes
    String   _fetchedPinlistSig;        // the pinlist string the current cache was built from —
                                        // if it changes on the web (pins added/removed/reordered
                                        // or a background colour edited) we force a fresh fetch

    // Slideshow state
    uint8_t _slideshowSecs  = 10;
    uint8_t _slideshowCount = 0;   // counts seconds down

    // ── FreeRTOS ─────────────────────────────────────────────────────────────
    volatile TaskHandle_t _bgTask  = nullptr;   // written by workers on core 0, read on core 1
    volatile TaskHandle_t _imgTask = nullptr;   // image download/decode worker
    volatile bool         _imgDirty = false;    // set by img task → poll refreshes cells
    int                   _decodedForGrid = 0;  // grid size the cached pixels were decoded for
    volatile uint16_t     _imgGen = 0;          // bumped when pixels are invalidated → stale workers abort
    static NftPendingResult _pendingResult;

    // ── Helpers ───────────────────────────────────────────────────────────────

    bool _cacheExpired() {
        if (_snapshotOnly) return true;   // disk snapshot displays, but refresh anyway
        if (_nftCache.empty()) return true;
        // A web edit to the pinlist (new/removed pin, reorder, or a changed
        // ordinal background colour) changes the stored string → refetch now
        // instead of waiting out the TTL, so the change shows on screen re-entry.
        if (storage.getNftPinlist() != _fetchedPinlistSig) return true;
        return (millis() - _cacheTimestamp) > NFT_CACHE_TTL_MS;
    }

    // ── Pinlist fetch (manual picks mode) ────────────────────────────────────
    // Fetches each NFT individually from OpenSea using the stored pinlist.
    // Format of pinlist: "ethereum:0xcontract:tokenId,base:0xcontract:tokenId,…"

    void _startPinlistFetch() {
        if (_fetching) return;
        _fetching = true;
        _fetchedPinlistSig = storage.getNftPinlist();   // remember what we're fetching
        _pendingResult.ready = false;
        _pendingResult.error = false;
        _pendingResult.count = 0;

        // Only blank the screen when there's nothing to show — with a disk
        // snapshot on display this refresh is silent.
        if (_nftCache.empty()) {
            lv_label_set_text(_loadingLabel, "Loading pinlisted NFTs...");
            lv_obj_clear_flag(_spinner, LV_OBJ_FLAG_HIDDEN);
            for (int i = 0; i < _cellCount; i++)
                if (_cells[i].container) lv_obj_add_flag(_cells[i].container, LV_OBJ_FLAG_HIDDEN);
        }

        if (_bgTask) { _fetching = false; return; }   // never vTaskDelete — see _startFetch
        xTaskCreatePinnedToCore(_bgPinlistFetchFn, "nft_pin", 16384, nullptr, 1, (TaskHandle_t*)&_bgTask, 0);
    }

    // Parses the stored pinlist and APPENDS each pick to _pendingResult
    // (pinned=true, deduped by image URL). Used by the pinlist-only task AND
    // by the wallet task so manual picks merge into the wallet scan.
    static void _appendPinlistItems() {
        String pinlist = storage.getNftPinlist();
        if (pinlist.length() == 0) return;

        // ── Parse "chain:contract:tokenId,…" ─────────────────────────────────
        struct PinEntry {
            char chain[16]    = {};
            char contract[72] = {};   // EVM 0x… (42) or Ordinals inscription id (66)
            char tokenId[24]  = {};
            char bg[10]       = {};   // ordinals only: optional 4th field "#rrggbb"
        };

        static PinEntry entries[NFT_MAX_ITEMS];
        int entryCount = 0;

        String item;
        pinlist += ',';  // sentinel to flush last item
        for (int ci = 0; ci < (int)pinlist.length() && entryCount < NFT_MAX_ITEMS; ci++) {
            char ch = pinlist[ci];
            if (ch == ',') {
                // Split on colons: chain : contract : tokenId [: #bg]
                // The optional 4th field is the ordinal's user-picked
                // background colour from the web editor ("ord:<id>:0:#f68b1f").
                int first  = item.indexOf(':');
                int second = item.indexOf(':', first + 1);
                if (first > 0 && second > first) {
                    PinEntry& e = entries[entryCount++];
                    item.substring(0, first).toCharArray(e.chain,    sizeof(e.chain));
                    item.substring(first + 1, second).toCharArray(e.contract, sizeof(e.contract));
                    String tail = item.substring(second + 1);
                    int third = tail.indexOf(':');
                    if (third > 0 && tail.charAt(third + 1) == '#') {
                        tail.substring(third + 1).toCharArray(e.bg, sizeof(e.bg));
                        tail = tail.substring(0, third);
                    }
                    tail.toCharArray(e.tokenId, sizeof(e.tokenId));
                }
                item = "";
            } else {
                item += ch;
            }
        }

        if (entryCount == 0) return;

        // ── Fetch each NFT individually from OpenSea ──────────────────────────

        for (int ei = 0; ei < entryCount && _pendingResult.count < NFT_MAX_ITEMS; ei++) {
            PinEntry& e = entries[ei];

            // Bitcoin Ordinals ("ord:<inscriptionId>:0"): no OpenSea metadata —
            // the artwork is served straight by ordinals.com/content/ (PNG for
            // NodeMonkes and friends; SVG/webp fall back to the wsrv proxy).
            if (strcmp(e.chain, "ord") == 0) {
                NftItem& oi = _pendingResult.items[_pendingResult.count++];
                oi = NftItem{};   // reset the reused slot before populating it
                snprintf(oi.name, sizeof(oi.name), "Ordinal");   // fallback
                snprintf(oi.collection, sizeof(oi.collection), "Ordinals");
                snprintf(oi.slug, sizeof(oi.slug), "ordinals");
                snprintf(oi.image_url, sizeof(oi.image_url),
                         "https://ordinals.com/content/%s", e.contract);
                oi.floor_price_eth = 0.0f;
                oi.floor_btc = true;   // Ordinals are ALWAYS BTC-denominated →
                                       // the caption must use ₿, never Ξ, even
                                       // before/without a resolved floor value.
                oi.pinned = true;
                // User-picked background from the pinlist 4th field: applied
                // up-front so it works even when resolve-ordinal is down.
                if (e.bg[0] == '#') oi.bg_color = (uint32_t)strtoul(e.bg + 1, nullptr, 16);

                // Best-effort real name ("NodeMonke #9401") + BTC floor via
                // our resolve-ordinal edge function (Magic Eden underneath).
                {
                    HTTPClient ho;
                    ho.useHTTP10(true);
                    ho.begin(String(SUPABASE_FUNCTIONS_BASE_URL) + "/resolve-ordinal");
                    ho.setTimeout(9000);
                    ho.addHeader("Content-Type", "application/json");
                    ho.addHeader("Authorization", String("Bearer ") + SUPABASE_ANON_KEY);
                    String body = String("{\"ids\":[\"") + e.contract + "\"]}";
                    int oCode = ho.POST(body);
                    Log.printf("NFT ord: resolve-ordinal POST %d for %.12s...\n", oCode, e.contract);
                    if (oCode == 200) {
                        JsonDocument od;
                        if (deserializeJson(od, ho.getStream()) == DeserializationError::Ok) {
                            const char* nm = od["results"][0]["name"] | "";
                            if (nm[0]) snprintf(oi.name, sizeof(oi.name), "%s", nm);
                            const char* cl = od["results"][0]["collection"] | "";
                            if (cl[0]) snprintf(oi.collection, sizeof(oi.collection), "%s", cl);
                            double fb = od["results"][0]["floor_btc"] | 0.0;
                            if (fb > 0) { oi.floor_price_eth = (float)fb; oi.floor_btc = true; }
                            const char* bg = od["results"][0]["bg"] | "";
                            if (bg[0] == '#') oi.bg_color = (uint32_t)strtoul(bg + 1, nullptr, 16);
                            // The owner's colour choice from the web editor
                            // WINS over any indexer-derived trait colour.
                            if (e.bg[0] == '#') oi.bg_color = (uint32_t)strtoul(e.bg + 1, nullptr, 16);
                            Log.printf("NFT ord: resolved name='%s' coll='%s' floor=%.5f bg=%06x\n",
                                          oi.name, oi.collection, oi.floor_price_eth, (unsigned)oi.bg_color);
                        } else {
                            Log.println("NFT ord: resolve-ordinal JSON parse failed");
                        }
                    }
                    ho.end();
                }
                delay(NFT_RATELIMIT_DELAY_MS);
                continue;
            }

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
            item2 = NftItem{};   // reset the reused slot — otherwise a slot that
                                 // previously held an Ordinal leaked floor_btc=true
                                 // (₿ on an ETH collection) and its bg_color.
            strncpy(item2.name,       name,                        sizeof(item2.name)       - 1);
            strncpy(item2.slug,       slug,                        sizeof(item2.slug)       - 1);
            strncpy(item2.collection, colName[0] ? colName : slug, sizeof(item2.collection) - 1);
            strncpy(item2.image_url,  image_url,                   sizeof(item2.image_url)  - 1);
            item2.floor_price_eth = fp;
            item2.floor_btc       = false;  // EVM/OpenSea collections are ETH-denominated
            item2.pinned          = true;   // manual pick → always earns a grid cell

            delay(NFT_RATELIMIT_DELAY_MS);
        }


        // Dedupe: a pick that also came in via the wallet scan keeps ONE entry
        // (the pinned one — mark the wallet copy pinned and drop the extra).
        for (int a = 0; a < _pendingResult.count; a++) {
            for (int b = a + 1; b < _pendingResult.count; b++) {
                if (strcmp(_pendingResult.items[a].image_url, _pendingResult.items[b].image_url) != 0) continue;
                _pendingResult.items[a].pinned = _pendingResult.items[a].pinned || _pendingResult.items[b].pinned;
                for (int c = b; c + 1 < _pendingResult.count; c++)
                    _pendingResult.items[c] = _pendingResult.items[c + 1];
                _pendingResult.count--;
                b--;
            }
        }
    }

    static void _bgPinlistFetchFn(void* /*pvArg*/) {
        NftScreen* self = s_instance;
        if (!self) { vTaskDelete(nullptr); return; }
        netLock();   // exclusive TLS ownership — see net_lock.h


        _pendingResult.count = 0;
        _appendPinlistItems();
        if (_pendingResult.count == 0) {
            snprintf(_pendingResult.error_msg, sizeof(_pendingResult.error_msg), "No valid pinlist entries.");
            _pendingResult.error = true;
            _pendingResult.ready = true;
            netUnlock();
            if (s_instance) s_instance->_bgTask = nullptr;   // ALWAYS clear before self-delete
            vTaskDelete(nullptr);
            return;
        }

        _refreshUsdRates();   // ETH/BTC USD rates so BTC & ETH floors rank together
        _sortByFloor();       // most valuable collection first, same as wallet mode
        _pendingResult.ready = true;
        self->_bgTask = nullptr;
        netUnlock();
        if (s_instance) s_instance->_bgTask = nullptr;   // ALWAYS clear before self-delete
        vTaskDelete(nullptr);
    }

    // ── Cross-currency floor sorting (USD) ───────────────────────────────────
    // EVM collection floors are in ETH, Ordinals floors in BTC — both stored raw
    // in floor_price_eth. Comparing the raw numbers made a 0.039 BTC NodeMonke
    // (~$4k) sort BELOW a 0.08 ETH collection (~$300). We convert each floor to
    // USD (floor × its currency's USD rate) so manual picks and wallet
    // collections rank together by real value. Rates come from DexScreener (the
    // same source the price ticker already uses), cached 10 min; if unavailable
    // we fall back to the raw value so ordering is never worse than before.
    static inline double   s_ethUsd  = 0;
    static inline double   s_btcUsd  = 0;
    static inline uint32_t s_ratesAt = 0;

    static double _dexPriceUsd(const char* tokenAddr) {
        HTTPClient http;
        http.useHTTP10(true);   // DexScreener replies chunked on HTTP/1.1
        http.begin(String(ENDPOINT_DEXSCREENER_TOKENS) + tokenAddr);
        http.setTimeout(8000);
        if (http.GET() != 200) { http.end(); return 0; }
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, http.getStream());
        http.end();
        if (err) return 0;
        // priceUsd is the price of the pair's BASE token. /tokens/{addr} returns
        // pairs where our token is EITHER base or quote — so we must only read
        // pairs where it's the BASE (else we'd read some other token's price),
        // and pick the deepest-liquidity one for a stable quote.
        double best = 0, bestLiq = -1;
        for (JsonObject pair : doc["pairs"].as<JsonArray>()) {
            const char* base = pair["baseToken"]["address"] | "";
            if (strcasecmp(base, tokenAddr) != 0) continue;
            double p   = atof(pair["priceUsd"] | "0");
            double liq = pair["liquidity"]["usd"] | 0.0;
            if (p > 0 && liq > bestLiq) { best = p; bestLiq = liq; }
        }
        return best;
    }

    static void _refreshUsdRates() {
        if (s_ratesAt != 0 && millis() - s_ratesAt < 600000UL && s_ethUsd > 0 && s_btcUsd > 0) return;
        double e = _dexPriceUsd("0x4200000000000000000000000000000000000006"); // WETH (Base)
        double b = _dexPriceUsd("0xcbB7C0000aB88B473b1f5aFd9ef808440eed33Bf"); // cbBTC (Base)
        if (e > 0) s_ethUsd = e;
        if (b > 0) s_btcUsd = b;
        if (e > 0 || b > 0) s_ratesAt = millis();
        Log.printf("NFT floor rates: ETH=$%.0f BTC=$%.0f\n", s_ethUsd, s_btcUsd);
    }

    static double _floorUsd(const NftItem& it) {
        double rate = it.floor_btc ? s_btcUsd : s_ethUsd;
        if (rate <= 0) return (double)it.floor_price_eth;   // no rate → raw fallback
        return (double)it.floor_price_eth * rate;
    }

    // Floor value desc (USD); ties broken by slug so collections stay contiguous.
    static void _sortByFloor() {
        for (int i = 0; i < _pendingResult.count - 1; i++) {
            for (int j = 0; j < _pendingResult.count - 1 - i; j++) {
                NftItem& a = _pendingResult.items[j];
                NftItem& b = _pendingResult.items[j+1];
                double ua = _floorUsd(a), ub = _floorUsd(b);
                bool swap = ua < ub || (ua == ub && strcmp(a.slug, b.slug) > 0);
                if (swap) { NftItem tmp = a; a = b; b = tmp; }
            }
        }
    }

    void _startFetch() {
        if (_fetching) return;
        _fetching = true;
        _fetchedPinlistSig = storage.getNftPinlist();   // pins merge into wallet mode too
        _pendingResult.ready = false;
        _pendingResult.error = false;
        _pendingResult.count = 0;

        // Spinner + blank grid only when there's nothing on screen yet —
        // with a snapshot showing, this is a silent background refresh.
        if (_nftCache.empty()) {
            lv_label_set_text(_loadingLabel, "Fetching your NFTs...");
            lv_obj_clear_flag(_spinner, LV_OBJ_FLAG_HIDDEN);
            for (int i = 0; i < _cellCount; i++)
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
        // Fresh decoded images → repaint ONLY the cells whose artwork changed
        // (a full 9-cell repaint per decoded image made the screen blink
        // randomly through the whole loading phase).
        if (_imgDirty) {
            _imgDirty = false;
            int gCls = _gridClass();
            for (int i = 0; i < _cellCount; i++) {
                CellWidgets& cw = _cells[i];
                if (!cw.container || cw.nftCount <= 0) continue;
                int idx = cw.nftStart + (cw.nftCurrent % cw.nftCount);
                if (idx < 0 || idx >= (int)_nftCache.size()) continue;
                const uint8_t* want = _nftCache[idx].px[gCls];
                if (want && want != cw.shownPx) _refreshCell(i);
            }
        }

        // Auto-hide the carousel dots ~2.5 s after a tap.
        for (int i = 0; i < _cellCount; i++) {
            CellWidgets& cwD = _cells[i];
            if (cwD.dotRow && cwD.dotsAt && millis() - cwD.dotsAt >= 2500) {
                cwD.dotsAt = 0;
                lv_obj_add_flag(cwD.dotRow, LV_OBJ_FLAG_HIDDEN);
            }
        }

        // Deferred edit-mode rebuild (reorder/delete/gear ran inside an event
        // callback of a widget the rebuild would delete).
        if (_rebuildReq) {
            _rebuildReq = false;
            _rebuildGrid();
        }

        // Auto-kick: whenever no worker is running but images are still
        // missing (interrupted run, grid-size change, beyond-cap items),
        // start one. ~every 500 ms; _startImageFetch() no-ops when done.
        static uint8_t kickDiv = 0;
        if (++kickDiv >= 5) {
            kickDiv = 0;
            if (!_fetching && !_imgTask) _startImageFetch();
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

        // Absorb results into cache. Decoded artwork is EXPENSIVE — carry it
        // over to the new list (matched by image URL) instead of dropping it,
        // so a periodic list refresh doesn't blank every cell and re-decode.
        std::vector<NftItem> oldCache;
        oldCache.swap(_nftCache);
        for (int i = 0; i < _pendingResult.count; i++) {
            NftItem it = _pendingResult.items[i];
            for (auto& prev : oldCache) {
                if (strcmp(prev.image_url, it.image_url) != 0) continue;
                // Same URL is NOT enough: the decoded pixels bake in the
                // composite background colour, so if the owner changed the
                // ordinal's background on the web (bg_color differs) we must
                // RE-DECODE rather than reuse the old-background bitmap — that
                // was why a background change never showed on the device.
                if (prev.bg_color != it.bg_color) continue;
                for (int c = 0; c < 3; c++) {
                    if (!prev.px[c]) continue;
                    it.px[c] = prev.px[c];    // steal the buffer
                    it.pw[c] = prev.pw[c];
                    it.ph[c] = prev.ph[c];
                    it.tried[c] = true;
                    prev.px[c] = nullptr;
                }
                break;
            }
            _nftCache.push_back(it);
        }
        for (auto& prev : oldCache)
            for (int c = 0; c < 3; c++)
                if (prev.px[c]) free(prev.px[c]);   // no longer in the wallet
        // Signature of the refreshed list: if nothing actually changed,
        // skip the full grid rebuild (it repainted all 9 cells every 30-min
        // background refresh — a periodic full-screen flicker for nothing).
        String sig;
        // Name is part of the signature: a refresh that only improves the
        // caption (e.g. resolve-ordinal finally returning "NodeMonke #9343"
        // with the floor still unknown) must NOT be skipped as "identical".
        for (auto& it2 : _nftCache) { sig += it2.image_url; sig += ':'; sig += it2.name; sig += ':'; sig += String(it2.floor_price_eth, 4); sig += ':'; sig += String((unsigned)it2.bg_color, HEX); sig += ';'; }
        bool listChanged = (sig != _lastListSig);
        _lastListSig = sig;

        _imgGen++;                                        // indices changed → stale workers abort
        _cacheTimestamp = millis();
        _snapshotOnly   = false;
        _saveSnapshot();                                  // reboots restore instantly from disk

        // Report the detected collections (ALL of them, hidden included) so
        // the web setup page can render the collections board.
        {
            String rep = "[";
            int total = (int)_nftCache.size();
            bool first = true;
            for (int i = 0; i < total; ) {
                int j = i;
                while (j < total && strcmp(_nftCache[j].slug, _nftCache[i].slug) == 0) j++;
                String nm = _nftCache[i].collection;
                nm.replace("\\", ""); nm.replace("\"", "");   // keep the JSON valid
                if (!first) rep += ',';
                first = false;
                rep += "{\"slug\":\"" + String(_nftCache[i].slug) + "\",\"name\":\"" + nm +
                       "\",\"floor\":" + String(_nftCache[i].floor_price_eth, 4) + "}";
                i = j;
            }
            rep += "]";
            if (rep != storage.getNftCollsReport()) {
                storage.setNftCollsReport(rep);
                storage.setNftCollsDirty(true);   // next heartbeat pushes it up
                Log.printf("NFT: collections report updated (%u bytes) — will push on next heartbeat\n",
                              (unsigned)rep.length());
            } else {
                Log.println("NFT: collections report unchanged");
            }
        }
        if (listChanged) _rebuildGrid();
        else             _startImageFetch();   // still top up any missing art
    }

    // ── Disk snapshot of the NFT list ────────────────────────────────────────
    // Metadata only (names, slugs, image URLs, floors) — with the compressed
    // images already on LittleFS this makes a reboot show the full gallery in
    // ~2 s with zero network, while a silent OpenSea refresh runs behind.
    struct NftSnapRec {
        char  name[64];
        char  collection[64];
        char  slug[64];
        char  image_url[256];
        float floor;
        uint8_t pinned;
        uint8_t floor_btc;
        uint32_t bg_color;
    };

    void _saveSnapshot() {
        uint32_t n = (uint32_t)min((size_t)NFT_MAX_ITEMS, _nftCache.size());
        if (n == 0) return;
        size_t sz = 4 + n * sizeof(NftSnapRec);
        uint8_t* buf = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!buf) return;
        memcpy(buf, &n, 4);
        NftSnapRec* r = (NftSnapRec*)(buf + 4);
        for (uint32_t i = 0; i < n; i++) {
            memset(&r[i], 0, sizeof(NftSnapRec));
            strncpy(r[i].name,       _nftCache[i].name,       sizeof(r[i].name) - 1);
            strncpy(r[i].collection, _nftCache[i].collection, sizeof(r[i].collection) - 1);
            strncpy(r[i].slug,       _nftCache[i].slug,       sizeof(r[i].slug) - 1);
            strncpy(r[i].image_url,  _nftCache[i].image_url,  sizeof(r[i].image_url) - 1);
            r[i].floor  = _nftCache[i].floor_price_eth;
            r[i].pinned    = _nftCache[i].pinned ? 1 : 0;
            r[i].floor_btc = _nftCache[i].floor_btc ? 1 : 0;
            r[i].bg_color  = _nftCache[i].bg_color;
        }
        diskcache::save("meta", "nft_list", buf, sz);
        free(buf);
    }

    bool _loadSnapshot() {
        size_t len = 0;
        uint8_t* buf = diskcache::loadAlloc("meta", "nft_list", &len);
        if (!buf) return false;
        uint32_t n = 0;
        if (len >= 4) memcpy(&n, buf, 4);
        if (n == 0 || n > NFT_MAX_ITEMS || len != 4 + n * sizeof(NftSnapRec)) { free(buf); return false; }
        NftSnapRec* r = (NftSnapRec*)(buf + 4);
        _nftCache.clear();
        for (uint32_t i = 0; i < n; i++) {
            NftItem it;
            strncpy(it.name,       r[i].name,       sizeof(it.name) - 1);
            strncpy(it.collection, r[i].collection, sizeof(it.collection) - 1);
            strncpy(it.slug,       r[i].slug,       sizeof(it.slug) - 1);
            strncpy(it.image_url,  r[i].image_url,  sizeof(it.image_url) - 1);
            it.floor_price_eth = r[i].floor;
            it.pinned          = r[i].pinned != 0;
            it.floor_btc       = r[i].floor_btc != 0;
            it.bg_color        = r[i].bg_color;
            _nftCache.push_back(it);
        }
        free(buf);
        _snapshotOnly = true;   // display now, but still refresh from OpenSea

        // Preload persisted DECODED covers (classes 1+2): the very first grid
        // paint after boot then shows real artwork with zero decode work.
        _refreshListBufs();
        int loaded = 0;
        for (int i = 0; i < (int)_nftCache.size(); i++) {
            for (int cls = 1; cls <= 2; cls++) {
                if (!_entitledSlot(cls, i)) continue;
                size_t blobLen = 0;
                uint8_t* blob = diskcache::loadAlloc("dec", _decKey(_nftCache[i].image_url, cls, _nftCache[i].bg_color).c_str(), &blobLen);
                if (!blob) continue;
                uint16_t w = 0, h = 0;
                if (blobLen > 4) { memcpy(&w, blob, 2); memcpy(&h, blob + 2, 2); }
                if (w > 0 && h > 0 && blobLen == 4 + (size_t)w * h * 2) {
                    uint8_t* px = (uint8_t*)heap_caps_malloc((size_t)w * h * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                    if (px) {
                        memcpy(px, blob + 4, (size_t)w * h * 2);
                        _nftCache[i].px[cls] = px;
                        _nftCache[i].pw[cls] = w;
                        _nftCache[i].ph[cls] = h;
                        _nftCache[i].tried[cls] = true;
                        loaded++;
                    }
                }
                free(blob);
            }
        }
        Log.printf("NFT: %u items + %d decoded covers restored from disk\n",
                      (unsigned)n, loaded);
        return true;
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

        // On a grid-size change: redirect the worker (generation bump) and
        // purge slots nobody is entitled to anymore — the current grid keeps
        // everything, other grids keep only their cell covers. This bounds
        // RAM while covers stay resident for instant switching.
        if (_decodedForGrid != _gridSize) {
            _decodedForGrid = _gridSize;
            _imgGen++;
            for (int i = 0; i < (int)_nftCache.size(); i++)
                for (int c = 0; c < 3; c++)
                    if (_nftCache[i].px[c] && !_entitledSlot(c, i))
                        _nftCache[i].freeSlot(c);
        }

        // ONE COLLECTION PER CELL (2x2 and 3x3): the cache is sorted by floor
        // price desc with same-collection items contiguous, so cell 0 gets the
        // most valuable collection, cell 1 the next, etc. Each cell's carousel
        // cycles through that collection's NFTs. 1x1 = the whole wallet in one
        // carousel. Fewer collections than cells → fewer cells (no repeats).
        int n = _gridSize;                   // 1, 4, or 9
        int side = (n == 1) ? 1 : (n == 4 ? 2 : 3);
        lv_coord_t cellW = (lv_coord_t)((480 - 4 - (side - 1) * 4) / side);
        lv_coord_t cellH = (lv_coord_t)((NFT_GRID_H - 4 - (side - 1) * 4) / side);

        _refreshListBufs();   // pick up manual order + hidden list from NVS
        _recomputeEntitled();
        int total = (int)_nftCache.size();
        int gStart[9], gCount[9], groups = 0;
        if (n == 1) {
            gStart[0] = 0; gCount[0] = total; groups = 1;
        } else {
            NftGrp* g = (NftGrp*)heap_caps_malloc(sizeof(NftGrp) * NFT_MAX_COLLECTIONS,
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!g) g = (NftGrp*)malloc(sizeof(NftGrp) * NFT_MAX_COLLECTIONS);
            if (g) {
                int gn = _buildGroups(g, NFT_MAX_COLLECTIONS, n);
                for (int k = 0; k < gn && groups < n; k++) {
                    gStart[groups] = g[k].start;
                    gCount[groups] = g[k].count;
                    groups++;
                }
                free(g);
            }
        }

        // Diagnostic (visible at /logs): the exact grid group order + each
        // collection's floor in USD and its currency, so a "wrong cell / repeat"
        // shows precisely which group landed where and why.
        {
            String dbg = "NFT grid: cache=" + String(total) + " groups=" + String(groups) + " | ";
            for (int k = 0; k < groups; k++) {
                const NftItem& gi = _nftCache[gStart[k]];
                dbg += String(k) + ":" + gi.slug + "(x" + String(gCount[k])
                     + ",usd=" + String((long)_floorUsd(gi))
                     + (gi.floor_btc ? ",BTC" : ",ETH")
                     + (gi.pinned ? ",pin" : "") + ") ";
            }
            Log.println(dbg);
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
        cw.cellW = w;
        cw.cellH = h;
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

        // Use the size recorded at build time: lv_obj_get_width() returns 0
        // until the first layout pass, which made build-time captions compute
        // a NEGATIVE label width → invisible names until any later repaint.
        lv_coord_t cw_w = cw.cellW - 8;   // minus container padding
        lv_coord_t cw_h = cw.cellH - 8;

        // Image placeholder (full-cell canvas colored by tier)
        // If img_pixels is non-null (JPEG decoded), display as lv_img instead.
        // Pick the decoded slot for the CURRENT grid; if it isn't ready yet,
        // stand in with another class's bitmap (covers keep all three sizes
        // resident) so a grid switch always paints something immediately.
        int gCls = _gridClass();
        const uint8_t* px = item.px[gCls];
        uint16_t pw = item.pw[gCls], ph = item.ph[gCls];
        if (!px) {
            static const int PREF[3][2] = { {1, 2}, {2, 0}, {1, 0} };  // closest size first
            for (int k = 0; k < 2 && !px; k++) {
                int c = PREF[gCls][k];
                if (item.px[c]) { px = item.px[c]; pw = item.pw[c]; ph = item.ph[c]; }
            }
        }

        cw.shownPx = px;   // may be a stand-in from another grid class, or null
        if (px && pw > 0) {
            // Decoded image available (RGB565 in PSRAM). One dsc PER CELL —
            // a shared static one made every cell display the same image.
            cw.imgDsc.header.always_zero = 0;
            cw.imgDsc.header.w           = pw;
            cw.imgDsc.header.h           = ph;
            cw.imgDsc.header.cf          = LV_IMG_CF_TRUE_COLOR;
            cw.imgDsc.data_size          = (uint32_t)pw * ph * 2;
            cw.imgDsc.data               = px;

            // ORIGINAL SIZE, 1:1: the bitmap was decoded to fit this exact
            // cell (contain), so it blits untransformed — centered, black
            // bands where the aspect doesn't match. NO lv_img zoom here:
            // runtime transforms re-scale every frame and made the whole
            // screen shimmer ("interference").
            lv_obj_t* imgObj = lv_img_create(cw.container);
            lv_img_set_src(imgObj, &cw.imgDsc);
            // Centered within the ART AREA (cell minus the caption band —
            // or the whole cell when the Data captions are toggled off).
            lv_coord_t artH = storage.getNftShowData() ? (lv_coord_t)(cw_h - NFT_CAPTION_H) : cw_h;
            lv_coord_t yOff = (artH > (lv_coord_t)ph) ? (lv_coord_t)((artH - ph) / 2) : 0;
            lv_obj_align(imgObj, LV_ALIGN_TOP_MID, 0, yOff);
            lv_obj_clear_flag(imgObj, LV_OBJ_FLAG_CLICKABLE);   // taps advance the carousel
        } else {
            // No decoded image — show a coloured tile with a subtle grid icon
            lv_obj_t* placeholder = lv_obj_create(cw.container);
            lv_obj_set_size(placeholder, cw_w, cw_h - NFT_CAPTION_H);
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

        // ── Edit-mode overlay: [◀] [✕] [▶] reorder/delete controls ──
        if (_editMode && _gridSize != 1) {
            static const char* glyphs[3] = { LV_SYMBOL_LEFT, LV_SYMBOL_CLOSE, LV_SYMBOL_RIGHT };
            static lv_event_cb_t cbs[3]  = { _onCellMoveLeft, _onCellDelete, _onCellMoveRight };
            for (int b = 0; b < 3; b++) {
                lv_obj_t* btn = lv_btn_create(cw.container);
                lv_obj_set_size(btn, 34, 30);
                lv_obj_set_style_bg_color(btn, lv_color_hex(0x000000), 0);
                lv_obj_set_style_bg_opa(btn, LV_OPA_70, 0);
                lv_obj_set_style_border_color(btn, lv_color_hex(0x3a3a42), 0);
                lv_obj_set_style_border_width(btn, 1, 0);
                lv_obj_set_style_radius(btn, 6, 0);
                lv_obj_align(btn, LV_ALIGN_CENTER, (lv_coord_t)((b - 1) * 42), 0);
                lv_obj_set_user_data(btn, (void*)(intptr_t)idx);
                lv_obj_add_event_cb(btn, cbs[b], LV_EVENT_CLICKED, nullptr);
                lv_obj_t* l = lv_label_create(btn);
                lv_label_set_text(l, glyphs[b]);
                lv_obj_set_style_text_color(l, lv_color_hex(b == 1 ? 0xff4d4d : NFT_CLR_TEXT), 0);
                lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
                lv_obj_center(l);
            }
        }

        if (!storage.getNftShowData()) return;   // "Data" toggle off → no captions

        // ── Caption band (dedicated dark strip UNDER the artwork): name on
        // the left, floor on the right, SAME baseline row — readable always,
        // never overlaid on the art.
        // Name: smaller face on the dense 3x3 grid. Long names stay on ONE
        // line and marquee sideways slowly (circular scroll) so the full
        // name — id included — is readable.
        const lv_font_t* nameFont = (_gridSize == 9) ? &lv_font_montserrat_8
                                                     : &lv_font_montserrat_10;
        const char* nameTxt = item.name[0] ? item.name : item.collection;
        lv_coord_t nameAvail = (lv_coord_t)(cw_w - 52);   // floor takes ~48 px
        if (nameAvail < 20) nameAvail = 20;

        cw.nameLbl = lv_label_create(cw.container);
        lv_label_set_text(cw.nameLbl, nameTxt);
        lv_obj_set_width(cw.nameLbl, nameAvail);
        lv_obj_set_style_text_font(cw.nameLbl, nameFont, 0);
        lv_obj_set_style_text_color(cw.nameLbl, lv_color_hex(NFT_CLR_TEXT), 0);
        lv_point_t tsz;
        lv_txt_get_size(&tsz, nameTxt, nameFont, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (tsz.x > nameAvail) {
            lv_label_set_long_mode(cw.nameLbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_obj_set_style_anim_speed(cw.nameLbl, 12, 0);   // ~12 px/s — slow, readable
        } else {
            lv_label_set_long_mode(cw.nameLbl, LV_LABEL_LONG_CLIP);
        }
        lv_obj_align(cw.nameLbl, LV_ALIGN_BOTTOM_LEFT, 0, -2);

        if (item.floor_price_eth > 0) {
            cw.floorLbl = lv_label_create(cw.container);
            char floorBuf[24];
            // Two decimals to save caption space (three below 0.01 so dust
            // floors don't render as 0.00). Ξ for ETH, ₿ for Ordinals.
            const char* sym = item.floor_btc ? "\xE2\x82\xBF" : "\xCE\x9E";
            if (item.floor_price_eth < 0.01f)
                snprintf(floorBuf, sizeof(floorBuf), "%.3f %s", item.floor_price_eth, sym);
            else
                snprintf(floorBuf, sizeof(floorBuf), "%.2f %s", item.floor_price_eth, sym);
            lv_label_set_text(cw.floorLbl, floorBuf);
            lv_obj_set_style_text_font(cw.floorLbl, ethXiFont10(), 0);
            // Same colour as the name for EVERY cell — the old per-tier
            // gold/blue/green colouring made the caption band look random.
            lv_obj_set_style_text_color(cw.floorLbl, lv_color_hex(NFT_CLR_TEXT), 0);
            lv_obj_align(cw.floorLbl, LV_ALIGN_BOTTOM_RIGHT, 0, -2);   // same row as the name
        }

        // Carousel dots — bottom edge of the ART area. Hidden by default
        // (they were visual noise); they appear for a moment after a tap.
        bool dotsVisible = cw.dotsAt && (millis() - cw.dotsAt < 2500);
        if (dotsVisible && cw.nftCount > 1 && cw_h > 40) {
            cw.dotRow = lv_obj_create(cw.container);
            int dots = min(cw.nftCount, 5);
            lv_coord_t dotRowW = (lv_coord_t)(dots * 10);
            lv_obj_set_size(cw.dotRow, dotRowW, 8);
            lv_obj_align(cw.dotRow, LV_ALIGN_BOTTOM_MID, 0, -(NFT_CAPTION_H + 2));
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

    // ── Edit-mode actions (persisted to NVS; visual rebuild is DEFERRED via
    // _rebuildReq — these run inside the tapped button's own event) ──
    NftGrp* _allocGroups() {
        NftGrp* g = (NftGrp*)heap_caps_malloc(sizeof(NftGrp) * NFT_MAX_COLLECTIONS,
                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        return g ? g : (NftGrp*)malloc(sizeof(NftGrp) * NFT_MAX_COLLECTIONS);
    }

    void _moveCollection(int cellIdx, int dir) {
        NftGrp* g = _allocGroups();
        if (!g) return;
        _refreshListBufs();
        int n = _buildGroups(g, NFT_MAX_COLLECTIONS, _classCells(_gridClass()));
        int to = cellIdx + dir;
        if (cellIdx < 0 || cellIdx >= n || to < 0 || to >= n) { free(g); return; }
        NftGrp t = g[cellIdx]; g[cellIdx] = g[to]; g[to] = t;
        String ord;
        for (int k = 0; k < n; k++) { if (k) ord += ','; ord += g[k].slug; }
        free(g);
        storage.setNftCollOrder(ord);
        storage.setNftListsDirty(true);   // sync to the web on next heartbeat
        _rebuildReq = true;
    }

    void _deleteCollection(int cellIdx) {
        NftGrp* g = _allocGroups();
        if (!g) return;
        _refreshListBufs();
        int n = _buildGroups(g, NFT_MAX_COLLECTIONS, _classCells(_gridClass()));
        if (cellIdx < 0 || cellIdx >= n) { free(g); return; }
        String hid = storage.getNftHidden();
        if (hid.length()) hid += ',';
        hid += g[cellIdx].slug;
        free(g);
        storage.setNftHidden(hid);
        storage.setNftListsDirty(true);   // sync to the web on next heartbeat
        // The freed cell auto-fills with the NEXT collection by floor on the
        // deferred rebuild (the group walk simply skips hidden slugs).
        _rebuildReq = true;
    }

    static void _onCellMoveLeft(lv_event_t* e)  {
        if (s_instance) s_instance->_moveCollection((int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(e)), -1);
    }
    static void _onCellMoveRight(lv_event_t* e) {
        if (s_instance) s_instance->_moveCollection((int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(e)), +1);
    }
    static void _onCellDelete(lv_event_t* e)    {
        if (s_instance) s_instance->_deleteCollection((int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(e)));
    }

    void _advanceCell(int idx) {
        CellWidgets& cw = _cells[idx];
        if (cw.nftCount <= 1) return;
        cw.nftCurrent = (cw.nftCurrent + 1) % cw.nftCount;
        cw.dotsAt = millis();   // reveal the position dots briefly
        _refreshCell(idx);
        // The first NFT_IMG_MAX_FETCH images load eagerly; anything beyond
        // that gets fetched the moment the carousel reaches it.
        int nftIdx = cw.nftStart + (cw.nftCurrent % cw.nftCount);
        int gCls = _gridClass();
        if (nftIdx < (int)_nftCache.size() && !_nftCache[nftIdx].px[gCls]
            && !_nftCache[nftIdx].tried[gCls])
            _startImageFetch();
    }

    // ── Slideshow tick ────────────────────────────────────────────────────────

    void _onSlideshowTick() {
        uint8_t slideSecs = storage.getNftSlideshowSecs();
        if (slideSecs == 0 || !storage.getNftCarousel()) return;
        if (_cellCount == 0) return;

        if (_slideshowCount > 0) { _slideshowCount--; return; }
        _slideshowCount = slideSecs;

        // Advance ONE cell per interval, round-robin, and only to an item
        // whose artwork is ALREADY decoded. The old "advance all 9 cells at
        // once, placeholders included" produced a burst of full-cell repaints
        // plus placeholder→art double repaints — the 3×3 flicker.
        static uint8_t rr = 0;
        int gCls = _gridClass();
        for (int scan = 0; scan < _cellCount; scan++) {
            int i = (rr + scan) % _cellCount;
            CellWidgets& cw = _cells[i];
            if (!cw.container || cw.nftCount <= 1) continue;
            for (int step = 1; step < cw.nftCount; step++) {
                int cand = cw.nftStart + ((cw.nftCurrent + step) % cw.nftCount);
                if (cand >= 0 && cand < (int)_nftCache.size() && _nftCache[cand].px[gCls]) {
                    cw.nftCurrent = (cw.nftCurrent + step) % cw.nftCount;
                    _refreshCell(i);
                    rr = (uint8_t)((i + 1) % _cellCount);
                    return;   // one repaint per tick — calm screen
                }
            }
        }
        if (_cellCount > 0) rr = (uint8_t)((rr + 1) % _cellCount);
    }

    // ── Carousel setting ──────────────────────────────────────────────────────

    // Green + full opacity when on; muted grey when off.
    void _refreshCarouselLabel() {
        if (!_carouselSwitch) return;
        bool on = storage.getNftCarousel();
        lv_obj_set_style_text_color(_carouselSwitch,
            lv_color_hex(on ? NFT_CLR_ACTIVE : NFT_CLR_MUTED), 0);
        lv_obj_set_style_text_opa(_carouselSwitch, on ? LV_OPA_COVER : LV_OPA_70, 0);
    }

    void _refreshDataLabel() {
        if (!_dataSwitch) return;
        bool on = storage.getNftShowData();
        lv_obj_set_style_text_color(_dataSwitch, lv_color_hex(on ? NFT_CLR_ACTIVE : NFT_CLR_MUTED), 0);
        lv_obj_set_style_text_opa(_dataSwitch, on ? LV_OPA_COVER : LV_OPA_70, 0);
    }

    void _applyCarouselSetting(bool /*on*/) {
        // The carousel flag only gates the slideshow tick (reads storage
        // directly) and the position dots — refresh the existing cells in
        // place. The old full _rebuildGrid() from inside the toggle's event
        // callback tore down and rebuilt every cell for nothing (and was
        // reported as "grid goes blank when toggling carousel off").
        for (int i = 0; i < _cellCount; i++) _refreshCell(i);
        _slideshowCount = storage.getNftSlideshowSecs();
    }

    // ── Size selector buttons ─────────────────────────────────────────────────

    lv_obj_t* _makeSizeBtn(lv_obj_t* parent, const char* label) {
        lv_obj_t* btn = lv_btn_create(parent);
        lv_obj_set_size(btn, 44, 20);
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
        lv_obj_set_style_bg_color(btn, lv_color_hex(active ? 0x26262c : 0x141414), 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(active ? 0x8a8a92 : NFT_CLR_BORDER), 0);
        lv_obj_t* lbl = lv_obj_get_child(btn, 0);
        if (lbl) lv_obj_set_style_text_color(lbl, lv_color_hex(active ? NFT_CLR_ACTIVE : NFT_CLR_MUTED), 0);
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
    // the CURRENT grid's cell size (contain, aspect preserved) so cells blit
    // it 1:1. NO runtime lv_img zoom: transformed draws re-scale every frame
    // on the RGB panel, which caused visible shimmer/"interference". When the
    // grid size changes, pixels are re-decoded from the disk cache (fast).
    #define NFT_IMG_MAX_FETCH 12   // per run — carousel taps re-trigger for the rest

    // Decoded-bitmap disk key: url + grid class (dims embedded in the blob).
    // The BACKGROUND colour is part of the key: decoded bitmaps are composited
    // onto it, so changing the colour (web picker) must miss the old entry —
    // otherwise the pre-recolour bitmap would be served from flash forever.
    // bg 0 (black, the default) keeps the legacy key so old caches stay warm.
    static String _decKey(const char* url, int cls, uint32_t bg = 0) {
        String k = String(url) + "|dec" + String(cls);
        if (bg != 0) { char b[10]; snprintf(b, sizeof(b), "|%06x", (unsigned)bg); k += b; }
        return k;
    }

    static String _urlEncode(const char* src) {
        String out;
        for (const char* p = src; *p; p++) {
            char c = *p;
            if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
                out += c;
            } else {
                char b[4];
                snprintf(b, sizeof(b), "%%%02X", (unsigned char)c);
                out += b;
            }
        }
        return out;
    }

    // Grid class: 0 = 1x1, 1 = 2x2, 2 = 3x3.
    int _gridClass() const { return _gridSize == 1 ? 0 : (_gridSize == 4 ? 1 : 2); }
    static int _classCells(int cls) { return cls == 0 ? 1 : (cls == 1 ? 4 : 9); }

    // Inner drawable size of one cell for a grid class (matches
    // _rebuildGrid's math, minus the container's 4 px padding).
    static void _cellInnerFor(int cls, int& wOut, int& hOut) {
        int sd = cls + 1;
        wOut = (480 - 4 - (sd - 1) * 4) / sd - 8;
        hOut = (NFT_GRID_H - 4 - (sd - 1) * 4) / sd - 8 - NFT_CAPTION_H;   // artwork area above the caption band
    }

    static bool _listHas(const char* csv, const char* slug) {
        const char* p = csv;
        size_t sl = strlen(slug);
        while (*p) {
            const char* c = strchr(p, ',');
            size_t len = c ? (size_t)(c - p) : strlen(p);
            if (len == sl && strncmp(p, slug, sl) == 0) return true;
            if (!c) break;
            p = c + 1;
        }
        return false;
    }

    // Effective collection list: contiguous runs of the floor-sorted cache,
    // minus hidden ("deleted") collections, re-ordered by the user's manual
    // order (unlisted collections keep their floor-desc position after the
    // listed ones' relative order is applied). Deleting a collection simply
    // lets the NEXT collection by floor take the freed cell.
    // `visCells` = how many cells the caller will show: every group holding a
    // MANUAL PICK is promoted into that window (displacing the lowest wallet
    // collections), then the whole visible set keeps floor-desc order.
    int _buildGroups(NftGrp* g, int maxG, int visCells = 9) {
        int total = (int)_nftCache.size();
        int n = 0;
        for (int i = 0; i < total && n < maxG; ) {
            int j = i;
            while (j < total && strcmp(_nftCache[j].slug, _nftCache[i].slug) == 0) j++;
            if (!_listHas(_hidBuf, _nftCache[i].slug)) {
                g[n].start = i;
                g[n].count = j - i;
                strncpy(g[n].slug, _nftCache[i].slug, sizeof(g[n].slug) - 1);
                g[n].slug[sizeof(g[n].slug) - 1] = 0;
                n++;
            }
            i = j;
        }

        // DEFAULT ORDER = collection floor value (USD), most valuable first.
        // _nftCache is already USD-sorted, but a stale custom order or the
        // pinned-promotion used to strand a high-value pick (e.g. a BTC Ordinal)
        // last — so we explicitly floor-sort the groups here. A manual reorder
        // via the on-device arrows (_ordBuf, applied just below) still wins.
        for (int a = 0; a < n - 1; a++)
            for (int b = 0; b < n - 1 - a; b++) {
                double fa = _floorUsd(_nftCache[g[b].start]);
                double fb = _floorUsd(_nftCache[g[b + 1].start]);
                if (fa < fb) { NftGrp t = g[b]; g[b] = g[b + 1]; g[b + 1] = t; }
            }

        if (_ordBuf[0]) {
            // In-place stable reorder (NO temp array: these run on the LVGL
            // task's 8 KB stack — stack-allocating 2× NftGrp[30] here was a
            // stack overflow that corrupted whatever ran next, crashing in
            // innocent NVS reads with wild addresses).
            int placed = 0;
            const char* p = _ordBuf;
            while (*p && placed < n) {
                const char* c = strchr(p, ',');
                size_t len = c ? (size_t)(c - p) : strlen(p);
                for (int k = placed; k < n; k++) {
                    if (strlen(g[k].slug) == len && strncmp(g[k].slug, p, len) == 0) {
                        NftGrp moved = g[k];
                        for (int w = k; w > placed; w--) g[w] = g[w - 1];
                        g[placed++] = moved;
                        break;
                    }
                }
                if (!c) break;
                p = c + 1;
            }
        }

        // Promote pinned groups into the visible window (manual picks must
        // ALWAYS make the grid). Scan beyond the window; each pinned group
        // found outside is moved to the last in-window position, pushing the
        // lowest unpinned collection out. Relative floor order is preserved.
        if (visCells > 0 && n > visCells) {
            for (int k = visCells; k < n; k++) {
                bool grpPinned = false;
                for (int q = 0; q < g[k].count; q++)
                    if (_nftCache[g[k].start + q].pinned) { grpPinned = true; break; }
                if (!grpPinned) continue;
                // find the lowest unpinned group inside the window to evict
                int evict = -1;
                for (int w = visCells - 1; w >= 0; w--) {
                    bool wPinned = false;
                    for (int q = 0; q < g[w].count; q++)
                        if (_nftCache[g[w].start + q].pinned) { wPinned = true; break; }
                    if (!wPinned) { evict = w; break; }
                }
                if (evict < 0) break;   // window is all picks already
                NftGrp moved = g[k];
                for (int w = k; w > evict; w--) g[w] = g[w - 1];
                g[evict] = moved;
            }
        }
        return n;
    }

    void _refreshListBufs() {
        String o = storage.getNftCollOrder();
        String h = storage.getNftHidden();
        strncpy(_ordBuf, o.c_str(), sizeof(_ordBuf) - 1);
        strncpy(_hidBuf, h.c_str(), sizeof(_hidBuf) - 1);
    }

    // Whether item `idx` is entitled to keep/get a decoded slot for class
    // `cls`: everything for the CURRENT grid; for other grids only the cell
    // covers (first item of each collection group, up to that grid's cell
    // count) — those are what make grid switching feel instant.
    // Precomputed per-item entitlement bits (bit c = item is a visible cell
    // cover for grid class c). Refreshed by _recomputeEntitled() on every
    // rebuild; read lock-free from the img worker and the poll timer.
    uint8_t _entMask[NFT_MAX_ITEMS] = {};

    void _recomputeEntitled() {
        memset(_entMask, 0, sizeof(_entMask));
        // Heap, not stack: this runs inside deep LVGL event stacks.
        NftGrp* g = (NftGrp*)heap_caps_malloc(sizeof(NftGrp) * NFT_MAX_COLLECTIONS,
                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!g) g = (NftGrp*)malloc(sizeof(NftGrp) * NFT_MAX_COLLECTIONS);
        if (!g) return;
        for (int c = 0; c < 3; c++) {
            int cells = _classCells(c);
            int n = _buildGroups(g, NFT_MAX_COLLECTIONS, cells);
            for (int k = 0; k < n && k < cells; k++)
                if (g[k].start >= 0 && g[k].start < NFT_MAX_ITEMS)
                    _entMask[g[k].start] |= (uint8_t)(1u << c);
        }
        free(g);
    }

    bool _entitledSlot(int cls, int idx) {
        if (cls == _gridClass()) return true;
        return idx >= 0 && idx < NFT_MAX_ITEMS && (_entMask[idx] & (1u << cls));
    }

    void _startImageFetch() {
        if (_imgTask || _nftCache.empty()) return;
        // Don't spawn (and log) a no-op worker when every entitled slot is
        // already decoded or attempted.
        bool anyPending = false;
        for (int i = 0; i < (int)_nftCache.size() && !anyPending; i++) {
            NftItem& it = _nftCache[i];
            if (!it.image_url[0]) continue;
            for (int c = 0; c < 3; c++)
                if (!it.px[c] && !it.tried[c] && _entitledSlot(c, i)) { anyPending = true; break; }
        }
        if (!anyPending) return;
        xTaskCreatePinnedToCore(_bgImgFetchFn, "nft_img", 16384, nullptr, 1, (TaskHandle_t*)&_imgTask, 0);
    }

    static void _bgImgFetchFn(void* /*pvArg*/) {
        NftScreen* self = s_instance;
        if (!self) { vTaskDelete(nullptr); return; }
        netLock();   // exclusive TLS ownership — see net_lock.h

        uint16_t myGen = self->_imgGen;   // snapshot: invalidation aborts this run
        int g = self->_gridClass();

        // Priority order for the CURRENT grid: the visible NFT of each cell,
        // then everything else.
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
        bool aborted = false;

        // Phase 1: current grid class, in priority order.
        // Phase 2: pre-decode the cell COVERS for the OTHER two grid classes
        // (from the disk cache when possible) so grid switches paint instantly.
        for (int phase = 0; phase < 2 && !aborted; phase++) {
            int cls0 = 0, cls1 = 2;
            if (phase == 0) { cls0 = g; cls1 = g; }
            for (int cls = cls0; cls <= cls1 && !aborted; cls++) {
                if (phase == 1 && cls == g) continue;
                int boxW, boxH;
                _cellInnerFor(cls, boxW, boxH);
                for (int k = 0; k < (phase == 0 ? on : total) && !aborted; k++) {
                    int idx = (phase == 0) ? order[k] : k;
                    NftItem& it = self->_nftCache[idx];
                    if (it.px[cls] || it.tried[cls] || !it.image_url[0]) continue;
                    if (phase == 1 && !self->_entitledSlot(cls, idx)) continue;
                    it.tried[cls] = true;

                    // seadn.io is imgix-backed: ask for a small variant. Fall
                    // back to the raw URL if the sized variant fails.
                    String base = it.image_url;
                    int q = base.indexOf('?');
                    if (q >= 0) base = base.substring(0, q);
                    bool fromDisk = diskcache::has("img", it.image_url);
                    uint16_t w = 0, h = 0;
                    uint8_t* px = imgdec::fetchRgb565((base + "?w=512&auto=format").c_str(),
                                                      boxW, boxH, it.name, it.image_url, &w, &h, it.bg_color);
                    if (!px) px = imgdec::fetchRgb565(it.image_url, boxW, boxH, it.name, it.image_url, &w, &h, it.bg_color);
                    if (!px) {
                        // Last resort for formats the chip can't decode (SVG —
                        // e.g. on-chain Checks —, webp, gif): the wsrv.nl image
                        // proxy rasterizes/transcodes ANYTHING to JPEG. Only
                        // reached when both direct attempts failed, and the
                        // JPEG result lands in the disk cache like any other.
                        String prox = "https://wsrv.nl/?url=" + _urlEncode(it.image_url) + "&w=512&output=jpg";
                        px = imgdec::fetchRgb565(prox.c_str(), boxW, boxH, it.name, it.image_url, &w, &h, it.bg_color);
                    }

                    if (myGen != self->_imgGen) {
                        // Grid/cache changed mid-decode: wrong size or wrong
                        // item. Drop it; the poll timer's auto-kick relaunches
                        // us with fresh parameters.
                        if (px) free(px);
                        it.tried[cls] = false;
                        aborted = true;
                        break;
                    }
                    if (px) {
                        it.px[cls] = px;
                        it.pw[cls] = w;
                        it.ph[cls] = h;
                        if (cls == g) self->_imgDirty = true;   // poll timer repaints
                        fetched++;
                        // Persist DECODED cover bitmaps (2x2/3x3 classes are
                        // small: ~20-50 KB) so a reboot paints instantly with
                        // zero decode. 1x1 (~200 KB) isn't worth the flash.
                        if (cls >= 1 && self->_entitledSlot(cls, idx)) {
                            size_t blobLen = 4 + (size_t)w * h * 2;
                            uint8_t* blob = (uint8_t*)heap_caps_malloc(blobLen, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                            if (blob) {
                                memcpy(blob, &w, 2);
                                memcpy(blob + 2, &h, 2);
                                memcpy(blob + 4, px, (size_t)w * h * 2);
                                diskcache::save("dec", _decKey(it.image_url, cls, it.bg_color).c_str(), blob, blobLen);
                                free(blob);
                            }
                        }
                    }
                    // Rate-limit only real CDN hits — disk-cache re-decodes fly.
                    if (!fromDisk) delay(NFT_RATELIMIT_DELAY_MS);
                }
            }
        }
        Log.printf("NFT img worker: %d slots decoded%s\n", fetched, aborted ? " (aborted: params changed)" : "");

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

        // ── Step 1: Fetch the NFT list from OpenSea, PAGINATED ──────────────
        // OpenSea returns NFTs by acquisition date (newest first), so a single
        // page from a wallet that just bought 24 of one collection contains
        // ONLY that collection (this is exactly what happened: 24× Penimals,
        // one slug, every grid cell the same). Walk the `next` cursor across
        // several pages and cap how many we keep per collection, so every
        // collection in the wallet gets represented.
        struct RawNft {
            char name[64]       = {};
            char slug[64]       = {};
            char image_url[256] = {};
        };
        static RawNft rawNfts[NFT_MAX_ITEMS];
        static char slugList[NFT_MAX_COLLECTIONS][64];
        static uint8_t perSlug[NFT_MAX_COLLECTIONS];
        int rawCount  = 0;
        int slugCount = 0;
        memset(perSlug, 0, sizeof(perSlug));
        const int MAX_PER_COLLECTION = 6;   // grid carousels don't need more
        const int MAX_PAGES          = 5;   // up to ~250 NFTs scanned

        String nextCursor = "";
        int    code       = -1;
        for (int page = 0; page < MAX_PAGES && rawCount < NFT_MAX_ITEMS; page++) {
            String nftsUrl = String(ENDPOINT_OPENSEA_BASE) +
                             "/chain/" + NFT_OPENSEA_CHAIN +
                             "/account/" + wallet +
                             "/nfts?limit=50";
            if (nextCursor.length()) nftsUrl += "&next=" + nextCursor;

            // Up to 3 attempts on the FIRST page (TLS handshakes can fail
            // while internal RAM is momentarily fragmented); later pages are
            // best-effort — whatever was gathered so far still renders.
            HTTPClient http;
            code = -1;
            int attempts = (page == 0) ? 3 : 1;
            for (int attempt = 0; attempt < attempts; attempt++) {
                if (attempt > 0) {
                    Log.printf("NFT fetch retry %d (prev code %d)\n", attempt, code);
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
                if (page > 0) break;   // keep earlier pages' items
                if (code == 401 || code == 403) {
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

            // Filtered parse: only the four fields we use + the page cursor —
            // keeps the JsonDocument small even at limit=50.
            JsonDocument filter;
            filter["next"] = true;
            filter["nfts"][0]["collection"]        = true;
            filter["nfts"][0]["name"]              = true;
            filter["nfts"][0]["display_image_url"] = true;
            filter["nfts"][0]["image_url"]         = true;
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, http.getStream(),
                                                       DeserializationOption::Filter(filter));
            http.end();
            if (err) {
                if (page > 0) break;
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
            for (JsonObject nft : nfts) {
                if (rawCount >= NFT_MAX_ITEMS) break;

                const char* slug   = nft["collection"] | "";
                const char* name   = nft["name"]       | "";
                // display_image_url is OpenSea's pre-resized variant (~500px) —
                // ideal for on-device decode. NEVER metadata_url (that's JSON).
                const char* imgUrl = nft["display_image_url"] | nft["image_url"] | "";

                if (!slug[0])  continue;   // no collection slug
                if (!imgUrl[0]) continue;  // no image

                // Find/register the collection; cap items kept per collection.
                int si = -1;
                for (int k = 0; k < slugCount; k++)
                    if (strcmp(slugList[k], slug) == 0) { si = k; break; }
                if (si < 0) {
                    if (slugCount >= NFT_MAX_COLLECTIONS) continue;
                    si = slugCount++;
                    strncpy(slugList[si], slug, 63);
                }
                if (perSlug[si] >= MAX_PER_COLLECTION) continue;
                perSlug[si]++;

                strncpy(rawNfts[rawCount].name,      name,   sizeof(rawNfts[0].name)-1);
                strncpy(rawNfts[rawCount].slug,      slug,   sizeof(rawNfts[0].slug)-1);
                strncpy(rawNfts[rawCount].image_url, imgUrl, sizeof(rawNfts[0].image_url)-1);
                rawCount++;
            }

            const char* nx = doc["next"] | "";
            Log.printf("NFT: page %d -> %d kept, %d collections so far%s\n",
                          page + 1, rawCount, slugCount, nx[0] ? "" : " (last page)");
            if (!nx[0]) break;
            nextCursor = nx;
            delay(NFT_RATELIMIT_DELAY_MS);
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
            Log.printf("NFT stats[%s] HTTP %d floor=%.4f\n", slugList[si], sc, floorPrices[si]);

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
            item = NftItem{};   // reset reused slot (no leaked floor_btc/bg_color)
            strncpy(item.name,       rawNfts[ri].name,      sizeof(item.name)-1);
            strncpy(item.slug,       rawNfts[ri].slug,      sizeof(item.slug)-1);
            strncpy(item.collection, colName[0] ? colName : rawNfts[ri].slug, sizeof(item.collection)-1);
            strncpy(item.image_url,  rawNfts[ri].image_url, sizeof(item.image_url)-1);
            item.floor_price_eth = fp;
        }

        // Sort by floor price descending, tie-broken by slug so items of the
        // SAME collection stay contiguous — the grid walks these as groups
        // (one collection per cell). Bubble sort — small N.
        // Manual picks MERGE into the wallet scan (dedup by image URL) — a
        // pick always earns a grid cell via the pinned-promotion in
        // _buildGroups(), displacing the lowest wallet collection if needed.
        _appendPinlistItems();

        _refreshUsdRates();   // ETH/BTC USD rates so BTC & ETH floors rank together
        _sortByFloor();
        Log.printf("NFT: %d items after spam filter + picks (sorted by floor desc)\n", _pendingResult.count);

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
