#include <gtest/gtest.h>

#include "src/util/BleMemoryPolicy.h"

TEST(BleMemoryPolicy, RequiresBothReaderHeapFloors) {
  EXPECT_TRUE(bleinput::readerBleStartMemoryAdmitted(100 * 1024, 36 * 1024));
  EXPECT_FALSE(bleinput::readerBleStartMemoryAdmitted(100 * 1024 - 1, 36 * 1024));
  EXPECT_FALSE(bleinput::readerBleStartMemoryAdmitted(100 * 1024, 36 * 1024 - 1));
}

TEST(BleMemoryPolicy, MeasuredReaderStartSampleIsAdmitted) {
  EXPECT_TRUE(bleinput::readerBleStartMemoryAdmitted(105420, 38900));
}

TEST(BleMemoryPolicy, FragmentedReaderHeapIsDeferred) {
  EXPECT_FALSE(bleinput::readerBleStartMemoryAdmitted(109420, 26612));
}
