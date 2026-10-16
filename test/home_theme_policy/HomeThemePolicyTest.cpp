#include <gtest/gtest.h>

#include "src/util/HomeThemePolicy.h"

namespace {
constexpr uint8_t kLiteThemeValue = 5;
constexpr uint8_t kCarouselThreeCover = 0;
constexpr uint8_t kFolioThreeCoverCarousel = 2;
constexpr uint8_t kFolioFiveCoverCarousel = 3;
}  // namespace

TEST(HomeThemePolicy, MigratesPersistedLegacyCarouselLayoutForThemeValueFive) {
  const auto migrated = HomeThemePolicy::migrateLegacyCarouselLayout(
      5, kLiteThemeValue, kCarouselThreeCover, true, false, 0, false, 0,
      kCarouselThreeCover, kFolioThreeCoverCarousel, kFolioFiveCoverCarousel);

  EXPECT_TRUE(migrated.applied);
  EXPECT_EQ(migrated.recentBookLayout, kFolioThreeCoverCarousel);
  EXPECT_EQ(migrated.finishedBookLayout, kFolioThreeCoverCarousel);
}

TEST(HomeThemePolicy, PreservesExplicitRecentAndFinishedLayouts) {
  const auto migrated = HomeThemePolicy::migrateLegacyCarouselLayout(
      5, kLiteThemeValue, kCarouselThreeCover, true, true, 4, true, 1,
      kCarouselThreeCover, kFolioThreeCoverCarousel, kFolioFiveCoverCarousel);

  EXPECT_FALSE(migrated.applied);
  EXPECT_EQ(migrated.recentBookLayout, 4);
  EXPECT_EQ(migrated.finishedBookLayout, 1);
}

TEST(HomeThemePolicy, MigratesOnlyMissingLayoutAndKeepsLegacyThemeIdsIsolated) {
  const auto partial = HomeThemePolicy::migrateLegacyCarouselLayout(
      5, kLiteThemeValue, 1, true, true, 4, false, 0,
      kCarouselThreeCover, kFolioThreeCoverCarousel, kFolioFiveCoverCarousel);
  EXPECT_TRUE(partial.applied);
  EXPECT_EQ(partial.recentBookLayout, 4);
  EXPECT_EQ(partial.finishedBookLayout, kFolioFiveCoverCarousel);

  const auto unrelatedTheme = HomeThemePolicy::migrateLegacyCarouselLayout(
      4, kLiteThemeValue, 1, true, false, 0, false, 0,
      kCarouselThreeCover, kFolioThreeCoverCarousel, kFolioFiveCoverCarousel);
  EXPECT_FALSE(unrelatedTheme.applied);
  EXPECT_EQ(unrelatedTheme.recentBookLayout, 0);
  EXPECT_EQ(unrelatedTheme.finishedBookLayout, 0);
}
