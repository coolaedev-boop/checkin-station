#include "Network.h"
#include "config.h"
#include <Preferences.h>
#include <WiFi.h>
#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>

namespace {
constexpr uint32_t SCAN_MS_PER_CHANNEL = 300;   // the core gives up on a scan after 20x this
constexpr size_t   MAX_NETWORKS        = 20;
constexpr uint8_t  TRIES_PER_JOIN      = 2;     // the first try after a switch sometimes fails for no real reason

struct Credentials {
  std::string ssid;
  std::string password;
};

enum class CmdType : uint8_t { Scan, Join, Forget };
struct Command {
  CmdType     type;
  Credentials creds;
};

// ---- Shared with other tasks (guarded by g_mutex) ------------------------------
std::mutex               g_mutex;
std::deque<Command>      g_commands;
std::vector<WifiNetwork> g_networks;
NetworkStatus            g_status{NetworkState::Offline, "", 0, false, false, JoinResult::None, ""};
std::atomic<uint32_t>    g_networksRev{0};
std::atomic<uint8_t>     g_lastReason{0};       // why the driver last dropped the link (0 = no news)
std::atomic<bool>        g_gotIp{false};        // an IP arrived since the current attempt started

// ---- Owned by the Wi-Fi task ------------------------------------------------------
Preferences  g_prefs;                           // flash storage for the saved network
Credentials  g_active;                          // what we (re)connect to: saved network, else config.h's
Credentials  g_current;                         // what the station is connected with right now
Credentials  g_lastBegun;                       // what the driver was last told to join
bool         g_hasSaved = false;

NetworkState g_state = NetworkState::Offline;
std::string  g_stateSsid;
JoinResult   g_join = JoinResult::None;
std::string  g_joinSsid;

struct Attempt {
  bool        running   = false;
  bool        trial     = false;                // picked on the System tab: save on success, fall back on failure
  uint8_t     tries     = 0;
  Credentials creds;
  uint32_t    startedMs = 0;
};
Attempt g_attempt;

bool        g_scanning       = false;
bool        g_scanPending    = false;
uint8_t     g_scanStartFails = 0;               // driver said no (busy reconnecting): retry a few times
uint32_t    g_scanFailedAtMs = 0;
bool        g_joinPending    = false;
bool        g_pendingIsTrial = false;           // picked on the System tab (vs. Forget switching back)
Credentials g_pendingJoin;
uint32_t    g_lastAttemptEndMs = 0;

bool sameCredentials(const Credentials& a, const Credentials& b) {
  return a.ssid == b.ssid && a.password == b.password;
}

// What the driver would refuse outright. Checked here because WiFi.begin()'s return
// value can be a leftover status from the previous try, so it can't be trusted.
bool credentialsValid(const Credentials& c) {
  if (c.ssid.empty() || c.ssid.size() > 32) return false;
  const size_t n = c.password.size();
  if (n == 0 || (n >= 8 && n <= 63)) return true;
  return n == 64 && std::all_of(c.password.begin(), c.password.end(),
                                [](char ch) { return isxdigit(static_cast<unsigned char>(ch)) != 0; });
}

// Copy the task's view into the shared status other tasks read.
void publish() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_status.state    = g_state;
  g_status.ssid     = g_stateSsid;
  g_status.scanning = g_scanning || g_scanPending;
  g_status.hasSaved = g_hasSaved;
  g_status.join     = g_join;
  g_status.joinSsid = g_joinSsid;
}

// Runs on the Wi-Fi driver's event task: just take notes, the Wi-Fi task acts on them.
void onWifiEvent(arduino_event_t* event) {
  if (event->event_id == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    g_gotIp = true;
  } else if (event->event_id == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    const uint8_t reason = event->event_info.wifi_sta_disconnected.reason;
    if (reason == WIFI_REASON_ASSOC_LEAVE) return;            // we hung up ourselves
    g_lastReason = reason ? reason : static_cast<uint8_t>(WIFI_REASON_UNSPECIFIED);
  }
}

