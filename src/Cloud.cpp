#include "Cloud.h"
#include "Inventory.h"
#include "Network.h"
#include "Telemetry.h"
#include "config.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <atomic>
#include <deque>
#include <mutex>
#include <time.h>

namespace {
constexpr size_t   MAX_PENDING        = 200;     // oldest events drop past this (about 40 KB)
constexpr uint32_t HTTP_TIMEOUT_MS    = 15000;
constexpr uint32_t SYNC_FAIL_RETRY_MS = 30000;   // wait after a failed sync

std::mutex            g_mutex;
std::deque<String>    g_pending;                 // JSON bodies waiting to upload
CloudStatus           g_status{false, 0, 0, CloudLink::Unknown, 0, UINT32_MAX};
uint32_t              g_lastSyncMs = 0;
bool                  g_everSynced = false;
std::atomic<bool>     g_syncRequested{true};     // sync right after boot
std::atomic<uint32_t> g_eventSeq{0};             // bumps on every queued event (see pullSnapshot)

// ---- Talking to Apps Script --------------------------------------------------
// Apps Script answers every request to script.google.com with a 302. A working
// deployment points it at script.googleusercontent.com, where the real JSON reply
// waits. A deployment the station can't use points it at a Google sign-in page.
// So the first hop is taken by hand: we look at where Google sends us, then decide.
struct ScriptReply {
  CloudLink link     = CloudLink::Unknown;
  int       httpCode = 0;          // last HTTP status seen, <0 = network error
  String    body;                  // JSON text; only filled when the reply was JSON
};

struct HopResult {
  int    code = 0;
  String location;                 // Location header, if the server redirected
  bool   json = false;             // Content-Type was application/json
  String body;                     // only read when code == 200 and json (HTML pages are big)
};

const char* COLLECTED_HEADERS[] = {"Content-Type"};

HopResult hop(const char* method, const String& url, const String& body, bool followRedirects) {
  HopResult r;
  WiFiClientSecure client;
  client.setInsecure();                          // skips certificate check; fine for this project
  HTTPClient http;
  http.setFollowRedirects(followRedirects ? HTTPC_STRICT_FOLLOW_REDIRECTS : HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(client, url)) {
    r.code = HTTPC_ERROR_CONNECTION_REFUSED;
    return r;
  }
  http.collectHeaders(COLLECTED_HEADERS, 1);
  if (strcmp(method, "POST") == 0) {
    http.addHeader("Content-Type", "application/json");
    r.code = http.POST(body);
  } else {
    r.code = http.GET();
  }
  r.location = http.getLocation();
  r.json     = http.header("Content-Type").startsWith("application/json");
  if (r.code == 200 && r.json) r.body = http.getString();
  http.end();
  return r;
}

bool isRedirect(int code) { return code == 301 || code == 302 || code == 303 || code == 307 || code == 308; }

bool isSignInUrl(const String& location) {
  return location.indexOf("ServiceLogin") >= 0 || location.indexOf("accounts.google.com") >= 0;
}

// Only looks at "ok" and "error", so it's cheap even on a big snapshot.
CloudLink classifyJson(const String& body) {
  JsonDocument filter;
  filter["ok"]    = true;
  filter["error"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return CloudLink::ServerError;  // cut off mid-reply
  if (!doc["ok"].is<bool>()) return CloudLink::ServerError;              // not one of our replies
  if (doc["ok"].as<bool>()) return CloudLink::Ok;
  const char* error = doc["error"] | "";
  return strcmp(error, "forbidden") == 0 ? CloudLink::BadToken : CloudLink::Rejected;
}

ScriptReply classifyFinal(HopResult h) {          // a reply that isn't a redirect
  ScriptReply r;
  r.httpCode = h.code;
  if (h.code < 0)                           r.link = CloudLink::NetError;
  else if (h.code == 401 || h.code == 403)  r.link = CloudLink::SignInWall;
  else if (h.code != 200)                   r.link = CloudLink::ServerError;
  else if (!h.json)                         r.link = CloudLink::WebPage;
  else {
    r.link = classifyJson(h.body);
    r.body = std::move(h.body);
  }
  return r;
}

ScriptReply callScript(const char* method, const String& url, const String& body) {
  const HopResult first = hop(method, url, body, false);
  if (!isRedirect(first.code)) return classifyFinal(first);

  if (first.location.startsWith("https://script.googleusercontent.com/"))
    return classifyFinal(hop("GET", first.location, "", true));   // the real reply lives here

  ScriptReply r;
  r.httpCode = first.code;
  r.link     = isSignInUrl(first.location) ? CloudLink::SignInWall : CloudLink::WebPage;
  return r;
}

void setReply(const ScriptReply& reply) {
  CloudLink before;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    before                = g_status.link;
    g_status.lastHttpCode = reply.httpCode;
    g_status.link         = reply.link;
  }
  if (reply.link != before)
    Serial.printf("[cloud] Sheets: %s (HTTP %d)\n", Cloud::linkText(reply.link), reply.httpCode);
}

// ---- Work items ----------------------------------------------------------------
bool peekPending(String& out) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_pending.empty()) return false;
  out = g_pending.front();
  return true;
}

