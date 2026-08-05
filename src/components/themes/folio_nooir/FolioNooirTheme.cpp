#include "FolioNooirTheme.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "fontIds.h"

FolioShelfLayout FolioNooirTheme::shelfLayout(const GfxRenderer& renderer, const ThemeMetrics& metrics) const {
  FolioShelfLayout out{};
  out.headerTop = metrics.topPadding + 8;
  out.headerHeight = 52;
  out.contentTop = out.headerTop + out.headerHeight + 2;
  out.contentHeight = renderer.getScreenHeight() - out.contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  out.detailHeight = std::min(238, std::max(150, out.contentHeight * 38 / 100));
  // Keep the statistics strip close to the button hints so the page label has
  // a dedicated gap below the cover grid instead of sharing its last row.
  out.statsHeight = std::min(52, std::max(40, out.contentHeight / 10 - 10));
  out.statsTop = out.contentTop + out.contentHeight - out.statsHeight;
  out.gridTop = out.contentTop + out.detailHeight;
  out.gridHeight = out.statsTop - out.gridTop;
  out.gridGap = 6;
  out.columns = 4;
  out.cardWidth = (renderer.getScreenWidth() - out.gridGap * (out.columns + 1)) / out.columns;
  out.cardHeight = out.gridHeight / 2;
  return out;
}

void FolioNooirTheme::drawShelfTabs(const GfxRenderer& renderer, const FolioShelfLayout& layout,
                                    const uint8_t activeTab) const {
  const int pageWidth = renderer.getScreenWidth();
  renderer.fillRect(0, layout.headerTop - 8, pageWidth, layout.headerHeight + 10, false);
  const char* tabs[] = {tr(STR_LIBRARY), tr(STR_RECENT), tr(STR_FINISHED)};
  for (int i = 0; i < 3; ++i) {
    const int left = pageWidth * i / 3;
    const int right = pageWidth * (i + 1) / 3;
    const int width = renderer.getTextWidth(UI_10_FONT_ID, tabs[i]);
    renderer.drawText(UI_10_FONT_ID, left + (right - left - width) / 2, layout.headerTop + 8, tabs[i], true,
                      i == activeTab ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    if (i == activeTab) renderer.fillRect(left + 12, layout.headerTop + layout.headerHeight - 3, right - left - 24, 3);
  }
  renderer.drawLine(0, layout.headerTop + layout.headerHeight, pageWidth - 1,
                    layout.headerTop + layout.headerHeight);
}

void FolioNooirTheme::drawShelfStats(const GfxRenderer& renderer, const FolioShelfLayout& layout,
                                     const uint32_t lastMinutes, const uint32_t todayMinutes,
                                     const uint16_t finishedCount) const {
  const int pageWidth = renderer.getScreenWidth();
  renderer.fillRect(0, layout.statsTop, pageWidth, layout.statsHeight, false);
  renderer.drawLine(0, layout.statsTop, pageWidth - 1, layout.statsTop);
  const char* labels[] = {tr(STR_LAST_READ), tr(STR_TODAY), tr(STR_FINISHED)};
  char values[3][24];
  snprintf(values[0], sizeof(values[0]), "%lu %s", static_cast<unsigned long>(lastMinutes), tr(STR_MIN_SHORT));
  snprintf(values[1], sizeof(values[1]), "%lu %s", static_cast<unsigned long>(todayMinutes), tr(STR_MIN_SHORT));
  snprintf(values[2], sizeof(values[2]), "%u", finishedCount);
  for (int i = 0; i < 3; ++i) {
    const int left = pageWidth * i / 3;
    const int right = pageWidth * (i + 1) / 3;
    if (i > 0) renderer.drawLine(left, layout.statsTop + 7, left, layout.statsTop + layout.statsHeight - 7);
    const int labelWidth = renderer.getTextWidth(SMALL_FONT_ID, labels[i]);
    const int valueWidth = renderer.getTextWidth(UI_10_FONT_ID, values[i]);
    renderer.drawText(SMALL_FONT_ID, left + (right - left - labelWidth) / 2, layout.statsTop + 8, labels[i]);
    renderer.drawText(UI_10_FONT_ID, left + (right - left - valueWidth) / 2, layout.statsTop + 29, values[i], true);
  }
}

void FolioNooirTheme::drawCoverProgress(const GfxRenderer& renderer, const int x, const int y, const int width,
                                        const uint8_t percent) const {
  char progress[16];
  snprintf(progress, sizeof(progress), "%u%%", percent);
  const int progressWidth = renderer.getTextWidth(SMALL_FONT_ID, progress);
  renderer.drawText(SMALL_FONT_ID, x + (width - progressWidth) / 2, y, progress);
}

void FolioNooirTheme::drawPageIndicator(const GfxRenderer& renderer, const FolioShelfLayout& layout,
                                        const size_t page, const size_t pages) const {
  if (pages <= 1) return;
  char text[32];
  snprintf(text, sizeof(text), "< %s %u/%u >", tr(STR_PAGE), static_cast<unsigned>(page),
           static_cast<unsigned>(pages));
  const int width = renderer.getTextWidth(SMALL_FONT_ID, text) + 12;
  const int x = (renderer.getScreenWidth() - width) / 2;
  renderer.fillRect(x, layout.statsTop - 18, width, 18, false);
  renderer.drawText(SMALL_FONT_ID, x + 6, layout.statsTop - 16, text);
}
