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
std::vector<std::string> g_projects;
std::vector<Item>        g_items;
std::atomic<uint32_t>    g_revision{0};
uint32_t                 g_cacheCrc = 0;
uint32_t                 g_snapshotRev = UINT32_MAX;  // g_revision right after the last parse; differs once check-ins/outs change data

uint32_t crcOf(const String& s) {
  return esp_rom_crc32_le(0, reinterpret_cast<const uint8_t*>(s.c_str()), s.length());
}

// Parse a snapshot from Apps Script:
// {"ok":true,"roster":[{"class":"Period 1","people":["Ava"]}],"projects":["Film Club"],
//  "items":[{"barcode":"0123","name":"TI-84 #1","out":false,"holder":"","desc":"Calculator, TI-84 Plus"}]}
// "projects" and "desc" are newer; a snapshot or cache without them still parses.
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

  std::vector<std::string> projects;
  for (JsonVariant p : doc["projects"].as<JsonArray>()) {
    std::string name = p | "";
    if (!name.empty()) projects.push_back(std::move(name));
  }

  std::vector<Item> items;
  for (JsonObject it : doc["items"].as<JsonArray>()) {
    Item item;
    item.barcode    = it["barcode"] | "";
    item.name       = it["name"] | "";
    item.checkedOut = it["out"] | false;
    item.holder     = it["holder"] | "";
    item.description = it["desc"] | "";
    if (!item.barcode.empty()) items.push_back(std::move(item));
  }

  {
    // Swap, so the old data ends up in the locals and is freed after the lock is released;
    // freeing a big item list while holding it would stall the UI's getters.
    std::lock_guard<std::mutex> lock(g_mutex);
    g_roster.swap(roster);
    g_projects.swap(projects);
    g_items.swap(items);
  }
  g_snapshotRev = ++g_revision;
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
  g_roster   = {{"2A", {"Ava", "Jordan", "Malik"}}, {"2B", {"Chris", "Dana"}}};
  g_projects = {"Morning Show", "Film Club"};
  g_items    = {{"012345678905", "TI-84 #1", false, "", "Calculator, TI-84 Plus"},
                {"012345678912", "TI-84 #2", false, "", "Calculator, TI-84 Plus"},
                {"036000291452", "Chromebook 7", false, "", "Laptop, Lenovo 100e"}};
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
  // Same bytes as the cache and nothing changed locally since: skip the parse and the
  // revision bump, so the UI doesn't redraw every 5 minutes for no reason. A local check-in
  // or check-out moves the revision, so a snapshot after one is still parsed and corrects it.
  if (g_cacheCrc != 0 && g_revision.load() == g_snapshotRev && crcOf(json) == g_cacheCrc) return true;
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

std::vector<std::string> Inventory::projects() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_projects;
}

bool Inventory::findItem(const std::string& barcode, Item& out) {
  std::lock_guard<std::mutex> lock(g_mutex);
  const Item* it = findLocked(barcode);
  if (!it) return false;
  out = *it;
  return true;
}

bool Inventory::checkIn(const std::string& barcode, const std::string& className, const std::string& person,
                        const std::string& project) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (Item* it = findLocked(barcode)) {
      it->checkedOut = false;
      it->holder.clear();
    } else {
      g_items.push_back({barcode, "Unknown item", false, "", ""});  // Sheets adds it too; rename it there
    }
  }
  ++g_revision;
  Cloud::sendEvent("checkin", barcode, className, person, project);
  return true;
}

bool Inventory::checkOut(const std::string& barcode, const std::string& className, const std::string& person,
                         const std::string& project) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    Item* it = findLocked(barcode);
    if (!it || it->checkedOut) return false;
    it->checkedOut = true;
    it->holder     = person + " (" + className + ")";
  }
  ++g_revision;
  Cloud::sendEvent("checkout", barcode, className, person, project);
  return true;
}

uint32_t Inventory::revision() { return g_revision.load(); }
