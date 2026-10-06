#pragma once
// ============================================================================
//  Ui: the three touchscreen tabs (Check in, Check out, System).
//  It only reads from the other modules' APIs; it never touches Bluetooth,
//  Wi-Fi or Sheets directly. LVGL isn't thread-safe, so call these ONLY from
//  the Arduino loop (the UI task).
// ============================================================================

namespace Ui {
  void begin();    // build all three tabs (call after Display::begin)
  void update();   // pull new scans, data changes and scanner search results onto the screen
}
