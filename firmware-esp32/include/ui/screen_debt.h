// include/ui/screen_debt.h — US Debt screen: live total, historical chart
// with adjustable year range, and the SINCE/RATE widgets. Mirrors
// renderDebt() / debtChartInteractive() from the browser simulator.

#pragma once
#include <lvgl.h>
#include <math.h>          // isfinite() — guards the async value updaters
#include "api_client.h"
#include "ui/shared_components.h"
#include "ui/modal.h"

class DebtScreen {
public:
    lv_obj_t* build(lv_obj_t* parentScreen, lv_event_cb_t onLogoTapped, lv_event_cb_t onDateTapped,
                     lv_event_cb_t onQrTapped, lv_event_cb_t onRangeBtnTapped,
                     lv_event_cb_t onSinceBtnTapped, lv_event_cb_t onRateBtnTapped, void* userData) {
        header = buildSharedHeader(parentScreen, onLogoTapped, onDateTapped, userData);
        footer = buildSharedFooter(parentScreen, onQrTapped, userData);

        // Flex COLUMN body: topRow / chart title / chart (grows) / X-year row /
        // SINCE-RATE row. The old absolute align_to() layout computed one-shot
        // positions from pre-layout coordinates, which is why the chart sat
        // off-center and overlapped the two bottom sections.
        lv_obj_t* body = lv_obj_create(parentScreen);
        lv_obj_set_size(body, LV_PCT(100), 480 - 38 - 38);
        lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 38);
        lv_obj_set_style_bg_color(body, lv_color_black(), 0);
        lv_obj_set_style_border_width(body, 0, 0);
        lv_obj_set_style_pad_all(body, 14, 0);
        lv_obj_set_style_pad_row(body, 4, 0);
        lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* topRow = lv_obj_create(body);
        lv_obj_set_size(topRow, LV_PCT(100), 50);
        lv_obj_set_style_bg_opa(topRow, LV_OPA_0, 0);
        lv_obj_set_style_border_width(topRow, 0, 0);
        lv_obj_set_style_pad_all(topRow, 0, 0);
        lv_obj_clear_flag(topRow, LV_OBJ_FLAG_SCROLLABLE);

