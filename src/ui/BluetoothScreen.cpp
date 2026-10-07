// Bluetooth Options: find the barcode scanner and connect to it.
// Figma frame "Settings Screen - Bluetooth Options" (1002:488). Rows 20 -> 28 px,
// Scan 21 -> 32 px, Back 21 -> 34 px, text 8 px -> 11-13 px.
#include "Chrome.h"
#include "Scanner.h"
#include "Screens.h"
#include "Theme.h"

using namespace Theme;

namespace BluetoothScreen {
namespace {
constexpr int32_t CARD_W = 468, LIST_Y = 44, ROW_H = 28;

lv_obj_t*                g_list = nullptr;
lv_obj_t*                g_scan = nullptr;
lv_obj_t*                g_showAllTick = nullptr;
bool                     g_showAll = false;
bool                     g_searched = false;     // a search has run since the screen opened
std::vector<ScannerInfo> g_found;
std::string              g_shown;                // what the list was built from; rebuild when it changes

void rebuild();
void rebuildLater(void*) { rebuild(); }

void onBack(lv_event_t*) { Nav::go(ScreenId::Settings); }

void onScan(lv_event_t*) {
  g_searched = true;
  Scanner::discover(!g_showAll);
  tick();
}

void onShowAll(lv_event_t*) {
  g_showAll = !g_showAll;
  setVisible(g_showAllTick, g_showAll);
}

void onFound(lv_event_t* e) {
  const size_t i = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
  if (i >= g_found.size()) return;
  Scanner::connect(g_found[i]);
  Chrome::toast("Connecting to " + g_found[i].name);
}

void onForget(lv_event_t*) {
  Scanner::forget();
  lv_async_call(rebuildLater, nullptr);                  // this pill's row is about to be rebuilt
}

lv_obj_t* row(const std::string& name, bool connected, const char* right, bool forget) {
  lv_obj_t* r = lv_button_create(g_list);
  lv_obj_remove_style_all(r);
  lv_obj_set_size(r, CARD_W, ROW_H);
  lv_obj_set_style_bg_color(r, color(LIST_DARK), 0);
  lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(r, color(DARK_PILL), LV_STATE_PRESSED);
  lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(r, 1, 0);
  lv_obj_set_style_border_color(r, color(DARK_PILL), 0);
  const std::string label = connected ? name + "  " LV_SYMBOL_OK : name;
  textBox(r, label.c_str(), F_BODY, WHITE, 13, 6, 240, LV_LABEL_LONG_DOT);
  textBox(r, right, F_SMALL, MUTED, 260, 8, 120, LV_LABEL_LONG_DOT);
  if (forget) pill(r, "Forget", 390, 3, 64, 22, DARK_PILL, F_SMALL, onForget);
  return r;
}

std::string snapshotText() {             // everything the list shows, to tell when it changed
  const ScannerStatus s = Scanner::status();
  std::string t = std::to_string(static_cast<int>(s.state)) + s.name + s.address + (s.hasSavedDevice ? "1" : "0");
  for (const ScannerInfo& f : g_found) t += "|" + f.address;
  return t + (g_searched ? "s" : "");
}

void rebuild() {
  g_shown = snapshotText();
  lv_obj_clean(g_list);
  const ScannerStatus s = Scanner::status();
  if (s.hasSavedDevice) {                  // the station's scanner first, found or not
    const char* state = s.state == ScannerState::Connected    ? "Connected"
                      : s.state == ScannerState::Connecting   ? "Connecting..."
                                                              : "Waiting for it to wake up";
    row(s.name, s.state == ScannerState::Connected, state, true);
  }
  for (size_t i = 0; i < g_found.size(); ++i) {
    if (s.hasSavedDevice && g_found[i].address == s.address) continue;
    char dbm[16];
    snprintf(dbm, sizeof(dbm), "%d dBm", g_found[i].rssi);
    lv_obj_t* r = row(g_found[i].name, false, dbm, false);
    lv_obj_add_event_cb(r, onFound, LV_EVENT_CLICKED, reinterpret_cast<void*>(i));
  }
  if (lv_obj_get_child_count(g_list) == 0)
    text(g_list, g_searched ? "Nothing found. Is the scanner in BLE mode?" : "Tap Scan to look for scanners",
         F_BODY, WHITE, 13, 8);
}
}  // namespace

void build(lv_obj_t* scr) {
  breadcrumb(scr, &img_ic_bt, "Bluetooth Options", onBack);

  lv_obj_t* card = rect(scr, 6, 96, CARD_W, 128, CARD, 15);
  lv_obj_set_style_clip_corner(card, true, 0);
  rect(card, 13, 6, 262, 32, SCAN_BAR, 16);
  g_scan = pill(card, "Scan", 13, 6, 80, 32, DARK_PILL, F_BODY, onScan);

  lv_obj_t* showAll = plain(card, 96, 6, 179, 32);          // tick box + its words, one tap target
  lv_obj_add_flag(showAll, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(showAll, onShowAll, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* box = rect(showAll, 4, 9, 14, 14, WHITE, 3);
  g_showAllTick = text(box, LV_SYMBOL_OK, F_SMALL, DARK_PILL, 0, 0);
  lv_obj_center(g_showAllTick);
  text(showAll, "Show all Bluetooth Devices", F_SMALL, WHITE, 23, 8);
  setVisible(g_showAllTick, false);

  g_list = plain(card, 0, LIST_Y, CARD_W, 128 - LIST_Y);
  lv_obj_set_style_bg_color(g_list, color(LIST_DARK), 0);
  lv_obj_set_style_bg_opa(g_list, LV_OPA_COVER, 0);
  lv_obj_add_flag(g_list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(g_list, LV_DIR_VER);
  lv_obj_set_flex_flow(g_list, LV_FLEX_FLOW_COLUMN);

  pill(scr, "Back", 6, 232, 80, 34, DARK_PILL, F_BODY, onBack);
  rebuild();
}

void onShow() {
  g_found = Scanner::found();
  rebuild();
}

void tick() {
  const bool searching = Scanner::status().state == ScannerState::Discovering;
  setButtonText(g_scan, searching ? "Scanning" : "Scan");
  if (snapshotText() != g_shown && !fingerDown()) rebuild();
}

void refreshList() {
  g_found = Scanner::found();
  if (!fingerDown()) rebuild();                          // otherwise tick() picks it up
}
}  // namespace BluetoothScreen
