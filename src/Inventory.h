#pragma once
// ============================================================================
//  Inventory: the station's data. Who's in each class, which items exist,
//  and which are checked out. Thread-safe; every getter returns a copy.
//
//  Source of truth is Google Sheets. Cloud pulls a snapshot every few minutes
//  and hands it to applySnapshot(). The last snapshot is cached in flash so
//  the station boots with data even when Wi-Fi is down.
// ============================================================================
#include <Arduino.h>
#include <string>
#include <vector>

struct Item {
  std::string barcode;
  std::string name;
  bool        checkedOut = false;
  std::string holder;       // "Ava (Period 1)" while checked out, empty otherwise
};

namespace Inventory {
  void begin();                                          // load cached data (or demo data on first boot)
  bool applySnapshot(const String& json);                // replace everything with data from Sheets

  std::vector<std::string> classes();
  std::vector<std::string> people(const std::string& className);
  bool                     findItem(const std::string& barcode, Item& out);
  std::vector<Item>        availableItems();             // checked in, so they can be checked out
  std::vector<Item>        checkedOutItems();

  // Update local state right away and queue the event for upload to Sheets.
  // checkOut() returns false if the item is unknown or already out.
  bool checkIn(const std::string& barcode, const std::string& className, const std::string& person);
  bool checkOut(const std::string& barcode, const std::string& className, const std::string& person);

  uint32_t revision();                                   // bumps on every change; the UI redraws when it moves
}
