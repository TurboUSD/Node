// include/ui/shared_components.h — the header and footer bars that appear
// (with minor variations) on every screen, factored out once instead of
// rebuilt five times. Mirrors iosHeader() / bottomBar() from the browser
// simulator.

#pragma once
#include <lvgl.h>
#include "storage.h"

LV_IMG_DECLARE(turbousd_logo); // generated asset, see assets/README.md

// Global "the current gesture is a swipe, not a tap" flag, mirrored from the
// touch read_cb (UiManager). Lets tap handlers that live OUTSIDE UiManager (e.g.
// the node-name info popups in screen_node.h) bail when a swipe merely started
// on their widget — otherwise the popup opened on the screen you swiped to.
inline bool& g_touchWasSwipe() { static bool v = false; return v; }

struct SharedHeaderRefs {
    lv_obj_t* dateLabel    = nullptr;
    lv_obj_t* tempLabel    = nullptr;
    lv_obj_t* humidityLabel= nullptr;
    lv_obj_t* timeLabel    = nullptr;
    lv_obj_t* logo         = nullptr;
    lv_obj_t* alarmIcon    = nullptr;  // bell icon left of timeLabel; yellow=active, dim=off
};

struct SharedFooterRefs {
    lv_obj_t* bar = nullptr;            // the footer bar itself (so screens can host controls in it)
    lv_obj_t* nodeSepLabel  = nullptr;  // grey "|" between name and node count
    lv_obj_t* liveDot = nullptr;
    lv_obj_t* nodeNameLabel = nullptr;
    lv_obj_t* nodeCountLabel = nullptr;
    lv_obj_t* qrIcon = nullptr;
    lv_obj_t* controls = nullptr;       // screen's control row; sits just RIGHT of the "|" separator
    lv_coord_t controlsGap = 12;        // gap between the separator and the controls
    lv_coord_t nameMaxW = 0;            // >0: cap the node name to this width and marquee if longer
                                        // (set by screens that host controls). 0 = unconstrained.
};

// Replace ONLY the "Network: N nodes" count with a screen's own controls (which
// sit just to the RIGHT of the "|" separator). The live dot, node name, "|"
// separator AND the settings gear (far right) all STAY (used on tickers / NFT).
inline void hideFooterNetworkText(SharedFooterRefs& f) {
    if (f.nodeCountLabel) lv_obj_add_flag(f.nodeCountLabel, LV_OBJ_FLAG_HIDDEN);
}

