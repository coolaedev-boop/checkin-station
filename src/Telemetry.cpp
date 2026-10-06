#include "Telemetry.h"
#include <WiFi.h>
#include <esp_system.h>
#include <esp_timer.h>

namespace {
char g_deviceId[13] = "000000000000";

const char* resetReasonStr(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "power_on";
    case ESP_RST_SW:        return "software";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:   return "int_wdt";
    case ESP_RST_TASK_WDT:  return "task_wdt";
    case ESP_RST_WDT:       return "wdt";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_DEEPSLEEP: return "deep_sleep";
    case ESP_RST_EXT:       return "external";
    default:                return "other";
  }
}
}  // namespace

void Telemetry::begin() {
  snprintf(g_deviceId, sizeof(g_deviceId), "%012llX", (unsigned long long)ESP.getEfuseMac());
}

const char* Telemetry::deviceId() { return g_deviceId; }

SystemMetrics Telemetry::read() {
  SystemMetrics m{};
  m.uptimeSec    = static_cast<uint32_t>(esp_timer_get_time() / 1000000LL);
  m.heapFree     = ESP.getFreeHeap();
  m.heapMin      = ESP.getMinFreeHeap();
  m.heapMaxAlloc = ESP.getMaxAllocHeap();
  m.heapTotal    = ESP.getHeapSize();
  m.psramFree    = ESP.getFreePsram();
  m.psramTotal   = ESP.getPsramSize();
  m.chipTempC    = temperatureRead();
  m.wifiRssi     = WiFi.isConnected() ? WiFi.RSSI() : 0;
  m.cpuMhz       = ESP.getCpuFreqMHz();
  m.resetReason  = resetReasonStr(esp_reset_reason());
  return m;
}
