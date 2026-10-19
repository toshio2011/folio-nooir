#include <gtest/gtest.h>

#include "src/util/HomeThemePolicy.h"

namespace {
constexpr uint8_t kLiteThemeValue = 5;
constexpr uint8_t kCarouselThreeCover = 0;
constexpr uint8_t kLiteThreeCoverCarousel = 0;
constexpr uint8_t kLiteFiveCoverCarousel = 1;
}  // namespace

TEST(HomeThemePolicy, MigratesPersistedLegacyCarouselLayoutForThemeValueFive) {
  const auto migrated = HomeThemePolicy::migrateLegacyCarouselLayout(
      5, kLiteThemeValue, kCarouselThreeCover, true, false, 0, false, 0,
      kCarouselThreeCover, kLiteThreeCoverCarousel, kLiteFiveCoverCarousel);

  EXPECT_TRUE(migrated.applied);
  EXPECT_EQ(migrated.recentLayout, kLiteThreeCoverCarousel);
  EXPECT_EQ(migrated.finishedLayout, kLiteThreeCoverCarousel);
}

TEST(HomeThemePolicy, DoesNotOverrideExplicitLiteLayoutPreferences) {
  const auto migrated = HomeThemePolicy::migrateLegacyCarouselLayout(
      5, kLiteThemeValue, kCarouselThreeCover, true, true, 3, true, 1,
      kCarouselThreeCover, kLiteThreeCoverCarousel, kLiteFiveCoverCarousel);

  EXPECT_FALSE(migrated.applied);
  EXPECT_EQ(migrated.recentLayout, 3);
  EXPECT_EQ(migrated.finishedLayout, 1);
}

TEST(HomeThemePolicy, MigratesOnlyMissingLayoutAndKeepsLegacyThemeIdsIsolated) {
  const auto partial = HomeThemePolicy::migrateLegacyCarouselLayout(
      5, kLiteThemeValue, 1, true, true, 2, false, 0,
      kCarouselThreeCover, kLiteThreeCoverCarousel, kLiteFiveCoverCarousel);
  EXPECT_TRUE(partial.applied);
  EXPECT_EQ(partial.recentLayout, 2);
  EXPECT_EQ(partial.finishedLayout, kLiteFiveCoverCarousel);

  const auto unrelatedTheme = HomeThemePolicy::migrateLegacyCarouselLayout(
      4, kLiteThemeValue, 1, true, false, 0, false, 0,
      kCarouselThreeCover, kLiteThreeCoverCarousel, kLiteFiveCoverCarousel);
  EXPECT_FALSE(unrelatedTheme.applied);
  EXPECT_EQ(unrelatedTheme.recentLayout, 0);
  EXPECT_EQ(unrelatedTheme.finishedLayout, 0);
}
