#include "Chrome.h"
#include "Cloud.h"
#include "Network.h"
#include "Scanner.h"
#include "Session.h"
#include "Theme.h"

using namespace Theme;

namespace Chrome {
namespace {
// The bottom border's diagonal edge, from the Figma path: (315,281) to (480,320).
constexpr float BORDER_X0 = 315, BORDER_Y0 = 281, BORDER_SLOPE = 165.0f / 39.0f;   // px right per px down
float borderX(float y) { return BORDER_X0 + (y - BORDER_Y0) * BORDER_SLOPE; }

lv_obj_t*   g_header = nullptr;
lv_obj_t*   g_lblName = nullptr;
lv_obj_t*   g_lblClass = nullptr;
lv_obj_t*   g_lblProject = nullptr;
lv_obj_t*   g_tagline = nullptr;
lv_obj_t*   g_status[3] = {};          // Bluetooth, Wi-Fi, server text
lv_obj_t*   g_nav = nullptr;
lv_obj_t*   g_navIn = nullptr;
lv_obj_t*   g_navOut = nullptr;
lv_obj_t*   g_navSettings = nullptr;
lv_obj_t*   g_logOut = nullptr;
lv_obj_t*   g_toast = nullptr;
lv_obj_t*   g_toastText = nullptr;
lv_timer_t* g_toastTimer = nullptr;

void fillRect(lv_layer_t* layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, lv_color_t c, int32_t radius) {
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_color = c;
  d.bg_opa   = LV_OPA_COVER;
  d.radius   = radius;
  const lv_area_t a = {x1, y1, x2, y2};
  lv_draw_rect(layer, &d, &a);
}

void fillTriangle(lv_layer_t* layer, float ax, float ay, float bx, float by, float cx, float cy, lv_color_t c) {
  lv_draw_triangle_dsc_t d;
  lv_draw_triangle_dsc_init(&d);
  d.bg_color = c;
  d.bg_opa   = LV_OPA_COVER;
  d.p[0].x = ax; d.p[0].y = ay;
  d.p[1].x = bx; d.p[1].y = by;
  d.p[2].x = cx; d.p[2].y = cy;
  lv_draw_triangle(layer, &d);
}

// TopBorder is BottomBorder turned 180 degrees: (0,0)-(480,0)-(480,39)-(165,39).
void drawBorders(lv_event_t* e) {
  lv_layer_t* layer = lv_event_get_layer(e);
  const lv_color_t red = color(RED);
  fillTriangle(layer, 0, 0, 165, 0, 165, 39, red);
  fillRect(layer, 165, 0, SCREEN_W - 1, 38, red, 0);
  fillRect(layer, 0, 281, 314, SCREEN_H - 1, red, 0);
  fillTriangle(layer, 315, 281, 480, 320, 315, 320, red);
}

// Log Out: round on the left, cut along the border's diagonal on the right.
void drawLogOut(lv_event_t* e) {
  auto*       btn   = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
  lv_layer_t* layer = lv_event_get_layer(e);
  lv_area_t   a;
  lv_obj_get_coords(btn, &a);
  lv_color_t c = color(NAV_DARK);
  if (lv_obj_has_state(btn, LV_STATE_PRESSED)) c = lv_color_darken(c, LV_OPA_30);
  const int32_t h    = lv_area_get_height(&a);
  const float   xTop = borderX(a.y1), xBottom = borderX(a.y2 + 1);
  fillRect(layer, a.x1, a.y1, a.x1 + h, a.y2, c, h / 2);
  fillRect(layer, a.x1 + h / 2, a.y1, static_cast<int32_t>(xTop), a.y2, c, 0);
  fillTriangle(layer, xTop, a.y1, xBottom, a.y2 + 1, xTop, a.y2 + 1, c);
}

void onNav(lv_event_t* e) {
  Nav::go(static_cast<ScreenId>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e))));
}

void onLogOut(lv_event_t*) { Nav::logOut(); }

void onStatusTapped(lv_event_t*) { Nav::go(ScreenId::Settings); }   // the way into Settings before sign-on

void hideToast(lv_timer_t* t) {
  lv_obj_add_flag(g_toast, LV_OBJ_FLAG_HIDDEN);
  lv_timer_pause(t);
}

lv_obj_t* sessionLabel(int32_t x, int32_t w) {
  return textBox(g_header, "", F_HEADER, WHITE, x - 147, 1, w, LV_LABEL_LONG_DOT);
}
}  // namespace

