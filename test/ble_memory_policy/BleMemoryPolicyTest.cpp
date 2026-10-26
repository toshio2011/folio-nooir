#include <gtest/gtest.h>

#include "src/util/BleMemoryPolicy.h"
#include "src/util/ReaderRenderBudget.h"

TEST(BleMemoryPolicy, RequiresBothGlobalColdStartFloors) {
  EXPECT_TRUE(bleinput::readerBleStartMemoryAdmitted(96 * 1024, 36 * 1024));
  EXPECT_FALSE(bleinput::readerBleStartMemoryAdmitted(96 * 1024 - 1, 36 * 1024));
  EXPECT_FALSE(bleinput::readerBleStartMemoryAdmitted(96 * 1024, 36 * 1024 - 1));
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

TEST(BleMemoryPolicy, MarginalConnectedRenderRequestsBoundedReclaim) {
  EXPECT_TRUE(bleinput::readerBleCachedRenderNeedsReclaim(true, true, 32 * 1024, 26 * 1024));
  EXPECT_TRUE(bleinput::readerBleCachedRenderNeedsReclaim(true, true, 40 * 1024, 18 * 1024));
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
