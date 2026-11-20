#include <gtest/gtest.h>

#include "src/util/BleMemoryPolicy.h"
#include "src/util/ReaderRenderBudget.h"

TEST(BleMemoryPolicy, RequiresBothGlobalColdStartFloors) {
  EXPECT_TRUE(bleinput::readerBleStartMemoryAdmitted(96 * 1024, 36 * 1024));
  EXPECT_FALSE(bleinput::readerBleStartMemoryAdmitted(96 * 1024 - 1, 36 * 1024));
  EXPECT_FALSE(bleinput::readerBleStartMemoryAdmitted(96 * 1024, bleinput::kReaderBleStartMinLargestBlock - 1));
}

TEST(BleMemoryPolicy, XtcStreamingUsesItsOwnMeasuredColdStartBudget) {
  // Supplied 20261119.1 X4 sample: the general 96 KiB gate missed by 144 B
  // despite a single 65,524 B block. The isolated XTC/XTCH experiment lowers
  // only the measured free-heap floor by 144 B and retains the same block floor.
  EXPECT_FALSE(bleinput::readerBleStartMemoryAdmitted(98160, 65524));
  EXPECT_TRUE(bleinput::readerBleXtcStartMemoryAdmitted(98160, 65524));
  EXPECT_FALSE(bleinput::readerBleXtcStartMemoryAdmitted(98160 - 1, 65524));
  EXPECT_FALSE(bleinput::readerBleXtcStartMemoryAdmitted(95 * 1024, bleinput::kReaderBleStartMinLargestBlock - 1));
}

TEST(BleMemoryPolicy, RuntimeStateDistinguishesOffFromAStoppedHost) {
  EXPECT_STREQ(bleinput::bleRuntimeStateName(false, false, false), "runtime_off");
  EXPECT_STREQ(bleinput::bleRuntimeStateName(false, false, true), "disabled_after_use");
  EXPECT_STREQ(bleinput::bleRuntimeStateName(true, false, false), "enabled_not_started");
  EXPECT_STREQ(bleinput::bleRuntimeStateName(true, false, true), "stopped_after_use");
  EXPECT_STREQ(bleinput::bleRuntimeStateName(false, true, true), "running");
}

TEST(BleMemoryPolicy, EpubRuntimeFloorFaultClearsOnlyAfterCompletedRenderAndConfirmedTeardown) {
  using bleinput::readerBleEpubRuntimeFloorFaultCanClear;
  EXPECT_TRUE(readerBleEpubRuntimeFloorFaultCanClear(true, true, true, true, false, false, true));
  EXPECT_FALSE(readerBleEpubRuntimeFloorFaultCanClear(false, true, true, true, false, false, true));
  EXPECT_FALSE(readerBleEpubRuntimeFloorFaultCanClear(true, false, true, true, false, false, true));
  EXPECT_FALSE(readerBleEpubRuntimeFloorFaultCanClear(true, true, false, true, false, false, true));
  EXPECT_FALSE(readerBleEpubRuntimeFloorFaultCanClear(true, true, true, false, false, false, true));
  EXPECT_FALSE(readerBleEpubRuntimeFloorFaultCanClear(true, true, true, true, true, false, true));
  EXPECT_FALSE(readerBleEpubRuntimeFloorFaultCanClear(true, true, true, true, false, true, true));
  EXPECT_FALSE(readerBleEpubRuntimeFloorFaultCanClear(true, true, true, true, false, false, false));
}

TEST(BleMemoryPolicy, LifecycleRetryRequiresPreferenceAndNoExclusiveOrTimeoutGate) {
  EXPECT_TRUE(bleinput::lifecycleReevaluationAllowed(true, false, false));
  EXPECT_FALSE(bleinput::lifecycleReevaluationAllowed(false, false, false));
  EXPECT_FALSE(bleinput::lifecycleReevaluationAllowed(true, true, false));
  EXPECT_FALSE(bleinput::lifecycleReevaluationAllowed(true, false, true));
}

