// Settings: scanner, Wi-Fi and server status with their option buttons, plus chip health.
// Figma frame "Settings Screen" (1002:372). Option buttons 21 -> 30 px (right-aligned so
// long scanner/network names can't run into them); cards 121 -> 130 px; telemetry text
// 6-7 px -> 11 px with shorter values.
#include "Chrome.h"
#include "Cloud.h"
#include "Network.h"
#include "Scanner.h"
#include "Screens.h"
#include "Telemetry.h"
#include "Theme.h"

using namespace Theme;

namespace SettingsScreen {
namespace {
constexpr int32_t ROW_Y[3] = {100, 142, 184};

struct Row {
  lv_obj_t* title = nullptr;
  lv_obj_t* sub = nullptr;
  lv_obj_t* sub2 = nullptr;
};
Row       g_rows[3];
lv_obj_t* g_metric[4] = {};

void onBluetooth(lv_event_t*) { Nav::go(ScreenId::Bluetooth); }
void onWifi(lv_event_t*) { Nav::go(ScreenId::Wifi); }
void onSync(lv_event_t*) {
  Cloud::requestSync();
  Chrome::toast("Syncing with the Sheet");
}

Row row(lv_obj_t* scr, int i, const lv_image_dsc_t* icon, const char* button, int32_t buttonW, lv_event_cb_t cb) {
  const int32_t y = ROW_Y[i], pillX = 334 - buttonW;
  image(scr, icon, 17 + (29 - icon->header.w) / 2, y + 3 + (29 - icon->header.h) / 2);
  pill(scr, button, pillX, y + 5, buttonW, 30, DARK_PILL, F_BODY, cb);
  Row r;
  r.title = textBox(scr, "", F_STRONG, BLACK, 49, y, pillX - 49 - 6, LV_LABEL_LONG_DOT);
  r.sub   = textBox(scr, "", F_SMALL, SUBTEXT, 49, y + 17, pillX - 49 - 6, LV_LABEL_LONG_DOT);
  return r;
}

void metric(lv_obj_t* scr, int i, const lv_image_dsc_t* icon, const char* name) {
  const int32_t y = 119 + i * 26;
  image(scr, icon, 357, y + 2);
  text(scr, name, F_LABEL, BLACK, 377, y - 1);
  g_metric[i] = textBox(scr, "", F_SMALL, BLACK, 377, y + 11, 92, LV_LABEL_LONG_DOT);
}
}  // namespace

void build(lv_obj_t* scr) {
  title(scr, "Settings");

  rect(scr, 6, 96, 336, 130, CARD, 15);
  g_rows[0] = row(scr, 0, &img_ic_bt, "Bluetooth Options", 142, onBluetooth);
  g_rows[1] = row(scr, 1, &img_ic_wifi, "WiFi Options", 108, onWifi);
  g_rows[2] = row(scr, 2, &img_ic_server, "Force Sync", 89, onSync);
  g_rows[2].sub2 = textBox(scr, "", F_SMALL, SUBTEXT, 49, ROW_Y[2] + 29, 190, LV_LABEL_LONG_DOT);

  rect(scr, 348, 96, 124, 130, CARD, 15);
  text(scr, "Telemetry:", F_LABEL, BLACK, 358, 101);
  metric(scr, 0, &img_ic_ram, "RAM");
  metric(scr, 1, &img_ic_psram, "PSRAM");
  metric(scr, 2, &img_ic_clock, "Uptime");
  metric(scr, 3, &img_ic_temp, "CPU Temp (F)");
}

void tick() {
  char buf[96];

  const ScannerStatus s = Scanner::status();
  setTextIfChanged(g_rows[0].title, s.hasSavedDevice || s.state == ScannerState::Connected ? s.name.c_str()
                                                                                          : "No scanner");
  const char* scanner = "";
  switch (s.state) {
    case ScannerState::Connected:   scanner = "Connected"; break;
    case ScannerState::Connecting:  scanner = "Connecting..."; break;
    case ScannerState::Discovering: scanner = "Searching for scanners..."; break;
    case ScannerState::Idle:        scanner = s.hasSavedDevice ? "Waiting for it to wake up" : "Add one in Bluetooth Options"; break;
  }
  setTextIfChanged(g_rows[0].sub, scanner);

  const NetworkStatus n = Network::status();
  switch (n.state) {
    case NetworkState::Connected:  snprintf(buf, sizeof(buf), "Online (%d dBm)", n.rssi); break;
    case NetworkState::Connecting: snprintf(buf, sizeof(buf), "Connecting..."); break;
    case NetworkState::Offline:    snprintf(buf, sizeof(buf), "Offline"); break;
  }
  setTextIfChanged(g_rows[1].title, buf);
  setTextIfChanged(g_rows[1].sub, n.ssid.empty() ? "No network set" : n.ssid.c_str());

  const CloudStatus c = Cloud::status();
  if (!c.wifiConnected)                 snprintf(buf, sizeof(buf), "Offline");
  else if (c.link == CloudLink::Ok)     snprintf(buf, sizeof(buf), "Connected");
  else if (c.link == CloudLink::NetError || c.link == CloudLink::ServerError)
    snprintf(buf, sizeof(buf), "Sheets: %s (HTTP %d)", Cloud::linkText(c.link), c.lastHttpCode);
  else                                  snprintf(buf, sizeof(buf), "Sheets: %s", Cloud::linkText(c.link));
  setTextIfChanged(g_rows[2].title, buf);
  const std::string synced = c.lastSyncAgoSec == UINT32_MAX ? "never" : formatDuration(c.lastSyncAgoSec) + " ago";
  setTextIfChanged(g_rows[2].sub, ("Synced: " + synced).c_str());
  snprintf(buf, sizeof(buf), "Waiting: %lu", (unsigned long)c.pendingEvents);
  setTextIfChanged(g_rows[2].sub2, buf);

  const SystemMetrics m = Telemetry::read();
  snprintf(buf, sizeof(buf), "%lu KB, low %lu", (unsigned long)(m.heapFree / 1024), (unsigned long)(m.heapMin / 1024));
  setTextIfChanged(g_metric[0], buf);
  snprintf(buf, sizeof(buf), "%.1f MB free", m.psramFree / 1048576.0);
  setTextIfChanged(g_metric[1], buf);
  setTextIfChanged(g_metric[2], formatDuration(m.uptimeSec).c_str());
  snprintf(buf, sizeof(buf), "%.1f \xC2\xB0" "F", m.chipTempC * 9.0f / 5.0f + 32.0f);
  setTextIfChanged(g_metric[3], buf);
}
}  // namespace SettingsScreen
