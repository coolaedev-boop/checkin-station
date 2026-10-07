#pragma once
// ============================================================================
//  The screens, and the router that moves between them (implemented in Ui.cpp).
//  Each screen builds into its own full-screen container, hidden until shown.
//  LVGL isn't thread-safe: everything here runs on the UI loop only.
// ============================================================================
#include <lvgl.h>
#include <stdint.h>
#include <string>
#include "Session.h"

enum class ScreenId : uint8_t { SignOn, CheckIn, CheckOut, Pin, Settings, Bluetooth, Wifi, Count };

namespace Nav {
void     go(ScreenId id);        // Check In/Out need someone signed on; Settings pages need the PIN
ScreenId current();
void     back();                 // leave Settings: Check In if signed on, else Sign On
void     unlockSettings();       // right PIN entered
void     signedOn();             // Log In pressed: Check In, plus any item scanned before sign-on
void     logOut();               // Log Out button: asks first if the cart or returns aren't empty
void     signOut(const char* toast);   // no questions asked (idle timeout); toast may be nullptr
}  // namespace Nav

namespace SignOnScreen {
void build(lv_obj_t* screen);
void refresh();                  // roster or projects changed
void reset();                    // clear the form for the next person
}

namespace CheckInScreen {
void build(lv_obj_t* screen);
void onShow();
void onScan(const std::string& barcode);
void refresh();                  // item data changed
void reset();                    // signed out
void cartToast(Session::AddResult r, const std::string& barcode);   // what a scan into the cart says
}

namespace CheckOutScreen {
void build(lv_obj_t* screen);
void onShow();
void onScan(const std::string& barcode);
void refresh();
}

namespace PinScreen {
void build(lv_obj_t* screen);
void onShow();
}

namespace SettingsScreen {
void build(lv_obj_t* screen);
void tick();                     // 1 Hz while showing
}

namespace BluetoothScreen {
void build(lv_obj_t* screen);
void onShow();
void tick();
void refreshList();              // a scanner search finished
}

namespace WifiScreen {
void build(lv_obj_t* screen);
void buildPasswordPanel();       // full-screen, so it's built last (draws over everything)
void onShow();
void tick();
bool refreshList();              // false: a finger is down, try again next pass
bool passwordOpen();
void closePassword();
}