TEST(BleMemoryPolicy, MeasuredCleanReaderIdleSampleIsAdmitted) {
  EXPECT_TRUE(bleinput::readerBleStartMemoryAdmitted(99952, 65524));
  EXPECT_TRUE(bleinput::readerBleStartMemoryAdmitted(105420, 38900));
}

TEST(BleMemoryPolicy, FragmentedReaderHeapIsDeferred) {
  EXPECT_FALSE(bleinput::readerBleStartMemoryAdmitted(109420, 26612));
}

TEST(BleMemoryPolicy, ContaminatedLowFreeHeapIsDeferred) {
  EXPECT_FALSE(bleinput::readerBleStartMemoryAdmitted(85000, 65524));
}

TEST(BleMemoryPolicy, HomeReclaimOnlyRunsForNearFloorQuiescentHeap) {
  EXPECT_TRUE(bleinput::homeBleStartReclaimAllowed(true, false, false, false, false, 95 * 1024, 65524));
  EXPECT_FALSE(bleinput::homeBleStartReclaimAllowed(true, false, false, false, false, 84 * 1024, 65524));
  EXPECT_FALSE(bleinput::homeBleStartReclaimAllowed(true, false, false, false, false,
                                                    96 * 1024, 65524));
  EXPECT_FALSE(bleinput::homeBleStartReclaimAllowed(true, false, false, false, true, 95 * 1024, 65524));
  EXPECT_FALSE(bleinput::homeBleStartReclaimAllowed(true, false, false, true, false, 95 * 1024, 65524));
  EXPECT_FALSE(bleinput::homeBleStartReclaimAllowed(false, false, false, false, false, 95 * 1024, 65524));
  EXPECT_FALSE(bleinput::homeBleStartReclaimAllowed(true, true, false, false, false, 95 * 1024, 65524));
  EXPECT_FALSE(bleinput::homeBleStartReclaimAllowed(true, false, true, false, false, 95 * 1024, 65524));
  EXPECT_FALSE(bleinput::homeBleStartReclaimAllowed(true, false, false, false, false,
                                                    95 * 1024, 20 * 1024));
  EXPECT_FALSE(bleinput::homeBleStartReclaimAllowed(true, false, false, false, false,
                                                    95 * 1024, 65524, true));
}

TEST(BleMemoryPolicy, OrdinaryHomeTransitionsKeepConnectedBleAlive) {
  EXPECT_FALSE(bleinput::activityTransitionNeedsBleSuspension(true, false));
  EXPECT_FALSE(bleinput::activityTransitionNeedsBleSuspension(false, false));
}

TEST(BleMemoryPolicy, ResourceSensitiveTransitionsStillYieldConnectedBle) {
  EXPECT_TRUE(bleinput::activityTransitionNeedsBleSuspension(true, true));
  EXPECT_FALSE(bleinput::activityTransitionNeedsBleSuspension(false, true));
}

TEST(BleMemoryPolicy, AnyReaderRenderWorkDefersBleAdmission) {
  // The application ORs RenderLock with pending/in-flight render work. The
  // current child activity has no bypass when a Reader remains on the stack.
  EXPECT_FALSE(bleinput::readerBleAdmissionRenderWorkSafe(true, true));
  EXPECT_TRUE(bleinput::readerBleAdmissionRenderWorkSafe(true, false));
}

TEST(BleMemoryPolicy, NonReaderRenderWorkKeepsExistingBleAdmissionBehavior) {
  EXPECT_TRUE(bleinput::readerBleAdmissionRenderWorkSafe(false, true));
}

