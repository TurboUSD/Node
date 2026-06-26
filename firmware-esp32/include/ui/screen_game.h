// include/ui/screen_game.h — Inflation Game screen: projected value of
// $10,000 over an adjustable year range. Mirrors renderGame() /
// gameProjectionChart() from the browser simulator.

#pragma once
#include <lvgl.h>
#include "ui/shared_components.h"

class GameScreen {
public:
    lv_obj_t* build(lv_obj_t* parentScreen, lv_event_cb_t onLogoTapped, lv_event_cb_t onDateTapped,
                     lv_event_cb_t onQrTapped, lv_event_cb_t onYearsBtnTapped, void* userData) {
        header = buildSharedHeader(parentScreen, onLogoTapped, onDateTapped, userData);
        footer = buildSharedFooter(parentScreen, onQrTapped, userData);

        lv_obj_t* body = lv_obj_create(parentScreen);
        lv_obj_set_size(body, LV_PCT(100), 480 - 38 - 38);
        lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 38);
        lv_obj_set_style_bg_color(body, lv_color_black(), 0);
        lv_obj_set_style_border_width(body, 0, 0);
        lv_obj_set_style_pad_all(body, 14, 0);
        lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

        captionLabel = lv_label_create(body);
        lv_obj_set_style_text_color(captionLabel, lv_color_hex(0x9a9a9e), 0);
        lv_obj_set_style_text_font(captionLabel, &lv_font_montserrat_10, 0);
        lv_obj_align(captionLabel, LV_ALIGN_TOP_MID, 0, 0);

        projectedValueLabel = lv_label_create(body);
        lv_obj_set_style_text_color(projectedValueLabel, lv_color_hex(0xff4d4d), 0);
        lv_obj_set_style_text_font(projectedValueLabel, &lv_font_montserrat_32, 0);
        lv_obj_align_to(projectedValueLabel, captionLabel, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

        detailLabel = lv_label_create(body);
        lv_obj_set_style_text_color(detailLabel, lv_color_hex(0x9a9a9e), 0);
        lv_obj_set_style_text_font(detailLabel, &lv_font_montserrat_12, 0);
        lv_obj_align_to(detailLabel, projectedValueLabel, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

        chart = lv_chart_create(body);
        lv_obj_set_size(chart, LV_PCT(100), 180);
        lv_obj_align_to(chart, detailLabel, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
        lv_obj_set_style_bg_color(chart, lv_color_black(), 0);
        lv_obj_set_style_border_width(chart, 0, 0);
        lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
        lv_chart_set_point_count(chart, 24);
        projectionSeries = lv_chart_add_series(chart, lv_color_hex(0xff4d4d), LV_CHART_AXIS_PRIMARY_Y);

        yearsButton = lv_btn_create(chart);
        lv_obj_set_style_bg_color(yearsButton, lv_color_hex(0x262626), 0);
        lv_obj_set_style_border_color(yearsButton, lv_color_hex(0x9a9a9e), 0);
        lv_obj_set_style_border_width(yearsButton, 1, 0);
        lv_obj_align(yearsButton, LV_ALIGN_TOP_RIGHT, -4, 4);
        lv_obj_add_event_cb(yearsButton, onYearsBtnTapped, LV_EVENT_CLICKED, userData);
        yearsButtonLabel = lv_label_create(yearsButton);
        lv_obj_set_style_text_font(yearsButtonLabel, &lv_font_montserrat_10, 0);
        lv_label_set_text(yearsButtonLabel, "3Y \xE2\x96\xBE");

        return body;
    }

    void updateProjection(int dayCount, double projectedValue, int years) {
        char captionBuf[64];
        snprintf(captionBuf, sizeof(captionBuf), "$10,000 SINCE THIS NODE WENT ONLINE - DAY %d", dayCount);
        lv_label_set_text(captionLabel, captionBuf);

        char valueBuf[24];
        snprintf(valueBuf, sizeof(valueBuf), "$%d", (int)round(projectedValue));
        lv_label_set_text(projectedValueLabel, valueBuf);

        double lost = 10000.0 - projectedValue;
        char detailBuf[80];
        snprintf(detailBuf, sizeof(detailBuf), "projected value in %d year%s - lost $%d",
                 years, years > 1 ? "s" : "", (int)round(lost));
        lv_label_set_text(detailLabel, detailBuf);
    }

    void setYearsButtonLabel(const String& text) { lv_label_set_text(yearsButtonLabel, text.c_str()); }
    lv_chart_series_t* getSeries() { return projectionSeries; }
    lv_obj_t* getChart() { return chart; }

public:
    SharedHeaderRefs header;   // accessed by UIManager::refreshSharedAlarmIcon
    SharedFooterRefs footer;

private:
    lv_obj_t* captionLabel = nullptr;
    lv_obj_t* projectedValueLabel = nullptr;
    lv_obj_t* detailLabel = nullptr;
    lv_obj_t* chart = nullptr;
    lv_chart_series_t* projectionSeries = nullptr;
    lv_obj_t* yearsButton = nullptr;
    lv_obj_t* yearsButtonLabel = nullptr;
};