WifiSecurity securityOf(wifi_auth_mode_t mode) {
  switch (mode) {
    case WIFI_AUTH_OPEN:          return WifiSecurity::Open;
    case WIFI_AUTH_WEP:
    case WIFI_AUTH_WPA_PSK:       return WifiSecurity::Legacy;   // below the driver's WPA2 minimum
    case WIFI_AUTH_ENTERPRISE:
    case WIFI_AUTH_WPA3_ENT_192:  return WifiSecurity::Enterprise;
    default:                      return WifiSecurity::Password;
  }
}

JoinResult resultFor(uint8_t reason, bool hasPassword) {
  switch (reason) {
    case WIFI_REASON_NO_AP_FOUND:
      return JoinResult::NotFound;
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE:
      return hasPassword ? JoinResult::WrongPassword : JoinResult::Failed;
    default:
      return JoinResult::Failed;
  }
}

const char* resultText(JoinResult r) {
  switch (r) {
    case JoinResult::WrongPassword: return "wrong password?";
    case JoinResult::NotFound:      return "not found";
    default:                        return "failed";
  }
}

// ---- Joining ----------------------------------------------------------------------
void startAttempt(const Credentials& creds, bool trial, uint8_t tries) {
  WiFi.disconnect();                             // stop whatever the driver was doing
  for (int i = 0; i < 20 && WiFi.isConnected(); ++i) vTaskDelay(pdMS_TO_TICKS(50));
  vTaskDelay(pdMS_TO_TICKS(100));                // let that hang-up's event go by
  g_lastReason = 0;
  g_gotIp      = false;
  g_attempt    = {true, trial, tries, creds, millis()};
  g_state      = NetworkState::Connecting;
  g_stateSsid  = creds.ssid;
  publish();
  Serial.printf("[wifi] connecting to %s\n", creds.ssid.c_str());
  if (!credentialsValid(creds)) {                // fails on the next pass, like a refused join
    g_lastReason = WIFI_REASON_UNSPECIFIED;
    return;
  }
  g_lastBegun = creds;
  WiFi.begin(creds.ssid.c_str(), creds.password.empty() ? nullptr : creds.password.c_str());
  // Return value ignored on purpose: the outcome arrives as events (GOT_IP or DISCONNECTED).
}

void attemptSucceeded() {
  if (g_attempt.trial) {                         // it works: make it the saved network
    g_active   = g_attempt.creds;
    g_hasSaved = true;
    g_prefs.putString("ssid", g_active.ssid.c_str());
    g_prefs.putString("pass", g_active.password.c_str());
    g_join     = JoinResult::Joined;
    g_joinSsid = g_active.ssid;
  }
  g_attempt.running = false;
  g_current   = g_attempt.creds;
  g_state     = NetworkState::Connected;
  g_stateSsid = g_attempt.creds.ssid;
  publish();
  Serial.printf("[wifi] connected to %s, IP %s\n", g_stateSsid.c_str(), WiFi.localIP().toString().c_str());
}

void attemptFailed(JoinResult why, uint8_t reason) {
  const Attempt failed = g_attempt;
  g_attempt.running  = false;
  g_lastAttemptEndMs = millis();
  WiFi.disconnect();                             // stop the driver's own retry
  Serial.printf("[wifi] couldn't connect to %s: %s (reason %u)\n",
                failed.creds.ssid.c_str(), resultText(why), static_cast<unsigned>(reason));

  if (failed.tries + 1 < TRIES_PER_JOIN) {       // one more go before giving up on it
    startAttempt(failed.creds, failed.trial, failed.tries + 1);
    return;
  }
  if (failed.trial) {
    g_join     = why;
    g_joinSsid = failed.creds.ssid;
    if (!g_active.ssid.empty()) {                // back to the network that was working
      startAttempt(g_active, false, 0);
      return;
    }
  }
  g_state     = NetworkState::Offline;
  g_stateSsid = g_active.ssid;
  publish();
}