TEST(BleMemoryPolicy, QuiescentEpubReturnHasSmallContextGatedMargin) {
  static_assert(bleinput::kReaderBleStartMinFreeHeap == 96 * 1024);
  static_assert(bleinput::kReaderBleQuiescentReturnFreeHeapMargin == 2 * 1024);

  // The observed ~97.8 KiB / ~59 KiB healthy idle shape may return BLE only
  // when the caller has already established the narrow quiescent EPUB gate.
  EXPECT_TRUE(bleinput::readerBleStartMemoryAdmittedForQuiescentEpubReturn(97756, 60000, true));
  EXPECT_FALSE(bleinput::readerBleStartMemoryAdmittedForQuiescentEpubReturn(97756, 60000, false));
}

TEST(BleMemoryPolicy, QuiescentEpubMarginStillRejectsLowOrFragmentedHeap) {
  EXPECT_FALSE(bleinput::readerBleStartMemoryAdmittedForQuiescentEpubReturn(
      bleinput::kReaderBleQuiescentReturnMinFreeHeap - 1,
      bleinput::kReaderBleQuiescentReturnMinLargestBlock, true));
  EXPECT_FALSE(bleinput::readerBleStartMemoryAdmittedForQuiescentEpubReturn(97756, 50000, true));
  EXPECT_FALSE(bleinput::readerBleStartMemoryAdmittedForQuiescentEpubReturn(109420, 26612, true));
}

TEST(BleMemoryPolicy, QuiescentReadmissionReservesHostAndObservedPageDemand) {
  constexpr std::size_t renderPeak = 8 * 1024;
  constexpr std::size_t largestRequest = 4 * 1024;
  constexpr std::size_t requiredFree = bleinput::kReaderBleHostStartFreeReserve +
                                       bleinput::kReaderBleRenderMinFreeHeap + renderPeak;
  constexpr std::size_t requiredLargest = bleinput::kReaderBleHostStartLargestReserve +
                                          bleinput::kReaderBleRenderMinLargestBlock + largestRequest;

  EXPECT_TRUE(bleinput::readerBleQuiescentReadmissionAdmitted(
      requiredFree, requiredLargest, true, renderPeak, largestRequest));
  EXPECT_FALSE(bleinput::readerBleQuiescentReadmissionAdmitted(
      requiredFree - 1, requiredLargest, true, renderPeak, largestRequest));
  EXPECT_FALSE(bleinput::readerBleQuiescentReadmissionAdmitted(
      requiredFree, requiredLargest - 1, true, renderPeak, largestRequest));
}

TEST(BleMemoryPolicy, ReaderReadmissionDefersWhenHostStartWouldCrowdTheNextRender) {
  EXPECT_FALSE(bleinput::readerBleQuiescentReadmissionAdmitted(
      100 * 1024, 65524, true, 12 * 1024, 4096));
  EXPECT_FALSE(bleinput::readerBleQuiescentReadmissionAdmitted(
      130 * 1024, 56 * 1024 + 4095, true, 12 * 1024, 4096));
}

TEST(BleMemoryPolicy, EpubPageAwareReadmissionUsesKnownCachedPageBudgetAndMargins) {
  constexpr std::size_t nextRenderFree = 20 * 1024;
  constexpr std::size_t nextRenderLargest = 6 * 1024;
  constexpr std::size_t requiredFree = bleinput::kReaderBleHostStartFreeReserve +
                                      bleinput::kReaderBleEpubPageAwareRecoveryFreeFloor + nextRenderFree +
                                      bleinput::kReaderBleEpubPageAwareFreeMargin;
  constexpr std::size_t requiredLargest = bleinput::kReaderBleHostStartLargestReserve +
                                          bleinput::kReaderBleEpubPageAwareRecoveryLargestFloor +
                                          nextRenderLargest +
                                          bleinput::kReaderBleEpubPageAwareLargestMargin;
  EXPECT_TRUE(bleinput::readerBleEpubPageAwareReadmissionAdmitted(
      requiredFree, requiredLargest, true, true, nextRenderFree, nextRenderLargest));
  EXPECT_FALSE(bleinput::readerBleEpubPageAwareReadmissionAdmitted(
      requiredFree - 1, requiredLargest, true, true, nextRenderFree, nextRenderLargest));
  EXPECT_FALSE(bleinput::readerBleEpubPageAwareReadmissionAdmitted(
      requiredFree, requiredLargest - 1, true, true, nextRenderFree, nextRenderLargest));
}

