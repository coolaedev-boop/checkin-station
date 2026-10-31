#include "Session.h"
#include "Inventory.h"
#include <algorithm>

namespace Session {
namespace {
bool                     g_active = false;
Who                      g_who;
std::vector<std::string> g_cart;
std::vector<std::string> g_returns;

bool contains(const std::vector<std::string>& list, const std::string& s) {
  return std::find(list.begin(), list.end(), s) != list.end();
}
}  // namespace

bool       active() { return g_active; }
const Who& who() { return g_who; }

void signOn(const Who& w) {
  g_who    = w;
  g_active = true;
  g_cart.clear();
  g_returns.clear();
}

void signOut() {
  g_active = false;
  g_who    = Who{};
  g_cart.clear();
  g_returns.clear();
}

AddResult addToCart(const std::string& barcode) {
  if (contains(g_cart, barcode)) return AddResult::AlreadyInCart;
  Item item;
  if (!Inventory::findItem(barcode, item)) return AddResult::Unknown;
  if (item.checkedOut) return AddResult::CheckedOut;
  g_cart.push_back(barcode);
  return AddResult::Added;
}

bool removeFromCart(const std::string& barcode) {
  const auto it = std::find(g_cart.begin(), g_cart.end(), barcode);
  if (it == g_cart.end()) return false;
  g_cart.erase(it);
  return true;
}

const std::vector<std::string>& cart() { return g_cart; }

size_t pruneCart() {
  const size_t before = g_cart.size();
  g_cart.erase(std::remove_if(g_cart.begin(), g_cart.end(),
                              [](const std::string& code) {
                                Item item;
                                return !Inventory::findItem(code, item) || item.checkedOut;
                              }),
               g_cart.end());
  return before - g_cart.size();
}

Result checkOutCart() {
  Result r;
  for (const std::string& code : g_cart) {
    if (Inventory::checkOut(code, g_who.cls, g_who.name, g_who.project)) ++r.done;
    else                                                                  ++r.failed;
  }
  g_cart.clear();
  return r;
}

bool queueReturn(const std::string& barcode) {
  if (contains(g_returns, barcode)) return false;
  g_returns.push_back(barcode);
  return true;
}

const std::vector<std::string>& returns() { return g_returns; }

Result checkInReturns() {
  Result r;
  for (const std::string& code : g_returns)
    if (Inventory::checkIn(code, g_who.cls, g_who.name, g_who.project)) ++r.done;
  g_returns.clear();
  return r;
}
}  // namespace Session
