// Settings PIN: 6 digits (SETTINGS_PIN in config.h), asked every time Settings opens.
// Figma frame "Settings Screen" (1002:838). The design's 31x16 keys are too small for
// fingers, so the keypad is a 3x4 grid of 60x28 keys (1-9, backspace, 0, Back). To fit it
// above the bottom border, "Enter PIN..." moves up next to the title and the PIN bar is
// 36 px tall instead of 44.
#include "Chrome.h"
#include "Screens.h"
#include "Theme.h"
#include "config.h"
#include <string.h>

using namespace Theme;

namespace PinScreen {
namespace {
constexpr size_t PIN_LEN = 6;
constexpr int32_t KEY_W = 60, KEY_H = 28, KEY_GAP = 4;

lv_obj_t*   g_dots[PIN_LEN] = {};
std::string g_entry;

const char* const KEYS[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", LV_SYMBOL_BACKSPACE, "0", "Back"};

void showEntry() {
  for (size_t i = 0; i < PIN_LEN; ++i) setVisible(g_dots[i], i < g_entry.size());
}

void onKey(lv_event_t* e) {
  const size_t k = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
  if (k == 11) { Nav::back(); return; }
  if (k == 9) {
    if (!g_entry.empty()) g_entry.pop_back();
  } else if (g_entry.size() < PIN_LEN) {
    g_entry += KEYS[k];
  }
  showEntry();
  if (g_entry.size() < PIN_LEN) return;
  const bool right = g_entry == SETTINGS_PIN;
  g_entry.clear();
  showEntry();
  if (right) Nav::unlockSettings();
  else       Chrome::toast("Wrong PIN. Try again.", true);
}
}  // namespace

void build(lv_obj_t* scr) {
  title(scr, "Settings");
  text(scr, "Enter PIN...", F_HEADING, BLACK, 146, 58);

  rect(scr, 7, 104, 465, 36, CARD, 15);
  for (size_t i = 0; i < PIN_LEN; ++i) {
    lv_obj_t* cell = rect(scr, 160 + static_cast<int32_t>(i) * 27, 108, 25, 28, KEY, 5);
    g_dots[i] = rect(cell, 7, 9, 11, 11, WHITE, LV_RADIUS_CIRCLE);
  }

  const int32_t padW = 3 * KEY_W + 2 * KEY_GAP + 8, padX = (SCREEN_W - padW) / 2;
  rect(scr, padX, 144, padW, 4 * KEY_H + 3 * KEY_GAP + 8, CARD, 12);
  for (uintptr_t k = 0; k < 12; ++k) {
    const int32_t col = k % 3, row = k / 3;
    lv_obj_t* key = pill(scr, KEYS[k], padX + 4 + col * (KEY_W + KEY_GAP), 148 + row * (KEY_H + KEY_GAP),
                         KEY_W, KEY_H, KEY, k == 11 ? F_BODY : F_KEY, onKey, reinterpret_cast<void*>(k));
    lv_obj_set_style_radius(key, 6, 0);                    // keys are squarer than pills, as in the design
    lv_obj_set_ext_click_area(key, KEY_GAP / 2);           // no dead gaps between keys
  }
  showEntry();
}

void onShow() {
  g_entry.clear();
  showEntry();
}
}  // namespace PinScreen
