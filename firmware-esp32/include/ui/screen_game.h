// include/ui/screen_game.h — Inflation Game screen: projected value of
// $10,000 over an adjustable year range. Mirrors renderGame() /
// gameProjectionChart() from the browser simulator.
//
// LAYOUT NOTE: the body is a flex COLUMN. The old version positioned every
// element with lv_obj_align_to() at build time, but align_to computes a
// one-shot position from PRE-layout coordinates — as soon as label text
// changed length ("$--" → "$7,432") everything drifted off-center and the
// chart ended up partly off-screen (hiding its Y-axis labels). Flex re-lays
// children out on every change, so nothing can drift or overlap.

#pragma once
#include <lvgl.h>
#include <time.h>
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
        lv_obj_set_style_pad_row(body, 4, 0);
        lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

        captionLabel = lv_label_create(body);
        lv_label_set_text(captionLabel, "$10,000 SINCE THIS NODE WENT ONLINE");
        lv_obj_set_style_text_color(captionLabel, lv_color_hex(0x9a9a9e), 0);
        lv_obj_set_style_text_font(captionLabel, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_align(captionLabel, LV_TEXT_ALIGN_CENTER, 0);

        projectedValueLabel = lv_label_create(body);
        lv_label_set_text(projectedValueLabel, "$--");
        lv_obj_set_style_text_color(projectedValueLabel, lv_color_hex(0xff4d4d), 0);
        lv_obj_set_style_text_font(projectedValueLabel, &lv_font_montserrat_32, 0);
        lv_obj_set_style_text_align(projectedValueLabel, LV_TEXT_ALIGN_CENTER, 0);

        detailLabel = lv_label_create(body);
        lv_label_set_text(detailLabel, "projected value lost to inflation");
        lv_obj_set_style_text_color(detailLabel, lv_color_hex(0x9a9a9e), 0);
        lv_obj_set_style_text_font(detailLabel, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_align(detailLabel, LV_TEXT_ALIGN_CENTER, 0);

        chart = lv_chart_create(body);
        lv_obj_set_width(chart, LV_PCT(100));
        lv_obj_set_flex_grow(chart, 1);            // fill what's left, never overflow
        lv_obj_set_style_bg_color(chart, lv_color_black(), 0);
        lv_obj_set_style_border_width(chart, 0, 0);
        lv_obj_set_style_pad_left(chart, 46, 0);    // room for the Y-axis $ labels
        lv_obj_set_style_pad_bottom(chart, 2, 0);
        lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
        lv_chart_set_point_count(chart, 24);
        // vdiv must be 0 or >= 2 — vdiv_cnt == 1 divide-by-zero panics LVGL 8's
        // draw_div_lines on first draw (same crash as the Debt screen had).
        lv_chart_set_div_line_count(chart, 4, 0);
        lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_Y, 4, 0, 5, 1, true, 46);
        projectionSeries = lv_chart_add_series(chart, lv_color_hex(0xff4d4d), LV_CHART_AXIS_PRIMARY_Y);
        lv_obj_add_event_cb(chart, _axisDrawCb, LV_EVENT_DRAW_PART_BEGIN, this);

        yearsButton = lv_btn_create(chart);
        lv_obj_set_style_bg_color(yearsButton, lv_color_hex(0x262626), 0);
        lv_obj_set_style_border_color(yearsButton, lv_color_hex(0x9a9a9e), 0);
        lv_obj_set_style_border_width(yearsButton, 1, 0);
        lv_obj_align(yearsButton, LV_ALIGN_TOP_RIGHT, -4, 4);
        lv_obj_add_event_cb(yearsButton, onYearsBtnTapped, LV_EVENT_CLICKED, userData);
        yearsButtonLabel = lv_label_create(yearsButton);
        lv_obj_set_style_text_font(yearsButtonLabel, &lv_font_montserrat_10, 0);
        lv_label_set_text(yearsButtonLabel, "3Y \xEF\x81\xB8");

        // X-axis: real calendar years ("now", 2027, 2028…) spread across the
        // plot width, filled by setXAxisYears() whenever the horizon changes.
        lv_obj_t* xRow = lv_obj_create(body);
        lv_obj_set_size(xRow, LV_PCT(100), 14);
        lv_obj_set_style_bg_opa(xRow, LV_OPA_0, 0);
        lv_obj_set_style_border_width(xRow, 0, 0);
        lv_obj_set_style_pad_all(xRow, 0, 0);
        lv_obj_set_style_pad_left(xRow, 46, 0);   // line up with the plot, not the Y labels
        lv_obj_set_flex_flow(xRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(xRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(xRow, LV_OBJ_FLAG_SCROLLABLE);
        for (int i = 0; i < X_LABELS; i++) {
            xLabels[i] = lv_label_create(xRow);
            lv_label_set_text(xLabels[i], "");
            lv_obj_set_style_text_color(xLabels[i], lv_color_hex(0x6e7280), 0);
            lv_obj_set_style_text_font(xLabels[i], &lv_font_montserrat_10, 0);
        }
        lv_label_set_text(xLabels[0], "now");

        return body;
    }

    void updateProjection(int dayCount, double projectedValue, int years) {
        char captionBuf[64];
        snprintf(captionBuf, sizeof(captionBuf), "$10,000 SINCE THIS NODE WENT ONLINE - DAY %d", dayCount);
        lv_label_set_text(captionLabel, captionBuf);

        char thou[24];
        _fmtThousands(thou, sizeof(thou), projectedValue);
        lv_label_set_text(projectedValueLabel, thou);

        double lost = 10000.0 - projectedValue;
        char lostBuf[24];
        _fmtThousands(lostBuf, sizeof(lostBuf), lost);
        char detailBuf[80];
        snprintf(detailBuf, sizeof(detailBuf), "projected value in %d year%s - lost %s",
                 years, years > 1 ? "s" : "", lostBuf);
        lv_label_set_text(detailLabel, detailBuf);

        _refreshXAxis(years);
    }

    void setYearsButtonLabel(const String& text) { lv_label_set_text(yearsButtonLabel, text.c_str()); }
    // Kept for API compatibility; the horizon now feeds the X-axis year labels.
    void setHorizonLabel(int years) { _refreshXAxis(years); }
    lv_chart_series_t* getSeries() { return projectionSeries; }
    lv_obj_t* getChart() { return chart; }

public:
    SharedHeaderRefs header;   // accessed by UIManager::refreshSharedAlarmIcon
    SharedFooterRefs footer;

private:
    static const int X_LABELS = 5;

    lv_obj_t* captionLabel = nullptr;
    lv_obj_t* projectedValueLabel = nullptr;
    lv_obj_t* detailLabel = nullptr;
    lv_obj_t* chart = nullptr;
    lv_chart_series_t* projectionSeries = nullptr;
    lv_obj_t* yearsButton = nullptr;
    lv_obj_t* yearsButtonLabel = nullptr;
    lv_obj_t* xLabels[X_LABELS] = { nullptr };

    // "now", then real calendar years evenly spread over the horizon
    // (e.g. 10Y from 2026 → now · 2029 · 2031 · 2034 · 2036).
    void _refreshXAxis(int years) {
        if (!xLabels[X_LABELS - 1]) return;
        time_t nowT = time(nullptr);
        struct tm t;
        localtime_r(&nowT, &t);
        int nowYear = 1900 + t.tm_year;
        for (int i = 1; i < X_LABELS; i++) {
            int yr = nowYear + (int)lroundf((float)years * i / (X_LABELS - 1));
            char b[8]; snprintf(b, sizeof(b), "%d", yr);
            lv_label_set_text(xLabels[i], b);
        }
    }

    // "$12,345" — integer USD with thousands separators.
    static void _fmtThousands(char* out, size_t sz, double v) {
        char digits[24];
        snprintf(digits, sizeof(digits), "%.0f", v < 0 ? -v : v);
        int n = strlen(digits);
        size_t o = 0;
        if (o < sz - 1 && v < 0) out[o++] = '-';
        if (o < sz - 1) out[o++] = '$';
        for (int i = 0; i < n && o < sz - 1; i++) {
            out[o++] = digits[i];
            int rem = n - 1 - i;
            if (rem > 0 && rem % 3 == 0 && o < sz - 1) out[o++] = ',';
        }
        out[o] = '\0';
    }

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