void buildBackground() {
  lv_obj_t* scr      = lv_screen_active();
  lv_obj_t* backdrop = plain(scr, 0, 0, SCREEN_W, SCREEN_H);
  lv_obj_add_event_cb(backdrop, drawBorders, LV_EVENT_DRAW_MAIN, nullptr);
  image(scr, &img_logo_avtf, 12, 19);
  image(scr, &img_logo_ncca, 399, 233);
}

void buildOverlay() {
  lv_obj_t* scr = lv_screen_active();

  // Session bar (design: 147,10 328x20)
  g_header     = rect(scr, 147, 10, 328, 20, HEADER_RED, 10);
  g_lblName    = sessionLabel(157, 118);
  g_lblClass   = sessionLabel(280, 60);
  g_lblProject = sessionLabel(344, 126);

  // Shown on the Settings pages, under the logo. The design's 7 px text, at 11 px.
  g_tagline = text(scr, "Barcode. Equipment. Scanner. System.", F_SMALL, TAGLINE, 62, 41);

  // Status icons, top right (design: 11 px icons, 5 px text; here 14 px and 11 px),
  // ending above the image frame at y 83. Tapping them opens Settings, which also
  // works before anyone has signed on.
  lv_obj_t* status = plain(scr, 398, 40, 80, 42);
  lv_obj_add_flag(status, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(status, onStatusTapped, LV_EVENT_CLICKED, nullptr);
  const lv_image_dsc_t* icons[3] = {&img_ic_bt_small, &img_ic_wifi_small, &img_ic_server_small};
  for (int i = 0; i < 3; ++i) {
    image(status, icons[i], 2, i * 14);
    g_status[i] = textBox(status, "", F_LABEL, BLACK, 19, i * 14, 61, LV_LABEL_LONG_DOT);
  }

  // Nav bar. Pills 21 -> 28 px tall; the dark band behind them starts a little higher
  // than the design (283 vs 288) to fit them.
  g_nav = plain(scr, 0, 283, SCREEN_W, SCREEN_H - 283);
  rect(g_nav, -16, 0, 289 + 16, 60, HEADER_RED, 16);        // only its top-right corner is on screen
  g_navIn       = pill(g_nav, "Check In", 12, 4, 89, 28, NAV_DARK, F_BODY, onNav,
                       reinterpret_cast<void*>(static_cast<uintptr_t>(ScreenId::CheckIn)));
  g_navOut      = pill(g_nav, "Check Out", 106, 4, 89, 28, NAV_DARK, F_BODY, onNav,
                       reinterpret_cast<void*>(static_cast<uintptr_t>(ScreenId::CheckOut)));
  g_navSettings = pill(g_nav, "Settings", 200, 4, 74, 28, NAV_DARK, F_BODY, onNav,
                       reinterpret_cast<void*>(static_cast<uintptr_t>(ScreenId::Settings)));

  const int32_t logX = 297, logW = static_cast<int32_t>(borderX(283 + 4 + 28)) - logX + 1;
  g_logOut = lv_button_create(g_nav);
  lv_obj_remove_style_all(g_logOut);
  lv_obj_set_pos(g_logOut, logX, 4);
  lv_obj_set_size(g_logOut, logW, 28);
  lv_obj_set_ext_click_area(g_logOut, 4);
  lv_obj_add_event_cb(g_logOut, drawLogOut, LV_EVENT_DRAW_MAIN, nullptr);
  lv_obj_add_event_cb(g_logOut, onLogOut, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lbl = text(g_logOut, "Log Out", F_BODY, WHITE, 17, 0);
  lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 17, 0);

  // Toast: on the top layer, so it shows over everything. Taps go through it.
  g_toast = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(g_toast);
  lv_obj_remove_flag(g_toast, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(g_toast, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(g_toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(g_toast, LV_OPA_90, 0);
  lv_obj_set_style_radius(g_toast, 16, 0);
  lv_obj_set_style_pad_hor(g_toast, 16, 0);
  lv_obj_set_style_pad_ver(g_toast, 9, 0);
  lv_obj_add_flag(g_toast, LV_OBJ_FLAG_HIDDEN);
  g_toastText = text(g_toast, "", F_BODY, WHITE, 0, 0);
  lv_label_set_long_mode(g_toastText, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(g_toastText, LV_TEXT_ALIGN_CENTER, 0);
  g_toastTimer = lv_timer_create(hideToast, 2600, nullptr);
  lv_timer_pause(g_toastTimer);
}

void layout(ScreenId shown) {
  const bool signedOn   = Session::active();
  const bool signOnPage = shown == ScreenId::SignOn;
  const bool settings   = shown == ScreenId::Pin || shown == ScreenId::Settings ||
                          shown == ScreenId::Bluetooth || shown == ScreenId::Wifi;
  setVisible(g_header, shown == ScreenId::CheckIn || shown == ScreenId::CheckOut);   // as in the Figma frames
  setVisible(g_nav, !signOnPage);
  setVisible(g_tagline, settings);

  const struct { lv_obj_t* pill; bool active; } pills[] = {
    {g_navIn, shown == ScreenId::CheckIn}, {g_navOut, shown == ScreenId::CheckOut}, {g_navSettings, settings}};
  for (const auto& p : pills)
    lv_obj_set_style_bg_color(p.pill, color(p.active ? NAV_ACTIVE : NAV_DARK), 0);
  setEnabled(g_navIn, signedOn);                       // nobody to check items in or out for yet
  setEnabled(g_navOut, signedOn);
  setTextIfChanged(lv_obj_get_child(g_logOut, 0), signedOn ? "Log Out" : "Sign On");
  refreshSession();
}

void refreshSession() {
  const Session::Who& w = Session::who();
  setTextIfChanged(g_lblName, ("Name: " + w.name).c_str());
  setTextIfChanged(g_lblClass, ("Class: " + w.cls).c_str());
  setTextIfChanged(g_lblProject, ("Project: " + w.project).c_str());
}

void refreshStatus() {
  const char* text[3];
  bool        ok[3];

  const ScannerStatus s = Scanner::status();
  ok[0] = s.state == ScannerState::Connected;
  switch (s.state) {
    case ScannerState::Connected:   text[0] = "Connected"; break;
    case ScannerState::Connecting:  text[0] = "Connecting"; break;
    case ScannerState::Discovering: text[0] = "Searching"; break;
    case ScannerState::Idle:        text[0] = s.hasSavedDevice ? "Asleep" : "No scanner"; break;
  }

  const NetworkStatus n = Network::status();
  ok[1] = n.state == NetworkState::Connected;
  text[1] = ok[1] ? "Connected" : n.state == NetworkState::Connecting ? "Joining" : "Offline";

  const CloudStatus c = Cloud::status();
  ok[2] = c.wifiConnected && c.link == CloudLink::Ok;
  if (!c.wifiConnected)                      text[2] = "Offline";
  else switch (c.link) {
    case CloudLink::Ok:          text[2] = c.pendingEvents ? "Uploading" : "Connected"; break;
    case CloudLink::Unknown:     text[2] = "Waiting"; break;
    case CloudLink::NetError:    text[2] = "No internet"; break;
    case CloudLink::SignInWall:
    case CloudLink::BadToken:    text[2] = "Setup issue"; break;
    case CloudLink::WebPage:     text[2] = "Wi-Fi login"; break;
    case CloudLink::Rejected:    ok[2] = true; text[2] = "Connected"; break;   // one bad event; the link works
    case CloudLink::ServerError: text[2] = "Error"; break;
  }

  for (int i = 0; i < 3; ++i) {
    setTextIfChanged(g_status[i], text[i]);
    lv_obj_set_style_text_color(g_status[i], color(ok[i] ? BLACK : PANEL_RED), 0);
  }
}

void toast(const std::string& msg, bool warning) {
  lv_label_set_text(g_toastText, msg.c_str());
  lv_point_t size;                                       // wrap at 300 px, otherwise hug the text
  lv_text_get_size(&size, msg.c_str(), F_BODY, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  lv_obj_set_width(g_toastText, size.x < 300 ? size.x + 1 : 300);
  lv_obj_set_style_bg_color(g_toast, color(warning ? DEEP_RED : LIST_DARK), 0);
  lv_obj_remove_flag(g_toast, LV_OBJ_FLAG_HIDDEN);
  lv_obj_align(g_toast, LV_ALIGN_TOP_MID, 0, 4);       // over the top border, clear of the item details
  lv_timer_reset(g_toastTimer);
  lv_timer_resume(g_toastTimer);
}

void hideToast() {
  lv_obj_add_flag(g_toast, LV_OBJ_FLAG_HIDDEN);
  lv_timer_pause(g_toastTimer);
}
}  // namespace Chrome