        // Screen title FIRST and big, the live number to its right, and the
        // range selector as plain white text + dropdown glyph (same treatment
        // as the SINCE/RATE pickers) hugging the right edge.
        lv_obj_t* titleLabel = lv_label_create(topRow);
        lv_label_set_text(titleLabel, "US TOTAL DEBT");
        lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xe8e8e8), 0);
        lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_20, 0);
        lv_obj_align(titleLabel, LV_ALIGN_LEFT_MID, 0, 0);

        totalDebtLabel = lv_label_create(topRow);
        lv_label_set_text(totalDebtLabel, "--");
        lv_obj_set_style_text_color(totalDebtLabel, lv_color_hex(0xff4d4d), 0);
        lv_obj_set_style_text_font(totalDebtLabel, &lv_font_montserrat_32, 0);
        // The title text is static, so a one-shot align_to is safe here.
        lv_obj_align_to(totalDebtLabel, titleLabel, LV_ALIGN_OUT_RIGHT_MID, 12, 0);

        // Range selector: SAME widget/format as the turbo chart's 1D/1W/1M
        // timeframe dropdown. VALUE_CHANGED fires onRangeBtnTapped(userData).
        rangeButton = lv_dropdown_create(topRow);
        lv_dropdown_set_options_static(rangeButton, "5Y\n10Y\n20Y\n30Y\n50Y\n75Y");
        lv_dropdown_set_selected(rangeButton, 4);   // matches debtYearsRangeIndex default (50Y)
        lv_obj_set_size(rangeButton, 74, 32);
        lv_obj_align(rangeButton, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_color(rangeButton, lv_color_hex(0x1a1a1e), 0);
        lv_obj_set_style_border_color(rangeButton, lv_color_hex(0x3a3a42), 0);
        lv_obj_set_style_border_width(rangeButton, 1, 0);
        lv_obj_set_style_radius(rangeButton, 6, 0);
        lv_obj_set_style_pad_all(rangeButton, 7, 0);
        lv_obj_set_style_text_font(rangeButton, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(rangeButton, lv_color_hex(0xe8e8e8), 0);
        lv_obj_t* rangeList = lv_dropdown_get_list(rangeButton);
        lv_obj_set_style_bg_color(rangeList, lv_color_hex(0x1a1a1e), 0);
        lv_obj_set_style_text_color(rangeList, lv_color_hex(0xe8e8e8), 0);
        lv_obj_set_style_text_font(rangeList, &lv_font_montserrat_12, 0);
        lv_obj_add_event_cb(rangeButton, onRangeBtnTapped, LV_EVENT_VALUE_CHANGED, userData);

        // IMPORTANT: LVGL 8 draws PRIMARY_Y tick labels OUTSIDE the chart's
        // left edge (draw_y_ticks uses x_ofs = obj->coords.x1, i.e. the widget
        // boundary — the chart's own pad_left does NOT hold them). A
        // full-width chart therefore paints its labels off-screen: only the
        // trailing "t-" was visible. The fix is a transparent wrapper whose
        // pad_left reserves real in-parent space for the labels.
        lv_obj_t* chartWrap = lv_obj_create(body);
        lv_obj_set_width(chartWrap, LV_PCT(100));
        // FIXED height (not flex-grow): the chart was eating every leftover
        // pixel and left SINCE/RATE crammed at the bottom. The leftover space
        // now goes to bottomRow instead (it has flex_grow), so the two
        // metric columns get real breathing room.
        lv_obj_set_height(chartWrap, 168);
        lv_obj_set_style_bg_opa(chartWrap, LV_OPA_0, 0);
        lv_obj_set_style_border_width(chartWrap, 0, 0);
        lv_obj_set_style_pad_all(chartWrap, 0, 0);
        lv_obj_set_style_pad_left(chartWrap, 48, 0);   // ← the Y tick labels live here
        // Tick labels are vertically CENTERED on their tick, so the top and
        // bottom ones stick half a line above/below the chart — reserve that
        // space or they get clipped (the bottom label was cut in half).
        lv_obj_set_style_pad_top(chartWrap, 7, 0);
        lv_obj_set_style_pad_bottom(chartWrap, 7, 0);
        lv_obj_clear_flag(chartWrap, LV_OBJ_FLAG_SCROLLABLE);

        chart = lv_chart_create(chartWrap);
        lv_obj_set_size(chart, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(chart, lv_color_black(), 0);
        lv_obj_set_style_border_width(chart, 0, 0);
        lv_obj_set_style_pad_right(chart, 4, 0);
        lv_obj_set_style_pad_top(chart, 4, 0);
        lv_obj_set_style_pad_bottom(chart, 2, 0);
        lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
        // NOTE: vertical div count must be 0 or >= 2. LVGL 8's draw_div_lines
        // computes `x = w*i / (vdiv_cnt - 1)`, so vdiv_cnt == 1 causes an
        // IntegerDivideByZero panic the first time the chart is drawn — this
        // was the "device reboots when opening the US Debt screen" crash
        // (confirmed by symbolized backtrace → lv_chart.c draw_div_lines).
        lv_chart_set_div_line_count(chart, 4, 0);
        lv_chart_set_point_count(chart, 40);
        // Y axis: 5 major ticks WITH labels (intermediate $T values, not just the
        // endpoints). The draw callback below rewrites each tick's text into "$NT".
        lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_Y, 4, 0, 5, 1, true, 46);
        debtSeries = lv_chart_add_series(chart, lv_color_hex(0xff4d4d), LV_CHART_AXIS_PRIMARY_Y);
        lv_obj_add_flag(chart, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(chart, _axisDrawCb, LV_EVENT_DRAW_PART_BEGIN, this);

        // X-axis year labels (5, evenly spread) under the chart — filled by
        // updateAxisLegend() once the history loads.
        lv_obj_t* xRow = lv_obj_create(body);
        lv_obj_set_size(xRow, LV_PCT(100), 14);
        lv_obj_set_style_bg_opa(xRow, LV_OPA_0, 0);
        lv_obj_set_style_border_width(xRow, 0, 0);
        lv_obj_set_style_pad_all(xRow, 0, 0);
        lv_obj_set_style_pad_left(xRow, 46, 0);   // line up with the plot
        lv_obj_set_flex_flow(xRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(xRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(xRow, LV_OBJ_FLAG_SCROLLABLE);
        for (int i = 0; i < X_LABELS; i++) xYear[i] = _axisLabel(xRow);

        // SINCE (left) and RATE (right) on one flex row, pushed to the edges and
        // vertically centred. SIZE_CONTENT: a fixed 58 px was shorter than the
        // columns' real content, so the value labels spilled out of the row and
        // the two sides looked vertically misaligned.
        lv_obj_t* bottomRow = lv_obj_create(body);
        lv_obj_set_width(bottomRow, LV_PCT(100));
        lv_obj_set_flex_grow(bottomRow, 1);   // absorbs the space freed from the chart
        lv_obj_set_style_bg_opa(bottomRow, LV_OPA_0, 0);
        lv_obj_set_style_border_width(bottomRow, 0, 0);
        lv_obj_set_style_pad_all(bottomRow, 0, 0);
        lv_obj_set_style_pad_hor(bottomRow, 2, 0);
        lv_obj_set_flex_flow(bottomRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(bottomRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(bottomRow, LV_OBJ_FLAG_SCROLLABLE);

        makeMetricColumn(bottomRow, "SINCE", &sinceButton, "1H\n24H\n7D\n30D\nNODE ON", 4,
                          &sinceValueLabel, onSinceBtnTapped, userData, false);
        makeMetricColumn(bottomRow, "RATE",  &rateButton,  "SEC\nMIN\nHOUR\nDAY", 0,
                          &rateValueLabel,  onRateBtnTapped,  userData, true);

        return body;
    }

    void updateLiveTotal(double totalDebtUsd) {
        if (!totalDebtLabel) return;   // update raced screen build/teardown
        if (!isfinite(totalDebtUsd)) { lv_label_set_text(totalDebtLabel, "--"); return; }
        char buf[32];
        snprintf(buf, sizeof(buf), "$%.2fT", totalDebtUsd / 1e12);
        lv_label_set_text(totalDebtLabel, buf);
    }

    void updateSinceValue(double valueUsd) {
        if (!sinceValueLabel || !isfinite(valueUsd)) return;
        // FULL figure for every timeframe — "+$7,711,260", no k/M/B, no
        // decimals. Long is fine (there's room) and the whole number visibly
        // climbs on the 1-second recompute tick.
        char digits[24];
        snprintf(digits, sizeof(digits), "%.0f", valueUsd);
        int n = (int)strlen(digits);
        char buf[36];
        size_t o = 0;
        buf[o++] = '+'; buf[o++] = '$';
        for (int i = 0; i < n && o < sizeof(buf) - 2; i++) {
            buf[o++] = digits[i];
            int rem = n - 1 - i;
            if (rem > 0 && rem % 3 == 0) buf[o++] = ',';
        }
        buf[o] = '\0';
        lv_label_set_text(sinceValueLabel, buf);
    }

    void updateRateValue(double valueUsd) {
        if (!rateValueLabel || !isfinite(valueUsd)) return;
        char buf[28];
        if (valueUsd >= 1e9) {
            snprintf(buf, sizeof(buf), "+$%.2fB", valueUsd / 1e9);
        } else {
            // Full figure with thousands separators — "+$69,000" hits harder
            // than "+$69K", and there's room for it.
            char digits[16];
            snprintf(digits, sizeof(digits), "%.0f", valueUsd);
            int n = strlen(digits);
            size_t o = 0;
            buf[o++] = '+'; buf[o++] = '$';
            for (int i = 0; i < n && o < sizeof(buf) - 1; i++) {
                buf[o++] = digits[i];
                int rem = n - 1 - i;
                if (rem > 0 && rem % 3 == 0 && o < sizeof(buf) - 1) buf[o++] = ',';
            }
            buf[o] = '\0';
        }
        lv_label_set_text(rateValueLabel, buf);
    }

    lv_chart_series_t* getSeries() { return debtSeries; }
    lv_obj_t* getChart() { return chart; }

public:
    SharedHeaderRefs header;   // accessed by UIManager::refreshSharedAlarmIcon
    SharedFooterRefs footer;

    // Set the axis legends from the loaded history: X = 5 years evenly spread
    // first→last. Y values render as $T axis ticks (see _axisDrawCb). Called
    // from UiManager::reloadDebtHistory.
    void updateAxisLegend(int startYear, int endYear, double minUsd, double maxUsd) {
        (void)minUsd; (void)maxUsd;
        char buf[12];
        for (int i = 0; i < X_LABELS; i++) {
            if (!xYear[i]) continue;
            int yr = startYear + (int)lroundf((float)(endYear - startYear) * i / (X_LABELS - 1));
            snprintf(buf, sizeof(buf), "%d", yr);
            lv_label_set_text(xYear[i], buf);
        }
    }

    // Rewrites each Y-axis tick label into "$Nt". Chart values are scaled to
    // USD/1e11, so trillions = value / 10.
    static void _axisDrawCb(lv_event_t* e) {
        lv_obj_draw_part_dsc_t* dsc = lv_event_get_draw_part_dsc(e);
        if (!lv_obj_draw_part_check_type(dsc, &lv_chart_class, LV_CHART_DRAW_PART_TICK_LABEL)) return;
        if (dsc->text == NULL) return;
        if (dsc->id == LV_CHART_AXIS_PRIMARY_Y) {
            snprintf(dsc->text, dsc->text_length, "$%.0fT", (double)dsc->value / 10.0);
        }
    }

private:
    static const int X_LABELS = 5;

    lv_obj_t* totalDebtLabel = nullptr;
    lv_obj_t* chart = nullptr;
    lv_obj_t* xYear[X_LABELS] = { nullptr };
    lv_chart_series_t* debtSeries = nullptr;

    // Small grey axis-legend label helper.
    lv_obj_t* _axisLabel(lv_obj_t* parent) {
        lv_obj_t* l = lv_label_create(parent);
        lv_label_set_text(l, "");
        lv_obj_set_style_text_color(l, lv_color_hex(0x6e7280), 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_10, 0);
        return l;
    }

    lv_obj_t* rangeButton = nullptr;
    lv_obj_t* sinceButton = nullptr;
    lv_obj_t* sinceValueLabel = nullptr;
    lv_obj_t* rateButton = nullptr;
    lv_obj_t* rateValueLabel = nullptr;

    lv_obj_t* makeMetricColumn(lv_obj_t* parent, const char* title, lv_obj_t** btnOut,
                                const char* ddOptions, uint16_t ddSelected,
                                lv_obj_t** valueOut, lv_event_cb_t onChanged, void* userData, bool alignRight) {
        lv_obj_t* col = lv_obj_create(parent);
        // SIZE_CONTENT height + identical fixed-height INTERNALS (title row 28,
        // value below): both columns measure the same, so they stay aligned —
        // and unlike the fixed-56 attempt, the value can't get clipped away.
        lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(col, 0, 0);
        lv_obj_set_style_pad_row(col, 2, 0);   // title row ↔ value: tight (theme default gapped them apart)
        lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(col, LV_OPA_0, 0);
        lv_obj_set_style_border_width(col, 0, 0);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        // Main axis (vertical): pack from the TOP for both. Cross axis
        // (horizontal): right-align the RATE column's rows.
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START,
                               alignRight ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
                               alignRight ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START);

        lv_obj_t* row = lv_obj_create(col);
        // 34, NOT 28: the dropdown inside is 32 px tall — a 28 px row clipped
        // its top/bottom 2 px, which is exactly where the 1 px grey border
        // lives (the dropdowns looked borderless on top and bottom).
        lv_obj_set_size(row, LV_SIZE_CONTENT, 34);   // fixed: keeps both columns' geometry identical
        lv_obj_set_style_bg_opa(row, LV_OPA_0, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_column(row, 6, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* titleLabel = lv_label_create(row);
        lv_label_set_text(titleLabel, title);
        lv_obj_set_style_text_color(titleLabel, lv_color_hex(0x9a9a9e), 0);
        lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_10, 0);

        // SAME dropdown widget/format as the chart's range selector. These
        // sit near the BOTTOM of the screen, so the list opens UPWARD —
        // otherwise most options would fall off-screen.
        *btnOut = lv_dropdown_create(row);
        lv_dropdown_set_options_static(*btnOut, ddOptions);
        lv_dropdown_set_selected(*btnOut, ddSelected);
        lv_dropdown_set_dir(*btnOut, LV_DIR_TOP);
        lv_dropdown_set_symbol(*btnOut, LV_SYMBOL_UP);
        lv_obj_set_size(*btnOut, 96, 32);
        lv_obj_set_style_bg_color(*btnOut, lv_color_hex(0x1a1a1e), 0);
        lv_obj_set_style_border_color(*btnOut, lv_color_hex(0x3a3a42), 0);
        lv_obj_set_style_border_width(*btnOut, 1, 0);
        lv_obj_set_style_radius(*btnOut, 6, 0);
        lv_obj_set_style_pad_all(*btnOut, 7, 0);
        lv_obj_set_style_text_font(*btnOut, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(*btnOut, lv_color_hex(0xe8e8e8), 0);
        lv_obj_t* ddList = lv_dropdown_get_list(*btnOut);
        lv_obj_set_style_bg_color(ddList, lv_color_hex(0x1a1a1e), 0);
        lv_obj_set_style_text_color(ddList, lv_color_hex(0xe8e8e8), 0);
        lv_obj_set_style_text_font(ddList, &lv_font_montserrat_12, 0);
        lv_obj_add_event_cb(*btnOut, onChanged, LV_EVENT_VALUE_CHANGED, userData);

        *valueOut = lv_label_create(col);
        lv_obj_set_style_text_color(*valueOut, lv_color_hex(0xff4d4d), 0);
        lv_obj_set_style_text_font(*valueOut, &lv_font_montserrat_20, 0);

        return col;
    }
};