// Host a screen's control row in the footer: left-aligned right after the "|"
// separator, with the device settings gear left in place at the far right. Caps
// the node name so name + separator + controls always fit left of the gear; names
// that fit stay static, longer ones marquee (handled in refreshSharedFooter).
// Call AFTER the controls container is built and populated.
inline void layoutFooterControls(SharedFooterRefs& f, lv_obj_t* controls, lv_coord_t gap) {
    if (!f.bar || !controls || !f.nodeNameLabel) return;
    f.controls    = controls;
    f.controlsGap = gap;
    lv_obj_update_layout(f.bar);                         // finalise sizes/positions
    lv_coord_t nameLeft = lv_obj_get_x(f.nodeNameLabel);
    lv_coord_t ctrlW    = lv_obj_get_width(controls);
    lv_coord_t gearLeft = f.qrIcon ? lv_obj_get_x(f.qrIcon) : lv_obj_get_width(f.bar);
    const lv_coord_t sepReserve = 14;                    // 8px gap + ~6px "|"
    const lv_coord_t endGap     = 14;                    // clear space before the settings gear
    lv_coord_t maxW = gearLeft - endGap - ctrlW - gap - sepReserve - nameLeft;
    if (maxW < 40) maxW = 40;                            // never collapse to nothing
    f.nameMaxW = maxW;
    if (f.nodeSepLabel)                                  // initial placement (refined on name update)
        lv_obj_align_to(controls, f.nodeSepLabel, LV_ALIGN_OUT_RIGHT_MID, gap, 0);
}

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
        lv_obj_set_ext_click_area(refs.dateLabel, 14);   // 12 px text alone is a fiddly
                                                         // target — pad the halo to finger size
        lv_obj_add_event_cb(refs.dateLabel, onDateTapped, LV_EVENT_CLICKED, userData);
    }

    // Center: logo, tap -> go to Clock screen.
    refs.logo = lv_img_create(bar);
    lv_img_set_src(refs.logo, &turbousd_logo);
    lv_img_set_zoom(refs.logo, 140);   // 48px asset → ~26px, leaves a margin in the 38px bar
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
    if (onAlarmTapped) {
        // The TIME also opens the alarm picker: even with a generous halo the
        // 12 px bell alone stayed a fiddly target, and bell+time read as one
        // cluster anyway — tapping anywhere on it now works.
        lv_obj_add_flag(refs.timeLabel, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(refs.timeLabel, 14);
        lv_obj_add_event_cb(refs.timeLabel, onAlarmTapped, LV_EVENT_CLICKED, userData);
    }

    // Alarm bell icon — tap opens alarm picker. Yellow = active today, dim = off.
    refs.alarmIcon = lv_label_create(bar);
    lv_label_set_text(refs.alarmIcon, LV_SYMBOL_BELL);
    lv_obj_set_style_text_font(refs.alarmIcon, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(refs.alarmIcon, lv_color_hex(0x3a3a3a), 0);  // starts dim; refreshed by refreshSharedAlarmIcon()
    lv_obj_align_to(refs.alarmIcon, refs.timeLabel, LV_ALIGN_OUT_LEFT_MID, -5, 0);
    if (onAlarmTapped) {
        lv_obj_add_flag(refs.alarmIcon, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(refs.alarmIcon, 14);  // bell glyph is ~12 px — the old
                                                        // 6 px halo took several tries to hit
        lv_obj_add_event_cb(refs.alarmIcon, onAlarmTapped, LV_EVENT_CLICKED, userData);
    }

    // Temp/humidity labels: created (so the refresh code stays valid) but HIDDEN
    // for now per design. Remove the LV_OBJ_FLAG_HIDDEN lines to show them again.
    refs.humidityLabel = lv_label_create(bar);
    lv_obj_set_style_text_color(refs.humidityLabel, lv_color_hex(0x9a9a9e), 0);
    lv_obj_set_style_text_font(refs.humidityLabel, &lv_font_montserrat_12, 0);
    lv_obj_align_to(refs.humidityLabel, refs.alarmIcon, LV_ALIGN_OUT_LEFT_MID, -8, 0);
    lv_obj_add_flag(refs.humidityLabel, LV_OBJ_FLAG_HIDDEN);

    refs.tempLabel = lv_label_create(bar);
    lv_obj_set_style_text_color(refs.tempLabel, lv_color_hex(0x9a9a9e), 0);
    lv_obj_set_style_text_font(refs.tempLabel, &lv_font_montserrat_12, 0);
    lv_obj_align_to(refs.tempLabel, refs.humidityLabel, LV_ALIGN_OUT_LEFT_MID, -8, 0);
    lv_obj_add_flag(refs.tempLabel, LV_OBJ_FLAG_HIDDEN);

    return refs;
}

// Builds the bottom bar: live dot + node name + node count (left cluster),
// gear icon (right) that opens the config popup (QR + display prefs) when tapped.
inline SharedFooterRefs buildSharedFooter(lv_obj_t* parent, lv_event_cb_t onQrTapped, void* userData) {
    SharedFooterRefs refs;

    lv_obj_t* bar = lv_obj_create(parent);
    refs.bar = bar;
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
    // Slow, soft "alive" pulse: fade the dot's opacity 1.0 → 0.3 → 1.0 over
    // ~2.4 s, looping forever, so the footer always signals the node is active.
    {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, refs.liveDot);
        lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_30);
        lv_anim_set_time(&a, 1200);
        lv_anim_set_playback_time(&a, 1200);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        lv_anim_set_exec_cb(&a, [](void* obj, int32_t v) {
            lv_obj_set_style_bg_opa((lv_obj_t*)obj, (lv_opa_t)v, 0);
        });
        lv_anim_start(&a);
    }

    refs.nodeNameLabel = lv_label_create(bar);
    lv_label_set_text(refs.nodeNameLabel, "");   // default so it never shows LVGL's "Text"
    lv_obj_set_style_text_color(refs.nodeNameLabel, lv_color_hex(0x9a9a9e), 0);
    lv_obj_set_style_text_font(refs.nodeNameLabel, &lv_font_montserrat_12, 0);
    lv_obj_align_to(refs.nodeNameLabel, refs.liveDot, LV_ALIGN_OUT_RIGHT_MID, 6, 0);

    // Separator as its OWN grey label with symmetric 8 px gaps (baked into
    // the count string it sat glued to the number and far from the name).
    refs.nodeSepLabel = lv_label_create(bar);
    lv_label_set_text(refs.nodeSepLabel, "|");
    lv_obj_set_style_text_color(refs.nodeSepLabel, lv_color_hex(0x6e7280), 0);
    lv_obj_set_style_text_font(refs.nodeSepLabel, &lv_font_montserrat_12, 0);

    refs.nodeCountLabel = lv_label_create(bar);
    lv_label_set_text(refs.nodeCountLabel, "Network: 0 Nodes");   // default
    lv_obj_set_style_text_color(refs.nodeCountLabel, lv_color_hex(0x9a9a9e), 0);   // same tone as the name
    lv_obj_set_style_text_font(refs.nodeCountLabel, &lv_font_montserrat_12, 0);
    lv_obj_align_to(refs.nodeSepLabel,   refs.nodeNameLabel, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    lv_obj_align_to(refs.nodeCountLabel, refs.nodeSepLabel,  LV_ALIGN_OUT_RIGHT_MID, 8, 0);

    // Gear icon — tap opens the config popup (shows QR code + display preferences).
    refs.qrIcon = lv_label_create(bar);
    lv_label_set_text(refs.qrIcon, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(refs.qrIcon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(refs.qrIcon, lv_color_hex(0x6e7280), 0);
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

    // Re-seat the bell: lv_obj_align_to() is ONE-SHOT and originally ran at
    // build time against an EMPTY time label (width 0). Once "22:30" fills in,
    // the label grows leftward into the bell's gap — so re-align against the
    // real width on every clock refresh (needs an up-to-date layout first).
    if (refs.alarmIcon) {
        lv_obj_update_layout(refs.timeLabel);
        // -5 (half the old -10): the bell sits snug against the time. This
        // per-second re-align OVERRIDES the build-time gap, so change BOTH.
        lv_obj_align_to(refs.alarmIcon, refs.timeLabel, LV_ALIGN_OUT_LEFT_MID, -5, 0);
    }

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

// nodeName should be the device's display name, or its node code while it has
// no name yet. The count renders as "| N NODES", separated from the name by "|".
inline void refreshSharedFooter(SharedFooterRefs& refs, const String& nodeName, int onlineNodeCount) {
    if (refs.nodeNameLabel) {
        lv_label_set_text(refs.nodeNameLabel, nodeName.c_str());
        // On screens with right-side controls, cap the name width: if it fits it
        // stays static (as before); if it's longer it marquees like the NFT
        // collection captions, so it never runs into the controls.
        if (refs.nameMaxW > 0) {
            const lv_font_t* fnt = lv_obj_get_style_text_font(refs.nodeNameLabel, LV_PART_MAIN);
            lv_point_t sz;
            lv_txt_get_size(&sz, nodeName.c_str(), fnt, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            if (sz.x > refs.nameMaxW) {
                lv_label_set_long_mode(refs.nodeNameLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
                lv_obj_set_width(refs.nodeNameLabel, refs.nameMaxW);
            } else {
                lv_label_set_long_mode(refs.nodeNameLabel, LV_LABEL_LONG_WRAP);   // single line at content width → static
                lv_obj_set_width(refs.nodeNameLabel, LV_SIZE_CONTENT);
            }
        }
    }
    if (refs.nodeCountLabel) {
        char countBuf[24];
        snprintf(countBuf, sizeof(countBuf), "Network: %d Node%s", onlineNodeCount, onlineNodeCount == 1 ? "" : "s");
        lv_label_set_text(refs.nodeCountLabel, countBuf);
        // Re-align the [name | count] chain to the name's CURRENT width — the
        // build-time align happened while the name was empty.
        if (refs.nodeNameLabel && refs.nodeSepLabel) {
            lv_obj_update_layout(refs.nodeNameLabel);
            lv_obj_align_to(refs.nodeSepLabel, refs.nodeNameLabel, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
            lv_obj_update_layout(refs.nodeSepLabel);
            lv_obj_align_to(refs.nodeCountLabel, refs.nodeSepLabel, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
        }
    }
    // Keep a screen's control row pinned right after the (re-aligned) "|" separator,
    // so it follows the node name as it grows/shrinks instead of overlapping it.
    if (refs.controls && refs.nodeSepLabel) {
        lv_obj_update_layout(refs.nodeSepLabel);
        lv_obj_align_to(refs.controls, refs.nodeSepLabel, LV_ALIGN_OUT_RIGHT_MID, refs.controlsGap, 0);
    }
}
