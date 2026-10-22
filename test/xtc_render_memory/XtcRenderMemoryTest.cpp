#include <gtest/gtest.h>

#include "Xtc/XtcFrameGeometry.h"
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

TEST(XtcFrameGeometry, FitContainsWholePageAndCentersUnusedSpace) {
  xtc_frame::Mapping mapping;
  ASSERT_TRUE(xtc_frame::calculateMapping(600, 600, 20, 30, 480, 800,
                                          xtc_frame::Mode::FitContain, mapping));
  EXPECT_EQ(mapping.source.x, 0);
  EXPECT_EQ(mapping.source.y, 0);
  EXPECT_EQ(mapping.source.width, 600);
  EXPECT_EQ(mapping.source.height, 600);
  EXPECT_EQ(mapping.destination.x, 20);
  EXPECT_EQ(mapping.destination.y, 190);
  EXPECT_EQ(mapping.destination.width, 480);
  EXPECT_EQ(mapping.destination.height, 480);
}

TEST(XtcFrameGeometry, FillUsesCenteredSourceCropAndWholeViewport) {
  xtc_frame::Mapping mapping;
  ASSERT_TRUE(xtc_frame::calculateMapping(1000, 1000, 10, 20, 480, 800,
                                          xtc_frame::Mode::CropFill, mapping));
  EXPECT_EQ(mapping.source.x, 200);
  EXPECT_EQ(mapping.source.y, 0);
  EXPECT_EQ(mapping.source.width, 600);
  EXPECT_EQ(mapping.source.height, 1000);
  EXPECT_EQ(mapping.destination.x, 10);
  EXPECT_EQ(mapping.destination.y, 20);
  EXPECT_EQ(mapping.destination.width, 480);
  EXPECT_EQ(mapping.destination.height, 800);
}

TEST(XtcFrameGeometry, PreservesExactDeviceAspectAndRejectsEmptyGeometry) {
  xtc_frame::Mapping mapping;
  ASSERT_TRUE(xtc_frame::calculateMapping(480, 800, 0, 0, 480, 800,
                                          xtc_frame::Mode::FitContain, mapping));
  EXPECT_EQ(mapping.destination.width, 480);
  EXPECT_EQ(mapping.destination.height, 800);
  EXPECT_FALSE(xtc_frame::calculateMapping(0, 800, 0, 0, 480, 800,
                                           xtc_frame::Mode::CropFill, mapping));
  EXPECT_FALSE(mapping.valid);
  EXPECT_FALSE(xtc_frame::calculateMapping(480, 800, 65'000, 0, 800, 800,
                                           xtc_frame::Mode::FitContain, mapping));
  EXPECT_FALSE(xtc_frame::calculateMapping(480, 800, 0, 0, 480, 800,
                                           static_cast<xtc_frame::Mode>(99), mapping));
}

