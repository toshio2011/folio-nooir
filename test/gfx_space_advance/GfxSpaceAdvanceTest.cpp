#include <gtest/gtest.h>

#include "GfxRenderer/SpaceAdvance.h"
#include "lib/EpdFont/EpdFontData.h"

namespace {

TEST(SdSpaceAdvance, ValidCachedMetricWins) {
  const int32_t selected = gfx::selectSpaceAdvanceFP(/*cached=*/144, /*fallback=*/192);

  EXPECT_EQ(selected, 144);
  EXPECT_EQ(fp4::toPixel(selected), 9);
}

TEST(SdSpaceAdvance, MissingCachedMetricUsesGlyphFallback) {
  const int32_t selected = gfx::selectSpaceAdvanceFP(/*cached=*/0, /*fallback=*/160);

  EXPECT_EQ(selected, 160);
  EXPECT_EQ(fp4::toPixel(selected), 10);
}

TEST(SdSpaceAdvance, MissingMetricNeverForcesZeroWhenFallbackIsValid) {
  const int32_t selected = gfx::selectSpaceAdvanceFP(/*cached=*/0, /*fallback=*/128);

  EXPECT_GT(fp4::toPixel(selected), 0);
}

TEST(SdSpaceAdvance, ZeroFallbackRemainsZeroWhenFontHasNoMetric) {
  EXPECT_EQ(gfx::selectSpaceAdvanceFP(/*cached=*/0, /*fallback=*/0), 0);
}

}  // namespace
