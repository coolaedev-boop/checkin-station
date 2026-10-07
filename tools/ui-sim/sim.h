#pragma once
// Hooks into the fakes (fakes.cpp) for the simulator's test script (main.cpp).
#include <deque>
#include <stdint.h>
#include <string>
#include <vector>

namespace sim {
extern uint32_t                 nowMs;          // millis() and the LVGL tick
extern std::vector<std::string> events;         // "action|barcode|class|person|project" per Cloud::sendEvent
extern std::deque<std::string>  scans;          // barcodes waiting for Scanner::nextBarcode
extern uint32_t                 foundRev, networksRev;
extern int                      discoverCalls, wifiScanCalls, connectCalls, syncCalls;
extern std::string              joined;         // last Network::join() SSID
}  // namespace sim
