#pragma once
// ============================================================================
//  Theme: the AVTF B.E.S.S. look from the Figma file (Design/Design.fig).
//  Colors, fonts and the few widget shapes every screen is built from.
//  Positions in the screens are the Figma coordinates (480x320), except where
//  a control was enlarged to be finger-sized; those spots say so.
// ============================================================================
#include <lvgl.h>
#include <string>
#include <vector>
#include "fonts/Fonts.h"
#include "images/Images.h"

namespace Theme {
// ---- Colors (hex from the Figma file) ------------------------------------------
constexpr uint32_t RED          = 0xFF0A0A;   // borders, main buttons, arrows, image frame
constexpr uint32_t PANEL_RED    = 0xC50909;   // item panel, cart card
constexpr uint32_t DEEP_RED     = 0x870000;   // description box, image well border
constexpr uint32_t TOGGLE_RED   = 0x840202;   // "Check Into Cart" pill
constexpr uint32_t CHECK_RED    = 0x470000;   // its tick box
constexpr uint32_t ROW_RED      = 0xFF5151;   // cart rows
constexpr uint32_t PILL_RED     = 0xFF5555;   // "Items: N" pill
constexpr uint32_t HEADER_RED   = 0x990707;   // session bar, nav background
constexpr uint32_t NAV_DARK     = 0x690000;   // nav pills, Log Out
constexpr uint32_t NAV_ACTIVE   = 0xE61616;   // the current nav pill
constexpr uint32_t BARCODE_PINK = 0xFF9494;
constexpr uint32_t LIST_ARROW   = 0xFF2D2D;
constexpr uint32_t WHITE        = 0xFFFFFF;
constexpr uint32_t BLACK        = 0x000000;
constexpr uint32_t CARD         = 0xD9D9D9;   // settings cards, PIN bar, class chips
constexpr uint32_t BAR          = 0xB1B1B1;   // Sign On bars
constexpr uint32_t SCAN_BAR     = 0xB8B8B8;
constexpr uint32_t DARK_PILL    = 0x434343;   // option buttons, Back, Forget
constexpr uint32_t DROPDOWN     = 0x4C4C4C;
constexpr uint32_t KEY          = 0x696969;   // PIN keys and cells
constexpr uint32_t LIST_DARK    = 0x1C1C1C;   // Bluetooth / Wi-Fi rows
constexpr uint32_t SUBTEXT      = 0x434343;
constexpr uint32_t MUTED        = 0x9E9E9E;
constexpr uint32_t TAGLINE      = 0xC7B8B8;   // the design's 24%-opacity gradient, flattened

inline lv_color_t color(uint32_t hex) { return lv_color_hex(hex); }

// ---- Fonts: Lexend, as in the design. Nothing below 11 px (the design's 5-8 px
// status and list text is enlarged so it can be read on the 4" panel). --------
constexpr const lv_font_t* F_SMALL   = &lexend_light_11;   // barcode, descriptions, sublines
constexpr const lv_font_t* F_BODY    = &lexend_light_13;   // pills, nav, list rows (LV_FONT_DEFAULT)
constexpr const lv_font_t* F_HEADER  = &lexend_light_14;   // session bar, "Items:"
constexpr const lv_font_t* F_ITEM    = &lexend_light_16;   // item name, dropdowns, keyboard
constexpr const lv_font_t* F_BIG     = &lexend_light_20;   // big buttons, field labels
constexpr const lv_font_t* F_TITLE   = &lexend_light_24;
constexpr const lv_font_t* F_LABEL   = &lexend_medium_11;  // status icons, telemetry
constexpr const lv_font_t* F_STRONG  = &lexend_medium_13;  // settings row titles
constexpr const lv_font_t* F_KEY     = &lexend_bold_18;    // PIN keypad
constexpr const lv_font_t* F_HEADING = &lexend_bold_32;    // "Enter PIN..."

// ---- Screen geometry shared by several screens ---------------------------------
constexpr int32_t SCREEN_W = 480;
constexpr int32_t SCREEN_H = 320;
constexpr int32_t BIG_BUTTON_Y = 226;   // Check In / Check Out(s) / Log In row (design: 230, 28 tall)
constexpr int32_t BIG_BUTTON_H = 36;    // enlarged from 28

void init();   // LVGL theme + shared styles. Call once before building screens.

// ---- Building blocks ------------------------------------------------------------
// A bare object: no style from the LVGL theme, not scrollable, not clickable.
lv_obj_t* plain(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h);
lv_obj_t* rect(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t bg, int32_t radius);
lv_obj_t* text(lv_obj_t* parent, const char* s, const lv_font_t* font, uint32_t color, int32_t x, int32_t y);
lv_obj_t* textBox(lv_obj_t* parent, const char* s, const lv_font_t* font, uint32_t color,
                  int32_t x, int32_t y, int32_t w, lv_label_long_mode_t mode);
lv_obj_t* image(lv_obj_t* parent, const void* src, int32_t x, int32_t y);

// Rounded button with a centered label. Darkens while pressed, fades when disabled.
lv_obj_t* pill(lv_obj_t* parent, const char* label, int32_t x, int32_t y, int32_t w, int32_t h,
               uint32_t bg, const lv_font_t* font, lv_event_cb_t onClick, void* user = nullptr);
// The design's big buttons: flat against the left screen edge, rounded on the right.
lv_obj_t* edgeButton(lv_obj_t* parent, const char* label, int32_t y, int32_t w, int32_t h,
                     uint32_t bg, const lv_font_t* font, lv_event_cb_t onClick);
lv_obj_t* labelOf(lv_obj_t* button);                     // a pill's/edgeButton's text
void      setButtonText(lv_obj_t* button, const char* s);

// "> Title" at the top left, as on every screen.
void title(lv_obj_t* parent, const char* text);
// "> Settings > [icon] Page". Tapping "Settings" calls onBack.
void breadcrumb(lv_obj_t* parent, const lv_image_dsc_t* icon, const char* page, lv_event_cb_t onBack);

// Red framed image well (design: 141x141 at 318,83). Shows /items/<barcode>.jpg.
struct ImageFrame {
  lv_obj_t* img         = nullptr;
  lv_obj_t* placeholder = nullptr;
};
ImageFrame imageFrame(lv_obj_t* parent);
void       showItemImage(const ImageFrame& f, const std::string& barcode, bool known);
void       clearItemImage(const ImageFrame& f);

// Dark rounded dropdown with the design's chevron. Option 0 is the placeholder.
lv_obj_t*   dropdown(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h);
std::string optionsText(const char* placeholder, const std::vector<std::string>& opts);
std::string selectedText(lv_obj_t* dd);
void        setOptionsKeepPick(lv_obj_t* dd, const char* placeholder, const std::vector<std::string>& opts);

// Small helpers
void setEnabled(lv_obj_t* obj, bool on);
void setVisible(lv_obj_t* obj, bool on);
void setTextIfChanged(lv_obj_t* label, const char* text);   // no redraw when nothing changed
bool fingerDown();                                          // lists wait for this before rebuilding
std::string itemCount(size_t n);                            // "1 item", "3 items"
std::string formatDuration(uint32_t sec);                   // "45s", "3m 10s", "2h 13m"
}  // namespace Theme
