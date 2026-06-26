// include/lv_conf.h — LVGL v8 configuration for the TurboUSD Node firmware.
// This is LVGL's standard config file (normally copied from
// lvgl/lv_conf_template.h and trimmed down) -- only the settings that
// differ from defaults or that this project specifically depends on are
// annotated below. If something doesn't compile because a widget/feature
// is missing, it's almost certainly because it needs to be enabled here.

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_COLOR_DEPTH 16          // matches the SenseCAP D1's RGB565 panel
#define LV_COLOR_16_SWAP 0         // confirm against the actual panel's byte order; flip if colors look swapped on first boot

#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (64U * 1024U)  // adjust upward if you see LV_LOG_WARN allocation failures in practice; 64KB is a starting point, not a measured value

#define LV_USE_PERF_MONITOR 0      // flip to 1 temporarily if a screen feels janky and you want to see the FPS overlay

// --- Widgets this firmware actually uses (see include/ui/*.h for where) ---
#define LV_USE_LABEL     1
#define LV_USE_BTN       1
#define LV_USE_IMG       1
#define LV_USE_CHART     1
#define LV_USE_ROLLER    1
#define LV_USE_CALENDAR  1
#define LV_USE_ARC       1
#define LV_USE_LINE      1
#define LV_USE_QRCODE    1   // wraps nayuki's QR-Code-generator; used for the real
                              // setup QR in the config popup, see ui/modal.h

// --- Fonts actually referenced across the ui/ headers ---
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_48 1

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN

#endif // LV_CONF_H
