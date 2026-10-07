// ============================================================================
//  Ui: builds the AVTF B.E.S.S. screens (src/ui/) and moves between them.
//
//  Sign On -> Check In <-> Check Out, plus Settings (PIN) -> Bluetooth / WiFi.
//  Scans go to Check In (or into the cart on Check Out). Nobody signed on: the
//  scan is held until someone logs in. SESSION_IDLE_MS without a touch or scan
//  signs out, so the next student's items never land under the last name.
// ============================================================================
#include "Ui.h"
#include "Inventory.h"
#include "Network.h"
#include "Scanner.h"
#include "config.h"
#include "ui/Chrome.h"
#include "ui/Screens.h"
#include "ui/Session.h"
#include "ui/Theme.h"
#include <lvgl.h>
#include <string>

namespace {
constexpr size_t SCREEN_COUNT = static_cast<size_t>(ScreenId::Count);

lv_obj_t*   g_screens[SCREEN_COUNT] = {};
ScreenId    g_current  = ScreenId::SignOn;
bool        g_unlocked = false;           // PIN entered; forgotten when Settings is left
std::string g_heldScan;                   // scanned before anyone signed on
lv_obj_t*   g_confirm = nullptr;          // "empty the cart?" box while it's open

uint32_t g_seenInventoryRev = UINT32_MAX;
uint32_t g_seenFoundRev     = UINT32_MAX;
uint32_t g_seenNetworksRev  = UINT32_MAX;

bool isSettingsPage(ScreenId id) {
  return id == ScreenId::Pin || id == ScreenId::Settings || id == ScreenId::Bluetooth || id == ScreenId::Wifi;
}

void everySecond(lv_timer_t*) {
  Chrome::refreshStatus();
  switch (g_current) {
    case ScreenId::Settings:  SettingsScreen::tick(); break;
    case ScreenId::Bluetooth: BluetoothScreen::tick(); break;
    case ScreenId::Wifi:      WifiScreen::tick(); break;
    default: break;
  }
}

void closeConfirm() {
  if (!g_confirm) return;
  lv_msgbox_close(g_confirm);
  g_confirm = nullptr;
}

void onConfirmSignOut(lv_event_t*) {
  closeConfirm();
  Nav::signOut(nullptr);
}

void onConfirmCancel(lv_event_t*) { closeConfirm(); }

void handleScan(const std::string& code) {
  lv_display_trigger_activity(nullptr);                  // a scan counts as activity for the idle timer
  if (!Session::active()) {
    g_heldScan = code;
    if (!isSettingsPage(g_current)) Nav::go(ScreenId::SignOn);
    Chrome::toast("Sign on first. Your item will be waiting.");
    return;
  }
  closeConfirm();
  if (g_current == ScreenId::CheckOut) {
    CheckOutScreen::onScan(code);
    return;
  }
  if (g_current != ScreenId::CheckIn) Nav::go(ScreenId::CheckIn);
  CheckInScreen::onScan(code);
}
}  // namespace

// ---- Navigation ------------------------------------------------------------------
void Nav::go(ScreenId id) {
  if ((id == ScreenId::CheckIn || id == ScreenId::CheckOut) && !Session::active()) id = ScreenId::SignOn;
  if (isSettingsPage(id) && id != ScreenId::Pin && !g_unlocked) id = ScreenId::Pin;
  if (!isSettingsPage(id)) g_unlocked = false;           // ask for the PIN again next time
  if (WifiScreen::passwordOpen()) WifiScreen::closePassword();
  Chrome::hideToast();

  for (size_t i = 0; i < SCREEN_COUNT; ++i) Theme::setVisible(g_screens[i], i == static_cast<size_t>(id));
  g_current = id;
  Chrome::layout(id);
  switch (id) {
    case ScreenId::CheckIn:   CheckInScreen::onShow(); break;
    case ScreenId::CheckOut:  CheckOutScreen::onShow(); break;
    case ScreenId::Pin:       PinScreen::onShow(); break;
    case ScreenId::Settings:  SettingsScreen::tick(); break;
    case ScreenId::Bluetooth: BluetoothScreen::onShow(); break;
    case ScreenId::Wifi:      WifiScreen::onShow(); break;
    default: break;
  }
}

ScreenId Nav::current() { return g_current; }

void Nav::back() { go(Session::active() ? ScreenId::CheckIn : ScreenId::SignOn); }

void Nav::unlockSettings() {
  g_unlocked = true;
  go(ScreenId::Settings);
}

