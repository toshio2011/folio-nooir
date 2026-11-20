#pragma once

#include <cstddef>

namespace ble_bonds {

// Keep a successfully connected bond first for the next automatic reconnect.
// The array is fixed-capacity and the move preserves every other bond's order.
template <typename Bond>
bool promoteToFront(Bond* bonds, const std::size_t count, const std::size_t index) {
  if (!bonds || index >= count) return false;
  if (index == 0) return false;
  const Bond selected = bonds[index];
  for (std::size_t i = index; i > 0; --i) bonds[i] = bonds[i - 1];
  bonds[0] = selected;
  return true;
}

}  // namespace ble_bonds
