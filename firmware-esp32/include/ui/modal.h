// include/ui/modal.h — generic popup component (always green-bordered, per
// the latest design decision) used for: alarm time picker, calendar,
// weather forecast, device config, and the verification status tooltip.
// Mirrors openModal() from the browser simulator.

#pragma once
#include <lvgl.h>

// Global count of currently-open modals. The screen carousel checks this so it
// never rotates the screen out from under an open popup (config, alarm picker,
// calendar, etc.) — that would orphan the modal's backdrop on the old screen.
inline int& _modalOpenCount() { static int c = 0; return c; }
inline bool anyModalOpen()    { return _modalOpenCount() > 0; }

// Returns the modal's content container -- callers add their own widgets
// (labels, rollers, buttons) as children of it. The dimmed background and
// the green-bordered card are already set up.
inline lv_obj_t* openModal(lv_obj_t* parent) {
    lv_obj_t* backdrop = lv_obj_create(parent);
    // Decrement on ANY delete path (X button, closeModal, screen teardown).
    _modalOpenCount()++;
    lv_obj_add_event_cb(backdrop, [](lv_event_t*) {
        if (_modalOpenCount() > 0) _modalOpenCount()--;
    }, LV_EVENT_DELETE, nullptr);
    lv_obj_set_size(backdrop, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(backdrop, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_70, 0);
    lv_obj_set_style_border_width(backdrop, 0, 0);
    lv_obj_set_style_radius(backdrop, 0, 0);
    lv_obj_clear_flag(backdrop, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* card = lv_obj_create(backdrop);
    lv_obj_set_size(card, LV_PCT(84), LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(card, LV_PCT(92), 0);   // cap + scroll if content is tall
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x111111), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2eaa50), 0); // always green, per design decision
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 18, 0);

    // CRITICAL: without a layout, every child the caller adds lands at (0,0) and
    // overlaps — that was the "deformed, everything-on-top-of-each-other" popup.
    // A vertical flex stacks them top-to-bottom with spacing; the CLOSE button
    // (added last) naturally ends up at the bottom.
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(card, 10, 0);
    // Popups only ever scroll vertically — never sideways. Without this, wide
    // inner content (day pickers, calendar grid, rollers) let the card scroll
    // horizontally, which felt broken.
    lv_obj_set_scroll_dir(card, LV_DIR_VER);

    // Every modal gets a round "X" in the top-right corner so it can always be
    // dismissed, regardless of whatever buttons the caller adds. IGNORE_LAYOUT
    // keeps it out of the vertical flex flow, floating in the corner.
    lv_obj_t* xBtn = lv_btn_create(card);
    lv_obj_add_flag(xBtn, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(xBtn, 30, 30);
    lv_obj_align(xBtn, LV_ALIGN_TOP_RIGHT, 10, -10);   // nudge into the padding corner
    lv_obj_set_style_radius(xBtn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(xBtn, lv_color_hex(0x262626), 0);
    lv_obj_set_style_border_width(xBtn, 0, 0);
    lv_obj_set_style_pad_all(xBtn, 0, 0);
    lv_obj_set_ext_click_area(xBtn, 8);
    lv_obj_t* xLbl = lv_label_create(xBtn);
    lv_label_set_text(xLbl, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(xLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(xLbl, lv_color_hex(0xcfcfd4), 0);
    lv_obj_center(xLbl);
    lv_obj_add_event_cb(xBtn, [](lv_event_t* e) {
        lv_obj_t* c = (lv_obj_t*)lv_event_get_user_data(e);
        lv_obj_del(lv_obj_get_parent(c));   // delete the backdrop → closes the modal
    }, LV_EVENT_CLICKED, card);

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
    // flex_grow so that when TWO buttons share a horizontal row (Cancel / Save)
    // they split the width evenly instead of each being 100% wide — which used
    // to overflow the card and push SAVE off-screen behind a horizontal scroll.
    // In a vertical card there's no free main-axis space, so a lone button just
    // sizes to its content and stays centred.
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_set_height(btn, LV_SIZE_CONTENT);
    lv_obj_set_style_min_width(btn, 96, 0);
    lv_obj_set_style_bg_color(btn, primary ? lv_color_hex(0x2eaa50) : lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(btn, primary ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x6a6a6e), 0);
    lv_obj_set_style_border_width(btn, primary ? 0 : 1, 0);
    lv_obj_set_style_radius(btn, 8, 0);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_color(lbl, primary ? lv_color_hex(0x06150a) : lv_color_hex(0x9a9a9e), 0);
    lv_obj_center(lbl);

    return btn;
}

// LVGL 8 roller gotcha: the default theme sets text_line_space (dpx(20)) on
// the roller's MAIN part only. The highlighted middle band is drawn by
// re-rendering the ENTIRE options label with the SELECTED part's styles and
// mapping its position proportionally onto the background label — and the
// SELECTED part's line_space resolves to 0 (part-specific lookup misses own
// MAIN; inheritance jumps to the parent). The two renders therefore drift
// apart the further the roller sits from its center, so the middle band
// showed an option from pages away (e.g. "40" with "02" right below it) and
// the visually-centered value wasn't the real selection. INFINITE mode made
// it obvious: its options label is 7× taller, so the drift is huge.
inline void fixRollerSelectedSpacing(lv_obj_t* roller) {
    lv_obj_set_style_text_line_space(
        roller, lv_obj_get_style_text_line_space(roller, LV_PART_MAIN), LV_PART_SELECTED);
}

// Hour/minute picker built on LVGL's built-in roller widget -- much less
// code than the hand-rolled drag physics the browser simulator needed,
// since lv_roller already implements exactly this scroll-to-select pattern.
struct TimePickerRefs {
    lv_obj_t* hourRoller = nullptr;
    lv_obj_t* minuteRoller = nullptr;
    lv_obj_t* ampmRoller = nullptr;   // only in 12-hour mode; nullptr in 24h
    bool      is24h       = true;     // how to read hourRoller back (see decodeHour)
};

// Convert the picker's current selection back to a 0–23 hour. In 24h mode the
// hour roller's index IS the hour. In 12h mode the hour roller lists
// 12,1,2,…,11 (index 0 = "12") and the AM/PM roller says which half of the day.
inline uint8_t decodePickerHour(const TimePickerRefs& refs) {
    if (refs.is24h || !refs.ampmRoller)
        return (uint8_t)lv_roller_get_selected(refs.hourRoller);
    int idx  = (int)lv_roller_get_selected(refs.hourRoller);   // 0="12", 1..11
    int h12  = (idx == 0) ? 12 : idx;                          // 1..12
    bool pm  = lv_roller_get_selected(refs.ampmRoller) == 1;
    if (pm) return (uint8_t)((h12 == 12) ? 12 : h12 + 12);     // 12 PM = noon
    return       (uint8_t)((h12 == 12) ?  0 : h12);            // 12 AM = midnight
}

inline TimePickerRefs addTimePicker(lv_obj_t* card, uint8_t initialHour, uint8_t initialMinute,
                                    bool is24h = true) {
    TimePickerRefs refs;
    refs.is24h = is24h;

    lv_obj_t* row = lv_obj_create(card);
    lv_obj_set_size(row, LV_PCT(100), 120);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    if (!is24h) lv_obj_set_style_pad_column(row, 6, 0);   // room for the AM/PM wheel
    // ONLY the rollers scroll. The container itself must not — dragging just
    // outside a roller used to scroll this whole section, which felt broken.
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);

    // 24h: 00..23, index == hour. 12h: 12,01..11 (index 0 = "12"), paired with
    // the AM/PM wheel — the standard iOS-style three-wheel time picker.
    static const char* hourOptions24 =
        "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23";
    static const char* hourOptions12 =
        "12\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11";
    refs.hourRoller = lv_roller_create(row);
    // NORMAL mode (finite list). INFINITE was tried and reverted: even with
    // the selected-band line-space fix the wheel misbehaved on hardware.
    lv_roller_set_options(refs.hourRoller, is24h ? hourOptions24 : hourOptions12, LV_ROLLER_MODE_NORMAL);
    // Selected index: 24h → the hour itself; 12h → hour % 12 (0/12 → "12" at idx 0).
    lv_roller_set_selected(refs.hourRoller, is24h ? initialHour : (initialHour % 12), LV_ANIM_OFF);
    lv_obj_set_style_text_color(refs.hourRoller, lv_color_hex(0xe8b339), LV_PART_SELECTED);
    lv_obj_set_style_bg_color(refs.hourRoller, lv_color_hex(0x1a1a1a), LV_PART_SELECTED);
    fixRollerSelectedSpacing(refs.hourRoller);

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
    lv_roller_set_options(refs.minuteRoller, minuteOptions, LV_ROLLER_MODE_NORMAL);  // finite, like the hours
    lv_roller_set_selected(refs.minuteRoller, initialMinute, LV_ANIM_OFF);
    lv_obj_set_style_text_color(refs.minuteRoller, lv_color_hex(0xe8b339), LV_PART_SELECTED);
    lv_obj_set_style_bg_color(refs.minuteRoller, lv_color_hex(0x1a1a1a), LV_PART_SELECTED);
    fixRollerSelectedSpacing(refs.minuteRoller);

    // Third wheel: AM / PM (12-hour mode only).
    if (!is24h) {
        refs.ampmRoller = lv_roller_create(row);
        lv_roller_set_options(refs.ampmRoller, "AM\nPM", LV_ROLLER_MODE_NORMAL);
        lv_roller_set_selected(refs.ampmRoller, initialHour < 12 ? 0 : 1, LV_ANIM_OFF);
        lv_obj_set_style_text_color(refs.ampmRoller, lv_color_hex(0xe8b339), LV_PART_SELECTED);
        lv_obj_set_style_bg_color(refs.ampmRoller, lv_color_hex(0x1a1a1a), LV_PART_SELECTED);
        fixRollerSelectedSpacing(refs.ampmRoller);
    }

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
    fixRollerSelectedSpacing(roller);
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
    lv_obj_set_style_bg_color(pair->leftBtn, leftActive ? lv_color_hex(0x2eaa50) : lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(pair->leftBtn, leftActive ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(pair->leftLbl, leftActive ? lv_color_hex(0x06150a) : lv_color_hex(0x9a9a9e), 0);
    lv_obj_set_style_bg_color(pair->rightBtn, !leftActive ? lv_color_hex(0x2eaa50) : lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(pair->rightBtn, !leftActive ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(pair->rightLbl, !leftActive ? lv_color_hex(0x06150a) : lv_color_hex(0x9a9a9e), 0);
}

// ── Numeric stepper row:  LABEL        [ − ]  value+suffix  [ + ] ────────────
struct StepperState {
    lv_obj_t* valLbl;
    int  val, minV, maxV, step;
    char suffix[6];
    void (*onChange)(int newVal);
};
inline void _stepperPaintValue(StepperState* s) {
    char b[16]; snprintf(b, sizeof(b), "%d%s", s->val, s->suffix);
    lv_label_set_text(s->valLbl, b);
}
inline void addStepperRow(lv_obj_t* card, const char* label, int initial,
                          int minV, int maxV, int step, const char* suffix,
                          void (*onChange)(int newVal)) {
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
    lv_obj_set_flex_align(group, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(group, LV_OBJ_FLAG_SCROLLABLE);

    const lv_coord_t STEP_BTN_W = 34, STEP_BTN_H = 28;

    // Field-by-field (not brace-aggregate-init): a string-literal initialiser for
    // the char suffix[] member inside the braced list doesn't compile under the
    // toolchain's C++ standard ("could not convert {...} to StepperState").
    StepperState* st = new StepperState();
    st->valLbl   = nullptr;
    st->val      = initial;
    st->minV     = minV;
    st->maxV     = maxV;
    st->step     = step;
    st->onChange = onChange;
    st->suffix[0] = '\0';
    strncpy(st->suffix, suffix ? suffix : "", sizeof(st->suffix) - 1);
    st->suffix[sizeof(st->suffix) - 1] = '\0';

    lv_obj_t* minusBtn = lv_btn_create(group);
    lv_obj_set_size(minusBtn, STEP_BTN_W, STEP_BTN_H);
    lv_obj_set_style_radius(minusBtn, 0, 0);
    lv_obj_set_style_pad_all(minusBtn, 0, 0);
    lv_obj_set_style_bg_color(minusBtn, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(minusBtn, LV_OPA_TRANSP, 0);
    lv_obj_t* minusLbl = lv_label_create(minusBtn);
    lv_label_set_text(minusLbl, "-");
    lv_obj_set_style_text_color(minusLbl, lv_color_hex(0x9a9a9e), 0);
    lv_obj_center(minusLbl);

    lv_obj_t* valLbl = lv_label_create(group);
    lv_obj_set_width(valLbl, 52);
    lv_obj_set_style_text_color(valLbl, lv_color_hex(0xd4d4d8), 0);
    lv_obj_set_style_text_font(valLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(valLbl, LV_TEXT_ALIGN_CENTER, 0);
    st->valLbl = valLbl;
    _stepperPaintValue(st);

    lv_obj_t* plusBtn = lv_btn_create(group);
    lv_obj_set_size(plusBtn, STEP_BTN_W, STEP_BTN_H);
    lv_obj_set_style_radius(plusBtn, 0, 0);
    lv_obj_set_style_pad_all(plusBtn, 0, 0);
    lv_obj_set_style_bg_color(plusBtn, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(plusBtn, LV_OPA_TRANSP, 0);
    lv_obj_t* plusLbl = lv_label_create(plusBtn);
    lv_label_set_text(plusLbl, "+");
    lv_obj_set_style_text_color(plusLbl, lv_color_hex(0x9a9a9e), 0);
    lv_obj_center(plusLbl);

    lv_obj_set_user_data(minusBtn, st);
    lv_obj_add_event_cb(minusBtn, [](lv_event_t* e) {
        auto* s = (StepperState*)lv_obj_get_user_data(lv_event_get_target(e));
        s->val -= s->step; if (s->val < s->minV) s->val = s->minV;
        _stepperPaintValue(s); s->onChange(s->val);
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_set_user_data(plusBtn, st);
    lv_obj_add_event_cb(plusBtn, [](lv_event_t* e) {
        auto* s = (StepperState*)lv_obj_get_user_data(lv_event_get_target(e));
        s->val += s->step; if (s->val > s->maxV) s->val = s->maxV;
        _stepperPaintValue(s); s->onChange(s->val);
    }, LV_EVENT_CLICKED, nullptr);
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

    // Fixed button size so every preference row's toggle is the SAME width
    // (matching the widest, DATE FORMAT's "DD/MM"/"MM/DD"), giving the settings
    // list tidy, aligned right edges instead of ragged per-label widths.
    const lv_coord_t TOGGLE_BTN_W = 62;
    const lv_coord_t TOGGLE_BTN_H = 28;

    lv_obj_t* leftBtn = lv_btn_create(group);
    lv_obj_set_size(leftBtn, TOGGLE_BTN_W, TOGGLE_BTN_H);
    lv_obj_set_style_radius(leftBtn, 0, 0);
    lv_obj_set_style_pad_all(leftBtn, 0, 0);
    lv_obj_t* leftLbl = lv_label_create(leftBtn);
    lv_label_set_text(leftLbl, leftLabel);
    lv_obj_set_style_text_font(leftLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(leftLbl);

    lv_obj_t* rightBtn = lv_btn_create(group);
    lv_obj_set_size(rightBtn, TOGGLE_BTN_W, TOGGLE_BTN_H);
    lv_obj_set_style_radius(rightBtn, 0, 0);
    lv_obj_set_style_pad_all(rightBtn, 0, 0);
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