void driveAttempt() {
  if (!g_attempt.running) return;
  if (g_gotIp && WiFi.isConnected()) {           // a fresh IP, not a stale flag from the old network
    attemptSucceeded();
  } else if (const uint8_t reason = g_lastReason.exchange(0)) {
    attemptFailed(resultFor(reason, !g_attempt.creds.password.empty()), reason);
  } else if (millis() - g_attempt.startedMs >= WIFI_JOIN_TIMEOUT_MS) {
    attemptFailed(JoinResult::Failed, 0);
  }
}

// Notice a dropped link, and reconnect to the active network every WIFI_RETRY_MS.
void keepAlive() {
  if (g_attempt.running || g_scanning || g_joinPending) return;
  if (g_state == NetworkState::Offline && WiFi.isConnected()) {
    // The Arduino core retries once by itself after the first drop since boot, even
    // with auto-reconnect off. If that worked, keep the link instead of redoing it.
    g_current   = g_lastBegun;
    g_state     = NetworkState::Connected;
    g_stateSsid = g_current.ssid;
    publish();
    Serial.printf("[wifi] back on %s\n", g_stateSsid.c_str());
    return;
  }
  if (g_state == NetworkState::Connected && !WiFi.isConnected()) {
    g_state            = NetworkState::Offline;
    g_lastAttemptEndMs = millis();
    publish();
    Serial.printf("[wifi] lost %s, retrying in %lu s\n", g_stateSsid.c_str(),
                  static_cast<unsigned long>(WIFI_RETRY_MS / 1000));
  }
  if (g_state == NetworkState::Offline && !g_active.ssid.empty() &&
      millis() - g_lastAttemptEndMs >= WIFI_RETRY_MS) {
    startAttempt(g_active, false, 0);
  }
}

// ---- Scanning (never during a join: the driver refuses) ------------------------
void startScanIfIdle() {
  if (!g_scanPending || g_attempt.running || g_joinPending) return;
  if (g_scanStartFails > 0 && millis() - g_scanFailedAtMs < 1000) return;
  if (WiFi.scanNetworks(true, false, false, SCAN_MS_PER_CHANNEL) == WIFI_SCAN_FAILED) {
    g_scanFailedAtMs = millis();
    if (++g_scanStartFails < 4) return;          // driver busy (e.g. the core reconnecting): try again in 1 s
    Serial.println("[wifi] scan didn't start");
    g_scanPending = false;
  } else {
    g_scanPending = false;
    g_scanning    = true;
  }
  g_scanStartFails = 0;
  publish();
}

void pollScan() {
  if (!g_scanning) return;
  const int16_t count = WiFi.scanComplete();
  if (count == WIFI_SCAN_RUNNING) return;
  g_scanning = false;

  if (count < 0) {                               // failed or timed out: keep the old list
    Serial.println("[wifi] scan failed");
    WiFi.scanDelete();
    publish();
    return;
  }
  std::vector<WifiNetwork> list;
  for (int16_t i = 0; i < count; ++i) {
    const String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;            // hidden network
    const WifiNetwork net{ssid.c_str(), static_cast<int8_t>(WiFi.RSSI(i)), securityOf(WiFi.encryptionType(i))};
    auto same = std::find_if(list.begin(), list.end(), [&](const WifiNetwork& n) { return n.ssid == net.ssid; });
    if (same == list.end())      list.push_back(net);
    else if (net.rssi > same->rssi) *same = net;   // several access points, one name: keep the strongest
  }
  WiFi.scanDelete();
  std::sort(list.begin(), list.end(), [](const WifiNetwork& a, const WifiNetwork& b) { return a.rssi > b.rssi; });
  if (list.size() > MAX_NETWORKS) list.resize(MAX_NETWORKS);
  Serial.printf("[wifi] found %u networks\n", static_cast<unsigned>(list.size()));

  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_networks = std::move(list);
  }
  ++g_networksRev;
  publish();
}

