// include/ui/font_tenge.h — hand-made ₸ (U+20B8, tenge / TurboUSD symbol)
// glyph fonts. LVGL's built-in Montserrat fonts don't cover U+20B8, so the
// Node screen used a plain "T" for rewards. Same pattern as the Ξ/₿ font in
// screen_nft.h: a 1-glyph 1bpp font that falls back to the matching
// Montserrat size for every other character, so one label renders "₸12.34".
//
// Bitmaps are bit-packed row-major (LVGL fmt_txt, no row padding), generated
// programmatically — see the row art in the comments.

#pragma once
#include <lvgl.h>

// ₸ 7×7 for montserrat_10:  ▬▬▬ / · / ▬▬▬ / four stem rows
static const uint8_t s_tenge10Bitmap[] = {
    0xFE, 0x03, 0xF8, 0x81, 0x02, 0x04, 0x00,
};
// ₸ 9×9 for montserrat_12
static const uint8_t s_tenge12Bitmap[] = {
    0xFF, 0x80, 0x3F, 0xE1, 0x00, 0x80, 0x40, 0x20, 0x10, 0x08, 0x00,
};
// ₸ 13×14 for montserrat_20 (2 px bars + 2 px stem)
static const uint8_t s_tenge20Bitmap[] = {
    0xFF, 0xFF, 0xFF, 0xC0, 0x01, 0xFF, 0xFF, 0xFF, 0x83, 0x00, 0x18, 0x00,
    0xC0, 0x06, 0x00, 0x30, 0x01, 0x80, 0x0C, 0x00, 0x60, 0x03, 0x00,
};

static const lv_font_fmt_txt_glyph_dsc_t s_tenge10Glyphs[] = {
    {0, 0,   0, 0, 0, 0},
    {0, 144, 7, 7, 1, 0},    // adv 9 px (144/16)
};
static const lv_font_fmt_txt_glyph_dsc_t s_tenge12Glyphs[] = {
    {0, 0,   0, 0, 0, 0},
    {0, 176, 9, 9, 1, 0},    // adv 11 px
};
static const lv_font_fmt_txt_glyph_dsc_t s_tenge20Glyphs[] = {
    {0, 0,   0,  0,  0, 0},
    {0, 240, 13, 14, 1, 0},  // adv 15 px
};

// Builds one static font per size on first use (field-by-field, like
// ethXiFont10 — aggregate init breaks whenever the struct layout changes).
static const lv_font_t* _tengeFontFor(const lv_font_t* base,
                                      const uint8_t* bitmap,
                                      const lv_font_fmt_txt_glyph_dsc_t* glyphs,
                                      lv_font_fmt_txt_cmap_t* cmap,
                                      lv_font_fmt_txt_dsc_t* dsc,
                                      lv_font_fmt_txt_glyph_cache_t* cache,
                                      lv_font_t* font, bool* ready) {
    if (!*ready) {
        memset(cmap, 0, sizeof(*cmap));
        cmap->range_start    = 0x20B8;   // ₸
        cmap->range_length   = 1;
        cmap->glyph_id_start = 1;
        cmap->type           = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY;

        memset(dsc, 0, sizeof(*dsc));
        dsc->glyph_bitmap = bitmap;
        dsc->glyph_dsc    = glyphs;
        dsc->cmaps        = cmap;
        dsc->cmap_num     = 1;
        dsc->bpp          = 1;
        dsc->cache        = cache;

        memset(font, 0, sizeof(*font));
        font->get_glyph_dsc    = lv_font_get_glyph_dsc_fmt_txt;
        font->get_glyph_bitmap = lv_font_get_bitmap_fmt_txt;
        font->line_height      = base->line_height;
        font->base_line        = base->base_line;
        font->subpx            = LV_FONT_SUBPX_NONE;
        font->dsc              = dsc;
        font->fallback         = base;   // digits, letters, punctuation
        *ready = true;
    }
    return font;
}

static const lv_font_t* tengeFont10() {
    static bool ready = false;
    static lv_font_fmt_txt_cmap_t cmap; static lv_font_fmt_txt_dsc_t dsc;
    static lv_font_fmt_txt_glyph_cache_t cache; static lv_font_t font;
    return _tengeFontFor(&lv_font_montserrat_10, s_tenge10Bitmap, s_tenge10Glyphs,
                         &cmap, &dsc, &cache, &font, &ready);
}
static const lv_font_t* tengeFont12() {
    static bool ready = false;
    static lv_font_fmt_txt_cmap_t cmap; static lv_font_fmt_txt_dsc_t dsc;
    static lv_font_fmt_txt_glyph_cache_t cache; static lv_font_t font;
    return _tengeFontFor(&lv_font_montserrat_12, s_tenge12Bitmap, s_tenge12Glyphs,
                         &cmap, &dsc, &cache, &font, &ready);
}
static const lv_font_t* tengeFont20() {
    static bool ready = false;
    static lv_font_fmt_txt_cmap_t cmap; static lv_font_fmt_txt_dsc_t dsc;
    static lv_font_fmt_txt_glyph_cache_t cache; static lv_font_t font;
    return _tengeFontFor(&lv_font_montserrat_20, s_tenge20Bitmap, s_tenge20Glyphs,
                         &cmap, &dsc, &cache, &font, &ready);
}