TEST(BleMemoryPolicy, EpubPageAwareReadmissionRejectsUnknownOrActiveWork) {
  EXPECT_FALSE(bleinput::readerBleEpubPageAwareReadmissionAdmitted(
      200 * 1024, 100 * 1024, false, true, 1, 1));
  EXPECT_FALSE(bleinput::readerBleEpubPageAwareReadmissionAdmitted(
      200 * 1024, 100 * 1024, true, false, 1, 1));
  constexpr std::size_t zeroDemandFree = bleinput::kReaderBleHostStartFreeReserve +
                                         bleinput::kReaderBleEpubPageAwareRecoveryFreeFloor +
                                         bleinput::kReaderBleEpubPageAwareFreeMargin;
  constexpr std::size_t zeroDemandLargest = bleinput::kReaderBleHostStartLargestReserve +
                                           bleinput::kReaderBleEpubPageAwareRecoveryLargestFloor +
                                           bleinput::kReaderBleEpubPageAwareLargestMargin;
  EXPECT_TRUE(bleinput::readerBleEpubPageAwareReadmissionAdmitted(
      zeroDemandFree, zeroDemandLargest, true, true, 0, 0));
  EXPECT_FALSE(bleinput::readerBleEpubPageAwareReadmissionAdmitted(
      zeroDemandFree - 1, zeroDemandLargest, true, true, 0, 0));
}

TEST(BleMemoryPolicy, EpubIdleStartAndPostStartUseSeparateLargestBlockPhases) {
  EXPECT_EQ(bleinput::kReaderBleEpubIdleOnlyRequiredFree, 102 * 1024u);
  EXPECT_EQ(bleinput::kReaderBleEpubIdleOnlyRequiredLargest, bleinput::kReaderBleStartMinLargestBlock);
  EXPECT_EQ(bleinput::kReaderBleEpubIdleOnlyRequiredLargest,
            36 * 1024u - bleinput::kHeapPoisonLargestBlockOverhead);

  // The automatic sequential-mode COM3 reached 106,332 B free / 53,236 B
  // largest after cleanup. This is not a 12-byte exception: pre-start uses
  // the existing cold-start block floor, and begin()/runtime use a separate
  // fail-closed 32 KiB / 16 KiB post-start floor.
  EXPECT_TRUE(bleinput::readerBleEpubIdleOnlyPostStartSafe(32 * 1024, 16 * 1024));
  EXPECT_FALSE(bleinput::readerBleEpubIdleOnlyPostStartSafe(32 * 1024 - 1, 16 * 1024));
  EXPECT_FALSE(bleinput::readerBleEpubIdleOnlyPostStartSafe(32 * 1024, 16 * 1024 - 1));
}

TEST(ReaderRenderBudget, KnownCachedPageUsesObservedTurnDemandAndBoundedMargins) {
  constexpr std::size_t observedPeak = 6'676;
  constexpr std::size_t largestRequest = 2'048;
  constexpr auto budget = reader_memory::observedCachedPageTurnBudget(
      observedPeak, largestRequest,
      bleinput::kReaderBleEpubPageAwareRecoveryFreeFloor,
      bleinput::kReaderBleEpubPageAwareRecoveryLargestFloor,
      bleinput::kReaderBleEpubPageAwareFreeMargin,
      bleinput::kReaderBleEpubPageAwareLargestMargin);
  EXPECT_EQ(budget.freeBytes, bleinput::kReaderBleEpubPageAwareRecoveryFreeFloor + observedPeak +
                                  bleinput::kReaderBleEpubPageAwareFreeMargin);
  EXPECT_EQ(budget.largestBlock, bleinput::kReaderBleEpubPageAwareRecoveryLargestFloor + largestRequest +
                                     bleinput::kReaderBleEpubPageAwareLargestMargin);
  EXPECT_EQ(reader_memory::saturatingAdd(static_cast<std::size_t>(-1), 1),
            static_cast<std::size_t>(-1));
}

