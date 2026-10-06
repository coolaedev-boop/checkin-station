#pragma once
// ============================================================================
//  Scanner: finds and connects to Bluetooth LE barcode scanners, turns their
//  data into barcode strings, and remembers the last scanner across reboots.
//
//  Handles both kinds of BLE scanner automatically:
//   - HID mode (acts like a keyboard): decodes key presses into text
//   - Serial/BLE mode (sends raw text): collects characters into lines
//  Bluetooth Classic-only scanners can't work: the ESP32-S3 has no Classic radio.
// ============================================================================
#include <Arduino.h>
#include <string>
#include <vector>

struct ScannerInfo {
  std::string name;          // advertised name, "(no name)" if blank
  std::string address;       // MAC, e.g. "aa:bb:cc:dd:ee:ff"
  uint8_t     addressType;   // public/random; needed to reconnect
  int         rssi;          // dBm, closer to 0 = closer to the station
};

enum class ScannerState : uint8_t { Idle, Discovering, Connecting, Connected };

struct ScannerStatus {
  ScannerState state;
  std::string  name;         // connected / saved scanner's name
  std::string  address;
  bool         hasSavedDevice;
};

namespace Scanner {
  void begin();                                // start Bluetooth; auto-reconnects to the saved scanner

  void discover(bool scannersOnly = true);     // search in the background for DISCOVERY_DURATION_MS
  std::vector<ScannerInfo> found();            // results of the last search, strongest signal first
  uint32_t foundRevision();                    // bumps when found() changes

  void connect(const ScannerInfo& device);     // connect and remember it as the station's scanner
  void forget();                               // disconnect and clear the saved scanner

  bool nextBarcode(std::string& out);          // pop the next scanned barcode; false if none waiting
  void simulateScan(const std::string& code);  // testing: behaves exactly like a real scan

  ScannerStatus status();
}