// Pop only if the front is still the event we uploaded. sendEvent() drops the
// oldest event when the queue is full, which could otherwise make us pop one
// that never reached Sheets.
void popPending(const String& uploaded) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_pending.empty() && g_pending.front() == uploaded) g_pending.pop_front();
}

bool queueEmpty() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_pending.empty();
}

enum class SyncResult : uint8_t { Applied, Deferred, Failed };

SyncResult pullSnapshot() {
  const uint32_t    seqBefore = g_eventSeq.load();
  const ScriptReply reply     = callScript("GET", String(SCRIPT_URL) + "?action=snapshot&token=" + API_TOKEN, "");
  setReply(reply);
  if (reply.link != CloudLink::Ok) {
    Serial.printf("[cloud] sync failed: %s (HTTP %d)\n", Cloud::linkText(reply.link), reply.httpCode);
    return SyncResult::Failed;
  }
  // A check-in/out queued during the download isn't in this snapshot yet. Applying it
  // would undo that change on screen, so skip it and sync again after the upload.
  if (g_eventSeq.load() != seqBefore || !queueEmpty()) {
    g_syncRequested = true;
    return SyncResult::Deferred;
  }
  if (!Inventory::applySnapshot(reply.body)) {
    Serial.println("[cloud] sync failed: snapshot didn't parse");
    return SyncResult::Failed;
  }
  if (g_eventSeq.load() != seqBefore) g_syncRequested = true;   // raced the apply itself; heal soon
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_lastSyncMs = millis();
    g_everSynced = true;
  }
  Serial.println("[cloud] synced roster + items");
  return SyncResult::Applied;
}

void pushMetrics() {
  const SystemMetrics m = Telemetry::read();
  JsonDocument doc;
  doc["token"]          = API_TOKEN;
  doc["type"]           = "metrics";
  doc["device"]         = Telemetry::deviceId();
  doc["uptime_s"]       = m.uptimeSec;
  doc["heap_free"]      = m.heapFree;
  doc["heap_min"]       = m.heapMin;
  doc["heap_max_alloc"] = m.heapMaxAlloc;
  doc["heap_total"]     = m.heapTotal;
  doc["psram_free"]     = m.psramFree;
  doc["psram_total"]    = m.psramTotal;
  doc["temp_c"]         = roundf(m.chipTempC * 10.0f) / 10.0f;
  doc["rssi"]           = m.wifiRssi;
  doc["cpu_mhz"]        = m.cpuMhz;
  doc["reset_reason"]   = m.resetReason;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    doc["pending"] = static_cast<uint32_t>(g_pending.size());
  }
  String body;
  serializeJson(doc, body);
  setReply(callScript("POST", SCRIPT_URL, body));
}

// ---- The background task ------------------------------------------------------
// True once waitMs have passed since `since`. Unsigned subtraction stays correct
// when millis() wraps (every 49.7 days), however long the station has been up.
bool elapsed(uint32_t now, uint32_t since, uint32_t waitMs) { return now - since >= waitMs; }

// Problems a person has to fix (deployment access, token). Retrying fast won't help.
bool isSetupProblem(CloudLink link) { return link == CloudLink::SignInWall || link == CloudLink::BadToken; }

