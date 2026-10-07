#ifndef SIM_LV_CONF_H
#define SIM_LV_CONF_H
// The station's own LVGL settings, minus the ESP32-only parts.
#include "../../../include/lv_conf.h"
#undef LV_MEM_POOL_INCLUDE
#undef LV_MEM_POOL_ALLOC
#undef LV_USE_FS_ARDUINO_ESP_LITTLEFS
#define LV_USE_FS_ARDUINO_ESP_LITTLEFS 0
#undef LV_USE_TJPGD
#define LV_USE_TJPGD 0
#endif
