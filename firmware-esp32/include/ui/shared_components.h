// include/ui/shared_components.h — the header and footer bars that appear
// (with minor variations) on every screen, factored out once instead of
// rebuilt five times. Mirrors iosHeader() / bottomBar() from the browser
// simulator.

#pragma once
#include <lvgl.h>
#include "storage.h"

LV_IMG_DECLARE(turbousd_logo); // generated asset, see assets/README.md

struct SharedHeaderRefs {
    lv_obj_t* dateLabel    = nullptr;
    lv_obj_t* tempLabel    = nullptr;
    lv_obj_t* humidityLabel= nullptr;
    lv_obj_t* timeLabel    = nullptr;
    lv_obj_t* logo         = nullptr;
    lv_obj_t* alarmIcon    = nullptr;  // bell icon left of timeLabel; yellow=active, dim=off
};

struct SharedFooterRefs {
    lv_obj_t* liveDot = nullptr;
    lv_obj_t* nodeNameLabel = nullptr;
    lv_obj_t* nodeCountLabel = nullptr;
    lv_obj_t* qrIcon = nullptr;
};

// Builds the top bar used on every screen except Clock (which has its own
// simpler logo-only header). `onLogoTapped`/`onDateTapped`/`onAlarmTapped`
// are optional event callbacks (pass nullptr to skip wiring one).
// The alarm bell icon is always created; call refreshSharedAlarmIcon() each
// second to keep its colour in sync with the current alarm state.
inline SharedHeaderRefs buildSharedHeader(lv_obj_t* parent,
                                           lv_event_cb_t onLogoTapped,
                                           lv_event_cb_t onDateTapped,
                                           void* userData,
                                           lv_event_cb_t onAlarmTapped = nullptr) {
    SharedHeaderRefs refs;

    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LV_PCT(100), 38);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x0a0a0a), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(0x262626), 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_pad_all(bar, 8, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    // Left cluster: wifi icon (TODO: swap for a real signal-strength glyph
    // once we have one rendered for the font/icon set in use) + date.
    refs.dateLabel = lv_label_create(bar);
    lv_obj_set_style_text_color(refs.dateLabel, lv_color_hex(0x9a9a9e), 0);
    lv_obj_set_style_text_font(refs.dateLabel, &lv_font_montserrat_12, 0);
    lv_obj_align(refs.dateLabel, LV_ALIGN_LEFT_MID, 4, 0);
    if (onDateTapped) {
        lv_obj_add_flag(refs.dateLabel, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(refs.dateLabel, onDateTapped, LV_EVENT_CLICKED, userData);
    }

    // Center: logo, tap -> go to Clock screen.
    refs.logo = lv_img_create(bar);
    lv_img_set_src(refs.logo, &turbousd_logo);
    lv_obj_align(refs.logo, LV_ALIGN_CENTER, 0, 0);
    if (onLogoTapped) {
        lv_obj_add_flag(refs.logo, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(refs.logo, onLogoTapped, LV_EVENT_CLICKED, userData);
    }

    // Right cluster (right → left): time | alarm-bell | humidity | temp
    refs.timeLabel = lv_label_create(bar);
    lv_obj_set_style_text_color(refs.timeLabel, lv_color_white(), 0);
    lv_obj_set_style_text_font(refs.timeLabel, &lv_font_montserrat_12, 0);
    lv_obj_align(refs.timeLabel, LV_ALIGN_RIGHT_MID, -4, 0);

    // Alarm bell icon — tap opens alarm picker. Yellow = active today, dim = off.
    refs.alarmIcon = lv_label_create(bar);
    lv_label_set_text(refs.alarmIcon, LV_SYMBOL_BELL);
    lv_obj_set_style_text_font(refs.alarmIcon, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(refs.alarmIcon, lv_color_hex(0x3a3a3a), 0);  // starts dim; refreshed by refreshSharedAlarmIcon()
    lv_obj_align_to(refs.alarmIcon, refs.timeLabel, LV_ALIGN_OUT_LEFT_MID, -6, 0);
    if (onAlarmTapped) {
        lv_obj_add_flag(refs.alarmIcon, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(refs.alarmIcon, 6);  // extra tap area (small target)
        lv_obj_add_event_cb(refs.alarmIcon, onAlarmTapped, LV_EVENT_CLICKED, userData);
    }

    refs.humidityLabel = lv_label_create(bar);
    lv_obj_set_style_text_color(refs.humidityLabel, lv_color_hex(0x9a9a9e), 0);
    lv_obj_set_style_text_font(refs.humidityLabel, &lv_font_montserrat_12, 0);
    lv_obj_align_to(refs.humidityLabel, refs.alarmIcon, LV_ALIGN_OUT_LEFT_MID, -8, 0);

    refs.tempLabel = lv_label_create(bar);
    lv_obj_set_style_text_color(refs.tempLabel, lv_color_hex(0x9a9a9e), 0);
    lv_obj_set_style_text_font(refs.tempLabel, &lv_font_montserrat_12, 0);
    lv_obj_align_to(refs.tempLabel, refs.humidityLabel, LV_ALIGN_OUT_LEFT_MID, -8, 0);

    return refs;
}

// Builds the bottom bar: live dot + node name + node count (left cluster),
// gear icon (right) that opens the config popup (QR + display prefs) when tapped.
inline SharedFooterRefs buildSharedFooter(lv_obj_t* parent, lv_event_cb_t onQrTapped, void* userData) {
    SharedFooterRefs refs;

    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LV_PCT(100), 38);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_black(), 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(0x262626), 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_pad_all(bar, 8, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    refs.liveDot = lv_obj_create(bar);
    lv_obj_set_size(refs.liveDot, 7, 7);
    lv_obj_set_style_radius(refs.liveDot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(refs.liveDot, lv_color_hex(0x3aff7a), 0);
    lv_obj_set_style_border_width(refs.liveDot, 0, 0);
    lv_obj_align(refs.liveDot, LV_ALIGN_LEFT_MID, 2, 0);
    // TODO: port the livePulse keyframe animation (opacity 1 -> 0.35 -> 1,
    // 2.4s) using lv_anim_t on the bg_opa style property.

    refs.nodeNameLabel = lv_label_create(bar);
    lv_obj_set_style_text_color(refs.nodeNameLabel, lv_color_hex(0x9a9a9e), 0);
    lv_obj_set_style_text_font(refs.nodeNameLabel, &lv_font_montserrat_12, 0);
    lv_obj_align_to(refs.nodeNameLabel, refs.liveDot, LV_ALIGN_OUT_RIGHT_MID, 6, 0);

    refs.nodeCountLabel = lv_label_create(bar);
    lv_obj_set_style_text_color(refs.nodeCountLabel, lv_color_hex(0x3aff7a), 0);
    lv_obj_set_style_text_font(refs.nodeCountLabel, &lv_font_montserrat_12, 0);
    lv_obj_align_to(refs.nodeCountLabel, refs.nodeNameLabel, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

    // Gear icon — tap opens the config popup (shows QR code + display preferences).
    refs.qrIcon = lv_label_create(bar);
    lv_label_set_text(refs.qrIcon, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(refs.qrIcon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(refs.qrIcon, lv_color_hex(0x3aff7a), 0);
    lv_obj_align(refs.qrIcon, LV_ALIGN_RIGHT_MID, -2, 0);
    lv_obj_add_flag(refs.qrIcon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(refs.qrIcon, 6);
    lv_obj_add_event_cb(refs.qrIcon, onQrTapped, LV_EVENT_CLICKED, userData);

    return refs;
}

// Call once a second (or whenever the data actually changes) to keep the
// header/footer text current without needing each screen to duplicate this.
// `sensorValid` = false when the RP2040 has no current reading (e.g. no AHT20
// plugged in, or the link is down); the temp/humidity then render as "--".
inline void refreshSharedHeader(SharedHeaderRefs& refs, struct tm& t, float tempC, int humidityPct, bool is24h, char tempUnit, bool sensorValid = true) {
    const char* days[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
    char dateBuf[16];
    snprintf(dateBuf, sizeof(dateBuf), "%s %02d/%02d", days[t.tm_wday], t.tm_mday, t.tm_mon + 1);
    lv_label_set_text(refs.dateLabel, dateBuf);

    char timeBuf[12];
    if (is24h) {
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", t.tm_hour, t.tm_min);
    } else {
        int hour12 = t.tm_hour % 12; if (hour12 == 0) hour12 = 12;
        snprintf(timeBuf, sizeof(timeBuf), "%d:%02d%s", hour12, t.tm_min, t.tm_hour < 12 ? "a" : "p");
    }
    lv_label_set_text(refs.timeLabel, timeBuf);

    char tempBuf[12];
    char humBuf[12];
    if (sensorValid) {
        float displayTemp = (tempUnit == 'F') ? (tempC * 9.0f / 5.0f + 32.0f) : tempC;
        snprintf(tempBuf, sizeof(tempBuf), "%d\xC2\xB0", (int)roundf(displayTemp));
        snprintf(humBuf, sizeof(humBuf), "%d%%", humidityPct);
    } else {
        snprintf(tempBuf, sizeof(tempBuf), "--\xC2\xB0");
        snprintf(humBuf, sizeof(humBuf), "--%%");
    }
    lv_label_set_text(refs.tempLabel, tempBuf);
    lv_label_set_text(refs.humidityLabel, humBuf);
}

// Update the alarm bell icon colour in the header.
// Call once per second alongside refreshSharedHeader().
//   alarmEnabled  — storage.getAlarmEnabled()
//   activeToday   — storage.isAlarmActiveToday(t.tm_wday)
inline void refreshSharedAlarmIcon(SharedHeaderRefs& refs, bool alarmEnabled, bool activeToday) {
    if (!refs.alarmIcon) return;
    lv_color_t col;
    if (!alarmEnabled)    col = lv_color_hex(0x3a3a3a);   // fully off — almost invisible
    else if (!activeToday) col = lv_color_hex(0x6e7280);  // enabled globally, but silent today
    else                   col = lv_color_hex(0xe8b339);  // active and ringing today → yellow
    lv_obj_set_style_text_color(refs.alarmIcon, col, 0);
}

inline void refreshSharedFooter(SharedFooterRefs& refs, const String& nodeName, int onlineNodeCount) {
    lv_label_set_text(refs.nodeNameLabel, nodeName.c_str());
    char countBuf[24];
    snprintf(countBuf, sizeof(countBuf), "%d NODES", onlineNodeCount);
    lv_label_set_text(refs.nodeCountLabel, countBuf);
}
