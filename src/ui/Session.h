#pragma once
// ============================================================================
//  Session: who is signed on, what they're about to borrow (the cart), and what
//  they've scanned to return. No LVGL here, so the rules are easy to test.
//
//  Everything is checked in or out under the signed-on Class + Name + Project.
//  Signing out (Log Out, or SESSION_IDLE_MS with no touch or scan) forgets the
//  cart and any returns that weren't checked in yet.
// ============================================================================
#include <string>
#include <vector>

namespace Session {
struct Who {
  std::string cls;
  std::string name;
  std::string project;   // empty when the Sheet has no Projects tab
};

bool       active();
const Who& who();
void       signOn(const Who& who);
void       signOut();

// ---- Cart: items to borrow ----------------------------------------------------
enum class AddResult { Added, AlreadyInCart, CheckedOut, Unknown };
AddResult                       addToCart(const std::string& barcode);   // only items that are in can go in
bool                            removeFromCart(const std::string& barcode);
const std::vector<std::string>& cart();
size_t                          pruneCart();   // drop items a sync says are out now; returns how many

struct Result {
  size_t done   = 0;
  size_t failed = 0;   // check-out only: already out by the time it was confirmed
};
Result checkOutCart();   // borrows everything in the cart, then empties it

// ---- Returns: scanned on Check In with "Check Into Cart" off ------------------
bool                            queueReturn(const std::string& barcode);  // false if already queued
const std::vector<std::string>& returns();
Result                          checkInReturns();                          // checks them all in, then empties
}  // namespace Session
