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

        totalDebtLabel = lv_label_create(topRow);
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
        lv_label_set_text(rangeButtonLabel, "LAST 50Y \xE2\x96\xBE");

        chart = lv_chart_create(body);
        lv_obj_set_size(chart, LV_PCT(100), 130);
        lv_obj_set_style_bg_color(chart, lv_color_black(), 0);
        lv_obj_set_style_border_width(chart, 0, 0);
        lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
        lv_chart_set_point_count(chart, 40);
        debtSeries = lv_chart_add_series(chart, lv_color_hex(0xff4d4d), LV_CHART_AXIS_PRIMARY_Y);
        lv_obj_add_flag(chart, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* bottomRow = lv_obj_create(body);
        lv_obj_set_size(bottomRow, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(bottomRow, LV_OPA_0, 0);
        lv_obj_set_style_border_width(bottomRow, 0, 0);
        lv_obj_set_style_pad_all(bottomRow, 0, 0);
        lv_obj_align(bottomRow, LV_ALIGN_BOTTOM_MID, 0, -4);

        lv_obj_t* sinceCol = makeMetricColumn(bottomRow, "SINCE", &sinceButton, &sinceButtonLabel, &sinceValueLabel, onSinceBtnTapped, userData, false);
        lv_obj_align(sinceCol, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t* rateCol = makeMetricColumn(bottomRow, "RATE", &rateButton, &rateButtonLabel, &rateValueLabel, onRateBtnTapped, userData, true);
        lv_obj_align(rateCol, LV_ALIGN_RIGHT_MID, 0, 0);

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

private:
    SharedHeaderRefs header;
    SharedFooterRefs footer;
    lv_obj_t* totalDebtLabel = nullptr;
    lv_obj_t* chart = nullptr;
    lv_chart_series_t* debtSeries = nullptr;

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
        lv_obj_set_size(col, 150, LV_SIZE_CONTENT);
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