TEST(BleMemoryPolicy, EpubPageAwareRetryStartsAsSoonAsMeasuredAdmissionPasses) {
  EXPECT_TRUE(bleinput::readerBleEpubPageAwareRetryAllowed(
      true, true, false, true, true, false, false, true));
  EXPECT_FALSE(bleinput::readerBleEpubPageAwareRetryAllowed(
      false, true, false, true, true, false, false, true));
  EXPECT_FALSE(bleinput::readerBleEpubPageAwareRetryAllowed(
      true, false, false, true, true, false, false, true));
  EXPECT_FALSE(bleinput::readerBleEpubPageAwareRetryAllowed(
      true, true, true, true, true, false, false, true));
  EXPECT_FALSE(bleinput::readerBleEpubPageAwareRetryAllowed(
      true, true, false, false, true, false, false, true));
  EXPECT_FALSE(bleinput::readerBleEpubPageAwareRetryAllowed(
      true, true, false, true, false, false, false, true));
  EXPECT_FALSE(bleinput::readerBleEpubPageAwareRetryAllowed(
      true, true, false, true, true, true, false, true));
  EXPECT_FALSE(bleinput::readerBleEpubPageAwareRetryAllowed(
      true, true, false, true, true, false, true, true));
  EXPECT_FALSE(bleinput::readerBleEpubPageAwareRetryAllowed(
      true, true, false, true, true, false, false, false));
}

TEST(BleMemoryPolicy, EpubImmediateAdmissionRequiresQuiescenceAndBothHeapBudgets) {
  EXPECT_TRUE(bleinput::readerBleEpubImmediateReadmissionAllowed(
      true, true, false, true, true, false, false, true));
  EXPECT_FALSE(bleinput::readerBleEpubImmediateReadmissionAllowed(
      true, true, false, true, true, false, false, false));  // low or fragmented heap
  EXPECT_FALSE(bleinput::readerBleEpubImmediateReadmissionAllowed(
      true, true, false, false, true, false, false, true));  // render/build still active
  EXPECT_FALSE(bleinput::readerBleEpubImmediateReadmissionAllowed(
      true, true, false, true, false, false, false, true));  // no usable demand estimate
  EXPECT_FALSE(bleinput::readerBleEpubImmediateReadmissionAllowed(
      true, true, false, true, true, true, false, true));   // pending page turn
  EXPECT_FALSE(bleinput::readerBleEpubImmediateReadmissionAllowed(
      true, true, false, true, true, false, true, true));   // per-book safety latch
}

TEST(BleMemoryPolicy, NonReaderColdAdmissionKeepsItsExistingFloors) {
  EXPECT_TRUE(bleinput::readerBleQuiescentReadmissionAdmitted(
      bleinput::kReaderBleStartMinFreeHeap, bleinput::kReaderBleStartMinLargestBlock,
      false, 64 * 1024, 16 * 1024));
  EXPECT_FALSE(bleinput::readerBleQuiescentReadmissionAdmitted(
      bleinput::kReaderBleStartMinFreeHeap - 1, bleinput::kReaderBleStartMinLargestBlock,
      false, 0, 0));
}

TEST(BleMemoryPolicy, StrictReaderAdmissionIsUnchangedByQuiescentMargin) {
  EXPECT_FALSE(bleinput::readerBleStartMemoryAdmitted(97756, 60000));
  EXPECT_TRUE(bleinput::readerBleStartMemoryAdmittedForQuiescentEpubReturn(98304, 60000, false));
}

