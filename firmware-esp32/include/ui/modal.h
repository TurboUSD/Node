// include/ui/modal.h — generic popup component (always green-bordered, per
// the latest design decision) used for: alarm time picker, calendar,
// weather forecast, device config, and the verification status tooltip.
// Mirrors openModal() from the browser simulator.

#pragma once
#include <lvgl.h>

// Returns the modal's content container -- callers add their own widgets
// (labels, rollers, buttons) as children of it. The dimmed background and
// the green-bordered card are already set up.
inline lv_obj_t* openModal(lv_obj_t* parent) {
    lv_obj_t* backdrop = lv_obj_create(parent);
    lv_obj_set_size(backdrop, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(backdrop, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_70, 0);
    lv_obj_set_style_border_width(backdrop, 0, 0);
    lv_obj_set_style_radius(backdrop, 0, 0);
    lv_obj_clear_flag(backdrop, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* card = lv_obj_create(backdrop);
    lv_obj_set_size(card, LV_PCT(84), LV_SIZE_CONTENT);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x111111), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2eaa50), 0); // always green, per design decision
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 18, 0);

    return card;
}

inline void closeModal(lv_obj_t* card) {
    lv_obj_t* backdrop = lv_obj_get_parent(card);
    lv_obj_del(backdrop);
}

// Adds a CLOSE/CANCEL-style button at the bottom of a modal card. Returns
// the button so the caller can attach its own click handler.
inline lv_obj_t* addModalButton(lv_obj_t* card, const char* label, bool primary) {
    lv_obj_t* btn = lv_btn_create(card);
    lv_obj_set_width(btn, LV_PCT(100));
    lv_obj_set_style_bg_color(btn, primary ? lv_color_hex(0x2eaa50) : lv_color_transparent(), 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x6a6a6e), 0);
    lv_obj_set_style_border_width(btn, primary ? 0 : 1, 0);
    lv_obj_set_style_radius(btn, 8, 0);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_color(lbl, primary ? lv_color_hex(0x06150a) : lv_color_hex(0x9a9a9e), 0);
    lv_obj_center(lbl);

    return btn;
}

// Hour/minute picker built on LVGL's built-in roller widget -- much less
// code than the hand-rolled drag physics the browser simulator needed,
// since lv_roller already implements exactly this scroll-to-select pattern.
struct TimePickerRefs {
    lv_obj_t* hourRoller = nullptr;
    lv_obj_t* minuteRoller = nullptr;
};

