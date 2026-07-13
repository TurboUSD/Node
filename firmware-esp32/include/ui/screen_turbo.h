// include/ui/screen_turbo.h — "Ticker Stats" screen (screen index 1).
//
// A GENERIC single-ticker stats page. It shows a 2×2 grid of stat cells plus a
// weekly candle chart for whichever DEX pool the node has selected (₸USD by
// default). The BACKEND computes every stat: the ticker-stats edge function
// resolves the selected pool to pre-formatted display fields (₸USD → treasury
// service, custom tokens like DRB → their own source, everything else → a basic
// DexScreener read) and the device just paints them. The pool is chosen from
// the web setting OR the footer picker on this screen (a token search, same as
// the Token Screener's "+ Add"). See api_client.h fetchTickerStats().
//
// THE CHART is real weekly OHLCV for the selected pool: ₸USD comes from our
// Supabase cache (sync-ohlcv-history), any other pool straight from
// GeckoTerminal — see api_client.h fetchOhlcvHistory(pool, chain). Rendering is
// a single BAR series reshaped into candlesticks via DRAW_PART callbacks (LVGL
// has no candlestick type). When the backend supplies circSupply (₸USD), the
// Y-axis ticks are labelled as MARKET CAP; otherwise as price.
//
// The centre of the grid shows the token's logo (downloaded + decoded via
// imgdec::fetchRgb565 in a bg task) — EXCEPT ₸USD, which by design has none.

#pragma once
#include <lvgl.h>
#include <vector>
#include "api_client.h"
#include "img_decode.h"
#include "net_lock.h"
#include "ui/shared_components.h"

