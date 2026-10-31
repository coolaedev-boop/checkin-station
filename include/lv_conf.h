// Minimal LVGL 9.2 config. Anything not set here uses LVGL's built-in default.
#if 1
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

// LVGL's widget memory lives in PSRAM so internal RAM stays free for Wi-Fi, BLE and TLS.
// 2 MB of the 8 MB PSRAM: 1 MB for widgets (each list row costs memory, and the item list
// can get long) plus 1 MB for the image cache below, which is carved out of the same heap.
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_MEM_SIZE          (2 * 1024 * 1024U)
#define LV_MEM_POOL_INCLUDE  <esp32-hal-psram.h>
#define LV_MEM_POOL_ALLOC    ps_malloc

#define LV_DEF_REFR_PERIOD 16          // check for redraws every 16 ms (60 FPS ceiling)
#define LV_USE_OS          LV_OS_NONE  // only the UI loop touches LVGL

// Lexend, from the design. Generated into src/ui/fonts by tools/make_fonts.ps1.
// LVGL's own Montserrat fonts aren't used any more.
#define LV_FONT_MONTSERRAT_14  0
#define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(lexend_light_13)
#define LV_FONT_DEFAULT        &lexend_light_13

// Item images: baseline JPEGs read from flash as "L:/items/<barcode>.jpg"
#define LV_USE_TJPGD 1
#define LV_USE_FS_ARDUINO_ESP_LITTLEFS 1
#define LV_FS_ARDUINO_ESP_LITTLEFS_LETTER 'L'

// Keep decoded item photos so a redraw strip doesn't run TJPGD again. A 120x120 photo is
// about 29 KB decoded, so 1 MB holds dozens. The header cache saves re-reading file headers.
#define LV_CACHE_DEF_SIZE              (1024 * 1024)
#define LV_IMAGE_HEADER_CACHE_DEF_CNT  16

// Set both to 1 to show an FPS / CPU overlay while tuning
#define LV_USE_SYSMON       0
#define LV_USE_PERF_MONITOR 0

#endif
#endif
