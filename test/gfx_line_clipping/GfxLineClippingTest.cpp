#include <gtest/gtest.h>

#include "GfxRenderer/GfxLineClip.h"

namespace {

constexpr int SCREEN_WIDTH = 480;
constexpr int SCREEN_HEIGHT = 800;

TEST(GfxLineClipping, FullyInBoundsLineIsUnchanged) {
  int x1 = 10;
  int y1 = 20;
  int x2 = 430;
  int y2 = 700;

  EXPECT_TRUE(gfx::clipLineToLogicalScreen(x1, y1, x2, y2, SCREEN_WIDTH, SCREEN_HEIGHT));
  EXPECT_EQ(x1, 10);
  EXPECT_EQ(y1, 20);
  EXPECT_EQ(x2, 430);
  EXPECT_EQ(y2, 700);
}

TEST(GfxLineClipping, PartiallyClipsHorizontalLine) {
  int x1 = -40;
  int y1 = 300;
  int x2 = 120;
  int y2 = 300;

  EXPECT_TRUE(gfx::clipLineToLogicalScreen(x1, y1, x2, y2, SCREEN_WIDTH, SCREEN_HEIGHT));
  EXPECT_EQ(x1, 0);
  EXPECT_EQ(y1, 300);
  EXPECT_EQ(x2, 120);
  EXPECT_EQ(y2, 300);
}

TEST(GfxLineClipping, PartiallyClipsVerticalLine) {
  int x1 = 240;
  int y1 = -50;
  int x2 = 240;
  int y2 = 120;

  EXPECT_TRUE(gfx::clipLineToLogicalScreen(x1, y1, x2, y2, SCREEN_WIDTH, SCREEN_HEIGHT));
  EXPECT_EQ(x1, 240);
  EXPECT_EQ(y1, 0);
  EXPECT_EQ(x2, 240);
  EXPECT_EQ(y2, 120);
}

TEST(GfxLineClipping, FullyOutsideLineIsRejected) {
  int x1 = -100;
  int y1 = -80;
  int x2 = -20;
  int y2 = -10;

  EXPECT_FALSE(gfx::clipLineToLogicalScreen(x1, y1, x2, y2, SCREEN_WIDTH, SCREEN_HEIGHT));
}

TEST(GfxLineClipping, ClipsLineCrossingScreenEdge) {
  int x1 = -10;
  int y1 = 10;
  int x2 = 10;
  int y2 = 30;

  EXPECT_TRUE(gfx::clipLineToLogicalScreen(x1, y1, x2, y2, SCREEN_WIDTH, SCREEN_HEIGHT));
  EXPECT_EQ(x1, 0);
  EXPECT_EQ(y1, 20);
  EXPECT_EQ(x2, 10);
  EXPECT_EQ(y2, 30);
}

}  // namespace
