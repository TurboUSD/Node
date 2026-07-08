// include/ui/screen_tickers.h — Token Screener screen (screen 3 / index 2).
//
// Displays up to 10 saved tickers per node. Each ticker shows a compact row
// (symbol · price · 24h change · mini chart) that expands on tap to a full
// card (name, FDV, price, change, 24-bar OHLCV chart).
//
// Adding a ticker is done entirely on-device: user taps "+ Add", types a
// symbol or name, the screen calls the search-tokens Edge Function, shows
// the results list, and on selection calls add-node-ticker. No web config
// needed.
//
// Layout (480×480):
//   Header bar      38 px  (shared_components.h)
//   Scrollable body 404 px — tickers stack vertically, scroll if overflow
//   Footer bar      38 px  (shared_components.h)
//
// Compact card: 62 px tall   → max ~6 visible without scroll
// Expanded card: 196 px tall → leaves room for ~3 compact + 1 expanded
//
// HTTP calls are dispatched to a FreeRTOS task so they never block LVGL.

#pragma once
#include <lvgl.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "storage.h"
#include "ui/shared_components.h"
#include "net_lock.h"
#include "disk_cache.h"
// LVGL bundles lodepng (compiled as C when LV_USE_PNG=1). We use exactly one
// function from it — declared here directly instead of including lodepng.h,
// because that header conflicts with extern "C" (it exposes its own C++
// std::vector overloads when __cplusplus is set). Decodes token logos ONCE in
// the bg task; the LVGL png decoder itself is not registered (per-frame
// decoding is too slow for the RGB panel). Output buffer is allocated with
// lv_mem_alloc → free it with lv_mem_free.
extern "C" unsigned lodepng_decode32(unsigned char** out, unsigned* w, unsigned* h,
                                     const unsigned char* in, size_t insize);
// LVGL also bundles ChaN's tjpgd (compiled as C when LV_USE_SJPG=1) — needed
// because DexScreener's CDN serves token logos as BASELINE JPEG no matter
// what format is requested (verified: format=png still returns image/jpeg).
// tjpgd.h has its own extern "C" guards, so a plain include works.
#include <src/extra/libs/sjpg/tjpgd.h>

// ── Brand colours (match web palette) ─────────────────────────────────────────
#define CLR_GREEN    0x43e397
#define CLR_RED      0xff6b6b
#define CLR_BLUE     0x5b8dee
#define CLR_YELLOW   0xffcf72
#define CLR_BG       0x000000
#define CLR_CARD     0x121214   // was 0x0c0c0c — matches the lightened web cards
#define CLR_SURFACE  0x1b1b1e   // was 0x141414
#define CLR_BORDER   0x2a2a2e   // was 0x1c1c1c — edges now read on black
#define CLR_TEXT     0xe8e8e8
#define CLR_MUTED    0x9096a1   // was 0x6e7280 — secondary text was too dark

// ── Constants ──────────────────────────────────────────────────────────────────
#define TICKER_MAX          10
#define COMPACT_H           58      // px collapsed card: 6·58 + 5·6 gap + 2·8 pad = 394 ≤ 404 BODY_H,
                                    // so all 6 fit in 1-col with a slight header/footer margin (no scroll)
#define EXPANDED_H          250     // px height of expanded ticker card (chart got taller)
#define CHART_BARS          24      // hourly OHLCV bars shown in expanded view
#define BODY_H              (SCREEN_HEIGHT - 38 - 38)  // 404 px

// ── Data structures ────────────────────────────────────────────────────────────
struct TickerEntry {
    char pool_address[68]  = {};
    char chain_id[20]      = {};
    char base_symbol[16]   = {};
    char base_name[52]     = {};
    char quote_symbol[16]  = {};
    float price_usd        = 0.0f;
    float change_24h       = 0.0f;   // percent
    float fdv              = 0.0f;
    // Full OHLC per hourly bar so the expanded card can draw real candles
    // (wick = low→high, body = open→close, green/red by direction).
    float chart_open [CHART_BARS] = {};
    float chart_high [CHART_BARS] = {};
    float chart_low  [CHART_BARS] = {};
    float chart_closes[CHART_BARS] = {};
    int   chart_count      = 0;
    time_t chart_last_ts   = 0;          // unix ts of the NEWEST candle (for X-axis dates)
    bool  live_loaded      = false;
    bool  chart_loaded     = false;
    volatile bool chart_dirty = false;  // bg task → UI: fresh chart data to draw
    volatile bool live_dirty  = false;  // bg task → UI: fresh price/mcap/change
    volatile bool chart_want  = false;  // UI → dispatcher: this chart NEEDS a fetch.
                                        // Retried in pollPending until chart_loaded —
                                        // a direct dispatch was silently DROPPED when
                                        // the worker was busy (expand/TF-change showed
                                        // an empty chart that never filled in).
    uint32_t live_at       = 0;         // millis() of last live refresh (cache TTL)
    uint32_t chart_at      = 0;         // millis() of last chart refresh (cache TTL)
    bool  is_expanded      = false;

    // Token logo (from DexScreener pair info.imageUrl), downloaded + decoded
    // once in the bg task into a ready-to-blit 40×40 TRUE_COLOR_ALPHA bitmap.
    // logo_px lives in PSRAM and is intentionally NEVER freed while running
    // (entries keep it across list reloads by pool matching; freeing from the
    // bg task could yank a bitmap LVGL is mid-drawing on the other core).
    char           logo_url[160]  = {};
    uint8_t*       logo_px       = nullptr;
    lv_img_dsc_t   logo_dsc      = {};
    volatile bool  logo_ready    = false;  // bg task → UI: bitmap is complete
    bool           logo_applied  = false;  // UI: lv_img already created on card
};

struct SearchResultEntry {
    char pair_address[68]  = {};
    char chain_id[20]      = {};
    char base_symbol[16]   = {};
    char base_name[52]     = {};
    char quote_symbol[16]  = {};
    float liquidity_usd    = 0.0f;
    float price_usd        = 0.0f;
    float change_24h       = 0.0f;
};

// ── Async task payloads ────────────────────────────────────────────────────────
enum TickerTaskType { TT_LOAD_LIST, TT_LOAD_LIVE, TT_LOAD_CHART, TT_SEARCH, TT_ADD, TT_REMOVE, TT_REORDER };

struct TickerTaskPayload {
    TickerTaskType type;
    char node_code[8];
    int  ticker_index;           // for TT_LOAD_LIVE / TT_LOAD_CHART
    char query[64];              // for TT_SEARCH
    SearchResultEntry to_add;    // for TT_ADD
    char pool_to_remove[68];     // for TT_REMOVE
    char pools_ordered[TICKER_MAX][68]; // for TT_REORDER: pool addresses in new display order
    int  pools_count = 0;               // for TT_REORDER
};

// ── Utility ────────────────────────────────────────────────────────────────────
static void fmtFdv(char* buf, size_t sz, float v) {
    if      (v >= 1e9f) snprintf(buf, sz, "$%.2fB", v / 1e9f);
    else if (v >= 1e6f) snprintf(buf, sz, "$%.2fM", v / 1e6f);
    else if (v >= 1e3f) snprintf(buf, sz, "$%.2fK", v / 1e3f);
    else                snprintf(buf, sz, "$%.2f",  v);
}

static void fmtPrice(char* buf, size_t sz, float v) {
    if      (v < 0.00001f) snprintf(buf, sz, "$%.2e",  v);
    else if (v < 0.001f)   snprintf(buf, sz, "$%.6f",  v);
    else if (v < 0.1f)     snprintf(buf, sz, "$%.4f",  v);
    else if (v < 10.0f)    snprintf(buf, sz, "$%.3f",  v);
    else                   snprintf(buf, sz, "$%.2f",  v);
}

// Compact liquidity: "$850k", "$12M", "$1.2B" — no decimals under 1B (the
// search rows are narrow; this is a magnitude cue, not an exact figure).
static void fmtLiq(char* buf, size_t sz, float v) {
    if      (v >= 1e9f) snprintf(buf, sz, "$%.1fB", v / 1e9f);
    else if (v >= 1e6f) snprintf(buf, sz, "$%.0fM", v / 1e6f);
    else if (v >= 1e3f) snprintf(buf, sz, "$%.0fk", v / 1e3f);
    else                snprintf(buf, sz, "$%.0f",  v);
}

// GeckoTerminal network slug from DexScreener chain ID
static const char* chainToGT(const char* chainId) {
    if (strcmp(chainId, "ethereum") == 0) return "eth";
    if (strcmp(chainId, "bsc")      == 0) return "bsc";
    if (strcmp(chainId, "polygon")  == 0) return "polygon_pos";
    if (strcmp(chainId, "arbitrum") == 0) return "arbitrum";
    if (strcmp(chainId, "optimism") == 0) return "optimism";
    return chainId;  // "base", "solana", etc. pass through unchanged
}

// ── TickerScreen class ────────────────────────────────────────────────────────
class TickerScreen {
public:

