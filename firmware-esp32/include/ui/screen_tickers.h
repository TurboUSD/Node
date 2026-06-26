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

// ── Brand colours (match web palette) ─────────────────────────────────────────
#define CLR_GREEN    0x43e397
#define CLR_RED      0xff6b6b
#define CLR_BLUE     0x5b8dee
#define CLR_YELLOW   0xffcf72
#define CLR_BG       0x000000
#define CLR_CARD     0x0c0c0c
#define CLR_SURFACE  0x141414
#define CLR_BORDER   0x1c1c1c
#define CLR_TEXT     0xe8e8e8
#define CLR_MUTED    0x6e7280

// ── Constants ──────────────────────────────────────────────────────────────────
#define TICKER_MAX          10
#define COMPACT_H           62      // px height of collapsed ticker card
#define EXPANDED_H          196     // px height of expanded ticker card
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
    float chart_closes[CHART_BARS] = {};
    int   chart_count      = 0;
    bool  live_loaded      = false;
    bool  chart_loaded     = false;
    bool  is_expanded      = false;
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
enum TickerTaskType { TT_LOAD_LIST, TT_LOAD_LIVE, TT_LOAD_CHART, TT_SEARCH, TT_ADD, TT_REMOVE };

struct TickerTaskPayload {
    TickerTaskType type;
    char node_code[8];
    int  ticker_index;           // for TT_LOAD_LIVE / TT_LOAD_CHART
    char query[64];              // for TT_SEARCH
    SearchResultEntry to_add;    // for TT_ADD
    char pool_to_remove[68];     // for TT_REMOVE
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

