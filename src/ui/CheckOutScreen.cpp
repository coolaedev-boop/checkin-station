// Check Out: the cart. Scanning here adds to it too. Check Out(s) borrows everything
// in it under the signed-on Class + Name + Project.
// Figma frame "Check Out Screen" (1002:304). Rows 18 -> 30 px with a Remove pill
// (Forget-pill style), card 120 -> 128 px tall, button 28 -> 36 px.
#include "Chrome.h"
#include "Inventory.h"
#include "Screens.h"
#include "Session.h"
#include "Theme.h"
#include <algorithm>

using namespace Theme;

namespace CheckOutScreen {
namespace {
constexpr int32_t CARD_X = 19, CARD_Y = 94, CARD_W = 281, CARD_H = 128;
constexpr int32_t HEAD_H = 30, ROW_H = 30;

lv_obj_t*   g_list = nullptr;
lv_obj_t*   g_checkOut = nullptr;
ImageFrame  g_frame;
std::string g_preview;               // barcode shown in the image well

void rebuild();

void rebuildLater(void*) { rebuild(); }   // after the tap that asked for it has finished

// Highlight the previewed row, show its photo, and set the button. Doesn't delete rows,
// so it's safe to call from a row's own tap.
void showPreview() {
  const std::vector<std::string>& cart = Session::cart();
  for (size_t i = 0; i < cart.size() && i < lv_obj_get_child_count(g_list); ++i)
    lv_obj_set_style_bg_color(lv_obj_get_child(g_list, static_cast<int32_t>(i)),
                              color(cart[i] == g_preview ? NAV_ACTIVE : ROW_RED), 0);
  Item item;
  if (!g_preview.empty()) showItemImage(g_frame, g_preview, Inventory::findItem(g_preview, item));
  else                    clearItemImage(g_frame);
  setEnabled(g_checkOut, !cart.empty());
  setButtonText(g_checkOut, cart.size() > 1 ? ("Check Out (" + std::to_string(cart.size()) + ")").c_str()
                                            : "Check Out");
}

void onRow(lv_event_t* e) {
  const size_t i = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
  if (i >= Session::cart().size()) return;
  g_preview = Session::cart()[i];
  showPreview();
}

void onRemove(lv_event_t* e) {
  const size_t i = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
  if (i >= Session::cart().size()) return;
  const std::string code = Session::cart()[i];
  Session::removeFromCart(code);
  if (g_preview == code) g_preview.clear();
  lv_async_call(rebuildLater, nullptr);                     // this pill is inside the row being deleted
}

void onCheckOut(lv_event_t*) {
  const std::vector<std::string>& cart = Session::cart();
  if (cart.empty()) return;
  Item item;
  const std::string only = cart.size() == 1 && Inventory::findItem(cart[0], item) ? item.name : "";
  const Session::Result r = Session::checkOutCart();
  std::string msg;
  if (r.done == 0) {
    msg = "Those items are already checked out";
  } else {
    msg = LV_SYMBOL_OK " Checked out " + (only.empty() ? itemCount(r.done) : only) + " to " + Session::who().name;
    if (r.failed) msg += " (" + std::to_string(r.failed) + " already out)";
  }
  Chrome::toast(msg, r.done == 0);
  g_preview.clear();
  rebuild();
}

void rebuild() {
  const std::vector<std::string>& cart = Session::cart();
  lv_obj_clean(g_list);
  for (size_t i = 0; i < cart.size(); ++i) {
    Item item;
    const bool known = Inventory::findItem(cart[i], item);
    lv_obj_t* row = lv_button_create(g_list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, CARD_W, ROW_H);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, color(PANEL_RED), 0);
    lv_obj_add_event_cb(row, onRow, LV_EVENT_CLICKED, reinterpret_cast<void*>(i));
    textBox(row, known ? item.name.c_str() : cart[i].c_str(), F_BODY, WHITE, 17, 6, 186, LV_LABEL_LONG_DOT);
    pill(row, "Remove", CARD_W - 74, 4, 66, 22, DEEP_RED, F_SMALL, onRemove, reinterpret_cast<void*>(i));
  }
  if (cart.empty()) {
    lv_obj_t* hint = textBox(g_list, "Scan items here, or tick Check Into Cart on Check In.", F_SMALL, WHITE,
                             0, 0, CARD_W - 40, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_margin_top(hint, 24, 0);
  }
  showPreview();
}
}  // namespace

void build(lv_obj_t* scr) {
  title(scr, "Check Out");

  lv_obj_t* card = rect(scr, CARD_X, CARD_Y, CARD_W, CARD_H, PANEL_RED, 15);
  lv_obj_set_style_clip_corner(card, true, 0);              // rows follow the card's round bottom
  image(card, &img_arrow_small, 17, 8);
  text(card, "Checked Out Items", F_BODY, WHITE, 26, 6);
  g_list = plain(card, 0, HEAD_H, CARD_W, CARD_H - HEAD_H);
  lv_obj_add_flag(g_list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(g_list, LV_DIR_VER);
  lv_obj_set_flex_flow(g_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(g_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  g_frame = imageFrame(scr);
  g_checkOut = edgeButton(scr, "Check Out", BIG_BUTTON_Y, 204, BIG_BUTTON_H, RED, F_BIG, onCheckOut);
  rebuild();
}

void onShow() { rebuild(); }

void onScan(const std::string& barcode) {
  const Session::AddResult r = Session::addToCart(barcode);
  CheckInScreen::cartToast(r, barcode);
  if (r == Session::AddResult::Added || r == Session::AddResult::AlreadyInCart) g_preview = barcode;
  rebuild();
  if (r == Session::AddResult::Added) lv_obj_scroll_to_y(g_list, LV_COORD_MAX, LV_ANIM_ON);   // newest is last
}

void refresh() {
  if (!g_preview.empty() &&
      std::find(Session::cart().begin(), Session::cart().end(), g_preview) == Session::cart().end())
    g_preview.clear();
  rebuild();
}
}  // namespace CheckOutScreen
