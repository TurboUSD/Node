// include/ui/screen_debt.h — US Debt screen: live total, historical chart
// with adjustable year range, and the SINCE/RATE widgets. Mirrors
// renderDebt() / debtChartInteractive() from the browser simulator.

#pragma once
#include <lvgl.h>
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

        lv_obj_t* body = lv_obj_create(parentScreen);
        lv_obj_set_size(body, LV_PCT(100), 480 - 38 - 38);
        lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 38);
        lv_obj_set_style_bg_color(body, lv_color_black(), 0);
        lv_obj_set_style_border_width(body, 0, 0);
        lv_obj_set_style_pad_all(body, 14, 0);
        lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* topRow = lv_obj_create(body);
        lv_obj_set_size(topRow, LV_PCT(100), 50);
        lv_obj_set_style_bg_opa(topRow, LV_OPA_0, 0);
        lv_obj_set_style_border_width(topRow, 0, 0);
        lv_obj_set_style_pad_all(topRow, 0, 0);
        lv_obj_align(topRow, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_clear_flag(topRow, LV_OBJ_FLAG_SCROLLABLE);

        totalDebtLabel = lv_label_create(topRow);
        lv_label_set_text(totalDebtLabel, "--");
        lv_obj_set_style_text_color(totalDebtLabel, lv_color_hex(0xff4d4d), 0);
        lv_obj_set_style_text_font(totalDebtLabel, &lv_font_montserrat_32, 0);
        lv_obj_align(totalDebtLabel, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t* rightCol = lv_obj_create(topRow);
        lv_obj_set_size(rightCol, 140, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(rightCol, LV_OPA_0, 0);
        lv_obj_set_style_border_width(rightCol, 0, 0);
        lv_obj_align(rightCol, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_flex_flow(rightCol, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(rightCol, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);

        lv_obj_t* titleLabel = lv_label_create(rightCol);
        lv_label_set_text(titleLabel, "US TOTAL DEBT");
        lv_obj_set_style_text_color(titleLabel, lv_color_hex(0x9a9a9e), 0);
        lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_10, 0);

        rangeButton = lv_btn_create(rightCol);
        lv_obj_set_style_bg_color(rangeButton, lv_color_hex(0x262626), 0);
        lv_obj_set_style_border_color(rangeButton, lv_color_hex(0x9a9a9e), 0);
        lv_obj_set_style_border_width(rangeButton, 1, 0);
        lv_obj_add_event_cb(rangeButton, onRangeBtnTapped, LV_EVENT_CLICKED, userData);
        rangeButtonLabel = lv_label_create(rangeButton);
        lv_obj_set_style_text_font(rangeButtonLabel, &lv_font_montserrat_10, 0);
        lv_label_set_text(rangeButtonLabel, "LAST 50Y \xEF\x81\xB8");

        // Chart heading, so the graph itself is clearly labelled (in addition to
        // the top-right "US TOTAL DEBT" selector title).
        chartTitle = lv_label_create(body);
        lv_label_set_text(chartTitle, "US TOTAL DEBT (USD)");
        lv_obj_set_style_text_color(chartTitle, lv_color_hex(0x9a9a9e), 0);
        lv_obj_set_style_text_font(chartTitle, &lv_font_montserrat_10, 0);
        lv_obj_align_to(chartTitle, topRow, LV_ALIGN_OUT_BOTTOM_LEFT, 30, 4);

        chart = lv_chart_create(body);
        lv_obj_set_size(chart, LV_PCT(100), 128);
        lv_obj_align_to(chart, chartTitle, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);
        lv_obj_set_style_bg_color(chart, lv_color_black(), 0);
        lv_obj_set_style_border_width(chart, 0, 0);
        // Left padding holds the Y-axis $T tick labels; a little bottom padding
        // keeps the line off the very edge. The series still uses the full plot.
        lv_obj_set_style_pad_left(chart, 40, 0);
        lv_obj_set_style_pad_right(chart, 4, 0);
        lv_obj_set_style_pad_top(chart, 4, 0);
        lv_obj_set_style_pad_bottom(chart, 2, 0);
        lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
        lv_chart_set_div_line_count(chart, 4, 1);
        lv_chart_set_point_count(chart, 40);
        // Y axis: 4 major ticks WITH labels (intermediate $T values, not just the
        // endpoints). The draw callback below rewrites each tick's text into "$Nt".
        lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_Y, 4, 0, 4, 1, true, 40);
        debtSeries = lv_chart_add_series(chart, lv_color_hex(0xff4d4d), LV_CHART_AXIS_PRIMARY_Y);
        lv_obj_add_flag(chart, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(chart, _axisDrawCb, LV_EVENT_DRAW_PART_BEGIN, this);

        // X-axis year labels (start / middle / end) under the chart — filled by
        // updateAxisLegend() once the history loads.
        xYearLeft  = _axisLabel(body); lv_obj_align_to(xYearLeft,  chart, LV_ALIGN_OUT_BOTTOM_LEFT,  40, 1);
        xYearMid   = _axisLabel(body); lv_obj_align_to(xYearMid,   chart, LV_ALIGN_OUT_BOTTOM_MID,   18, 1);
        xYearRight = _axisLabel(body); lv_obj_align_to(xYearRight, chart, LV_ALIGN_OUT_BOTTOM_RIGHT, -2, 1);

        // SINCE (left) and RATE (right) on one flex row, pushed to the edges and
        // vertically centred. Using flex space-between (instead of absolute
        // LEFT/RIGHT aligns on fixed-width 150px columns) keeps the RATE column
        // from being clipped at the right edge, and centres both vertically.
        lv_obj_t* bottomRow = lv_obj_create(body);
        lv_obj_set_size(bottomRow, LV_PCT(100), 58);
        lv_obj_set_style_bg_opa(bottomRow, LV_OPA_0, 0);
        lv_obj_set_style_border_width(bottomRow, 0, 0);
        lv_obj_set_style_pad_all(bottomRow, 0, 0);
        lv_obj_set_style_pad_hor(bottomRow, 2, 0);
        lv_obj_align(bottomRow, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_flex_flow(bottomRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(bottomRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(bottomRow, LV_OBJ_FLAG_SCROLLABLE);

        makeMetricColumn(bottomRow, "SINCE", &sinceButton, &sinceButtonLabel, &sinceValueLabel, onSinceBtnTapped, userData, false);
        makeMetricColumn(bottomRow, "RATE",  &rateButton,  &rateButtonLabel,  &rateValueLabel,  onRateBtnTapped,  userData, true);

        return body;
    }

    void updateLiveTotal(double totalDebtUsd) {
        char buf[20];
        snprintf(buf, sizeof(buf), "$%.2fT", totalDebtUsd / 1e12);
        lv_label_set_text(totalDebtLabel, buf);
    }

    void updateSinceValue(double valueUsd) {
        char buf[20];
        snprintf(buf, sizeof(buf), "+$%.3fB", valueUsd / 1e9);
        lv_label_set_text(sinceValueLabel, buf);
    }

    void updateRateValue(double valueUsd) {
        char buf[20];
        if (valueUsd >= 1e9) snprintf(buf, sizeof(buf), "+$%.0fB", valueUsd / 1e9);
        else if (valueUsd >= 1e6) snprintf(buf, sizeof(buf), "+$%.0fM", valueUsd / 1e6);
        else if (valueUsd >= 1e3) snprintf(buf, sizeof(buf), "+$%.0fK", valueUsd / 1e3);
        else snprintf(buf, sizeof(buf), "+$%.0f", valueUsd);
        lv_label_set_text(rateValueLabel, buf);
    }

    void setRangeButtonLabel(const String& text) { lv_label_set_text(rangeButtonLabel, text.c_str()); }
    void setSinceButtonLabel(const String& text) { lv_label_set_text(sinceButtonLabel, text.c_str()); }
    void setRateButtonLabel(const String& text) { lv_label_set_text(rateButtonLabel, text.c_str()); }

    lv_chart_series_t* getSeries() { return debtSeries; }
    lv_obj_t* getChart() { return chart; }

public:
    SharedHeaderRefs header;   // accessed by UIManager::refreshSharedAlarmIcon
    SharedFooterRefs footer;

    // Set the axis legends from the loaded history: X = first→last year,
    // Y = min→max total in $T. Called from UiManager::reloadDebtHistory.
    void updateAxisLegend(int startYear, int endYear, double minUsd, double maxUsd) {
        (void)minUsd; (void)maxUsd;   // Y values now render as $T axis ticks (see _axisDrawCb)
        char buf[12];
        if (xYearLeft)  { snprintf(buf, sizeof(buf), "%d", startYear);              lv_label_set_text(xYearLeft, buf); }
        if (xYearMid)   { snprintf(buf, sizeof(buf), "%d", (startYear + endYear) / 2); lv_label_set_text(xYearMid, buf); }
        if (xYearRight) { snprintf(buf, sizeof(buf), "%d", endYear);               lv_label_set_text(xYearRight, buf); }
    }

    // Rewrites each Y-axis tick label into "$Nt". Chart values are scaled to
    // USD/1e11, so trillions = value / 10.
    static void _axisDrawCb(lv_event_t* e) {
        lv_obj_draw_part_dsc_t* dsc = lv_event_get_draw_part_dsc(e);
        if (!lv_obj_draw_part_check_type(dsc, &lv_chart_class, LV_CHART_DRAW_PART_TICK_LABEL)) return;
        if (dsc->text == NULL) return;
        if (dsc->id == LV_CHART_AXIS_PRIMARY_Y) {
            snprintf(dsc->text, dsc->text_length, "$%.0ft", (double)dsc->value / 10.0);
        }
    }

private:
    lv_obj_t* totalDebtLabel = nullptr;
    lv_obj_t* chart = nullptr;
    lv_obj_t* chartTitle = nullptr;
    lv_obj_t* xYearLeft = nullptr;
    lv_obj_t* xYearMid = nullptr;
    lv_obj_t* xYearRight = nullptr;
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
    lv_obj_t* rangeButtonLabel = nullptr;
    lv_obj_t* sinceButton = nullptr;
    lv_obj_t* sinceButtonLabel = nullptr;
    lv_obj_t* sinceValueLabel = nullptr;
    lv_obj_t* rateButton = nullptr;
    lv_obj_t* rateButtonLabel = nullptr;
    lv_obj_t* rateValueLabel = nullptr;

    lv_obj_t* makeMetricColumn(lv_obj_t* parent, const char* title, lv_obj_t** btnOut, lv_obj_t** btnLabelOut,
                                lv_obj_t** valueOut, lv_event_cb_t onTap, void* userData, bool alignRight) {
        lv_obj_t* col = lv_obj_create(parent);
        lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(col, 0, 0);
        lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(col, LV_OPA_0, 0);
        lv_obj_set_style_border_width(col, 0, 0);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, alignRight ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
                               LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        lv_obj_t* row = lv_obj_create(col);
        lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, LV_OPA_0, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* titleLabel = lv_label_create(row);
        lv_label_set_text(titleLabel, title);
        lv_obj_set_style_text_color(titleLabel, lv_color_hex(0x9a9a9e), 0);
        lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_10, 0);

        *btnOut = lv_btn_create(row);
        lv_obj_set_style_bg_color(*btnOut, lv_color_hex(0x262626), 0);
        lv_obj_set_style_border_color(*btnOut, lv_color_hex(0x9a9a9e), 0);
        lv_obj_set_style_border_width(*btnOut, 1, 0);
        lv_obj_add_event_cb(*btnOut, onTap, LV_EVENT_CLICKED, userData);
        *btnLabelOut = lv_label_create(*btnOut);
        lv_obj_set_style_text_font(*btnLabelOut, &lv_font_montserrat_10, 0);

        *valueOut = lv_label_create(col);
        lv_obj_set_style_text_color(*valueOut, lv_color_hex(0xff4d4d), 0);
        lv_obj_set_style_text_font(*valueOut, &lv_font_montserrat_20, 0);

        return col;
    }
};
