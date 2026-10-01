#pragma once

#include <cstddef>

namespace bleinput {

// Measured candidate for the finished lifecycle: lightweight Reader/UI idle
// baselines are about 99--100 KiB, while contaminated 70--85 KiB states stay
// rejected. BLE is stopped before heavy Reader work; measured connected
// lightweight UI retains high-20s/low-30s KiB. This remains one global cold
// start floor, not an activity-specific exception or a NimBLE minimum.
inline constexpr std::size_t kReaderBleStartMinFreeHeap = 96 * 1024;
inline constexpr std::size_t kReaderBleStartMinLargestBlock = 36 * 1024;

constexpr bool readerBleStartMemoryAdmitted(const std::size_t freeHeap, const std::size_t largestBlock) {
  return freeHeap >= kReaderBleStartMinFreeHeap && largestBlock >= kReaderBleStartMinLargestBlock;
}

}  // namespace bleinput