void netTask(void*) {
  uint32_t uploadFrom  = 0, uploadWaitMs  = 0;   // wait 0 = upload as soon as something is queued
  uint32_t syncFrom    = 0, syncWaitMs    = 0;
  uint32_t metricsFrom = 0, metricsWaitMs = 0;
  bool     timeConfigured = false;

  for (;;) {
    const uint32_t now = millis();

    // 1) Wait for Wi-Fi (Network keeps it up and handles switching networks)
    if (!Network::connected()) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    if (!timeConfigured) {                       // real clock for event timestamps
      configTime(0, 0, "pool.ntp.org", "time.google.com");
      timeConfigured = true;
    }

    // 2) Upload queued events first, oldest first, one per pass.
    //    An event only leaves the queue once Sheets confirms it (or rejects it for good).
    String body;
    const bool hasPending = peekPending(body);
    if (hasPending && elapsed(now, uploadFrom, uploadWaitMs)) {
      const ScriptReply reply = callScript("POST", SCRIPT_URL, body);
      setReply(reply);
      uploadFrom = millis();
      if (reply.link == CloudLink::Ok) {
        popPending(body);
        uploadWaitMs = 0;
      } else if (reply.link == CloudLink::Rejected) {   // retrying can't fix bad data; don't block the queue
        Serial.printf("[cloud] Sheets rejected an event, dropping it: %s\n", reply.body.c_str());
        popPending(body);
        uploadWaitMs = 0;
      } else if (isSetupProblem(reply.link)) {           // keep it; retry slowly until someone fixes setup
        uploadWaitMs = CONFIG_ERROR_RETRY_MS;
      } else {                                           // keep it; network trouble usually passes
        Serial.printf("[cloud] upload failed: %s (HTTP %d), retrying soon\n",
                      Cloud::linkText(reply.link), reply.httpCode);
        uploadWaitMs = UPLOAD_RETRY_MS;
      }
    }
    // 3) Sync only when the queue is empty, so Sheets already has our latest events
    else if (!hasPending && (g_syncRequested.exchange(false) || elapsed(now, syncFrom, syncWaitMs))) {
      const SyncResult result = pullSnapshot();
      syncFrom = millis();
      if (result != SyncResult::Failed)           syncWaitMs = SYNC_INTERVAL_MS;
      else if (isSetupProblem(Cloud::status().link)) syncWaitMs = CONFIG_ERROR_RETRY_MS;
      else                                        syncWaitMs = SYNC_FAIL_RETRY_MS;
    }

    // 4) Telemetry
    if (elapsed(millis(), metricsFrom, metricsWaitMs)) {
      metricsFrom   = millis();
      metricsWaitMs = METRICS_INTERVAL_MS;
      pushMetrics();
    }

    vTaskDelay(pdMS_TO_TICKS(200));
  }
}
}  // namespace

// ---- Public API ------------------------------------------------------------------
void Cloud::begin() {                            // call after Network::begin()
  xTaskCreatePinnedToCore(netTask, "cloud", 16384, nullptr, 1, nullptr, 0);
}

void Cloud::sendEvent(const char* action, const std::string& barcode,
                      const std::string& className, const std::string& person) {
  JsonDocument doc;
  doc["token"]   = API_TOKEN;
  doc["type"]    = "event";
  doc["action"]  = action;
  doc["barcode"] = barcode;
  doc["class"]   = className;
  doc["person"]  = person;
  doc["device"]  = Telemetry::deviceId();
  const time_t t = time(nullptr);
  if (t > 1700000000) doc["ts"] = static_cast<int64_t>(t) * 1000;  // ms since epoch, only if NTP has synced
  String body;
  serializeJson(doc, body);

  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_pending.size() >= MAX_PENDING) g_pending.pop_front();
  g_pending.push_back(body);
  ++g_eventSeq;                                  // same lock as the push, so pullSnapshot never misses it
}

void Cloud::requestSync() { g_syncRequested = true; }

CloudStatus Cloud::status() {
  std::lock_guard<std::mutex> lock(g_mutex);
  CloudStatus s     = g_status;
  s.wifiConnected   = WiFi.isConnected();
  s.wifiRssi        = s.wifiConnected ? WiFi.RSSI() : 0;
  s.pendingEvents   = static_cast<uint32_t>(g_pending.size());
  s.lastSyncAgoSec  = g_everSynced ? (millis() - g_lastSyncMs) / 1000 : UINT32_MAX;
  return s;
}

const char* Cloud::linkText(CloudLink link) {
  switch (link) {
    case CloudLink::Unknown:     return "not contacted yet";
    case CloudLink::Ok:          return "OK";
    case CloudLink::NetError:    return "can't connect";
    case CloudLink::SignInWall:  return "needs public deployment";
    case CloudLink::WebPage:     return "got a web page (Wi-Fi login?)";
    case CloudLink::BadToken:    return "wrong API token";
    case CloudLink::Rejected:    return "rejected last request";
    case CloudLink::ServerError: return "server error";
  }
  return "unknown";
}
