#pragma once

#include <cstdint>

namespace HomeThemePolicy {

struct LegacyCarouselLayoutMigration {
  uint8_t recentBookLayout;
  uint8_t finishedBookLayout;
  bool applied;
};

// Theme value 5 is retained for old settings files. When those files predate
// the independent Recent/Finished selectors, map their Carousel choice once
// and leave any explicitly stored Folio layout untouched.
constexpr LegacyCarouselLayoutMigration migrateLegacyCarouselLayout(
    const uint8_t uiTheme, const uint8_t liteThemeValue, const uint8_t carouselLayout,
    const bool hasCarouselLayout, const bool hasRecentLayout, const uint8_t recentLayout,
    const bool hasFinishedLayout, const uint8_t finishedLayout, const uint8_t threeCoverValue,
    const uint8_t threeCoverLayout, const uint8_t fiveCoverLayout) {
  if (uiTheme != liteThemeValue || !hasCarouselLayout || (hasRecentLayout && hasFinishedLayout)) {
    return {recentLayout, finishedLayout, false};
  }

  const uint8_t migratedLayout = carouselLayout == threeCoverValue ? threeCoverLayout : fiveCoverLayout;
  return {hasRecentLayout ? recentLayout : migratedLayout,
          hasFinishedLayout ? finishedLayout : migratedLayout, true};
}

}  // namespace HomeThemePolicy
