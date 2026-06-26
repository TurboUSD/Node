// include/ui/screen_turbo.h — TurboUSD Stats screen: supply, price, total
// burned, treasury value, and a weekly candle chart. Mirrors renderTurbo()
// / candleChartLog() from the browser simulator.
//
// THE CHART IS NOW REAL DATA: weekly OHLCV candles synced from
// GeckoTerminal into Supabase (see backend/functions/sync-ohlcv-history +
// ohlcv-history, and api_client.h's fetchOhlcvHistory()). Real candle
// rendering (wick + body, colored by direction) uses an LV_EVENT_DRAW_PART_BEGIN
// callback on an LV_CHART_TYPE_BAR chart -- LVGL has no built-in candlestick
// series type, so this is the standard way to get one. Two series share
// the same point index: `highLowSeries` carries each candle's full
// high-low range (drawn as a thin wick), `openCloseSeries` carries the
// open-close body (drawn as a thicker bar, green if close >= open, red
// otherwise). NOTE: GeckoTerminal's free tier only returns ~6 months of
// history, so early on this chart will show fewer than 26 candles -- see
// sync-ohlcv-history's header comment.

#pragma once
#include <lvgl.h>
#include <vector>
#include "api_client.h"
#include "ui/shared_components.h"

class TurboScreen {
public:
    lv_obj_t* build(lv_obj_t* parentScreen, lv_event_cb_t onLogoTapped, lv_event_cb_t onDateTapped,
                     lv_event_cb_t onQrTapped, void* userData) {
        header = buildSharedHeader(parentScreen, onLogoTapped, onDateTapped, userData);
        footer = buildSharedFooter(parentScreen, onQrTapped, userData);

        lv_obj_t* body = lv_obj_create(parentScreen);
        lv_obj_set_size(body, LV_PCT(100), 480 - 38 - 38);
        lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 38);
        lv_obj_set_style_bg_color(body, lv_color_black(), 0);
        lv_obj_set_style_border_width(body, 0, 0);
        lv_obj_set_style_pad_all(body, 0, 0);
        lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* row1 = makeStatRow(body);
        supplyValueLabel = makeStatCell(row1, "SUPPLY", lv_color_hex(0x3a8ade));
        priceValueLabel = makeStatCell(row1, "PRICE", lv_color_hex(0x3aff7a));

        lv_obj_t* row2 = makeStatRow(body);
        burnedValueLabel = makeStatCell(row2, "TOTAL BURNED", lv_color_hex(0xff4d4d));
        treasuryValueLabel = makeStatCell(row2, "TREASURY", lv_color_hex(0xe8b339));

