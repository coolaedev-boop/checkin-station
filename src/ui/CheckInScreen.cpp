// Check In: every scan lands here. With "Check Into Cart" ticked, a scan goes into
// the cart (borrowed later from Check Out). Unticked, scans line up as returns and
// Check In returns them all under the signed-on person.
// Figma frame "Check In Screen" (1002:137). Buttons 28 -> 36 px, Items pill 27 -> 32 px.
#include "Chrome.h"
#include "Inventory.h"
#include "Screens.h"
#include "Session.h"
#include "Theme.h"

using namespace Theme;

namespace CheckInScreen {
namespace {
lv_obj_t*  g_count = nullptr;        // the number in the "Items: N" pill
lv_obj_t*  g_name = nullptr;
lv_obj_t*  g_barcode = nullptr;
lv_obj_t*  g_desc = nullptr;
lv_obj_t*  g_checkIn = nullptr;
lv_obj_t*  g_tick = nullptr;         // check mark in the cart toggle's box
ImageFrame g_frame;
std::string g_shown;                 // barcode in the panel, empty before the first scan
bool        g_cartMode = false;

void updateButtons() {
  const size_t returns = Session::returns().size();
  setEnabled(g_checkIn, returns > 0);
  setButtonText(g_checkIn, returns > 1 ? ("Check In (" + std::to_string(returns) + ")").c_str() : "Check In");
  setTextIfChanged(g_count, std::to_string(Session::cart().size()).c_str());
  setVisible(g_tick, g_cartMode);
}

void showItem(const std::string& barcode) {
  g_shown = barcode;
  Item item;
  const bool known = Inventory::findItem(barcode, item);
  setTextIfChanged(g_name, known ? item.name.c_str() : "Unknown item");
  setTextIfChanged(g_barcode, barcode.c_str());
  const char* desc = !known                    ? "This barcode isn't in the Sheet yet."
                   : item.description.empty() ? "No description in the Sheet."
                                               : item.description.c_str();
  setTextIfChanged(g_desc, desc);
  showItemImage(g_frame, barcode, known);
}

void showNothing() {
  g_shown.clear();
  lv_label_set_text(g_name, "Scan an item");
  lv_label_set_text(g_barcode, "");
  lv_label_set_text(g_desc, "Scan to return it, or tick Check Into Cart to borrow it.");
  clearItemImage(g_frame);
}

void onItemsPill(lv_event_t*) { Nav::go(ScreenId::CheckOut); }

void onCartToggle(lv_event_t*) {
  g_cartMode = !g_cartMode;
  updateButtons();
  Chrome::toast(g_cartMode ? "Scans go into the cart" : "Scans are returns");
}

void onCheckIn(lv_event_t*) {
  const std::vector<std::string>& queued = Session::returns();
  if (queued.empty()) return;
  Item item;
  const std::string what = queued.size() == 1 ? (Inventory::findItem(queued[0], item) ? item.name : "Unknown item")
                                              : itemCount(queued.size());
  Session::checkInReturns();
  Chrome::toast(LV_SYMBOL_OK " Checked in " + what);
  updateButtons();
}
}  // namespace

// Shared with Check Out: what a scan into the cart says.
void cartToast(Session::AddResult r, const std::string& barcode) {
  Item item;
  Inventory::findItem(barcode, item);
  switch (r) {
    case Session::AddResult::Added:         Chrome::toast(LV_SYMBOL_OK " Added " + item.name + " to the cart"); break;
    case Session::AddResult::AlreadyInCart: Chrome::toast(item.name + " is already in the cart"); break;
    case Session::AddResult::CheckedOut:
      Chrome::toast(item.name + " is checked out to " + (item.holder.empty() ? "someone" : item.holder) +
                    ". Untick Check Into Cart to return it.", true);
      break;
    case Session::AddResult::Unknown:
      Chrome::toast("That barcode isn't in the Sheet, so it can't be borrowed", true);
      break;
  }
}

void build(lv_obj_t* scr) {
  title(scr, "Check In");

  lv_obj_t* items = pill(scr, "", 149, 55, 112, 32, PILL_RED, F_HEADER, onItemsPill);
  lv_obj_delete(labelOf(items));
  image(items, &img_ic_cart, 9, 6);
  text(items, "Items:", F_HEADER, WHITE, 32, 6);
  g_count = text(items, "0", F_BIG, WHITE, 82, 2);

  // Item panel. It runs under the image frame, so only its left corners show round.
  lv_obj_t* panel = rect(scr, 43, 94, 296, 120, PANEL_RED, 15);
  g_name    = textBox(panel, "", F_ITEM, WHITE, 17, 6, 250, LV_LABEL_LONG_DOT);
  g_barcode = text(panel, "", F_SMALL, BARCODE_PINK, 17, 26);
  lv_obj_t* descBox = rect(panel, 16, 50, 235, 52, DEEP_RED, 15);
  g_desc = textBox(descBox, "", F_SMALL, WHITE, 12, 6, 211, LV_LABEL_LONG_WRAP);
  lv_obj_set_height(g_desc, 40);                        // two lines at most

  g_frame = imageFrame(scr);

  // "Check Into Cart" sits behind Check In; its left end hides under the button.
  lv_obj_t* toggle = pill(scr, "", 150, BIG_BUTTON_Y, 184, BIG_BUTTON_H, TOGGLE_RED, F_BODY, onCartToggle);
  lv_obj_delete(labelOf(toggle));
  lv_obj_t* box = rect(toggle, 49, 8, 20, 20, CHECK_RED, 4);
  g_tick = text(box, LV_SYMBOL_OK, F_BODY, WHITE, 0, 0);
  lv_obj_center(g_tick);
  text(toggle, "Check Into Cart", F_BODY, WHITE, 77, 9);

  g_checkIn = edgeButton(scr, "Check In", BIG_BUTTON_Y, 190, BIG_BUTTON_H, RED, F_BIG, onCheckIn);

  reset();
}

void onShow() { updateButtons(); }

void onScan(const std::string& barcode) {
  showItem(barcode);
  Item item;
  const bool known = Inventory::findItem(barcode, item);
  if (g_cartMode) {
    cartToast(Session::addToCart(barcode), barcode);
  } else if (!Session::queueReturn(barcode)) {
    Chrome::toast("Already scanned. Tap Check In to return it.");
  } else if (!known) {
    Chrome::toast("Not in the Sheet yet. Checking it in adds it.");
  } else if (!item.checkedOut) {
    Chrome::toast(item.name + " is already checked in");
  }
  updateButtons();
}

void refresh() {
  if (!g_shown.empty()) showItem(g_shown);              // a sync may have renamed it or added a description
  updateButtons();
}

void reset() {
  g_cartMode = false;
  showNothing();
  updateButtons();
}
}  // namespace CheckInScreen
