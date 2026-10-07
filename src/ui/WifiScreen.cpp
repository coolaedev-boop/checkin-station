// WiFi Options: pick a network. Tapping a password network opens the password screen
// (not in the Figma file; styled to match). An open network needs a second tap.
// Figma frame "Settings Screen - Bluetooth Options" (1002:568, the Wi-Fi one).
// Rows 20 -> 28 px, Scan 21 -> 32 px, Back 21 -> 34 px, text 8 px -> 11-13 px.
#include "Network.h"
#include "Screens.h"
#include "Theme.h"
#include <Arduino.h>

using namespace Theme;

namespace WifiScreen {
namespace {
constexpr int32_t CARD_W = 468, LIST_Y = 44, ROW_H = 28;

lv_obj_t*                g_list = nullptr;
lv_obj_t*                g_scan = nullptr;
lv_obj_t*                g_status = nullptr;
std::vector<WifiNetwork> g_items;              // what the list is showing
std::string              g_listedOn;           // network shown with the check mark
std::string              g_note;               // one-off message, e.g. "needs a username"
std::string              g_confirmOpen;        // open network tapped once; a second tap joins
uint32_t                 g_confirmOpenMs = 0;

// Password screen
lv_obj_t*   g_pwPanel = nullptr;
lv_obj_t*   g_pwTitle = nullptr;
lv_obj_t*   g_pwHint = nullptr;
lv_obj_t*   g_pwShow = nullptr;
lv_obj_t*   g_pwText = nullptr;
std::string g_pwSsid;

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

void onBack(lv_event_t*) { Nav::go(ScreenId::Settings); }

void refreshStatus() {
  const NetworkStatus s = Network::status();
  char line1[96] = "";
  switch (s.state) {
    case NetworkState::Connected:  snprintf(line1, sizeof(line1), "Connected to %s (%d dBm)", s.ssid.c_str(), s.rssi); break;
    case NetworkState::Connecting: snprintf(line1, sizeof(line1), "Connecting to %s...", s.ssid.c_str()); break;
    case NetworkState::Offline:
      if (s.ssid.empty()) snprintf(line1, sizeof(line1), "Offline, no network set");
      else                snprintf(line1, sizeof(line1), "Offline, retrying %s soon", s.ssid.c_str());
      break;
  }
  char line2[128] = "";
  const char* js = s.joinSsid.c_str();
  if (!g_note.empty()) snprintf(line2, sizeof(line2), "%s", g_note.c_str());
  else switch (s.join) {
    case JoinResult::Joining:       snprintf(line2, sizeof(line2), "Joining %s...", js); break;
    case JoinResult::Joined:        snprintf(line2, sizeof(line2), LV_SYMBOL_OK " Joined %s. Saved for next time.", js); break;
    case JoinResult::WrongPassword: snprintf(line2, sizeof(line2), LV_SYMBOL_WARNING " Couldn't join %s: wrong password?", js); break;
    case JoinResult::NotFound:      snprintf(line2, sizeof(line2), LV_SYMBOL_WARNING " Couldn't find %s", js); break;
    case JoinResult::Failed:        snprintf(line2, sizeof(line2), LV_SYMBOL_WARNING " Couldn't join %s", js); break;
    case JoinResult::None:          snprintf(line2, sizeof(line2), "Tap a network to join it"); break;
  }
  setTextIfChanged(g_status, (std::string(line1) + "\n" + line2).c_str());
  setButtonText(g_scan, s.scanning ? "Scanning" : "Scan");
}

void openPassword(const std::string& ssid) {
  g_pwSsid = ssid;
  lv_label_set_text(g_pwTitle, ("Password for " + ssid).c_str());
  lv_label_set_text(g_pwHint, "");
  lv_textarea_set_text(g_pwText, "");
  lv_textarea_set_password_mode(g_pwText, true);
  setButtonText(g_pwShow, "Show");
  setVisible(g_pwPanel, true);
}

void onPicked(lv_event_t* e) {
  const size_t idx = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
  if (idx >= g_items.size()) return;
  const WifiNetwork net = g_items[idx];
  g_note.clear();
  switch (net.security) {
    case WifiSecurity::Open:                      // two taps, so a stray tap can't switch networks
      if (g_confirmOpen == net.ssid && millis() - g_confirmOpenMs < 5000) {
        g_confirmOpen.clear();
        Network::join(net.ssid, "");
      } else {
        g_confirmOpen   = net.ssid;
        g_confirmOpenMs = millis();
        g_note = net.ssid + " has no password. Tap it again to join.";
      }
      break;
    case WifiSecurity::Password:   openPassword(net.ssid); break;
    case WifiSecurity::Enterprise: g_note = net.ssid + " needs a username too. Not supported."; break;
    case WifiSecurity::Legacy:     g_note = net.ssid + " uses old security (WEP/WPA1). Not supported."; break;
  }
  refreshStatus();
}

void onScan(lv_event_t*) {
  g_note.clear();
  g_confirmOpen.clear();
  Network::scan();
  refreshStatus();
}

void forgetLater(void*) { refreshList(); }

void onForget(lv_event_t*) {
  g_note.clear();
  Network::forget();
  refreshStatus();
  g_items.clear();                                       // force a full rebuild: the Forget pill goes away
  lv_async_call(forgetLater, nullptr);                   // not now: this pill is inside a row
}

const char* kindText(WifiSecurity s) {
  return s == WifiSecurity::Open ? "open" : s == WifiSecurity::Enterprise ? "needs username"
       : s == WifiSecurity::Legacy ? "old security" : "";
}

std::string strengthText(const WifiNetwork& net) {
  char buf[48];
  snprintf(buf, sizeof(buf), "Strength (%d dBm)", net.rssi);
  return buf;
}

bool sameRows(const std::vector<WifiNetwork>& a, const std::vector<WifiNetwork>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (a[i].ssid != b[i].ssid || a[i].security != b[i].security) return false;
  return true;
}

// Password screen actions
void submitPassword() {
  const std::string password = lv_textarea_get_text(g_pwText);
  if (password.size() < 8) {
    lv_label_set_text(g_pwHint, LV_SYMBOL_WARNING " Wi-Fi passwords are at least 8 characters");
    return;
  }
  Network::join(g_pwSsid, password);
  closePassword();
  refreshStatus();
}
void onConnect(lv_event_t*) { submitPassword(); }
void onCancel(lv_event_t*) { closePassword(); }
void onShowPassword(lv_event_t*) {
  const bool hidden = lv_textarea_get_password_mode(g_pwText);
  lv_textarea_set_password_mode(g_pwText, !hidden);
  setButtonText(g_pwShow, hidden ? "Hide" : "Show");
}
}  // namespace

void build(lv_obj_t* scr) {
  breadcrumb(scr, &img_ic_wifi, "WiFi Options", onBack);

  lv_obj_t* card = rect(scr, 6, 96, CARD_W, 128, CARD, 15);
  lv_obj_set_style_clip_corner(card, true, 0);
  g_scan   = pill(card, "Scan", 13, 6, 80, 32, DARK_PILL, F_BODY, onScan);
  g_status = textBox(card, "", F_SMALL, SUBTEXT, 103, 5, CARD_W - 103 - 10, LV_LABEL_LONG_DOT);
  lv_obj_set_height(g_status, 32);

  g_list = plain(card, 0, LIST_Y, CARD_W, 128 - LIST_Y);
  lv_obj_set_style_bg_color(g_list, color(LIST_DARK), 0);
  lv_obj_set_style_bg_opa(g_list, LV_OPA_COVER, 0);
  lv_obj_add_flag(g_list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(g_list, LV_DIR_VER);
  lv_obj_set_flex_flow(g_list, LV_FLEX_FLOW_COLUMN);

  pill(scr, "Back", 6, 232, 80, 34, DARK_PILL, F_BODY, onBack);
}

void buildPasswordPanel() {
  g_pwPanel = rect(lv_screen_active(), 0, 0, SCREEN_W, SCREEN_H, WHITE, 0);
  lv_obj_add_flag(g_pwPanel, LV_OBJ_FLAG_CLICKABLE);      // taps don't reach the screens underneath
  rect(g_pwPanel, 0, 0, SCREEN_W, 6, RED, 0);
  image(g_pwPanel, &img_arrow_title, 14, 14);
  g_pwTitle = textBox(g_pwPanel, "", F_BIG, BLACK, 32, 16, 330, LV_LABEL_LONG_DOT);
  pill(g_pwPanel, "Cancel", 380, 12, 90, 32, DARK_PILL, F_BODY, onCancel);

  g_pwText = lv_textarea_create(g_pwPanel);
  lv_obj_set_pos(g_pwText, 14, 54);
  lv_obj_set_size(g_pwText, 282, 40);
  lv_textarea_set_one_line(g_pwText, true);
  lv_textarea_set_password_mode(g_pwText, true);
  lv_textarea_set_max_length(g_pwText, 64);
  lv_textarea_set_placeholder_text(g_pwText, "Password");
  lv_obj_set_style_text_font(g_pwText, F_ITEM, 0);
  lv_obj_set_style_radius(g_pwText, 12, 0);
  lv_obj_set_style_border_color(g_pwText, color(DARK_PILL), 0);
  lv_obj_set_style_border_color(g_pwText, color(RED), LV_STATE_FOCUSED);
  g_pwShow = pill(g_pwPanel, "Show", 304, 58, 70, 32, DARK_PILL, F_BODY, onShowPassword);
  pill(g_pwPanel, "Connect", 380, 58, 90, 32, RED, F_BODY, onConnect);
  g_pwHint = text(g_pwPanel, "", F_SMALL, PANEL_RED, 16, 98);

  lv_obj_t* kb = lv_keyboard_create(g_pwPanel);
  lv_obj_set_size(kb, SCREEN_W, SCREEN_H - 116);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);            // keyboards are bottom-aligned; set_pos would offset that
  lv_obj_set_style_text_font(kb, F_ITEM, 0);
  lv_obj_set_style_text_font(kb, F_ITEM, LV_PART_ITEMS);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_SPECIAL, KB_MAP_SPECIAL, KB_CTRL_SPECIAL);
  lv_keyboard_set_textarea(kb, g_pwText);
  // The keyboard forwards its check key and Enter to the text box as READY. Its
  // keyboard/close key (CANCEL) is left unhandled: Cancel at the top leaves.
  lv_obj_add_event_cb(g_pwText, onConnect, LV_EVENT_READY, nullptr);
  setVisible(g_pwPanel, false);
}