        chart = lv_chart_create(body);
        lv_obj_set_size(chart, LV_PCT(96), LV_PCT(55));
        lv_obj_set_style_bg_color(chart, lv_color_black(), 0);
        lv_obj_set_style_border_width(chart, 0, 0);
        lv_chart_set_type(chart, LV_CHART_TYPE_BAR);
        lv_chart_set_point_count(chart, 26);
        // ONE series whose bar height = the candle's HIGH (= full wick height).
        // The draw callback reshapes each bar into a thin grey wick and then
        // paints the colored open-close body on top in DRAW_PART_END.
        // (A second series would render as grouped side-by-side bars in LVGL 8.3,
        // which is wrong for candlesticks -- so we use one series + custom draw.)
        openCloseSeries = lv_chart_add_series(chart, lv_color_hex(0x3aff7a), LV_CHART_AXIS_PRIMARY_Y);
        lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 1000);
        lv_chart_set_div_line_count(chart, 3, 0);

        auto chartCb = [](lv_event_t* e) {
            TurboScreen* self = (TurboScreen*)lv_event_get_user_data(e);
            self->onChartDrawPart(e);
        };
        lv_obj_add_event_cb(chart, chartCb, LV_EVENT_DRAW_PART_BEGIN, this);
        lv_obj_add_event_cb(chart, chartCb, LV_EVENT_DRAW_PART_END,   this);

        return body;
    }

    void updateData(const TreasuryData& data) {
        char buf[32];

        snprintf(buf, sizeof(buf), "%s", formatCompact(data.tusdSupplyNum - data.tusdBurnedNum).c_str());
        lv_label_set_text(supplyValueLabel, buf);

        snprintf(buf, sizeof(buf), "%s", formatPriceSubscript(data.tusdPriceUsd).c_str());
        lv_label_set_text(priceValueLabel, buf);

        double burnedPct = (data.tusdBurnedNum / 100000000000.0) * 100.0;
        snprintf(buf, sizeof(buf), "%.2f%%", burnedPct);
        lv_label_set_text(burnedValueLabel, buf);

        snprintf(buf, sizeof(buf), "$%d", (int)round(data.treasuryValueUsd));
        lv_label_set_text(treasuryValueLabel, buf);
    }

    // Loads real OHLCV candles fetched via api_client.h's
    // fetchOhlcvHistory() into the chart. `count` may be less than 26 if
    // GeckoTerminal's cache doesn't have that much history yet.
    void loadRealCandles(OhlcvCandle* candles, int count) {
        if (count == 0) return;

        _minPrice = candles[0].low;
        double maxPrice = candles[0].high;
        for (int i = 0; i < count; i++) {
            _minPrice = min(_minPrice, candles[i].low);
            maxPrice  = max(maxPrice,  candles[i].high);
        }
        // Scale all values to [0..1000] so lv_coord_t can represent sub-cent
        // TUSD prices without precision loss. Range is explicitly set on the
        // chart so the callback can use 1000 as the known y-axis max.
        _priceRange = (maxPrice - _minPrice);
        if (_priceRange <= 0) _priceRange = 1;

        auto scale = [&](double v) -> lv_coord_t {
            return (lv_coord_t)(((v - _minPrice) / _priceRange) * 1000.0);
        };

        candleData.clear();
        lv_chart_set_point_count(chart, count);
        for (int i = 0; i < count; i++) {
            candleData.push_back(candles[i]);

            // Bar height = HIGH (the wick's top). The draw callback reshapes
            // this into a thin wick and then draws the body on top.
            openCloseSeries->y_points[i] = scale(candles[i].high);
            // CAVEAT: direct y_points[] access is the LVGL 8.3 approach for
            // candlestick-chart how-tos. If your LVGL version differs, check
            // lv_chart.h or use lv_chart_set_value_by_id() instead.

            lowValues[i]  = scale(candles[i].low);                              // wick bottom
            openValues[i] = min(scale(candles[i].open), scale(candles[i].close)); // body bottom
        }
        lv_chart_refresh(chart);
    }

public:
    SharedHeaderRefs header;   // accessed by UIManager::refreshSharedAlarmIcon
    SharedFooterRefs footer;

