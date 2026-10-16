#pragma once

#include <algorithm>
#include <cstddef>
#include <limits>

// /api/settings uses a fixed 256-byte writer and streams a response page at a
// time. Keep a Web-specific admission reserve for transport and transient
// response costs; these values are not BLE admission floors.
namespace WebSettingsMemoryPolicy {
constexpr size_t kMinFreeHeap = 20 * 1024;
constexpr size_t kMinLargestBlock = 8 * 1024;
constexpr size_t kSafetyReserve = 12 * 1024;
constexpr size_t kObservedPeakMultiplier = 3;

constexpr size_t requiredFreeHeap(const size_t responseBytes) {
  constexpr size_t maxSize = std::numeric_limits<size_t>::max();
  const size_t projectedTransport =
      responseBytes > (maxSize - kSafetyReserve) / kObservedPeakMultiplier
          ? maxSize
          : kSafetyReserve + responseBytes * kObservedPeakMultiplier;
  return std::max(kMinFreeHeap, projectedTransport);
}

constexpr bool admitted(const size_t freeHeap, const size_t largestBlock, const size_t responseBytes) {
  return freeHeap >= requiredFreeHeap(responseBytes) && largestBlock >= kMinLargestBlock;
}
}  // namespace WebSettingsMemoryPolicy
