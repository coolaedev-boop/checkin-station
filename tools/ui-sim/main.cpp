// UI simulator: runs the station's real UI (src/Ui.cpp, src/ui/) and Inventory on a PC,
// drives it with taps and scans, checks what happens, and saves 480x320 screenshots.
// Built and run by tools/ui-sim/run.ps1.
#include "sim.h"
#include "Inventory.h"
#include "Ui.h"
#include "config.h"
#include "ui/Session.h"
#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

namespace {
uint16_t    g_fb[480 * 320];
uint8_t     g_drawBuf[480 * 320 * 2];
int         g_x = 0, g_y = 0;
bool        g_down = false;
int         g_failures = 0;
std::string g_out = ".";

uint32_t tickCb() { return sim::nowMs; }

void flushCb(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
  const int w = lv_area_get_width(a);
  for (int y = a->y1; y <= a->y2; ++y) memcpy(&g_fb[y * 480 + a->x1], px + (y - a->y1) * w * 2, w * 2);
  lv_display_flush_ready(d);
}

void readCb(lv_indev_t*, lv_indev_data_t* data) {
  data->point.x = g_x;
  data->point.y = g_y;
  data->state   = g_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void run(int ms) {
  for (int t = 0; t < ms; t += 5) {
    Ui::update();
    sim::nowMs += 5;
    lv_timer_handler();
  }
}

void shot(const char* name) {
  run(500);                                         // let animations settle
  const std::string path = g_out + "/" + name + ".bmp";
  FILE* f = fopen(path.c_str(), "wb");
  const uint32_t rowBytes = 480 * 3, size = 54 + rowBytes * 320, off = 54, dib = 40, img = rowBytes * 320;
  const int32_t  w = 480, h = 320;
  const uint16_t planes = 1, bpp = 24;
  uint8_t hdr[54] = {'B', 'M'};
  memcpy(hdr + 2, &size, 4); memcpy(hdr + 10, &off, 4); memcpy(hdr + 14, &dib, 4); memcpy(hdr + 18, &w, 4);
  memcpy(hdr + 22, &h, 4); memcpy(hdr + 26, &planes, 2); memcpy(hdr + 28, &bpp, 2); memcpy(hdr + 34, &img, 4);
  fwrite(hdr, 1, 54, f);
  for (int y = 319; y >= 0; --y)
    for (int x = 0; x < 480; ++x) {
      const uint16_t c = g_fb[y * 480 + x];
      const uint8_t  r = c >> 11, g = (c >> 5) & 0x3F, b = c & 0x1F;   // RGB565 -> 888, full range
      const uint8_t  p[3] = {uint8_t(b << 3 | b >> 2), uint8_t(g << 2 | g >> 4), uint8_t(r << 3 | r >> 2)};
      fwrite(p, 1, 3, f);
    }
  fclose(f);
  printf("      screenshot %s\n", name);
}

void expect(bool ok, const std::string& what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
  if (!ok) ++g_failures;
}

// ---- Finding things on screen ----------------------------------------------------
bool visible(lv_obj_t* o) {
  for (; o; o = lv_obj_get_parent(o))
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return false;
  return true;
}

void walk(lv_obj_t* o, std::vector<lv_obj_t*>& out) {
  out.push_back(o);
  for (uint32_t i = 0; i < lv_obj_get_child_count(o); ++i) walk(lv_obj_get_child(o, i), out);
}

std::vector<lv_obj_t*> everything() {      // top layer first: it draws over the screen
  std::vector<lv_obj_t*> v;
  walk(lv_layer_top(), v);
  walk(lv_screen_active(), v);
  return v;
}

// A visible label whose text equals (or starts with) `text`.
lv_obj_t* label(const std::string& text, bool prefix = false) {
  for (lv_obj_t* o : everything()) {
    if (!lv_obj_has_class(o, &lv_label_class) || !visible(o)) continue;
    const std::string t = lv_label_get_text(o);
    if (prefix ? t.rfind(text, 0) == 0 : t == text) return o;
  }
  return nullptr;
}

std::string labelText(const std::string& prefix) {
  lv_obj_t* l = label(prefix, true);
  return l ? lv_label_get_text(l) : "<none>";
}

bool shown(const std::string& text) { return label(text) != nullptr; }

bool somewhere(const std::string& part) {   // any visible label containing `part`
  for (lv_obj_t* o : everything())
    if (lv_obj_has_class(o, &lv_label_class) && visible(o) &&
        std::string(lv_label_get_text(o)).find(part) != std::string::npos)
      return true;
  return false;
}

lv_obj_t* clickableOf(lv_obj_t* o) {
  while (o && !lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE)) o = lv_obj_get_parent(o);
  return o;
}

void tapAt(int x, int y) {
  g_x = x;
  g_y = y;
  g_down = true;
  run(60);
  g_down = false;
  run(120);
}

bool tap(const std::string& text, bool prefix = false) {
  lv_obj_t* l = label(text, prefix);
  if (!l) {
    expect(false, "tap: \"" + text + "\" is on screen");
    return false;
  }
  lv_obj_t* target = clickableOf(l);
  lv_obj_scroll_to_view_recursive(target, LV_ANIM_OFF);
  lv_obj_update_layout(target);
  lv_area_t a;
  lv_obj_get_coords(target, &a);
  lv_area_t vis = a;                                 // the part of a row that's on screen
  if (lv_obj_t* p = lv_obj_get_parent(target)) {
    lv_area_t pa;
    lv_obj_get_coords(p, &pa);
    vis.x1 = LV_MAX(a.x1, pa.x1); vis.y1 = LV_MAX(a.y1, pa.y1);
    vis.x2 = LV_MIN(a.x2, pa.x2); vis.y2 = LV_MIN(a.y2, pa.y2);
  }
  tapAt((vis.x1 + vis.x2) / 2, (vis.y1 + vis.y2) / 2);
  return true;
}

bool disabled(const std::string& text) {
  lv_obj_t* l = label(text, true);
  lv_obj_t* b = l ? clickableOf(l) : nullptr;
  return b && lv_obj_has_state(b, LV_STATE_DISABLED);
}

std::vector<lv_obj_t*> visibleDropdowns() {
  std::vector<lv_obj_t*> v;
  for (lv_obj_t* o : everything())
    if (lv_obj_check_type(o, &lv_dropdown_class) && visible(o)) v.push_back(o);
  return v;
}

void pick(lv_obj_t* dd, const char* option) {
  const int32_t idx = lv_dropdown_get_option_index(dd, option);
  expect(idx > 0, std::string("dropdown has \"") + option + "\"");
  lv_dropdown_set_selected(dd, static_cast<uint32_t>(idx));
  lv_obj_send_event(dd, LV_EVENT_VALUE_CHANGED, nullptr);
  run(50);
}

void scan(const char* code) {
  sim::scans.push_back(code);
  run(100);
}

void typePin(const char* digits) {
  for (const char* d = digits; *d; ++d) tap(std::string(1, *d));
}

int eventsLike(const std::string& prefix) {
  int n = 0;
  for (const std::string& e : sim::events) n += e.rfind(prefix, 0) == 0;
  return n;
}

// The Sheet the test starts from: six classes like the design, projects, descriptions,
// and two items already out.
const char* SNAPSHOT = R"json({"ok":true,
 "roster":[{"class":"2A","people":["Ava","Jordan"]},{"class":"2B","people":["Chris"]},
           {"class":"3A","people":["Dana","Eli"]},{"class":"3B","people":["Finn"]},
           {"class":"4A","people":["Gia"]},{"class":"4B","people":["Hugo"]}],
 "projects":["Morning Show","Film Club","Yearbook"],
 "items":[
  {"barcode":"100001","name":"Canon XA11 Camera","out":false,"holder":"","desc":"Camcorder, Canon XA11 (bag 3)"},
  {"barcode":"100002","name":"Rode Wireless GO","out":false,"holder":"","desc":"Wireless mic kit, 2 transmitters"},
  {"barcode":"100003","name":"Manfrotto Tripod","out":false,"holder":"","desc":"Tripod, Manfrotto 190X"},
  {"barcode":"100004","name":"Zoom H5 Recorder","out":true,"holder":"Finn (3B)","desc":"Audio recorder, Zoom H5"},
  {"barcode":"100005","name":"LED Light Panel","out":true,"holder":"Gia (4A)","desc":""}]})json";
}  // namespace

