#include <gtest/gtest.h>

#include "src/util/BleMemoryPolicy.h"

TEST(BleMemoryPolicy, RequiresBothGlobalColdStartFloors) {
  EXPECT_TRUE(bleinput::readerBleStartMemoryAdmitted(96 * 1024, 36 * 1024));
  EXPECT_FALSE(bleinput::readerBleStartMemoryAdmitted(96 * 1024 - 1, 36 * 1024));
  EXPECT_FALSE(bleinput::readerBleStartMemoryAdmitted(96 * 1024, 36 * 1024 - 1));
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

TEST(BleMemoryPolicy, ReaderReturnCleanupRequiresEveryLifecycleGate) {
  EXPECT_TRUE(bleinput::readerBleReturnCleanupAllowed(true, true, true, true, true, false, false));
  EXPECT_FALSE(bleinput::readerBleReturnCleanupAllowed(false, true, true, true, true, false, false));
  EXPECT_FALSE(bleinput::readerBleReturnCleanupAllowed(true, true, false, true, true, false, false));
  EXPECT_FALSE(bleinput::readerBleReturnCleanupAllowed(true, true, true, false, true, false, false));
  EXPECT_FALSE(bleinput::readerBleReturnCleanupAllowed(true, true, true, true, false, false, false));
  EXPECT_FALSE(bleinput::readerBleReturnCleanupAllowed(true, true, true, true, true, true, false));
  EXPECT_FALSE(bleinput::readerBleReturnCleanupAllowed(true, true, true, true, true, false, true));
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
