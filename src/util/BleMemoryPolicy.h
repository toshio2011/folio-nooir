#pragma once

#include <cstddef>
#include <cstdint>

#ifndef NOOIR_EPUB_BLE_PAGE_AWARE_EXPERIMENT
#define NOOIR_EPUB_BLE_PAGE_AWARE_EXPERIMENT 0
#endif

#ifndef NOOIR_EPUB_BLE_IDLE_ADMISSION_DIAGNOSTIC
#define NOOIR_EPUB_BLE_IDLE_ADMISSION_DIAGNOSTIC 0
#endif

#if __has_include(<sdkconfig.h>)
#include <sdkconfig.h>
#endif

namespace bleinput {

// Increment when the Reader's projected-demand admission gate changes so a
// physical serial capture can identify which readmission policy produced it.
inline constexpr uint8_t kReaderBleReadmissionPolicyVersion = 2;

// Distinguish a host that has never initialized this boot from a NimBLE host
// that was initialized and later stopped to return memory to Reader work.
constexpr const char* bleRuntimeStateName(const bool enabled, const bool running, const bool everStarted) {
  if (running) return "running";
  if (enabled) return everStarted ? "stopped_after_use" : "enabled_not_started";
  return everStarted ? "disabled_after_use" : "runtime_off";
}

// Measured candidate for the finished lifecycle: lightweight Reader/UI idle
// baselines are about 99--100 KiB, while contaminated 70--85 KiB states stay
// rejected. BLE is stopped before heavy Reader work; measured connected
// lightweight UI retains high-20s/low-30s KiB. This remains one global cold
// start floor, not an activity-specific exception or a NimBLE minimum.
inline constexpr std::size_t kReaderBleStartMinFreeHeap = 96 * 1024;
// XTC/XTCH are streamed one page at a time and do not own the EPUB section,
// font-prewarm, or image-layout working set. The supplied X4 run was denied at
// 98,160 B despite a 65,524 B largest block; the normal 98,304 B floor is a
// universal cold-start floor, not an XTC rendering requirement. Relax only the observed 144-byte miss and retain the same contiguous-block
// requirement. This is an isolated experiment, not evidence that lower XTC
// memory levels are safe.
inline constexpr std::size_t kXtcBleStartMinFreeHeap = kReaderBleStartMinFreeHeap - 144;
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

// A cold BLE start can be delayed at Home by a small amount of reconstructible
// UI state. Allow the caller to reclaim that state once, and only when both
// heap measures are near their existing floors and the screen is quiescent.
// This does not weaken the admission floors or apply to Reader work.
inline constexpr std::size_t kHomeBleReclaimMargin = 8 * 1024;

constexpr bool homeBleStartReclaimAllowed(const bool bluetoothEnabled, const bool webExclusive,
                                          const bool wifiActive, const bool readerContext,
                                          const bool renderBusy, const std::size_t freeHeap,
                                          const std::size_t largestBlock,
                                          const bool reclaimAlreadyAttempted = false) {
  const bool belowFloor = freeHeap < kReaderBleStartMinFreeHeap ||
                          largestBlock < kReaderBleStartMinLargestBlock;
  const bool nearBothFloors =
      freeHeap >= kReaderBleStartMinFreeHeap - kHomeBleReclaimMargin &&
      largestBlock >= kReaderBleStartMinLargestBlock - kHomeBleReclaimMargin;
  return bluetoothEnabled && !webExclusive && !wifiActive && !readerContext && !renderBusy &&
         !reclaimAlreadyAttempted && belowFloor && nearBothFloors;
}

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

// A quiescent Reader return is only useful if the host can start and leave
// room for the next measured page render. X4 captures show NimBLE start uses
// about 66-69 KiB and reduces the largest block by about 36 KiB. Round the
// total reserve upward; retain the existing connected-render floors and add
// demand observed in this Reader session. These estimates gate readmission
// only; they do not change cold Home/Settings admission or render safeguards.
inline constexpr std::size_t kReaderBleHostStartFreeReserve = 70 * 1024;
inline constexpr std::size_t kReaderBleHostStartLargestReserve = 36 * 1024;
// The isolated EPUB cached-page experiment keeps the existing 32 KiB / 16 KiB
// recovery floors and adds a small measured-operation margin. Unknown work
// continues to use the stricter book-wide predictor above.
inline constexpr std::size_t kReaderBleEpubPageAwareRecoveryFreeFloor = 32 * 1024;
inline constexpr std::size_t kReaderBleEpubPageAwareRecoveryLargestFloor = 16 * 1024;
inline constexpr std::size_t kReaderBleEpubPageAwareFreeMargin = 4 * 1024;
inline constexpr std::size_t kReaderBleEpubPageAwareLargestMargin = 4 * 1024;

// Compile-gated sequential experiment: admission is for a stable displayed EPUB
// page and HID reception, not for a future render that will be preceded by a
// confirmed BLE teardown. The largest-block requirements are phase-specific:
// use the existing cold-start floor before begin(), then enforce the recovery
// floor after host startup and while BLE is running. Adding the pre-start and
// post-start block floors double-counts two different heap snapshots and can
// reject a start that the existing post-start guard would safely roll back.
// Keep the free-heap reserve additive because it represents total bytes needed
// across startup plus the post-start reserve. The experiment activates
// automatically after a stable page; post-start floors and confirmed shutdown
// remain mandatory. This does not replace the simultaneous-render predictor.
inline constexpr std::size_t kReaderBleEpubIdleOnlyRequiredFree =
    kReaderBleHostStartFreeReserve + kReaderBleEpubPageAwareRecoveryFreeFloor;
inline constexpr std::size_t kReaderBleEpubIdleOnlyRequiredLargest =
    kReaderBleStartMinLargestBlock;

constexpr bool readerBleEpubIdleOnlyPostStartSafe(const std::size_t freeHeap,
                                                   const std::size_t largestBlock) {
  return freeHeap >= kReaderBleEpubPageAwareRecoveryFreeFloor &&
         largestBlock >= kReaderBleEpubPageAwareRecoveryLargestFloor;
}

constexpr bool readerBleEpubIdleOnlyAdmissionAdmitted(const std::size_t freeHeap,
                                                       const std::size_t largestBlock,
                                                       const bool experimentActive,
                                                       const bool stableIdlePage) {
  return NOOIR_EPUB_BLE_IDLE_ADMISSION_DIAGNOSTIC && experimentActive && stableIdlePage &&
         freeHeap >= kReaderBleEpubIdleOnlyRequiredFree &&
         largestBlock >= kReaderBleEpubIdleOnlyRequiredLargest;
}

constexpr std::size_t readerBleSaturatingAdd(const std::size_t left, const std::size_t right) {
  return left > static_cast<std::size_t>(-1) - right ? static_cast<std::size_t>(-1) : left + right;
}

// Connected Reader renders need a separate, lower runtime policy than cold BLE
// admission. Physical traces put connected idle around 29--32 KiB free, while a
// cached/light render can consume more than 10 KiB and fragment the largest
// block. Trigger the bounded Reader reclaim before entering that danger band.
// These values are intentionally local to cached/light Reader rendering and do
// not change the existing 98,304/36,852 cold-start floors above.
inline constexpr std::size_t kReaderBleRenderMinFreeHeap = 36 * 1024;
inline constexpr std::size_t kReaderBleRenderMinLargestBlock = 20 * 1024;

// A Reader render that has just forced a survival stop should not immediately
// reconnect into the same measured pressure cycle. Delay only the automatic
// quiescent return, grow the delay for repeated stops, and cap it so BLE can
// recover without waiting for a book exit.
inline constexpr uint32_t kReaderBleSurvivalBackoffInitialMs = 15'000;
inline constexpr uint32_t kReaderBleSurvivalBackoffMaxMs = 120'000;
inline constexpr uint32_t kReaderBleSurvivalBackoffResetMs = 60'000;
inline constexpr uint8_t kReaderBleSurvivalBackoffMaxStops = 4;
inline constexpr uint8_t kReaderBleSurvivalBackoffSafeRenderResetCount = 3;

// Keep a separate book-lifetime history so a sequence of real render-survival
// stops cannot be erased by the bounded cooldown or a few easy pages.
inline constexpr uint8_t kReaderBleBookSurvivalStopLimit = 3;

constexpr uint8_t nextReaderBleBookSurvivalStopCount(const uint8_t currentStopCount) {
  return currentStopCount < kReaderBleBookSurvivalStopLimit ? currentStopCount + 1
                                                            : kReaderBleBookSurvivalStopLimit;
}

constexpr bool readerBleBookSurvivalReadmissionBlocked(const uint8_t bookStopCount) {
  return bookStopCount >= kReaderBleBookSurvivalStopLimit;
}

constexpr uint32_t readerBleSurvivalBackoffMs(const uint8_t recentStopCount,
                                              const uint32_t sincePreviousStopMs) {
  if (recentStopCount == 0 || sincePreviousStopMs >= kReaderBleSurvivalBackoffResetMs) {
    return kReaderBleSurvivalBackoffInitialMs;
  }
  uint32_t delay = kReaderBleSurvivalBackoffInitialMs;
  const uint8_t cappedCount = recentStopCount > kReaderBleSurvivalBackoffMaxStops
                                  ? kReaderBleSurvivalBackoffMaxStops
                                  : recentStopCount;
  for (uint8_t stop = 1; stop < cappedCount; ++stop) {
    delay = delay >= kReaderBleSurvivalBackoffMaxMs / 2
                ? kReaderBleSurvivalBackoffMaxMs
                : delay * 2;
  }
  return delay;
}

constexpr bool readerBleCachedRenderNeedsReclaim(const bool bleEnabled, const bool bleRunning,
                                                 const std::size_t freeHeap, const std::size_t largestBlock) {
  return bleEnabled && bleRunning &&
         (freeHeap < kReaderBleRenderMinFreeHeap || largestBlock < kReaderBleRenderMinLargestBlock);
}

constexpr bool readerBleCachedRenderHeadroomSafe(const std::size_t freeHeap, const std::size_t largestBlock) {
  return freeHeap >= kReaderBleRenderMinFreeHeap && largestBlock >= kReaderBleRenderMinLargestBlock;
}

constexpr bool readerBleSafeRenderResetsSurvivalBackoff(const uint8_t consecutiveSafeRenders,
                                                        const std::size_t currentFreeHeap,
                                                        const std::size_t largestBlock,
                                                        const std::size_t recentRenderMinFreeHeap) {
  return consecutiveSafeRenders >= kReaderBleSurvivalBackoffSafeRenderResetCount &&
         readerBleCachedRenderHeadroomSafe(currentFreeHeap, largestBlock) &&
         recentRenderMinFreeHeap >= kReaderBleRenderMinFreeHeap;
}

constexpr bool readerBleStartMemoryAdmitted(const std::size_t freeHeap, const std::size_t largestBlock) {
  return freeHeap >= kReaderBleStartMinFreeHeap && largestBlock >= kReaderBleStartMinLargestBlock;
}

constexpr bool readerBleXtcStartMemoryAdmitted(const std::size_t freeHeap, const std::size_t largestBlock) {
  return freeHeap >= kXtcBleStartMinFreeHeap && largestBlock >= kReaderBleStartMinLargestBlock;
}

constexpr bool lifecycleReevaluationAllowed(const bool bluetoothEnabled, const bool webExclusive,
                                             const bool noDeviceTimeoutStoppedHost) {
  return bluetoothEnabled && !webExclusive && !noDeviceTimeoutStoppedHost;
}

// A connected host stays alive across ordinary UI transitions. It yields only
// when the incoming activity explicitly declares a resource-sensitive phase.
constexpr bool activityTransitionNeedsBleSuspension(const bool bleRunning, const bool resourceSensitive) {
  return bleRunning && resourceSensitive;
}

// Any render-lock or queued/in-flight render blocks BLE admission while a
// Reader is current or on the activity stack. A lightweight child activity
// does not waive the Reader's pending render boundary.
constexpr bool readerBleAdmissionRenderWorkSafe(const bool readerContext, const bool renderWorkPending) {
  return !readerContext || !renderWorkPending;
}

// A cold host start waits until the first Home frame after a Reader
// transition. Existing connections are handled before this cold-start gate.
constexpr bool homeBleColdStartRenderWorkSafe(const bool homeContext, const bool renderWorkPending) {
  return !homeContext || !renderWorkPending;
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

// A return after a Reader resource stop predicts both NimBLE's measured
// initialization cost and the strongest page-render demand observed so far.
// The allocation request is kept separate from total render peak because a
// fragmented heap can have ample free bytes and still fail one glyph buffer.
// If no render demand has been sampled yet, only host cost plus the normal
// connected-render safety floors are reserved.
constexpr bool readerBleQuiescentReadmissionAdmitted(const std::size_t freeHeap,
                                                      const std::size_t largestBlock,
                                                      const bool quiescentEpubReturn,
                                                      const std::size_t expectedRenderPeakBytes,
                                                      const std::size_t largestRenderRequestBytes) {
  if (!quiescentEpubReturn) return readerBleStartMemoryAdmitted(freeHeap, largestBlock);
  const std::size_t requiredFree = readerBleSaturatingAdd(
      readerBleSaturatingAdd(kReaderBleHostStartFreeReserve, kReaderBleRenderMinFreeHeap),
      expectedRenderPeakBytes);
  const std::size_t requiredLargest = readerBleSaturatingAdd(
      readerBleSaturatingAdd(kReaderBleHostStartLargestReserve, kReaderBleRenderMinLargestBlock),
      largestRenderRequestBytes);
  return freeHeap >= requiredFree && largestBlock >= requiredLargest;
}

// Experimental EPUB-only projection. The caller must prove that the Reader
// is idle and the next operation is a known cached page in a completed Section.
// Unknown work must use the existing conservative book-peak predictor.
constexpr bool readerBleEpubPageAwareReadmissionAdmitted(const std::size_t freeHeap,
                                                          const std::size_t largestBlock,
                                                          const bool quiescentEpubReturn,
                                                          const bool nextCachedPageKnown,
                                                          const std::size_t nextRenderFreeBytes,
                                                          const std::size_t nextRenderLargestBytes) {
  if (!quiescentEpubReturn || !nextCachedPageKnown) return false;
  const std::size_t requiredFree = readerBleSaturatingAdd(
      readerBleSaturatingAdd(
          readerBleSaturatingAdd(kReaderBleHostStartFreeReserve, kReaderBleEpubPageAwareRecoveryFreeFloor),
          nextRenderFreeBytes),
      kReaderBleEpubPageAwareFreeMargin);
  const std::size_t requiredLargest = readerBleSaturatingAdd(
      readerBleSaturatingAdd(
          readerBleSaturatingAdd(kReaderBleHostStartLargestReserve, kReaderBleEpubPageAwareRecoveryLargestFloor),
          nextRenderLargestBytes),
      kReaderBleEpubPageAwareLargestMargin);
  return freeHeap >= requiredFree && largestBlock >= requiredLargest;
}

// A completed EPUB boundary can request a fresh asynchronous admission attempt as soon
// as its existing predictive budget passes. This does not waive the budget or require
// a wall-clock wait; the cleanup path rechecks heap headroom immediately before starting.
constexpr bool readerBleEpubImmediateReadmissionAllowed(const bool bluetoothEnabled,
                                                        const bool returnPending,
                                                        const bool bleRunning,
                                                        const bool readerQuiescent,
                                                        const bool demandEstimateAvailable,
                                                        const bool pageTurnPending,
                                                        const bool bookReadmissionBlocked,
                                                        const bool admissionPasses) {
  return bluetoothEnabled && returnPending && !bleRunning && readerQuiescent &&
         demandEstimateAvailable && !pageTurnPending && !bookReadmissionBlocked && admissionPasses;
}

constexpr bool readerBleEpubPageAwareRetryAllowed(const bool bluetoothEnabled,
                                                   const bool returnPending,
                                                   const bool bleRunning,
                                                   const bool readerQuiescent,
                                                   const bool nextCachedPageKnown,
                                                   const bool pageTurnPending,
                                                   const bool bookReadmissionBlocked,
                                                   const bool admissionPasses) {
  return readerBleEpubImmediateReadmissionAllowed(
      bluetoothEnabled, returnPending, bleRunning, readerQuiescent, nextCachedPageKnown,
      pageTurnPending, bookReadmissionBlocked, admissionPasses);
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

// A connected-idle floor trip is recoverable only after a complete EPUB render
// has reached quiescence and worker-owned BLE teardown is confirmed. Startup
// failures use a separate hard fault and must not be cleared by this path.
constexpr bool readerBleEpubRuntimeFloorFaultCanClear(const bool renderComplete,
                                                       const bool readerQuiescent,
                                                       const bool bluetoothEnabled,
                                                       const bool diagnosticArmed,
                                                       const bool runtimeFloorFault,
                                                       const bool bleRunning,
                                                       const bool teardownPending,
                                                       const bool readerReturnPending) {
  return renderComplete && readerQuiescent && bluetoothEnabled && diagnosticArmed && runtimeFloorFault &&
         !bleRunning && !teardownPending && readerReturnPending;
}

}  // namespace bleinput