// ---- The background task ------------------------------------------------------------
bool popCommand(Command& out) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_commands.empty()) return false;
  out = std::move(g_commands.front());
  g_commands.pop_front();
  return true;
}

void pushCommand(Command cmd) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_commands.push_back(std::move(cmd));
}

void handleCommands() {
  Command cmd;
  while (popCommand(cmd)) {
    switch (cmd.type) {
      case CmdType::Scan:
        g_scanPending    = true;
        g_scanStartFails = 0;
        break;
      case CmdType::Join:
        g_pendingJoin    = std::move(cmd.creds);
        g_pendingIsTrial = true;
        g_joinPending    = true;
        g_join           = JoinResult::Joining;
        g_joinSsid       = g_pendingJoin.ssid;
        break;
      case CmdType::Forget:
        g_prefs.remove("ssid");
        g_prefs.remove("pass");
        g_hasSaved    = false;
        g_active      = {WIFI_SSID, WIFI_PASS};
        g_join        = JoinResult::None;
        g_joinSsid.clear();
        g_joinPending = false;
        if (g_active.ssid.empty()) {             // no default network: just go offline
          g_attempt.running = false;
          WiFi.disconnect();
          g_state = NetworkState::Offline;
          g_stateSsid.clear();
        } else if (!(WiFi.isConnected() && sameCredentials(g_current, g_active))) {
          g_pendingJoin    = g_active;           // switch back to config.h's network
          g_pendingIsTrial = false;
          g_joinPending    = true;
        }
        break;
    }
    publish();
  }
  if (g_joinPending && !g_scanning) {            // a join waits for a running scan to finish
    g_joinPending = false;
    startAttempt(g_pendingJoin, g_pendingIsTrial, 0);
  }
}

void wifiTask(void*) {
  if (!g_active.ssid.empty()) startAttempt(g_active, false, 0);
  for (;;) {
    handleCommands();
    driveAttempt();
    startScanIfIdle();
    pollScan();
    keepAlive();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
}  // namespace

// ---- Public API ------------------------------------------------------------------------
void Network::begin() {
  g_prefs.begin("network", false);
  if (g_prefs.isKey("ssid")) {
    g_active   = {g_prefs.getString("ssid", "").c_str(), g_prefs.getString("pass", "").c_str()};
    g_hasSaved = !g_active.ssid.empty();
  }
  if (!g_hasSaved) g_active = {WIFI_SSID, WIFI_PASS};
  g_stateSsid = g_active.ssid;

  WiFi.onEvent(onWifiEvent);                     // all events; the handler picks the two it needs
  WiFi.persistent(false);                        // credentials live in our own flash slot, not the driver's
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);                  // this module decides when to retry
  publish();
  xTaskCreatePinnedToCore(wifiTask, "wifi", 6144, nullptr, 1, nullptr, 0);
}

void Network::scan() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_commands.push_back({CmdType::Scan, {}});
  g_status.scanning = true;                      // show "Searching" now, not on the task's next pass
}

std::vector<WifiNetwork> Network::networks() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_networks;
}

uint32_t Network::networksRevision() { return g_networksRev.load(); }

void Network::join(const std::string& ssid, const std::string& password) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_commands.push_back({CmdType::Join, {ssid, password}});
  g_status.join     = JoinResult::Joining;       // replace the last result on screen right away
  g_status.joinSsid = ssid;
}

void Network::forget() { pushCommand({CmdType::Forget, {}}); }

bool Network::connected() { return WiFi.isConnected(); }

NetworkStatus Network::status() {
  NetworkStatus s;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    s = g_status;
  }
  s.rssi = (s.state == NetworkState::Connected && WiFi.isConnected()) ? static_cast<int8_t>(WiFi.RSSI()) : 0;
  return s;
}
