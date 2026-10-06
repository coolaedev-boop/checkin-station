#pragma once
// ============================================================================
//  Display: the ST7796S panel, capacitive touch, and LVGL wired together.
//  After begin(), you only ever talk to LVGL (through Ui); this module keeps
//  the pixels flowing to the screen and touches flowing back.
// ============================================================================

namespace Display {
  bool begin();   // panel + touch + LVGL. Returns false if PSRAM is missing (check platformio.ini)
  void tick();    // let LVGL redraw whatever changed; call every few ms from the UI loop
}