void Nav::signedOn() {
  go(ScreenId::CheckIn);
  if (!g_heldScan.empty()) {
    const std::string code = g_heldScan;
    g_heldScan.clear();
    CheckInScreen::onScan(code);
  }
}

void Nav::logOut() {
  if (!Session::active()) {                              // the button reads "Sign On" then
    go(ScreenId::SignOn);
    return;
  }
  const size_t waiting = Session::cart().size() + Session::returns().size();
  if (waiting == 0) {
    signOut(nullptr);
    return;
  }
  closeConfirm();
  g_confirm = lv_msgbox_create(nullptr);
  lv_msgbox_add_title(g_confirm, "Log out?");
  const std::string msg = Theme::itemCount(waiting) + " in the cart or waiting to check in won't be saved.";
  lv_msgbox_add_text(g_confirm, msg.c_str());
  lv_obj_t* cancel = lv_msgbox_add_footer_button(g_confirm, "Stay");
  lv_obj_t* leave  = lv_msgbox_add_footer_button(g_confirm, "Log Out");
  lv_obj_set_style_bg_color(leave, Theme::color(Theme::RED), 0);
  lv_obj_set_style_bg_color(cancel, Theme::color(Theme::DARK_PILL), 0);
  lv_obj_add_event_cb(cancel, onConfirmCancel, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(leave, onConfirmSignOut, LV_EVENT_CLICKED, nullptr);
}

void Nav::signOut(const char* toast) {
  closeConfirm();
  Session::signOut();
  CheckInScreen::reset();
  SignOnScreen::reset();
  go(ScreenId::SignOn);
  if (toast) Chrome::toast(toast);
}

// ---- Public API --------------------------------------------------------------------
void Ui::begin() {
  Theme::init();
  Chrome::buildBackground();
  for (size_t i = 0; i < SCREEN_COUNT; ++i) {
    g_screens[i] = Theme::plain(lv_screen_active(), 0, 0, Theme::SCREEN_W, Theme::SCREEN_H);
    lv_obj_add_flag(g_screens[i], LV_OBJ_FLAG_HIDDEN);
  }
  SignOnScreen::build(g_screens[static_cast<size_t>(ScreenId::SignOn)]);
  CheckInScreen::build(g_screens[static_cast<size_t>(ScreenId::CheckIn)]);
  CheckOutScreen::build(g_screens[static_cast<size_t>(ScreenId::CheckOut)]);
  PinScreen::build(g_screens[static_cast<size_t>(ScreenId::Pin)]);
  SettingsScreen::build(g_screens[static_cast<size_t>(ScreenId::Settings)]);
  BluetoothScreen::build(g_screens[static_cast<size_t>(ScreenId::Bluetooth)]);
  WifiScreen::build(g_screens[static_cast<size_t>(ScreenId::Wifi)]);
  Chrome::buildOverlay();
  WifiScreen::buildPasswordPanel();                      // last: it covers everything

  Nav::go(ScreenId::SignOn);
  // First status refresh in a second, after main.cpp has started Bluetooth, Wi-Fi and
  // Cloud (Cloud::status() asks the Wi-Fi driver, which isn't running yet).
  lv_timer_create(everySecond, 1000, nullptr);
}

void Ui::update() {
  std::string code;
  while (Scanner::nextBarcode(code)) handleScan(code);

  if (Session::active() && lv_display_get_inactive_time(nullptr) >= SESSION_IDLE_MS) {
    const std::string why = "Signed out after " + std::to_string(SESSION_IDLE_MS / 60000) +
                            " minutes with no activity";
    Nav::signOut(why.c_str());
  }

  const uint32_t invRev = Inventory::revision();
  if (invRev != g_seenInventoryRev) {                    // data changed (sync, check-in, check-out)
    g_seenInventoryRev = invRev;
    const size_t dropped = Session::pruneCart();
    SignOnScreen::refresh();
    CheckInScreen::refresh();
    CheckOutScreen::refresh();
    if (dropped) Chrome::toast(Theme::itemCount(dropped) + " left the cart: the Sheet says checked out", true);
  }

  const uint32_t foundRev = Scanner::foundRevision();
  if (foundRev != g_seenFoundRev) {                      // a scanner search finished
    g_seenFoundRev = foundRev;
    if (foundRev > 0) BluetoothScreen::refreshList();
  }

  const uint32_t netRev = Network::networksRevision();
  if (netRev != g_seenNetworksRev && WifiScreen::refreshList()) {   // a Wi-Fi search finished
    g_seenNetworksRev = netRev;                                      // (retried next pass if a finger was down)
  }
}
