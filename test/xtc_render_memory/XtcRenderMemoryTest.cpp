#include <gtest/gtest.h>

#include "activities/reader/XtcRenderMemoryPolicy.h"

TEST(XtcRenderMemory, SkipsOptionalSnapshotWhenBleLeavesLessThanFramePlusHeadroom) {
  constexpr std::size_t frameBytes = 48 * 1024;
  EXPECT_FALSE(xtc_memory::canPreserveBwFrame(35816, 32756, frameBytes));
  EXPECT_FALSE(xtc_memory::canPreserveBwFrame(27028, 24564, frameBytes));
}

TEST(XtcRenderMemory, KeepsSnapshotOptimizationWhenHeapHasRoom) {
  constexpr std::size_t frameBytes = 48 * 1024;
  EXPECT_TRUE(xtc_memory::canPreserveBwFrame(105176, 65524, frameBytes));
}

TEST(XtcRenderMemory, RejectsAFragmentedHeapEvenWhenTotalFreeIsHigh) {
  EXPECT_FALSE(xtc_memory::canPreserveBwFrame(105176, 7000, 48 * 1024));
}