inline TimePickerRefs addTimePicker(lv_obj_t* card, uint8_t initialHour, uint8_t initialMinute) {
    TimePickerRefs refs;

    lv_obj_t* row = lv_obj_create(card);
    lv_obj_set_size(row, LV_PCT(100), 120);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(row, 0, 0);

    static const char* hourOptions =
        "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23";
    refs.hourRoller = lv_roller_create(row);
    lv_roller_set_options(refs.hourRoller, hourOptions, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(refs.hourRoller, initialHour, LV_ANIM_OFF);
    lv_obj_set_style_text_color(refs.hourRoller, lv_color_hex(0xe8b339), LV_PART_SELECTED);
    lv_obj_set_style_bg_color(refs.hourRoller, lv_color_hex(0x1a1a1a), LV_PART_SELECTED);

    lv_obj_t* colon = lv_label_create(row);
    lv_label_set_text(colon, ":");
    lv_obj_set_style_text_color(colon, lv_color_hex(0xe8b339), 0);

    // Build "00".."59" at runtime since 60 literal lines isn't worth
    // hand-writing like the hour list above.
    static char minuteOptions[300];
    minuteOptions[0] = '\0';
    for (int m = 0; m < 60; m++) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02d", m);
        strcat(minuteOptions, buf);
        if (m < 59) strcat(minuteOptions, "\n");
    }
    refs.minuteRoller = lv_roller_create(row);
    lv_roller_set_options(refs.minuteRoller, minuteOptions, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(refs.minuteRoller, initialMinute, LV_ANIM_OFF);
    lv_obj_set_style_text_color(refs.minuteRoller, lv_color_hex(0xe8b339), LV_PART_SELECTED);
    lv_obj_set_style_bg_color(refs.minuteRoller, lv_color_hex(0x1a1a1a), LV_PART_SELECTED);

    return refs;
}

// Generic single-roller picker for things like the debt chart's year-range
// selector or the rate unit selector (SEC/MIN/HOUR/DAY) -- same component,
// just one roller instead of two, with arbitrary string options.
inline lv_obj_t* addOptionPicker(lv_obj_t* card, const char* newlineSeparatedOptions, int initialIndex) {
    lv_obj_t* roller = lv_roller_create(card);
    lv_roller_set_options(roller, newlineSeparatedOptions, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(roller, initialIndex, LV_ANIM_OFF);
    lv_obj_set_style_text_color(roller, lv_color_hex(0xff4d4d), LV_PART_SELECTED); // red, matches the debt-screen accent in the simulator; pass a color param here if other screens reuse this with a different accent
    lv_obj_set_style_bg_color(roller, lv_color_hex(0x1a1a1a), LV_PART_SELECTED);
    lv_obj_center(roller);
    return roller;
}

// Real QR code, using LVGL's built-in lv_qrcode widget (wraps nayuki's
// QR-Code-generator -- see LV_USE_QRCODE in lv_conf.h). Replaces the
// dashed-border placeholder box that was here originally; encodes
// `data` (e.g. "https://turbousd.com/setup/A3F2") directly, no external
// QR library or pre-generated bitmap needed.
inline lv_obj_t* addQrCode(lv_obj_t* card, const char* data, lv_coord_t size) {
    lv_obj_t* qr = lv_qrcode_create(card, size, lv_color_black(), lv_color_white());
    lv_qrcode_update(qr, data, strlen(data));
    lv_obj_set_style_border_color(qr, lv_color_hex(0x2eaa50), 0); // green, matching every other modal border
    lv_obj_set_style_border_width(qr, 2, 0);
    return qr;
}

// Two-option segmented toggle (e.g. "C | F", "24H | AM/PM") -- the closest
// LVGL equivalent to the simulator's prefToggleRow(), since LVGL has no
// stock "segmented control" widget. Calls onChange(0) or onChange(1)
// immediately on tap (no separate Save step, matching the simulator's
// behavior for these specific preference rows).
// Holds what a toggle button's click handler needs: its sibling button
// (to recolor both halves of the pair) and the user-supplied callback.
struct PrefTogglePair {
    lv_obj_t* leftBtn;
    lv_obj_t* rightBtn;
    lv_obj_t* leftLbl;
    lv_obj_t* rightLbl;
    void (*onChange)(bool leftNowActive);
};

inline void applyPrefToggleColors(PrefTogglePair* pair, bool leftActive) {
    lv_obj_set_style_bg_color(pair->leftBtn, leftActive ? lv_color_hex(0x2eaa50) : lv_color_transparent(), 0);
    lv_obj_set_style_text_color(pair->leftLbl, leftActive ? lv_color_hex(0x06150a) : lv_color_hex(0x9a9a9e), 0);
    lv_obj_set_style_bg_color(pair->rightBtn, !leftActive ? lv_color_hex(0x2eaa50) : lv_color_transparent(), 0);
    lv_obj_set_style_text_color(pair->rightLbl, !leftActive ? lv_color_hex(0x06150a) : lv_color_hex(0x9a9a9e), 0);
}

inline void addPrefToggleRow(lv_obj_t* card, const char* label, const char* leftLabel, const char* rightLabel,
                              bool leftActive, void (*onChange)(bool leftNowActive)) {
    lv_obj_t* row = lv_obj_create(card);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* lbl = lv_label_create(row);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x9a9a9e), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);

    lv_obj_t* group = lv_obj_create(row);
    lv_obj_set_size(group, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(group, LV_OPA_0, 0);
    lv_obj_set_style_border_color(group, lv_color_hex(0x2eaa50), 0);
    lv_obj_set_style_border_width(group, 1, 0);
    lv_obj_set_style_radius(group, 6, 0);
    lv_obj_set_style_pad_all(group, 0, 0);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(group, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* leftBtn = lv_btn_create(group);
    lv_obj_set_style_radius(leftBtn, 0, 0);
    lv_obj_t* leftLbl = lv_label_create(leftBtn);
    lv_label_set_text(leftLbl, leftLabel);
    lv_obj_set_style_text_font(leftLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(leftLbl);

    lv_obj_t* rightBtn = lv_btn_create(group);
    lv_obj_set_style_radius(rightBtn, 0, 0);
    lv_obj_t* rightLbl = lv_label_create(rightBtn);
    lv_label_set_text(rightLbl, rightLabel);
    lv_obj_set_style_text_font(rightLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(rightLbl);

    // Heap-allocate one PrefTogglePair per row and leak it deliberately --
    // these popups live as long as the device is on, there's no teardown
    // path that would make freeing this matter, and it's a handful of
    // bytes per modal open. Simpler than threading a free() through every
    // close path for no real benefit at this scale.
    PrefTogglePair* pair = new PrefTogglePair{ leftBtn, rightBtn, leftLbl, rightLbl, onChange };
    applyPrefToggleColors(pair, leftActive);

    lv_obj_set_user_data(leftBtn, pair);
    lv_obj_add_event_cb(leftBtn, [](lv_event_t* e) {
        auto* p = (PrefTogglePair*)lv_obj_get_user_data(lv_event_get_target(e));
        applyPrefToggleColors(p, true);
        p->onChange(true);
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_set_user_data(rightBtn, pair);
    lv_obj_add_event_cb(rightBtn, [](lv_event_t* e) {
        auto* p = (PrefTogglePair*)lv_obj_get_user_data(lv_event_get_target(e));
        applyPrefToggleColors(p, false);
        p->onChange(false);
    }, LV_EVENT_CLICKED, nullptr);
}