    // Called from ui_manager once during startup. Builds the static skeleton;
    // ticker data is populated later via refresh().
    lv_obj_t* build(lv_obj_t* parentScreen, lv_event_cb_t onLogoTapped,
                    lv_event_cb_t onDateTapped, lv_event_cb_t onQrTapped, void* userData) {
        _userData = userData;

        header = buildSharedHeader(parentScreen, onLogoTapped, onDateTapped, userData);
        footer = buildSharedFooter(parentScreen, onQrTapped, userData);

        // Scrollable body container
        _body = lv_obj_create(parentScreen);
        lv_obj_set_size(_body, SCREEN_WIDTH, BODY_H);
        lv_obj_align(_body, LV_ALIGN_TOP_MID, 0, 38);
        lv_obj_set_style_bg_color(_body, lv_color_black(), 0);
        lv_obj_set_style_border_width(_body, 0, 0);
        lv_obj_set_style_pad_hor(_body, 12, 0);
        lv_obj_set_style_pad_ver(_body, 8, 0);
        lv_obj_set_style_pad_row(_body, 6, 0);
        lv_obj_set_flex_flow(_body, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(_body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_scroll_dir(_body, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(_body, LV_SCROLLBAR_MODE_ACTIVE);

        // ── Controls live in the FOOTER now (used to be a top title row). This
        // frees ~28 px at the top so the 1-column view fits all 6 cards. The
        // "Network: N nodes" text is hidden; order is Add · Edit · [1|2] · Refresh.
        hideFooterNetworkText(footer);   // hides only the count; dot/name/"|"/gear stay
        lv_obj_t* fctl = lv_obj_create(footer.bar);
        // SIZE_CONTENT so the row hugs its controls (placed right after the "|" by
        // layoutFooterControls). FULL bar height + ext_click_area so a tap anywhere
        // in the footer band reaches the buttons — the old short strip left the
        // bottom padding untappable, which is why taps sometimes did nothing.
        lv_obj_set_size(fctl, LV_SIZE_CONTENT, 38);
        lv_obj_set_style_bg_opa(fctl, LV_OPA_0, 0);
        lv_obj_set_style_border_width(fctl, 0, 0);
        lv_obj_set_style_pad_ver(fctl, 0, 0);
        lv_obj_set_style_pad_hor(fctl, 2, 0);
        lv_obj_set_flex_flow(fctl, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(fctl, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(fctl, 14, 0);
        lv_obj_set_ext_click_area(fctl, 8);
        lv_obj_clear_flag(fctl, LV_OBJ_FLAG_SCROLLABLE);

        // Add (bare word)
        _addBtn = lv_btn_create(fctl);
        lv_obj_set_size(_addBtn, LV_SIZE_CONTENT, 30);
        lv_obj_set_style_bg_opa(_addBtn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(_addBtn, 0, 0);
        lv_obj_set_style_shadow_width(_addBtn, 0, 0);
        lv_obj_set_style_pad_hor(_addBtn, 4, 0);
        lv_obj_set_ext_click_area(_addBtn, 10);
        lv_obj_add_event_cb(_addBtn, _onAddBtnTapped, LV_EVENT_CLICKED, this);
        // ONE grey for every footer clickable (both screens): 0x80808a @ 90% —
        // exactly what the NFT data/carousel toggles show when active
        // (NFT_CLR_TOGGLE_ON).
        { lv_obj_t* l = lv_label_create(_addBtn); lv_label_set_text(l, "Add");
          lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
          lv_obj_set_style_text_color(l, lv_color_hex(0x80808a), 0);
          lv_obj_set_style_text_opa(l, LV_OPA_90, 0); lv_obj_center(l); }

        // Edit — TEXT word (like Add), toggles reorder/delete mode. Does NOT change
        // colour on toggle (per request); edit mode is shown by the cell arrows/delete.
        _editBtn = lv_btn_create(fctl);
        lv_obj_set_size(_editBtn, LV_SIZE_CONTENT, 30);
        lv_obj_set_style_bg_opa(_editBtn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(_editBtn, 0, 0);
        lv_obj_set_style_shadow_width(_editBtn, 0, 0);
        lv_obj_set_style_pad_hor(_editBtn, 4, 0);
        lv_obj_set_ext_click_area(_editBtn, 10);
        lv_obj_add_event_cb(_editBtn, _onEditBtnTapped, LV_EVENT_CLICKED, this);
        _editBtnLabel = lv_label_create(_editBtn);
        lv_label_set_text(_editBtnLabel, "Edit");
        lv_obj_set_style_text_font(_editBtnLabel, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(_editBtnLabel, lv_color_hex(0x80808a), 0);   // toolbar grey — see Add
        lv_obj_set_style_text_opa(_editBtnLabel, LV_OPA_90, 0);
        lv_obj_center(_editBtnLabel);

        // Columns toggle — FLAT icon, no button chrome, same muted colour as the
        // Add/Edit words. Drawn from primitive bars so it reads as "columns":
        // one wide bar in 1-column mode, two narrow bars side-by-side in
        // 2-column mode (i.e. it always shows the CURRENT layout). Tap cycles.
        _cols = storage.getTickerCols();
        _colsBtn = lv_obj_create(fctl);
        lv_obj_set_size(_colsBtn, 16, 10);
        lv_obj_set_style_bg_opa(_colsBtn, LV_OPA_0, 0);
        lv_obj_set_style_border_width(_colsBtn, 0, 0);
        lv_obj_set_style_pad_all(_colsBtn, 0, 0);
        lv_obj_add_flag(_colsBtn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(_colsBtn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_ext_click_area(_colsBtn, 14);   // icon shrank — grow the touch halo
        lv_obj_add_event_cb(_colsBtn, [](lv_event_t* e) {
            auto* self = static_cast<TickerScreen*>(lv_event_get_user_data(e));
            if (!self) return;
            self->_cols = (self->_cols >= 2) ? 1 : 2;   // cycle 1 → 2 → 1
            storage.setTickerCols((uint8_t)self->_cols);
            self->_refreshColsBtns();
            self->_rebuildRequested = true;
        }, LV_EVENT_CLICKED, this);
        for (int b = 0; b < 2; b++) {
            _colsBars[b] = lv_obj_create(_colsBtn);
            lv_obj_set_style_radius(_colsBars[b], 2, 0);
            lv_obj_set_style_border_width(_colsBars[b], 0, 0);
            lv_obj_set_style_bg_color(_colsBars[b], lv_color_hex(0x80808a), 0);   // toolbar grey — see Add
            lv_obj_set_style_bg_opa(_colsBars[b], LV_OPA_90, 0);
            lv_obj_clear_flag(_colsBars[b], LV_OBJ_FLAG_CLICKABLE);
            lv_obj_clear_flag(_colsBars[b], LV_OBJ_FLAG_SCROLLABLE);
        }
        _refreshColsBtns();

        // Refresh (bare arrows)
        lv_obj_t* tRefresh = lv_btn_create(fctl);
        lv_obj_set_size(tRefresh, LV_SIZE_CONTENT, 30);
        lv_obj_set_style_bg_opa(tRefresh, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(tRefresh, 0, 0);
        lv_obj_set_style_shadow_width(tRefresh, 0, 0);
        lv_obj_set_style_pad_hor(tRefresh, 4, 0);
        lv_obj_set_ext_click_area(tRefresh, 10);
        lv_obj_add_event_cb(tRefresh, [](lv_event_t* e) {
            auto* self = static_cast<TickerScreen*>(lv_event_get_user_data(e));
            if (self) self->_manualRefresh();
        }, LV_EVENT_CLICKED, this);
        { lv_obj_t* l = lv_label_create(tRefresh); lv_label_set_text(l, LV_SYMBOL_REFRESH);
          lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
          lv_obj_set_style_text_color(l, lv_color_hex(0x80808a), 0);   // toolbar grey — see Add
          lv_obj_set_style_text_opa(l, LV_OPA_90, 0); lv_obj_center(l); }

        // Left-align the row after the "|", cap the name (marquee if long), keep the gear.
        // Gap 2 + fctl pad 2 + Add's own pad 4 = 8 px of visible space between the
        // "|" and "Add" — the SAME 8 px the node name keeps to the "|". The old 12
        // read as a hole and wasted width the name could use.
        layoutFooterControls(footer, fctl, 2);

        // Placeholder shown when no tickers are loaded yet
        _emptyLabel = lv_label_create(_body);
        lv_label_set_text(_emptyLabel, "");
        lv_obj_set_style_text_color(_emptyLabel, lv_color_hex(CLR_MUTED), 0);
        lv_obj_set_style_text_font(_emptyLabel, &lv_font_montserrat_12, 0);
        lv_label_set_long_mode(_emptyLabel, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(_emptyLabel, LV_PCT(100));
        lv_label_set_text(_emptyLabel, "No tickers yet.\nTap \"+  Add\" to search and add tokens,\nor visit your node config page.");
        lv_obj_set_style_text_align(_emptyLabel, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(_emptyLabel, 60, 0);

        // Spinner shown during loading
        _spinner = lv_spinner_create(_body, 1000, 60);
        lv_obj_set_size(_spinner, 40, 40);
        lv_obj_set_style_arc_color(_spinner, lv_color_hex(CLR_GREEN), LV_PART_INDICATOR);
        lv_obj_add_flag(_spinner, LV_OBJ_FLAG_HIDDEN);

        return _body;
    }

    // Call this when the screen becomes visible (nav switched to this screen).
    void onShow(const char* nodeCode) {
        strncpy(_nodeCode, nodeCode, sizeof(_nodeCode) - 1);
        uint8_t cols = storage.getTickerCols();   // may have changed from the web
        if ((int)cols != _cols) { _cols = cols; _refreshColsBtns(); }
        _rebuildTickerCards();   // instant: cached entries render immediately
        // QUEUED, not direct: a direct dispatch was silently dropped whenever
        // the worker was still busy (charts/logos take a while with 6
        // tickers), so web-side add/remove/reorder never reached the device
        // until a lucky re-entry. The flag retries every poll tick until the
        // worker is free.
        _listReloadRequested = true;
    }

    void refreshHeaderFooter(SharedHeaderRefs& h, SharedFooterRefs& f,
                              struct tm& t, float tempC, int humidityPct,
                              bool is24h, char tempUnit,
                              const String& nodeName, int onlineCount) {
        refreshSharedHeader(h, t, tempC, humidityPct, is24h, tempUnit);
        refreshSharedFooter(f, nodeName, onlineCount);
    }

    SharedHeaderRefs header;
    SharedFooterRefs footer;

private:

    // ── Members ───────────────────────────────────────────────────────────────
    lv_obj_t*        _body        = nullptr;
    lv_obj_t*        _addBtn      = nullptr;
    lv_obj_t*        _editBtn     = nullptr;
    lv_obj_t*        _editBtnLabel= nullptr;
    lv_obj_t*        _emptyLabel  = nullptr;
    lv_obj_t*        _colsBtn     = nullptr;  // flat 1|2 column toggle icon (footer)
    lv_obj_t*        _colsBars[2] = { nullptr, nullptr };   // the icon's "column" bars
    int              _cols        = 1;       // 1 or 2 card columns (persisted)
    lv_coord_t       _savedScrollY = 0;      // view position restored across rebuilds/visits
    lv_obj_t*        _spinner     = nullptr;
    void*            _userData    = nullptr;
    char             _nodeCode[8] = {};
    bool             _editMode    = false;

    // Which charts are expanded, keyed by POOL ADDRESS (not array index).
    // The entries array gets rewritten in place on every server list reload
    // (bg task, other core) and the index-carried is_expanded occasionally
    // lost one of several open charts across a screen swipe. This set is the
    // single source of truth: updated on every user toggle, re-applied to the
    // freshly parsed entries after every list load.
    // MUST match pool_address[68]: a 44-byte buffer truncated longer pool
    // addresses (Solana/base58 ~44 chars), so _isExpandedPool() could never
    // match them again — the safety net that restores an open chart after the
    // reload onShow() triggers failed, and that chart collapsed on swipe-back.
    char             _expPools[TICKER_MAX][68] = {};
    bool _isExpandedPool(const char* pool) const {
        if (!pool || !pool[0]) return false;
        for (int i = 0; i < TICKER_MAX; i++)
            if (_expPools[i][0] && strcasecmp(_expPools[i], pool) == 0) return true;
        return false;
    }
    void _setExpandedPool(const char* pool, bool on) {
        if (!pool || !pool[0]) return;
        for (int i = 0; i < TICKER_MAX; i++) {
            if (_expPools[i][0] && strcasecmp(_expPools[i], pool) == 0) {
                if (!on) _expPools[i][0] = '\0';
                return;
            }
        }
        if (!on) return;
        for (int i = 0; i < TICKER_MAX; i++)
            if (!_expPools[i][0]) { strncpy(_expPools[i], pool, sizeof(_expPools[i]) - 1); return; }
    }
    void _clearExpandedPools() { for (int i = 0; i < TICKER_MAX; i++) _expPools[i][0] = '\0'; }

    // Card mutations (expand/collapse/delete/reorder) must NOT rebuild the
    // card tree from inside an LVGL event callback — lv_obj_del()'ing the
    // object that is currently dispatching the event corrupts LVGL's event
    // list (use-after-free → later crash). Callbacks set these flags instead;
    // pollPending (an lv_timer, safe context) performs the work.
    volatile bool    _rebuildRequested = false;

    // Chart timeframe (applies to every card): 0 = 1D (24 daily candles),
    // 1 = 1W (24 weekly), 2 = 1M (12 monthly). GT's OHLCV endpoint only does
    // day/hour/minute, so 1W/1M are aggregated client-side from daily bars.
    uint8_t _chartTf = 0;
    // TF GENERATION: bumped on every timeframe change. _fetchChart captures it
    // on entry and discards its result if it changed mid-fetch — an in-flight
    // list reload used to keep writing OLD-timeframe candles after the switch
    // and re-marked them chart_loaded, so the follow-up reload skipped them
    // (old-TF data under new-TF X labels). Same pattern as the NFT _imgGen.
    volatile uint16_t _tfGen = 0;
    // GeckoTerminal cooldown: set 20 s into the future when a chart fetch gets
    // a 429 — every chart path (list pass + chart_want queue) waits it out.
    volatile uint32_t _gtCooldownUntil = 0;
    int _tfGroup() const { return _chartTf == 0 ? 1 : (_chartTf == 1 ? 7 : 30); }
    int _tfBars()  const { return _chartTf == 2 ? 12 : CHART_BARS; }
    volatile bool    _listReloadRequested = false;  // retried until the worker is free
    volatile bool    _searchRequested     = false;  // ditto, for the search dialog

    // Mutations queued when the worker is busy. Previously add/remove/reorder
    // did `if (_bgTask) return;` and were silently dropped server-side while the
    // local UI still applied them — so a deleted ticker REAPPEARED and a reorder
    // REVERTED on the next list reload. These retry every poll until dispatched.
    // (All set/read on core 1 only — the poll timer and event callbacks.)
    SearchResultEntry _pendingAdd;
    bool             _pendingAddSet     = false;
    char             _pendingRemovePool[68] = {};
    bool             _pendingRemoveSet  = false;
    bool             _pendingReorderSet = false;

    // Token logo bitmaps freed after the card tree is rebuilt (freeing on the
    // spot could yank a bitmap LVGL is mid-drawing). Filled on delete/reorder.
    uint8_t*         _logoFreeList[TICKER_MAX] = {};
    int              _logoFreeCount = 0;

    bool             _loadedOnce = false;   // false until the first list load returns

    TickerEntry      _tickers[TICKER_MAX];
    int              _tickerCount = 0;

    // Per-ticker card widgets (rebuilt whenever list changes)
    struct CardWidgets {
        lv_obj_t* container      = nullptr;
        lv_obj_t* symBg          = nullptr;   // logo circle (letter fallback inside)
        lv_obj_t* logoImg        = nullptr;   // token logo image (once downloaded)
        lv_obj_t* symbolLabel    = nullptr;   // compact: symbol text block
        lv_obj_t* nameLabel      = nullptr;   // compact
        lv_obj_t* fdvLabel       = nullptr;   // compact
        lv_obj_t* changeLabel    = nullptr;   // compact
        lv_obj_t* chart          = nullptr;   // compact sparkline / expanded chart
        lv_obj_t* expandedPanel  = nullptr;   // shown only when expanded
        lv_obj_t* priceLabel     = nullptr;   // expanded
        lv_obj_t* removeBtn      = nullptr;
        lv_chart_series_t* series = nullptr;
        float chartMin           = 0.0f;      // candle scaling for the draw cb
        float chartRange         = 1.0f;
    };
    CardWidgets _cards[TICKER_MAX];

    // Search dialog widgets
    lv_obj_t*  _searchOverlay    = nullptr;
    lv_obj_t*  _searchTA         = nullptr;
    lv_obj_t*  _searchKB         = nullptr;
    lv_obj_t*  _searchResultsCont= nullptr;
    lv_obj_t*  _searchSpinner    = nullptr;

    SearchResultEntry _searchResults[12];
    int               _searchResultCount = 0;

    // FreeRTOS async task. `volatile` because it's written by the bg task on
    // core 0 (self-clearing on exit) and read by the UI on core 1.
    volatile TaskHandle_t _bgTask = nullptr;
    SemaphoreHandle_t _dataMutex = nullptr;

    // Pending result from background task (written under mutex, read on timer cb)
    enum PendingResultType { PR_NONE, PR_LIST_LOADED, PR_LIVE_LOADED, PR_CHART_LOADED,
                             PR_SEARCH_DONE, PR_ADD_DONE, PR_REMOVE_DONE };
    struct PendingResult {
        PendingResultType type = PR_NONE;
        int  tickerIndex = -1;
    } _pending;

    // ── Card building ─────────────────────────────────────────────────────────

    // Columns icon: reshape the two bars to mirror the ACTIVE layout —
    // one full-width bar (1 column) or two half-width bars (2 columns).
    void _refreshColsBtns() {
        if (!_colsBtn || !_colsBars[0] || !_colsBars[1]) return;
        if (_cols >= 2) {
            lv_obj_set_size(_colsBars[0], 6, 10);
            lv_obj_align(_colsBars[0], LV_ALIGN_LEFT_MID, 0, 0);
            lv_obj_set_size(_colsBars[1], 6, 10);
            lv_obj_align(_colsBars[1], LV_ALIGN_RIGHT_MID, 0, 0);
            lv_obj_clear_flag(_colsBars[1], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_set_size(_colsBars[0], 16, 10);
            lv_obj_align(_colsBars[0], LV_ALIGN_LEFT_MID, 0, 0);
            lv_obj_add_flag(_colsBars[1], LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Manual force-reload triggered by the refresh button. Marks every ticker's
    // live AND chart data stale (bypassing both TTLs) and requests a full list
    // reload; pollPending dispatches TT_LOAD_LIST when the worker is free.
    // Charts were NOT included before — the button only reset live_at, so a
    // stuck/empty chart inside its 15-min TTL was skipped by the reload and the
    // refresh "did nothing" for the miniatures.
    void _manualRefresh() {
        for (int i = 0; i < _tickerCount; i++) {
            _tickers[i].live_at  = 0;   // ignore the live TTL → re-fetch price/mcap/change
            _tickers[i].chart_at = 0;   // ignore the chart TTL → re-fetch candles too
        }
        _listReloadRequested = true;   // retried each poll until the worker is free
        Log.println("tickers: manual refresh requested (force live + chart + logo reload)");
    }

    void _rebuildTickerCards() {
        // Permanent diagnostic: every rebuild logs which cards are expanded.
        // If a chart ever "collapses on its own", the serial log shows whether
        // the STATE was lost (flag flips between rebuilds) or a stray
        // tap/collapse event fired (those log separately above).
        {
            char em[TICKER_MAX + 1];
            for (int i = 0; i < _tickerCount && i < TICKER_MAX; i++) em[i] = _tickers[i].is_expanded ? 'E' : '.';
            em[min(_tickerCount, (int)TICKER_MAX)] = '\0';
            Log.printf("tickers: rebuild n=%d expanded=[%s]\n", _tickerCount, em);
        }
        // Delete all existing ticker card objects.
        // NOTE: only ever called from timer/init context (pollPending, onShow),
        // never from inside a card's own event callback — see _rebuildRequested.
        for (int i = 0; i < TICKER_MAX; i++) {
            if (_cards[i].container) {
                lv_obj_del(_cards[i].container);
            }
            _cards[i] = {};
            _tickers[i].logo_applied = false;   // cards are new → re-attach logos
        }

        // Empty body: show the spinner while the FIRST list load is still in
        // flight, and only fall back to the "No tickers yet" copy once a load
        // has actually returned empty (it used to flash "No tickers yet" during
        // every initial load).
        bool hasAny   = (_tickerCount > 0);
        bool loading  = !hasAny && !_loadedOnce;
        if (hasAny || !loading) lv_obj_add_flag(_spinner, LV_OBJ_FLAG_HIDDEN);
        else                    lv_obj_clear_flag(_spinner, LV_OBJ_FLAG_HIDDEN);
        if (hasAny || loading)  lv_obj_add_flag(_emptyLabel, LV_OBJ_FLAG_HIDDEN);
        else                    lv_obj_clear_flag(_emptyLabel, LV_OBJ_FLAG_HIDDEN);

        // 1 column: classic full-width stack. 2 columns: row-wrap grid — the
        // full-width title row keeps its own line, cards pair up below it.
        lv_obj_set_flex_flow(_body, _cols == 2 ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_column(_body, 6, 0);

        for (int i = 0; i < _tickerCount; i++) {
            _buildCard(i);
        }

        // Hide add button if at max capacity
        if (_tickerCount >= TICKER_MAX) {
            lv_obj_add_flag(_addBtn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(_addBtn, LV_OBJ_FLAG_HIDDEN);
        }

        // Equalize the header and footer margins: with top-anchored flex, ALL the
        // leftover space piled up between the last card and the footer (top gap 8,
        // bottom gap 8 + slack). When the cards DON'T fill the viewport, center
        // them so the slack splits evenly. When they overflow, stay top-anchored —
        // flex-center clips overflowing content at BOTH ends and the first card
        // would become unreachable.
        lv_obj_update_layout(_body);
        bool overflows = lv_obj_get_scroll_bottom(_body) > 0 || lv_obj_get_scroll_top(_body) > 0;
        if (_cols == 2)   // ROW_WRAP: vertical distribution is the TRACK placement (3rd arg)
            lv_obj_set_flex_align(_body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                                  overflows ? LV_FLEX_ALIGN_START : LV_FLEX_ALIGN_CENTER);
        else              // COLUMN: vertical distribution is the MAIN placement (1st arg)
            lv_obj_set_flex_align(_body, overflows ? LV_FLEX_ALIGN_START : LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // Restore the view position (someone parked an expanded chart mid-list
        // and expects to find the screen exactly there on return).
        lv_obj_update_layout(_body);
        lv_obj_scroll_to_y(_body, _savedScrollY, LV_ANIM_OFF);
    }

    void _buildCard(int idx) {
        TickerEntry& t = _tickers[idx];
        CardWidgets& w = _cards[idx];

        bool exp = t.is_expanded;
        lv_coord_t cardH = exp ? EXPANDED_H : COMPACT_H;

        // Card container. Border deliberately LIGHTER than CLR_BORDER so the
        // card outline reads clearly against the black background.
        w.container = lv_obj_create(_body);
        lv_obj_set_size(w.container, _cols == 2 ? LV_PCT(49) : LV_PCT(100), cardH);
        lv_obj_set_style_bg_color(w.container, lv_color_hex(CLR_CARD), 0);
        lv_obj_set_style_border_color(w.container, lv_color_hex(0x3a3a42), 0);
        lv_obj_set_style_border_width(w.container, 1, 0);
        lv_obj_set_style_radius(w.container, 8, 0);
        lv_obj_set_style_pad_all(w.container, 10, 0);
        lv_obj_clear_flag(w.container, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(w.container, LV_OBJ_FLAG_CLICKABLE);

        // Store index in user data for the click callback
        lv_obj_set_user_data(w.container, (void*)(intptr_t)idx);
        lv_obj_add_event_cb(w.container, _onCardTapped, LV_EVENT_CLICKED, this);

        if (!exp) {
            _buildCompactCard(idx);
        } else {
            _buildExpandedCard(idx);
        }
    }

    // Shared: builds the 40×40 round logo holder (letter fallback inside) as a
    // flex child of `parent`, stores refs in the card, and overlays the real
    // token logo if it's already downloaded.
    void _buildSymCircle(int idx, lv_obj_t* parent) {
        TickerEntry& t = _tickers[idx];
        CardWidgets& w = _cards[idx];

        w.symBg = lv_obj_create(parent);
        lv_obj_set_size(w.symBg, 40, 40);
        lv_obj_set_style_bg_color(w.symBg, lv_color_hex(CLR_SURFACE), 0);
        lv_obj_set_style_border_color(w.symBg, lv_color_hex(CLR_BORDER), 0);
        lv_obj_set_style_border_width(w.symBg, 1, 0);
        lv_obj_set_style_radius(w.symBg, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_clip_corner(w.symBg, true, 0);   // clip the logo round
        lv_obj_set_style_pad_all(w.symBg, 0, 0);
        lv_obj_clear_flag(w.symBg, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(w.symBg, LV_OBJ_FLAG_CLICKABLE);   // taps fall through to the card

        w.symbolLabel = lv_label_create(w.symBg);
        lv_label_set_text(w.symbolLabel, t.base_symbol[0] ? t.base_symbol : "?");
        lv_obj_set_style_text_font(w.symbolLabel, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(w.symbolLabel, lv_color_hex(CLR_TEXT), 0);
        lv_obj_center(w.symbolLabel);

        _applyLogoIfReady(idx);
    }

    // Creates the lv_img for a downloaded logo (40×40 bitmap in te.logo_dsc).
    void _applyLogoIfReady(int idx) {
        TickerEntry& t = _tickers[idx];
        CardWidgets& w = _cards[idx];
        if (!t.logo_ready || t.logo_applied || !w.symBg) return;
        w.logoImg = lv_img_create(w.symBg);
        lv_img_set_src(w.logoImg, &t.logo_dsc);
        lv_obj_center(w.logoImg);
        if (w.symbolLabel) lv_obj_add_flag(w.symbolLabel, LV_OBJ_FLAG_HIDDEN);
        t.logo_applied = true;
    }

    void _buildCompactCard(int idx) {
        TickerEntry& t = _tickers[idx];
        CardWidgets& w = _cards[idx];

        // Flex ROW with cross-axis CENTER: logo circle | name+meta (grows) |
        // sparkline (or edit-mode buttons). Flex vertically centers every
        // child against the tallest one — the old manual lv_obj_align_to()
        // approach computed positions from pre-layout coordinates, which left
        // the text block off-center and overflowing the card.
        lv_obj_set_flex_flow(w.container, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(w.container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(w.container, 8, 0);

        _buildSymCircle(idx, w.container);

        // Name + meta column — flex-grow eats the width between logo & chart.
        lv_obj_t* textCol = lv_obj_create(w.container);
        lv_obj_set_height(textCol, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(textCol, 1);
        lv_obj_set_style_bg_opa(textCol, LV_OPA_0, 0);
        lv_obj_set_style_border_width(textCol, 0, 0);
        lv_obj_set_style_pad_all(textCol, 0, 0);
        lv_obj_set_flex_flow(textCol, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(textCol, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_clear_flag(textCol, LV_OBJ_FLAG_SCROLLABLE);
        // CRITICAL for tap-to-expand: plain lv_obj containers are CLICKABLE by
        // default and swallow the tap before it reaches the card container —
        // that's why expanding/collapsing "only worked sometimes" (only taps
        // on empty card padding got through). Every inner container and chart
        // must let taps fall through.
        lv_obj_clear_flag(textCol, LV_OBJ_FLAG_CLICKABLE);

        w.nameLabel = lv_label_create(textCol);
        lv_label_set_text(w.nameLabel, t.base_name);
        lv_obj_set_style_text_font(w.nameLabel, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(w.nameLabel, lv_color_hex(CLR_TEXT), 0);
        if (_cols == 2) {
            // Narrow cards: long names marquee slowly on one line (CIRCULAR
            // only animates when the text doesn't fit).
            lv_label_set_long_mode(w.nameLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_obj_set_style_anim_speed(w.nameLabel, 12, 0);
        } else {
            lv_label_set_long_mode(w.nameLabel, LV_LABEL_LONG_DOT);
        }
        lv_obj_set_width(w.nameLabel, LV_PCT(100));

        // Symbol · FDV · change% on second line
        lv_obj_t* metaRow = lv_obj_create(textCol);
        lv_obj_set_size(metaRow, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(metaRow, LV_OPA_0, 0);
        lv_obj_set_style_border_width(metaRow, 0, 0);
        lv_obj_set_style_pad_all(metaRow, 0, 0);
        lv_obj_set_style_pad_column(metaRow, 6, 0);
        lv_obj_set_flex_flow(metaRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(metaRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(metaRow, LV_OBJ_FLAG_CLICKABLE);   // taps fall through to the card

        // Ticker · MCAP · 24h% — 12pt and clearly readable (10pt muted-grey
        // was too small and too dark to read at arm's length).
        lv_obj_t* symTxt = lv_label_create(metaRow);
        lv_label_set_text(symTxt, t.base_symbol);
        lv_obj_set_style_text_font(symTxt, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(symTxt, lv_color_hex(0xc4c4cc), 0);

        lv_obj_t* dot = lv_label_create(metaRow);
        lv_label_set_text(dot, "\xE2\x80\xA2");
        lv_obj_set_style_text_font(dot, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(dot, lv_color_hex(0x8a8a92), 0);

        w.fdvLabel = lv_label_create(metaRow);
        _updateFdvLabel(idx);
        lv_obj_set_style_text_font(w.fdvLabel, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(w.fdvLabel, lv_color_hex(0xc4c4cc), 0);

        w.changeLabel = lv_label_create(metaRow);
        _updateChangeLabel(idx);
        lv_obj_set_style_text_font(w.changeLabel, &lv_font_montserrat_12, 0);

        if (_editMode) {
            // Edit mode: ▲ / ▼ reorder + red delete, in place of the sparkline.
            _makeCardActionBtn(w.container, LV_SYMBOL_UP,    CLR_TEXT, idx, _onMoveUpTapped);
            _makeCardActionBtn(w.container, LV_SYMBOL_DOWN,  CLR_TEXT, idx, _onMoveDownTapped);
            _makeCardActionBtn(w.container, LV_SYMBOL_TRASH, CLR_RED,  idx, _onDeleteTapped);
        } else if (_cols == 2) {
            // Two-column cards have no room for the sparkline — skip it.
        } else {
            // Mini sparkline chart (right side, 70px wide)
            w.chart = lv_chart_create(w.container);
            lv_obj_set_size(w.chart, 70, 36);
            lv_chart_set_type(w.chart, LV_CHART_TYPE_LINE);
            // Point count follows the timeframe (12 on 1M): a fixed 24 left the
            // 12 monthly closes squashed into the left half of the sparkline.
            lv_chart_set_point_count(w.chart, _tfBars());
            lv_obj_set_style_bg_opa(w.chart, LV_OPA_0, 0);
            lv_obj_set_style_border_width(w.chart, 0, 0);
            lv_obj_set_style_size(w.chart, 0, LV_PART_INDICATOR);
            lv_obj_clear_flag(w.chart, LV_OBJ_FLAG_CLICKABLE);   // taps fall through to the card
            lv_chart_set_div_line_count(w.chart, 0, 0);

            bool up = (t.change_24h >= 0);
            lv_color_t lineCol = lv_color_hex(up ? CLR_GREEN : CLR_RED);
            w.series = lv_chart_add_series(w.chart, lineCol, LV_CHART_AXIS_PRIMARY_Y);
            _updateChartData(idx);
        }
    }

    // Small square icon button used by edit mode (reorder/delete).
    lv_obj_t* _makeCardActionBtn(lv_obj_t* parent, const char* symbol, uint32_t color,
                                 int idx, lv_event_cb_t cb) {
        lv_obj_t* btn = lv_btn_create(parent);
        lv_obj_set_size(btn, 32, 32);
        lv_obj_set_style_bg_color(btn, lv_color_hex(CLR_SURFACE), 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(CLR_BORDER), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);
        lv_obj_set_user_data(btn, (void*)(intptr_t)idx);
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, this);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, symbol);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
        lv_obj_center(lbl);
        return btn;
    }

    void _buildExpandedCard(int idx) {
        TickerEntry& t = _tickers[idx];
        CardWidgets& w = _cards[idx];

        // Flex COLUMN: topRow / priceRow / candle chart. (Manual align_to on
        // pre-layout coordinates was what pushed content outside the card.)
        lv_obj_set_flex_flow(w.container, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(w.container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(w.container, 4, 0);

        // ── Row 1: logo circle + name + pair + collapse button ──
        lv_obj_t* topRow = lv_obj_create(w.container);
        lv_obj_set_size(topRow, LV_PCT(100), 44);
        lv_obj_set_style_bg_opa(topRow, LV_OPA_0, 0);
        lv_obj_set_style_border_width(topRow, 0, 0);
        lv_obj_set_style_pad_all(topRow, 0, 0);
        lv_obj_set_style_pad_column(topRow, 8, 0);
        lv_obj_set_flex_flow(topRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(topRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(topRow, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(topRow, LV_OBJ_FLAG_CLICKABLE);   // taps fall through to the card

        _buildSymCircle(idx, topRow);

        lv_obj_t* nameCol = lv_obj_create(topRow);
        lv_obj_set_height(nameCol, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(nameCol, 1);
        lv_obj_set_style_bg_opa(nameCol, LV_OPA_0, 0);
        lv_obj_set_style_border_width(nameCol, 0, 0);
        lv_obj_set_style_pad_all(nameCol, 0, 0);
        lv_obj_set_flex_flow(nameCol, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(nameCol, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_clear_flag(nameCol, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(nameCol, LV_OBJ_FLAG_CLICKABLE);   // taps fall through to the card

        w.nameLabel = lv_label_create(nameCol);
        lv_label_set_text(w.nameLabel, t.base_name);
        lv_obj_set_style_text_font(w.nameLabel, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(w.nameLabel, lv_color_hex(CLR_TEXT), 0);
        if (_cols == 2) {
            lv_label_set_long_mode(w.nameLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_obj_set_style_anim_speed(w.nameLabel, 12, 0);
        } else {
            lv_label_set_long_mode(w.nameLabel, LV_LABEL_LONG_DOT);
        }
        lv_obj_set_width(w.nameLabel, LV_PCT(100));

        lv_obj_t* chainLbl = lv_label_create(nameCol);
        char chainBuf[32];
        snprintf(chainBuf, sizeof(chainBuf), "%s/%s", t.base_symbol, t.quote_symbol[0] ? t.quote_symbol : "USD");
        lv_label_set_text(chainLbl, chainBuf);
        lv_obj_set_style_text_font(chainLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(chainLbl, lv_color_hex(CLR_MUTED), 0);

        // Timeframe selector (1D / 1W / 1M), top-right next to the collapse
        // button — mirrors the web board. One shared setting for all cards.
        lv_obj_t* tfDd = lv_dropdown_create(topRow);
        lv_dropdown_set_options_static(tfDd, "1D\n1W\n1M");
        lv_dropdown_set_selected(tfDd, _chartTf);
        lv_dropdown_set_symbol(tfDd, NULL);   // drop the ▼ arrow — it read as a
                                              // second "collapse" control next to
                                              // the actual collapse (▲) button
        lv_obj_set_size(tfDd, _cols == 2 ? 54 : 62, 36);
        lv_obj_set_style_bg_color(tfDd, lv_color_hex(CLR_SURFACE), 0);
        lv_obj_set_style_border_color(tfDd, lv_color_hex(CLR_BORDER), 0);
        lv_obj_set_style_border_width(tfDd, 1, 0);
        lv_obj_set_style_radius(tfDd, 6, 0);
        lv_obj_set_style_pad_all(tfDd, 8, 0);
        lv_obj_set_style_text_font(tfDd, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(tfDd, lv_color_hex(CLR_TEXT), 0);
        lv_obj_t* tfList = lv_dropdown_get_list(tfDd);
        lv_obj_set_style_bg_color(tfList, lv_color_hex(CLR_SURFACE), 0);
        lv_obj_set_style_text_color(tfList, lv_color_hex(CLR_TEXT), 0);
        lv_obj_set_style_text_font(tfList, &lv_font_montserrat_12, 0);
        lv_obj_set_user_data(tfDd, (void*)(intptr_t)idx);
        lv_obj_add_event_cb(tfDd, _onTfChanged, LV_EVENT_VALUE_CHANGED, this);

        // Collapse button (top-right). Deleting/reordering lives in edit mode
        // (gear button in the title row) — this button ONLY collapses the
        // card. Chevron-up icon: an X read as "delete" (and the web's ⤡
        // collapse glyph doesn't exist in LVGL's built-in symbol font, so the
        // collapse chevron is the closest equivalent).
        w.removeBtn = lv_btn_create(topRow);
        lv_obj_set_size(w.removeBtn, 36, 36);                 // finger-sized
        lv_obj_set_style_bg_color(w.removeBtn, lv_color_hex(CLR_SURFACE), 0);
        lv_obj_set_style_border_color(w.removeBtn, lv_color_hex(CLR_BORDER), 0);
        lv_obj_set_style_border_width(w.removeBtn, 1, 0);
        lv_obj_set_style_radius(w.removeBtn, 6, 0);
        lv_obj_set_style_pad_all(w.removeBtn, 0, 0);
        lv_obj_set_ext_click_area(w.removeBtn, 10);           // forgiving touch target
        lv_obj_set_user_data(w.removeBtn, (void*)(intptr_t)idx);
        lv_obj_add_event_cb(w.removeBtn, _onCollapseTapped, LV_EVENT_CLICKED, this);
        lv_obj_t* xLbl = lv_label_create(w.removeBtn);
        lv_label_set_text(xLbl, LV_SYMBOL_UP);
        lv_obj_set_style_text_font(xLbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(xLbl, lv_color_hex(CLR_TEXT), 0);
        lv_obj_center(xLbl);

        // ── Row 2: FDV (big) + price (small) + change ──
        lv_obj_t* priceRow = lv_obj_create(w.container);
        lv_obj_set_size(priceRow, LV_PCT(100), 30);
        lv_obj_set_style_bg_opa(priceRow, LV_OPA_0, 0);
        lv_obj_set_style_border_width(priceRow, 0, 0);
        lv_obj_set_style_pad_all(priceRow, 0, 0);
        lv_obj_set_flex_flow(priceRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(priceRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
        lv_obj_set_style_pad_column(priceRow, 12, 0);
        lv_obj_clear_flag(priceRow, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(priceRow, LV_OBJ_FLAG_CLICKABLE);   // taps fall through to the card

        w.fdvLabel = lv_label_create(priceRow);
        _updateFdvLabel(idx);
        lv_obj_set_style_text_font(w.fdvLabel, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(w.fdvLabel, lv_color_hex(CLR_TEXT), 0);

        w.priceLabel = lv_label_create(priceRow);
        _updatePriceLabel(idx);
        lv_obj_set_style_text_font(w.priceLabel, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(w.priceLabel, lv_color_hex(0xc4c4cc), 0);

        w.changeLabel = lv_label_create(priceRow);
        _updateChangeLabel(idx);
        lv_obj_set_style_text_font(w.changeLabel, &lv_font_montserrat_12, 0);

        // ── Real candlestick price chart (DAILY bars) ──
        // Same one-series + draw-callback technique as the Turbo screen's
        // weekly chart: the BAR series carries each candle's HIGH; the
        // callback reshapes the bar into a thin grey wick (low→high) in
        // DRAW_PART_BEGIN and paints the open→close body (green/red) on top
        // in DRAW_PART_END. It also rewrites the Y-axis ticks into real USD
        // prices so the chart is actually readable.
        // Wrapper with pad_left: LVGL 8 draws Y tick labels OUTSIDE the chart's
        // left edge, so they need reserved space in the parent (see screen_debt).
        lv_obj_t* chartWrap = lv_obj_create(w.container);
        lv_obj_set_size(chartWrap, LV_PCT(100), 118);
        lv_obj_set_style_bg_opa(chartWrap, LV_OPA_0, 0);
        lv_obj_set_style_border_width(chartWrap, 0, 0);
        lv_obj_set_style_pad_all(chartWrap, 0, 0);
        lv_obj_set_style_pad_left(chartWrap, _cols == 2 ? 42 : 56, 0);   // ← Y tick labels live here (tighter in 2-col)
        lv_obj_set_style_pad_top(chartWrap, 7, 0);     // edge tick labels are centered on the first/last
        lv_obj_set_style_pad_bottom(chartWrap, 7, 0);  // tick — reserve space or they clip in half
        lv_obj_clear_flag(chartWrap, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(chartWrap, LV_OBJ_FLAG_CLICKABLE);

        w.chart = lv_chart_create(chartWrap);
        lv_obj_set_size(w.chart, LV_PCT(100), LV_PCT(100));
        lv_chart_set_type(w.chart, LV_CHART_TYPE_BAR);
        lv_chart_set_point_count(w.chart, _tfBars());   // 12 monthly / 24 daily-weekly
        lv_chart_set_range(w.chart, LV_CHART_AXIS_PRIMARY_Y, 0, 1000);
        lv_obj_set_style_bg_opa(w.chart, LV_OPA_0, 0);
        lv_obj_set_style_border_width(w.chart, 0, 0);
        lv_obj_clear_flag(w.chart, LV_OBJ_FLAG_CLICKABLE);   // taps fall through to the card
        lv_chart_set_div_line_count(w.chart, 3, 0);   // vdiv MUST be 0 or >= 2 (LVGL div-by-zero)
        lv_chart_set_axis_tick(w.chart, LV_CHART_AXIS_PRIMARY_Y, 4, 0, 4, 1, true, _cols == 2 ? 38 : 52);
        lv_obj_set_style_line_color(w.chart, lv_color_hex(CLR_BORDER), LV_PART_MAIN);
        lv_obj_set_user_data(w.chart, (void*)(intptr_t)idx);
        lv_obj_add_event_cb(w.chart, _candleDrawCb, LV_EVENT_DRAW_PART_BEGIN, this);
        lv_obj_add_event_cb(w.chart, _candleDrawCb, LV_EVENT_DRAW_PART_END,   this);

        w.series = lv_chart_add_series(w.chart, lv_color_hex(CLR_GREEN), LV_CHART_AXIS_PRIMARY_Y);
        _updateChartData(idx);

        // X-axis time legend (24 daily bars, newest on the right).
        lv_obj_t* xRow = lv_obj_create(w.container);
        lv_obj_set_size(xRow, LV_PCT(100), 12);
        lv_obj_set_style_bg_opa(xRow, LV_OPA_0, 0);
        lv_obj_set_style_border_width(xRow, 0, 0);
        lv_obj_set_style_pad_all(xRow, 0, 0);
        lv_obj_set_style_pad_left(xRow, 46, 0);
        lv_obj_set_flex_flow(xRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(xRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(xRow, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(xRow, LV_OBJ_FLAG_CLICKABLE);   // taps fall through to the card
        // Real calendar dates ("Jun 12", "Jun 24", "Jul 5") instead of the old
        // relative "-24d/-12d/now" — 24 daily bars, newest on the right.
        // Anchor the date labels on the NEWEST candle's real timestamp when we
        // have it (falls back to now before the chart has loaded).
        time_t nowT = t.chart_last_ts > 0 ? t.chart_last_ts : time(nullptr);
        int tfG = _tfGroup(), tfB = _tfBars();
        for (int i = 0; i < 3; i++) {
            time_t ts = nowT - (time_t)((tfB - 1) - i * (tfB - 1) / 2) * tfG * 86400;
            struct tm tmv;
            localtime_r(&ts, &tmv);
            char dbuf[12];
            strftime(dbuf, sizeof(dbuf), "%b %d", &tmv);
            lv_obj_t* l = lv_label_create(xRow);
            lv_label_set_text(l, dbuf);
            lv_obj_set_style_text_color(l, lv_color_hex(CLR_MUTED), 0);
            lv_obj_set_style_text_font(l, &lv_font_montserrat_10, 0);
        }
    }

    // Candlestick renderer for the expanded card chart (see comment above).
    static void _candleDrawCb(lv_event_t* e) {
        auto* self = static_cast<TickerScreen*>(lv_event_get_user_data(e));
        lv_obj_t* chart = lv_event_get_current_target(e);
        if (!self || !chart) return;
        int idx = (int)(intptr_t)lv_obj_get_user_data(chart);
        if (idx < 0 || idx >= self->_tickerCount) return;
        TickerEntry& t = self->_tickers[idx];
        CardWidgets& w = self->_cards[idx];

        lv_obj_draw_part_dsc_t* dsc = lv_event_get_draw_part_dsc(e);
        bool hasData = t.chart_loaded && t.chart_count > 0;

        // Y-axis tick labels → real USD prices (values are scaled 0–1000).
        // MUST run even when the chart has no data yet: the old early-return
        // above this block skipped the rewrite and LVGL printed the RAW tick
        // values (0/333/667/1000) — the "Y legends look wrong" bug, visible
        // exactly whenever a chart failed to load. With no data → blank labels.
        if (lv_obj_draw_part_check_type(dsc, &lv_chart_class, LV_CHART_DRAW_PART_TICK_LABEL)) {
            if (dsc->text && dsc->id == LV_CHART_AXIS_PRIMARY_Y) {
                if (!hasData) { dsc->text[0] = '\0'; return; }
                float p = w.chartMin + ((float)dsc->value / 1000.0f) * w.chartRange;
                // MARKET CAP ticks: mcap-per-price factor from the live data
                // (mcap = price × supply → factor = mcap/price). Far more
                // readable than raw micro-cap prices.
                if (t.fdv > 0 && t.price_usd > 0) {
                    double mc = (double)p * ((double)t.fdv / (double)t.price_usd);
                    if      (mc >= 1e9) snprintf(dsc->text, dsc->text_length, "$%.1fB", mc / 1e9);
                    else if (mc >= 1e6) snprintf(dsc->text, dsc->text_length, "$%.0fM", mc / 1e6);
                    else if (mc >= 1e3) snprintf(dsc->text, dsc->text_length, "$%.0fk", mc / 1e3);
                    else                snprintf(dsc->text, dsc->text_length, "$%.0f",  mc);
                }
                else if (p >= 1.0f)   snprintf(dsc->text, dsc->text_length, "$%.2f", p);
                else if (p >= 0.001f) snprintf(dsc->text, dsc->text_length, "$%.4f", p);
                else                  snprintf(dsc->text, dsc->text_length, "%.0e", p);
            }
            return;
        }

        if (!hasData) return;
        if (dsc->part != LV_PART_ITEMS) return;
        uint32_t i = dsc->id;
        if (i >= (uint32_t)t.chart_count) return;
        if (!dsc->draw_area) return;

        lv_area_t ca;
        lv_obj_get_content_coords(chart, &ca);
        lv_coord_t chartH = ca.y2 - ca.y1;
        if (chartH <= 0) return;

        float range = w.chartRange > 1e-12f ? w.chartRange : 1.0f;
        auto scaleY = [&](float v) -> lv_coord_t {
            float s = ((v - w.chartMin) / range) * 1000.0f;
            if (s < 0) s = 0; if (s > 1000) s = 1000;
            return ca.y2 - (lv_coord_t)((int32_t)s * chartH / 1000);
        };

        if (lv_event_get_code(e) == LV_EVENT_DRAW_PART_BEGIN) {
            if (!dsc->rect_dsc) return;
            // Wick: thin grey bar from low to high.
            dsc->rect_dsc->bg_color     = lv_color_hex(0x6a6a6e);
            dsc->rect_dsc->border_width = 0;
            dsc->rect_dsc->radius       = 0;
            dsc->draw_area->y2 = scaleY(t.chart_low[i]);
            lv_coord_t cx = (dsc->draw_area->x1 + dsc->draw_area->x2) / 2;
            dsc->draw_area->x1 = cx;
            dsc->draw_area->x2 = cx + 1;
        } else {
            // Body: colored open→close rect over the wick.
            bool isUp = t.chart_closes[i] >= t.chart_open[i];
            lv_coord_t yO = scaleY(t.chart_open[i]);
            lv_coord_t yC = scaleY(t.chart_closes[i]);
            lv_coord_t top = LV_MIN(yO, yC), bot = LV_MAX(yO, yC);
            if (bot <= top) bot = top + 1;
            lv_coord_t cx = (dsc->draw_area->x1 + dsc->draw_area->x2) / 2;
            lv_area_t body = { (lv_coord_t)(cx - 3), top, (lv_coord_t)(cx + 3), bot };
            if (body.x1 < ca.x1) body.x1 = ca.x1;
            if (body.x2 > ca.x2) body.x2 = ca.x2;
            lv_draw_rect_dsc_t d;
            lv_draw_rect_dsc_init(&d);
            d.bg_color = lv_color_hex(isUp ? CLR_GREEN : CLR_RED);
            d.bg_opa   = LV_OPA_COVER;
            d.radius   = 0;
            lv_draw_rect(dsc->draw_ctx, &d, &body);
        }
    }

    // ── Live data label helpers ────────────────────────────────────────────────

    void _updateFdvLabel(int idx) {
        if (!_cards[idx].fdvLabel) return;
        char buf[16];
        if (_tickers[idx].live_loaded && _tickers[idx].fdv > 0)
            fmtFdv(buf, sizeof(buf), _tickers[idx].fdv);
        else
            snprintf(buf, sizeof(buf), "...");
        lv_label_set_text(_cards[idx].fdvLabel, buf);
    }

    void _updatePriceLabel(int idx) {
        if (!_cards[idx].priceLabel) return;
        char buf[16];
        if (_tickers[idx].live_loaded && _tickers[idx].price_usd > 0)
            fmtPrice(buf, sizeof(buf), _tickers[idx].price_usd);
        else
            snprintf(buf, sizeof(buf), "...");
        lv_label_set_text(_cards[idx].priceLabel, buf);
    }

    void _updateChangeLabel(int idx) {
        if (!_cards[idx].changeLabel) return;
        if (!_tickers[idx].live_loaded) {
            lv_label_set_text(_cards[idx].changeLabel, "...");
            lv_obj_set_style_text_color(_cards[idx].changeLabel, lv_color_hex(CLR_MUTED), 0);
            return;
        }
        float ch = _tickers[idx].change_24h;
        bool up = (ch >= 0.0f);
        char buf[16];
        snprintf(buf, sizeof(buf), "%s%.2f%%", up ? "+" : "", ch);
        lv_label_set_text(_cards[idx].changeLabel, buf);
        lv_obj_set_style_text_color(_cards[idx].changeLabel, lv_color_hex(up ? CLR_GREEN : CLR_RED), 0);
    }

    void _updateChartData(int idx) {
        CardWidgets& w = _cards[idx];
        TickerEntry&  t = _tickers[idx];
        if (!w.chart || !w.series) return;

        int pc = (int)lv_chart_get_point_count(w.chart);
        int nPts = min((int)t.chart_count, pc);
        if (!t.chart_loaded || t.chart_count == 0) {
            // Fill with sentinel (no data)
            for (int i = 0; i < pc; i++)
                w.series->y_points[i] = LV_CHART_POINT_NONE;
        } else if (t.is_expanded) {
            // Candles: scale over the full LOW..HIGH span; the bar carries the
            // HIGH (wick top) and _candleDrawCb draws wick + body from OHLC.
            float minV = t.chart_low[0], maxV = t.chart_high[0];
            for (int i = 1; i < t.chart_count; i++) {
                if (t.chart_low[i]  < minV) minV = t.chart_low[i];
                if (t.chart_high[i] > maxV) maxV = t.chart_high[i];
            }
            w.chartMin   = minV;
            w.chartRange = (maxV - minV) < 1e-12f ? 1.0f : (maxV - minV);
            lv_chart_set_range(w.chart, LV_CHART_AXIS_PRIMARY_Y, 0, 1000);
            for (int i = 0; i < nPts; i++)
                w.series->y_points[i] = (lv_coord_t)(((t.chart_high[i] - minV) / w.chartRange) * 1000.0f);
            for (int i = nPts; i < pc; i++)
                w.series->y_points[i] = LV_CHART_POINT_NONE;
        } else {
            // Compact sparkline: line of closes.
            float minV = t.chart_closes[0], maxV = t.chart_closes[0];
            for (int i = 1; i < t.chart_count; i++) {
                if (t.chart_closes[i] < minV) minV = t.chart_closes[i];
                if (t.chart_closes[i] > maxV) maxV = t.chart_closes[i];
            }
            float range = (maxV - minV) < 1e-10f ? 1.0f : (maxV - minV);
            lv_chart_set_range(w.chart, LV_CHART_AXIS_PRIMARY_Y, 0, 1000);
            for (int i = 0; i < nPts; i++)
                w.series->y_points[i] = (lv_coord_t)(((t.chart_closes[i] - minV) / range) * 1000.0f);
            for (int i = nPts; i < pc; i++)
                w.series->y_points[i] = LV_CHART_POINT_NONE;
        }
        lv_chart_refresh(w.chart);
    }

    // ── Search dialog ─────────────────────────────────────────────────────────

    void _openSearchDialog() {
        // Full-screen overlay
        _searchOverlay = lv_obj_create(lv_scr_act());
        lv_obj_set_size(_searchOverlay, SCREEN_WIDTH, SCREEN_HEIGHT);
        lv_obj_align(_searchOverlay, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_set_style_bg_color(_searchOverlay, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(_searchOverlay, LV_OPA_90, 0);
        lv_obj_set_style_border_width(_searchOverlay, 0, 0);
        lv_obj_set_style_pad_all(_searchOverlay, 0, 0);
        lv_obj_clear_flag(_searchOverlay, LV_OBJ_FLAG_SCROLLABLE);

        // Panel for search content
        lv_obj_t* panel = lv_obj_create(_searchOverlay);
        lv_obj_set_size(panel, SCREEN_WIDTH - 24, SCREEN_HEIGHT - 40);
        lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_color(panel, lv_color_hex(CLR_CARD), 0);
        lv_obj_set_style_border_color(panel, lv_color_hex(CLR_BORDER), 0);
        lv_obj_set_style_border_width(panel, 1, 0);
        lv_obj_set_style_radius(panel, 10, 0);
        lv_obj_set_style_pad_all(panel, 10, 0);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(panel, 6, 0);

        // Header row: "Add Ticker" title + X close button
        lv_obj_t* dlgHeader = lv_obj_create(panel);
        lv_obj_set_size(dlgHeader, LV_PCT(100), 28);
        lv_obj_set_style_bg_opa(dlgHeader, LV_OPA_0, 0);
        lv_obj_set_style_border_width(dlgHeader, 0, 0);
        lv_obj_set_style_pad_all(dlgHeader, 0, 0);

        lv_obj_t* dlgTitle = lv_label_create(dlgHeader);
        lv_label_set_text(dlgTitle, "Add Ticker");
        lv_obj_set_style_text_font(dlgTitle, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(dlgTitle, lv_color_hex(CLR_TEXT), 0);
        lv_obj_align(dlgTitle, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t* closeBtn = lv_btn_create(dlgHeader);
        lv_obj_set_size(closeBtn, 26, 26);
        lv_obj_set_style_bg_color(closeBtn, lv_color_hex(CLR_SURFACE), 0);
        lv_obj_set_style_border_color(closeBtn, lv_color_hex(CLR_BORDER), 0);
        lv_obj_set_style_border_width(closeBtn, 1, 0);
        lv_obj_set_style_radius(closeBtn, 6, 0);
        lv_obj_set_style_pad_all(closeBtn, 0, 0);
        lv_obj_align(closeBtn, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(closeBtn, _onSearchClose, LV_EVENT_CLICKED, this);
        lv_obj_t* closeLbl = lv_label_create(closeBtn);
        lv_label_set_text(closeLbl, LV_SYMBOL_CLOSE);
        lv_obj_set_style_text_font(closeLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(closeLbl, lv_color_hex(CLR_MUTED), 0);
        lv_obj_center(closeLbl);

        // Text area for query
        _searchTA = lv_textarea_create(panel);
        lv_obj_set_size(_searchTA, LV_PCT(100), 36);
        lv_textarea_set_placeholder_text(_searchTA, "Token name, symbol or 0x contract...");
        lv_textarea_set_one_line(_searchTA, true);
        lv_obj_set_style_text_font(_searchTA, &lv_font_montserrat_12, 0);
        lv_obj_set_style_bg_color(_searchTA, lv_color_hex(CLR_SURFACE), 0);
        lv_obj_set_style_border_color(_searchTA, lv_color_hex(CLR_GREEN), LV_STATE_FOCUSED);
        lv_obj_set_style_border_width(_searchTA, 1, 0);
        lv_obj_set_style_radius(_searchTA, 6, 0);
        lv_obj_add_event_cb(_searchTA, _onSearchSubmit, LV_EVENT_READY, this);

        // Results container (scrollable)
        _searchResultsCont = lv_obj_create(panel);
        lv_obj_set_size(_searchResultsCont, LV_PCT(100), 120);
        lv_obj_set_style_bg_opa(_searchResultsCont, LV_OPA_0, 0);
        lv_obj_set_style_border_width(_searchResultsCont, 0, 0);
        lv_obj_set_style_pad_all(_searchResultsCont, 0, 0);
        lv_obj_set_flex_flow(_searchResultsCont, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(_searchResultsCont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(_searchResultsCont, 4, 0);

        _searchSpinner = lv_spinner_create(panel, 1000, 60);
        lv_obj_set_size(_searchSpinner, 32, 32);
        lv_obj_set_style_arc_color(_searchSpinner, lv_color_hex(CLR_GREEN), LV_PART_INDICATOR);
        lv_obj_add_flag(_searchSpinner, LV_OBJ_FLAG_HIDDEN);

        // Keyboard
        _searchKB = lv_keyboard_create(_searchOverlay);
        lv_obj_set_size(_searchKB, SCREEN_WIDTH, 170);
        lv_obj_align(_searchKB, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(_searchKB, lv_color_hex(CLR_SURFACE), 0);
        lv_keyboard_set_textarea(_searchKB, _searchTA);
        lv_obj_add_event_cb(_searchKB, _onSearchKbReady, LV_EVENT_READY, this);
    }

    void _populateSearchResults() {
        if (!_searchResultsCont) return;
        lv_obj_clean(_searchResultsCont);

        if (_searchResultCount == 0) {
            lv_obj_t* noRes = lv_label_create(_searchResultsCont);
            lv_label_set_text(noRes, "No results found.");
            lv_obj_set_style_text_font(noRes, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(noRes, lv_color_hex(CLR_MUTED), 0);
            return;
        }

        for (int i = 0; i < _searchResultCount; i++) {
            SearchResultEntry& r = _searchResults[i];

            lv_obj_t* row = lv_obj_create(_searchResultsCont);
            lv_obj_set_size(row, LV_PCT(100), 36);
            lv_obj_set_style_bg_color(row, lv_color_hex(CLR_SURFACE), 0);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_style_radius(row, 6, 0);
            lv_obj_set_style_pad_hor(row, 8, 0);
            lv_obj_set_style_pad_ver(row, 4, 0);
            lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_user_data(row, (void*)(intptr_t)i);
            lv_obj_add_event_cb(row, _onResultSelected, LV_EVENT_CLICKED, this);

            lv_obj_t* symLbl = lv_label_create(row);
            char lineA[32];
            snprintf(lineA, sizeof(lineA), "%s/%s", r.base_symbol, r.quote_symbol);
            lv_label_set_text(symLbl, lineA);
            lv_obj_set_style_text_font(symLbl, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(symLbl, lv_color_hex(CLR_TEXT), 0);
            lv_obj_align(symLbl, LV_ALIGN_LEFT_MID, 0, -6);

            lv_obj_t* nameLbl = lv_label_create(row);
            char chainInfo[32];
            snprintf(chainInfo, sizeof(chainInfo), "%s", r.chain_id);
            lv_label_set_text(nameLbl, chainInfo);
            lv_obj_set_style_text_font(nameLbl, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(nameLbl, lv_color_hex(CLR_MUTED), 0);
            lv_obj_align(nameLbl, LV_ALIGN_LEFT_MID, 0, 8);

            // Price on right
            lv_obj_t* priceLbl = lv_label_create(row);
            char prBuf[16];
            fmtPrice(prBuf, sizeof(prBuf), r.price_usd);
            lv_label_set_text(priceLbl, prBuf);
            lv_obj_set_style_text_font(priceLbl, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(priceLbl, lv_color_hex(CLR_GREEN), 0);
            lv_obj_align(priceLbl, LV_ALIGN_RIGHT_MID, 0, 0);

            // Pair liquidity left of the price ("$850k" / "$12M") — the main
            // signal for picking the right pool among same-symbol results.
            lv_obj_t* liqLbl = lv_label_create(row);
            char liqBuf[16];
            fmtLiq(liqBuf, sizeof(liqBuf), r.liquidity_usd);
            lv_label_set_text(liqLbl, liqBuf);
            lv_obj_set_style_text_font(liqLbl, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(liqLbl, lv_color_hex(CLR_MUTED), 0);
            lv_obj_align(liqLbl, LV_ALIGN_RIGHT_MID, -78, 0);
        }
    }

    void _closeSearchDialog() {
        if (_searchOverlay) {
            lv_obj_del(_searchOverlay);
            _searchOverlay    = nullptr;
            _searchTA         = nullptr;
            _searchKB         = nullptr;
            _searchResultsCont= nullptr;
            _searchSpinner    = nullptr;
        }
    }

    // ── Async task dispatcher ─────────────────────────────────────────────────

    void _dispatchTask(TickerTaskType type, int tickerIdx = -1) {
        // NEVER vTaskDelete() a live worker here. Killing a task mid-HTTPS
        // leaves the WiFiClientSecure/TLS context (~45 KB of internal RAM) and
        // the lwIP socket permanently leaked — a few repeated searches were
        // enough to exhaust internal heap and reboot the device (which showed
        // up as "random" resets, e.g. while swiping to the Debt screen).
        // The worker self-clears _bgTask when it finishes; until then, new
        // requests are simply dropped (the in-flight one will report via
        // _pending, so spinners still resolve).
        if (_bgTask) return;
        TickerTaskPayload* payload = new TickerTaskPayload();
        payload->type         = type;
        payload->ticker_index = tickerIdx;
        strncpy(payload->node_code, _nodeCode, sizeof(payload->node_code) - 1);

        if (type == TT_SEARCH && _searchTA) {
            const char* q = lv_textarea_get_text(_searchTA);
            strncpy(payload->query, q ? q : "", sizeof(payload->query) - 1);
        }

        xTaskCreatePinnedToCore(
            _bgTaskFn, "ticker_bg", 8192, payload, 1, (TaskHandle_t*)&_bgTask, 0
        );
    }

    void _dispatchAdd(int searchResultIdx) {
        _dispatchAddEntry(_searchResults[searchResultIdx]);
    }
    void _dispatchAddEntry(const SearchResultEntry& e) {
        if (_bgTask) { _pendingAdd = e; _pendingAddSet = true; return; }   // queue, retried in poll
        TickerTaskPayload* payload = new TickerTaskPayload();
        payload->type    = TT_ADD;
        strncpy(payload->node_code, _nodeCode, sizeof(payload->node_code) - 1);
        payload->to_add  = e;
        xTaskCreatePinnedToCore(_bgTaskFn, "ticker_add", 8192, payload, 1, (TaskHandle_t*)&_bgTask, 0);
    }

    // Persists the current on-screen order to the backend (fire-and-forget:
    // if the worker is busy or the function isn't deployed, the order still
    // applies locally until the next list reload).
    void _dispatchReorder() {
        if (_bgTask) { _pendingReorderSet = true; return; }   // queue, retried in poll
        TickerTaskPayload* payload = new TickerTaskPayload();
        payload->type = TT_REORDER;
        strncpy(payload->node_code, _nodeCode, sizeof(payload->node_code) - 1);
        payload->pools_count = _tickerCount;
        for (int i = 0; i < _tickerCount && i < TICKER_MAX; i++)
            strncpy(payload->pools_ordered[i], _tickers[i].pool_address, sizeof(payload->pools_ordered[i]) - 1);
        xTaskCreatePinnedToCore(_bgTaskFn, "ticker_ord", 8192, payload, 1, (TaskHandle_t*)&_bgTask, 0);
    }

    void _dispatchRemove(int tickerIdx) {
        _dispatchRemovePool(_tickers[tickerIdx].pool_address);
    }
    void _dispatchRemovePool(const char* pool) {
        if (_bgTask) {   // queue, retried in poll — otherwise the server keeps the
                         // ticker and it reappears on the next list reload
            strncpy(_pendingRemovePool, pool, sizeof(_pendingRemovePool) - 1);
            _pendingRemoveSet = true;
            return;
        }
        TickerTaskPayload* payload = new TickerTaskPayload();
        payload->type         = TT_REMOVE;
        strncpy(payload->node_code,       _nodeCode, sizeof(payload->node_code) - 1);
        strncpy(payload->pool_to_remove,  pool,      sizeof(payload->pool_to_remove) - 1);
        xTaskCreatePinnedToCore(_bgTaskFn, "ticker_rm", 8192, payload, 1, (TaskHandle_t*)&_bgTask, 0);
    }

    // ── Static FreeRTOS task ──────────────────────────────────────────────────

    static void _bgTaskFn(void* pvArg) {
        // NOTE: This task runs on core 0; LVGL must only be touched from core 1
        // (the main Arduino loop). All results are written to _pending and applied
        // via the LVGL timer callback _onPollTimer below, which runs on core 1.
        TickerTaskPayload* p = static_cast<TickerTaskPayload*>(pvArg);

        // We need access to the TickerScreen instance. We use a static pointer
        // set in _dispatchTask — safe because only one TickerScreen exists and
        // tasks are serialised (previous task deleted before new one is spawned).
        TickerScreen* self = s_instance;
        if (!self) { delete p; vTaskDelete(nullptr); return; }

        // Short re-validation only — pollPending's dispatch chain already
        // gates task creation on netTlsRamOk(), so this task only exists when
        // there WAS headroom moments ago. (Long waits inside a live task pin
        // its 8 KB stack and starve everyone else — the 0.2.2 lesson.)
        if (!netWaitTlsRam(3000))
            Log.println("tickers: TLS RAM wait timed out — trying anyway");
        netLock();   // exclusive TLS ownership for this whole job — see net_lock.h

        switch (p->type) {

            case TT_LOAD_LIST: {
                // GET node_ticker_config from Supabase REST
                String url = String(ENDPOINT_TICKER_CONFIG) + p->node_code +
                             "&order=display_order.asc";
                HTTPClient http;
                http.useHTTP10(true);   // body parsed from getStream(): HTTP/1.1 would arrive chunked and break the JSON parser
                http.begin(url);
                http.addHeader("apikey", SUPABASE_ANON_KEY);
                http.addHeader("Authorization", String("Bearer ") + SUPABASE_ANON_KEY);
                int code = http.GET();
                if (code == 200) {
                    // Snapshot the WHOLE old entries so a list reload keeps
                    // everything already fetched (prices, charts, decoded
                    // logos). Re-entering the screen used to throw all of it
                    // away and re-download ticker by ticker — slow, and with
                    // 6+ tickers the later ones often never finished.
                    // (~7 KB, too big for this task's stack; safe because list
                    // loads are serialised on one worker. PSRAM — was an
                    // internal-BSS function-static, RAM that TLS needed.)
                    static TickerEntry* oldEntries = nullptr;
                    if (!oldEntries) oldEntries = (TickerEntry*)heap_caps_malloc(
                            sizeof(TickerEntry) * TICKER_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                    if (!oldEntries) oldEntries = (TickerEntry*)malloc(sizeof(TickerEntry) * TICKER_MAX);
                    if (!oldEntries) { http.end(); break; }   // OOM — keep current list, retry later
                    int oldCount = self->_tickerCount;
                    for (int i = 0; i < oldCount; i++) oldEntries[i] = self->_tickers[i];

                    JsonDocument doc;
                    deserializeJson(doc, http.getStream());
                    JsonArray arr = doc.as<JsonArray>();
                    int n = 0;
                    for (JsonObject obj : arr) {
                        if (n >= TICKER_MAX) break;
                        TickerEntry& te = self->_tickers[n];
                        memset(&te, 0, sizeof(te));
                        const char* pool = obj["pool_address"] | "";
                        // Carry over the cached entry for this pool, if any.
                        for (int j = 0; j < oldCount; j++) {
                            if (strcasecmp(oldEntries[j].pool_address, pool) == 0) {
                                te = oldEntries[j];   // keeps is_expanded too —
                                // several charts may stay open at once now
                                break;
                            }
                        }
                        strncpy(te.pool_address, pool,                       sizeof(te.pool_address)-1);
                        strncpy(te.chain_id,     obj["chain_id"]     | "",   sizeof(te.chain_id)-1);
                        strncpy(te.base_symbol,  obj["base_symbol"]  | "",   sizeof(te.base_symbol)-1);
                        strncpy(te.base_name,    obj["base_name"]    | "",   sizeof(te.base_name)-1);
                        strncpy(te.quote_symbol, obj["quote_symbol"] | "USD", sizeof(te.quote_symbol)-1);
                        te.logo_applied = false;   // new cards → re-attach
                        // Expansion = the pool-matched CARRY-OVER (line above kept
                        // te.is_expanded from the old snapshot) OR the pool-keyed
                        // set. Using ONLY the set overwrote a correctly-carried-over
                        // TRUE with a stale/empty set → open charts collapsed on a
                        // background reload (log: expanded=[..E...] → [......]).
                        // OR-ing both keeps a chart open if EITHER source says so.
                        te.is_expanded = te.is_expanded || self->_isExpandedPool(pool);
                        n++;
                    }
                    self->_tickerCount = n;
                    self->_pending.type = PR_LIST_LOADED;
                } else {
                    // Was completely silent — a failed boot-time list load looked
                    // like "the tickers screen just never loads".
                    Log.printf("tickers: list HTTP %d\n", code);
                }
                http.end();

                // Refresh strategy (all cache-aware — instant when re-entering
                // the screen within the TTLs):
                //  • Live prices: ONE batched DexScreener request per chain
                //    (it accepts comma-separated pool addresses) instead of a
                //    TLS handshake per ticker. TTL 2 min.
                //  • Logos: only for tickers that still lack one.
                //  • Charts: per pool (GeckoTerminal has no batch). TTL 15 min.
                _fetchLiveBatch(self);
                for (int i = 0; i < self->_tickerCount; i++) {
                    if (self->_searchRequested) break;   // user is waiting — yield
                    TickerEntry& te = self->_tickers[i];
                    if (te.logo_url[0] && !te.logo_ready) _fetchLogo(self, i);
                }
                for (int i = 0; i < self->_tickerCount; i++) {
                    if (self->_searchRequested) break;   // user is waiting — yield
                    if (millis() < self->_gtCooldownUntil) break;   // 429 backoff — retry next pass
                    TickerEntry& te = self->_tickers[i];
                    // chart_at == 0 → forced by the manual refresh button (the
                    // plain age test misses it while uptime < the 15-min TTL).
                    if (!te.chart_loaded || te.chart_at == 0 ||
                        millis() - te.chart_at > 15UL * 60UL * 1000UL) {
                        TickerScreen::_fetchChart(self, i);
                        // Breathe between candle fetches: GeckoTerminal's free
                        // tier is ~30 req/min and 6+ back-to-back calls got
                        // rate-limited (429) — silently, before the log above.
                        vTaskDelay(pdMS_TO_TICKS(350));
                    }
                }
                // If we yielded to a search, resume the remaining work after it.
                if (self->_searchRequested) self->_listReloadRequested = true;
                break;
            }

            case TT_LOAD_LIVE: {
                _fetchLive(self, p->ticker_index);
                break;
            }

            case TT_LOAD_CHART: {
                _fetchChart(self, p->ticker_index);
                break;
            }

            case TT_SEARCH: {
                // Detect contract address paste: "0x" + exactly 40 hex chars (42 total).
                // Route these directly to DexScreener /tokens/{address} instead of
                // calling the Supabase search-tokens Edge Function.
                const char* q = p->query;
                size_t qlen = strlen(q);
                bool isContractAddr = (qlen >= 42 && qlen <= 44  // allow for any trailing whitespace
                                       && strncmp(q, "0x", 2) == 0);
                // Verify it's all hex after the prefix
                if (isContractAddr) {
                    for (size_t ci = 2; ci < 42; ci++) {
                        char c = q[ci];
                        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                            isContractAddr = false;
                            break;
                        }
                    }
                }

                if (isContractAddr) {
                    // DexScreener /latest/dex/tokens/{address} returns pairs for this token
                    // across all chains, sorted by liquidity. We pick the top 12.
                    char contractAddr[45] = {};
                    strncpy(contractAddr, q, 42);  // trim to exactly 42 chars
                    String url = String(ENDPOINT_DEXSCREENER_TOKENS) + contractAddr;
                    HTTPClient http;
                    http.useHTTP10(true);   // DexScreener replies chunked on HTTP/1.1 — this broke ALL searches
                    http.begin(url);
                    http.setTimeout(8000);
                    int code = http.GET();
                    if (code == 200) {
                        JsonDocument doc;
                        deserializeJson(doc, http.getStream());
                        JsonArray pairs = doc["pairs"].as<JsonArray>();
                        int n = 0;
                        for (JsonObject pair : pairs) {
                            if (n >= 12) break;
                            // Skip pairs with no price or very low liquidity (< $1 k)
                            float liq = pair["liquidity"]["usd"] | 0.0f;
                            if (liq < 1000.0f) continue;
                            SearchResultEntry& sr = self->_searchResults[n];
                            memset(&sr, 0, sizeof(sr));
                            strncpy(sr.pair_address,  pair["pairAddress"]            | "", sizeof(sr.pair_address)-1);
                            strncpy(sr.chain_id,      pair["chainId"]                | "", sizeof(sr.chain_id)-1);
                            strncpy(sr.base_symbol,   pair["baseToken"]["symbol"]    | "", sizeof(sr.base_symbol)-1);
                            strncpy(sr.base_name,     pair["baseToken"]["name"]      | "", sizeof(sr.base_name)-1);
                            strncpy(sr.quote_symbol,  pair["quoteToken"]["symbol"]   | "", sizeof(sr.quote_symbol)-1);
                            const char* priceStr = pair["priceUsd"] | "0";
                            sr.price_usd     = atof(priceStr);
                            sr.liquidity_usd = liq;
                            sr.change_24h    = pair["priceChange"]["h24"] | 0.0f;
                            n++;
                        }
                        self->_searchResultCount = n;
                    }
                    // Always mark done so the spinner hides even on error/no-result.
                    self->_pending.type = PR_SEARCH_DONE;
                    http.end();
                } else {
                    // Name/symbol search via DexScreener's public search API — no
                    // backend needed (the Supabase search-tokens function isn't a
                    // hard dependency). Returns pairs across all chains ordered by
                    // relevance/liquidity; we keep the top 12 with real liquidity so
                    // dust/scam clones sink. This is why "LFI", "BNKR", etc. now
                    // resolve where the (undeployed) Edge Function returned nothing.
                    String query = q;
                    query.replace(" ", "%20");
                    String url = String(ENDPOINT_DEXSCREENER_SEARCH) + query;
                    HTTPClient http;
                    http.useHTTP10(true);   // DexScreener replies chunked on HTTP/1.1 — this broke ALL searches
                    http.begin(url);
                    http.setTimeout(8000);
                    int code = http.GET();
                    Log.printf("search[%s]: HTTP %d\n", query.c_str(), code);
                    if (code == 200) {
                        // DexScreener /search returns ~30 pairs, each a big object
                        // (txns, volume, fdv, socials…). Parsing all of that in the
                        // background task used enough heap to risk an out-of-memory
                        // reset. A filter keeps ONLY the handful of fields we use,
                        // so the parsed document stays tiny. ([0] applies to every
                        // element of the "pairs" array.)
                        JsonDocument filter;
                        filter["pairs"][0]["chainId"]              = true;
                        filter["pairs"][0]["pairAddress"]          = true;
                        filter["pairs"][0]["priceUsd"]             = true;
                        filter["pairs"][0]["liquidity"]["usd"]     = true;
                        filter["pairs"][0]["priceChange"]["h24"]   = true;
                        filter["pairs"][0]["baseToken"]["symbol"]  = true;
                        filter["pairs"][0]["baseToken"]["name"]    = true;
                        filter["pairs"][0]["quoteToken"]["symbol"] = true;

                        JsonDocument doc;
                        DeserializationError err = deserializeJson(doc, http.getStream(),
                                                                   DeserializationOption::Filter(filter));
                        if (!err) {
                            JsonArray pairs = doc["pairs"].as<JsonArray>();
                            int n = 0;
                            for (JsonObject pair : pairs) {
                                if (n >= 12) break;
                                float liq = pair["liquidity"]["usd"] | 0.0f;
                                if (liq < 1000.0f) continue;   // skip dust/scam clones
                                SearchResultEntry& sr = self->_searchResults[n];
                                memset(&sr, 0, sizeof(sr));
                                strncpy(sr.pair_address,  pair["pairAddress"]          | "", sizeof(sr.pair_address)-1);
                                strncpy(sr.chain_id,      pair["chainId"]              | "", sizeof(sr.chain_id)-1);
                                strncpy(sr.base_symbol,   pair["baseToken"]["symbol"]  | "", sizeof(sr.base_symbol)-1);
                                strncpy(sr.base_name,     pair["baseToken"]["name"]    | "", sizeof(sr.base_name)-1);
                                strncpy(sr.quote_symbol,  pair["quoteToken"]["symbol"] | "", sizeof(sr.quote_symbol)-1);
                                const char* priceStr = pair["priceUsd"] | "0";
                                sr.price_usd     = atof(priceStr);
                                sr.liquidity_usd = liq;
                                sr.change_24h    = pair["priceChange"]["h24"] | 0.0f;
                                n++;
                            }
                            self->_searchResultCount = n;
                            Log.printf("search[%s]: %d results kept\n", query.c_str(), n);
                        } else {
                            Log.printf("search[%s]: JSON parse error %s\n", query.c_str(), err.c_str());
                        }
                    }
                    // Always mark done so the spinner hides even on error/no-result.
                    self->_pending.type = PR_SEARCH_DONE;
                    http.end();
                }
                break;
            }

            case TT_ADD: {
                HTTPClient http;
                http.begin(ENDPOINT_ADD_TICKER);
                http.addHeader("Content-Type", "application/json");
                http.addHeader("Authorization", String("Bearer ") + SUPABASE_ANON_KEY);
                JsonDocument req;
                req["node_code"]    = p->node_code;
                req["pool_address"] = p->to_add.pair_address;
                req["chain_id"]     = p->to_add.chain_id;
                req["base_symbol"]  = p->to_add.base_symbol;
                req["base_name"]    = p->to_add.base_name;
                req["quote_symbol"] = p->to_add.quote_symbol;
                String body;
                serializeJson(req, body);
                int code = http.POST(body);
                if (code == 200 || code == 201) {
                    self->_pending.type = PR_ADD_DONE;
                }
                http.end();
                break;
            }

            case TT_REORDER: {
                HTTPClient http;
                http.begin(ENDPOINT_REORDER_TICKERS);
                http.addHeader("Content-Type", "application/json");
                http.addHeader("Authorization", String("Bearer ") + SUPABASE_ANON_KEY);
                JsonDocument req;
                req["node_code"] = p->node_code;
                JsonArray pools = req["pool_addresses"].to<JsonArray>();
                for (int i = 0; i < p->pools_count; i++) pools.add(p->pools_ordered[i]);
                String body;
                serializeJson(req, body);
                http.POST(body);   // best-effort; no UI feedback needed
                http.end();
                break;
            }

            case TT_REMOVE: {
                HTTPClient http;
                http.begin(ENDPOINT_REMOVE_TICKER);
                http.addHeader("Content-Type", "application/json");
                http.addHeader("Authorization", String("Bearer ") + SUPABASE_ANON_KEY);
                JsonDocument req;
                req["node_code"]    = p->node_code;
                req["pool_address"] = p->pool_to_remove;
                String body;
                serializeJson(req, body);
                int code = http.POST(body);
                if (code == 200 || code == 204) {
                    self->_pending.type = PR_REMOVE_DONE;
                }
                http.end();
                break;
            }
        }

        netUnlock();
        delete p;
        self->_bgTask = nullptr;
        vTaskDelete(nullptr);
    }

    // Fetches DAILY OHLC candles from GeckoTerminal for one ticker's chart
    // (called from the bg task). Sets te.chart_dirty; pollPending applies the
    // data to whichever card widget exists on the UI thread.
    static void _fetchChart(TickerScreen* self, int idx) {
        TickerEntry& te = self->_tickers[idx];
        const char* net = chainToGT(te.chain_id);
        // Capture the TF generation on entry: if the user switches timeframe
        // while this fetch is in flight, the result below is DISCARDED instead
        // of overwriting the (already invalidated) cache with old-TF candles
        // and re-marking it loaded — which made the follow-up reload skip it.
        uint16_t gen = self->_tfGen;
        // GT only serves day/hour/minute → 1W/1M candles are aggregated here
        // from daily bars (group = 7 or 30 days per candle).
        int g = self->_tfGroup(), bars = self->_tfBars();
        int want = bars * g;   // 24 / 168 / 360 daily rows
        String url = String(ENDPOINT_GECKOTERMINAL_OHLCV) + net +
                     "/pools/" + te.pool_address +
                     "/ohlcv/day?aggregate=1&limit=" + want + "&currency=usd&token=base";
        HTTPClient http;
        http.useHTTP10(true);   // avoid chunked encoding — parsed from getStream()
        http.begin(url);
        http.addHeader("Accept", "application/json");
        int code = http.GET();
        if (code == 200) {
            JsonDocument doc;
            deserializeJson(doc, http.getStream());
            JsonArray ohlcv = doc["data"]["attributes"]["ohlcv_list"].as<JsonArray>();
            if (gen != self->_tfGen) {   // timeframe changed mid-fetch → stale result
                Log.printf("tickers: chart [%s] discarded (timeframe changed mid-fetch)\n", te.base_symbol);
                http.end();
                return;
            }
            // ohlcv_list rows are [ts, o, h, l, c, v], NEWEST first. Group g
            // consecutive days into one candle, emit oldest-first.
            int total = min((int)ohlcv.size(), want);
            int buckets = (total + g - 1) / g;
            if (buckets > bars) buckets = bars;
            if (buckets > CHART_BARS) buckets = CHART_BARS;
            if (buckets == 0) {
                // A 200 with an EMPTY ohlcv_list (rate-limited/new pool) used to
                // set chart_loaded=true with 0 candles — an empty card that the
                // 15-min TTL then protected from every refetch, and that even
                // the refresh button couldn't recover. Leave it NOT loaded so
                // the chart_want queue / next list pass retries.
                Log.printf("tickers: chart [%s] HTTP 200 but 0 candles — will retry\n", te.base_symbol);
                http.end();
                return;
            }
            for (int j = 0; j < buckets; j++) {
                int lo = j * g;                        // newest day of this group
                int hi = min(total, (j + 1) * g) - 1;  // oldest day of this group
                int n2 = buckets - 1 - j;              // oldest-first output slot
                te.chart_closes[n2] = ohlcv[lo][4].as<float>();
                te.chart_open  [n2] = ohlcv[hi][1].as<float>();
                float hh = 0.0f, ll = 0.0f;
                for (int k = lo; k <= hi; k++) {
                    float h2 = ohlcv[k][2].as<float>(), l2 = ohlcv[k][3].as<float>();
                    if (h2 > hh) hh = h2;
                    if (ll == 0.0f || (l2 > 0.0f && l2 < ll)) ll = l2;
                }
                te.chart_high[n2] = hh;
                te.chart_low [n2] = ll;
            }
            te.chart_count  = buckets;
            // Newest candle's timestamp (row 0, field 0 — seconds) so the X-axis
            // labels reflect the real data dates instead of "now" (which lied
            // whenever GeckoTerminal's data was a day stale).
            if (ohlcv.size() > 0) te.chart_last_ts = (time_t)(ohlcv[0][0].as<long long>());
            te.chart_loaded = true;
            te.chart_at     = millis();
            te.chart_dirty  = true;   // pollPending redraws on core 1
        } else {
            // GeckoTerminal failures were completely silent (no log, no retry
            // path) — "the mini charts just never load". 429 = rate limit
            // (free tier ~30 req/min): back off ALL chart fetches for 20 s,
            // retrying instantly just burns more of the quota.
            Log.printf("tickers: chart [%s] HTTP %d\n", te.base_symbol, code);
            if (code == 429) self->_gtCooldownUntil = millis() + 20000UL;
        }
        http.end();
    }

    // ONE DexScreener request per chain for all tickers whose live data is
    // stale (>2 min) — /latest/dex/pairs/{chain}/{a},{b},{c} accepts up to 30
    // comma-separated pool addresses. Cuts 6 sequential TLS handshakes down
    // to 1-2, which is why the screen used to crawl ticker by ticker.
    static void _fetchLiveBatch(TickerScreen* self) {
        bool needed[TICKER_MAX] = {};
        // baseToken address per ticker index (for the logo fallback below).
        // static (task stack is small) but cleared per call — stale entries
        // would map to the wrong ticker after a reorder.
        static char baseAddrs[TICKER_MAX][68];
        memset(baseAddrs, 0, sizeof(baseAddrs));
        for (int i = 0; i < self->_tickerCount; i++) {
            TickerEntry& te = self->_tickers[i];
            needed[i] = !te.live_loaded || millis() - te.live_at > 2UL * 60UL * 1000UL;
        }

        for (int i = 0; i < self->_tickerCount; i++) {
            if (!needed[i]) continue;
            // Build one request for every still-needed ticker on this chain.
            const char* chain = self->_tickers[i].chain_id;
            String url = String(ENDPOINT_DEXSCREENER_PAIRS) + chain + "/";
            bool first = true;
            for (int j = i; j < self->_tickerCount; j++) {
                if (!needed[j] || strcmp(self->_tickers[j].chain_id, chain) != 0) continue;
                if (!first) url += ",";
                url += self->_tickers[j].pool_address;
                first = false;
            }

            HTTPClient http;
            http.useHTTP10(true);
            http.begin(url);
            http.setTimeout(9000);
            int liveCode = http.GET();
            if (liveCode != 200)
                // A silently failed batch left cards without price/mcap/change
                // (and without logo_url, so the logo pass skipped them too) —
                // the "VVV shows no market data above its chart" case.
                Log.printf("tickers: live batch [%s] HTTP %d\n", chain, liveCode);
            if (liveCode == 200) {
                JsonDocument filter;
                filter["pairs"][0]["pairAddress"]          = true;
                filter["pairs"][0]["priceUsd"]             = true;
                filter["pairs"][0]["priceChange"]["h24"]   = true;
                filter["pairs"][0]["marketCap"]            = true;
                filter["pairs"][0]["fdv"]                  = true;
                filter["pairs"][0]["info"]["imageUrl"]     = true;
                filter["pairs"][0]["baseToken"]["address"] = true;
                JsonDocument doc;
                if (deserializeJson(doc, http.getStream(),
                                    DeserializationOption::Filter(filter)) == DeserializationError::Ok) {
                    for (JsonObject pair : doc["pairs"].as<JsonArray>()) {
                        const char* addr = pair["pairAddress"] | "";
                        for (int j = 0; j < self->_tickerCount; j++) {
                            TickerEntry& te = self->_tickers[j];
                            if (!needed[j] || strcasecmp(te.pool_address, addr) != 0) continue;
                            te.price_usd  = atof(pair["priceUsd"] | "0");
                            te.change_24h = pair["priceChange"]["h24"] | 0.0f;
                            te.fdv        = pair["marketCap"] | (pair["fdv"] | 0.0f);
                            if (!te.logo_url[0])
                                strncpy(te.logo_url, pair["info"]["imageUrl"] | "", sizeof(te.logo_url)-1);
                            strncpy(baseAddrs[j], pair["baseToken"]["address"] | "", sizeof(baseAddrs[j])-1);
                            te.live_loaded = true;
                            te.live_at     = millis();
                            te.live_dirty  = true;   // pollPending updates the labels
                            needed[j] = false;
                            break;
                        }
                    }
                }
            }
            http.end();

            // Logo-URL fallback for pools whose batched entry came WITHOUT
            // `info` (some pools omit it even when the token has a logo on
            // other pools — this was CLAWD's case): scan the token's OTHER
            // pairs for any imageUrl. The single-ticker path already did
            // this; the batch path silently skipped it.
            for (int j = 0; j < self->_tickerCount; j++) {
                TickerEntry& te = self->_tickers[j];
                if (te.logo_url[0] || te.logo_ready || !baseAddrs[j][0]) continue;
                HTTPClient th;
                th.useHTTP10(true);
                th.begin(String(ENDPOINT_DEXSCREENER_TOKENS) + baseAddrs[j]);
                th.setTimeout(8000);
                if (th.GET() == 200) {
                    JsonDocument f2;
                    f2["pairs"][0]["info"]["imageUrl"] = true;
                    JsonDocument d2;
                    if (deserializeJson(d2, th.getStream(),
                                        DeserializationOption::Filter(f2)) == DeserializationError::Ok) {
                        for (JsonObject pr : d2["pairs"].as<JsonArray>()) {
                            const char* iu = pr["info"]["imageUrl"] | "";
                            if (iu[0]) { strncpy(te.logo_url, iu, sizeof(te.logo_url)-1); break; }
                        }
                    }
                }
                th.end();
                if (!te.logo_url[0])
                    Log.printf("logo[%s] no imageUrl anywhere (token %s)\n", te.base_symbol, baseAddrs[j]);
            }
        }

        // Safety net for the first-ticker-never-loads bug: the batched pairs
        // endpoint occasionally returns a response that OMITS one of the
        // requested pools (in practice almost always the FIRST ticker). That
        // left needed[j] == true, and since the outer loop had already passed
        // index j it was never retried until the next full list reload — so the
        // card sat blank for a long time while every other ticker loaded fine.
        // Retry any still-unfilled ticker individually, in THIS same pass.
        for (int j = 0; j < self->_tickerCount; j++) {
            if (!needed[j]) continue;
            Log.printf("tickers: batch missed [%s] — individual retry\n", self->_tickers[j].base_symbol);
            _fetchLive(self, j);
            self->_tickers[j].live_dirty = true;   // pollPending refreshes the labels
        }
    }

    // Fetches live price/FDV from DexScreener for one ticker (called from bg task)
    static void _fetchLive(TickerScreen* self, int idx) {
        TickerEntry& te = self->_tickers[idx];
        String url = String(ENDPOINT_DEXSCREENER_PAIRS) +
                     te.chain_id + "/" + te.pool_address;
        HTTPClient http;
        http.useHTTP10(true);   // avoid chunked encoding — parsed from getStream()
        http.begin(url);
        int code = http.GET();
        char baseTokenAddr[68] = {};
        if (code == 200) {
            JsonDocument doc;
            deserializeJson(doc, http.getStream());
            JsonObject pair = doc["pairs"][0];
            if (!pair.isNull()) {
                te.price_usd  = atof(pair["priceUsd"] | "0");
                te.change_24h = pair["priceChange"]["h24"] | 0.0f;
                // Market cap preferred; FDV only as fallback when DexScreener
                // doesn't report a circulating-supply-based cap for the token.
                te.fdv        = pair["marketCap"] | (pair["fdv"] | 0.0f);
                strncpy(te.logo_url, pair["info"]["imageUrl"] | "", sizeof(te.logo_url)-1);
                strncpy(baseTokenAddr, pair["baseToken"]["address"] | "", sizeof(baseTokenAddr)-1);
                te.live_loaded = true;
                te.live_at     = millis();
                self->_pending.type        = PR_LIVE_LOADED;
                self->_pending.tickerIndex = idx;
            }
        }
        http.end();

        // Some pools don't carry `info.imageUrl` even when the token HAS a
        // logo on other pools (this is why e.g. CLAWD showed no logo while LFI
        // did). Fallback: ask the tokens endpoint for ALL of the token's pairs
        // and take the first imageUrl any of them provides.
        if (!te.logo_url[0] && baseTokenAddr[0] && !te.logo_ready) {
            HTTPClient th;
            th.useHTTP10(true);
            th.begin(String(ENDPOINT_DEXSCREENER_TOKENS) + baseTokenAddr);
            th.setTimeout(8000);
            if (th.GET() == 200) {
                JsonDocument filter;
                filter["pairs"][0]["info"]["imageUrl"] = true;
                JsonDocument doc;
                if (deserializeJson(doc, th.getStream(),
                                    DeserializationOption::Filter(filter)) == DeserializationError::Ok) {
                    for (JsonObject pr : doc["pairs"].as<JsonArray>()) {
                        const char* iu = pr["info"]["imageUrl"] | "";
                        if (iu[0]) { strncpy(te.logo_url, iu, sizeof(te.logo_url)-1); break; }
                    }
                }
            }
            th.end();
        }

        // Token logo: download + decode once (skipped if already have it).
        if (te.logo_url[0] && !te.logo_ready) _fetchLogo(self, idx);
    }

    // ── tjpgd glue: decode a baseline JPEG from RAM into an RGB888 buffer ──
    struct JpegCtx {
        const uint8_t* in;      // compressed JPEG
        size_t   inSize;
        size_t   inPos;
        uint8_t* rgb;           // decoded RGB888, w*h*3 (allocated by caller)
        uint16_t w, h;
    };
    static size_t _jpegIn(JDEC* jd, uint8_t* buf, size_t len) {
        JpegCtx* c = (JpegCtx*)jd->device;
        if (len > c->inSize - c->inPos) len = c->inSize - c->inPos;
        if (buf) memcpy(buf, c->in + c->inPos, len);
        c->inPos += len;
        return len;
    }
    static int _jpegOut(JDEC* jd, void* bitmap, JRECT* rect) {
        JpegCtx* c = (JpegCtx*)jd->device;
        const uint8_t* src = (const uint8_t*)bitmap;   // RGB888 (JD_FORMAT 0)
        for (int y = rect->top; y <= rect->bottom; y++) {
            for (int x = rect->left; x <= rect->right; x++) {
                if (x < c->w && y < c->h)
                    memcpy(&c->rgb[(y * c->w + x) * 3], src, 3);
                src += 3;
            }
        }
        return 1;   // continue decompression
    }

    // Downloads the token logo (DexScreener info.imageUrl), decodes it (PNG
    // via lodepng or baseline JPEG via tjpgd — the CDN serves JPEG even when
    // asked for PNG), and downscales to a ready-to-blit 40×40
    // LV_IMG_CF_TRUE_COLOR_ALPHA bitmap. Doing the decode+scale ONCE here in
    // the bg task (instead of registering LVGL image decoders) means zero
    // per-frame decode cost on the RGB panel and a tiny fixed footprint
    // (40×40×3 B ≈ 4.7 KB per ticker). Runs on core 0; hands the finished
    // bitmap to the UI via te.logo_ready (applied by pollPending on core 1).
    static void _fetchLogo(TickerScreen* self, int idx) {
        TickerEntry& te = self->_tickers[idx];

        // Disk cache first: the finished 40×40 bitmap (4.8 KB) persists on the
        // LittleFS partition, so reboots/re-flashes skip the CDN entirely.
        {
            size_t n = 0;
            uint8_t* blob = diskcache::loadAlloc("logo", te.logo_url, &n);
            if (blob && n == 40 * 40 * 3) {
                te.logo_dsc.header.always_zero = 0;
                te.logo_dsc.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
                te.logo_dsc.header.w  = 40;
                te.logo_dsc.header.h  = 40;
                te.logo_dsc.data_size = 40 * 40 * 3;
                te.logo_dsc.data      = blob;
                te.logo_px            = blob;
                te.logo_ready         = true;   // pollPending picks this up on core 1
                return;
            }
            if (blob) free(blob);   // wrong size → stale format, refetch
        }

        // Strip the original query (it requests 800×800) and try small CDN
        // variants. 64×64@q80 first; if the DECODER rejects that encode
        // (some variants use asymmetric chroma sampling tjpgd can't parse —
        // CLAWD's case, "jd_prepare failed"), 128×128@q80 re-encodes with
        // standard 4:2:0 and decodes fine.
        String base = te.logo_url;
        int q = base.indexOf('?');
        if (q >= 0) base = base.substring(0, q);
        if (_tryLogoVariant(self, idx, base + "?width=64&height=64&quality=80")) return;
        Log.printf("logo[%s] retrying at 128x128\n", te.base_symbol);
        _tryLogoVariant(self, idx, base + "?width=128&height=128&quality=80");
    }

    // One download+decode+downscale attempt. Returns true when te.logo_ready.
    static bool _tryLogoVariant(TickerScreen* self, int idx, const String& url) {
        TickerEntry& te = self->_tickers[idx];

        HTTPClient http;
        http.useHTTP10(true);
        http.begin(url);
        http.setTimeout(9000);
        int lcode = http.GET();
        if (lcode != 200) {
            Log.printf("logo[%s] GET %d url=%s\n", te.base_symbol, lcode, url.c_str());
            http.end();
            return false;
        }

        // Read the PNG body (Content-Length if present, else until close).
        const size_t PNG_CAP = 200 * 1024;
        int declared = http.getSize();
        if (declared > (int)PNG_CAP) { http.end(); return false; }
        uint8_t* png = (uint8_t*)malloc(declared > 0 ? declared : PNG_CAP);
        if (!png) { http.end(); return false; }
        size_t pngLen = 0;
        WiFiClient* s = http.getStreamPtr();
        uint32_t lastData = millis();
        while ((http.connected() || s->available()) && millis() - lastData < 5000) {
            size_t avail = s->available();
            if (!avail) { delay(5); continue; }
            size_t cap = (declared > 0 ? (size_t)declared : PNG_CAP) - pngLen;
            if (!cap) break;
            int got = s->readBytes(png + pngLen, min(avail, cap));
            if (got > 0) { pngLen += got; lastData = millis(); }
            if (declared > 0 && pngLen >= (size_t)declared) break;
        }
        http.end();
        if (pngLen < 8) {
            Log.printf("logo[%s] body too short (%u bytes)\n", te.base_symbol, (unsigned)pngLen);
            free(png);
            return false;
        }

        // Decode to RGBA8888 (rgba, iw×ih) — format picked by magic bytes.
        unsigned char* rgba = nullptr;
        unsigned iw = 0, ih = 0;
        bool rgbaFromLvMem = false;   // lodepng allocates via lv_mem_alloc

        if (png[0] == 0x89 && png[1] == 0x50) {
            // PNG
            unsigned rc = lodepng_decode32(&rgba, &iw, &ih, png, pngLen);
            free(png);
            if (rc != 0 || !rgba || iw == 0 || ih == 0) { if (rgba) lv_mem_free(rgba); return false; }
            rgbaFromLvMem = true;
        } else if (png[0] == 0xFF && png[1] == 0xD8) {
            // Baseline JPEG via tjpgd (what DexScreener's CDN actually sends).
            uint8_t* work = (uint8_t*)malloc(4096);   // tjpgd workspace (needs ~3.1 KB)
            if (!work) { Log.printf("logo[%s] no mem for tjpgd work\n", te.base_symbol); free(png); return false; }
            JDEC jd;
            JpegCtx ctx{ png, pngLen, 0, nullptr, 0, 0 };
            if (jd_prepare(&jd, _jpegIn, work, 4096, &ctx) != JDR_OK) { Log.printf("logo[%s] jd_prepare failed\n", te.base_symbol); free(work); free(png); return false; }
            ctx.w = jd.width; ctx.h = jd.height;
            if (ctx.w == 0 || ctx.h == 0 || (uint32_t)ctx.w * ctx.h > 512u * 512u) { Log.printf("logo[%s] bad dims %ux%u\n", te.base_symbol, ctx.w, ctx.h); free(work); free(png); return false; }
            ctx.rgb = (uint8_t*)heap_caps_malloc((uint32_t)ctx.w * ctx.h * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!ctx.rgb) ctx.rgb = (uint8_t*)malloc((uint32_t)ctx.w * ctx.h * 3);
            if (!ctx.rgb) { Log.printf("logo[%s] no mem for rgb\n", te.base_symbol); free(work); free(png); return false; }
            JRESULT dr = jd_decomp(&jd, _jpegOut, 0);
            free(work);
            free(png);
            if (dr != JDR_OK) {
                Log.printf("logo[%s] tjpgd decomp failed rc=%d (%ux%u)\n", te.base_symbol, (int)dr, ctx.w, ctx.h);
                free(ctx.rgb);
                return false;
            }
            // Expand RGB888 → RGBA8888 (alpha 255) so the scaler below is shared.
            iw = ctx.w; ih = ctx.h;
            rgba = (unsigned char*)heap_caps_malloc((uint32_t)iw * ih * 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!rgba) rgba = (unsigned char*)malloc((uint32_t)iw * ih * 4);
            if (!rgba) { Log.printf("logo[%s] no mem for rgba\n", te.base_symbol); free(ctx.rgb); return false; }
            for (uint32_t i = 0; i < (uint32_t)iw * ih; i++) {
                rgba[i * 4 + 0] = ctx.rgb[i * 3 + 0];
                rgba[i * 4 + 1] = ctx.rgb[i * 3 + 1];
                rgba[i * 4 + 2] = ctx.rgb[i * 3 + 2];
                rgba[i * 4 + 3] = 255;
            }
            free(ctx.rgb);
        } else {
            // webp/unknown — no decoder on-device; keep the letter fallback.
            free(png);
            return false;
        }

        // Nearest-neighbour downscale to 40×40, RGBA8888 → RGB565+A8 (LVGL
        // TRUE_COLOR_ALPHA at LV_COLOR_DEPTH 16: [lo, hi, alpha] per pixel).
        const int W = 40, H = 40;
        uint8_t* px = (uint8_t*)heap_caps_malloc(W * H * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!px) px = (uint8_t*)malloc(W * H * 3);
        if (!px) { Log.printf("logo[%s] no mem for px\n", te.base_symbol); if (rgbaFromLvMem) lv_mem_free(rgba); else free(rgba); return false; }
        for (int y = 0; y < H; y++) {
            unsigned sy = (unsigned)((uint64_t)y * ih / H);
            for (int x = 0; x < W; x++) {
                unsigned sx = (unsigned)((uint64_t)x * iw / W);
                const unsigned char* sp = &rgba[(sy * iw + sx) * 4];
                uint16_t c565 = ((sp[0] & 0xF8) << 8) | ((sp[1] & 0xFC) << 3) | (sp[2] >> 3);
                uint8_t* dp = &px[(y * W + x) * 3];
                dp[0] = c565 & 0xFF;
                dp[1] = c565 >> 8;
                dp[2] = sp[3];
            }
        }
        if (rgbaFromLvMem) lv_mem_free(rgba); else free(rgba);

        te.logo_dsc.header.always_zero = 0;
        te.logo_dsc.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
        te.logo_dsc.header.w  = W;
        te.logo_dsc.header.h  = H;
        te.logo_dsc.data_size = W * H * 3;
        te.logo_dsc.data      = px;
        te.logo_px            = px;
        te.logo_ready         = true;   // pollPending picks this up on core 1
        diskcache::save("logo", te.logo_url, px, W * H * 3);   // survive reboots
        Log.printf("logo[%s] READY (%ux%u -> 40x40)\n", te.base_symbol, iw, ih);
        return true;
    }

    // ── Event callbacks (static, forwarded to instance) ───────────────────────

    static void _onAddBtnTapped(lv_event_t* e) {
        auto* self = static_cast<TickerScreen*>(lv_event_get_user_data(e));
        if (self) self->_openSearchDialog();
    }

    // NOTE for every callback below: NEVER call _rebuildTickerCards() from
    // here. These run inside the event dispatch of a widget the rebuild would
    // lv_obj_del() — deleting the object mid-event corrupts LVGL and crashes
    // later. They set _rebuildRequested instead; pollPending does the work.

    static void _onCardTapped(lv_event_t* e) {
        auto* self = static_cast<TickerScreen*>(lv_event_get_user_data(e));
        lv_obj_t* obj = lv_event_get_current_target(e);
        int idx = (int)(intptr_t)lv_obj_get_user_data(obj);
        if (!self || idx < 0 || idx >= self->_tickerCount) return;
        // A finger that travelled >TAP_SLOP_PX before release was a SWIPE that
        // merely started on this card — don't also toggle the chart open/closed.
        if (g_touchWasSwipe()) return;
        if (self->_editMode) return;   // taps don't expand while editing

        self->_tickers[idx].is_expanded = !self->_tickers[idx].is_expanded;
        self->_setExpandedPool(self->_tickers[idx].pool_address, self->_tickers[idx].is_expanded);
        Log.printf("tickers: tap %s -> %s\n", self->_tickers[idx].base_symbol,
                      self->_tickers[idx].is_expanded ? "EXPAND" : "collapse");
        if (self->_tickers[idx].is_expanded && !self->_tickers[idx].chart_loaded) {
            self->_tickers[idx].chart_want = true;   // queued — retried until it loads
        }
        self->_rebuildRequested = true;
    }

    // Timeframe dropdown changed. All cached chart data is the OLD timeframe
    // now, so invalidate everything; the deferred rebuild recreates the cards
    // (X labels + point count) and the bg task refetches with the new grouping.
    static void _onTfChanged(lv_event_t* e) {
        auto* self = static_cast<TickerScreen*>(lv_event_get_user_data(e));
        lv_obj_t* dd = lv_event_get_current_target(e);
        if (!self || !dd) return;
        uint8_t sel = (uint8_t)lv_dropdown_get_selected(dd);
        if (sel == self->_chartTf) return;
        self->_chartTf = sel;
        self->_tfGen++;   // in-flight fetches with the old timeframe discard their result
        for (int i = 0; i < self->_tickerCount; i++)
            self->_tickers[i].chart_loaded = false;
        // The card whose dropdown changed goes first (visible card, user waiting).
        int ddIdx = (int)(intptr_t)lv_obj_get_user_data(dd);
        if (ddIdx >= 0 && ddIdx < self->_tickerCount)
            self->_tickers[ddIdx].chart_want = true;
        self->_rebuildRequested = true;   // pollPending rebuilds outside this callback
        // Every card's chart is now the OLD timeframe — trigger a full list
        // reload so ALL charts refetch with the new grouping, not just the one
        // whose dropdown changed (the others used to go blank until re-entry).
        self->_listReloadRequested = true;
    }

    static void _onCollapseTapped(lv_event_t* e) {
        lv_event_stop_bubbling(e);   // don't also toggle via _onCardTapped
        auto* self = static_cast<TickerScreen*>(lv_event_get_user_data(e));
        lv_obj_t* obj = lv_event_get_current_target(e);
        int idx = (int)(intptr_t)lv_obj_get_user_data(obj);
        if (!self || idx < 0 || idx >= self->_tickerCount) return;
        self->_tickers[idx].is_expanded = false;
        self->_setExpandedPool(self->_tickers[idx].pool_address, false);
        Log.printf("tickers: X-collapse %s\n", self->_tickers[idx].base_symbol);
        self->_rebuildRequested = true;
    }

    static void _onEditBtnTapped(lv_event_t* e) {
        auto* self = static_cast<TickerScreen*>(lv_event_get_user_data(e));
        if (!self) return;
        self->_editMode = !self->_editMode;   // no colour change — cells show the mode
        // Collapse everything when entering edit mode — reordering wants the
        // uniform compact rows.
        if (self->_editMode) {
            for (int i = 0; i < self->_tickerCount; i++) self->_tickers[i].is_expanded = false;
            self->_clearExpandedPools();
        }
        self->_rebuildRequested = true;
    }

    static void _onMoveUpTapped(lv_event_t* e)   { _moveTicker(e, -1); }
    static void _onMoveDownTapped(lv_event_t* e) { _moveTicker(e, +1); }

    static void _moveTicker(lv_event_t* e, int dir) {
        lv_event_stop_bubbling(e);
        auto* self = static_cast<TickerScreen*>(lv_event_get_user_data(e));
        lv_obj_t* obj = lv_event_get_current_target(e);
        int idx = (int)(intptr_t)lv_obj_get_user_data(obj);
        if (!self || idx < 0 || idx >= self->_tickerCount) return;
        int to = idx + dir;
        if (to < 0 || to >= self->_tickerCount) return;
        TickerEntry tmp = self->_tickers[idx];
        self->_tickers[idx] = self->_tickers[to];
        self->_tickers[to]  = tmp;
        self->_rebuildRequested = true;
        self->_dispatchReorder();   // persist new display_order (best-effort)
    }

    static void _onDeleteTapped(lv_event_t* e) {
        lv_event_stop_bubbling(e);
        auto* self = static_cast<TickerScreen*>(lv_event_get_user_data(e));
        lv_obj_t* obj = lv_event_get_current_target(e);
        int idx = (int)(intptr_t)lv_obj_get_user_data(obj);
        if (!self || idx < 0 || idx >= self->_tickerCount) return;
        self->_dispatchRemove(idx);
        // Reclaim the removed ticker's logo bitmap (the array shift below would
        // otherwise overwrite the pointer and leak ~4.7 KB PSRAM per delete).
        // Deferred: freed in pollPending AFTER the card tree is rebuilt, so we
        // never free a bitmap an lv_img is still drawing.
        if (self->_tickers[idx].logo_px && self->_logoFreeCount < TICKER_MAX)
            self->_logoFreeList[self->_logoFreeCount++] = self->_tickers[idx].logo_px;
        // Optimistic UI: remove from local array immediately
        for (int i = idx; i < self->_tickerCount - 1; i++)
            self->_tickers[i] = self->_tickers[i + 1];
        self->_tickerCount--;
        self->_rebuildRequested = true;
    }

    static void _onSearchClose(lv_event_t* e) {
        auto* self = static_cast<TickerScreen*>(lv_event_get_user_data(e));
        if (self) self->_closeSearchDialog();
    }

    static void _onSearchSubmit(lv_event_t* e) {
        auto* self = static_cast<TickerScreen*>(lv_event_get_user_data(e));
        if (!self || !self->_searchSpinner) return;
        lv_obj_clear_flag(self->_searchSpinner, LV_OBJ_FLAG_HIDDEN);
        if (self->_searchResultsCont) lv_obj_clean(self->_searchResultsCont);
        self->_searchResultCount = 0;
        // Queued like the list reload: a direct dispatch was dropped while the
        // worker was busy, which surfaced as searches that never returned.
        self->_searchRequested = true;
    }

    static void _onSearchKbReady(lv_event_t* e) {
        // Keyboard "ok" key fires READY — same as pressing enter in textarea
        _onSearchSubmit(e);
    }

    static void _onResultSelected(lv_event_t* e) {
        auto* self = static_cast<TickerScreen*>(lv_event_get_user_data(e));
        lv_obj_t* obj = lv_event_get_current_target(e);   // the row the cb is on, not a child label
        int idx = (int)(intptr_t)lv_obj_get_user_data(obj);
        if (!self || idx < 0 || idx >= self->_searchResultCount) return;
        self->_closeSearchDialog();
        self->_dispatchAdd(idx);
    }

    // ── LVGL timer: apply pending results on the main thread ──────────────────
    // Call this from your main loop (or a lv_timer) once per tick, AFTER
    // acquiring the LVGL mutex. Pattern: lv_timer_create(_pollPending, 100, this)
public:
    static void pollPending(lv_timer_t* timer) {
        {
            auto* s0 = static_cast<TickerScreen*>(timer->user_data);
            if (s0 && s0->_body && !s0->_rebuildRequested)
                s0->_savedScrollY = lv_obj_get_scroll_y(s0->_body);
        }
        auto* self = static_cast<TickerScreen*>(timer->user_data);
        if (!self) return;

        // Deferred card-tree rebuild (requested by event callbacks — doing it
        // there would delete the widget that was dispatching the event).
        if (self->_rebuildRequested) {
            self->_rebuildRequested = false;
            self->_rebuildTickerCards();
            // Now that the old cards (and their lv_img logos) are gone, it's safe
            // to free the bitmaps of any removed/replaced tickers.
            for (int i = 0; i < self->_logoFreeCount; i++)
                if (self->_logoFreeList[i]) { free(self->_logoFreeList[i]); self->_logoFreeList[i] = nullptr; }
            self->_logoFreeCount = 0;
            // Chart fetches are NOT dispatched here anymore: a dispatch while the
            // worker was busy was silently dropped (the "expand → chart never
            // loads" bug). chart_want entries are serviced by the queued-work
            // chain below, retried until the data actually arrives.
        }

        // Attach any freshly downloaded token logos to their cards.
        for (int i = 0; i < self->_tickerCount; i++) {
            if (self->_tickers[i].logo_ready && !self->_tickers[i].logo_applied)
                self->_applyLogoIfReady(i);
        }

        // Apply freshly fetched chart data (compact sparklines now load right
        // after the list does — they used to stay empty until first expand).
        for (int i = 0; i < self->_tickerCount; i++) {
            if (self->_tickers[i].chart_dirty) {
                self->_tickers[i].chart_dirty = false;
                self->_updateChartData(i);
            }
        }

        // Apply fresh live prices from the batched fetch.
        for (int i = 0; i < self->_tickerCount; i++) {
            if (self->_tickers[i].live_dirty) {
                self->_tickers[i].live_dirty = false;
                self->_updateFdvLabel(i);
                self->_updatePriceLabel(i);
                self->_updateChangeLabel(i);
                // The expanded chart's Y-axis ticks are derived from fdv/price
                // (mcap-per-price factor) — repaint it so the legend tracks the
                // fresh live data instead of showing the previous factor (or raw
                // prices from before the first live load).
                if (self->_tickers[i].is_expanded && self->_cards[i].chart)
                    lv_obj_invalidate(self->_cards[i].chart);
            }
        }

        // SELF-HEAL: while any card is still missing live data, its logo or its
        // chart (a fetch failed — RAM jam, DexScreener/GT hiccup, rate limit),
        // queue another list pass. TTL-aware fetches make the retry cheap: only
        // the missing pieces are re-requested. Growing backoff (30 s → 8 min)
        // so a genuinely dead pool can't hammer the APIs forever.
        {
            static uint32_t lastHealAt   = 0;
            static uint8_t  healAttempts = 0;
            bool incomplete = false;
            for (int i = 0; i < self->_tickerCount; i++) {
                TickerEntry& te = self->_tickers[i];
                if (!te.live_loaded || !te.chart_loaded ||
                    (te.logo_url[0] && !te.logo_ready)) { incomplete = true; break; }
            }
            if (!incomplete) {
                healAttempts = 0;   // everything present — reset the backoff
            } else if (self->_loadedOnce && !self->_bgTask && !self->_listReloadRequested) {
                uint32_t backoff = 30000UL << (healAttempts > 4 ? 4 : healAttempts);
                if (lastHealAt == 0) {
                    lastHealAt = millis();   // ARM only — firing immediately after the
                                             // first incomplete pass re-hit GeckoTerminal
                                             // within seconds and earned a 429
                } else if (millis() - lastHealAt > backoff) {
                    lastHealAt = millis();
                    if (healAttempts < 250) healAttempts++;
                    Log.printf("tickers: self-heal reload (attempt %u)\n", healAttempts);
                    self->_listReloadRequested = true;
                }
            }
        }

        // Queued work: retried every tick until the worker is free. Priority:
        // search (user waiting) → mutations (add/remove/reorder must reach the
        // server before a list reload, or the reload undoes them) → list reload.
        // canSpawn ALSO requires TLS RAM headroom (net_lock.h): spawning a
        // worker that can't complete a handshake just burns an 8 KB stack and
        // a silent -1 — the queued flags stay set and retry when RAM returns.
        bool canSpawn = !self->_bgTask && netTlsRamOk();
        if (self->_searchRequested && canSpawn) {
            if (self->_searchOverlay && self->_searchTA) {
                self->_searchRequested = false;
                self->_dispatchTask(TT_SEARCH);
            } else {
                self->_searchRequested = false;   // dialog closed meanwhile — cancel
            }
        }
        else if (self->_pendingAddSet && canSpawn) {
            self->_pendingAddSet = false;
            self->_dispatchAddEntry(self->_pendingAdd);
        }
        else if (self->_pendingRemoveSet && canSpawn) {
            self->_pendingRemoveSet = false;
            self->_dispatchRemovePool(self->_pendingRemovePool);
        }
        else if (self->_pendingReorderSet && canSpawn) {
            self->_pendingReorderSet = false;
            self->_dispatchReorder();
        }
        else if (self->_listReloadRequested && canSpawn) {
            self->_listReloadRequested = false;
            self->_dispatchTask(TT_LOAD_LIST);
        }
        else if (canSpawn) {
            // Individual chart loads (expand / TF change). LOWEST priority: a
            // queued list reload refetches every chart anyway. chart_want stays
            // set until the data really lands, so a failed fetch is retried —
            // throttled so a persistently failing pool can't hammer
            // GeckoTerminal (free tier ~30 req/min).
            static uint32_t lastChartDispatchAt = 0;
            for (int i = 0; i < self->_tickerCount; i++) {
                TickerEntry& te = self->_tickers[i];
                if (!te.chart_want) continue;
                if (te.chart_loaded) { te.chart_want = false; continue; }   // satisfied
                if (millis() < self->_gtCooldownUntil) break;   // 429 backoff
                if (millis() - lastChartDispatchAt < 5000 && lastChartDispatchAt != 0) break;
                lastChartDispatchAt = millis();
                self->_dispatchTask(TT_LOAD_CHART, i);
                break;   // one at a time — the worker is serialised anyway
            }
        }

        if (self->_pending.type == PR_NONE) return;

        PendingResultType type = self->_pending.type;
        int               tidx = self->_pending.tickerIndex;
        self->_pending.type = PR_NONE;   // consume

        switch (type) {
            case PR_LIST_LOADED:
                self->_loadedOnce = true;   // real data has arrived at least once
                lv_obj_add_flag(self->_spinner, LV_OBJ_FLAG_HIDDEN);
                self->_rebuildTickerCards();
                break;
            case PR_LIVE_LOADED:
                if (tidx >= 0 && tidx < self->_tickerCount) {
                    self->_updateFdvLabel(tidx);
                    self->_updatePriceLabel(tidx);
                    self->_updateChangeLabel(tidx);
                }
                break;
            case PR_CHART_LOADED:
                if (tidx >= 0 && tidx < self->_tickerCount) {
                    self->_updateChartData(tidx);
                }
                break;
            case PR_SEARCH_DONE:
                if (self->_searchSpinner)
                    lv_obj_add_flag(self->_searchSpinner, LV_OBJ_FLAG_HIDDEN);
                self->_populateSearchResults();
                break;
            case PR_ADD_DONE:
                // Reload full list from server to reflect the new ticker.
                // Via the retry flag: the ADD worker may not have exited yet,
                // in which case a direct dispatch would be silently dropped.
                self->_listReloadRequested = true;
                break;
            case PR_REMOVE_DONE:
                // List was already updated optimistically in _onRemoveTapped
                break;
            default: break;
        }
    }

    // Static instance pointer used by FreeRTOS task to reach the TickerScreen
    // (set by ui_manager when it instantiates this screen)
    inline static TickerScreen* s_instance = nullptr;  // C++17 inline static

private:
};

// s_instance is declared as inline static above (C++17). No separate
// definition needed in a .cpp file. ui_manager.h sets it in buildAllScreens().
