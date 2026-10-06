#pragma once
// ============================================================================
//  Network: owns Wi-Fi. Joins the saved network (or the default in config.h),
//  keeps it connected, scans for nearby networks, and joins one picked on the
//  System tab. A new network is only saved once it actually connects, so a
//  mistyped password never strands the station offline: it falls back to the
//  network that was working.
// ============================================================================
#include <Arduino.h>
#include <string>
#include <vector>

enum class WifiSecurity : uint8_t {
  Open,         // no password
  Password,     // WPA2/WPA3 personal: one shared password
  Enterprise,   // username + password (many school networks). Not supported
  Legacy,       // WEP or WPA1: too old, the Wi-Fi driver refuses them. Not supported
};

struct WifiNetwork {
  std::string  ssid;
  int8_t       rssi;       // dBm, closer to 0 = stronger
  WifiSecurity security;
};

enum class NetworkState : uint8_t { Offline, Connecting, Connected };

enum class JoinResult : uint8_t {   // outcome of the last network picked on the System tab
  None,           // nothing picked since boot
  Joining,        // trying it now
  Joined,         // connected, and saved for next boot
  WrongPassword,  // the network refused the password (the usual cause of these errors)
  NotFound,       // network not in range
  Failed,         // timed out, or another error
};

struct NetworkStatus {
  NetworkState state;
  std::string  ssid;        // network in use, or being joined
  int8_t       rssi;        // dBm while connected, else 0
  bool         scanning;
  bool         hasSaved;    // a network picked on the System tab is saved (Forget brings back config.h's)
  JoinResult   join;
  std::string  joinSsid;    // network the join result is about
};

namespace Network {
  void begin();                                // load the saved network (or config.h's) and start connecting

  void scan();                                 // search in the background; results via networks()
  std::vector<WifiNetwork> networks();         // last search, strongest first, one entry per name
  uint32_t networksRevision();                 // bumps when networks() changes

  // Try a network. It replaces the saved one only if it connects;
  // otherwise the station goes back to the network it was using.
  void join(const std::string& ssid, const std::string& password);
  void forget();                               // drop the saved network, go back to config.h's

  bool          connected();                   // on Wi-Fi with an IP address
  NetworkStatus status();
}