int main(int argc, char** argv) {
  if (argc > 1) g_out = argv[1];
  lv_init();
  lv_tick_set_cb(tickCb);
  lv_display_t* disp = lv_display_create(480, 320);
  lv_display_set_buffers(disp, g_drawBuf, nullptr, sizeof(g_drawBuf), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, flushCb);
  lv_indev_t* touch = lv_indev_create();
  lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touch, readCb);

  Inventory::begin();
  expect(Inventory::applySnapshot(SNAPSHOT), "test Sheet snapshot parses");
  Ui::begin();
  run(200);

  // ---- Sign On ---------------------------------------------------------------------
  shot("1_sign_on");
  expect(shown("Sign On") && shown("Class:") && shown("Name:") && shown("Project:"), "Sign On screen shows");
  expect(disabled("Log In"), "Log In is disabled until the form is filled");
  expect(!shown("Check In") && !shown("Log Out"), "no nav bar on Sign On");

  scan("100001");                                   // before anyone signed on: held
  expect(shown("Sign On"), "scan before sign-on stays on Sign On");
  expect(somewhere("Sign on first"), "toast asks to sign on first");

  tap("3");                                         // the "3" of chip 3A (its A is a separate label)
  auto dds = visibleDropdowns();
  expect(dds.size() == 2, "Name and Project dropdowns");
  pick(dds[0], "Dana");
  expect(disabled("Log In"), "still disabled without a project");
  pick(dds[1], "Film Club");
  expect(!disabled("Log In"), "Log In enabled with class, name and project");
  shot("1b_sign_on_filled");
  tap("Log In");

  expect(Session::active() && Session::who().name == "Dana" && Session::who().cls == "3A" &&
             Session::who().project == "Film Club",
         "signed on as Dana / 3A / Film Club");
  expect(shown("Name: Dana") && shown("Class: 3A") && shown("Project: Film Club"), "session bar shows who");
  expect(shown("Canon XA11 Camera"), "the held scan shows up on Check In after sign-on");
  expect(labelText("Check In (") == "<none>" && Session::returns().size() == 1, "held scan queued as a return");
  shot("2_check_in");

  // ---- Cart: borrow ------------------------------------------------------------------
  tap("Check Into Cart");
  scan("100002");
  scan("100003");
  scan("100003");                                   // twice: once in the cart
  expect(Session::cart().size() == 2, "two items in the cart (duplicate ignored)");
  expect(shown("2"), "Items pill shows 2");
  scan("100004");                                   // checked out to Finn: can't go in the cart
  expect(Session::cart().size() == 2, "a checked-out item is refused");
  expect(somewhere("Zoom H5 Recorder is checked out to Finn (3B)"), "and the toast says who has it");
  scan("999999");
  expect(Session::cart().size() == 2, "an unknown barcode is refused");
  shot("2b_check_in_cart");

  tap("Items:");                                    // the pill opens Check Out
  expect(shown("Checked Out Items") && shown("Rode Wireless GO") && shown("Manfrotto Tripod"),
         "Check Out lists the cart");
  expect(shown("Check Out (2)"), "button counts 2");
  shot("3_check_out");

  scan("100001");                                   // scanning on Check Out adds to the cart
  expect(Session::cart().size() == 3 && shown("Check Out (3)"), "scan on Check Out adds a third item");
  tap("Remove");                                    // first row: Rode Wireless GO
  run(100);
  expect(Session::cart().size() == 2 && !shown("Rode Wireless GO"), "Remove takes it out of the cart");
  sim::events.clear();
  tap("Check Out (2)");
  expect(eventsLike("checkout|") == 2 && eventsLike("checkout|100003|3A|Dana|Film Club") == 1 &&
             eventsLike("checkout|100001|3A|Dana|Film Club") == 1,
         "Check Out sends both under Dana / 3A / Film Club");
  for (const std::string& e : sim::events) printf("      %s\n", e.c_str());
  expect(Session::cart().empty() && labelText(LV_SYMBOL_OK " Checked out") == LV_SYMBOL_OK " Checked out 2 items to Dana",
         "cart empties, toast confirms");
  shot("3b_check_out_done");

  // ---- Returns -------------------------------------------------------------------------
  tap("Check In");                                  // nav pill
  tap("Check Into Cart");                           // untick: scans are returns again
  scan("100004");
  scan("100005");
  expect(Session::returns().size() == 3 && shown("Check In (3)"), "returns queue up: Check In (3)");
  sim::events.clear();
  tap("Check In (3)");
  expect(eventsLike("checkin|") == 3 && eventsLike("checkin|100004|3A|Dana|Film Club") == 1,
         "Check In returns all three under Dana / Film Club");
  Item it;
  expect(Inventory::findItem("100004", it) && !it.checkedOut, "the Zoom recorder is back in");

  // ---- Settings: PIN, main, Bluetooth, Wi-Fi --------------------------------------------
  tap("Settings");
  expect(shown("Enter PIN..."), "Settings asks for the PIN");
  shot("4_pin");
  typePin("123456");
  expect(shown("Enter PIN...") && somewhere("Wrong PIN"), "wrong PIN is refused");
  typePin(SETTINGS_PIN);
  expect(shown("Telemetry:") && shown("NT-1228BC") && shown("Online (-49 dBm)"), "right PIN opens Settings");
  shot("5_settings");
  tap("Force Sync");
  expect(sim::syncCalls == 1, "Force Sync asks Cloud to sync");

  tap("Bluetooth Options");
  sim::foundRev++;                                  // pretend a search finished
  run(100);
  expect(shown("Bluetooth Options") && shown("BarCode Scanner BLE"), "Bluetooth Options lists scanners");
  shot("6_bluetooth");
  tap("Inateck BCST-70");
  expect(sim::connectCalls == 1, "tapping a scanner connects to it");
  tap("Back");

  tap("WiFi Options");
  run(100);
  expect(shown("Library Guest") && labelText("NCCA-Staff").find(LV_SYMBOL_OK) != std::string::npos,
         "WiFi Options lists networks, check mark on the current one");
  shot("7_wifi");
  tap("NCCA-Students");
  expect(somewhere("NCCA-Students needs a username too"), "enterprise network explains it isn't supported");
  tap("Library Guest");
  expect(sim::joined.empty(), "open network needs a second tap");
  tap("Library Guest");
  expect(sim::joined == "Library Guest", "second tap joins it");
  tap("NCCA-Staff", true);
  expect(shown("Password for NCCA-Staff"), "a password network opens the password screen");
  shot("8_wifi_password");
  tap("Cancel");

  tap("Check In");                                  // leaving Settings locks it again
  tap("Settings");
  expect(shown("Enter PIN..."), "Settings asks for the PIN again after leaving");
  tap("Back");
  expect(shown("Check Into Cart"), "Back on the keypad returns to Check In");

  // ---- Log Out ---------------------------------------------------------------------------
  tap("Check Into Cart");
  scan("100002");
  tap("Log Out");
  expect(shown("Log out?"), "Log Out with a full cart asks first");
  shot("9_log_out_confirm");
  tap("Stay");
  expect(Session::active() && Session::cart().size() == 1, "Stay keeps the session and cart");
  tap("Log Out");
  tap("Log Out", false);                            // the box's button (the nav one is under it)
  run(100);
  expect(!Session::active() && shown("Sign On"), "Log Out signs out");

  // ---- Idle sign-out ----------------------------------------------------------------------
  tap("2");                                         // chip 2A
  pick(visibleDropdowns()[0], "Ava");
  pick(visibleDropdowns()[1], "Yearbook");
  tap("Log In");
  expect(Session::active(), "signed on again");
  run(1000);
  sim::nowMs += SESSION_IDLE_MS;                    // nobody touches anything for a while
  run(200);
  expect(!Session::active() && shown("Sign On") && somewhere("Signed out after 3 minutes"),
         "idle sign-out after SESSION_IDLE_MS");
  expect(disabled("Log In"), "the form is cleared for the next person");

  // ---- A sync while signed on --------------------------------------------------------------
  tap("4");
  pick(visibleDropdowns()[0], "Gia");
  pick(visibleDropdowns()[1], "Morning Show");
  tap("Log In");
  tap("Check Into Cart");
  scan("100002");
  expect(Session::cart().size() == 1, "Gia has the Rode mics in the cart");
  std::string later = SNAPSHOT;                      // then the Sheet says they're out
  const std::string was = R"("Rode Wireless GO","out":false)";
  later.replace(later.find(was), was.size(), R"("Rode Wireless GO","out":true)");
  Inventory::applySnapshot(later);
  run(100);
  expect(Session::cart().empty() && somewhere("1 item left the cart"), "an item the Sheet says is out leaves the cart");

  printf("\n%d failure(s)\n", g_failures);
  return g_failures ? 1 : 0;
}