class TurboScreen {
public:
    lv_obj_t* build(lv_obj_t* parentScreen, lv_event_cb_t onLogoTapped, lv_event_cb_t onDateTapped,
                     lv_event_cb_t onQrTapped, void* userData) {
        s_inst = this;
        header = buildSharedHeader(parentScreen, onLogoTapped, onDateTapped, userData);
        footer = buildSharedFooter(parentScreen, onQrTapped, userData);

        // ── Footer picker: a single bare-word control (same style as the Token
        // Screener / NFT footer buttons) placed to the RIGHT of "Network: N
        // nodes" behind a "|" separator — there's room to spare on this screen.
        // It shows the current symbol; tapping opens the ticker search (wired by
        // UIManager via setPickHandler).
        lv_obj_t* fctl = lv_obj_create(footer.bar);
        lv_obj_set_size(fctl, LV_SIZE_CONTENT, 38);
        lv_obj_set_style_bg_opa(fctl, LV_OPA_0, 0);
        lv_obj_set_style_border_width(fctl, 0, 0);
        lv_obj_set_style_pad_ver(fctl, 0, 0);
        lv_obj_set_style_pad_hor(fctl, 2, 0);
        lv_obj_set_flex_flow(fctl, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(fctl, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(fctl, 6, 0);
        lv_obj_set_ext_click_area(fctl, 8);
        lv_obj_clear_flag(fctl, LV_OBJ_FLAG_SCROLLABLE);

        _selBtn = lv_btn_create(fctl);
        lv_obj_set_size(_selBtn, LV_SIZE_CONTENT, 30);
        lv_obj_set_style_bg_opa(_selBtn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(_selBtn, 0, 0);
        lv_obj_set_style_shadow_width(_selBtn, 0, 0);
        lv_obj_set_style_pad_hor(_selBtn, 0, 0);
        lv_obj_set_ext_click_area(_selBtn, 10);
        lv_obj_add_event_cb(_selBtn, _onSelectorTapped, LV_EVENT_CLICKED, this);
        _symbolLabel = lv_label_create(_selBtn);
        lv_label_set_text(_symbolLabel, "Ticker");
        lv_obj_set_style_text_font(_symbolLabel, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(_symbolLabel, lv_color_hex(0x80808a), 0);
        lv_obj_set_style_text_opa(_symbolLabel, LV_OPA_90, 0);
        lv_obj_center(_symbolLabel);
        layoutFooterControlsAfterCount(footer, fctl, 8);

        lv_obj_t* body = lv_obj_create(parentScreen);
        lv_obj_set_size(body, LV_PCT(100), 480 - 38 - 38);
        lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 38);
        lv_obj_set_style_bg_color(body, lv_color_black(), 0);
        lv_obj_set_style_border_width(body, 0, 0);
        lv_obj_set_style_pad_all(body, 0, 0);
        lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

        // 2×2 stat grid drawn with SINGLE separator lines (no doubled borders):
        // right-column cells draw only their LEFT edge, second-row cells only
        // their TOP edge. Cells are GENERIC now — labels + values come from the
        // backend (applyStats), not hardcoded here.
        static const uint32_t kColors[4] = { 0x3a8ade, 0x3aff7a, 0xff4d4d, 0xe8b339 };
        static const lv_border_side_t kSides[4] = {
            LV_BORDER_SIDE_NONE,
            LV_BORDER_SIDE_LEFT,
            LV_BORDER_SIDE_TOP,
            (lv_border_side_t)(LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT),
        };
        lv_obj_t* row1 = makeStatRow(body);
        makeStatCell(row1, 0, lv_color_hex(kColors[0]), kSides[0]);
        makeStatCell(row1, 1, lv_color_hex(kColors[1]), kSides[1]);
        lv_obj_t* row2 = makeStatRow(body);
        makeStatCell(row2, 2, lv_color_hex(kColors[2]), kSides[2]);
        makeStatCell(row2, 3, lv_color_hex(kColors[3]), kSides[3]);

        // Centre logo medallion — floats over the grid crossing point (grid is
        // 56+56 px tall, so its centre is 56 px down from the body top). Ignored
        // by the flex layout; positioned by align. Hidden until a logo loads.
        _logoBox = lv_obj_create(body);
        lv_obj_add_flag(_logoBox, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_set_size(_logoBox, kLogoBox, kLogoBox);
        lv_obj_align(_logoBox, LV_ALIGN_TOP_MID, 0, 56 - kLogoBox / 2);
        lv_obj_set_style_bg_color(_logoBox, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(_logoBox, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(_logoBox, lv_color_hex(0x2e2e34), 0);
        lv_obj_set_style_border_width(_logoBox, 1, 0);
        lv_obj_set_style_radius(_logoBox, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_clip_corner(_logoBox, true, 0);
        lv_obj_set_style_pad_all(_logoBox, 0, 0);
        lv_obj_clear_flag(_logoBox, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(_logoBox, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(_logoBox, LV_OBJ_FLAG_HIDDEN);

        // Wrapper with pad_left: LVGL 8 draws Y tick labels OUTSIDE the chart's
        // left edge, so they need reserved space in the parent.
        lv_obj_t* chartWrap = lv_obj_create(body);
        lv_obj_set_size(chartWrap, LV_PCT(100), LV_PCT(50));
        lv_obj_set_style_bg_opa(chartWrap, LV_OPA_0, 0);
        lv_obj_set_style_border_width(chartWrap, 0, 0);
        lv_obj_set_style_pad_all(chartWrap, 0, 0);
        lv_obj_set_style_pad_left(chartWrap, 64, 0);
        lv_obj_set_style_pad_top(chartWrap, 7, 0);
        lv_obj_set_style_pad_bottom(chartWrap, 7, 0);
        lv_obj_clear_flag(chartWrap, LV_OBJ_FLAG_SCROLLABLE);

        chart = lv_chart_create(chartWrap);
        lv_obj_set_size(chart, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(chart, lv_color_black(), 0);
        lv_obj_set_style_border_width(chart, 0, 0);
        lv_obj_set_style_pad_right(chart, 6, 0);
        lv_obj_set_style_pad_top(chart, 6, 0);
        lv_chart_set_type(chart, LV_CHART_TYPE_BAR);
        lv_chart_set_point_count(chart, 26);
        openCloseSeries = lv_chart_add_series(chart, lv_color_hex(0x3aff7a), LV_CHART_AXIS_PRIMARY_Y);
        lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 1000);
        lv_chart_set_div_line_count(chart, 3, 0);
        lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_Y, 4, 0, 5, 1, true, 60);

        auto chartCb = [](lv_event_t* e) {
            TurboScreen* self = (TurboScreen*)lv_event_get_user_data(e);
            self->onChartDrawPart(e);
        };
        lv_obj_add_event_cb(chart, chartCb, LV_EVENT_DRAW_PART_BEGIN, this);
        lv_obj_add_event_cb(chart, chartCb, LV_EVENT_DRAW_PART_END,   this);

        // Timeframe selector (1D / 1W / 1M) floating at the chart's top-right.
        lv_obj_t* tfDd = lv_dropdown_create(chartWrap);
        lv_dropdown_set_options_static(tfDd, "1D\n1W\n1M");
        lv_dropdown_set_selected(tfDd, timeframeSel);
        lv_obj_set_size(tfDd, 62, 32);
        lv_obj_align(tfDd, LV_ALIGN_TOP_RIGHT, -4, 0);
        lv_obj_set_style_bg_color(tfDd, lv_color_hex(0x1a1a1e), 0);
        lv_obj_set_style_border_color(tfDd, lv_color_hex(0x3a3a42), 0);
        lv_obj_set_style_border_width(tfDd, 1, 0);
        lv_obj_set_style_radius(tfDd, 6, 0);
        lv_obj_set_style_pad_all(tfDd, 7, 0);
        lv_obj_set_style_text_font(tfDd, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(tfDd, lv_color_hex(0xe8e8e8), 0);
        lv_obj_t* tfList = lv_dropdown_get_list(tfDd);
        lv_obj_set_style_bg_color(tfList, lv_color_hex(0x1a1a1e), 0);
        lv_obj_set_style_text_color(tfList, lv_color_hex(0xe8e8e8), 0);
        lv_obj_set_style_text_font(tfList, &lv_font_montserrat_12, 0);
        lv_obj_add_event_cb(tfDd, [](lv_event_t* e) {
            TurboScreen* self = (TurboScreen*)lv_event_get_user_data(e);
            uint8_t sel = (uint8_t)lv_dropdown_get_selected(lv_event_get_current_target(e));
            if (sel == self->timeframeSel) return;
            self->timeframeSel = sel;
            self->tfDirty = true;   // main loop refetches inside the net lock
        }, LV_EVENT_VALUE_CHANGED, this);

        // X-axis time legend — real dates, filled by loadRealCandles().
        lv_obj_t* xRow = lv_obj_create(body);
        lv_obj_set_size(xRow, LV_PCT(100), 14);
        lv_obj_set_style_bg_opa(xRow, LV_OPA_0, 0);
        lv_obj_set_style_border_width(xRow, 0, 0);
        lv_obj_set_style_pad_all(xRow, 0, 0);
        lv_obj_set_style_pad_left(xRow, 56, 0);
        lv_obj_set_style_pad_right(xRow, 6, 0);
        lv_obj_set_flex_flow(xRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(xRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(xRow, LV_OBJ_FLAG_SCROLLABLE);
        for (int i = 0; i < 3; i++) {
            _xDateLabels[i] = lv_label_create(xRow);
            lv_label_set_text(_xDateLabels[i], "");
            lv_obj_set_style_text_color(_xDateLabels[i], lv_color_hex(0x6e7280), 0);
            lv_obj_set_style_text_font(_xDateLabels[i], &lv_font_montserrat_10, 0);
        }

        return body;
    }

    // UIManager wires the footer picker here: cb(ud) is invoked on tap.
    void setPickHandler(void (*cb)(void*), void* ud) { _pickCb = cb; _pickUd = ud; }

    // Paint the backend-computed stats (generic — up to 4 cells). Also sets the
    // footer symbol, the chart's market-cap scale (circSupply), and requests the
    // logo download when the URL changed.
    void applyStats(const TickerStats& s) {
        for (int i = 0; i < 4; i++) {
            if (i < s.count) {
                lv_label_set_text(_cellTitle[i], s.fields[i].label);
                const char* v = s.fields[i].value;
                bool twoLine = strchr(v, '\n') != nullptr;
                lv_obj_set_style_text_font(_cellValue[i], twoLine ? &lv_font_montserrat_14 : &lv_font_montserrat_16, 0);
                lv_obj_set_style_text_align(_cellValue[i], LV_TEXT_ALIGN_CENTER, 0);
                lv_label_set_text(_cellValue[i], v);
            } else {
                lv_label_set_text(_cellTitle[i], "");
                lv_label_set_text(_cellValue[i], "");
            }
        }
        _circSupply = s.circSupply;
        if (_symbolLabel) lv_label_set_text(_symbolLabel, s.symbol[0] ? s.symbol : "Ticker");

        // Logo: request a (re)download only when the URL actually changed.
        if (s.logoUrl[0]) {
            if (strncmp(s.logoUrl, _logoWantUrl, sizeof(_logoWantUrl)) != 0 &&
                strncmp(s.logoUrl, _logoUrl,     sizeof(_logoUrl))     != 0) {
                strncpy(_logoWantUrl, s.logoUrl, sizeof(_logoWantUrl) - 1);
                _logoWant = true;
            }
        } else {
            // No logo (e.g. ₸USD): hide the medallion and forget any prior one.
            _logoWantUrl[0] = '\0';
            _logoUrl[0]     = '\0';
            _logoWant       = false;
            if (_logoBox) lv_obj_add_flag(_logoBox, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Loads real OHLCV candles into the chart (identical technique to before).
    void loadRealCandles(OhlcvCandle* candles, int count, int groupDays = 7) {
        if (count <= 0) return;
        if (count > 26) count = 26;
        _groupDays = groupDays;

        _minPrice = candles[0].low;
        double maxPrice = candles[0].high;
        for (int i = 0; i < count; i++) {
            _minPrice = min(_minPrice, candles[i].low);
            maxPrice  = max(maxPrice,  candles[i].high);
        }
        _priceRange = (maxPrice - _minPrice);
        if (_priceRange <= 0) _priceRange = 1;

        auto scale = [&](double v) -> lv_coord_t {
            return (lv_coord_t)(((v - _minPrice) / _priceRange) * 1000.0);
        };

        candleData.clear();
        lv_chart_set_point_count(chart, count);
        for (int i = 0; i < count; i++) {
            candleData.push_back(candles[i]);
            openCloseSeries->y_points[i] = scale(candles[i].high);
            lowValues[i]  = scale(candles[i].low);
            openValues[i] = min(scale(candles[i].open), scale(candles[i].close));
        }
        lv_chart_refresh(chart);

        if (_xDateLabels[2]) {
            time_t nowT = time(nullptr);
            for (int i = 0; i < 3; i++) {
                time_t ts = nowT - (time_t)((count - 1) - i * (count - 1) / 2) * _groupDays * 86400;
                struct tm tmv;
                localtime_r(&ts, &tmv);
                char dbuf[12];
                strftime(dbuf, sizeof(dbuf), "%b %d", &tmv);
                lv_label_set_text(_xDateLabels[i], dbuf);
            }
        }
    }

    // ── Logo bg task poll (lv_timer on core 1). Applies a decoded logo and
    // spawns the download task when one is pending. ──────────────────────────
    static void pollLogo(lv_timer_t* t) {
        TurboScreen* self = (TurboScreen*)t->user_data;
        if (!self) return;

        // Apply a freshly decoded bitmap (bg task → UI handoff).
        if (self->_logoReady) {
            self->_logoReady = false;
            if (self->_logoNewPx) {
                if (self->_logoPx && self->_logoPx != self->_logoNewPx) free(self->_logoPx);
                self->_logoPx = self->_logoNewPx;
                self->_logoNewPx = nullptr;
                self->_logoDsc.header.always_zero = 0;
                self->_logoDsc.header.cf = LV_IMG_CF_TRUE_COLOR;
                self->_logoDsc.header.w  = self->_logoW;
                self->_logoDsc.header.h  = self->_logoH;
                self->_logoDsc.data_size = (uint32_t)self->_logoW * self->_logoH * 2;
                self->_logoDsc.data      = self->_logoPx;
                if (!self->_logoImg) {
                    self->_logoImg = lv_img_create(self->_logoBox);
                    lv_obj_center(self->_logoImg);
                }
                lv_img_set_src(self->_logoImg, &self->_logoDsc);
                lv_obj_center(self->_logoImg);
                lv_obj_clear_flag(self->_logoBox, LV_OBJ_FLAG_HIDDEN);
            }
        }

        // Spawn a download when one is pending, the worker is free, and there's
        // TLS RAM headroom (same gate the ticker/NFT bg tasks use).
        if (self->_logoWant && !self->_logoTask && netTlsRamOk()) {
            self->_logoWant = false;
            xTaskCreatePinnedToCore(_logoTaskFn, "tstats_logo", 6144, self, 1,
                                    (TaskHandle_t*)&self->_logoTask, 0);
        }
    }

public:
    uint8_t timeframeSel = 1;   // 0=1D, 1=1W (default), 2=1M
    bool    tfDirty      = false;

    SharedHeaderRefs header;
    SharedFooterRefs footer;

private:
    inline static TurboScreen* s_inst = nullptr;   // C++17 inline static (bg task)

    lv_obj_t* chart = nullptr;
    lv_chart_series_t* openCloseSeries = nullptr;

    lv_coord_t lowValues[26]  = {0};
    lv_coord_t openValues[26] = {0};
    std::vector<OhlcvCandle> candleData;

    double _minPrice   = 0.0;
    double _priceRange = 1.0;
    double _circSupply = 0.0;
    lv_obj_t* _xDateLabels[3] = { nullptr };
    int _groupDays = 7;

    // Generic 2×2 grid cells.
    lv_obj_t* _cellTitle[4] = { nullptr };
    lv_obj_t* _cellValue[4] = { nullptr };

    // Footer picker.
    lv_obj_t* _selBtn      = nullptr;
    lv_obj_t* _symbolLabel = nullptr;
    void (*_pickCb)(void*) = nullptr;
    void*  _pickUd         = nullptr;

    // Centre logo.
    static constexpr int kLogo    = 46;   // decoded bitmap size (px)
    static constexpr int kLogoBox = 52;   // medallion diameter (px)
    lv_obj_t* _logoBox = nullptr;
    lv_obj_t* _logoImg = nullptr;
    char      _logoUrl[160]     = {};   // currently-applied URL
    char      _logoWantUrl[160] = {};   // URL to fetch next
    volatile bool _logoWant  = false;
    volatile bool _logoReady = false;
    volatile TaskHandle_t _logoTask = nullptr;
    uint8_t*  _logoPx    = nullptr;     // live bitmap (freed on replace)
    uint8_t*  _logoNewPx = nullptr;     // bg → UI handoff
    uint16_t  _logoW = 0, _logoH = 0;
    lv_img_dsc_t _logoDsc = {};

    static void _logoTaskFn(void* arg) {
        TurboScreen* self = (TurboScreen*)arg;
        if (!self) { vTaskDelete(nullptr); return; }
        char url[160];
        strncpy(url, self->_logoWantUrl, sizeof(url) - 1); url[sizeof(url) - 1] = '\0';
        if (netWaitTlsRam(3000)) { /* headroom */ }
        netLock();
        uint16_t w = 0, h = 0;
        uint8_t* px = imgdec::fetchRgb565(url, kLogo, kLogo, "tstats", url, &w, &h);
        netUnlock();
        if (px) {
            self->_logoNewPx = px;
            self->_logoW = w; self->_logoH = h;
            strncpy(self->_logoUrl, url, sizeof(self->_logoUrl) - 1);
            self->_logoReady = true;   // pollLogo applies on core 1
        }
        self->_logoTask = nullptr;
        vTaskDelete(nullptr);
    }

    static void _onSelectorTapped(lv_event_t* e) {
        TurboScreen* self = (TurboScreen*)lv_event_get_user_data(e);
        if (!self) return;
        if (g_touchWasSwipe()) return;   // a swipe that began on the button isn't a tap
        if (self->_pickCb) self->_pickCb(self->_pickUd);
    }

    // "$26M" / "$850k" / "$1.2B" — chart Y-axis market-cap ticks.
    static void _fmtMcap(char* out, size_t sz, double v) {
        if      (v >= 1e9) snprintf(out, sz, "$%.1fB", v / 1e9);
        else if (v >= 1e6) snprintf(out, sz, "$%.0fM", v / 1e6);
        else if (v >= 1e3) snprintf(out, sz, "$%.0fk", v / 1e3);
        else               snprintf(out, sz, "$%.0f",  v);
    }

    void onChartDrawPart(lv_event_t* e) {
        lv_obj_draw_part_dsc_t* dsc = lv_event_get_draw_part_dsc(e);

        if (lv_obj_draw_part_check_type(dsc, &lv_chart_class, LV_CHART_DRAW_PART_TICK_LABEL)) {
            if (dsc->text && dsc->id == LV_CHART_AXIS_PRIMARY_Y) {
                double p = _minPrice + ((double)dsc->value / 1000.0) * _priceRange;
                if (_circSupply > 0) {
                    _fmtMcap(dsc->text, dsc->text_length, p * _circSupply);
                } else if (p >= 1.0) snprintf(dsc->text, dsc->text_length, "$%.2f", p);
                else if (p >= 0.001) snprintf(dsc->text, dsc->text_length, "$%.4f", p);
                else                 snprintf(dsc->text, dsc->text_length, "%.0e", p);
            }
            return;
        }

        if (dsc->part != LV_PART_ITEMS) return;
        uint32_t idx = dsc->id;
        if (idx >= (uint32_t)candleData.size()) return;
        if (!dsc->rect_dsc || !dsc->draw_area) return;

        lv_area_t ca;
        lv_obj_get_content_coords(chart, &ca);
        lv_coord_t chartH = ca.y2 - ca.y1;
        if (chartH <= 0) return;

        lv_event_code_t code = lv_event_get_code(e);

        if (code == LV_EVENT_DRAW_PART_BEGIN) {
            dsc->rect_dsc->bg_color     = lv_color_hex(0x6a6a6e);
            dsc->rect_dsc->border_color = lv_color_hex(0x6a6a6e);
            dsc->rect_dsc->border_width = 0;
            dsc->rect_dsc->radius       = 0;
            lv_coord_t lowY = ca.y2 - (lv_coord_t)((int32_t)lowValues[idx] * chartH / 1000);
            dsc->draw_area->y2 = lowY;
            lv_coord_t cx = (dsc->draw_area->x1 + dsc->draw_area->x2) / 2;
            dsc->draw_area->x1 = cx - 1;
            dsc->draw_area->x2 = cx + 1;
        }
        else if (code == LV_EVENT_DRAW_PART_END) {
            bool isUp = candleData[idx].close >= candleData[idx].open;
            lv_color_t col = isUp ? lv_color_hex(0x3aff7a) : lv_color_hex(0xff4d4d);
            auto scaleY = [&](double v) -> lv_coord_t {
                lv_coord_t scaled = (lv_coord_t)(((v - _minPrice) / _priceRange) * 1000.0);
                return ca.y2 - (lv_coord_t)((int32_t)scaled * chartH / 1000);
            };
            lv_coord_t bodyTop = scaleY(max(candleData[idx].open, candleData[idx].close));
            lv_coord_t bodyBot = ca.y2 - (lv_coord_t)((int32_t)openValues[idx] * chartH / 1000);
            if (bodyTop >= bodyBot) bodyBot = bodyTop + 1;
            lv_coord_t halfW = (dsc->draw_area->x2 - dsc->draw_area->x1) / 2 + 2;
            lv_coord_t cx    = (dsc->draw_area->x1 + dsc->draw_area->x2) / 2;
            lv_area_t bodyArea = { (lv_coord_t)(cx - halfW), bodyTop, (lv_coord_t)(cx + halfW), bodyBot };
            if (bodyArea.x1 < ca.x1) bodyArea.x1 = ca.x1;
            if (bodyArea.x2 > ca.x2) bodyArea.x2 = ca.x2;
            lv_draw_rect_dsc_t bodyDsc;
            lv_draw_rect_dsc_init(&bodyDsc);
            bodyDsc.bg_color   = col;
            bodyDsc.bg_opa     = LV_OPA_COVER;
            bodyDsc.radius     = 0;
            bodyDsc.border_width = 0;
            lv_draw_rect(dsc->draw_ctx, &bodyDsc, &bodyArea);
        }
    }

    lv_obj_t* makeStatRow(lv_obj_t* parent) {
        lv_obj_t* row = lv_obj_create(parent);
        lv_obj_set_size(row, LV_PCT(100), 56);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_bg_opa(row, LV_OPA_0, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_column(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        return row;
    }

    void makeStatCell(lv_obj_t* row, int idx, lv_color_t color, lv_border_side_t side) {
        lv_obj_t* cell = lv_obj_create(row);
        lv_obj_set_size(cell, LV_PCT(50), LV_PCT(100));
        lv_obj_set_style_bg_opa(cell, LV_OPA_0, 0);
        lv_obj_set_style_border_color(cell, lv_color_hex(0x2e2e34), 0);
        lv_obj_set_style_border_width(cell, side == LV_BORDER_SIDE_NONE ? 0 : 1, 0);
        lv_obj_set_style_border_side(cell, side, 0);
        lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(cell, LV_SCROLLBAR_MODE_OFF);

        _cellTitle[idx] = lv_label_create(cell);
        lv_label_set_text(_cellTitle[idx], "");
        lv_obj_set_style_text_color(_cellTitle[idx], lv_color_hex(0x9a9a9e), 0);
        lv_obj_set_style_text_font(_cellTitle[idx], &lv_font_montserrat_10, 0);

        _cellValue[idx] = lv_label_create(cell);
        lv_label_set_text(_cellValue[idx], "--");
        lv_obj_set_style_text_color(_cellValue[idx], color, 0);
        lv_obj_set_style_text_font(_cellValue[idx], &lv_font_montserrat_16, 0);
    }
};
