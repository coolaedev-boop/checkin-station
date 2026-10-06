#include "Ui.h"
#include "Cloud.h"
#include "Inventory.h"
#include "Network.h"
#include "Scanner.h"
#include "Telemetry.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <lvgl.h>
#include <string>
#include <vector>

namespace {
struct DropdownPair {
  lv_obj_t* cls    = nullptr;
  lv_obj_t* person = nullptr;
};

// ---- Screen state -------------------------------------------------------------
lv_obj_t*    g_tabview = nullptr;
DropdownPair g_inDd, g_outDd;

// Check in
lv_obj_t*   g_img = nullptr;
lv_obj_t*   g_lblItem = nullptr;
lv_obj_t*   g_lblCode = nullptr;
lv_obj_t*   g_lblInMsg = nullptr;
lv_obj_t*   g_btnIn = nullptr;
std::string g_scanned;                       // barcode waiting to be checked in

// Check out
lv_obj_t*         g_listOut = nullptr;
lv_obj_t*         g_lblOutMsg = nullptr;
lv_obj_t*         g_btnOut = nullptr;
std::vector<Item> g_listItems;               // what the list is currently showing
std::string       g_selectedBarcode;

// System
lv_obj_t*                g_lblBle = nullptr;
lv_obj_t*                g_lblCloud = nullptr;
lv_obj_t*                g_lblChip = nullptr;
lv_obj_t*                g_cbShowAll = nullptr;
lv_obj_t*                g_listScanners = nullptr;
std::vector<ScannerInfo> g_scannerItems;

// Wi-Fi: two full-screen panels opened from the System tab
lv_obj_t*                g_wifiPanel = nullptr;      // network list
lv_obj_t*                g_lblWifi = nullptr;
lv_obj_t*                g_btnWifiForget = nullptr;
lv_obj_t*                g_listWifi = nullptr;
std::vector<WifiNetwork> g_wifiItems;
std::string              g_wifiListedOn;             // network shown with a check mark
std::string              g_wifiNote;                 // one-off message, e.g. "needs a username"
lv_obj_t*                g_pwPanel = nullptr;        // password entry
lv_obj_t*                g_lblPwTitle = nullptr;
lv_obj_t*                g_lblPwHint = nullptr;
lv_obj_t*                g_lblPwToggle = nullptr;
lv_obj_t*                g_taPassword = nullptr;
std::string              g_pwSsid;
std::string              g_confirmOpenSsid;          // open network tapped once; a second tap joins
uint32_t                 g_confirmOpenMs = 0;

// LVGL's stock "1#" keyboard page has no ~ ^ | or ` keys, and Wi-Fi passwords can
// contain them. Same page with ~ ^ added to row 2 and | ` to row 3. LVGL keeps these
// pointers, so they must live forever (file scope, not locals).
constexpr lv_buttonmatrix_ctrl_t kbKey(uint32_t bits) { return static_cast<lv_buttonmatrix_ctrl_t>(bits); }
const char* const KB_MAP_SPECIAL[] = {
  "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",                     // 11 keys
  "abc", "+", "&", "/", "*", "=", "%", "!", "?", "#", "<", ">", "~", "^", "\n",                  // 14 keys
  "\\", "@", "$", "(", ")", "{", "}", "[", "]", ";", "\"", "'", "|", "`", "\n",                  // 14 keys
  LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""};                    //  5 keys
const lv_buttonmatrix_ctrl_t KB_CTRL_SPECIAL[] = {                                                // one per key
  kbKey(1), kbKey(1), kbKey(1), kbKey(1), kbKey(1), kbKey(1), kbKey(1), kbKey(1), kbKey(1), kbKey(1),
  kbKey(LV_BUTTONMATRIX_CTRL_CHECKED | 2),
  kbKey(LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2), kbKey(1), kbKey(1), kbKey(1), kbKey(1), kbKey(1), kbKey(1),
  kbKey(1), kbKey(1), kbKey(1), kbKey(1), kbKey(1), kbKey(1), kbKey(1),
  kbKey(1), kbKey(1), kbKey(1), kbKey(1), kbKey(1), kbKey(1), kbKey(1),
  kbKey(1), kbKey(1), kbKey(1), kbKey(1), kbKey(1), kbKey(1), kbKey(1),
  kbKey(LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2), kbKey(LV_BUTTONMATRIX_CTRL_CHECKED | 2), kbKey(6),
  kbKey(LV_BUTTONMATRIX_CTRL_CHECKED | 2), kbKey(LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2)};
// Map = keys + 3 row breaks + the "" terminator. LVGL reads past the end if these disagree.
static_assert(sizeof(KB_CTRL_SPECIAL) / sizeof(KB_CTRL_SPECIAL[0]) == 44, "44 keys");
static_assert(sizeof(KB_MAP_SPECIAL) / sizeof(KB_MAP_SPECIAL[0]) == 44 + 3 + 1, "one ctrl entry per key");

uint32_t g_seenInventoryRev = UINT32_MAX;
uint32_t g_seenFoundRev     = UINT32_MAX;
uint32_t g_seenNetworksRev  = UINT32_MAX;

// ---- Small helpers ------------------------------------------------------------
std::string buildOptions(const char* placeholder, const std::vector<std::string>& opts) {
  std::string s = placeholder;               // option 0 means "nothing picked yet"
  for (const std::string& o : opts) {
    s += '\n';
    s += o;
  }
  return s;
}

std::string selectedText(lv_obj_t* dd) {
  char buf[64];
  lv_dropdown_get_selected_str(dd, buf, sizeof(buf));
  return buf;
}

// Replace a dropdown's options but keep the current pick if it still exists.
void setOptionsKeepPick(lv_obj_t* dd, const char* placeholder, const std::vector<std::string>& opts) {
  const std::string prev = selectedText(dd);
  lv_dropdown_set_options(dd, buildOptions(placeholder, opts).c_str());
  const int32_t idx = lv_dropdown_get_option_index(dd, prev.c_str());
  lv_dropdown_set_selected(dd, idx > 0 ? static_cast<uint32_t>(idx) : 0);
}

bool pairComplete(const DropdownPair& p) {
  return lv_dropdown_get_selected(p.cls) > 0 && lv_dropdown_get_selected(p.person) > 0;
}

void setEnabled(lv_obj_t* obj, bool on) {
  if (on) lv_obj_remove_state(obj, LV_STATE_DISABLED);
  else    lv_obj_add_state(obj, LV_STATE_DISABLED);
}

void updateButtons() {
  setEnabled(g_btnIn, !g_scanned.empty() && pairComplete(g_inDd));
  setEnabled(g_btnOut, !g_selectedBarcode.empty() && pairComplete(g_outDd));
}

std::string formatDuration(uint32_t sec) {
  char buf[24];
  if (sec < 60)        snprintf(buf, sizeof(buf), "%lus", (unsigned long)sec);
  else if (sec < 3600) snprintf(buf, sizeof(buf), "%lum %lus", (unsigned long)(sec / 60), (unsigned long)(sec % 60));
  else                 snprintf(buf, sizeof(buf), "%luh %lum", (unsigned long)(sec / 3600), (unsigned long)((sec % 3600) / 60));
  return buf;
}

// ---- Layout builders ----------------------------------------------------------
void setupTab(lv_obj_t* tab, lv_flex_flow_t flow) {
  lv_obj_set_flex_flow(tab, flow);
  lv_obj_set_style_pad_all(tab, 8, 0);
  lv_obj_set_style_pad_gap(tab, 8, 0);
  lv_obj_remove_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* makeContainer(lv_obj_t* parent, lv_flex_flow_t flow) {
  lv_obj_t* box = lv_obj_create(parent);
  lv_obj_remove_style_all(box);
  lv_obj_set_flex_flow(box, flow);
  lv_obj_set_style_pad_gap(box, 8, 0);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  return box;
}

lv_obj_t* makeColumn(lv_obj_t* parent, int32_t width) {   // width <= 0 means "take the rest"
  lv_obj_t* col = makeContainer(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_height(col, lv_pct(100));
  if (width > 0) lv_obj_set_width(col, width);
  else           lv_obj_set_flex_grow(col, 1);
  return col;
}

lv_obj_t* makeButton(lv_obj_t* parent, const char* text, lv_event_cb_t onClick) {
  lv_obj_t* btn = lv_button_create(parent);
  lv_obj_set_size(btn, lv_pct(100), 48);
  lv_obj_t* lbl = lv_label_create(btn);
  lv_label_set_text(lbl, text);
  lv_obj_center(lbl);
  lv_obj_add_event_cb(btn, onClick, LV_EVENT_CLICKED, nullptr);
  return btn;
}

lv_obj_t* makeLabel(lv_obj_t* parent, const char* text) {
  lv_obj_t* lbl = lv_label_create(parent);
  lv_obj_set_width(lbl, lv_pct(100));
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
  lv_label_set_text(lbl, text);
  return lbl;
}

void styleSelectable(lv_obj_t* listButton) {
  lv_obj_set_style_bg_color(listButton, lv_palette_main(LV_PALETTE_BLUE), LV_STATE_CHECKED);
  lv_obj_set_style_text_color(listButton, lv_color_white(), LV_STATE_CHECKED);
}

lv_obj_t* makeRow(lv_obj_t* parent, int32_t height) {    // full-width row, children centered vertically
  lv_obj_t* row = makeContainer(parent, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(row, lv_pct(100), height);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  return row;
}

lv_obj_t* makeFixedButton(lv_obj_t* parent, const char* text, int32_t width, lv_event_cb_t onClick) {
  lv_obj_t* btn = makeButton(parent, text, onClick);
  lv_obj_set_width(btn, width);
  return btn;
}

void setTextIfChanged(lv_obj_t* label, const char* text) {   // no redraw when nothing changed
  if (strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

// ---- Wi-Fi panels ----------------------------------------------------------------
// Full-screen layers drawn over the tabs: the network list, and a password screen
// with an on-screen keyboard. showOnly() picks which one is visible.
lv_obj_t* makePanel() {
  lv_obj_t* panel = lv_obj_create(lv_screen_active());
  lv_obj_set_size(panel, lv_pct(100), lv_pct(100));
  lv_obj_set_style_radius(panel, 0, 0);
  lv_obj_set_style_border_width(panel, 0, 0);
  lv_obj_set_style_pad_all(panel, 8, 0);
  lv_obj_set_style_pad_gap(panel, 6, 0);
  lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
  return panel;
}

void showOnly(lv_obj_t* panel) {                           // nullptr = back to the tabs
  if (!g_wifiPanel) return;
  if (panel != g_pwPanel) lv_textarea_set_text(g_taPassword, "");   // don't leave a password lying around
  for (lv_obj_t* p : {g_wifiPanel, g_pwPanel}) {
    if (p == panel) lv_obj_remove_flag(p, LV_OBJ_FLAG_HIDDEN);
    else            lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
  }
}

bool wifiPanelOpen() { return g_wifiPanel && !lv_obj_has_flag(g_wifiPanel, LV_OBJ_FLAG_HIDDEN); }

NetworkStatus refreshWifiStatus() {
  const NetworkStatus s = Network::status();
  char line1[96] = "";
  switch (s.state) {
    case NetworkState::Connected:
      snprintf(line1, sizeof(line1), LV_SYMBOL_WIFI " Connected to %s (%d dBm)", s.ssid.c_str(), s.rssi);
      break;
    case NetworkState::Connecting:
      snprintf(line1, sizeof(line1), LV_SYMBOL_WIFI " Connecting to %s...", s.ssid.c_str());
      break;
    case NetworkState::Offline:
      if (s.ssid.empty()) snprintf(line1, sizeof(line1), LV_SYMBOL_WIFI " Offline, no network set");
      else                snprintf(line1, sizeof(line1), LV_SYMBOL_WIFI " Offline, retrying %s soon", s.ssid.c_str());
      break;
  }

  char line2[128] = "";
  const char* js = s.joinSsid.c_str();
  if (!g_wifiNote.empty()) snprintf(line2, sizeof(line2), "%s", g_wifiNote.c_str());
  else switch (s.join) {
    case JoinResult::Joining:       snprintf(line2, sizeof(line2), "Joining %s...", js); break;
    case JoinResult::Joined:        snprintf(line2, sizeof(line2), LV_SYMBOL_OK " Joined %s. Saved for next time.", js); break;
    case JoinResult::WrongPassword: snprintf(line2, sizeof(line2), LV_SYMBOL_WARNING " Couldn't join %s: wrong password?", js); break;
    case JoinResult::NotFound:      snprintf(line2, sizeof(line2), LV_SYMBOL_WARNING " Couldn't find %s", js); break;
    case JoinResult::Failed:        snprintf(line2, sizeof(line2), LV_SYMBOL_WARNING " Couldn't join %s", js); break;
    case JoinResult::None:          snprintf(line2, sizeof(line2), "Tap a network to join it"); break;
  }
  std::string text = std::string(line1) + "\n" + line2;
  if (s.scanning) text += "\nSearching for networks...";   // own line, so it never hides a join result
  setTextIfChanged(g_lblWifi, text.c_str());

  if (s.hasSaved) lv_obj_remove_flag(g_btnWifiForget, LV_OBJ_FLAG_HIDDEN);   // nothing to forget otherwise
  else            lv_obj_add_flag(g_btnWifiForget, LV_OBJ_FLAG_HIDDEN);
  return s;
}

void openPasswordPanel(const std::string& ssid) {
  g_pwSsid = ssid;
  const std::string title = "Password for " + ssid;
  lv_label_set_text(g_lblPwTitle, title.c_str());
  lv_label_set_text(g_lblPwHint, "");
  lv_textarea_set_text(g_taPassword, "");
  lv_textarea_set_password_mode(g_taPassword, true);
  lv_label_set_text(g_lblPwToggle, "Show");
  showOnly(g_pwPanel);
}

void onWifiPicked(lv_event_t* e) {
  const size_t idx = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
  if (idx >= g_wifiItems.size()) return;
  const WifiNetwork net = g_wifiItems[idx];
  g_wifiNote.clear();
  switch (net.security) {
    case WifiSecurity::Open:                      // two taps, so a stray tap can't switch networks
      if (g_confirmOpenSsid == net.ssid && millis() - g_confirmOpenMs < 5000) {
        g_confirmOpenSsid.clear();
        Network::join(net.ssid, "");
      } else {
        g_confirmOpenSsid = net.ssid;
        g_confirmOpenMs   = millis();
        g_wifiNote = net.ssid + " has no password. Tap it again to join.";
      }
      break;
    case WifiSecurity::Password:   openPasswordPanel(net.ssid); break;
    case WifiSecurity::Enterprise: g_wifiNote = net.ssid + " needs a username too. Not supported."; break;
    case WifiSecurity::Legacy:     g_wifiNote = net.ssid + " uses old security (WEP/WPA1). Not supported."; break;
  }
  refreshWifiStatus();
}

std::string wifiRowText(const WifiNetwork& net) {
  const char* kind = net.security == WifiSecurity::Open       ? "   open"
                   : net.security == WifiSecurity::Enterprise ? "   needs username"
                   : net.security == WifiSecurity::Legacy     ? "   old security"
                                                              : "";
  char text[96];
  snprintf(text, sizeof(text), "%s   %d dBm%s", net.ssid.c_str(), net.rssi, kind);
  return text;
}

bool sameWifiRows(const std::vector<WifiNetwork>& a, const std::vector<WifiNetwork>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (a[i].ssid != b[i].ssid || a[i].security != b[i].security) return false;
  return true;
}

bool touchHeld() {
  for (lv_indev_t* in = lv_indev_get_next(nullptr); in; in = lv_indev_get_next(in))
    if (lv_indev_get_state(in) == LV_INDEV_STATE_PRESSED) return true;
  return false;
}

// Refresh the network list. Returns false if it had to wait because a finger is on
// the screen (rows never move under a tap); the caller tries again next pass.
bool rebuildWifiList() {
  std::vector<WifiNetwork> fresh = Network::networks();
  const NetworkStatus s  = Network::status();
  const std::string   on = s.state == NetworkState::Connected ? s.ssid : "";

  if (!fresh.empty() && on == g_wifiListedOn && sameWifiRows(fresh, g_wifiItems) &&
      lv_obj_get_child_count(g_listWifi) == fresh.size()) {
    for (size_t i = 0; i < fresh.size(); ++i)     // same rows in the same order: just update the signal numbers
      setTextIfChanged(lv_obj_get_child(lv_obj_get_child(g_listWifi, static_cast<int32_t>(i)), -1),
                       wifiRowText(fresh[i]).c_str());
    g_wifiItems = std::move(fresh);
    return true;
  }
  if (touchHeld()) return false;

  const int32_t scrollY = lv_obj_get_scroll_y(g_listWifi);
  g_wifiItems    = std::move(fresh);
  g_wifiListedOn = on;
  lv_obj_clean(g_listWifi);
  for (size_t i = 0; i < g_wifiItems.size(); ++i) {
    const WifiNetwork& net = g_wifiItems[i];
    lv_obj_t* btn = lv_list_add_button(g_listWifi, net.ssid == g_wifiListedOn ? LV_SYMBOL_OK : LV_SYMBOL_WIFI,
                                       wifiRowText(net).c_str());
    lv_obj_add_event_cb(btn, onWifiPicked, LV_EVENT_CLICKED, reinterpret_cast<void*>(i));
  }
  if (g_wifiItems.empty()) lv_list_add_text(g_listWifi, "No networks listed yet. Tap Scan.");
  lv_obj_scroll_to_y(g_listWifi, scrollY, LV_ANIM_OFF);   // stay where the user was
  return true;
}

void submitPassword() {
  const std::string password = lv_textarea_get_text(g_taPassword);
  if (password.size() < 8) {
    lv_label_set_text(g_lblPwHint, LV_SYMBOL_WARNING " Wi-Fi passwords are at least 8 characters");
    return;
  }
  Network::join(g_pwSsid, password);
  showOnly(g_wifiPanel);                                   // also clears the text box
  refreshWifiStatus();
}

void onConnectPressed(lv_event_t*) { submitPassword(); }
void onPasswordCancel(lv_event_t*) { showOnly(g_wifiPanel); }

void onTogglePassword(lv_event_t*) {
  const bool hidden = lv_textarea_get_password_mode(g_taPassword);
  lv_textarea_set_password_mode(g_taPassword, !hidden);
  lv_label_set_text(g_lblPwToggle, hidden ? "Hide" : "Show");
}

void onOpenWifi(lv_event_t*) {
  g_wifiNote.clear();
  g_confirmOpenSsid.clear();
  showOnly(g_wifiPanel);
  Network::scan();                                         // fresh list every time it opens
  refreshWifiStatus();
}

void onCloseWifi(lv_event_t*) { showOnly(nullptr); }

void onScanWifi(lv_event_t*) {
  g_wifiNote.clear();
  g_confirmOpenSsid.clear();
  Network::scan();
  refreshWifiStatus();
}

void onForgetWifi(lv_event_t*) {
  g_wifiNote.clear();
  Network::forget();
  refreshWifiStatus();
}

void buildWifiPanels() {
  // Network list: [Back] Wi-Fi [Forget] [Scan] / status / networks
  g_wifiPanel = makePanel();
  lv_obj_t* top = makeRow(g_wifiPanel, 48);                 // 48 = button height, so nothing gets clipped
  makeFixedButton(top, LV_SYMBOL_LEFT " Back", 100, onCloseWifi);
  lv_obj_t* title = lv_label_create(top);
  lv_label_set_text(title, "Wi-Fi");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
  lv_obj_set_flex_grow(title, 1);
  g_btnWifiForget = makeFixedButton(top, "Forget", 100, onForgetWifi);
  makeFixedButton(top, LV_SYMBOL_REFRESH " Scan", 100, onScanWifi);

  g_lblWifi  = makeLabel(g_wifiPanel, "");
  g_listWifi = lv_list_create(g_wifiPanel);
  lv_obj_set_width(g_listWifi, lv_pct(100));
  lv_obj_set_flex_grow(g_listWifi, 1);

  // Password: [Cancel] Password for X / [text box] [Show] [Connect] / hint / keyboard
  g_pwPanel = makePanel();
  lv_obj_t* head = makeRow(g_pwPanel, 48);
  makeFixedButton(head, LV_SYMBOL_LEFT " Cancel", 120, onPasswordCancel);
  g_lblPwTitle = lv_label_create(head);
  lv_obj_set_flex_grow(g_lblPwTitle, 1);

  lv_obj_t* entry = makeRow(g_pwPanel, 48);
  g_taPassword = lv_textarea_create(entry);
  lv_textarea_set_one_line(g_taPassword, true);
  lv_textarea_set_password_mode(g_taPassword, true);
  lv_textarea_set_max_length(g_taPassword, 64);
  lv_textarea_set_placeholder_text(g_taPassword, "Password");
  lv_obj_set_flex_grow(g_taPassword, 1);
  g_lblPwToggle = lv_obj_get_child(makeFixedButton(entry, "Show", 80, onTogglePassword), 0);
  makeFixedButton(entry, LV_SYMBOL_OK " Connect", 130, onConnectPressed);

  g_lblPwHint = makeLabel(g_pwPanel, "");
  lv_obj_set_style_text_color(g_lblPwHint, lv_palette_main(LV_PALETTE_ORANGE), 0);

  lv_obj_t* keyboard = lv_keyboard_create(g_pwPanel);
  lv_obj_set_width(keyboard, lv_pct(100));
  lv_obj_set_flex_grow(keyboard, 1);
  lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_SPECIAL, KB_MAP_SPECIAL, KB_CTRL_SPECIAL);
  lv_keyboard_set_textarea(keyboard, g_taPassword);
  // The keyboard forwards its check key and Enter to the text box as READY. Its
  // keyboard/close key (CANCEL) is left unhandled: Cancel in the header leaves.
  lv_obj_add_event_cb(g_taPassword, onConnectPressed, LV_EVENT_READY, nullptr);
}

// ---- Class + name dropdown pair --------------------------------------------------
void onClassChanged(lv_event_t* e) {
  auto* pair = static_cast<DropdownPair*>(lv_event_get_user_data(e));
  const bool picked = lv_dropdown_get_selected(pair->cls) > 0;
  const std::vector<std::string> names = picked ? Inventory::people(selectedText(pair->cls))
                                                : std::vector<std::string>{};
  lv_dropdown_set_options(pair->person, buildOptions("Select name", names).c_str());
  updateButtons();
}

void onPersonChanged(lv_event_t*) { updateButtons(); }

void makeDropdownPair(lv_obj_t* parent, DropdownPair* pair) {
  pair->cls    = lv_dropdown_create(parent);
  pair->person = lv_dropdown_create(parent);
  for (lv_obj_t* dd : {pair->cls, pair->person}) {
    lv_obj_set_width(dd, lv_pct(100));
    lv_obj_set_height(dd, 44);                         // finger-sized
  }
  lv_dropdown_set_options(pair->cls, buildOptions("Select class", Inventory::classes()).c_str());
  lv_dropdown_set_options(pair->person, "Select name");
  lv_obj_add_event_cb(pair->cls, onClassChanged, LV_EVENT_VALUE_CHANGED, pair);
  lv_obj_add_event_cb(pair->person, onPersonChanged, LV_EVENT_VALUE_CHANGED, nullptr);
}

void refreshPair(DropdownPair& pair) {                 // roster changed after a sync
  setOptionsKeepPick(pair.cls, "Select class", Inventory::classes());
  const bool picked = lv_dropdown_get_selected(pair.cls) > 0;
  setOptionsKeepPick(pair.person, "Select name",
                     picked ? Inventory::people(selectedText(pair.cls)) : std::vector<std::string>{});
}

// ---- Check in tab --------------------------------------------------------------
void showScanned(const std::string& barcode) {
  g_scanned = barcode;
  Item item;
  const bool known = Inventory::findItem(barcode, item);
  lv_label_set_text(g_lblItem, known ? item.name.c_str() : "Unknown item");
  lv_label_set_text(g_lblCode, barcode.c_str());
  lv_label_set_text(g_lblInMsg, known && !item.checkedOut ? "Already checked in" : "");

  const std::string file = known ? "/items/" + barcode + ".jpg" : "/items/unknown_item.jpg";
  if (LittleFS.exists(file.c_str())) lv_image_set_src(g_img, ("L:" + file).c_str());
  else                               lv_image_set_src(g_img, LV_SYMBOL_IMAGE);

  showOnly(nullptr);                                   // close the Wi-Fi screens if they're open
  lv_tabview_set_active(g_tabview, 0, LV_ANIM_OFF);    // jump to Check in on every scan
  updateButtons();
}

void onCheckIn(lv_event_t*) {
  Inventory::checkIn(g_scanned, selectedText(g_inDd.cls), selectedText(g_inDd.person));
  const std::string msg = std::string(LV_SYMBOL_OK " Checked in ") + lv_label_get_text(g_lblItem);
  lv_label_set_text(g_lblInMsg, msg.c_str());
  g_scanned.clear();                                   // dropdowns stay picked for the next scan
  updateButtons();
}

void buildCheckInTab(lv_obj_t* tab) {
  setupTab(tab, LV_FLEX_FLOW_ROW);

  lv_obj_t* left = makeColumn(tab, 150);
  g_img = lv_image_create(left);
  lv_obj_set_size(g_img, 120, 120);
  lv_image_set_src(g_img, LV_SYMBOL_IMAGE);
  g_lblItem = makeLabel(left, "Scan an item");
  lv_obj_set_style_text_font(g_lblItem, &lv_font_montserrat_22, 0);
  g_lblCode = makeLabel(left, "");
  lv_obj_set_style_text_color(g_lblCode, lv_palette_main(LV_PALETTE_GREY), 0);

  lv_obj_t* right = makeColumn(tab, 0);
  makeDropdownPair(right, &g_inDd);
  g_btnIn    = makeButton(right, LV_SYMBOL_DOWNLOAD " Check in", onCheckIn);
  g_lblInMsg = makeLabel(right, "");
}

// ---- Check out tab -------------------------------------------------------------
void onItemPicked(lv_event_t* e) {
  auto* picked = static_cast<lv_obj_t*>(lv_event_get_target(e));
  const size_t idx = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
  if (idx >= g_listItems.size()) return;
  g_selectedBarcode = g_listItems[idx].barcode;
  for (uint32_t i = 0; i < lv_obj_get_child_count(g_listOut); ++i) {   // radio-style highlight
    lv_obj_t* child = lv_obj_get_child(g_listOut, i);
    if (child == picked) lv_obj_add_state(child, LV_STATE_CHECKED);
    else                 lv_obj_remove_state(child, LV_STATE_CHECKED);
  }
  lv_label_set_text(g_lblOutMsg, "");
  updateButtons();
}

void rebuildAvailableList() {
  g_listItems = Inventory::availableItems();
  lv_obj_clean(g_listOut);
  bool stillThere = false;
  for (size_t i = 0; i < g_listItems.size(); ++i) {
    lv_obj_t* btn = lv_list_add_button(g_listOut, nullptr, g_listItems[i].name.c_str());
    styleSelectable(btn);
    lv_obj_add_event_cb(btn, onItemPicked, LV_EVENT_CLICKED, reinterpret_cast<void*>(i));
    if (g_listItems[i].barcode == g_selectedBarcode) {
      lv_obj_add_state(btn, LV_STATE_CHECKED);
      stillThere = true;
    }
  }
  if (g_listItems.empty()) lv_list_add_text(g_listOut, "Everything is checked out");
  if (!stillThere) g_selectedBarcode.clear();
  updateButtons();
}

void onCheckOut(lv_event_t*) {
  Item item;
  const bool known = Inventory::findItem(g_selectedBarcode, item);
  const bool ok = Inventory::checkOut(g_selectedBarcode, selectedText(g_outDd.cls), selectedText(g_outDd.person));
  const std::string msg = ok ? std::string(LV_SYMBOL_OK " Checked out ") + (known ? item.name : "")
                             : std::string(LV_SYMBOL_WARNING " Already checked out");
  lv_label_set_text(g_lblOutMsg, msg.c_str());
  g_selectedBarcode.clear();                           // list refreshes on the next update()
  updateButtons();
}

void buildCheckOutTab(lv_obj_t* tab) {
  setupTab(tab, LV_FLEX_FLOW_ROW);

  g_listOut = lv_list_create(tab);
  lv_obj_set_height(g_listOut, lv_pct(100));
  lv_obj_set_flex_grow(g_listOut, 1);

  lv_obj_t* right = makeColumn(tab, 200);
  makeDropdownPair(right, &g_outDd);
  g_btnOut    = makeButton(right, LV_SYMBOL_UPLOAD " Check out", onCheckOut);
  g_lblOutMsg = makeLabel(right, "");
}

// ---- System tab ----------------------------------------------------------------
void onFindScanners(lv_event_t*) {
  Scanner::discover(!lv_obj_has_state(g_cbShowAll, LV_STATE_CHECKED));
}

void onForgetScanner(lv_event_t*) { Scanner::forget(); }

void onSyncNow(lv_event_t*) { Cloud::requestSync(); }

void onScannerPicked(lv_event_t* e) {
  const size_t idx = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
  if (idx < g_scannerItems.size()) Scanner::connect(g_scannerItems[idx]);
}

void rebuildScannerList() {
  g_scannerItems = Scanner::found();
  lv_obj_clean(g_listScanners);
  for (size_t i = 0; i < g_scannerItems.size(); ++i) {
    char text[64];
    snprintf(text, sizeof(text), "%s   %d dBm", g_scannerItems[i].name.c_str(), g_scannerItems[i].rssi);
    lv_obj_t* btn = lv_list_add_button(g_listScanners, LV_SYMBOL_BLUETOOTH, text);
    lv_obj_add_event_cb(btn, onScannerPicked, LV_EVENT_CLICKED, reinterpret_cast<void*>(i));
  }
  if (g_scannerItems.empty()) lv_list_add_text(g_listScanners, "Nothing found. Is the scanner in BLE mode?");
}

void refreshStatus(lv_timer_t*) {
  char buf[200];

  const ScannerStatus s = Scanner::status();
  switch (s.state) {
    case ScannerState::Connected:
      snprintf(buf, sizeof(buf), LV_SYMBOL_BLUETOOTH " %s\nConnected", s.name.c_str()); break;
    case ScannerState::Connecting:
      snprintf(buf, sizeof(buf), LV_SYMBOL_BLUETOOTH " %s\nConnecting...", s.name.c_str()); break;
    case ScannerState::Discovering:
      snprintf(buf, sizeof(buf), LV_SYMBOL_BLUETOOTH " Searching for scanners..."); break;
    case ScannerState::Idle:
      if (s.hasSavedDevice) snprintf(buf, sizeof(buf), LV_SYMBOL_BLUETOOTH " %s\nWaiting for it to wake up", s.name.c_str());
      else                  snprintf(buf, sizeof(buf), LV_SYMBOL_BLUETOOTH " No scanner\nTap Find scanners"); break;
  }
  lv_label_set_text(g_lblBle, buf);

  const CloudStatus c = Cloud::status();
  const std::string synced = c.lastSyncAgoSec == UINT32_MAX ? "never" : formatDuration(c.lastSyncAgoSec) + " ago";
  if (c.wifiConnected) {
    char sheets[64];
    if (c.link == CloudLink::NetError || c.link == CloudLink::ServerError)
      snprintf(sheets, sizeof(sheets), "%s (HTTP %d)", Cloud::linkText(c.link), c.lastHttpCode);
    else
      snprintf(sheets, sizeof(sheets), "%s", Cloud::linkText(c.link));
    snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI " Online (%d dBm)\nSheets: %s\nSynced %s, waiting: %lu",
             c.wifiRssi, sheets, synced.c_str(), (unsigned long)c.pendingEvents);
  } else
    snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI " Offline\nSynced %s\nWaiting to upload: %lu",
             synced.c_str(), (unsigned long)c.pendingEvents);
  lv_label_set_text(g_lblCloud, buf);

  const SystemMetrics m = Telemetry::read();
  snprintf(buf, sizeof(buf), "RAM %lu KB free (low %lu)\nPSRAM %lu KB free\nUp %s, chip %.0f C",
           (unsigned long)(m.heapFree / 1024), (unsigned long)(m.heapMin / 1024),
           (unsigned long)(m.psramFree / 1024), formatDuration(m.uptimeSec).c_str(), m.chipTempC);
  lv_label_set_text(g_lblChip, buf);

  if (wifiPanelOpen()) {
    const NetworkStatus n = refreshWifiStatus();
    const std::string on  = n.state == NetworkState::Connected ? n.ssid : "";
    if (on != g_wifiListedOn) rebuildWifiList();       // move the check mark to the new network
  }
}

void buildSystemTab(lv_obj_t* tab) {
  setupTab(tab, LV_FLEX_FLOW_ROW);

  lv_obj_t* left = makeColumn(tab, 220);
  g_lblBle   = makeLabel(left, "");
  g_lblCloud = makeLabel(left, "");
  g_lblChip  = makeLabel(left, "");
  lv_obj_set_style_text_color(g_lblChip, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_t* actions = makeRow(left, 48);
  lv_obj_set_flex_grow(makeButton(actions, LV_SYMBOL_REFRESH " Sync", onSyncNow), 1);
  lv_obj_set_flex_grow(makeButton(actions, LV_SYMBOL_WIFI " Wi-Fi", onOpenWifi), 1);

  lv_obj_t* right = makeColumn(tab, 0);
  lv_obj_t* row   = makeContainer(right, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(row, lv_pct(100), 48);
  lv_obj_t* find   = makeButton(row, "Find scanners", onFindScanners);
  lv_obj_t* forget = makeButton(row, "Forget", onForgetScanner);
  lv_obj_set_flex_grow(find, 2);
  lv_obj_set_flex_grow(forget, 1);

  g_cbShowAll = lv_checkbox_create(right);
  lv_checkbox_set_text(g_cbShowAll, "Show all Bluetooth devices");

  g_listScanners = lv_list_create(right);
  lv_obj_set_width(g_listScanners, lv_pct(100));
  lv_obj_set_flex_grow(g_listScanners, 1);
  lv_list_add_text(g_listScanners, "Tap Find scanners to search");

  lv_timer_create(refreshStatus, 1000, nullptr);
}
}  // namespace

// ---- Public API --------------------------------------------------------------------
void Ui::begin() {
  lv_display_t* disp = lv_display_get_default();
  lv_display_set_theme(disp, lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE),
                                                   lv_palette_main(LV_PALETTE_TEAL), true, LV_FONT_DEFAULT));

  g_tabview = lv_tabview_create(lv_screen_active());
  lv_tabview_set_tab_bar_position(g_tabview, LV_DIR_BOTTOM);
  lv_tabview_set_tab_bar_size(g_tabview, 44);
  lv_obj_remove_flag(lv_tabview_get_content(g_tabview), LV_OBJ_FLAG_SCROLLABLE);  // no swipe = no full-screen scrolling

  buildCheckInTab(lv_tabview_add_tab(g_tabview, "Check in"));
  buildCheckOutTab(lv_tabview_add_tab(g_tabview, "Check out"));
  buildSystemTab(lv_tabview_add_tab(g_tabview, "System"));
  buildWifiPanels();                                   // created last so they draw over the tabs

  updateButtons();
}

void Ui::update() {
  std::string code;
  while (Scanner::nextBarcode(code)) showScanned(code);

  const uint32_t invRev = Inventory::revision();
  if (invRev != g_seenInventoryRev) {                  // data changed (sync, check-in, check-out)
    g_seenInventoryRev = invRev;
    refreshPair(g_inDd);
    refreshPair(g_outDd);
    rebuildAvailableList();
  }

  const uint32_t foundRev = Scanner::foundRevision();
  if (foundRev != g_seenFoundRev) {                    // a scanner search finished
    g_seenFoundRev = foundRev;
    if (foundRev > 0) rebuildScannerList();
  }

  const uint32_t netRev = Network::networksRevision();
  if (netRev != g_seenNetworksRev && rebuildWifiList()) {   // a Wi-Fi search finished
    g_seenNetworksRev = netRev;                              // (retried next pass if a finger was down)
  }
}