TEST(BleMemoryPolicy, ReaderReturnCleanupRequiresEveryLifecycleGate) {
  EXPECT_TRUE(bleinput::readerBleReturnCleanupAllowed(true, true, true, true, true, false, false));
  EXPECT_FALSE(bleinput::readerBleReturnCleanupAllowed(false, true, true, true, true, false, false));
  EXPECT_FALSE(bleinput::readerBleReturnCleanupAllowed(true, true, false, true, true, false, false));
  EXPECT_FALSE(bleinput::readerBleReturnCleanupAllowed(true, true, true, false, true, false, false));
  EXPECT_FALSE(bleinput::readerBleReturnCleanupAllowed(true, true, true, true, false, false, false));
  EXPECT_FALSE(bleinput::readerBleReturnCleanupAllowed(true, true, true, true, true, true, false));
  EXPECT_FALSE(bleinput::readerBleReturnCleanupAllowed(true, true, true, true, true, false, true));
}

TEST(BleMemoryPolicy, DeepReaderCacheReclaimRequiresEverySafetyGate) {
  EXPECT_TRUE(bleinput::readerBleDeepCacheReclaimSafe(true, true, true, true, true, true, true, true, true));
  EXPECT_FALSE(bleinput::readerBleDeepCacheReclaimSafe(false, true, true, true, true, true, true, true, true));
  EXPECT_FALSE(bleinput::readerBleDeepCacheReclaimSafe(true, false, true, true, true, true, true, true, true));
  EXPECT_FALSE(bleinput::readerBleDeepCacheReclaimSafe(true, true, false, true, true, true, true, true, true));
  EXPECT_FALSE(bleinput::readerBleDeepCacheReclaimSafe(true, true, true, false, true, true, true, true, true));
  EXPECT_FALSE(bleinput::readerBleDeepCacheReclaimSafe(true, true, true, true, false, true, true, true, true));
  EXPECT_FALSE(bleinput::readerBleDeepCacheReclaimSafe(true, true, true, true, true, false, true, true, true));
  EXPECT_FALSE(bleinput::readerBleDeepCacheReclaimSafe(true, true, true, true, true, true, false, true, true));
  EXPECT_FALSE(bleinput::readerBleDeepCacheReclaimSafe(true, true, true, true, true, true, true, false, true));
  EXPECT_FALSE(bleinput::readerBleDeepCacheReclaimSafe(true, true, true, true, true, true, true, true, false));
}

TEST(BleMemoryPolicy, StrictAdmissionMissEnablesOnlySafeDeepReclaim) {
  EXPECT_FALSE(bleinput::readerBleDeepCacheReclaimEligible(false, false));
  EXPECT_TRUE(bleinput::readerBleDeepCacheReclaimEligible(false, true));
  EXPECT_TRUE(bleinput::readerBleDeepCacheReclaimEligible(true, false));
  EXPECT_FALSE(bleinput::readerBleDeepCacheReclaimAllowed(false, true, true));
  EXPECT_FALSE(bleinput::readerBleDeepCacheReclaimAllowed(true, false, true));
  EXPECT_FALSE(bleinput::readerBleDeepCacheReclaimAllowed(true, true, false));
  EXPECT_TRUE(bleinput::readerBleDeepCacheReclaimAllowed(true, true, true));
}

TEST(BleMemoryPolicy, BleOffNeverRequestsReaderRenderReclaim) {
  EXPECT_FALSE(bleinput::readerBleCachedRenderNeedsReclaim(false, false, 15 * 1024, 8 * 1024));
  EXPECT_FALSE(bleinput::readerBleCachedRenderNeedsReclaim(false, true, 15 * 1024, 8 * 1024));
}

TEST(BleMemoryPolicy, HealthyConnectedRenderLeavesCachesUntouched) {
  EXPECT_FALSE(bleinput::readerBleCachedRenderNeedsReclaim(true, true, 40 * 1024, 24 * 1024));
  EXPECT_TRUE(bleinput::readerBleCachedRenderHeadroomSafe(36 * 1024, 20 * 1024));
}

