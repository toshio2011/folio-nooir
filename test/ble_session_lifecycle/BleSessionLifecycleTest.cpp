#include <gtest/gtest.h>

#include "src/BleInput.h"

TEST(BleSessionLifecycle, ReaderOnlyBluetoothIsStoppedWhenSavedPreferenceIsOff) {
  EXPECT_TRUE(bleinput::readerSessionBleShouldStopOnExit(false, 120 * 1024, 96 * 1024));
}

TEST(BleSessionLifecycle, SavedBluetoothPreferenceKeepsHealthyRuntimeConnectionOnExit) {
  EXPECT_FALSE(bleinput::readerSessionBleShouldStopOnExit(true, 100 * 1024, 96 * 1024));
  EXPECT_FALSE(bleinput::readerSessionBleShouldStopOnExit(true, 96 * 1024, 96 * 1024));
}

TEST(BleSessionLifecycle, SavedBluetoothPreferenceSuspendsRuntimeAtLowHeap) {
  EXPECT_TRUE(bleinput::readerSessionBleShouldStopOnExit(true, 96 * 1024 - 1, 96 * 1024));
}
