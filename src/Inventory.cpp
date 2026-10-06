#include "Inventory.h"
#include "Cloud.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <esp_rom_crc.h>
#include <atomic>
#include <mutex>

namespace {
struct RosterClass {
  std::string              name;
  std::vector<std::string> people;
};

constexpr const char* CACHE_PATH = "/inventory.json";

std::mutex               g_mutex;
std::vector<RosterClass> g_roster;
std::vector<Item>        g_items;
std::atomic<uint32_t>    g_revision{0};
uint32_t                 g_cacheCrc = 0;

uint32_t crcOf(const String& s) {
  return esp_rom_crc32_le(0, reinterpret_cast<const uint8_t*>(s.c_str()), s.length());
}

// Parse a snapshot from Apps Script:
// {"ok":true,"roster":[{"class":"Period 1","people":["Ava"]}],
//  "items":[{"barcode":"0123","name":"TI-84 #1","out":false,"holder":""}]}
bool parseSnapshot(const String& json) {
  JsonDocument doc;
  if (deserializeJson(doc, json) || !(doc["ok"] | false)) return false;

  std::vector<RosterClass> roster;
  for (JsonObject c : doc["roster"].as<JsonArray>()) {
    RosterClass rc;
    rc.name = c["class"] | "";
    for (JsonVariant p : c["people"].as<JsonArray>()) rc.people.push_back(p | "");
    roster.push_back(std::move(rc));
  }

  std::vector<Item> items;
  for (JsonObject it : doc["items"].as<JsonArray>()) {
    Item item;
    item.barcode    = it["barcode"] | "";
    item.name       = it["name"] | "";
    item.checkedOut = it["out"] | false;
    item.holder     = it["holder"] | "";
    if (!item.barcode.empty()) items.push_back(std::move(item));
  }

  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_roster = std::move(roster);
    g_items  = std::move(items);
  }
  ++g_revision;
  return true;
}

void saveCache(const String& json) {
  const uint32_t crc = crcOf(json);
  if (crc == g_cacheCrc) return;                 // unchanged, skip the flash write
  File f = LittleFS.open(CACHE_PATH, "w");
  if (!f) return;
  f.print(json);
  f.close();
  g_cacheCrc = crc;
}

void loadDemoData() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_roster = {{"Period 1", {"Ava", "Jordan", "Malik"}}, {"Period 2", {"Chris", "Dana"}}};
  g_items  = {{"012345678905", "TI-84 #1", false, ""},
              {"012345678912", "TI-84 #2", false, ""},
              {"036000291452", "Chromebook 7", false, ""}};
}

Item* findLocked(const std::string& barcode) {  // caller must hold g_mutex
  for (Item& it : g_items)
    if (it.barcode == barcode) return &it;
  return nullptr;
}
}  // namespace

void Inventory::begin() {
  File f = LittleFS.open(CACHE_PATH, "r");
  if (f) {
    const String json = f.readString();
    f.close();
    if (parseSnapshot(json)) {
      g_cacheCrc = crcOf(json);
      Serial.println("[inventory] loaded cached data");
      return;
    }
  }
  loadDemoData();
  ++g_revision;
  Serial.println("[inventory] no cache yet, using demo data until the first sync");
}

bool Inventory::applySnapshot(const String& json) {
  if (!parseSnapshot(json)) return false;
  saveCache(json);
  return true;
}

std::vector<std::string> Inventory::classes() {
  std::lock_guard<std::mutex> lock(g_mutex);
  std::vector<std::string> out;
  for (const RosterClass& c : g_roster) out.push_back(c.name);
  return out;
}

std::vector<std::string> Inventory::people(const std::string& className) {
  std::lock_guard<std::mutex> lock(g_mutex);
  for (const RosterClass& c : g_roster)
    if (c.name == className) return c.people;
  return {};
}

bool Inventory::findItem(const std::string& barcode, Item& out) {
  std::lock_guard<std::mutex> lock(g_mutex);
  const Item* it = findLocked(barcode);
  if (!it) return false;
  out = *it;
  return true;
}

std::vector<Item> Inventory::availableItems() {
  std::lock_guard<std::mutex> lock(g_mutex);
  std::vector<Item> out;
  for (const Item& it : g_items)
    if (!it.checkedOut) out.push_back(it);
  return out;
}

std::vector<Item> Inventory::checkedOutItems() {
  std::lock_guard<std::mutex> lock(g_mutex);
  std::vector<Item> out;
  for (const Item& it : g_items)
    if (it.checkedOut) out.push_back(it);
  return out;
}

bool Inventory::checkIn(const std::string& barcode, const std::string& className, const std::string& person) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (Item* it = findLocked(barcode)) {
      it->checkedOut = false;
      it->holder.clear();
    } else {
      g_items.push_back({barcode, "Unknown item", false, ""});  // Sheets adds it too; rename it there
    }
  }
  ++g_revision;
  Cloud::sendEvent("checkin", barcode, className, person);
  return true;
}

bool Inventory::checkOut(const std::string& barcode, const std::string& className, const std::string& person) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    Item* it = findLocked(barcode);
    if (!it || it->checkedOut) return false;
    it->checkedOut = true;
    it->holder     = person + " (" + className + ")";
  }
  ++g_revision;
  Cloud::sendEvent("checkout", barcode, className, person);
  return true;
}

uint32_t Inventory::revision() { return g_revision.load(); }
