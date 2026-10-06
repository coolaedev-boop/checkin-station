#include "Scanner.h"
#include "config.h"
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>

namespace {
constexpr size_t BARCODE_MAX = 64;

// ---- Shared state (guarded by g_mutex) ------------------------------------------
enum class CmdType : uint8_t { Discover, Connect, Forget };
struct Command {
  CmdType     type;
  bool        scannersOnly;
  ScannerInfo target;
};

std::mutex               g_mutex;
std::deque<Command>      g_commands;
std::vector<ScannerInfo> g_found;
ScannerStatus            g_status{ScannerState::Idle, "", "", false};
std::atomic<uint32_t>    g_foundRev{0};
std::atomic<bool>        g_connected{false};

QueueHandle_t g_barcodes = nullptr;   // char[BARCODE_MAX] items: BLE -> UI
Preferences   g_prefs;                // flash storage for the saved scanner
NimBLEClient* g_client = nullptr;

// Saved scanner (only touched by the BLE task after begin())
std::string g_savedAddr, g_savedName;
uint8_t     g_savedType = 0;

void setStatus(ScannerState state, const std::string& name, const std::string& addr) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_status.state          = state;
  g_status.name           = name;
  g_status.address        = addr;
  g_status.hasSavedDevice = !g_savedAddr.empty();
}

// ---- Turning bytes into barcodes -------------------------------------------------
char   g_line[BARCODE_MAX];
size_t g_lineLen = 0;

void emitBarcode(const char* text) {
  char item[BARCODE_MAX];
  strlcpy(item, text, sizeof(item));
  xQueueSend(g_barcodes, item, 0);     // never block the Bluetooth stack
}

// Enter / newline / tab ends a barcode; anything printable is collected.
void pushChar(char c) {
  if (c == '\r' || c == '\n' || c == '\t') {
    if (g_lineLen > 0) {
      g_line[g_lineLen] = '\0';
      emitBarcode(g_line);
      g_lineLen = 0;
    }
  } else if (g_lineLen < BARCODE_MAX - 1 && isprint(static_cast<unsigned char>(c))) {
    g_line[g_lineLen++] = c;
  }
}

// Serial-style scanners send plain ASCII text.
void onSerialData(NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
  for (size_t i = 0; i < len; ++i) pushChar(static_cast<char>(data[i]));
}

// HID scanners send keyboard reports: [modifiers, reserved, key1..key6].
char hidToAscii(uint8_t key, bool shift) {
  if (key >= 0x04 && key <= 0x1D) return static_cast<char>((shift ? 'A' : 'a') + (key - 0x04));
  if (key >= 0x1E && key <= 0x26) return shift ? "!@#$%^&*("[key - 0x1E] : static_cast<char>('1' + (key - 0x1E));
  switch (key) {
    case 0x27: return shift ? ')' : '0';
    case 0x28: return '\n';                       // Enter
    case 0x2B: return '\t';                       // Tab
    case 0x2C: return ' ';
    case 0x2D: return shift ? '_' : '-';
    case 0x2E: return shift ? '+' : '=';
    case 0x36: return shift ? '<' : ',';
    case 0x37: return shift ? '>' : '.';
    case 0x38: return shift ? '?' : '/';
    default:   return 0;
  }
}

uint8_t g_lastKey = 0;
void onHidReport(NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
  if (len < 3) return;
  const size_t  off   = (len == 9) ? 1 : 0;       // some scanners prepend a report ID
  const bool    shift = data[off] & 0x22;         // left or right shift
  const uint8_t key   = data[off + 2];
  if (key != 0 && key != g_lastKey) {             // new key down (key-up reports are all zeros)
    const char c = hidToAscii(key, shift);
    if (c) pushChar(c);
  }
  g_lastKey = key;
}

// ---- Connection ------------------------------------------------------------------
class ClientCallbacks : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient* client, int reason) override {
    g_connected = false;
    Serial.printf("[scanner] disconnected (reason %d)\n", reason);
    setStatus(ScannerState::Idle, g_savedName, g_savedAddr);
  }
} g_clientCallbacks;

