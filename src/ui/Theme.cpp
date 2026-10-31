#include "Theme.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <string.h>

namespace Theme {
namespace {
lv_style_t            g_pressed;    // pills and buttons while a finger is on them
lv_style_t            g_disabled;   // faded, like the design's pale "Log In" before the form is filled
lv_color_filter_dsc_t g_darken;

lv_color_t darken(const lv_color_filter_dsc_t*, lv_color_t c, lv_opa_t opa) { return lv_color_darken(c, opa); }

void onCrumbClicked(lv_event_t* e) {
  auto cb = reinterpret_cast<lv_event_cb_t>(lv_event_get_user_data(e));
  if (cb) cb(e);
}
}  // namespace

void init() {
  lv_display_t* disp = lv_display_get_default();
  lv_display_set_theme(disp, lv_theme_default_init(disp, color(RED), color(DARK_PILL), false, F_BODY));
  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_white(), 0);

  lv_color_filter_dsc_init(&g_darken, darken);
  lv_style_init(&g_pressed);
  lv_style_set_color_filter_dsc(&g_pressed, &g_darken);
  lv_style_set_color_filter_opa(&g_pressed, LV_OPA_30);

  lv_style_init(&g_disabled);
  lv_style_set_bg_opa(&g_disabled, LV_OPA_40);
  lv_style_set_text_opa(&g_disabled, LV_OPA_80);
}

// ---- Building blocks ------------------------------------------------------------
lv_obj_t* plain(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  return o;
}

lv_obj_t* rect(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t bg, int32_t radius) {
  lv_obj_t* o = plain(parent, x, y, w, h);
  lv_obj_set_style_bg_color(o, color(bg), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(o, radius, 0);
  return o;
}

lv_obj_t* text(lv_obj_t* parent, const char* s, const lv_font_t* font, uint32_t c, int32_t x, int32_t y) {
  lv_obj_t* l = lv_label_create(parent);
  lv_label_set_text(l, s);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color(c), 0);
  lv_obj_set_pos(l, x, y);
  return l;
}

lv_obj_t* textBox(lv_obj_t* parent, const char* s, const lv_font_t* font, uint32_t c,
                  int32_t x, int32_t y, int32_t w, lv_label_long_mode_t mode) {
  lv_obj_t* l = text(parent, s, font, c, x, y);
  lv_obj_set_width(l, w);
  lv_label_set_long_mode(l, mode);
  return l;
}

lv_obj_t* image(lv_obj_t* parent, const void* src, int32_t x, int32_t y) {
  lv_obj_t* img = lv_image_create(parent);
  lv_image_set_src(img, src);
  lv_obj_set_pos(img, x, y);
  return img;
}

lv_obj_t* pill(lv_obj_t* parent, const char* label, int32_t x, int32_t y, int32_t w, int32_t h,
               uint32_t bg, const lv_font_t* font, lv_event_cb_t onClick, void* user) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_radius(b, h / 2, 0);
  lv_obj_set_style_bg_color(b, color(bg), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(b, lv_color_white(), 0);
  lv_obj_set_style_text_font(b, font, 0);
  lv_obj_add_style(b, &g_pressed, LV_STATE_PRESSED);
  lv_obj_add_style(b, &g_disabled, LV_STATE_DISABLED);
  lv_obj_set_ext_click_area(b, 4);
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, label);
  lv_obj_center(l);
  if (onClick) lv_obj_add_event_cb(b, onClick, LV_EVENT_CLICKED, user);
  return b;
}

lv_obj_t* edgeButton(lv_obj_t* parent, const char* label, int32_t y, int32_t w, int32_t h,
                     uint32_t bg, const lv_font_t* font, lv_event_cb_t onClick) {
  // Starts off-screen by one radius, so only the right end shows rounded.
  const int32_t r = h / 2;
  lv_obj_t* b = pill(parent, label, -r, y, w + r, h, bg, font, onClick);
  lv_obj_align(labelOf(b), LV_ALIGN_CENTER, r / 2, 0);
  return b;
}

lv_obj_t* labelOf(lv_obj_t* button) { return lv_obj_get_child(button, 0); }

void setButtonText(lv_obj_t* button, const char* s) { setTextIfChanged(labelOf(button), s); }

void title(lv_obj_t* parent, const char* t) {
  image(parent, &img_arrow_title, 14, 56);
  text(parent, t, F_TITLE, BLACK, 36, 56);
}

void breadcrumb(lv_obj_t* parent, const lv_image_dsc_t* icon, const char* page, lv_event_cb_t onBack) {
  image(parent, &img_arrow_title, 14, 56);
  lv_obj_t* back = text(parent, "Settings", F_TITLE, BLACK, 36, 56);
  lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);                  // the crumb goes back, like Back
  lv_obj_set_ext_click_area(back, 8);
  lv_obj_add_event_cb(back, onCrumbClicked, LV_EVENT_CLICKED, reinterpret_cast<void*>(onBack));
  image(parent, &img_arrow_title, 140, 56);
  image(parent, icon, 155 + (29 - icon->header.w) / 2, 57 + (29 - icon->header.h) / 2);
  text(parent, page, F_TITLE, BLACK, 187, 56);
}

