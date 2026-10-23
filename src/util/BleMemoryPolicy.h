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

// Recent/Home retains the complete visible book list and redraws covers while
// BLE is otherwise allowed to stay connected. The X4 trace showed only about
// 22 KiB free on the Settings -> RecentBooks transition before the 25-entry
// shelf was constructed. Release the host before that construction whenever
// the incoming Home transition starts below the normal cold-admission floor.
// This is a transition-only guard; it does not poll or retry during rendering.
inline constexpr std::size_t kHomeBleSuspendMinFreeHeap = 96 * 1024;

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

// Physical Reader traces show a small admission cliff around 97--98 KiB free
// even when the heap is still one large, healthy region (about 59--65 KiB).
// Permit only a 2 KiB soft margin, and only at a confirmed quiescent EPUB
// return boundary. The strong 56 KiB logical largest-block floor rejects
// fragmented heaps. These do not change the global cold-start floors above.
inline constexpr std::size_t kReaderBleQuiescentReturnFreeHeapMargin = 2 * 1024;
inline constexpr std::size_t kReaderBleQuiescentReturnMinFreeHeap =
    kReaderBleStartMinFreeHeap - kReaderBleQuiescentReturnFreeHeapMargin;
inline constexpr std::size_t kReaderBleQuiescentReturnLogicalMinLargestBlock = 56 * 1024;
inline constexpr std::size_t kReaderBleQuiescentReturnMinLargestBlock =
    kReaderBleQuiescentReturnLogicalMinLargestBlock - kHeapPoisonLargestBlockOverhead;

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

constexpr bool lifecycleReevaluationAllowed(const bool bluetoothEnabled, const bool webExclusive,
                                             const bool noDeviceTimeoutStoppedHost) {
  return bluetoothEnabled && !webExclusive && !noDeviceTimeoutStoppedHost;
}

constexpr bool homeTransitionNeedsBleSuspension(const bool bleRunning, const bool enteringRecentBooks,
                                                const std::size_t freeHeap) {
  return bleRunning && enteringRecentBooks && freeHeap < kHomeBleSuspendMinFreeHeap;
}

// Any render-lock or queued/in-flight render blocks BLE admission while a
// Reader is current or on the activity stack. A lightweight child activity
// does not waive the Reader's pending render boundary.
constexpr bool readerBleAdmissionRenderWorkSafe(const bool readerContext, const bool renderWorkPending) {
  return !readerContext || !renderWorkPending;
}

// `quiescentEpubReturn` must come from the activity/lifecycle gate: the current
// activity is EpubReader, its pending Reader return is enabled, rendering is
// idle, and no build or resource-sensitive work is active. Entry, settings,
// other Reader formats, and all heavy render/build/image phases use the strict
// global policy above.
constexpr bool readerBleStartMemoryAdmittedForQuiescentEpubReturn(
    const std::size_t freeHeap, const std::size_t largestBlock, const bool quiescentEpubReturn) {
  return readerBleStartMemoryAdmitted(freeHeap, largestBlock) ||
         (quiescentEpubReturn && freeHeap >= kReaderBleQuiescentReturnMinFreeHeap &&
          largestBlock >= kReaderBleQuiescentReturnMinLargestBlock);
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

// The final shared Reader font-cache release is more disruptive than the
// earlier disposable-cache releases. Keep its quiescence checks together so
// the whole gate can be tested without ESP runtime state.
constexpr bool readerBleDeepCacheReclaimSafe(const bool returnPending, const bool readerQuiescent,
                                             const bool sectionBuildIdle, const bool renderStable,
                                             const bool renderLockFree, const bool renderWorkIdle,
                                             const bool stopRequestClear, const bool replayHoldClear,
                                             const bool notWaitingForBleStop) {
  return returnPending && readerQuiescent && sectionBuildIdle && renderStable && renderLockFree &&
         renderWorkIdle && stopRequestClear && replayHoldClear && notWaitingForBleStop;
}

// Explicit heavy-boundary signals remain useful, but an admission miss at an
// otherwise safe return boundary also makes the final rebuildable-cache tier
// eligible. The release still requires every safety gate and failed admission.
constexpr bool readerBleDeepCacheReclaimEligible(const bool explicitRescue,
                                                 const bool admissionNeeded) {
  return explicitRescue || admissionNeeded;
}

constexpr bool readerBleDeepCacheReclaimAllowed(const bool eligible, const bool deepSafe,
                                                const bool admissionNeeded) {
  return eligible && deepSafe && admissionNeeded;
}

}  // namespace bleinput