bool subscribeHid(NimBLEClient* client) {
  NimBLERemoteService* hid = client->getService(NimBLEUUID(static_cast<uint16_t>(0x1812)));
  if (!hid) return false;
  client->secureConnection();                     // HID data needs an encrypted link
  bool any = false;
  for (NimBLERemoteCharacteristic* chr : hid->getCharacteristics(true)) {
    if (chr->getUUID() == NimBLEUUID(static_cast<uint16_t>(0x2A4D)) && chr->canNotify())
      any |= chr->subscribe(true, onHidReport);
  }
  return any;
}

bool isStandardService(const NimBLEUUID& uuid) {  // device info, battery etc. never carry barcodes
  for (uint16_t id : {0x1800, 0x1801, 0x180A, 0x180F})
    if (uuid == NimBLEUUID(id)) return true;
  return false;
}

bool subscribeSerial(NimBLEClient* client) {
  bool any = false;
  for (NimBLERemoteService* svc : client->getServices(true)) {
    if (isStandardService(svc->getUUID())) continue;
    for (NimBLERemoteCharacteristic* chr : svc->getCharacteristics(true)) {
      if (chr->canNotify() || chr->canIndicate()) any |= chr->subscribe(chr->canNotify(), onSerialData);
    }
  }
  return any;
}

bool connectTo(const std::string& address, uint8_t type, const std::string& name) {
  setStatus(ScannerState::Connecting, name, address);
  if (!g_client) {
    g_client = NimBLEDevice::createClient();
    g_client->setClientCallbacks(&g_clientCallbacks, false);
    g_client->setConnectTimeout(5000);
  }
  if (g_client->isConnected()) {
    g_client->disconnect();
    for (int i = 0; i < 20 && g_client->isConnected(); ++i) vTaskDelay(pdMS_TO_TICKS(50));
  }

  if (!g_client->connect(NimBLEAddress(address, type))) {
    setStatus(ScannerState::Idle, g_savedName, g_savedAddr);
    return false;
  }
  const bool hasHid = g_client->getService(NimBLEUUID(static_cast<uint16_t>(0x1812))) != nullptr;
  const bool ok     = hasHid ? subscribeHid(g_client) : subscribeSerial(g_client);
  if (!ok) {
    Serial.println("[scanner] connected, but found nothing that sends data");
    g_client->disconnect();
    setStatus(ScannerState::Idle, g_savedName, g_savedAddr);
    return false;
  }
  g_lineLen = 0;
  g_lastKey = 0;
  g_connected = true;
  setStatus(ScannerState::Connected, name, address);
  Serial.printf("[scanner] connected to %s (%s mode)\n", name.c_str(), hasHid ? "HID" : "serial");
  return true;
}

void saveDevice(const ScannerInfo& d) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_savedAddr = d.address;
    g_savedName = d.name;
    g_savedType = d.addressType;
  }
  g_prefs.putString("addr", d.address.c_str());
  g_prefs.putString("name", d.name.c_str());
  g_prefs.putUChar("type", d.addressType);
}

// ---- Discovery -------------------------------------------------------------------
bool looksLikeScanner(const NimBLEAdvertisedDevice* d) {
  if (d->isAdvertisingService(NimBLEUUID(static_cast<uint16_t>(0x1812)))) return true;   // HID
  if (d->haveAppearance()) {
    const uint16_t a = d->getAppearance();
    if (a == 0x03C1 || a == 0x03C8) return true;  // keyboard, barcode scanner
  }
  std::string n = d->getName();
  std::transform(n.begin(), n.end(), n.begin(), ::tolower);
  for (const char* kw : {"scan", "barcode", "netum", "eyoyo", "inateck", "tera", "symcode", "zebra", "honeywell"})
    if (n.find(kw) != std::string::npos) return true;
  return false;
}