private:
    lv_obj_t* chart = nullptr;
    lv_chart_series_t* openCloseSeries = nullptr;  // sole series; bar height = HIGH

    // Parallel arrays: extra per-candle values the single-series BAR chart
    // can't carry itself. Sized to the max point count we ever set (26).
    lv_coord_t lowValues[26]  = {0};  // wick bottom (scaled low)
    lv_coord_t openValues[26] = {0};  // body bottom (min of open/close, scaled)
    std::vector<OhlcvCandle> candleData;

    double _minPrice   = 0.0;  // set by loadRealCandles, used by draw callback
    double _priceRange = 1.0;

    lv_obj_t* supplyValueLabel = nullptr;
    lv_obj_t* priceValueLabel = nullptr;
    lv_obj_t* burnedValueLabel = nullptr;
    lv_obj_t* treasuryValueLabel = nullptr;

    // Two-phase candlestick draw using a single BAR series:
    //   DRAW_PART_BEGIN  — reshape each bar into a thin grey wick (low→high).
    //   DRAW_PART_END    — paint the colored open-close body on top using
    //                      lv_draw_rect so it overlays the wick.
    // Chart y-axis is fixed [0..1000] (set in build()), so the mapping
    //   screen_y = chartArea.y2 - value * chartH / 1000
    // is exact without needing lv_chart_get_y_range_max().
    void onChartDrawPart(lv_event_t* e) {
        lv_obj_draw_part_dsc_t* dsc = lv_event_get_draw_part_dsc(e);
        if (dsc->part != LV_PART_ITEMS) return;
        uint32_t idx = dsc->id;
        if (idx >= (uint32_t)candleData.size()) return;
        if (!dsc->rect_dsc || !dsc->draw_area) return;

        // Get chart content area in absolute screen coordinates.
        lv_area_t ca;
        lv_obj_get_content_coords(chart, &ca);
        lv_coord_t chartH = ca.y2 - ca.y1;
        if (chartH <= 0) return;

        lv_event_code_t code = lv_event_get_code(e);

        if (code == LV_EVENT_DRAW_PART_BEGIN) {
            // ── Wick: thin grey vertical bar from low to high ──────────────
            dsc->rect_dsc->bg_color     = lv_color_hex(0x6a6a6e);
            dsc->rect_dsc->border_color = lv_color_hex(0x6a6a6e);
            dsc->rect_dsc->border_width = 0;
            dsc->rect_dsc->radius       = 0;

            // Raise the bar's bottom from the baseline to the scaled LOW.
            lv_coord_t lowY = ca.y2 - (lv_coord_t)((int32_t)lowValues[idx] * chartH / 1000);
            dsc->draw_area->y2 = lowY;

            // Make the wick 2 px wide, horizontally centered.
            lv_coord_t cx = (dsc->draw_area->x1 + dsc->draw_area->x2) / 2;
            dsc->draw_area->x1 = cx - 1;
            dsc->draw_area->x2 = cx + 1;
        }
        else if (code == LV_EVENT_DRAW_PART_END) {
            // ── Body: colored filled rect from min(open,close) to max(open,close) ──
            bool isUp = candleData[idx].close >= candleData[idx].open;
            lv_color_t col = isUp ? lv_color_hex(0x3aff7a) : lv_color_hex(0xff4d4d);

            auto scaleY = [&](double v) -> lv_coord_t {
                lv_coord_t scaled = (lv_coord_t)(((v - _minPrice) / _priceRange) * 1000.0);
                return ca.y2 - (lv_coord_t)((int32_t)scaled * chartH / 1000);
            };
            lv_coord_t bodyTop = scaleY(max(candleData[idx].open, candleData[idx].close));
            lv_coord_t bodyBot = ca.y2 - (lv_coord_t)((int32_t)openValues[idx] * chartH / 1000);
            if (bodyTop >= bodyBot) bodyBot = bodyTop + 1; // always at least 1px tall

            // Body is slightly wider than the 2 px wick.
            lv_coord_t halfW = (dsc->draw_area->x2 - dsc->draw_area->x1) / 2 + 2;
            lv_coord_t cx    = (dsc->draw_area->x1 + dsc->draw_area->x2) / 2;

            lv_area_t bodyArea = { cx - halfW, bodyTop, cx + halfW, bodyBot };
            // Clamp to chart content area to avoid overflowing into padding.
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
        return row;
    }

    lv_obj_t* makeStatCell(lv_obj_t* row, const char* label, lv_color_t color) {
        lv_obj_t* cell = lv_obj_create(row);
        lv_obj_set_size(cell, LV_PCT(49), LV_PCT(100));
        lv_obj_set_style_bg_opa(cell, LV_OPA_0, 0);
        lv_obj_set_style_border_color(cell, lv_color_hex(0x262626), 0);
        lv_obj_set_style_border_width(cell, 1, 0);
        lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* lbl = lv_label_create(cell);
        lv_label_set_text(lbl, label);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x9a9a9e), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);

        lv_obj_t* valueLabel = lv_label_create(cell);
        lv_obj_set_style_text_color(valueLabel, color, 0);
        lv_obj_set_style_text_font(valueLabel, &lv_font_montserrat_16, 0);
        return valueLabel;
    }

    String formatCompact(double n) {
        char buf[24];
        if (n >= 1e9) snprintf(buf, sizeof(buf), "%.2fB", n / 1e9);
        else if (n >= 1e6) snprintf(buf, sizeof(buf), "%.2fM", n / 1e6);
        else if (n >= 1e3) snprintf(buf, sizeof(buf), "%.2fK", n / 1e3);
        else snprintf(buf, sizeof(buf), "%.0f", n);
        return String(buf);
    }

    String formatPriceSubscript(double n) {
        if (n <= 0) return "$0.00";
        if (n >= 0.01) {
            char buf[16];
            snprintf(buf, sizeof(buf), "$%.4f", n);
            return String(buf);
        }
        int leadingZeros = (int)floor(-log10(n)) - 1;
        double mantissa = n * pow(10, leadingZeros + 2);
        char buf[24];
        snprintf(buf, sizeof(buf), "$0.0(%d)%d", leadingZeros, (int)mantissa);
        return String(buf);
    }
};
