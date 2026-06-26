// include/lv_conf.h — LVGL v8 configuration for the TurboUSD Node firmware.
// Enabled via -DLV_CONF_INCLUDE_SIMPLE in platformio.ini, which makes LVGL
// do #include "lv_conf.h" via the normal include path instead of the
// relative ../../lv_conf.h path that doesn't exist in PlatformIO.

#if 1  /* Required by LVGL's lv_conf_internal.h — do not remove */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_DEPTH     16   /* RGB565 — matches the SenseCAP D1's LCD panel */
#define LV_COLOR_16_SWAP   0    /* Flip to 1 if colors look wrong on first boot */

/*====================
   MEMORY SETTINGS
 *====================*/
/* Use stdlib malloc/free. With CONFIG_SPIRAM_USE_MALLOC this automatically
 * uses PSRAM on the ESP32-S3 — plenty of room for LVGL's draw buffers. */
#define LV_MEM_CUSTOM      1
#define LV_MEM_CUSTOM_INCLUDE  <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC    malloc
#define LV_MEM_CUSTOM_FREE     free
#define LV_MEM_CUSTOM_REALLOC  realloc

/*====================
   HAL / TICK
 *====================*/
/* Feed LVGL's internal tick from Arduino millis() — no separate task needed */
#define LV_TICK_CUSTOM                   1
#define LV_TICK_CUSTOM_INCLUDE           <Arduino.h>
#define LV_TICK_CUSTOM_SYS_TIME_EXPR     millis()

#define LV_DISP_DEF_REFR_PERIOD   16    /* ~60 fps */
#define LV_INDEV_DEF_READ_PERIOD  30    /* touch polling interval (ms) */

/*====================
   DRAW / RENDER
 *====================*/
#define LV_DRAW_COMPLEX    1   /* rounded corners, gradients, shadow */
#define LV_SHADOW_CACHE_SIZE    0
#define LV_CIRCLE_CACHE_SIZE    4
#define LV_IMG_CACHE_DEF_SIZE   0

/*====================
   LOGGING / DEBUG
 *====================*/
#define LV_USE_LOG         0   /* Flip to 1 temporarily to debug LVGL alloc warnings */
/* Only define LV_LOG_LEVEL when logging is enabled. When LV_USE_LOG==0,
 * lv_conf_internal.h forces LV_LOG_LEVEL to LV_LOG_LEVEL_NONE; defining it
 * here unconditionally caused a "LV_LOG_LEVEL redefined" warning at build. */
#if LV_USE_LOG
#define LV_LOG_LEVEL       LV_LOG_LEVEL_WARN
#endif
#define LV_USE_PERF_MONITOR 0  /* Flip to 1 to show FPS overlay for jank diagnosis */
#define LV_USE_MEM_MONITOR  0

/*====================
   FONTS
 *====================*/
#define LV_FONT_DEFAULT    &lv_font_montserrat_14

/* Montserrat sizes used in firmware-esp32/include/ui/*.h */
#define LV_FONT_MONTSERRAT_10  1
#define LV_FONT_MONTSERRAT_12  1
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_MONTSERRAT_16  1
#define LV_FONT_MONTSERRAT_20  1
#define LV_FONT_MONTSERRAT_32  1
#define LV_FONT_MONTSERRAT_48  1

/*====================
   WIDGETS
 *====================*/
#define LV_USE_ARC         1
#define LV_USE_BAR         1
#define LV_USE_BTN         1
#define LV_USE_BTNMATRIX   1
#define LV_USE_CANVAS      1   /* REQUIRED: lv_qrcode (below) is a canvas wrapper —
                                * it calls lv_canvas_create/set_buffer/set_palette.
                                * With this at 0 the build failed with
                                * "'lv_canvas_class' undeclared" in lv_qrcode.c. */
#define LV_USE_CHECKBOX    1
#define LV_USE_CHART       1   /* screen_turbo.h candle chart */
#define LV_USE_COLORWHEEL  0
#define LV_USE_DROPDOWN    1
#define LV_USE_IMG         1
#define LV_USE_IMGBTN      0
#define LV_USE_KEYBOARD    1   /* screen_tickers.h + screen_nft.h use lv_keyboard_create */
#define LV_USE_LABEL       1
#define LV_USE_LED         0
#define LV_USE_LINE        1
#define LV_USE_LIST        1
#define LV_USE_MENU        0
#define LV_USE_METER       0
#define LV_USE_MSGBOX      0
#define LV_USE_ROLLER      1
#define LV_USE_CALENDAR    1
#define LV_USE_SLIDER      1
#define LV_USE_SPAN        0
#define LV_USE_SPINBOX     0
#define LV_USE_SPINNER     1
#define LV_USE_SWITCH      1
#define LV_USE_TEXTAREA    1   /* screen_tickers.h + screen_nft.h use lv_textarea_create */
#define LV_USE_TABLE       0
#define LV_USE_TABVIEW     1
#define LV_USE_TILEVIEW    1   /* ui_manager.h screen-swipe navigation */
#define LV_USE_WIN         0

#define LV_USE_QRCODE      1   /* modal.h setup QR — uses LVGL's bundled qrcodegen
                                * (src/extra/libs/qrcode) and renders onto a canvas,
                                * so LV_USE_CANVAS above must also be 1. */

/*====================
   THEME
 *====================*/
#define LV_USE_THEME_DEFAULT    1
#define LV_THEME_DEFAULT_DARK   1
#define LV_THEME_DEFAULT_GROW   0
#define LV_THEME_DEFAULT_TRANSITION_TIME  80
#define LV_USE_THEME_SIMPLE     0
#define LV_USE_THEME_MONO       0

/*====================
   LAYOUTS
 *====================*/
#define LV_USE_FLEX    1   /* row/column flex used throughout ui/ */
#define LV_USE_GRID    0

/*====================
   TEXT
 *====================*/
#define LV_TXT_ENC         LV_TXT_ENC_UTF8
#define LV_USE_BIDI        0
#define LV_USE_ARABIC_PERSIAN_CHARS 0

/*====================
   MISC
 *====================*/
#define LV_USE_SNAPSHOT    0
#define LV_USE_MONKEY      0
#define LV_USE_GRIDNAV     0
#define LV_USE_FRAGMENT    0
#define LV_BUILD_EXAMPLES  0

#endif  /* LV_CONF_H */
#endif  /* Content enable guard */