// ---- Item image well ------------------------------------------------------------
ImageFrame imageFrame(lv_obj_t* parent) {
  ImageFrame f;
  rect(parent, 318, 83, 141, 141, RED, 15);
  lv_obj_t* well = rect(parent, 329, 94, 120, 120, WHITE, 15);
  lv_obj_set_style_clip_corner(well, true, 0);                   // photos get the rounded corners too
  f.img = lv_image_create(well);
  lv_obj_center(f.img);
  f.placeholder = lv_label_create(well);
  lv_obj_set_style_text_font(f.placeholder, F_SMALL, 0);
  lv_obj_set_style_text_color(f.placeholder, color(MUTED), 0);
  lv_obj_center(f.placeholder);
  lv_obj_t* border = plain(parent, 329, 94, 120, 120);          // drawn over the photo
  lv_obj_set_style_border_width(border, 4, 0);
  lv_obj_set_style_border_color(border, color(DEEP_RED), 0);
  lv_obj_set_style_radius(border, 15, 0);
  clearItemImage(f);
  return f;
}

void showItemImage(ImageFrame& f, const std::string& barcode, bool known) {
  const std::string file = known ? "/items/" + barcode + ".jpg" : "/items/unknown_item.jpg";
  if (file != f.shown) {                                         // same path: nothing on disk or in LVGL changed
    f.shown    = file;
    f.hasPhoto = LittleFS.exists(file.c_str());
    if (f.hasPhoto) lv_image_set_src(f.img, ("L:" + file).c_str());
    else lv_label_set_text(f.placeholder, known ? "No photo" : "Unknown item");
  }
  setVisible(f.img, f.hasPhoto);
  setVisible(f.placeholder, !f.hasPhoto);
}

void clearItemImage(const ImageFrame& f) {
  setVisible(f.img, false);
  setVisible(f.placeholder, false);
}

// ---- Dropdowns ------------------------------------------------------------------
lv_obj_t* dropdown(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h) {
  lv_obj_t* dd = lv_dropdown_create(parent);
  lv_obj_set_pos(dd, x, y);
  lv_obj_set_size(dd, w, h);
  lv_obj_set_style_bg_color(dd, color(DROPDOWN), 0);
  lv_obj_set_style_border_width(dd, 0, 0);
  lv_obj_set_style_radius(dd, h / 2, 0);
  lv_obj_set_style_text_color(dd, lv_color_white(), 0);
  lv_obj_set_style_text_font(dd, F_ITEM, 0);
  lv_obj_set_style_pad_hor(dd, 14, 0);
  lv_obj_set_style_pad_ver(dd, (h - lv_font_get_line_height(F_ITEM)) / 2, 0);
  lv_obj_set_ext_click_area(dd, 4);
  lv_dropdown_set_symbol(dd, &img_chevron);

  lv_obj_t* list = lv_dropdown_get_list(dd);                    // the part that opens
  lv_obj_set_style_bg_color(list, color(DROPDOWN), 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_radius(list, 12, 0);
  lv_obj_set_style_text_color(list, lv_color_white(), 0);
  lv_obj_set_style_text_font(list, F_ITEM, 0);
  lv_obj_set_style_text_line_space(list, 20, 0);                // ~40 px per option: finger-sized
  lv_obj_set_style_max_height(list, 196, 0);
  lv_obj_set_style_bg_color(list, color(RED), LV_PART_SELECTED | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(list, color(PANEL_RED), LV_PART_SELECTED | LV_STATE_PRESSED);
  return dd;
}

std::string optionsText(const char* placeholder, const std::vector<std::string>& opts) {
  std::string s = placeholder;                                   // option 0 means "nothing picked yet"
  for (const std::string& o : opts) {
    s += '\n';
    s += o;
  }
  return s;
}

std::string selectedText(lv_obj_t* dd) {
  char buf[96];
  lv_dropdown_get_selected_str(dd, buf, sizeof(buf));
  return buf;
}

void setOptionsKeepPick(lv_obj_t* dd, const char* placeholder, const std::vector<std::string>& opts) {
  const std::string prev = selectedText(dd);
  lv_dropdown_set_options(dd, optionsText(placeholder, opts).c_str());
  const int32_t idx = lv_dropdown_get_option_index(dd, prev.c_str());
  lv_dropdown_set_selected(dd, idx > 0 ? static_cast<uint32_t>(idx) : 0);
}

// ---- Small helpers --------------------------------------------------------------
void setEnabled(lv_obj_t* obj, bool on) {
  if (on) lv_obj_remove_state(obj, LV_STATE_DISABLED);
  else    lv_obj_add_state(obj, LV_STATE_DISABLED);
}

void setVisible(lv_obj_t* obj, bool on) {
  if (on) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
  else    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

void setTextIfChanged(lv_obj_t* label, const char* t) {
  if (strcmp(lv_label_get_text(label), t) != 0) lv_label_set_text(label, t);
}

bool fingerDown() {
  for (lv_indev_t* in = lv_indev_get_next(nullptr); in; in = lv_indev_get_next(in))
    if (lv_indev_get_state(in) == LV_INDEV_STATE_PRESSED) return true;
  return false;
}

std::string itemCount(size_t n) { return n == 1 ? "1 item" : std::to_string(n) + " items"; }

std::string formatDuration(uint32_t sec) {
  char buf[24];
  if (sec < 60)        snprintf(buf, sizeof(buf), "%lus", (unsigned long)sec);
  else if (sec < 3600) snprintf(buf, sizeof(buf), "%lum %lus", (unsigned long)(sec / 60), (unsigned long)(sec % 60));
  else                 snprintf(buf, sizeof(buf), "%luh %lum", (unsigned long)(sec / 3600), (unsigned long)((sec % 3600) / 60));
  return buf;
}
}  // namespace Theme