        // Title row: "TICKERS" label + "+ Add" button
        _titleRow = lv_obj_create(_body);
        lv_obj_set_size(_titleRow, LV_PCT(100), 28);
        lv_obj_set_style_bg_opa(_titleRow, LV_OPA_0, 0);
        lv_obj_set_style_border_width(_titleRow, 0, 0);
        lv_obj_set_style_pad_all(_titleRow, 0, 0);
        lv_obj_clear_flag(_titleRow, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* titleLbl = lv_label_create(_titleRow);
        lv_label_set_text(titleLbl, "TICKERS");
        lv_obj_set_style_text_color(titleLbl, lv_color_hex(CLR_MUTED), 0);
        lv_obj_set_style_text_font(titleLbl, &lv_font_montserrat_10, 0);
        lv_obj_align(titleLbl, LV_ALIGN_LEFT_MID, 0, 0);

        _addBtn = lv_btn_create(_titleRow);
        lv_obj_set_size(_addBtn, 52, 22);
        lv_obj_set_style_bg_color(_addBtn, lv_color_hex(CLR_SURFACE), 0);
        lv_obj_set_style_border_color(_addBtn, lv_color_hex(CLR_BORDER), 0);
        lv_obj_set_style_border_width(_addBtn, 1, 0);
        lv_obj_set_style_radius(_addBtn, 6, 0);
        lv_obj_set_style_pad_all(_addBtn, 2, 0);
        lv_obj_align(_addBtn, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(_addBtn, _onAddBtnTapped, LV_EVENT_CLICKED, this);
        lv_obj_t* addLbl = lv_label_create(_addBtn);
        lv_label_set_text(addLbl, "+ Add");
        lv_obj_set_style_text_font(addLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(addLbl, lv_color_hex(CLR_GREEN), 0);
        lv_obj_center(addLbl);

        // Placeholder shown when no tickers are loaded yet
        _emptyLabel = lv_label_create(_body);
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
        _rebuildTickerCards();
        _dispatchTask(TT_LOAD_LIST);
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
    lv_obj_t*        _titleRow    = nullptr;
    lv_obj_t*        _addBtn      = nullptr;
    lv_obj_t*        _emptyLabel  = nullptr;
    lv_obj_t*        _spinner     = nullptr;
    void*            _userData    = nullptr;
    char             _nodeCode[8] = {};

    TickerEntry      _tickers[TICKER_MAX];
    int              _tickerCount = 0;

    // Per-ticker card widgets (rebuilt whenever list changes)
    struct CardWidgets {
        lv_obj_t* container      = nullptr;
        lv_obj_t* symbolLabel    = nullptr;   // compact: symbol text block
        lv_obj_t* nameLabel      = nullptr;   // compact
        lv_obj_t* fdvLabel       = nullptr;   // compact
        lv_obj_t* changeLabel    = nullptr;   // compact
        lv_obj_t* chart          = nullptr;   // compact sparkline / expanded chart
        lv_obj_t* expandedPanel  = nullptr;   // shown only when expanded
        lv_obj_t* priceLabel     = nullptr;   // expanded
        lv_obj_t* removeBtn      = nullptr;
        lv_chart_series_t* series = nullptr;
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

    // FreeRTOS async task
    TaskHandle_t      _bgTask    = nullptr;
    SemaphoreHandle_t _dataMutex = nullptr;

    // Pending result from background task (written under mutex, read on timer cb)
    enum PendingResultType { PR_NONE, PR_LIST_LOADED, PR_LIVE_LOADED, PR_CHART_LOADED,
                             PR_SEARCH_DONE, PR_ADD_DONE, PR_REMOVE_DONE };
    struct PendingResult {
        PendingResultType type = PR_NONE;
        int  tickerIndex = -1;
    } _pending;

    // ── Card building ─────────────────────────────────────────────────────────

    void _rebuildTickerCards() {
        // Delete all existing ticker card objects
        for (int i = 0; i < TICKER_MAX; i++) {
            if (_cards[i].container) {
                lv_obj_del(_cards[i].container);
                _cards[i] = {};
            }
        }

        bool hasAny = (_tickerCount > 0);
        if (hasAny) {
            lv_obj_add_flag(_emptyLabel, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(_emptyLabel, LV_OBJ_FLAG_HIDDEN);
        }

        for (int i = 0; i < _tickerCount; i++) {
            _buildCard(i);
        }

        // Hide add button if at max capacity
        if (_tickerCount >= TICKER_MAX) {
            lv_obj_add_flag(_addBtn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(_addBtn, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void _buildCard(int idx) {
        TickerEntry& t = _tickers[idx];
        CardWidgets& w = _cards[idx];

        bool exp = t.is_expanded;
        lv_coord_t cardH = exp ? EXPANDED_H : COMPACT_H;

        // Card container
        w.container = lv_obj_create(_body);
        lv_obj_set_size(w.container, LV_PCT(100), cardH);
        lv_obj_set_style_bg_color(w.container, lv_color_hex(CLR_CARD), 0);
        lv_obj_set_style_border_color(w.container, lv_color_hex(CLR_BORDER), 0);
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

    void _buildCompactCard(int idx) {
        TickerEntry& t = _tickers[idx];
        CardWidgets& w = _cards[idx];
        lv_coord_t innerW = SCREEN_WIDTH - 24 - 20;  // body padding + card padding

        // Symbol circle (colored background pill)
        lv_obj_t* symBg = lv_obj_create(w.container);
        lv_obj_set_size(symBg, 40, 40);
        lv_obj_set_style_bg_color(symBg, lv_color_hex(CLR_SURFACE), 0);
        lv_obj_set_style_border_color(symBg, lv_color_hex(CLR_BORDER), 0);
        lv_obj_set_style_border_width(symBg, 1, 0);
        lv_obj_set_style_radius(symBg, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_pad_all(symBg, 0, 0);
        lv_obj_align(symBg, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_clear_flag(symBg, LV_OBJ_FLAG_SCROLLABLE);

        w.symbolLabel = lv_label_create(symBg);
        lv_label_set_text(w.symbolLabel, t.base_symbol[0] ? t.base_symbol : "?");
        lv_obj_set_style_text_font(w.symbolLabel, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(w.symbolLabel, lv_color_hex(CLR_TEXT), 0);
        lv_obj_center(w.symbolLabel);

        // Name + meta (left, next to symbol)
        lv_obj_t* textCol = lv_obj_create(w.container);
        lv_obj_set_size(textCol, innerW - 40 - 70 - 16, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(textCol, LV_OPA_0, 0);
        lv_obj_set_style_border_width(textCol, 0, 0);
        lv_obj_set_style_pad_all(textCol, 0, 0);
        lv_obj_set_flex_flow(textCol, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(textCol, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_align_to(textCol, symBg, LV_ALIGN_OUT_RIGHT_MID, 8, 0);

        w.nameLabel = lv_label_create(textCol);
        lv_label_set_text(w.nameLabel, t.base_name);
        lv_obj_set_style_text_font(w.nameLabel, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(w.nameLabel, lv_color_hex(CLR_TEXT), 0);
        lv_label_set_long_mode(w.nameLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_width(w.nameLabel, innerW - 40 - 70 - 16);

        // Symbol · FDV · change% on second line
        lv_obj_t* metaRow = lv_obj_create(textCol);
        lv_obj_set_size(metaRow, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(metaRow, LV_OPA_0, 0);
        lv_obj_set_style_border_width(metaRow, 0, 0);
        lv_obj_set_style_pad_all(metaRow, 0, 0);
        lv_obj_set_style_pad_column(metaRow, 6, 0);
        lv_obj_set_flex_flow(metaRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(metaRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* symTxt = lv_label_create(metaRow);
        lv_label_set_text(symTxt, t.base_symbol);
        lv_obj_set_style_text_font(symTxt, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(symTxt, lv_color_hex(CLR_MUTED), 0);

        lv_obj_t* dot = lv_label_create(metaRow);
        lv_label_set_text(dot, "\xC2\xB7");
        lv_obj_set_style_text_font(dot, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(dot, lv_color_hex(CLR_MUTED), 0);

        w.fdvLabel = lv_label_create(metaRow);
        _updateFdvLabel(idx);
        lv_obj_set_style_text_font(w.fdvLabel, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(w.fdvLabel, lv_color_hex(CLR_MUTED), 0);

        w.changeLabel = lv_label_create(metaRow);
        _updateChangeLabel(idx);
        lv_obj_set_style_text_font(w.changeLabel, &lv_font_montserrat_10, 0);

        // Mini sparkline chart (right side, 70px wide)
        w.chart = lv_chart_create(w.container);
        lv_obj_set_size(w.chart, 70, 36);
        lv_obj_align(w.chart, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_chart_set_type(w.chart, LV_CHART_TYPE_LINE);
        lv_chart_set_point_count(w.chart, CHART_BARS);
        lv_obj_set_style_bg_opa(w.chart, LV_OPA_0, 0);
        lv_obj_set_style_border_width(w.chart, 0, 0);
        lv_obj_set_style_size(w.chart, 0, LV_PART_INDICATOR);
        lv_chart_set_div_line_count(w.chart, 0, 0);

        bool up = (t.change_24h >= 0);
        lv_color_t lineCol = lv_color_hex(up ? CLR_GREEN : CLR_RED);
        w.series = lv_chart_add_series(w.chart, lineCol, LV_CHART_AXIS_PRIMARY_Y);
        _updateChartData(idx);
    }

    void _buildExpandedCard(int idx) {
        TickerEntry& t = _tickers[idx];
        CardWidgets& w = _cards[idx];

        // ── Row 1: symbol circle + name + chain + remove button ──
        lv_obj_t* topRow = lv_obj_create(w.container);
        lv_obj_set_size(topRow, LV_PCT(100), 44);
        lv_obj_set_style_bg_opa(topRow, LV_OPA_0, 0);
        lv_obj_set_style_border_width(topRow, 0, 0);
        lv_obj_set_style_pad_all(topRow, 0, 0);
        lv_obj_align(topRow, LV_ALIGN_TOP_MID, 0, 0);

        lv_obj_t* symBg = lv_obj_create(topRow);
        lv_obj_set_size(symBg, 40, 40);
        lv_obj_set_style_bg_color(symBg, lv_color_hex(CLR_SURFACE), 0);
        lv_obj_set_style_border_color(symBg, lv_color_hex(CLR_BORDER), 0);
        lv_obj_set_style_border_width(symBg, 1, 0);
        lv_obj_set_style_radius(symBg, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_pad_all(symBg, 0, 0);
        lv_obj_align(symBg, LV_ALIGN_LEFT_MID, 0, 0);

        w.symbolLabel = lv_label_create(symBg);
        lv_label_set_text(w.symbolLabel, t.base_symbol);
        lv_obj_set_style_text_font(w.symbolLabel, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(w.symbolLabel, lv_color_hex(CLR_TEXT), 0);
        lv_obj_center(w.symbolLabel);

        lv_obj_t* nameCol = lv_obj_create(topRow);
        lv_obj_set_size(nameCol, 280, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(nameCol, LV_OPA_0, 0);
        lv_obj_set_style_border_width(nameCol, 0, 0);
        lv_obj_set_style_pad_all(nameCol, 0, 0);
        lv_obj_set_flex_flow(nameCol, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(nameCol, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_align_to(nameCol, symBg, LV_ALIGN_OUT_RIGHT_MID, 8, 0);

        w.nameLabel = lv_label_create(nameCol);
        lv_label_set_text(w.nameLabel, t.base_name);
        lv_obj_set_style_text_font(w.nameLabel, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(w.nameLabel, lv_color_hex(CLR_TEXT), 0);
        lv_label_set_long_mode(w.nameLabel, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(w.nameLabel, 260);

        lv_obj_t* chainLbl = lv_label_create(nameCol);
        char chainBuf[32];
        snprintf(chainBuf, sizeof(chainBuf), "%s/%s", t.base_symbol, t.quote_symbol[0] ? t.quote_symbol : "USD");
        lv_label_set_text(chainLbl, chainBuf);
        lv_obj_set_style_text_font(chainLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(chainLbl, lv_color_hex(CLR_MUTED), 0);

        // Remove button (top-right)
        w.removeBtn = lv_btn_create(topRow);
        lv_obj_set_size(w.removeBtn, 26, 26);
        lv_obj_set_style_bg_color(w.removeBtn, lv_color_hex(CLR_SURFACE), 0);
        lv_obj_set_style_border_color(w.removeBtn, lv_color_hex(CLR_BORDER), 0);
        lv_obj_set_style_border_width(w.removeBtn, 1, 0);
        lv_obj_set_style_radius(w.removeBtn, 6, 0);
        lv_obj_set_style_pad_all(w.removeBtn, 0, 0);
        lv_obj_align(w.removeBtn, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_user_data(w.removeBtn, (void*)(intptr_t)idx);
        lv_obj_add_event_cb(w.removeBtn, _onRemoveTapped, LV_EVENT_CLICKED, this);
        lv_obj_t* xLbl = lv_label_create(w.removeBtn);
        lv_label_set_text(xLbl, LV_SYMBOL_CLOSE);
        lv_obj_set_style_text_font(xLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(xLbl, lv_color_hex(CLR_MUTED), 0);
        lv_obj_center(xLbl);

        // ── Row 2: FDV (big) + price (small) + change ──
        lv_obj_t* priceRow = lv_obj_create(w.container);
        lv_obj_set_size(priceRow, LV_PCT(100), 38);
        lv_obj_set_style_bg_opa(priceRow, LV_OPA_0, 0);
        lv_obj_set_style_border_width(priceRow, 0, 0);
        lv_obj_set_style_pad_all(priceRow, 0, 0);
        lv_obj_align_to(priceRow, topRow, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);
        lv_obj_set_flex_flow(priceRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(priceRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
        lv_obj_set_style_pad_column(priceRow, 12, 0);

        w.fdvLabel = lv_label_create(priceRow);
        _updateFdvLabel(idx);
        lv_obj_set_style_text_font(w.fdvLabel, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(w.fdvLabel, lv_color_hex(CLR_TEXT), 0);

        w.priceLabel = lv_label_create(priceRow);
        _updatePriceLabel(idx);
        lv_obj_set_style_text_font(w.priceLabel, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(w.priceLabel, lv_color_hex(CLR_MUTED), 0);

        w.changeLabel = lv_label_create(priceRow);
        _updateChangeLabel(idx);
        lv_obj_set_style_text_font(w.changeLabel, &lv_font_montserrat_12, 0);

        // ── OHLCV chart (bar chart for candle feel) ──
        w.chart = lv_chart_create(w.container);
        lv_obj_set_size(w.chart, LV_PCT(100), 80);
        lv_obj_align_to(w.chart, priceRow, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);
        lv_chart_set_type(w.chart, LV_CHART_TYPE_BAR);
        lv_chart_set_point_count(w.chart, CHART_BARS);
        lv_obj_set_style_bg_opa(w.chart, LV_OPA_0, 0);
        lv_obj_set_style_border_width(w.chart, 0, 0);
        lv_chart_set_div_line_count(w.chart, 3, 0);
        lv_obj_set_style_line_color(w.chart, lv_color_hex(CLR_BORDER), LV_PART_MAIN);

        bool up = (t.change_24h >= 0);
        w.series = lv_chart_add_series(w.chart, lv_color_hex(up ? CLR_GREEN : CLR_RED), LV_CHART_AXIS_PRIMARY_Y);
        _updateChartData(idx);
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

        if (!t.chart_loaded || t.chart_count == 0) {
            // Fill with sentinel (no data)
            for (int i = 0; i < CHART_BARS; i++)
                w.series->y_points[i] = LV_CHART_POINT_NONE;
        } else {
            // Scale to lv_coord_t
            float minV = t.chart_closes[0], maxV = t.chart_closes[0];
            for (int i = 1; i < t.chart_count; i++) {
                if (t.chart_closes[i] < minV) minV = t.chart_closes[i];
                if (t.chart_closes[i] > maxV) maxV = t.chart_closes[i];
            }
            float range = (maxV - minV) < 1e-10f ? 1.0f : (maxV - minV);
            lv_chart_set_range(w.chart, LV_CHART_AXIS_PRIMARY_Y, 0, 1000);
            for (int i = 0; i < t.chart_count; i++)
                w.series->y_points[i] = (lv_coord_t)(((t.chart_closes[i] - minV) / range) * 1000.0f);
            for (int i = t.chart_count; i < CHART_BARS; i++)
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
        lv_textarea_set_placeholder_text(_searchTA, "Token name, symbol or 0x contract…");
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
        if (_bgTask) {
            vTaskDelete(_bgTask);
            _bgTask = nullptr;
        }
        TickerTaskPayload* payload = new TickerTaskPayload();
        payload->type         = type;
        payload->ticker_index = tickerIdx;
        strncpy(payload->node_code, _nodeCode, sizeof(payload->node_code) - 1);

        if (type == TT_SEARCH && _searchTA) {
            const char* q = lv_textarea_get_text(_searchTA);
            strncpy(payload->query, q ? q : "", sizeof(payload->query) - 1);
        }

        xTaskCreatePinnedToCore(
            _bgTaskFn, "ticker_bg", 8192, payload, 1, &_bgTask, 0
        );
    }

    void _dispatchAdd(int searchResultIdx) {
        if (_bgTask) { vTaskDelete(_bgTask); _bgTask = nullptr; }
        TickerTaskPayload* payload = new TickerTaskPayload();
        payload->type    = TT_ADD;
        strncpy(payload->node_code, _nodeCode, sizeof(payload->node_code) - 1);
        payload->to_add  = _searchResults[searchResultIdx];
        xTaskCreatePinnedToCore(_bgTaskFn, "ticker_add", 8192, payload, 1, &_bgTask, 0);
    }

    void _dispatchRemove(int tickerIdx) {
        if (_bgTask) { vTaskDelete(_bgTask); _bgTask = nullptr; }
        TickerTaskPayload* payload = new TickerTaskPayload();
        payload->type         = TT_REMOVE;
        payload->ticker_index = tickerIdx;
        strncpy(payload->node_code,       _nodeCode,                    sizeof(payload->node_code) - 1);
        strncpy(payload->pool_to_remove,  _tickers[tickerIdx].pool_address, sizeof(payload->pool_to_remove) - 1);
        xTaskCreatePinnedToCore(_bgTaskFn, "ticker_rm", 8192, payload, 1, &_bgTask, 0);
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

        switch (p->type) {

            case TT_LOAD_LIST: {
                // GET node_ticker_config from Supabase REST
                String url = String(ENDPOINT_TICKER_CONFIG) + p->node_code +
                             "&order=display_order.asc";
                HTTPClient http;
                http.begin(url);
                http.addHeader("apikey", SUPABASE_ANON_KEY);
                http.addHeader("Authorization", String("Bearer ") + SUPABASE_ANON_KEY);
                int code = http.GET();
                if (code == 200) {
                    JsonDocument doc;
                    deserializeJson(doc, http.getStream());
                    JsonArray arr = doc.as<JsonArray>();
                    int n = 0;
                    for (JsonObject obj : arr) {
                        if (n >= TICKER_MAX) break;
                        TickerEntry& te = self->_tickers[n];
                        memset(&te, 0, sizeof(te));
                        strncpy(te.pool_address, obj["pool_address"] | "",  sizeof(te.pool_address)-1);
                        strncpy(te.chain_id,     obj["chain_id"]     | "",  sizeof(te.chain_id)-1);
                        strncpy(te.base_symbol,  obj["base_symbol"]  | "",  sizeof(te.base_symbol)-1);
                        strncpy(te.base_name,    obj["base_name"]    | "",  sizeof(te.base_name)-1);
                        strncpy(te.quote_symbol, obj["quote_symbol"] | "USD", sizeof(te.quote_symbol)-1);
                        n++;
                    }
                    self->_tickerCount = n;
                    self->_pending.type = PR_LIST_LOADED;
                }
                http.end();
                // After loading list, kick off live price fetches for each ticker
                for (int i = 0; i < self->_tickerCount; i++) {
                    TickerScreen::_fetchLive(self, i);
                }
                break;
            }

            case TT_LOAD_LIVE: {
                _fetchLive(self, p->ticker_index);
                break;
            }

            case TT_LOAD_CHART: {
                TickerEntry& te = self->_tickers[p->ticker_index];
                const char* net = chainToGT(te.chain_id);
                String url = String(ENDPOINT_GECKOTERMINAL_OHLCV) + net +
                             "/pools/" + te.pool_address +
                             "/ohlcv/hour?aggregate=1&limit=" + CHART_BARS + "&currency=usd&token=base";
                HTTPClient http;
                http.begin(url);
                http.addHeader("Accept", "application/json");
                int code = http.GET();
                if (code == 200) {
                    JsonDocument doc;
                    deserializeJson(doc, http.getStream());
                    JsonArray ohlcv = doc["data"]["attributes"]["ohlcv_list"].as<JsonArray>();
                    int n = 0;
                    // ohlcv_list is newest-first; reverse into closes[]
                    int total = min((int)ohlcv.size(), CHART_BARS);
                    for (int i = total - 1; i >= 0 && n < CHART_BARS; i--) {
                        te.chart_closes[n++] = ohlcv[i][4].as<float>(); // close
                    }
                    te.chart_count  = n;
                    te.chart_loaded = true;
                    self->_pending.type         = PR_CHART_LOADED;
                    self->_pending.tickerIndex  = p->ticker_index;
                }
                http.end();
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
                        self->_pending.type = PR_SEARCH_DONE;
                    }
                    http.end();
                } else {
                    // Normal text search via Supabase Edge Function
                    HTTPClient http;
                    http.begin(ENDPOINT_SEARCH_TOKENS);
                    http.addHeader("Content-Type", "application/json");
                    http.addHeader("Authorization", String("Bearer ") + SUPABASE_ANON_KEY);
                    http.setTimeout(8000);
                    JsonDocument req;
                    req["query"] = q;
                    String body;
                    serializeJson(req, body);
                    int code = http.POST(body);
                    if (code == 200) {
                        JsonDocument doc;
                        deserializeJson(doc, http.getStream());
                        JsonArray arr = doc["results"].as<JsonArray>();
                        int n = 0;
                        for (JsonObject obj : arr) {
                            if (n >= 12) break;
                            SearchResultEntry& sr = self->_searchResults[n];
                            strncpy(sr.pair_address,  obj["pairAddress"]   | "", sizeof(sr.pair_address)-1);
                            strncpy(sr.chain_id,      obj["chainId"]       | "", sizeof(sr.chain_id)-1);
                            strncpy(sr.base_symbol,   obj["baseSymbol"]    | "", sizeof(sr.base_symbol)-1);
                            strncpy(sr.base_name,     obj["baseName"]      | "", sizeof(sr.base_name)-1);
                            strncpy(sr.quote_symbol,  obj["quoteSymbol"]   | "", sizeof(sr.quote_symbol)-1);
                            sr.price_usd     = obj["priceUsd"]      | 0.0f;
                            sr.liquidity_usd = obj["liquidityUsd"]  | 0.0f;
                            sr.change_24h    = obj["priceChange24h"]| 0.0f;
                            n++;
                        }
                        self->_searchResultCount = n;
                        self->_pending.type = PR_SEARCH_DONE;
                    }
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

        delete p;
        self->_bgTask = nullptr;
        vTaskDelete(nullptr);
    }

    // Fetches live price/FDV from DexScreener for one ticker (called from bg task)
    static void _fetchLive(TickerScreen* self, int idx) {
        TickerEntry& te = self->_tickers[idx];
        String url = String(ENDPOINT_DEXSCREENER_PAIRS) +
                     te.chain_id + "/" + te.pool_address;
        HTTPClient http;
        http.begin(url);
        int code = http.GET();
        if (code == 200) {
            JsonDocument doc;
            deserializeJson(doc, http.getStream());
            JsonObject pair = doc["pairs"][0];
            if (!pair.isNull()) {
                te.price_usd  = atof(pair["priceUsd"].as<const char*>() ? pair["priceUsd"] : "0");
                te.change_24h = pair["priceChange"]["h24"] | 0.0f;
                te.fdv        = pair["fdv"] | 0.0f;
                te.live_loaded = true;
                self->_pending.type        = PR_LIVE_LOADED;
                self->_pending.tickerIndex = idx;
            }
        }
        http.end();
    }

    // ── Event callbacks (static, forwarded to instance) ───────────────────────

    static void _onAddBtnTapped(lv_event_t* e) {
        auto* self = static_cast<TickerScreen*>(lv_event_get_user_data(e));
        if (self) self->_openSearchDialog();
    }

    static void _onCardTapped(lv_event_t* e) {
        auto* self = static_cast<TickerScreen*>(lv_event_get_user_data(e));
        lv_obj_t* obj = lv_event_get_target(e);
        int idx = (int)(intptr_t)lv_obj_get_user_data(obj);
        if (!self || idx < 0 || idx >= self->_tickerCount) return;

        // Toggle expand
        self->_tickers[idx].is_expanded = !self->_tickers[idx].is_expanded;
        self->_rebuildTickerCards();

        // If expanding and chart not loaded yet, fetch chart data
        if (self->_tickers[idx].is_expanded && !self->_tickers[idx].chart_loaded) {
            self->_dispatchTask(TT_LOAD_CHART, idx);
        }
    }

    static void _onRemoveTapped(lv_event_t* e) {
        // Stop tap from bubbling to _onCardTapped
        lv_event_stop_bubbling(e);
        auto* self = static_cast<TickerScreen*>(lv_event_get_user_data(e));
        lv_obj_t* obj = lv_event_get_target(e);
        int idx = (int)(intptr_t)lv_obj_get_user_data(obj);
        if (!self || idx < 0 || idx >= self->_tickerCount) return;
        self->_dispatchRemove(idx);
        // Optimistic UI: remove from local array immediately
        for (int i = idx; i < self->_tickerCount - 1; i++)
            self->_tickers[i] = self->_tickers[i + 1];
        self->_tickerCount--;
        self->_rebuildTickerCards();
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
        self->_dispatchTask(TT_SEARCH);
    }

    static void _onSearchKbReady(lv_event_t* e) {
        // Keyboard "ok" key fires READY — same as pressing enter in textarea
        _onSearchSubmit(e);
    }

    static void _onResultSelected(lv_event_t* e) {
        auto* self = static_cast<TickerScreen*>(lv_event_get_user_data(e));
        lv_obj_t* obj = lv_event_get_target(e);
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
        auto* self = static_cast<TickerScreen*>(lv_timer_get_user_data(timer));
        if (!self || self->_pending.type == PR_NONE) return;

        PendingResultType type = self->_pending.type;
        int               tidx = self->_pending.tickerIndex;
        self->_pending.type = PR_NONE;   // consume

        switch (type) {
            case PR_LIST_LOADED:
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
                // Reload full list from server to reflect the new ticker
                self->_dispatchTask(TT_LOAD_LIST);
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
