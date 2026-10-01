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
