// Minimal LVGL 9.2 config. Anything not set here uses LVGL's built-in default.
#if 1
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

// LVGL's widget memory lives in PSRAM so internal RAM stays free for Wi-Fi, BLE and TLS.
// 1 MB of the 8 MB PSRAM: each list row costs memory, and the item list can get long.
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_MEM_SIZE          (1024 * 1024U)
#define LV_MEM_POOL_INCLUDE  <esp32-hal-psram.h>
#define LV_MEM_POOL_ALLOC    ps_malloc

#define LV_DEF_REFR_PERIOD 16          // check for redraws every 16 ms (60 FPS ceiling)
#define LV_USE_OS          LV_OS_NONE  // only the UI loop touches LVGL

#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_DEFAULT       &lv_font_montserrat_16

// Item images: baseline JPEGs read from flash as "L:/items/<barcode>.jpg"
#define LV_USE_TJPGD 1
#define LV_USE_FS_ARDUINO_ESP_LITTLEFS 1
#define LV_FS_ARDUINO_ESP_LITTLEFS_LETTER 'L'

// Set both to 1 to show an FPS / CPU overlay while tuning
#define LV_USE_SYSMON       0
#define LV_USE_PERF_MONITOR 0

#endif
#endif
