// ============================================================================
//  Check-in station: the whole app, wired together.
//
//  Core 0: Scanner task (Bluetooth)  +  Network task (Wi-Fi)  +  Cloud task (Apps Script)
//  Core 1: this loop, the UI task (touchscreen / LVGL)
//  The modules talk only through their APIs; see README.md for the map.
// ============================================================================
#include <Arduino.h>
#include <LittleFS.h>
#include "Cloud.h"
#include "Display.h"
#include "Inventory.h"
#include "Network.h"
#include "Scanner.h"
#include "Telemetry.h"
#include "Ui.h"

// The UI runs on this loop task. LVGL's JPEG decoder puts a 4 KB buffer on the stack,
// which overflows the default 8 KB stack and reboots the board when an item image loads.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

// Type a barcode into the Serial Monitor + Enter to fake a scan (no scanner needed).
static void readSerialScans() {
  static String line;
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r' || c == '\n') {
      line.trim();
      if (line.length()) Scanner::simulateScan(line.c_str());
      line = "";
    } else {
      line += c;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  LittleFS.begin(true);          // flash storage: item images + cached Sheets data

  Telemetry::begin();
  Inventory::begin();            // cached data first, so the screen has something to show

  if (!Display::begin()) {
    Serial.println("[main] display failed to start, halting");
    while (true) delay(1000);
  }
  Ui::begin();

  Scanner::begin();              // Bluetooth task on core 0
  Network::begin();              // Wi-Fi task on core 0: saved network, or config.h's
  Cloud::begin();                // Apps Script task on core 0, waits for Wi-Fi
}

void loop() {
  readSerialScans();
  Ui::update();                  // scans, data changes, search results -> screen
  Display::tick();               // LVGL redraws whatever changed
  delay(5);
}
