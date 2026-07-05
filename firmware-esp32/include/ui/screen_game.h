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
        lv_label_set_text(captionLabel, "$10,000 SINCE THIS NODE WENT ONLINE");
        lv_obj_set_style_text_color(captionLabel, lv_color_hex(0x9a9a9e), 0);
        lv_obj_set_style_text_font(captionLabel, &lv_font_montserrat_10, 0);
        lv_obj_align(captionLabel, LV_ALIGN_TOP_MID, 0, 0);

        projectedValueLabel = lv_label_create(body);
        lv_label_set_text(projectedValueLabel, "$--");
        lv_obj_set_style_text_color(projectedValueLabel, lv_color_hex(0xff4d4d), 0);
        lv_obj_set_style_text_font(projectedValueLabel, &lv_font_montserrat_32, 0);
        lv_obj_align_to(projectedValueLabel, captionLabel, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

        detailLabel = lv_label_create(body);
        lv_label_set_text(detailLabel, "projected value lost to inflation");
        lv_obj_set_style_text_color(detailLabel, lv_color_hex(0x9a9a9e), 0);
        lv_obj_set_style_text_font(detailLabel, &lv_font_montserrat_12, 0);
        lv_obj_align_to(detailLabel, projectedValueLabel, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

        chart = lv_chart_create(body);
        lv_obj_set_size(chart, LV_PCT(100), 168);
        lv_obj_align_to(chart, detailLabel, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);
        lv_obj_set_style_bg_color(chart, lv_color_black(), 0);
        lv_obj_set_style_border_width(chart, 0, 0);
        lv_obj_set_style_pad_left(chart, 44, 0);    // room for the Y-axis $ labels
        lv_obj_set_style_pad_bottom(chart, 2, 0);
        lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
        lv_chart_set_point_count(chart, 24);
        // vdiv must be 0 or >= 2 — vdiv_cnt == 1 divide-by-zero panics LVGL 8's
        // draw_div_lines on first draw (same crash as the Debt screen had).
        lv_chart_set_div_line_count(chart, 4, 0);
        lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_Y, 4, 0, 4, 1, true, 44);
        projectionSeries = lv_chart_add_series(chart, lv_color_hex(0xff4d4d), LV_CHART_AXIS_PRIMARY_Y);
        lv_obj_add_event_cb(chart, _axisDrawCb, LV_EVENT_DRAW_PART_BEGIN, this);

        // X-axis: "now" on the left, the horizon (e.g. "30Y") on the right.
        xStartLabel = lv_label_create(body);
        lv_label_set_text(xStartLabel, "now");
        lv_obj_set_style_text_color(xStartLabel, lv_color_hex(0x6e7280), 0);
        lv_obj_set_style_text_font(xStartLabel, &lv_font_montserrat_10, 0);
        lv_obj_align_to(xStartLabel, chart, LV_ALIGN_OUT_BOTTOM_LEFT, 44, 0);
        xEndLabel = lv_label_create(body);
        lv_label_set_text(xEndLabel, "");
        lv_obj_set_style_text_color(xEndLabel, lv_color_hex(0x6e7280), 0);
        lv_obj_set_style_text_font(xEndLabel, &lv_font_montserrat_10, 0);
        lv_obj_align_to(xEndLabel, chart, LV_ALIGN_OUT_BOTTOM_RIGHT, -2, 0);

        yearsButton = lv_btn_create(chart);
        lv_obj_set_style_bg_color(yearsButton, lv_color_hex(0x262626), 0);
        lv_obj_set_style_border_color(yearsButton, lv_color_hex(0x9a9a9e), 0);
        lv_obj_set_style_border_width(yearsButton, 1, 0);
        lv_obj_align(yearsButton, LV_ALIGN_TOP_RIGHT, -4, 4);
        lv_obj_add_event_cb(yearsButton, onYearsBtnTapped, LV_EVENT_CLICKED, userData);
        yearsButtonLabel = lv_label_create(yearsButton);
        lv_obj_set_style_text_font(yearsButtonLabel, &lv_font_montserrat_10, 0);
        lv_label_set_text(yearsButtonLabel, "3Y \xEF\x81\xB8");

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
    void setHorizonLabel(int years) {
        if (!xEndLabel) return;
        char b[8]; snprintf(b, sizeof(b), "%dY", years);
        lv_label_set_text(xEndLabel, b);
    }
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
    lv_obj_t* xStartLabel = nullptr;
    lv_obj_t* xEndLabel = nullptr;

    // Formats the Y-axis $ ticks compactly ("$10k", "$7k"…).
    static void _axisDrawCb(lv_event_t* e) {
        lv_obj_draw_part_dsc_t* dsc = lv_event_get_draw_part_dsc(e);
        if (!lv_obj_draw_part_check_type(dsc, &lv_chart_class, LV_CHART_DRAW_PART_TICK_LABEL)) return;
        if (dsc->text == NULL) return;
        if (dsc->id == LV_CHART_AXIS_PRIMARY_Y) {
            int v = (int)dsc->value;
            if (v >= 1000) snprintf(dsc->text, dsc->text_length, "$%.1fk", v / 1000.0);
            else           snprintf(dsc->text, dsc->text_length, "$%d", v);
        }
    }
};