void runDiscovery(bool scannersOnly) {
  const ScannerState prev = g_connected ? ScannerState::Connected : ScannerState::Idle;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_status.state = ScannerState::Discovering;
  }
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);                      // ask for names, not just addresses
  NimBLEScanResults results = scan->getResults(DISCOVERY_DURATION_MS, false);

  std::vector<ScannerInfo> list;
  for (int i = 0; i < results.getCount(); ++i) {
    const NimBLEAdvertisedDevice* d = results.getDevice(i);
    if (scannersOnly && !looksLikeScanner(d)) continue;
    ScannerInfo info;
    info.name        = d->getName().empty() ? "(no name)" : d->getName();
    info.address     = d->getAddress().toString();
    info.addressType = d->getAddress().getType();
    info.rssi        = d->getRSSI();
    list.push_back(info);
  }
  std::sort(list.begin(), list.end(),
            [](const ScannerInfo& a, const ScannerInfo& b) { return a.rssi > b.rssi; });
  scan->clearResults();

  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_found        = std::move(list);
    g_status.state = prev;
  }
  ++g_foundRev;
}

// ---- The background task ---------------------------------------------------------
bool popCommand(Command& out) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_commands.empty()) return false;
  out = g_commands.front();
  g_commands.pop_front();
  return true;
}

void pushCommand(const Command& cmd) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_commands.push_back(cmd);
}

void bleTask(void*) {
  uint32_t lastAttemptMs = 0;
  for (;;) {
    Command cmd;
    if (popCommand(cmd)) {
      switch (cmd.type) {
        case CmdType::Discover:
          runDiscovery(cmd.scannersOnly);
          break;
        case CmdType::Connect:
          saveDevice(cmd.target);
          connectTo(cmd.target.address, cmd.target.addressType, cmd.target.name);
          lastAttemptMs = millis();
          break;
        case CmdType::Forget:
          if (g_client && g_client->isConnected()) g_client->disconnect();
          g_prefs.clear();
          NimBLEDevice::deleteAllBonds();
          {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_savedAddr.clear();
            g_savedName.clear();
          }
          setStatus(ScannerState::Idle, "", "");
          break;
      }
    } else if (!g_connected && !g_savedAddr.empty() && millis() - lastAttemptMs >= SCANNER_RETRY_MS) {
      lastAttemptMs = millis();                   // saved scanner asleep or out of range? keep trying
      connectTo(g_savedAddr, g_savedType, g_savedName);
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
}  // namespace

// ---- Public API --------------------------------------------------------------------
void Scanner::begin() {
  g_barcodes = xQueueCreate(16, BARCODE_MAX);
  g_prefs.begin("scanner", false);
  g_savedAddr = g_prefs.getString("addr", "").c_str();
  g_savedName = g_prefs.getString("name", "").c_str();
  g_savedType = g_prefs.getUChar("type", 0);

  NimBLEDevice::init("CheckinStation");
  NimBLEDevice::setSecurityAuth(true, false, true);          // bond, no PIN, secure connections
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  setStatus(ScannerState::Idle, g_savedName, g_savedAddr);
  xTaskCreatePinnedToCore(bleTask, "scanner", 8192, nullptr, 1, nullptr, 0);
}

void Scanner::discover(bool scannersOnly) { pushCommand({CmdType::Discover, scannersOnly, {}}); }

std::vector<ScannerInfo> Scanner::found() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_found;
}

uint32_t Scanner::foundRevision() { return g_foundRev.load(); }

void Scanner::connect(const ScannerInfo& device) { pushCommand({CmdType::Connect, false, device}); }

void Scanner::forget() { pushCommand({CmdType::Forget, false, {}}); }

bool Scanner::nextBarcode(std::string& out) {
  char item[BARCODE_MAX];
  if (!g_barcodes || xQueueReceive(g_barcodes, item, 0) != pdTRUE) return false;
  out = item;
  return true;
}

void Scanner::simulateScan(const std::string& code) {
  if (g_barcodes) emitBarcode(code.c_str());
}

ScannerStatus Scanner::status() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_status;
}