TEST(BleMemoryPolicy, ColdBleStartWaitsForPendingHomeRenderButNotOtherContexts) {
  EXPECT_FALSE(bleinput::homeBleColdStartRenderWorkSafe(true, true));
  EXPECT_TRUE(bleinput::homeBleColdStartRenderWorkSafe(true, false));
  EXPECT_TRUE(bleinput::homeBleColdStartRenderWorkSafe(false, true));
}

TEST(BleMemoryPolicy, MarginalConnectedRenderRequestsBoundedReclaim) {
  EXPECT_TRUE(bleinput::readerBleCachedRenderNeedsReclaim(true, true, 32 * 1024, 26 * 1024));
  EXPECT_TRUE(bleinput::readerBleCachedRenderNeedsReclaim(true, true, 40 * 1024, 18 * 1024));
}

TEST(BleMemoryPolicy, SurvivalStopReadmissionBackoffIsBoundedAndResetsAfterStableTime) {
  EXPECT_EQ(bleinput::readerBleSurvivalBackoffMs(0, 0), 15'000u);
  EXPECT_EQ(bleinput::readerBleSurvivalBackoffMs(1, 10'000), 15'000u);
  EXPECT_EQ(bleinput::readerBleSurvivalBackoffMs(2, 10'000), 30'000u);
  EXPECT_EQ(bleinput::readerBleSurvivalBackoffMs(3, 10'000), 60'000u);
  EXPECT_EQ(bleinput::readerBleSurvivalBackoffMs(4, 10'000), 120'000u);
  EXPECT_EQ(bleinput::readerBleSurvivalBackoffMs(8, 10'000), 120'000u);
  EXPECT_EQ(bleinput::readerBleSurvivalBackoffMs(4, bleinput::kReaderBleSurvivalBackoffResetMs), 15'000u);
}

TEST(BleMemoryPolicy, RepeatedSurvivalStopsBlockReadmissionForCurrentBookUntilExit) {
  uint8_t bookStops = 0;
  EXPECT_FALSE(bleinput::readerBleBookSurvivalReadmissionBlocked(bookStops));
  bookStops = bleinput::nextReaderBleBookSurvivalStopCount(bookStops);
  EXPECT_FALSE(bleinput::readerBleBookSurvivalReadmissionBlocked(bookStops));
  bookStops = bleinput::nextReaderBleBookSurvivalStopCount(bookStops);
  EXPECT_FALSE(bleinput::readerBleBookSurvivalReadmissionBlocked(bookStops));
  bookStops = bleinput::nextReaderBleBookSurvivalStopCount(bookStops);
  EXPECT_TRUE(bleinput::readerBleBookSurvivalReadmissionBlocked(bookStops));

  // Expiring the bounded retry delay does not erase book-lifetime evidence.
  EXPECT_EQ(bleinput::readerBleSurvivalBackoffMs(3, bleinput::kReaderBleSurvivalBackoffResetMs),
            bleinput::kReaderBleSurvivalBackoffInitialMs);
  EXPECT_TRUE(bleinput::readerBleBookSurvivalReadmissionBlocked(bookStops));
  EXPECT_EQ(bleinput::nextReaderBleBookSurvivalStopCount(bookStops),
            bleinput::kReaderBleBookSurvivalStopLimit);

  // A new Reader Activity represents an exited/opened book and starts clean.
  bookStops = 0;
  EXPECT_FALSE(bleinput::readerBleBookSurvivalReadmissionBlocked(bookStops));
}

TEST(BleMemoryPolicy, SurvivalBackoffResetsOnlyAfterConsecutiveSafeReaderRenders) {
  constexpr auto safeFree = bleinput::kReaderBleRenderMinFreeHeap;
  constexpr auto safeLargest = bleinput::kReaderBleRenderMinLargestBlock;
  EXPECT_FALSE(bleinput::readerBleSafeRenderResetsSurvivalBackoff(2, safeFree, safeLargest, safeFree));
  EXPECT_FALSE(bleinput::readerBleSafeRenderResetsSurvivalBackoff(
      3, safeFree - 1, safeLargest, safeFree));
  EXPECT_FALSE(bleinput::readerBleSafeRenderResetsSurvivalBackoff(
      3, safeFree, safeLargest, safeFree - 1));
  EXPECT_FALSE(bleinput::readerBleSafeRenderResetsSurvivalBackoff(
      3, safeFree, safeLargest - 1, safeFree));
  EXPECT_TRUE(bleinput::readerBleSafeRenderResetsSurvivalBackoff(
      bleinput::kReaderBleSurvivalBackoffSafeRenderResetCount, safeFree, safeLargest, safeFree));
}

TEST(ReaderRenderBudget, LargeRequestIsConstrainedWithHeadroomAndHeavyWhenBleConsumesIt) {
  const reader_memory::PressureObservation healthy{
      100 * 1024, 72 * 1024, 80 * 1024, 32635, false, false, false};
  const reader_memory::PressureObservation starved{
      32 * 1024, 28660, 14 * 1024, 32635, false, false, false};

  EXPECT_EQ(reader_memory::classifyPressure(healthy), reader_memory::PressureTier::Constrained);
  EXPECT_EQ(reader_memory::classifyPressure(starved), reader_memory::PressureTier::Heavy);
}

TEST(ReaderRenderBudget, WindowPressureAndAllocationFailureAreHeavy) {
  reader_memory::PressureObservation observation{90 * 1024, 64 * 1024, 60 * 1024, 0, true, false, false};
  EXPECT_EQ(reader_memory::classifyPressure(observation), reader_memory::PressureTier::Heavy);
  observation.readerWindowPressure = false;
  observation.renderAllocationFailed = true;
  EXPECT_EQ(reader_memory::classifyPressure(observation), reader_memory::PressureTier::Heavy);
}

TEST(ReaderRenderBudget, CacheBudgetsShrinkByTierAndRecoveryIsHysteretic) {
  const auto normal = reader_memory::cacheBudgetFor(reader_memory::PressureTier::Normal);
  const auto constrained = reader_memory::cacheBudgetFor(reader_memory::PressureTier::Constrained);
  const auto heavy = reader_memory::cacheBudgetFor(reader_memory::PressureTier::Heavy);
  EXPECT_GT(normal.pageGlyphLimit, constrained.pageGlyphLimit);
  EXPECT_GT(constrained.pageGlyphLimit, heavy.pageGlyphLimit);
  EXPECT_GT(normal.scanTextBytesPerFont, constrained.scanTextBytesPerFont);
  EXPECT_GT(constrained.scanTextBytesPerFont, heavy.scanTextBytesPerFont);

  EXPECT_EQ(reader_memory::advancePressureTier(reader_memory::PressureTier::Heavy,
                                               reader_memory::PressureTier::Normal, 1),
            reader_memory::PressureTier::Heavy);
  EXPECT_EQ(reader_memory::advancePressureTier(reader_memory::PressureTier::Heavy,
                                               reader_memory::PressureTier::Normal, 2),
            reader_memory::PressureTier::Constrained);
}

TEST(ReaderRenderBudget, PreflightIncludesMeasuredContiguousRequestAndSafetyMargin) {
  const auto required = reader_memory::cachedRenderBudget(
      2 * 4096, 512 * sizeof(uint32_t), 0, 384 * 16, 0, 0, 32635, false);

  EXPECT_GE(required.largestBlock, 32635 + reader_memory::kLargestBlockMargin);
  EXPECT_FALSE(reader_memory::renderAdmitted(required.freeBytes, 30000, required));
  EXPECT_TRUE(reader_memory::renderAdmitted(required.freeBytes, required.largestBlock, required));
}
