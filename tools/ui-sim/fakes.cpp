// Fakes for the hardware-facing modules, so the real UI and Inventory run on a PC.
// The test drives them through the sim:: hooks in sim.h.
#include "sim.h"
#include "Cloud.h"
#include "Network.h"
#include "Scanner.h"
#include "Telemetry.h"
#include <LittleFS.h>
#include <deque>

SerialStub   Serial;
LittleFSStub LittleFS;

namespace sim {
uint32_t                 nowMs = 0;
std::vector<std::string> events;
std::deque<std::string>  scans;
uint32_t                 foundRev = 0, networksRev = 0;
int                      discoverCalls = 0, wifiScanCalls = 0, connectCalls = 0, syncCalls = 0;
std::string              joined;
}  // namespace sim

uint32_t millis() { return sim::nowMs; }

// ---- Cloud: records events instead of uploading ----------------------------------
void Cloud::begin() {}
void Cloud::sendEvent(const char* action, const std::string& barcode, const std::string& cls,
                      const std::string& person, const std::string& project) {
  sim::events.push_back(std::string(action) + "|" + barcode + "|" + cls + "|" + person + "|" + project);
}
void        Cloud::requestSync() { ++sim::syncCalls; }
CloudStatus Cloud::status() { return {true, -49, 200, CloudLink::Ok, 0, 12}; }
const char* Cloud::linkText(CloudLink) { return "OK"; }

// ---- Scanner: a saved, connected scanner and three found devices ------------------
bool Scanner::nextBarcode(std::string& out) {
  if (sim::scans.empty()) return false;
  out = sim::scans.front();
  sim::scans.pop_front();
  return true;
}
void Scanner::begin() {}
void Scanner::discover(bool) { ++sim::discoverCalls; ++sim::foundRev; }
std::vector<ScannerInfo> Scanner::found() {
  return {{"NT-1228BC", "aa:bb:cc:dd:ee:01", 0, -48}, {"BarCode Scanner BLE", "aa:bb:cc:dd:ee:02", 0, -63},
          {"Inateck BCST-70", "aa:bb:cc:dd:ee:03", 0, -77}};
}
uint32_t      Scanner::foundRevision() { return sim::foundRev; }
void          Scanner::connect(const ScannerInfo&) { ++sim::connectCalls; }
void          Scanner::forget() {}
void          Scanner::simulateScan(const std::string& code) { sim::scans.push_back(code); }
ScannerStatus Scanner::status() { return {ScannerState::Connected, "NT-1228BC", "aa:bb:cc:dd:ee:01", true}; }

// ---- Network: on a saved network, three in range ----------------------------------
void Network::begin() {}
void Network::scan() { ++sim::wifiScanCalls; ++sim::networksRev; }
std::vector<WifiNetwork> Network::networks() {
  return {{"NCCA-Staff", -49, WifiSecurity::Password}, {"NCCA-Students", -58, WifiSecurity::Enterprise},
          {"Library Guest", -71, WifiSecurity::Open}};
}
uint32_t      Network::networksRevision() { return sim::networksRev; }
void          Network::join(const std::string& ssid, const std::string&) { sim::joined = ssid; }
void          Network::forget() {}
bool          Network::connected() { return true; }
NetworkStatus Network::status() {
  return {NetworkState::Connected, "NCCA-Staff", -49, false, true, JoinResult::None, ""};
}

// ---- Telemetry ---------------------------------------------------------------------
void          Telemetry::begin() {}
SystemMetrics Telemetry::read() {
  return {7980, 93184, 28672, 65536, 327680, 7340032, 8388608, 37.0f, -49, 240, "power_on"};
}
const char* Telemetry::deviceId() { return "SIMULATOR000"; }
