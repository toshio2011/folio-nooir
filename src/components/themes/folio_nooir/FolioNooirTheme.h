#pragma once

#include <cstdint>

#include "components/themes/lyra/LyraTheme.h"

struct FolioLibrarySummary {
  uint32_t total = 0;
  uint32_t reading = 0;
  uint32_t unread = 0;
  uint32_t onHold = 0;
  uint32_t finished = 0;
};

struct FolioShelfLayout {
  int headerTop;
  int headerHeight;
  int contentTop;
  int contentHeight;
  int detailHeight;
  int statsTop;
  int statsHeight;
  int gridTop;
  int gridHeight;
  int gridGap;
  int columns;
  int cardWidth;
  int cardHeight;
};

// Folio-specific presentation lives here; activities retain only navigation,
// storage and book-selection state. Ordinary screens continue to inherit
// Lyra's shared CrossPoint components.
class FolioNooirTheme final : public LyraTheme {
 public:
  static constexpr int BOOKS_PER_PAGE = 8;
  // Keep the cached cover large enough for the featured slot.  The 4x2 grid
  // still caps its displayed height to the existing card geometry.
  static constexpr int COVER_HEIGHT = 220;

  bool usesBookshelfHome() const override { return true; }
  bool usesGraphicalLibrary() const override { return true; }

  FolioShelfLayout shelfLayout(const GfxRenderer& renderer, const ThemeMetrics& metrics) const;
  void drawShelfTabs(const GfxRenderer& renderer, const FolioShelfLayout& layout, uint8_t activeTab) const;
  void drawShelfBattery(const GfxRenderer& renderer, const FolioShelfLayout& layout,
                        const ThemeMetrics& metrics) const;
  void drawLibrarySummary(const GfxRenderer& renderer, int x, int y, int width,
                          const FolioLibrarySummary& summary) const;
  void drawShelfStats(const GfxRenderer& renderer, const FolioShelfLayout& layout, uint32_t lastMinutes,
                      uint32_t middleMinutes, uint16_t finishedCount, bool accumulated) const;
  void drawCoverProgress(const GfxRenderer& renderer, int x, int y, int width, uint8_t percent) const;
  void drawCoverProgressBadge(const GfxRenderer& renderer, int x, int y, int width, int height,
                              uint8_t percent) const;
  void drawPageIndicator(const GfxRenderer& renderer, const FolioShelfLayout& layout, size_t page, size_t pages) const;
};
