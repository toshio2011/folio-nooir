#pragma once

// The host simulator does not create ESP-IDF STA/AP netifs. Keep the app's
// defensive netif-state checks available without substituting a real network.
struct esp_netif_t;

inline esp_netif_t* esp_netif_get_handle_from_ifkey(const char*) {
  return nullptr;
}
