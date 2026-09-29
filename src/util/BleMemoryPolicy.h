#pragma once

#include <cstddef>

namespace bleinput {

// The experimental BLE host consumes about 68--77 KiB of free heap and can
// reduce the largest block by about 12 KiB on the measured X4 runs. Keep the
// existing reader build floor (16 KiB contiguous) plus the observed 8 KiB
// grayscale/BW working pressure after that startup cost. The measured BLE
// free-heap loss is about 77 KiB, so keep a rounded 100 KiB pre-start floor;
// the measured 105 KiB reader-start sample still passes. The resulting
// pre-start largest-block floor is deliberately just above 36 KiB; the measured
// 38.9 KiB reader-start sample passes, while a fragmented heap does not.
inline constexpr std::size_t kReaderBleStartMinFreeHeap = 100 * 1024;
inline constexpr std::size_t kReaderBleStartMinLargestBlock = 36 * 1024;

constexpr bool readerBleStartMemoryAdmitted(const std::size_t freeHeap, const std::size_t largestBlock) {
  return freeHeap >= kReaderBleStartMinFreeHeap && largestBlock >= kReaderBleStartMinLargestBlock;
}

}  // namespace bleinput