void onShow() {
  g_note.clear();
  g_confirmOpen.clear();
  Network::scan();                                       // fresh list every time it opens
  refreshStatus();
  refreshList();
}

void tick() {
  refreshStatus();
  const NetworkStatus n = Network::status();
  if ((n.state == NetworkState::Connected ? n.ssid : "") != g_listedOn) refreshList();   // move the check mark
}

bool refreshList() {
  std::vector<WifiNetwork> fresh = Network::networks();
  const NetworkStatus      s     = Network::status();
  const std::string        on    = s.state == NetworkState::Connected ? s.ssid : "";

  if (!fresh.empty() && on == g_listedOn && sameRows(fresh, g_items) &&
      lv_obj_get_child_count(g_list) == fresh.size()) {
    for (size_t i = 0; i < fresh.size(); ++i)     // same rows in the same order: just update the signal numbers
      setTextIfChanged(lv_obj_get_child(lv_obj_get_child(g_list, static_cast<int32_t>(i)), 1),
                       strengthText(fresh[i]).c_str());
    g_items = std::move(fresh);
    return true;
  }
  if (fingerDown()) return false;                  // rows never move under a tap

  const int32_t scrollY = lv_obj_get_scroll_y(g_list);
  g_items    = std::move(fresh);
  g_listedOn = on;
  lv_obj_clean(g_list);
  for (size_t i = 0; i < g_items.size(); ++i) {
    const WifiNetwork& net = g_items[i];
    lv_obj_t* r = lv_button_create(g_list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, CARD_W, ROW_H);
    lv_obj_set_style_bg_color(r, color(LIST_DARK), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r, color(DARK_PILL), LV_STATE_PRESSED);
    lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(r, 1, 0);
    lv_obj_set_style_border_color(r, color(DARK_PILL), 0);
    lv_obj_add_event_cb(r, onPicked, LV_EVENT_CLICKED, reinterpret_cast<void*>(i));
    const std::string name = net.ssid == g_listedOn ? net.ssid + "  " LV_SYMBOL_OK : net.ssid;
    textBox(r, name.c_str(), F_BODY, WHITE, 13, 6, 170, LV_LABEL_LONG_DOT);     // child 0
    text(r, strengthText(net).c_str(), F_SMALL, WHITE, 190, 8);                  // child 1: updated in place
    text(r, kindText(net.security), F_SMALL, MUTED, 300, 8);
    if (net.ssid == g_listedOn && s.hasSaved) pill(r, "Forget", 390, 3, 64, 22, DARK_PILL, F_SMALL, onForget);
  }
  if (g_items.empty()) text(g_list, "No networks listed yet. Tap Scan.", F_BODY, WHITE, 13, 8);
  lv_obj_update_layout(g_list);
  lv_obj_scroll_to_y(g_list, scrollY, LV_ANIM_OFF);   // stay where the user was
  return true;
}

bool passwordOpen() { return g_pwPanel && !lv_obj_has_flag(g_pwPanel, LV_OBJ_FLAG_HIDDEN); }

void closePassword() {
  lv_textarea_set_text(g_pwText, "");                    // don't leave a password lying around
  setVisible(g_pwPanel, false);
}
}  // namespace WifiScreen
