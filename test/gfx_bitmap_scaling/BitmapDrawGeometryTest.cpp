#include <gtest/gtest.h>

#include "GfxRenderer/BitmapDrawGeometry.h"

TEST(BitmapDrawGeometry, UpscalingIsOptInAndFitsBothDimensions) {
  EXPECT_FLOAT_EQ(BitmapDrawGeometry::boundedScale(220, 360, 480, 800, false), 1.0f);

  const float scale = BitmapDrawGeometry::boundedScale(220, 360, 480, 800, true);
  EXPECT_NEAR(scale, 480.0f / 220.0f, 0.0001f);
  EXPECT_LE(220 * scale, 480.001f);
  EXPECT_LE(360 * scale, 800.001f);
}

TEST(BitmapDrawGeometry, EnlargementSpansCoverEveryDestinationPixelOnce) {
  constexpr int sourceWidth = 220;
  constexpr int targetWidth = 480;
  const float scale = BitmapDrawGeometry::boundedScale(sourceWidth, 360, targetWidth, 800, true);

  int nextDestination = 0;
  for (int sourceX = 0; sourceX < sourceWidth; ++sourceX) {
    const int first = BitmapDrawGeometry::pixelStart(sourceX, scale);
    const int end = BitmapDrawGeometry::pixelEndExclusive(sourceX, scale);
    EXPECT_EQ(first, nextDestination);
    EXPECT_GT(end, first);
    nextDestination = end;
  }
  EXPECT_EQ(nextDestination, targetWidth);
}

TEST(BitmapDrawGeometry, DownscalingKeepsSinglePixelSampling) {
  EXPECT_FLOAT_EQ(BitmapDrawGeometry::boundedScale(1000, 1000, 480, 800, true), 0.48f);
  EXPECT_EQ(BitmapDrawGeometry::pixelStart(3, 0.48f), 1);
  EXPECT_EQ(BitmapDrawGeometry::pixelEndExclusive(3, 0.48f), 2);
}
