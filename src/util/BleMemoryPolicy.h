#pragma once

#include <cstddef>
#include <cstdint>

#if __has_include(<sdkconfig.h>)
#include <sdkconfig.h>
#endif

namespace bleinput {

// Measured candidate for the finished lifecycle: lightweight Reader/UI idle
// baselines are about 99--100 KiB, while contaminated 70--85 KiB states stay
// rejected. BLE is stopped before heavy Reader work; measured connected
// lightweight UI retains high-20s/low-30s KiB. This remains one global cold
// start floor, not an activity-specific exception or a NimBLE minimum.
inline constexpr std::size_t kReaderBleStartMinFreeHeap = 96 * 1024;
inline constexpr std::size_t kReaderBleLogicalMinLargestBlock = 36 * 1024;

// ESP-IDF heap poisoning reports the largest user payload after subtracting
// poison_head_t (uint32_t + size_t) and poison_tail_t (uint32_t). On the 32-bit
// target this is 12 bytes, so translate the logical 36 KiB region floor into
// the exact quantity returned by getMaxAllocHeap().
#if defined(CONFIG_HEAP_POISONING_LIGHT) || defined(CONFIG_HEAP_POISONING_COMPREHENSIVE)
inline constexpr std::size_t kHeapPoisonLargestBlockOverhead = 2 * sizeof(std::uint32_t) + sizeof(std::size_t);
#else
inline constexpr std::size_t kHeapPoisonLargestBlockOverhead = 0;
#endif
inline constexpr std::size_t kReaderBleStartMinLargestBlock =
    kReaderBleLogicalMinLargestBlock - kHeapPoisonLargestBlockOverhead;

// Connected Reader renders need a separate, lower runtime policy than cold BLE
// admission. Physical traces put connected idle around 29--32 KiB free, while a
// cached/light render can consume more than 10 KiB and fragment the largest
// block. Trigger the bounded Reader reclaim before entering that danger band.
// These values are intentionally local to cached/light Reader rendering and do
// not change the existing 98,304/36,852 cold-start floors above.
inline constexpr std::size_t kReaderBleRenderMinFreeHeap = 36 * 1024;
inline constexpr std::size_t kReaderBleRenderMinLargestBlock = 20 * 1024;

constexpr bool readerBleCachedRenderNeedsReclaim(const bool bleEnabled, const bool bleRunning,
                                                 const std::size_t freeHeap, const std::size_t largestBlock) {
  return bleEnabled && bleRunning &&
         (freeHeap < kReaderBleRenderMinFreeHeap || largestBlock < kReaderBleRenderMinLargestBlock);
}

constexpr bool readerBleCachedRenderHeadroomSafe(const std::size_t freeHeap, const std::size_t largestBlock) {
  return freeHeap >= kReaderBleRenderMinFreeHeap && largestBlock >= kReaderBleRenderMinLargestBlock;
}

constexpr bool readerBleStartMemoryAdmitted(const std::size_t freeHeap, const std::size_t largestBlock) {
  return freeHeap >= kReaderBleStartMinFreeHeap && largestBlock >= kReaderBleStartMinLargestBlock;
}

// The Reader-return purge is deliberately narrower than ordinary BLE
// admission. Every lifecycle/cache condition must already be true; this
// helper is kept pure so the one-shot policy can be tested off-device.
constexpr bool readerBleReturnCleanupAllowed(const bool bleEnabled, const bool bleWanted,
                                             const bool returnPending, const bool readerQuiescent,
                                             const bool idleCleanupComplete, const bool bleRunning,
                                             const bool admissionPasses) {
  return bleEnabled && bleWanted && returnPending && readerQuiescent && idleCleanupComplete &&
         !bleRunning && !admissionPasses;
}

}  // namespace bleinput
