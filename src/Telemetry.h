#pragma once
// ============================================================================
//  Telemetry: read the ESP32's health (memory, temperature, signal, uptime).
// ============================================================================
#include <Arduino.h>

struct SystemMetrics {
  uint32_t    uptimeSec;
  uint32_t    heapFree;       // internal RAM free right now (bytes)
  uint32_t    heapMin;        // lowest heapFree has ever been since boot
  uint32_t    heapMaxAlloc;   // biggest single block you could allocate
  uint32_t    heapTotal;
  uint32_t    psramFree;
  uint32_t    psramTotal;
  float       chipTempC;      // internal die temperature, rough
  int8_t      wifiRssi;       // dBm, 0 when offline
  uint32_t    cpuMhz;
  const char* resetReason;    // why the chip last rebooted: "power_on", "panic", "brownout"...
};

namespace Telemetry {
  void          begin();      // call once at boot
  SystemMetrics read();       // snapshot of the chip's health right now
  const char*   deviceId();   // 12-char hex ID from the chip's MAC, e.g. "34B7DA5A1C08"
}
