#pragma once
// ============================================================================
//  Cloud: Google Apps Script, running in its own background task.
//  Wi-Fi itself belongs to Network; Cloud just waits until it's up.
//
//  - Uploads check-in/out events (queued, retried until Sheets confirms them)
//  - Pulls fresh roster + items from Sheets into Inventory every few minutes
//  - Pushes chip telemetry every minute
//  Nothing here blocks the UI: every call just drops work on a queue.
// ============================================================================
#include <Arduino.h>
#include <string>

// What the last Apps Script reply meant. The System tab shows this in words.
enum class CloudLink : uint8_t {
  Unknown,      // nothing sent yet
  Ok,           // Apps Script answered {"ok":true}
  NetError,     // DNS / TLS / timeout: HTTP code < 0
  SignInWall,   // Google wants a login: SCRIPT_URL isn't an "Anyone" deployment
  WebPage,      // HTML instead of JSON: Wi-Fi login page, or the script crashed
  BadToken,     // {"ok":false,"error":"forbidden"}: API_TOKEN doesn't match TOKEN in Code.gs
  Rejected,     // {"ok":false,...} for that one request; a rejected event is dropped
  ServerError,  // any other HTTP status (404 = wrong URL or deleted deployment)
};

struct CloudStatus {
  bool      wifiConnected;
  int8_t    wifiRssi;         // dBm
  int       lastHttpCode;     // last HTTP status seen: <0 = network error, 0 = none yet
  CloudLink link;             // what the last reply meant
  uint32_t  pendingEvents;    // check-ins/outs still waiting to upload
  uint32_t  lastSyncAgoSec;   // seconds since data was last pulled from Sheets (UINT32_MAX = never)
};

namespace Cloud {
  void begin();              // start the background task (core 0). Call after Network::begin()

  // Queue a check-in/out for upload. Never blocks; safe to call from the UI.
  void sendEvent(const char* action, const std::string& barcode,
                 const std::string& className, const std::string& person);

  void        requestSync(); // pull fresh roster + items as soon as possible
  CloudStatus status();

  const char* linkText(CloudLink link);   // short wording shared by the screen and Serial
}
