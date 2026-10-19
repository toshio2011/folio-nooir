#pragma once

#include <cstdint>

namespace HomeThemePolicy {

struct LegacyCarouselLayoutMigration {
  uint8_t recentLayout;
  uint8_t finishedLayout;
  bool applied;
};

// Theme value 5 is retained for old settings files. Its legacy Carousel
// selector initializes only Lite's new Recent/Finished selectors; the main
// Folio settings are never part of this migration.
constexpr LegacyCarouselLayoutMigration migrateLegacyCarouselLayout(
    const uint8_t uiTheme, const uint8_t liteThemeValue, const uint8_t carouselLayout,
    const bool hasCarouselLayout, const bool hasLiteRecentLayout, const uint8_t liteRecentLayout,
    const bool hasLiteFinishedLayout, const uint8_t liteFinishedLayout, const uint8_t threeCoverValue,
    const uint8_t liteThreeCoverLayout, const uint8_t liteFiveCoverLayout) {
  if (uiTheme != liteThemeValue || !hasCarouselLayout || (hasLiteRecentLayout && hasLiteFinishedLayout)) {
    return {liteRecentLayout, liteFinishedLayout, false};
  }

  const uint8_t migratedLayout = carouselLayout == threeCoverValue ? liteThreeCoverLayout : liteFiveCoverLayout;
  return {hasLiteRecentLayout ? liteRecentLayout : migratedLayout,
          hasLiteFinishedLayout ? liteFinishedLayout : migratedLayout, true};
}

}  // namespace HomeThemePolicy
